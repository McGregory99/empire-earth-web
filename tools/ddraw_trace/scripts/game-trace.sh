#!/usr/bin/env bash
# Capture a normalized ddraw trace of the ORIGINAL renderer plugin from startup to the main menu, in the real game.
#
#   game-trace.sh [--test-config <file>] [--scenario <file>] [--data] [--out-dir <dir>] <path-to-proxy-ddraw.dll> [--gate]
#
# One run: take the game-run lock, refuse if anything of an earlier run is left over or if the game (or Art of Conquest) is
# running, snapshot the game directory (verified against its sha256 manifest), launch through the unmodified 00xJS launcher
# with a Wine wrapper that swaps the proxy in at the moment the game is started (the launcher rewrites ddraw.dll on every
# launch), wait for the capture cap, then stop the prefix with its own wineserver, restore the game directory byte for byte
# and release the lock.  --gate does two runs and compares them.
#
# --scenario <file> (tests/ddraw_trace/scenarios/*.env) runs a scenario instead of the startup: the game is started with the
# scenario's command line (-loadgame "<save>") by tools/game_run/scripts/ee-start.exe, which the Wine wrapper starts in place
# of the game; the save (kept outside the repository, checked against its sha256) is in the game's Saved Games only for the
# run; the scenario's determinism switches (EE_SIMHASH, EE_SIM_FIXED_MS) reach the game; when the capture is complete the
# game is closed with WM_CLOSE (ee-close.exe) before the prefix is stopped.
#
# --data also captures the data the trace only hashes (README, "Data file"): the proxy writes <trace>.data (DDT_DATA is the raw
# trace path; DDT_DATA_MAX from the caller's environment is passed on) and a run keeps it as trace.<tag>.data.  --out-dir <dir>
# puts every output (traces, data, logs) in <dir> instead of tools/ddraw_trace/build: data holds the game's content, so it
# belongs outside the repository.
#
# The protocol, the lock, the snapshot and the signal-safe teardown are the shared library tools/game_run/lib.sh; the design
# and the safety argument are in README.md ("The ddraw chain").
set -u -o pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
USAGE="usage: game-trace.sh [--test-config <file>] [--scenario <file>] [--data] [--out-dir <dir>] <proxy ddraw.dll> [--gate]"
WITH_DATA=0
OUT_DIR_OVERRIDE=""
DATA_MAX="${DDT_DATA_MAX:-}" # read before gr_sanitize_env
# --test-config <file>: the fake world of tests/game_run (the only way anything but the real installation is used)
TEST_CONFIG=""
SCENARIO_FILE=""
while [ $# -gt 0 ]; do
  case "$1" in
    --test-config) TEST_CONFIG="${2:?$USAGE}"; shift 2 ;;
    --scenario) SCENARIO_FILE="${2:?$USAGE}"; shift 2 ;;
    --data) WITH_DATA=1; shift ;;
    --out-dir) OUT_DIR_OVERRIDE="${2:?$USAGE}"; shift 2 ;;
    *) break ;;
  esac
done
PROXY="$(cd "$(dirname "${1:?$USAGE}")" && pwd)/$(basename "$1")"
MODE="${2:-single}"

# shellcheck source=../../game_run/lib.sh
. "$HERE/../game_run/lib.sh"
GR_TOOL=game-trace

