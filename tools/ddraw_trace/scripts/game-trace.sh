#!/usr/bin/env bash
# Capture a normalized ddraw trace of the ORIGINAL renderer plugin from startup to the main menu, in the real game.
#
#   game-trace.sh <path-to-proxy-ddraw.dll> [--gate]
#
# One run: take the game-run lock, check the game is down, snapshot the game directory, launch through the
# unmodified 00xJS launcher (the proxy is swapped in by a Wine wrapper at the moment the game is started, because
# the launcher rewrites ddraw.dll on every launch), wait for the capture cap, stop the prefix with its own
# wineserver, restore the game directory byte for byte and release the lock.  --gate does two runs and diffs them.
# Design and safety argument: README.md ("The ddraw chain").
set -u -o pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
PROXY="$(cd "$(dirname "${1:?usage: game-trace.sh <proxy ddraw.dll> [--gate]}")" && pwd)/$(basename "$1")"
MODE="${2:-single}"

RE_DIR="${RE_DIR:-$HOME/Games/EmpireEarth-re}"
SUPPORT="$HOME/Library/Application Support/EmpireEarthMac"
PREFIX="$SUPPORT/wineprefix"
WINE_ROOT="${WINE_ROOT:-$SUPPORT/runtime/Wine Stable.app/Contents/Resources/wine}"
LAUNCHER_DIR="${LAUNCHER_DIR:-$HOME/Games/tools/ee-macos}"
GAME_DIR="$PREFIX/drive_c/GOG Games/Empire Earth Gold Edition/Empire Earth"
LOCK="$RE_DIR/locks/game-run"
OUT="$HERE/build/game-trace"
PRESENTS="${DDT_PRESENTS:-60}"      # the menu changes by wall clock after ~110 frames (README, "Determinism")
LOCK_WAIT="${DDT_LOCK_WAIT:-1800}"       # seconds to wait for another agent's game run
CAPTURE_WAIT="${DDT_CAPTURE_WAIT:-420}"  # seconds from launch to the capture cap
EXIT_WAIT=90                             # seconds for the game to disappear after wineserver -k

mkdir -p "$OUT" "$RE_DIR/locks"

# Keep the display awake for the length of this script.  A run once stalled for four minutes in Present while the
# idle display slept, and the game quit when the display woke; caffeinate ends together with this script (-w).
if command -v caffeinate >/dev/null 2>&1; then
  caffeinate -d -i -w $$ >/dev/null 2>&1 &
fi

# shellcheck source=lib.sh
. "$HERE/scripts/lib.sh"

cleanup() {
  local rc=$?
  trap - EXIT INT TERM
  if [ "$launched" -eq 1 ] && [ -n "$(game_pids)" ]; then
    log "cleanup: stopping the prefix"
    stop_game
  fi
  restore_game_dir || rc=6
  release_lock
  exit "$rc"
}
trap cleanup EXIT INT TERM

