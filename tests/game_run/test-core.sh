#!/usr/bin/env bash
# Tests of the core of the shared game-run library (tools/game_run/lib.sh): environment hygiene, anchored game
# detection, the game-run lock with its owner token, signal-safe teardown and the launcher as a background job.
# Runs in a fake world; never touches the real game.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/world.sh"
world_make
trap 'world_clean' EXIT
export W TOOL_DIR

# ---- 1. environment hygiene and the fixed lock path -------------------------------------------------------------------
out="$(env -i HOME="$HOME" PATH="$PATH" RE_DIR=/elsewhere/re EMPIRE_EARTH_SUPPORT_DIR=/x EMPIRE_EARTH_WINEPREFIX=/y \
  EMPIRE_EARTH_WINE=/z/wine WINEDEBUG=+all WINEPREFIX=/w WINEDLLOVERRIDES=ddraw=n EE_GRAPHICS=dxmt EE_X87T=1 \
  DXVK_HUD=1 FAKE_KEEP=1 bash -c '. "$1/lib.sh"; gr_sanitize_env; echo "lock=$GAME_RUN_LOCK"; env | grep -E "^(EE_|EMPIRE_EARTH_|WINE|DXVK_)" ; echo "keep=${FAKE_KEEP:-}"; echo "re=${RE_DIR:-}"' _ "$TOOL_DIR")"
check_eq "the lock path is fixed, not derived from RE_DIR" "lock=$HOME/Games/EmpireEarth-re/locks/game-run" "$(echo "$out" | sed -n 1p)"
check_eq "no ambient EE_*, EMPIRE_EARTH_*, WINE* or DXVK_* variable survives" "" "$(echo "$out" | grep -E '^(EE_|EMPIRE_EARTH_|WINE|DXVK_)')"
check "unrelated variables are left alone" test "$(echo "$out" | grep -c '^keep=1$')" -eq 1

# ---- 2. game detection: the launcher's anchored logic ------------------------------------------------------------------
. "$TOOL_DIR/lib.sh"
. "$W/config.sh"
: >"$W/ps.out"
check_eq "no process, no game" "" "$(gr_game_pids)"
cat >"$W/ps.out" <<'PS'
  101 90000 /usr/bin/python3 something
  102 3000 bash -c while pgrep -f 'C:.GOG Games.*Empire Earth.exe'; do sleep 1; done
  103 3000 pgrep -f C:.GOG Games.*Empire Earth.exe
  104 5000 vim notes C:\GOG Games\Empire Earth.exe
  105 5000 wine reg add HKCU\Software\Wine\AppDefaults\Empire Earth.exe\DllOverrides /v version
PS
check_eq "an agent's own pgrep loop, an editor and the launcher's reg command do not match" "" "$(gr_game_pids)"
echo '  201 90000 C:\GOG Games\Empire Earth Gold Edition\Empire Earth\Empire Earth.exe' >>"$W/ps.out"
check_eq "the game matches" "201" "$(gr_game_pids)"
: >"$W/ps.out"
echo '  202 90000 Z:\Users\x\Library\Application Support\EmpireEarthMac\wineprefix\drive_c\GOG Games\Empire Earth - The Art of Conquest\EE-AOC.exe' >"$W/ps.out"
check_eq "Art of Conquest counts as the game" "202" "$(gr_game_pids)"
GR_MIN_RSS_KB=1024
echo '  203 500 C:\GOG Games\Empire Earth Gold Edition\Empire Earth\Empire Earth.exe' >>"$W/ps.out"
check_eq "a Rosetta zombie with a few KB resident is not a running game" "202" "$(gr_game_pids)"
GR_MIN_RSS_KB=0
check "gr_game_running is true while the game lists" gr_game_running
: >"$W/ps.out"
check_not "gr_game_running is false when nothing lists" gr_game_running
echo '  204 90000 C:\GOG Games\Empire Earth Gold Edition\Empire Earth\dxcfg.exe' >"$W/ps.out"
check_eq "another program of the GOG tree blocks a start" "204" "$(gr_blocking_pids)"
check_eq "... but it is not 'the game'" "" "$(gr_game_pids)"
launcher_lib="$HOME/Games/tools/ee-macos/scripts/lib.sh"
if [ -f "$launcher_lib" ]; then
  check "the launcher's own game_pids uses the regex the library uses" \
    grep -qF '$0 ~ /^[A-Za-z]:\\.*\\(Empire Earth|EE-AOC)\.exe/' "$launcher_lib"
fi
: >"$W/ps.out"

# ---- 3. the lock -------------------------------------------------------------------------------------------------------
LOCK="$GAME_RUN_LOCK"
GR_TOOL=test

