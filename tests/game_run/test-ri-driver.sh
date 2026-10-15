#!/usr/bin/env bash
# End-to-end tests of tools/game_run/scripts/runtime-inventory.sh against a fake world (fake launcher, wine, ps and game): the
# invariants that make a run safe -- the Wine debug values are restored on every exit path, the game directory comes back
# byte for byte, lock discipline, the run budget, the log-size guard -- and that the run produces its evidence.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/world.sh"
world_make
trap 'world_clean' EXIT
export W TOOL_DIR
DRIVER="$(cd "$HERE/../../tools/game_run/scripts" && pwd)/runtime-inventory.sh"
if [ ! -f "$DRIVER" ]; then
  echo "FAIL: $DRIVER does not exist"
  exit 1
fi
# These tests run the driver against a fake world through --test-config.  A driver that does not load that file would run
# against the REAL game, lock and launcher: refuse before starting anything.
if ! grep -q 'gr_test_hook "\$TEST_CONFIG"' "$DRIVER"; then
  echo "refusing to run: $DRIVER does not load --test-config, so it would touch the real game"
  exit 2
fi
. "$TOOL_DIR/lib.sh"
. "$W/config.sh"
world_assert_isolated
STATE="$GAME_RUN_STATE"

# a game directory whose binaries are valid PE files (the driver reads their import tables)
python3 -I "$HERE/ri_fixture.py" "$W/fixture" || { echo "FAIL: cannot write the fixtures"; exit 2; }
cp "$W/fixture/game/Empire Earth.exe" "$W/fixture/game/Low-Level Engine.dll" "$W/fixture/game/Mss32.dll" "$W_GAME/"
cp "$W/fixture/relay.log" "$W/launch-log-template"
printf 'FAKE CLOSE' >"$W/ee-close.exe"
cat >>"$W/config.sh" <<CONF
CLOSE_BUILD=0
CLOSE_EXE="$W/ee-close.exe"
IDLE_SECONDS=1
MENU_WAIT=1
MENU_SETTLE=0
CAP_POLL=1
CLOSE_GRACE=2
EXIT_WAIT=4
LOG_CAP=100000000
RUNS_FILE="$W/runs.txt"
MAX_RUNS=3
CONF
. "$W/config.sh"

fresh() {
  rm -rf "$STATE" "$GAME_RUN_LOCK" "$W/out" "$W/refuse-stop" "$W/launcher-hang" "$W/launcher-late-start" "$W/launcher-go" "$W/launcher-fail-before-game" \
    "$W/launch.log" "$W/launcher-child.pid" "$W/wine.log" "$W/close.log" "$W/close-ignored" "$W/reg-at-launch.txt" "$W/runs.txt" "$W/readback-hang" \
    "$W/readback.ready" "$W/readback-go" "$W_SUP"/logs/launch-*.log
  rm -f "$W"/reg.* "$W/snoop.value"
  printf 'stock-exclude' >"$W/reg.RelayExclude"
  printf 'stock-from' >"$W/reg.RelayFromExclude"
  printf 'launcher ddraw v1' >"$W_GAME/ddraw.dll"
  printf 'run log 0' >"$W_GAME/ee-ddraw.log"
  : >"$W/ps.out"
  : >"$W_SUP/logs/launcher.log"
  gr_manifest "$W_GAME" "$W/manifest.expected"
}
same_game() { gr_manifest "$W_GAME" "$W/manifest.now" && cmp -s "$W/manifest.expected" "$W/manifest.now"; }
regs_restored() { [ "$(cat "$W/reg.RelayExclude" 2>/dev/null)" = stock-exclude ] && [ "$(cat "$W/reg.RelayFromExclude" 2>/dev/null)" = stock-from ] && [ ! -e "$W/reg.RelayInclude" ]; }
run_driver() { bash "$DRIVER" --test-config "$W/config.sh" "$@"; }