one_run() {
  local tag="$1" raw="$OUT/run.raw" win_raw wrapper="$OUT/wrap/bin"

  acquire_lock || return 2
  if pgrep -f 'C:.GOG Games.*Empire Earth.exe' >/dev/null; then
    log "FAIL: the game is running (pgrep matched); refusing to touch the game directory"
    return 3
  fi
  [ -d "$GAME_DIR" ] || { log "FAIL: no game directory at $GAME_DIR"; return 3; }

  log "recording the pre-run manifest of $GAME_DIR"
  manifest "$GAME_DIR" >"$OUT/manifest.before"
  snapshot="$OUT/snapshot"
  rm -rf "$snapshot"
  cp -cR "$GAME_DIR" "$snapshot" 2>/dev/null || cp -R "$GAME_DIR" "$snapshot"

  # Wine wrapper: the launcher honours EMPIRE_EARTH_WINE.  It forwards everything to the real Wine and, right before the
  # game is started (`wine start /unix <exe>`), swaps the proxy in.  By then apply-launch-patches.sh has finished
  # writing ddraw.dll, so nothing overwrites the proxy afterwards.
  win_raw="Z:$(printf '%s' "$raw" | tr '/' '\\')"
  rm -rf "$OUT/wrap" && mkdir -p "$wrapper"
  rm -f "$raw" "$raw.done" "$raw.times" "$raw.callers"
  ln -s "$WINE_ROOT/bin/wineserver" "$wrapper/wineserver"
  cat >"$wrapper/wine" <<WRAP
#!/bin/bash
if [ "\${1:-}" = start ] && [ "\${2:-}" = /unix ]; then
  '$HERE/scripts/swap-in.sh' '$GAME_DIR' '$PROXY' '$win_raw' '$PRESENTS' || exit 97
fi
exec '$WINE_ROOT/bin/wine' "\$@"
WRAP
  chmod +x "$wrapper/wine"

  swapped=1
  launched=1
  log "launching the game through the launcher (presents cap: $PRESENTS)"
  ( cd "$LAUNCHER_DIR" && EMPIRE_EARTH_WINE="$wrapper/wine" \
      EE_BIG_ARMIES=0 EE_BIG_CROWDS=0 EE_X87T=0 EE_SSE_MATH=0 EE_MEMFILE_DOUBLE=0 EE_POINTLIST_GUARD=0 EE_ANIMATION_SMOOTHING=0 \
      ./scripts/launch.sh ) >"$OUT/launch.log" 2>&1
  local rc=$?
  if [ "$rc" -ne 0 ]; then
    log "FAIL: launch.sh exited with $rc (see $OUT/launch.log)"
    tail -5 "$OUT/launch.log" | sed 's/^/    /' >&2
    return 4
  fi
  log "launcher reports the first frames are up; waiting for the capture cap"

  local waited=0
  while [ ! -f "$raw.done" ]; do
    if [ -z "$(game_pids)" ]; then
      log "FAIL: the game exited before the capture cap"
      return 5
    fi
    if [ "$waited" -ge "$CAPTURE_WAIT" ]; then
      log "FAIL: capture cap not reached after ${CAPTURE_WAIT}s"
      return 5
    fi
    sleep 2
    waited=$((waited + 2))
  done
  log "capture complete ($(wc -l <"$raw" | tr -d ' ') lines); stopping the prefix"
  stop_game || return 7
  launched=0
  cp "$raw" "$OUT/trace.$tag.txt"
  [ -f "$raw.times" ] && cp "$raw.times" "$OUT/trace.$tag.times"
  [ -f "$raw.callers" ] && cp "$raw.callers" "$OUT/trace.$tag.callers"
  log "trace saved: $OUT/trace.$tag.txt"
  restore_game_dir || return 6
  release_lock
  return 0
}

# Two traces are equal if they are identical, or identical after the cursor-sprite normalization (scripts/normalize.py).
compare_traces() {
  local a="$1" b="$2" na nb
  if cmp -s "$a" "$b"; then
    log "traces identical ($(wc -l <"$a" | tr -d ' ') lines)"
    return 0
  fi
  na="$(mktemp "$OUT/norm.XXXXXX")"
  nb="$(mktemp "$OUT/norm.XXXXXX")"
  python3 -I "$HERE/scripts/normalize.py" <"$a" >"$na"
  python3 -I "$HERE/scripts/normalize.py" <"$b" >"$nb"
  if cmp -s "$na" "$nb"; then
    log "traces differ raw but are identical after removing the cursor-sprite blits ($(wc -l <"$na" | tr -d ' ') lines left; raw $(wc -l <"$a" | tr -d ' ') and $(wc -l <"$b" | tr -d ' '))"
    rm -f "$na" "$nb"
    return 0
  fi
  diff -u "$na" "$nb" >"$OUT/diff.normalized.txt"
  log "traces DIFFER, also after normalization: $(grep -c '^[-+][^-+]' "$OUT/diff.normalized.txt") changed lines, see $OUT/diff.normalized.txt"
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
    log "GATE PASS"
  else
    log "GATE DIFFERENCES"
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