( . "$TOOL_DIR/lib.sh"; . "$W/config.sh"; gr_lock_acquire 1 >/dev/null 2>&1; echo $? >"$W/rc"; cat "$LOCK/owner" >"$W/owner.copy"; gr_lock_release >/dev/null 2>&1 )
check_eq "a free lock is taken" "0" "$(cat "$W/rc")"
check "the owner token holds pid, start time and tool" grep -q '^pid=[0-9]*$' "$W/owner.copy"
check "... and the start time" grep -q '^lstart=.' "$W/owner.copy"
check "... and the tool" grep -q '^tool=test$' "$W/owner.copy"
check "release removes the lock directory" test ! -e "$LOCK"

# a live owner: a second taker waits, times out, and does not touch the lock
start_job bash -c '. "$1/lib.sh"; . "$W/config.sh"; GR_TOOL=holder; gr_lock_acquire 1 >/dev/null 2>&1; echo ready >"$W/holder.ready"; sleep 30' _ "$TOOL_DIR"
holder=$!
wait_for 50 test -e "$W/holder.ready"
( . "$TOOL_DIR/lib.sh"; . "$W/config.sh"; gr_lock_acquire 1 >"$W/second.out" 2>&1; echo $? >"$W/rc" )
check_eq "a lock whose owner is alive is not taken (exit status 2 after the wait)" "2" "$(cat "$W/rc")"
check "... and the message names the owner" grep -q "holder" "$W/second.out"
check "... and the lock directory is intact" test -f "$LOCK/owner"
kill -TERM "$holder" 2>/dev/null
kill -KILL "$holder" 2>/dev/null
wait "$holder" 2>/dev/null
# the holder is dead now but could not clean up: its lock is stale
( . "$TOOL_DIR/lib.sh"; . "$W/config.sh"; gr_lock_acquire 2 >"$W/stale.out" 2>&1; echo $? >"$W/rc"; gr_lock_release >/dev/null 2>&1 )
check_eq "a lock whose owner is provably dead is reclaimed" "0" "$(cat "$W/rc")"
check "... loudly" grep -q "RECLAIMING STALE LOCK" "$W/stale.out"
check "... and the stale directory is gone" test ! -e "$LOCK"

# an owner pid that has been reused by another process (the start time differs)
sleep 30 &
reused=$!
mkdir -p "$LOCK"
printf 'pid=%s\nlstart=Thu Jan  1 00:00:00 1970\ntool=old\n' "$reused" >"$LOCK/owner"
( . "$TOOL_DIR/lib.sh"; . "$W/config.sh"; gr_lock_acquire 2 >"$W/reuse.out" 2>&1; echo $? >"$W/rc"; gr_lock_release >/dev/null 2>&1 )
check_eq "a live pid with another start time is not the owner: the lock is reclaimed" "0" "$(cat "$W/rc")"
# the same pid with the right start time is alive
mkdir -p "$LOCK"
printf 'pid=%s\nlstart=%s\ntool=old\n' "$reused" "$(LC_ALL=C ps -o lstart= -p "$reused" | sed 's/^ *//;s/ *$//')" >"$LOCK/owner"
( . "$TOOL_DIR/lib.sh"; . "$W/config.sh"; gr_lock_acquire 1 >"$W/live.out" 2>&1; echo $? >"$W/rc" )
check_eq "the same pid with the recorded start time is alive: not reclaimed" "2" "$(cat "$W/rc")"
check "... the lock is still there" test -f "$LOCK/owner"
kill "$reused" 2>/dev/null
wait "$reused" 2>/dev/null
rm -rf "$LOCK"

# a lock made by another agent (no owner token) is never reclaimed
mkdir -p "$LOCK"
( . "$TOOL_DIR/lib.sh"; . "$W/config.sh"; gr_lock_acquire 1 >"$W/foreign.out" 2>&1; echo $? >"$W/rc" )
check_eq "a lock without an owner token (another agent's) is respected" "2" "$(cat "$W/rc")"
check "... and survives" test -d "$LOCK"
rmdir "$LOCK"

# release removes only our own lock
( . "$TOOL_DIR/lib.sh"; . "$W/config.sh"; gr_lock_acquire 1 >/dev/null 2>&1
  printf 'pid=1\nlstart=x\ntool=intruder\n' >"$LOCK/owner"   # somebody replaced the token
  gr_lock_release >"$W/rel.out" 2>&1 )
check "release leaves a lock that is no longer ours" test -d "$LOCK"
rm -rf "$LOCK"

# ---- 4. signal-safe teardown, launcher as a background job --------------------------------------------------------------
cat >"$W/stub-launcher.sh" <<'STUB'
#!/bin/bash
echo $$ >"$W/stub.pid"
ps -o pgid= -p $$ | tr -d ' ' >"$W/stub.pgid"
sleep 300 &
echo $! >"$W/stub-child.pid"
wait
STUB
chmod +x "$W/stub-launcher.sh"
cat >"$W/drv-wait.sh" <<'DRV'
. "$TOOL_DIR/lib.sh"; . "$W/config.sh"
cleanup() { echo "cleanup start" >>"$W/cleanup.log"; gr_kill_launcher; sleep "${CLEANUP_SLEEP:-0}"; echo "cleanup finished" >>"$W/cleanup.log"; return "${CLEANUP_RC:-0}"; }
gr_install_traps cleanup
gr_run_launcher 120 "$W/launcher.out" "$W/stub-launcher.sh"
echo "launcher returned" >>"$W/cleanup.log"
DRV
alive() { kill -0 "$1" 2>/dev/null; }
dead() { ! kill -0 "$1" 2>/dev/null; }