# ---- 1. a complete run ---------------------------------------------------------------------------------------------------
fresh
run_driver --tag t1 >"$W/run1.out" 2>&1
check_eq "a complete run exits 0" "0" "$?"
check "the game directory is byte-for-byte what it was" same_game
check "the three Wine debug values are back (RelayInclude removed, the others as they were)" regs_restored
check "the lock is released and the state removed" test ! -e "$GAME_RUN_LOCK" -a ! -e "$STATE"
check "at launch RelayInclude held the game's imports" grep -q "^reg.RelayInclude=.*kernel32.CreateThread" "$W/reg-at-launch.txt"
check "at launch RelayExclude kept the stock entry and added the hot functions" grep -q "^reg.RelayExclude=stock-exclude;.*kernel32.GetLastError" "$W/reg-at-launch.txt"
check "at launch RelayFromExclude kept the stock entry" grep -q "^reg.RelayFromExclude=stock-from;" "$W/reg-at-launch.txt"
check "the launcher ran with the driver's WINEDEBUG (+relay +loaddll +tid +timestamp)" grep -q "WINEDEBUG=.*+relay.*+loaddll.*+tid.*+timestamp" "$W/launch.log"
check "the close helper was run through the game's Wine, naming the game and its virtual desktop" grep -q "close-helper .*ee-close.exe.*Empire Earth.exe.*EmpireEarth" "$W/close.log"
check "the evidence directory has the relay log, phases, summary, run.env, manifests" bash -c "cd '$W/out/t1' && test -s relay.log -a -s phases.txt -a -s summary/summary.json -a -s summary/tables.md -a -s run.env -a -s manifest-before.txt -a -s manifest-after.txt"
check "run.env says it was a clean exit by WM_CLOSE and names the profile" bash -c "grep -q '^clean_exit=1' '$W/out/t1/run.env' && grep -q '^exit_via=wm_close' '$W/out/t1/run.env' && grep -q '^profile=broad' '$W/out/t1/run.env'"
check "phases.txt has the five markers" bash -c "for p in launch menu idle_start idle_end exited; do grep -q \"^\$p \" '$W/out/t1/phases.txt' || exit 1; done"
check "the new launcher log was moved out of the support logs" bash -c "! ls '$W_SUP'/logs/launch-*.log >/dev/null 2>&1"
check "the run was counted" test "$(wc -l <"$W/runs.txt" | tr -d ' ')" = 1
check "the sha256 of version.dll was logged" grep -q "sha256 version.dll" "$W/run1.out"

# ---- 2. the game ignores the close request ---------------------------------------------------------------------------------
fresh
touch "$W/close-ignored"
run_driver --tag t2 >"$W/run2.out" 2>&1
check_eq "a game that ignores the close request still gives a finished run (exit 0)" "0" "$?"
check "... which says so" grep -q "UNCLEAN" "$W/run2.out"
check "... run.env records clean_exit=0, exit_via=killed" bash -c "grep -q '^clean_exit=0' '$W/out/t2/run.env' && grep -q '^exit_via=killed' '$W/out/t2/run.env'"
check "... the game directory" same_game
check "... the debug values" regs_restored
check "... lock and state" test ! -e "$GAME_RUN_LOCK" -a ! -e "$STATE"

# WM_CLOSE is not enough, WM_QUIT is
fresh
touch "$W/close-needs-quit"
run_driver --tag t2b >"$W/run2b.out" 2>&1
check_eq "a game that needs WM_QUIT still ends cleanly (exit 0)" "0" "$?"
check "... the helper was run a second time with --quit" grep -q "close-helper .*ee-close.exe --quit" "$W/close.log"
check "... run.env says clean_exit=1, exit_via=wm_quit" bash -c "grep -q '^clean_exit=1' '$W/out/t2b/run.env' && grep -q '^exit_via=wm_quit' '$W/out/t2b/run.env'"
rm -f "$W/close-needs-quit"

# ---- 3. the timers profile ------------------------------------------------------------------------------------------------------
fresh
run_driver --tag t3 --profile timers >"$W/run3.out" 2>&1
check_eq "the timers profile runs (exit 0)" "0" "$?"
check "its RelayInclude traces the timing functions" grep -q "^reg.RelayInclude=.*QueryPerformanceCounter" "$W/reg-at-launch.txt"
check "... and not the registry functions" bash -c "! grep '^reg.RelayInclude=' '$W/reg-at-launch.txt' | grep -q RegOpenKeyExA"
check "... run.env names the profile" grep -q '^profile=timers' "$W/out/t3/run.env"
check "... the launcher got the light WINEDEBUG (relay, loaddll, tid, timestamp; no component channels)" bash -c "grep -q 'WINEDEBUG=-all,+err,+relay,+loaddll,+tid,+timestamp ' '$W/launch.log' && ! grep -q '+dinput' '$W/launch.log'"
check "an unknown profile is refused before anything is taken (2)" bash -c "bash '$DRIVER' --test-config '$W/config.sh' --profile nonsense >'$W/run3b.out' 2>&1; test \$? -eq 2 -a ! -e '$GAME_RUN_LOCK' -a ! -e '$STATE'"

# ---- 4. budget, refusals, the environment ------------------------------------------------------------------------------------
fresh
printf 'a\nb\nc\n' >"$W/runs.txt"
run_driver --tag t4 >"$W/run4.out" 2>&1
check_eq "after MAX_RUNS runs the next is refused (3)" "3" "$?"
check "... the game directory" same_game
check "... the debug values" regs_restored
check "... the lock is released" test ! -e "$GAME_RUN_LOCK"
check "... and the output says why" grep -q "run budget" "$W/run4.out"

