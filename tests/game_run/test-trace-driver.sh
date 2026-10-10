#!/usr/bin/env bash
# End-to-end tests of tools/ddraw_trace/scripts/game-trace.sh against a fake world (fake launcher, wine, ps and game): the
# invariants that make a run safe -- restore on every exit path, lock discipline, leftovers, orphaned launchers, signals.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/world.sh"
world_make
trap 'world_clean' EXIT
export W TOOL_DIR
DRIVER="$(cd "$HERE/../../tools/ddraw_trace/scripts" && pwd)/game-trace.sh"
# These tests run the driver against a fake world through --test-config.  A driver that does not load that file would
# run against the REAL game, lock and launcher: refuse before starting anything.
if ! grep -q 'gr_test_hook "\$TEST_CONFIG"' "$DRIVER"; then
  echo "refusing to run: $DRIVER does not load --test-config, so it would touch the real game"
  exit 2
fi
. "$TOOL_DIR/lib.sh"
. "$W/config.sh"
world_assert_isolated
STATE="$GAME_RUN_STATE"
printf 'PROXY' >"$W/proxy.dll"

fresh() {
  rm -rf "$STATE" "$GAME_RUN_LOCK" "$W/out" "$W/refuse-stop" "$W/launcher-hang" "$W/launcher-late-start" "$W/launcher-go" "$W/launcher-fail-before-game" \
    "$W/launch.log" "$W/launcher-child.pid" "$W/wine.log" "$W_GAME/ddraw_next.dll" "$W_GAME/ddraw_trace.cfg"
  printf 'launcher ddraw v1' >"$W_GAME/ddraw.dll"
  printf 'run log 0' >"$W_GAME/ee-ddraw.log"
  : >"$W/ps.out"
  : >"$W_SUP/logs/launcher.log"
  gr_manifest "$W_GAME" "$W/manifest.expected"
}
same_game() { gr_manifest "$W_GAME" "$W/manifest.now" && cmp -s "$W/manifest.expected" "$W/manifest.now"; }
dead() { ! kill -0 "$1" 2>/dev/null; }
run_driver() { bash "$DRIVER" --test-config "$W/config.sh" "$W/proxy.dll" "$@"; }

# ---- 1. a complete run ----------------------------------------------------------------------------------------------------
fresh
run_driver >"$W/run1.out" 2>&1
check_eq "a complete run exits 0" "0" "$?"
check "the trace of the fake proxy was saved" grep -q "T1 fake" "$W/out"/trace.*.txt
check "the game directory is byte-for-byte what it was" same_game
check "the lock is released and the state removed" test ! -e "$GAME_RUN_LOCK" -a ! -e "$STATE"
check "the game was stopped with the prefix's wineserver" grep -q "wineserver -k (WINEPREFIX=$W_PREFIX)" "$W/wine.log"
check "the launcher got the mirrored wrapper as its wine" grep -q "EMPIRE_EARTH_WINE=.*/wrap/bin/wine" "$W/launch.log"
check "the shim was installed through the wrapper (launcher.log)" grep -q "Installed the version.dll shim" "$W_SUP/logs/launcher.log"
check "the sha256 of version.dll was logged" grep -q "sha256 version.dll" "$W/run1.out"
check "the driver set its own EE_* switches" grep -q "EE_BIG_ARMIES=0" "$W/launch.log"

# ---- 2. ambient environment does not leak into the launcher ------------------------------------------------------------------------
fresh
EMPIRE_EARTH_SUPPORT_DIR=/nonexistent EE_GRAPHICS=dxmt WINEDEBUG=+all RE_DIR=/elsewhere run_driver >"$W/run2.out" 2>&1
check_eq "a run with hostile ambient variables still succeeds" "0" "$?"
check "the launcher saw no ambient WINEDEBUG, EE_GRAPHICS or support dir" grep -q "WINEDEBUG=unset EE_GRAPHICS=unset .* SUPPORT=unset" "$W/launch.log"
check "the lock did not move with RE_DIR" test ! -e /elsewhere