OUT="$HERE/build/game-trace"
PRESENTS="${DDT_PRESENTS:-60}" # the menu changes by wall clock after ~110 frames (README, "Determinism")
CAPTURE_WAIT="${DDT_CAPTURE_WAIT:-420}"
LAUNCH_LIMIT=900
# the scenario helpers: built from tools/game_run/scripts unless a test names fake ones
START_BUILD=1
START_CC=i686-w64-mingw32-gcc
START_EXE=""
CLOSE_BUILD=1
CLOSE_EXE=""
CLOSE_GRACE=25  # seconds to wait for the game to leave after WM_CLOSE before WM_QUIT is posted
EXIT_WAIT=60    # seconds to wait for the game to leave after WM_QUIT
DESKTOP_NAME=EmpireEarth
GAME_EXE_NAME="Empire Earth.exe"
SCENARIO_DIR="$HOME/Games/EmpireEarth-re/scenarios" # the saves the scenarios load; outside the repository, never committed
SCN_NAME="" SCN_ARGS="" SCN_SAVE="" SCN_SAVE_SHA256="" SCN_PRESENTS="" SCN_ENV=""
SCN_NORMALIZE="" # "structure": two captures must be identical in normalize.py's --structure form (see there for what it leaves out)
SCN_KEY_WINDOW="" # class of the game's window: ee-key.exe gives it the focus (or posts the keys to it) before pressing
SCN_KEYS="" # scancodes (hex) pressed once the simulation runs; with keys, the proxy is armed only after the simulation stopped
SCN_SHOTS=0 # screenshots of the paused game taken after the capture window (reference frames for the viewer); 0 = none
SCN_SHOT_GAP=4 # seconds between them
SHOT_CMD=screencapture # read-only: it only reads the display; -x keeps it silent
KEY_BUILD=1
KEY_EXE=""
LOOP_WAIT=180        # seconds to wait for the game loop (EE_SIMHASH's "tick 0" line) after the launcher returns
PRE_KEY_DELAY=9      # seconds between the first tick line and the keys: the form must be taking input (two real runs lost F3 at 2-4 s)
KEY_TIMEOUT=40       # seconds the key helper may take (it presses a key and returns in about a second); then it is killed
LIVE_WAIT=60         # seconds to wait, after the first tick line, for LIVE_TICKS more of them
LIVE_TICKS=3         # tick lines (one per ~1.8 s) that must follow the first one before the key is pressed: the loop is running
SETTLE_SECONDS=6     # seconds between the keys and the check that the simulation stopped
PAUSE_WINDOW=5       # seconds without a new tick line that prove it stopped (a tick line comes every 50 ticks, ~1.8 s)
if [ -n "$SCENARIO_FILE" ]; then
  [ -f "$SCENARIO_FILE" ] || { echo "no scenario file $SCENARIO_FILE" >&2; exit 2; }
  # shellcheck source=/dev/null
  . "$SCENARIO_FILE"
  [ -n "$SCN_PRESENTS" ] && PRESENTS="${DDT_PRESENTS:-$SCN_PRESENTS}"
  OUT="$HERE/build/game-trace-$SCN_NAME" # its own folder: last.txt / prev.txt must never compare it with the startup trace
fi
gr_test_hook "$TEST_CONFIG"
[ -z "$OUT_DIR_OVERRIDE" ] || { mkdir -p "$OUT_DIR_OVERRIDE" && OUT="$(cd "$OUT_DIR_OVERRIDE" && pwd)"; } || { echo "cannot use --out-dir $OUT_DIR_OVERRIDE" >&2; exit 2; }
gr_sanitize_env
[ -n "$START_EXE" ] || START_EXE="$OUT/bin/ee-start.exe"
[ -n "$CLOSE_EXE" ] || CLOSE_EXE="$OUT/bin/ee-close.exe"
[ -n "$KEY_EXE" ] || KEY_EXE="$OUT/bin/ee-key.exe"

mkdir -p "$OUT"
GR_LOG_FILE="$OUT/game-trace.log"

# Keep the display awake for the length of this script.  A run once stalled for four minutes in Present while the idle
# display slept, and the game quit when the display woke; caffeinate ends together with this script (-w).
if command -v caffeinate >/dev/null 2>&1; then
  caffeinate -d -i -w $$ >/dev/null 2>&1 &
fi

cleanup() { gr_teardown; }
gr_install_traps cleanup

# ---- scenarios ---------------------------------------------------------------------------------------------------------------------

# Before the lock is taken and anything is touched: the save is there and is the one the scenario was recorded with.
scenario_check_save() {
  local save="$SCENARIO_DIR/$SCN_SAVE" got
  [ -f "$save" ] || { gr_log "FAIL: the scenario's save is not there: $save (it is user data, kept outside the repository)"; return 3; }
  got="$(shasum -a 256 "$save" | cut -c1-64)"
  if [ "$got" != "$SCN_SAVE_SHA256" ]; then
    gr_log "FAIL: $save has sha256 $got, the scenario was recorded with $SCN_SAVE_SHA256"
    return 3
  fi
  return 0
}