run_signal_case() { # run_signal_case <signal> <expected status> <what>
  rm -f "$W/cleanup.log" "$W/stub.pid" "$W/stub-child.pid" "$W/stub.pgid"
  start_job bash "$W/drv-wait.sh"
  local drv=$!
  wait_for 60 test -s "$W/stub-child.pid"
  kill "-$1" "$drv"
  wait "$drv"
  local rc=$?
  check_eq "$3: the exit status is preserved ($2)" "$2" "$rc"
  check "$3: cleanup ran" grep -q "cleanup finished" "$W/cleanup.log"
  check "$3: the driver did not go on after the interrupted wait" test "$(grep -c 'launcher returned' "$W/cleanup.log")" -eq 0
  wait_for 30 dead "$(cat "$W/stub.pid")"
  check "$3: the launcher is gone" dead "$(cat "$W/stub.pid")"
  check "$3: so is the child in its process group" dead "$(cat "$W/stub-child.pid")"
}
run_signal_case TERM 143 "SIGTERM during the launcher wait"
run_signal_case INT 130 "SIGINT during the launcher wait"
run_signal_case HUP 129 "SIGHUP during the launcher wait"
run_signal_case QUIT 131 "SIGQUIT during the launcher wait"

# a second signal while cleanup is running must not cut it short
rm -f "$W/cleanup.log" "$W/stub.pid" "$W/stub-child.pid"
CLEANUP_SLEEP=2 start_job bash "$W/drv-wait.sh"
drv=$!
wait_for 60 test -s "$W/stub-child.pid"
kill -TERM "$drv"
wait_for 30 grep -q "cleanup start" "$W/cleanup.log"
kill -INT "$drv"
sleep 0.2
kill -TERM "$drv"
kill -HUP "$drv"
kill -QUIT "$drv"
wait "$drv"
rc=$?
check "a second (and third, fourth, fifth) signal during cleanup does not abort it" grep -q "cleanup finished" "$W/cleanup.log"
check_eq "... and the first signal's status is kept" "143" "$rc"

# the status of a run that finishes normally but whose cleanup failed is non-zero
rm -f "$W/cleanup.log"
cat >"$W/drv-ok.sh" <<'DRV'
. "$TOOL_DIR/lib.sh"; . "$W/config.sh"
cleanup() { return "${CLEANUP_RC:-0}"; }
gr_install_traps cleanup
exit "${BODY_RC:-0}"
DRV
CLEANUP_RC=7 bash "$W/drv-ok.sh"
check_eq "a failed cleanup turns a successful run into a failure (its status)" "7" "$?"
BODY_RC=3 CLEANUP_RC=7 bash "$W/drv-ok.sh"
check_eq "a failing run keeps its own status" "3" "$?"
bash "$W/drv-ok.sh"
check_eq "a clean run exits 0" "0" "$?"

# the launcher job: its status, its own process group, stragglers
cat >"$W/stub-quick.sh" <<'STUB'
#!/bin/bash
ps -o pgid= -p $$ | tr -d ' ' >"$W/quick.pgid"
echo $$ >"$W/quick.pid"
sleep 300 &
echo $! >"$W/quick-child.pid"
exit 5
STUB
chmod +x "$W/stub-quick.sh"
( . "$TOOL_DIR/lib.sh"; . "$W/config.sh"; gr_run_launcher 60 "$W/quick.out" "$W/stub-quick.sh"; echo $? >"$W/rc"; gr_kill_launcher )
check "the stub launcher ran" test -s "$W/quick.pid"
check_eq "the launcher's exit status is returned" "5" "$(cat "$W/rc")"
check_eq "the launcher runs in a process group of its own (pgid == pid)" "$(cat "$W/quick.pid")" "$(cat "$W/quick.pgid")"
wait_for 30 test -s "$W/quick-child.pid"
wait_for 30 dead "$(cat "$W/quick-child.pid" 2>/dev/null || echo 0)"
check "the straggler was started" test -s "$W/quick-child.pid"
check "a straggler left in the launcher's group is killed by gr_kill_launcher" dead "$(cat "$W/quick-child.pid")"
( . "$TOOL_DIR/lib.sh"; . "$W/config.sh"; gr_run_launcher 1 "$W/slow.out" bash -c 'sleep 30'; echo $? >"$W/rc" )
check "the launcher's time limit ends it (SIGALRM, status above 128)" test "$(cat "$W/rc")" -gt 128

summary