# ---- 3. leftovers, config, blockers ---------------------------------------------------------------------------------------------
fresh
printf x >"$W_GAME/ddraw_next.dll"
gr_manifest "$W_GAME" "$W/manifest.expected"
run_driver >"$W/run3.out" 2>&1
check_eq "a leftover ddraw_next.dll refuses the run (3)" "3" "$?"
check "... nothing was touched" same_game
check "... the lock is released again" test ! -e "$GAME_RUN_LOCK"
check "... and the output says what to do" grep -q "recover.sh" "$W/run3.out"
fresh
printf '{ "base_exe": "/elsewhere/Empire Earth.exe" }\n' >"$W_SUP/config.json"
run_driver >"$W/run3b.out" 2>&1
check_eq "a config.json pointing at another game directory refuses the run (3)" "3" "$?"
printf '{ "base_exe": "%s/Empire Earth.exe", "game_dir": "%s" }\n' "$W_GAME" "$W_GAME" >"$W_SUP/config.json"
fresh
echo '  901 90000 bash -c while pgrep -f '"'"'C:.GOG Games.*Empire Earth.exe'"'"'; do sleep 1; done' >"$W/ps.out"
run_driver >"$W/run3c.out" 2>&1
check_eq "another agent's pgrep loop does not block a run" "0" "$?"
fresh
echo '  902 90000 C:\GOG Games\Empire Earth - The Art of Conquest\EE-AOC.exe' >"$W/ps.out"
run_driver >"$W/run3d.out" 2>&1
check_eq "Art of Conquest running blocks the run (3)" "3" "$?"
check "... and is named" grep -q "902" "$W/run3d.out"
check "... nothing was touched" same_game

# ---- 4. the lock ----------------------------------------------------------------------------------------------------------------------
fresh
start_job bash -c '. "$1/lib.sh"; . "$W/config.sh"; GR_TOOL=holder; gr_lock_acquire 1 >/dev/null 2>&1; echo ready >"$W/holder.ready"; sleep 30' _ "$TOOL_DIR"
holder=$!
wait_for 50 test -e "$W/holder.ready"
run_driver >"$W/run4.out" 2>&1
check_eq "a live owner of the lock makes the run give up (2)" "2" "$?"
check "... the other run's lock is untouched" test -f "$GAME_RUN_LOCK/owner"
check "... and so is the game directory" same_game
kill -KILL "$holder" 2>/dev/null
wait "$holder" 2>/dev/null
run_driver >"$W/run4b.out" 2>&1
check_eq "the lock of that dead run is reclaimed and the run goes on (0)" "0" "$?"
check "... loudly" grep -q "RECLAIMING STALE LOCK" "$W/run4b.out"
check "... and that run released its own lock" test ! -e "$GAME_RUN_LOCK"
rm -f "$W/holder.ready"

# ---- 5. a game that cannot be stopped -----------------------------------------------------------------------------------------------------
fresh
touch "$W/refuse-stop"
run_driver >"$W/run5.out" 2>&1
rc=$?
check "a game that refuses to stop fails the run (non-zero: $rc)" test "$rc" -ne 0
check "... the lock is kept" test -d "$GAME_RUN_LOCK"
check "... the snapshot is kept" test -d "$STATE/snapshot"
check "... the recovery commands were printed" grep -q "recover.sh" "$W/run5.out"
check "... and a second run refuses to start over it" bash -c "bash '$DRIVER' '$W/proxy.dll' >'$W/run5b.out' 2>&1; test \$? -ne 0"
rm -f "$W/refuse-stop"
: >"$W/ps.out"
bash "$TOOL_DIR/recover.sh" >"$W/recover.out" 2>&1
check_eq "recover.sh then restores" "0" "$?"
check "... the game directory is what it was" same_game
check "... lock and state are gone" test ! -e "$GAME_RUN_LOCK" -a ! -e "$STATE"