# Build ee-start.exe / ee-close.exe from tools/game_run/scripts (the same source the drivers use), unless a test names fakes.
scenario_build_helpers() {
  local src="$HERE/../game_run/scripts" cc_flags="-mcrtdll=msvcrt-os -O2 -Wall -D_WIN32_WINNT=0x0600"
  mkdir -p "$OUT/bin"
  if [ "$START_BUILD" = 1 ]; then
    # shellcheck disable=SC2086
    "$START_CC" $cc_flags -o "$START_EXE" "$src/ee-start.c" 2>"$OUT/ee-start.build.log" || { gr_log "FAIL: cannot build ee-start.exe (see $OUT/ee-start.build.log)"; return 1; }
  fi
  if [ "$CLOSE_BUILD" = 1 ]; then
    # shellcheck disable=SC2086
    "$START_CC" $cc_flags -o "$CLOSE_EXE" "$src/ee-close.c" -luser32 2>"$OUT/ee-close.build.log" || { gr_log "FAIL: cannot build ee-close.exe (see $OUT/ee-close.build.log)"; return 1; }
  fi
  if [ -n "$SCN_KEYS" ] && [ "$KEY_BUILD" = 1 ]; then
    # shellcheck disable=SC2086
    "$START_CC" $cc_flags -o "$KEY_EXE" "$src/ee-key.c" -luser32 2>"$OUT/ee-key.build.log" || { gr_log "FAIL: cannot build ee-key.exe (see $OUT/ee-key.build.log)"; return 1; }
  fi
  [ -z "$SCN_KEYS" ] || [ -f "$KEY_EXE" ] || { gr_log "FAIL: the key helper is missing ($KEY_EXE)"; return 1; }
  [ -f "$START_EXE" ] && [ -f "$CLOSE_EXE" ] || { gr_log "FAIL: the scenario helpers are missing ($START_EXE, $CLOSE_EXE)"; return 1; }
}

# The simulation's tick lines (EE_SIMHASH) in the game's ee-version.log.
tick_lines() { grep -c 'simhash: tick' "$GR_GAME/ee-version.log" 2>/dev/null || true; }

# The last tick line (its number) the game wrote, for the log.
last_tick() { grep 'simhash: tick' "$GR_GAME/ee-version.log" 2>/dev/null | tail -1 | sed 's/.*simhash: tick \([0-9]*\).*/\1/'; }

# Press the scenario's keys, wait, and say whether the simulation has stopped: no new tick line in PAUSE_WINDOW seconds.
press_and_check() {
  local before after
  # ee-key.exe lists the windows of its desktop and the foreground one before it presses: a run that loses its keys says where they went.
  # It runs in the background with a limit: a Wine process that hung after its last line once held the whole run for ten minutes.
  local kpid waited=0 krc
  # shellcheck disable=SC2086
  WINEPREFIX="$GR_PREFIX" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" "$GR_WINE_ROOT/bin/wine" "$KEY_EXE" ${SCN_KEY_WINDOW:+--window-class "$SCN_KEY_WINDOW"} $SCN_KEYS >>"$OUT/ee-key.out" 2>&1 &
  kpid=$!
  while kill -0 "$kpid" 2>/dev/null; do
    if [ "$waited" -ge "$KEY_TIMEOUT" ]; then
      pkill -KILL -P "$kpid" 2>/dev/null; kill -KILL "$kpid" 2>/dev/null
      pkill -KILL -f "$KEY_EXE" 2>/dev/null
      wait "$kpid" 2>/dev/null
      gr_log "the key helper did not finish in ${KEY_TIMEOUT}s: killed (see $OUT/ee-key.out)"
      return 3
    fi
    sleep 1
    waited=$((waited + 1))
  done
  wait "$kpid"; krc=$?
  [ "$krc" -eq 0 ] || { gr_log "FAIL: the key helper failed (see $OUT/ee-key.out)"; return 2; }
  sleep "$SETTLE_SECONDS"
  before="$(tick_lines)"
  sleep "$PAUSE_WINDOW"
  after="$(tick_lines)"
  [ "$after" = "$before" ] && { gr_log "the simulation stopped ($after tick lines, last tick $(last_tick), none new in ${PAUSE_WINDOW}s)"; return 0; }
  gr_log "the simulation is still running ${SETTLE_SECONDS}s after the keys ($before -> $after tick lines in ${PAUSE_WINDOW}s, last tick $(last_tick))"
  return 1
}

