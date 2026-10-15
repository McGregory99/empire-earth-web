#!/usr/bin/env bash
# Runtime Win32 inventory of the real game: startup to the main menu, idle at the menu, clean exit, under Wine's own +relay
# tracing of the Win32 functions that the game's binaries import.
#
#   runtime-inventory.sh [--test-config <file>] [--tag <name>] [--profile broad|timers|full] [--idle <seconds>]
#
# One run: take the game-run lock, refuse if anything of an earlier run is left over or the game (or Art of Conquest) is running,
# snapshot the game directory (sha256 manifest, verified), put the relay lists into the prefix's HKCU\Software\Wine\Debug
# (RelayInclude, RelayExclude, RelayFromExclude; the previous values are recorded and put back by an undo step, also by
# recover.sh after an interrupted run), launch through the unmodified launcher with WINEDEBUG set by this script (the launcher
# honours a WINEDEBUG given to it; the library scrubs the ambient one), wait for the main menu, idle, ask the game to close with
# ee-close.exe (WM_CLOSE), stop the prefix, restore everything, verify (registry values, game directory manifest), release the
# lock, and only then summarize the log (ri.py).  Evidence goes to <OUT>/<tag>/, outside the repository.
#
# The profiles: "broad" traces every imported function but the hot ones and the timing functions; "timers" traces only the timing
# functions (QueryPerformanceCounter, GetTickCount, timeGetTime, ...) and CreateThread, because a spin loop on one of them could
# write gigabytes.  Hard limits: MAX_RUNS real runs in total (RUNS_FILE), LOG_CAP bytes of log.
#
# The protocol, the lock, the snapshot and the signal-safe teardown are the shared library tools/game_run/lib.sh.
set -u -o pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
TEST_CONFIG=""
TAG=""
PROFILE=broad
IDLE_OVERRIDE=""
usage() { echo "usage: runtime-inventory.sh [--test-config <file>] [--tag <name>] [--profile broad|timers|full] [--idle <seconds>]" >&2; }
while [ $# -gt 0 ]; do
  case "$1" in
    --test-config) TEST_CONFIG="${2:?$(usage)}"; shift 2 ;;
    --tag) TAG="${2:?$(usage)}"; shift 2 ;;
    --profile) PROFILE="${2:?$(usage)}"; shift 2 ;;
    --idle) IDLE_OVERRIDE="${2:?$(usage)}"; shift 2 ;;
    *) usage; exit 2 ;;
  esac
done

# shellcheck source=../lib.sh
. "$HERE/../lib.sh"
GR_TOOL=runtime-inventory
RI="$HERE/ri.py"

# Plain defaults; nothing is read from the environment (a test replaces them through --test-config).
OUT="$HOME/Games/EmpireEarth-re/runtime/inventory"
RUNS_FILE=""
MAX_RUNS=3
IDLE_SECONDS=30
MENU_WAIT=120    # seconds to wait for the main menu after launch.sh returns
MENU_SETTLE=8    # seconds of quiet after the menu appears, before the idle window starts
CLOSE_GRACE=25   # seconds to wait for the game to leave after WM_CLOSE before WM_QUIT is posted
EXIT_WAIT=60     # seconds to wait for the game to leave after WM_QUIT
LOG_CAP=800000000
CAP_POLL=2
LAUNCH_LIMIT=900
CLOSE_BUILD=1
CLOSE_CC=i686-w64-mingw32-gcc
CLOSE_EXE=""
DESKTOP_NAME=EmpireEarth
WINEDEBUG_SPEC='-all,+err,+fixme,+relay,+loaddll,+tid,+timestamp,+dsound,+dinput,+winmm,+mci,+ole'
WINEDEBUG_TIMERS='-all,+err,+relay,+loaddll,+tid,+timestamp' # the timers profile: nothing but the call rates
GR_LOCK_WAIT=1200 # another agent's run: wait up to 20 minutes, never break its lock
gr_test_hook "$TEST_CONFIG"
gr_sanitize_env
[ -n "$IDLE_OVERRIDE" ] && IDLE_SECONDS="$IDLE_OVERRIDE"
[ -n "$RUNS_FILE" ] || RUNS_FILE="$OUT/runs.txt"
[ -n "$TAG" ] || TAG="$(date '+%Y%m%d-%H%M%S')-$PROFILE"
RUN_DIR="$OUT/$TAG"
[ -n "$CLOSE_EXE" ] || CLOSE_EXE="$OUT/bin/ee-close.exe"

