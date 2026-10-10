#!/usr/bin/env bash
# Capture a normalized ddraw trace of the ORIGINAL renderer plugin from startup to the main menu, in the real game.
#
#   game-trace.sh [--test-config <file>] <path-to-proxy-ddraw.dll> [--gate]
#
# One run: take the game-run lock, refuse if anything of an earlier run is left over or if the game (or Art of Conquest) is
# running, snapshot the game directory (verified against its sha256 manifest), launch through the unmodified 00xJS launcher
# with a Wine wrapper that swaps the proxy in at the moment the game is started (the launcher rewrites ddraw.dll on every
# launch), wait for the capture cap, then stop the prefix with its own wineserver, restore the game directory byte for byte
# and release the lock.  --gate does two runs and compares them.
#
# The protocol, the lock, the snapshot and the signal-safe teardown are the shared library tools/game_run/lib.sh; the design
# and the safety argument are in README.md ("The ddraw chain").
set -u -o pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
# --test-config <file>: the fake world of tests/game_run (the only way anything but the real installation is used)
TEST_CONFIG=""
if [ "${1:-}" = --test-config ]; then
  TEST_CONFIG="${2:?usage: game-trace.sh [--test-config <file>] <proxy ddraw.dll> [--gate]}"
  shift 2
fi
PROXY="$(cd "$(dirname "${1:?usage: game-trace.sh [--test-config <file>] <proxy ddraw.dll> [--gate]}")" && pwd)/$(basename "$1")"
MODE="${2:-single}"

# shellcheck source=../../game_run/lib.sh
. "$HERE/../game_run/lib.sh"
GR_TOOL=game-trace

OUT="$HERE/build/game-trace"
PRESENTS="${DDT_PRESENTS:-60}" # the menu changes by wall clock after ~110 frames (README, "Determinism")
CAPTURE_WAIT="${DDT_CAPTURE_WAIT:-420}"
LAUNCH_LIMIT=900
gr_test_hook "$TEST_CONFIG"
gr_sanitize_env

mkdir -p "$OUT"
GR_LOG_FILE="$OUT/game-trace.log"

# Keep the display awake for the length of this script.  A run once stalled for four minutes in Present while the idle
# display slept, and the game quit when the display woke; caffeinate ends together with this script (-w).
if command -v caffeinate >/dev/null 2>&1; then
  caffeinate -d -i -w $$ >/dev/null 2>&1 &
fi

cleanup() { gr_teardown; }
gr_install_traps cleanup