# With keys: wait for the game loop and for the form to take input, press the keys, prove from the tick lines that the simulation
# stopped (once more if it did not: the pause key may not have taken), then arm the proxy by creating its arm file.  Returns
# non-zero (and says why) if the loop never starts or the simulation keeps running.
scenario_pause_and_arm() {
  local arm="$1" waited=0 rc
  until [ "$(tick_lines)" -gt 0 ]; do
    if [ "$waited" -ge "$LOOP_WAIT" ]; then
      gr_log "FAIL: the game loop never started (no simhash tick line in ee-version.log after ${LOOP_WAIT}s)"
      return 1
    fi
    gr_game_running || { gr_log "FAIL: the game exited before its game loop started"; return 1; }
    sleep 1
    waited=$((waited + 1))
  done
  local first_at base lw=0
  first_at=$(date +%s)
  base="$(tick_lines)"
  until [ "$(tick_lines)" -ge $((base + LIVE_TICKS)) ]; do
    if [ "$lw" -ge "$LIVE_WAIT" ]; then
      gr_log "FAIL: the game loop never became live (fewer than ${LIVE_TICKS} tick lines after the first one in ${LIVE_WAIT}s)"
      return 1
    fi
    gr_game_running || { gr_log "FAIL: the game exited before its game loop became live"; return 1; }
    sleep 1
    lw=$((lw + 1))
  done
  gr_log "the game loop is live (${LIVE_TICKS} more tick lines after the first, $lw s; last tick $(last_tick))"
  gr_log "the game loop runs (first tick line after ${waited}s); at least ${PRE_KEY_DELAY}s after it, pressing scancode(s) $SCN_KEYS"
  local rest=$((PRE_KEY_DELAY - ($(date +%s) - first_at)))
  [ "$rest" -le 0 ] || sleep "$rest"
  press_and_check; rc=$?
  if [ "$rc" -eq 1 ] || [ "$rc" -eq 3 ]; then
    gr_log "pressing again (once): the first press did not stop the simulation"
    press_and_check; rc=$?
    [ "$rc" -eq 1 ] && gr_log "FAIL: the simulation is still running after the second press"
    [ "$rc" -eq 3 ] && gr_log "FAIL: the key helper did not finish in ${KEY_TIMEOUT}s twice"
  fi
  [ "$rc" -eq 0 ] || return 1
  gr_log "arming the proxy"
  : >"$arm"
}

# The paused game is still presented, so every shot shows the frame the trace describes (the window was opened by the arming).
# Returns non-zero (and says why) if one cannot be taken; the caller still closes the game and restores the directory first.
scenario_shots() {
  local i=0 f
  while [ "$i" -lt "$SCN_SHOTS" ]; do
    [ "$i" -eq 0 ] || sleep "$SCN_SHOT_GAP"
    f="$OUT/shot-$i.png"
    rm -f "$f"
    if "$SHOT_CMD" -x "$f" >>"$OUT/shot.log" 2>&1 && [ -s "$f" ]; then
      gr_log "screenshot $((i + 1)) of $SCN_SHOTS: $f"
    else
      gr_log "FAIL: screenshot $((i + 1)) of $SCN_SHOTS could not be taken (see $OUT/shot.log)"
      return 1
    fi
    i=$((i + 1))
  done
}

# The Windows path of the game's exe, from its place in the prefix: C:\GOG Games\...\Empire Earth.exe
game_exe_windows_path() {
  local rel="${GR_GAME#"$GR_PREFIX"/drive_c/}"
  printf 'C:\\%s\\%s' "$(printf '%s' "$rel" | tr '/' '\\')" "$GAME_EXE_NAME"
}