case "$PROFILE" in
  broad | timers | full) ;;
  *) echo "unknown profile '$PROFILE' (use broad, timers or full)" >&2; exit 2 ;;
esac
[ "$PROFILE" = timers ] && WINEDEBUG_SPEC="$WINEDEBUG_TIMERS"
case "$TAG" in */* | "" | .*) echo "bad tag '$TAG'" >&2; exit 2 ;; esac
if [ -e "$RUN_DIR" ]; then
  echo "$RUN_DIR already exists; choose another --tag" >&2
  exit 2
fi
mkdir -p "$RUN_DIR" || exit 2
GR_LOG_FILE="$RUN_DIR/runtime-inventory.log"

failures=0
pass() { gr_log "ok   $*"; }
fail() { gr_log "FAIL $*"; failures=$((failures + 1)); }

# Keep the display awake for the length of this script (see game-trace.sh).
if command -v caffeinate >/dev/null 2>&1; then
  caffeinate -d -i -w $$ >/dev/null 2>&1 &
fi

DEBUG_KEY='HKCU\Software\Wine\Debug'
DEBUG_VALUES="RelayInclude RelayExclude RelayFromExclude"
WATCHDOG_PID=""

cleanup() {
  [ -n "$WATCHDOG_PID" ] && kill "$WATCHDOG_PID" 2>/dev/null
  WATCHDOG_PID=""
  salvage_logs
  gr_teardown
}
gr_install_traps cleanup

wine_env() { WINEPREFIX="$GR_PREFIX" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" "$@"; }
flush_registry() { WINEPREFIX="$GR_PREFIX" "$GR_WINE_ROOT/bin/wineserver" -k >/dev/null 2>&1; }

# <name>: sets REG_PRESENT (0/1) and REG_VALUE from the prefix's Debug key.  Returns 2 for a value that is not a plain string.
reg_get() {
  local out line
  REG_PRESENT=0
  REG_VALUE=""
  out="$(wine_env "$GR_WINE_ROOT/bin/wine" reg query "$DEBUG_KEY" /v "$1" 2>/dev/null | tr -d '\r')" || return 0
  line="$(printf '%s\n' "$out" | grep -E "^[[:space:]]+$1[[:space:]]+REG_" | head -1)"
  [ -n "$line" ] || return 0
  case "$line" in
    *"$1    REG_SZ"*) ;;
    *) return 2 ;;
  esac
  REG_PRESENT=1
  REG_VALUE="$(printf '%s\n' "$line" | sed -E "s/^[[:space:]]+$1[[:space:]]+REG_SZ([[:space:]]{4}|\$)//")"
  return 0
}

# the last Wine tick (the +timestamp column) in the newest log of this run, or "-"
log_tick() {
  local f="$1" t
  [ -f "$f" ] || { echo "-"; return; }
  t="$(tail -c 262144 "$f" 2>/dev/null | grep -aoE '^[0-9]+\.[0-9]{3}:' | tail -1 | tr -d ':')"
  echo "${t:--}"
}
phase() { # <name> <log file>
  printf '%s %s %s\n' "$1" "$(date '+%s')" "$(log_tick "${2:-}")" >>"$RUN_DIR/phases.txt"
  gr_log "phase $1"
}

new_logs() { ls "$GR_LOG_DIR"/launch-*.log 2>/dev/null | sort | comm -13 "$GAME_RUN_STATE/logs-before.txt" -; }
newest_log() { new_logs | tail -1; }
file_size() { stat -f %z "$1" 2>/dev/null || echo 0; }
log_over_cap() {
  local f over=""
  while IFS= read -r f; do
    [ -n "$f" ] && [ "$(file_size "$f")" -gt "$LOG_CAP" ] && over="$f"
  done < <(new_logs)
  [ -n "$over" ]
}
# the launcher's logs of a run that ended early stay with its evidence, not in the support logs
salvage_logs() {
  local f i=0
  [ -f "$GAME_RUN_STATE/logs-before.txt" ] || return 0
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    i=$((i + 1))
    mv "$f" "$RUN_DIR/aborted-launch-$i.log" 2>/dev/null
  done < <(new_logs)
}

# ---- 1. the lock and the preconditions ------------------------------------------------------------------------------------

gr_lock_acquire || exit 2
[ -d "$GR_GAME" ] || { gr_log "no game copy at $GR_GAME"; exit 2; }
[ -x "$GR_LAUNCHER_DIR/scripts/launch.sh" ] || { gr_log "no launcher at $GR_LAUNCHER_DIR"; exit 2; }
command -v python3 >/dev/null 2>&1 || { gr_log "python3 is needed"; exit 2; }
gr_check_leftovers || exit 3
gr_assert_config || exit 3
blocking="$(gr_blocking_pids | tr '\n' ' ')"
if [ -n "$blocking" ]; then
  gr_log "FAIL: the game, Art of Conquest or another program of the GOG install is running (pid $blocking); refusing to touch the game directory"
  exit 3
fi
used=0
[ -f "$RUNS_FILE" ] && used="$(grep -c . "$RUNS_FILE" 2>/dev/null || true)"
if [ "${used:-0}" -ge "$MAX_RUNS" ]; then
  gr_log "FAIL: run budget used up: $used real run(s) recorded in $RUNS_FILE, the limit is $MAX_RUNS; refusing to start the game"
  exit 3
fi
if [ "$CLOSE_BUILD" = 1 ]; then
  command -v "$CLOSE_CC" >/dev/null 2>&1 || { gr_log "FAIL: $CLOSE_CC is needed to build ee-close.exe"; exit 2; }
  mkdir -p "$(dirname "$CLOSE_EXE")"
  "$CLOSE_CC" -mcrtdll=msvcrt-os -O2 -Wall -D_WIN32_WINNT=0x0600 -o "$CLOSE_EXE" "$HERE/ee-close.c" -luser32 2>"$RUN_DIR/ee-close.build.log" || {
    gr_log "FAIL: cannot build ee-close.exe (see $RUN_DIR/ee-close.build.log)"
    exit 2
  }
fi
[ -f "$CLOSE_EXE" ] || { gr_log "FAIL: no close helper at $CLOSE_EXE"; exit 2; }

# ---- 2. snapshot, then the Wine debug values ----------------------------------------------------------------------------------

gr_log "taking the sha256 manifest and a verified snapshot of the game directory"
gr_state_begin && gr_snapshot_take || exit 3
cp "$GAME_RUN_STATE/manifest.before" "$RUN_DIR/manifest-before.txt"

flush_registry
prev_present_RelayInclude=0 prev_present_RelayExclude=0 prev_present_RelayFromExclude=0
prev_value_RelayInclude="" prev_value_RelayExclude="" prev_value_RelayFromExclude=""
for name in $DEBUG_VALUES; do
  reg_get "$name"
  rc=$?
  if [ "$rc" -ne 0 ]; then
    gr_log "FAIL: $DEBUG_KEY\\$name is not a plain string (REG_SZ); not touching it"
    exit 2
  fi
  eval "prev_present_$name=$REG_PRESENT"
  eval "prev_value_$name=\$REG_VALUE"
done
flush_registry
mkdir -p "$RUN_DIR/registry-before"
for f in user.reg system.reg; do cp "$GR_PREFIX/$f" "$RUN_DIR/registry-before/$f" 2>/dev/null || true; done

python3 -I "$RI" lists --game-dir "$GR_GAME" --wine-root "$GR_WINE_ROOT" --profile "$PROFILE" \
  --existing-exclude "$prev_value_RelayExclude" --existing-fromexclude "$prev_value_RelayFromExclude" >"$RUN_DIR/relay-lists.json" || {
  gr_log "FAIL: cannot build the relay lists (see above)"
  exit 2
}
for key in include exclude fromexclude; do
  python3 -I -c 'import json,sys; sys.stdout.write(json.load(open(sys.argv[1]))[sys.argv[2]])' "$RUN_DIR/relay-lists.json" "$key" >"$RUN_DIR/relay-$key.txt" || exit 2
done
[ -s "$RUN_DIR/relay-include.txt" ] || { gr_log "FAIL: the relay include list is empty"; exit 2; }
gr_log "relay lists ($PROFILE): $(tr ';' '\n' <"$RUN_DIR/relay-include.txt" | grep -c .) entries to trace"

# The undo steps run before the restore, also from recover.sh after an interrupted run.
for name in $DEBUG_VALUES; do
  eval "present=\$prev_present_$name"
  eval "value=\$prev_value_$name"
  if [ "$present" = 1 ]; then
    gr_undo_add env "WINEPREFIX=$GR_PREFIX" WINEDEBUG=-all "WINEDLLOVERRIDES=mscoree,mshtml=" "$GR_WINE_ROOT/bin/wine" \
      reg add "$DEBUG_KEY" /v "$name" /t REG_SZ /d "$value" /f
  else
    gr_undo_add env "WINEPREFIX=$GR_PREFIX" WINEDEBUG=-all "WINEDLLOVERRIDES=mscoree,mshtml=" "$GR_WINE_ROOT/bin/wine" \
      reg delete "$DEBUG_KEY" /v "$name" /f
  fi
done
gr_undo_add env "WINEPREFIX=$GR_PREFIX" "$GR_WINE_ROOT/bin/wineserver" -k

set_value() { # <name> <file>
  wine_env "$GR_WINE_ROOT/bin/wine" reg add "$DEBUG_KEY" /v "$1" /t REG_SZ /d "$(cat "$2")" /f >/dev/null 2>&1
}
set_value RelayInclude "$RUN_DIR/relay-include.txt" && set_value RelayExclude "$RUN_DIR/relay-exclude.txt" &&
  set_value RelayFromExclude "$RUN_DIR/relay-fromexclude.txt" || { gr_log "FAIL: could not set the Wine debug values"; exit 2; }
flush_registry
reg_get RelayInclude
[ "$REG_VALUE" = "$(cat "$RUN_DIR/relay-include.txt")" ] || { gr_log "FAIL: RelayInclude did not read back as it was set"; exit 2; }
gr_log "Wine debug values set in $DEBUG_KEY (they are restored by the teardown)"

# ---- 3. launch --------------------------------------------------------------------------------------------------------------------

mkdir -p "$OUT" "$GR_LOG_DIR"
ls "$GR_LOG_DIR"/launch-*.log 2>/dev/null | sort >"$GAME_RUN_STATE/logs-before.txt"
printf '%s %s %s\n' "$TAG" "$PROFILE" "$(date '+%F %T')" >>"$RUNS_FILE"
gr_launcher_log_mark
: >"$RUN_DIR/phases.txt"
phase launch ""

# A log past the cap ends the run: the launcher's own watcher reads the whole log every second.
(
  while sleep "$CAP_POLL"; do
    if log_over_cap; then
      : >"$RUN_DIR/log-cap-exceeded"
      pkill -TERM -f "$GR_LAUNCHER_DIR/scripts/launch.sh" 2>/dev/null
      WINEPREFIX="$GR_PREFIX" "$GR_WINE_ROOT/bin/wineserver" -k >/dev/null 2>&1
      exit 0
    fi
  done
) &
WATCHDOG_PID=$!

gr_log "launching the game (WINEDEBUG=$WINEDEBUG_SPEC)"
gr_run_launcher "$LAUNCH_LIMIT" "$RUN_DIR/launch.stdout" \
  bash -c 'cd "$1" && shift && exec "$@"' _ "$GR_LAUNCHER_DIR" \
  env EE_BIG_ARMIES=0 EE_BIG_CROWDS=0 EE_X87T=0 EE_SSE_MATH=0 EE_MEMFILE_DOUBLE=0 EE_POINTLIST_GUARD=0 EE_ANIMATION_SMOOTHING=0 \
  WINEDEBUG="$WINEDEBUG_SPEC" "$GR_LAUNCHER_DIR/scripts/launch.sh"
launch_status=$?
gr_log "launch.sh exit status $launch_status"
LOG="$(newest_log)"
if [ -e "$RUN_DIR/log-cap-exceeded" ]; then
  gr_log "FAIL: the log size passed the cap ($LOG_CAP bytes) while launching; the run was stopped"
  exit 6
fi
if [ "$launch_status" -ne 0 ] || [ -z "$LOG" ]; then
  gr_log "FAIL: launch.sh failed (status $launch_status${LOG:+, log $LOG})"
  tail -5 "$RUN_DIR/launch.stdout" | sed 's/^/    /' >&2
  exit 4
fi
gr_check_launcher_log || exit 4
if ! gr_game_running; then
  gr_log "FAIL: the game is not running after launch.sh returned"
  exit 5
fi
phase launched "$LOG"

# ---- 4. the main menu, the idle window, the clean exit ---------------------------------------------------------------------------

waited=0
until grep -q 'SetDisplayMode 1024x768x32' "$GR_GAME/ee-ddraw.log" 2>/dev/null; do
  if [ "$waited" -ge "$MENU_WAIT" ]; then
    gr_log "WARNING: the proxy log never showed the 1024x768 menu mode after ${MENU_WAIT}s; idling from here"
    break
  fi
  gr_game_running || { gr_log "FAIL: the game exited before the main menu"; exit 5; }
  log_over_cap && { gr_log "FAIL: the log size passed the cap ($LOG_CAP bytes) before the menu"; exit 6; }
  sleep 1
  waited=$((waited + 1))
done
sleep "$MENU_SETTLE"
phase menu "$LOG"
phase idle_start "$LOG"
idle=0
while [ "$idle" -lt "$IDLE_SECONDS" ]; do
  gr_game_running || { gr_log "FAIL: the game exited during the idle window"; exit 5; }
  if log_over_cap; then
    gr_log "FAIL: the log size passed the cap ($LOG_CAP bytes) during the idle window"
    exit 6
  fi
  sleep 1
  idle=$((idle + 1))
done
phase idle_end "$LOG"
# wait <seconds> for the game to leave; 0 if it did
wait_game_exit() {
  local waited=0
  while gr_game_running; do
    [ "$waited" -ge "$1" ] && return 1
    sleep 1
    waited=$((waited + 1))
  done
  return 0
}
close_game() { # [--quit]
  wine_env "$GR_WINE_ROOT/bin/wine" "$CLOSE_EXE" "$@" "Empire Earth.exe" "$DESKTOP_NAME" Default >>"$RUN_DIR/close.out" 2>&1
  gr_log "ee-close.exe $*: $(tail -3 "$RUN_DIR/close.out" | tr '\n' ' ')"
}
phase close_sent "$LOG"
gr_log "asking the game to close (ee-close.exe: WM_CLOSE on desktop $DESKTOP_NAME)"
: >"$RUN_DIR/close.out"
close_game
clean_exit=1
exit_via=wm_close
if ! wait_game_exit "$CLOSE_GRACE"; then
  gr_log "the game is still there ${CLOSE_GRACE}s after WM_CLOSE; posting WM_QUIT to its window thread"
  close_game --quit
  exit_via=wm_quit
  if ! wait_game_exit "$EXIT_WAIT"; then
    clean_exit=0
    exit_via=killed
    gr_log "UNCLEAN: the game did not leave within ${EXIT_WAIT}s of WM_QUIT; stopping the prefix"
  fi
fi
[ "$clean_exit" -eq 1 ] && gr_log "the game left by itself (via $exit_via)"
sleep 2
phase exited "$LOG"
kill "$WATCHDOG_PID" 2>/dev/null
WATCHDOG_PID=""
gr_stop_game || exit 7

# ---- 5. evidence, then put everything back ----------------------------------------------------------------------------------------

i=0
while IFS= read -r f; do
  [ -n "$f" ] || continue
  i=$((i + 1))
  if [ "$f" = "$LOG" ]; then mv "$f" "$RUN_DIR/relay.log"; else mv "$f" "$RUN_DIR/launch-attempt-$i.log"; fi
done < <(new_logs)
[ -f "$GR_GAME/ee-version.log" ] && cp "$GR_GAME/ee-version.log" "$RUN_DIR/ee-version.log"
[ -f "$GR_GAME/ee-ddraw.log" ] && head -c 5000000 "$GR_GAME/ee-ddraw.log" >"$RUN_DIR/ee-ddraw.log"
mkdir -p "$RUN_DIR/registry-after"
for f in user.reg system.reg; do cp "$GR_PREFIX/$f" "$RUN_DIR/registry-after/$f" 2>/dev/null || true; done
{
  echo "tag=$TAG"
  echo "profile=$PROFILE"
  echo "clean_exit=$clean_exit"
  echo "exit_via=$exit_via"
  echo "idle_seconds=$IDLE_SECONDS"
  echo "launch_status=$launch_status"
  echo "log_bytes=$(file_size "$RUN_DIR/relay.log")"
  echo "wine=$("$GR_WINE_ROOT/bin/wine" --version 2>/dev/null | head -1)"
} >"$RUN_DIR/run.env"

# Undo steps, restore and verification; the lock is kept until the registry and the manifest have been read back.
GR_KEEP_LOCK=1
if ! gr_teardown; then
  gr_log "FAIL: the teardown did not finish: the lock and the snapshot are kept (see the recovery commands above)"
  exit 1
fi
pass "everything was put back: the undo steps ran and the game directory matches its manifest"
for name in $DEBUG_VALUES; do
  eval "present=\$prev_present_$name"
  eval "value=\$prev_value_$name"
  reg_get "$name"
  if [ "$REG_PRESENT" = "$present" ] && [ "$REG_VALUE" = "$value" ]; then
    pass "registry: $name is as it was ($([ "$present" = 1 ] && echo set || echo absent) before)"
  else
    fail "registry: $name was (present=$present) '$value' and is (present=$REG_PRESENT) '$REG_VALUE'"
  fi
done
flush_registry
section() { sed -n '/^\[Software\\\\Wine\\\\Debug\]/,/^$/p' "$1" 2>/dev/null | grep -v '^#time=' | sed -E 's/^(\[.*\]) [0-9]+$/\1/'; }
if [ -f "$RUN_DIR/registry-before/user.reg" ] && [ -f "$GR_PREFIX/user.reg" ]; then
  if [ "$(section "$RUN_DIR/registry-before/user.reg")" = "$(section "$GR_PREFIX/user.reg")" ]; then
    pass "user.reg: the Software\\Wine\\Debug section is identical to the one before the run"
  else
    fail "user.reg: the Software\\Wine\\Debug section differs from the one before the run"
  fi
  norm() { grep -v '^#time=' "$1" | sed -E 's/^(\[.*\]) [0-9]+$/\1/'; }
  diff <(norm "$RUN_DIR/registry-before/user.reg") <(norm "$GR_PREFIX/user.reg") >"$RUN_DIR/registry-diff-user.txt" 2>/dev/null
  diff <(norm "$RUN_DIR/registry-before/system.reg") <(norm "$GR_PREFIX/system.reg") >"$RUN_DIR/registry-diff-system.txt" 2>/dev/null
  gr_log "registry changes the run left (the game's and Wine's own, not ours): user.reg $(grep -c '^[<>]' "$RUN_DIR/registry-diff-user.txt") lines, system.reg $(grep -c '^[<>]' "$RUN_DIR/registry-diff-system.txt") lines"
fi
gr_manifest "$GR_GAME" "$RUN_DIR/manifest-after.txt"
if diff -u "$RUN_DIR/manifest-before.txt" "$RUN_DIR/manifest-after.txt" >"$RUN_DIR/manifest.diff"; then
  pass "the game directory matches the manifest taken before ($(wc -l <"$RUN_DIR/manifest-after.txt" | tr -d ' ') files, sha256)"
else
  fail "the game directory differs from the manifest taken before"
  head -20 "$RUN_DIR/manifest.diff" >&2
fi
GR_KEEP_LOCK=0
gr_lock_release
[ "$failures" -eq 0 ] || { gr_log "FAIL: $failures check(s) failed; evidence in $RUN_DIR/"; exit 1; }

# ---- 6. the summary (no lock needed: the game is gone and everything is back) -----------------------------------------------------

if [ -s "$RUN_DIR/relay.log" ]; then
  python3 -I "$RI" summarize --log "$RUN_DIR/relay.log" --game-dir "$GR_GAME" --wine-root "$GR_WINE_ROOT" --phases "$RUN_DIR/phases.txt" \
    --profile "$PROFILE" --out "$RUN_DIR/summary" || { gr_log "FAIL: ri.py summarize failed; the raw log is kept in $RUN_DIR/relay.log"; exit 7; }
else
  gr_log "FAIL: no relay log was produced"
  exit 7
fi
gr_log "PASS: run $TAG done (clean_exit=$clean_exit); evidence in $RUN_DIR/"
exit 0