# The launcher's start of the game is `wine start /unix <exe>`; the wrapper runs swap-in.sh just before it.
one_run() {
  local tag="$1" raw="$OUT/run.raw" win_raw blocking rc

  gr_lock_acquire || return 2
  gr_check_leftovers ddraw_next.dll ddraw_trace.cfg ddraw_next.dll.tmp ddraw.dll.tmp ddraw_trace.cfg.tmp || { gr_teardown; return 3; }
  gr_assert_config || { gr_teardown; return 3; }
  blocking="$(gr_blocking_pids | tr '\n' ' ')"
  if [ -n "$blocking" ]; then
    gr_log "FAIL: the game, Art of Conquest or another program of the GOG install is running (pid $blocking); refusing to touch the game directory"
    gr_teardown
    return 3
  fi
  gr_state_begin && gr_snapshot_take || { gr_teardown; return 3; }

  win_raw="Z:$(printf '%s' "$raw" | tr '/' '\\')"
  rm -f "$raw" "$raw.done" "$raw.times" "$raw.callers"
  gr_make_wine_wrapper "$OUT/wrap" "$HERE/scripts/swap-in.sh" "$GR_GAME" "$PROXY" "$win_raw" "$PRESENTS" || return 3

  gr_launcher_log_mark
  gr_log "launching the game through the launcher (presents cap: $PRESENTS)"
  gr_run_launcher "$LAUNCH_LIMIT" "$OUT/launch.log" \
    bash -c 'cd "$1" && shift && exec "$@"' _ "$GR_LAUNCHER_DIR" \
    env EMPIRE_EARTH_WINE="$GR_WRAPPED_WINE" \
    EE_BIG_ARMIES=0 EE_BIG_CROWDS=0 EE_X87T=0 EE_SSE_MATH=0 EE_MEMFILE_DOUBLE=0 EE_POINTLIST_GUARD=0 EE_ANIMATION_SMOOTHING=0 \
    "$GR_LAUNCHER_DIR/scripts/launch.sh"
  rc=$?
  if [ "$rc" -ne 0 ]; then
    gr_log "FAIL: launch.sh exited with $rc (see $OUT/launch.log)"
    tail -5 "$OUT/launch.log" | sed 's/^/    /' >&2
    return 4
  fi
  gr_check_launcher_log || return 4
  gr_log "launcher reports the first frames are up; waiting for the capture cap"

  local waited=0
  while [ ! -f "$raw.done" ]; do
    if ! gr_game_running; then
      gr_log "FAIL: the game exited before the capture cap"
      return 5
    fi
    if [ "$waited" -ge "$CAPTURE_WAIT" ]; then
      gr_log "FAIL: capture cap not reached after ${CAPTURE_WAIT}s"
      return 5
    fi
    sleep 1
    waited=$((waited + 1))
  done
  gr_log "capture complete ($(wc -l <"$raw" | tr -d ' ') lines); stopping the prefix"
  gr_stop_game || return 7
  cp "$raw" "$OUT/trace.$tag.txt"
  [ -f "$raw.times" ] && cp "$raw.times" "$OUT/trace.$tag.times"
  [ -f "$raw.callers" ] && cp "$raw.callers" "$OUT/trace.$tag.callers"
  gr_log "trace saved: $OUT/trace.$tag.txt"
  gr_teardown || return 6
  return 0
}

# Two traces are equal if they are identical, or identical after the cursor-sprite normalization (scripts/normalize.py).
compare_traces() {
  local a="$1" b="$2" na nb
  if cmp -s "$a" "$b"; then
    gr_log "traces identical ($(wc -l <"$a" | tr -d ' ') lines)"
    return 0
  fi
  na="$(mktemp "$OUT/norm.XXXXXX")"
  nb="$(mktemp "$OUT/norm.XXXXXX")"
  python3 -I "$HERE/scripts/normalize.py" <"$a" >"$na"
  python3 -I "$HERE/scripts/normalize.py" <"$b" >"$nb"
  if cmp -s "$na" "$nb"; then
    gr_log "traces differ raw but are identical after removing the cursor-sprite blits ($(wc -l <"$na" | tr -d ' ') lines left; raw $(wc -l <"$a" | tr -d ' ') and $(wc -l <"$b" | tr -d ' '))"
    rm -f "$na" "$nb"
    return 0
  fi
  diff -u "$na" "$nb" >"$OUT/diff.normalized.txt"
  gr_log "traces DIFFER, also after normalization: $(grep -c '^[-+][^-+]' "$OUT/diff.normalized.txt") changed lines, see $OUT/diff.normalized.txt"
  rm -f "$na" "$nb"
  return 1
}

stamp="$(date '+%Y%m%d-%H%M%S')"
if [ "$MODE" = "--gate" ]; then
  one_run "A.$stamp" || exit $?
  # give the filesystem and Wine a moment, then the second run
  sleep 5
  one_run "B.$stamp" || exit $?
  if compare_traces "$OUT/trace.A.$stamp.txt" "$OUT/trace.B.$stamp.txt"; then
    gr_log "GATE PASS"
  else
    gr_log "GATE DIFFERENCES"
    exit 1
  fi
else
  one_run "$stamp" || exit $?
  [ -f "$OUT/last.txt" ] && mv -f "$OUT/last.txt" "$OUT/prev.txt"
  cp "$OUT/trace.$stamp.txt" "$OUT/last.txt"
  if [ -f "$OUT/prev.txt" ]; then
    compare_traces "$OUT/prev.txt" "$OUT/last.txt" || exit 1
  fi
fi