fresh
echo '  902 90000 C:\GOG Games\Empire Earth - The Art of Conquest\EE-AOC.exe' >"$W/ps.out"
run_driver --tag t4b >"$W/run4b.out" 2>&1
check_eq "Art of Conquest running blocks the run (3)" "3" "$?"
check "... naming the process" grep -q "902" "$W/run4b.out"
check "... nothing was touched" same_game

fresh
echo "  903 90000 bash -c while pgrep -f 'C:.GOG Games.*Empire Earth.exe'; do sleep 1; done" >"$W/ps.out"
run_driver --tag t4c >"$W/run4c.out" 2>&1
check_eq "an agent's pgrep loop does not block (exit 0)" "0" "$?"

fresh
WINEDEBUG=+all WINEPREFIX=/nonexistent RE_DIR=/elsewhere EE_GRAPHICS=dxmt run_driver --tag t4d >"$W/run4d.out" 2>&1
check_eq "hostile ambient variables change nothing (exit 0)" "0" "$?"
check "the launcher got the driver's WINEDEBUG, not the ambient one" bash -c "grep -q 'WINEDEBUG=.*+relay' '$W/launch.log' && ! grep -q 'WINEDEBUG=+all' '$W/launch.log' && grep -q 'EE_GRAPHICS=unset' '$W/launch.log'"
check "RE_DIR did not move anything" test ! -e /elsewhere

fresh
start_job bash -c '. "$1/lib.sh"; . "$W/config.sh"; GR_TOOL=holder; gr_lock_acquire 1 >/dev/null 2>&1; echo ready >"$W/holder.ready"; sleep 30' _ "$TOOL_DIR"
holder=$!
wait_for 50 test -e "$W/holder.ready"
run_driver --tag t4e >"$W/run4e.out" 2>&1
check_eq "a live owner of the lock: the run gives up (2)" "2" "$?"
check "... the game directory" same_game
check "... the debug values" regs_restored
kill -KILL "$holder" 2>/dev/null
wait "$holder" 2>/dev/null
rm -f "$W/holder.ready"

# ---- 5. the log-size guard ---------------------------------------------------------------------------------------------------------
fresh
LOG_CAP_SAVE=100000000
sed -i.bak 's/^LOG_CAP=.*/LOG_CAP=100/' "$W/config.sh"
run_driver --tag t5 >"$W/run5.out" 2>&1
rc=$?
check "a log that grows past the cap ends the run (non-zero: $rc)" test "$rc" -ne 0
check "... and says so" grep -q "log size" "$W/run5.out"
check "... the game directory is restored" same_game
check "... the debug values are restored" regs_restored
check "... lock and state are gone" test ! -e "$GAME_RUN_LOCK" -a ! -e "$STATE"
sed -i.bak "s/^LOG_CAP=.*/LOG_CAP=$LOG_CAP_SAVE/" "$W/config.sh"

# ---- 6. a game that cannot be stopped ----------------------------------------------------------------------------------------------
fresh
touch "$W/refuse-stop" "$W/close-ignored"
run_driver --tag t6 >"$W/run6.out" 2>&1
rc=$?
check "a game that refuses to stop fails the run (non-zero: $rc)" test "$rc" -ne 0
check "... the lock is kept" test -d "$GAME_RUN_LOCK"
check "... the snapshot is kept" test -d "$STATE/snapshot"
check "... the recovery commands were printed" grep -q "recover.sh" "$W/run6.out"
rm -f "$W/refuse-stop"
: >"$W/ps.out"
bash "$TOOL_DIR/recover.sh" >"$W/recover.out" 2>&1
check_eq "recover.sh then restores" "0" "$?"
check "... the game directory is what it was" same_game
check "... the debug values are back (the undo steps ran)" regs_restored
check "... lock and state are gone" test ! -e "$GAME_RUN_LOCK" -a ! -e "$STATE"

# ---- 7. a signal during the launch --------------------------------------------------------------------------------------------------
fresh
touch "$W/launcher-hang"
start_job bash "$DRIVER" --test-config "$W/config.sh" --tag t7
drv=$!
wait_for 100 test -s "$W/launcher-child.pid"
kill -TERM "$drv"
wait "$drv" 2>/dev/null
check_eq "SIGTERM during the launcher wait: status 143" "143" "$?"
check "... the game directory is restored" same_game
check "... the debug values are restored" regs_restored
check "... the lock and the state are gone" test ! -e "$GAME_RUN_LOCK" -a ! -e "$STATE"

summary