# ---- 6. signals -------------------------------------------------------------------------------------------------------------------------------
fresh
touch "$W/launcher-hang"
start_job bash "$DRIVER" --test-config "$W/config.sh" "$W/proxy.dll"
drv=$!
wait_for 100 test -s "$W/launcher-child.pid"
kill -TERM "$drv"
wait "$drv" 2>/dev/null
check_eq "SIGTERM during the launcher wait: status 143" "143" "$?"
wait_for 30 dead "$(cat "$W/launcher-child.pid")"
check "... the launcher's process group is gone" dead "$(cat "$W/launcher-child.pid")"
check "... the game was stopped" test -z "$(gr_game_pids)"
check "... the game directory is restored" same_game
check "... the lock and the state are gone" test ! -e "$GAME_RUN_LOCK" -a ! -e "$STATE"

fresh
touch "$W/launcher-hang"
start_job bash "$DRIVER" --test-config "$W/config.sh" "$W/proxy.dll"
drv=$!
wait_for 100 test -s "$W/launcher-child.pid"
kill -HUP "$drv"
wait "$drv" 2>/dev/null
check_eq "SIGHUP: status 129 and everything restored" "129" "$?"
check "... restored" same_game

# ---- 7. an orphaned launcher after a killed driver ---------------------------------------------------------------------------------------------
fresh
: >"$W/launcher-late-start"      # the launcher waits for launcher-go before it asks for the game
start_job bash "$DRIVER" --test-config "$W/config.sh" "$W/proxy.dll"
drv=$!
wait_for 100 test -s "$W/launch.log"   # the launcher is running (and held at its gate)
kill -KILL "$drv"
wait "$drv" 2>/dev/null
check "the killed driver left its lock behind" test -d "$GAME_RUN_LOCK"
: >"$W/launcher-go"                    # now the orphan asks for the game
wait_for 100 grep -q "wine start exit status" "$W/launch.log"
check "the orphaned launcher got an answer to its request to start the game" grep -q "wine start exit status" "$W/launch.log"
check_eq "the orphaned launcher could not start the game (wrapper refused)" "" "$(gr_game_pids)"
check "... the wrapper refused with status 97" grep -q "wine start exit status 97" "$W/launch.log"
check_eq "... and nothing was swapped in (ddraw.dll is still the launcher's)" "no" "$(grep -q PROXY "$W_GAME/ddraw.dll" && echo yes || echo no)"
rm -f "$W/launcher-late-start"
run_driver >"$W/run7.out" 2>&1
rc=$?
check "the next run refuses to start over the killed run's state (non-zero: $rc)" test "$rc" -ne 0
check "... after reclaiming the dead run's lock loudly" grep -q "RECLAIMING STALE LOCK" "$W/run7.out"
check "... and it says to run recover.sh" grep -q "recover.sh" "$W/run7.out"
bash "$TOOL_DIR/recover.sh" >"$W/recover7.out" 2>&1
check_eq "recover.sh restores after the kill" "0" "$?"
# the launcher of the killed run rewrote ddraw.dll and the log before it was killed: those are exactly what recover undoes
check "... the game directory equals the manifest taken before that run (every file, not only ddraw.dll)" same_game
run_driver >"$W/run7b.out" 2>&1
check_eq "and a run after the recovery works (0)" "0" "$?"

# ---- 8. a launch without the version.dll shim fails the run --------------------------------------------------------------------------------------
fresh
rm -f "$W_WINE_ROOT/lib/wine/i386-windows/version.dll"
run_driver >"$W/run8.out" 2>&1
check "the run fails when the launcher could not find Wine's version.dll (non-zero)" test $? -ne 0
check "... saying so" grep -q "did not install the version.dll shim" "$W/run8.out"
check "... and the game directory is restored" same_game
printf 'wine version dll' >"$W_WINE_ROOT/lib/wine/i386-windows/version.dll"

summary