# After the snapshot: the helper's command file and the save in the game's Saved Games (the restore removes it again).
scenario_prepare() {
  printf '%s\n%s\n' "$(game_exe_windows_path)" "$SCN_ARGS" >"$(dirname "$START_EXE")/ee-start.cmd" || return 1
  mkdir -p "$GR_GAME/Data/Saved Games" && cp "$SCENARIO_DIR/$SCN_SAVE" "$GR_GAME/Data/Saved Games/$SCN_SAVE" || return 1
  [ "$(shasum -a 256 "$GR_GAME/Data/Saved Games/$SCN_SAVE" | cut -c1-64)" = "$SCN_SAVE_SHA256" ] || { gr_log "FAIL: the save copied into the game is not the scenario's"; return 1; }
  rm -f "$GR_GAME/ee-version.log" # tick lines of an earlier session must not pass for this run's; the restore brings the old file back
  gr_log "scenario $SCN_NAME: $SCN_ARGS (save sha256 $SCN_SAVE_SHA256)"
}

# The launcher's start of the game is `wine start /unix <exe>`; the wrapper runs swap-in.sh just before it.
one_run() {
  local tag="$1" raw="$OUT/run.raw" win_raw blocking rc unclean=0

  if [ -n "$SCENARIO_FILE" ]; then
    scenario_check_save || return 3
    scenario_build_helpers || return 3
  fi
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
  if [ -n "$SCENARIO_FILE" ]; then
    scenario_prepare || { gr_teardown; return 3; }
    GR_START_REPLACE="$START_EXE"
  fi

  win_raw="Z:$(printf '%s' "$raw" | tr '/' '\\')"
  rm -f "$raw" "$raw.done" "$raw.failed" "$raw.times" "$raw.callers" "$raw.data"
  local arm_args=() data_args=()
  if [ -n "$SCN_KEYS" ]; then
    rm -f "$raw.arm"
    arm_args=("Z:$(printf '%s' "$raw.arm" | tr '/' '\\')")
  fi
  if [ "$WITH_DATA" = 1 ]; then
    # swap-in.sh's arm argument is positional: an empty one when there is no arm file
    [ ${#arm_args[@]} -gt 0 ] || arm_args=("")
    data_args=("$win_raw" "${DATA_MAX:-}")
    # a keyed scenario arms late: store the pixels as the game writes them, before the arming (README, DDT_DATA_EARLY)
    [ -z "$SCN_KEYS" ] || data_args+=(1)
  fi
  gr_make_wine_wrapper "$OUT/wrap" "$HERE/scripts/swap-in.sh" "$GR_GAME" "$PROXY" "$win_raw" "$PRESENTS" ${arm_args[@]+"${arm_args[@]}"} ${data_args[@]+"${data_args[@]}"} || return 3

  gr_launcher_log_mark
  gr_log "launching the game through the launcher (presents cap: $PRESENTS)"
  gr_run_launcher "$LAUNCH_LIMIT" "$OUT/launch.log" \
    bash -c 'cd "$1" && shift && exec "$@"' _ "$GR_LAUNCHER_DIR" \
    env EMPIRE_EARTH_WINE="$GR_WRAPPED_WINE" \
    EE_BIG_ARMIES=0 EE_BIG_CROWDS=0 EE_X87T=0 EE_SSE_MATH=0 EE_MEMFILE_DOUBLE=0 EE_POINTLIST_GUARD=0 EE_ANIMATION_SMOOTHING=0 \
    $SCN_ENV \
    "$GR_LAUNCHER_DIR/scripts/launch.sh"
  rc=$?
  if [ "$rc" -ne 0 ]; then
    gr_log "FAIL: launch.sh exited with $rc (see $OUT/launch.log)"
    tail -5 "$OUT/launch.log" | sed 's/^/    /' >&2
    return 4
  fi
  gr_check_launcher_log || return 4
  gr_log "launcher reports the first frames are up; waiting for the capture cap"

  if [ -n "$SCN_KEYS" ]; then
    scenario_pause_and_arm "$raw.arm" || return 5
  fi
  local waited=0
  while [ ! -f "$raw.done" ]; do
    if [ -f "$raw.failed" ]; then
      gr_log "FAIL: the proxy ended the capture ($(grep -m1 '^# ERROR' "$raw" | cut -c1-200)); see the trace tail"
      return 5
    fi
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
  gr_log "capture complete ($(wc -l <"$raw" | tr -d ' ') lines)"
  cp "$raw" "$OUT/trace.$tag.txt"
  [ -f "$raw.times" ] && cp "$raw.times" "$OUT/trace.$tag.times"
  [ -f "$raw.callers" ] && cp "$raw.callers" "$OUT/trace.$tag.callers"
  if [ "$WITH_DATA" = 1 ]; then
    [ -f "$raw.data" ] || { gr_log "FAIL: --data was asked for but the proxy wrote no data file ($raw.data)"; return 5; }
    cp "$raw.data" "$OUT/trace.$tag.data"
    gr_log "data file saved: $OUT/trace.$tag.data ($(wc -c <"$raw.data" | tr -d ' ') bytes)"
  fi
  local shots_failed=0
  if [ -n "$SCENARIO_FILE" ] && [ "${SCN_SHOTS:-0}" -gt 0 ]; then
    scenario_shots || shots_failed=1
  fi
  if [ -n "$SCENARIO_FILE" ]; then
    # the scenario ends the way a player's session does: WM_CLOSE, then (if ignored) WM_QUIT; the prefix is stopped after
    [ -f "$GR_GAME/ee-version.log" ] && cp "$GR_GAME/ee-version.log" "$OUT/ee-version.$tag.log"
    gr_log "asking the game to close (ee-close.exe: WM_CLOSE on desktop $DESKTOP_NAME)"
    if gr_close_game "$CLOSE_EXE" "$CLOSE_GRACE" "$EXIT_WAIT" "$DESKTOP_NAME"; then
      gr_log "the game left by itself (via $GR_CLOSE_VIA)"
    else
      unclean=1
    fi
  fi
  gr_log "stopping the prefix"
  gr_stop_game || return 7
  gr_log "trace saved: $OUT/trace.$tag.txt"
  gr_teardown || return 6
  [ "$shots_failed" -eq 0 ] || { gr_log "FAIL: the reference screenshots were not all taken; the trace is kept, the run is not clean"; return 9; }
  [ "${unclean:-0}" -eq 0 ] || { gr_log "FAIL: the game did not close by itself (WM_CLOSE and WM_QUIT were ignored); the trace is kept, the run is not clean"; return 8; }
  return 0
}

# Two traces are equal if they are identical, or identical after the normalization (scripts/normalize.py): the cursor-sprite
# blits removed; and, for a scenario that asks for it (SCN_NORMALIZE=structure), the structure form.
compare_traces() {
  local a="$1" b="$2" na nb what="the cursor-sprite blits" form=""
  if [ "$SCN_NORMALIZE" = structure ]; then
    form="--structure"
    what="ids, content hashes, texture bindings, vertex ring phase and lock flags (structure form)"
  fi
  if cmp -s "$a" "$b"; then
    gr_log "traces identical ($(wc -l <"$a" | tr -d ' ') lines)"
    return 0
  fi
  na="$(mktemp "$OUT/norm.XXXXXX")"
  nb="$(mktemp "$OUT/norm.XXXXXX")"
  # shellcheck disable=SC2086
  python3 -I "$HERE/scripts/normalize.py" $form <"$a" >"$na"
  # shellcheck disable=SC2086
  python3 -I "$HERE/scripts/normalize.py" $form <"$b" >"$nb"
  gr_log "traces differ raw in $(paste -d '\n' "$a" "$b" | awk 'NR % 2 == 1 { x = $0; next } $0 != x { n++ } END { print n + 0 }') of $(wc -l <"$a" | tr -d ' ') lines (position by position)"
  if cmp -s "$na" "$nb"; then
    if [ -n "$form" ]; then
      gr_log "traces differ raw but are identical in structure form ($(wc -l <"$na" | tr -d ' ') lines; masked: $what)"
    else
      gr_log "traces differ raw but are identical after removing $what ($(wc -l <"$na" | tr -d ' ') lines left; raw $(wc -l <"$a" | tr -d ' ') and $(wc -l <"$b" | tr -d ' '))"
    fi
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
