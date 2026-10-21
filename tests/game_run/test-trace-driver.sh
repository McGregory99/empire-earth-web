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

# ---- 9. scenarios: a saved game loaded with -loadgame, started through ee-start.exe, closed with WM_CLOSE ----------------------------
# A scenario is a data file (tests/ddraw_trace/scenarios/*.env); the save lives outside the repository and outside the game
# directory, and is put in the game's Saved Games only for the run (the snapshot restore removes it again).
SCN_DIR="$W_HOME/Games/EmpireEarth-re/scenarios"
mkdir -p "$SCN_DIR" "$W/scn-bin"
printf 'SAVE-BYTES' >"$SCN_DIR/ee_p24_skirm1.ees"
save_sha="$(shasum -a 256 "$SCN_DIR/ee_p24_skirm1.ees" | cut -c1-64)"
cat >"$W/scn.env" <<SCN
SCN_NAME=skirmish-paused
SCN_ARGS='-loadgame "ee_p24_skirm1"'
SCN_SAVE=ee_p24_skirm1.ees
SCN_SAVE_SHA256=$save_sha
SCN_PRESENTS=3
SCN_ENV="EE_SIMHASH=1 EE_SIM_FIXED_MS=33"
SCN
{ cat "$W/config.sh"; cat <<CONF
START_BUILD=0
START_EXE="$W/scn-bin/ee-start.exe"
CLOSE_BUILD=0
CLOSE_EXE="$W/scn-bin/ee-close.exe"
CLOSE_GRACE=2
EXIT_WAIT=2
SCENARIO_DIR="$SCN_DIR"
CONF
} >"$W/config-scn.sh"
printf 'start' >"$W/scn-bin/ee-start.exe"
printf 'close' >"$W/scn-bin/ee-close.exe"
run_scn() { bash "$DRIVER" --test-config "$W/config-scn.sh" --scenario "$W/scn.env" "$W/proxy.dll" "$@"; }

fresh
rm -f "$W/close.log" "$W/scn-bin/ee-start.cmd"
run_scn >"$W/run9.out" 2>&1
check_eq "a scenario run exits 0" "0" "$?"
check "the game was started through the helper, not directly" grep -q "wine start /unix $W/scn-bin/ee-start.exe" "$W/wine.log"
check_eq "... the game's own path never reached start" "0" "$(grep -c 'wine start /unix .*Empire Earth.exe' "$W/wine.log")"
check "... its command file names the game and the exact arguments" \
  grep -qF 'scenario-start cmd=C:\GOG Games\Empire Earth Gold Edition\Empire Earth\Empire Earth.exe|-loadgame "ee_p24_skirm1"|' "$W/wine.log"
check "... and the save was in the game's Saved Games at that moment" grep -q 'save=SAVE-BYTES' "$W/wine.log"
check "the determinism helpers reached the launcher" grep -q "EE_SIMHASH=1 EE_SIM_FIXED_MS=33" "$W/launch.log"
check "the game was closed with the close helper (WM_CLOSE) before the prefix was stopped" test -s "$W/close.log"
check "... which was asked for Empire Earth.exe on the game's desktop" grep -q "Empire Earth.exe EmpireEarth Default" "$W/close.log"
check "the save is gone again and the game directory is what it was" same_game
check_not "... in particular no save is left in Saved Games" test -e "$W_GAME/Data/Saved Games/ee_p24_skirm1.ees"
check "the lock is released and the state removed" test ! -e "$GAME_RUN_LOCK" -a ! -e "$STATE"
check "the trace was kept" grep -q "T1 fake" "$W/out"/trace.*.txt
check "the run says how the game left" grep -q "left by itself (via wm_close)" "$W/run9.out"

fresh
touch "$W/close-needs-quit"
run_scn >"$W/run9b.out" 2>&1
check_eq "a game that ignores WM_CLOSE gets WM_QUIT (exit 0)" "0" "$?"
check "... and the run says so" grep -q "via wm_quit" "$W/run9b.out"
rm -f "$W/close-needs-quit"

fresh
touch "$W/close-ignored"
run_scn >"$W/run9c.out" 2>&1
check "a game that ignores both is stopped with the prefix's wineserver and the run is unclean (non-zero: $?)" test $? -ne 0
check "... and the game directory is restored all the same" same_game
rm -f "$W/close-ignored"

fresh
printf 'OTHER-BYTES' >"$SCN_DIR/ee_p24_skirm1.ees"
run_scn >"$W/run9d.out" 2>&1
check_eq "a save whose sha256 is not the scenario's is refused (3)" "3" "$?"
check "... naming both checksums" grep -q "$save_sha" "$W/run9d.out"
check "... before the lock is taken or anything is touched" same_game
check_not "... no game was started" grep -q "wine start" "$W/wine.log"
printf 'SAVE-BYTES' >"$SCN_DIR/ee_p24_skirm1.ees"
fresh
rm -f "$SCN_DIR/ee_p24_skirm1.ees"
run_scn >"$W/run9e.out" 2>&1
check_eq "a missing save is refused (3)" "3" "$?"
check "... saying where it should be" grep -q "$SCN_DIR/ee_p24_skirm1.ees" "$W/run9e.out"
printf 'SAVE-BYTES' >"$SCN_DIR/ee_p24_skirm1.ees"

# the user's own copy of the save is already in the game's Saved Games: the run replaces it and the restore puts it back
fresh
mkdir -p "$W_GAME/Data/Saved Games"
printf 'USERS-OWN-COPY' >"$W_GAME/Data/Saved Games/ee_p24_skirm1.ees"
gr_manifest "$W_GAME" "$W/manifest.expected"
run_scn >"$W/run9f.out" 2>&1
check_eq "a save already in the game's Saved Games: the run works (0)" "0" "$?"
check "... the scenario's bytes were the ones loaded" grep -q 'save=SAVE-BYTES' "$W/wine.log"
check "... and the user's copy is back, byte for byte" same_game
rm -rf "$W_GAME/Data/Saved Games"


# ---- 10. scenarios that settle the game first: wait for the game loop, press the pause key, check the simulation stopped, arm the proxy ---------
# The save does not come up paused, so the driver presses the scenario's keys once the simulation runs (EE_SIMHASH's tick lines in
# ee-version.log), proves from the same lines that it stopped, and only then creates the arm file the proxy waits for.
cat >"$W/scn-keys.env" <<SCN
$(cat "$W/scn.env")
SCN_KEYS="3d"
SCN
{ cat "$W/config-scn.sh"; cat <<CONF
KEY_BUILD=0
KEY_EXE="$W/scn-bin/ee-key.exe"
LOOP_WAIT=4
SETTLE_SECONDS=1
PAUSE_WINDOW=1
PRE_KEY_DELAY=3
CONF
} >"$W/config-keys.sh"
printf 'key' >"$W/scn-bin/ee-key.exe"
run_keys() { bash "$DRIVER" --test-config "$W/config-keys.sh" --scenario "$W/scn-keys.env" "$W/proxy.dll" "$@"; }

fresh
rm -f "$W/key.log" "$W/paused" "$W/not-paused"
: >"$W/ticking"
run_keys >"$W/run10.out" 2>&1
check_eq "a scenario with keys exits 0" "0" "$?"
check "the key helper was asked for the pause key (scancode 3d) on the game's desktop (the game runs on the Wine virtual desktop EmpireEarth)" grep -q "ee-key.exe --desktop EmpireEarth 3d" "$W/key.log"
check "the simulation was seen to stop" grep -q "the simulation stopped" "$W/run10.out"
t0="$(cat "$W/tick0.time")"; tk="$(sed -n 's/.* at \([0-9]*\)$/\1/p' "$W/key.log" | head -1)"
check "the key waits PRE_KEY_DELAY (3 s) after the first tick line (tick $t0, key $tk)" test "$((tk - t0))" -ge 3
check "... and the log says what it waited for" grep -q "first tick line" "$W/run10.out"
check_eq "a pause that held needs exactly one key press" "1" "$(wc -l <"$W/key.log" | tr -d ' ')"
check "the proxy was armed only after that (its trace exists)" grep -q "T1 fake" "$W/out"/trace.*.txt
check "the arm file is in the output folder, not in the game directory" test -e "$W/out/run.raw.arm"
check "the game directory is what it was (ee-version.log included)" same_game

fresh
rm -f "$W/key.log" "$W/paused"
: >"$W/ticking"
: >"$W/not-paused"
run_keys >"$W/run10b.out" 2>&1
check "a game that keeps ticking after the key fails the run (non-zero: $?)" test $? -ne 0
check "... saying the simulation did not stop" grep -q "still running" "$W/run10b.out"
check_eq "... after the key was pressed exactly twice (once more, not more)" "2" "$(wc -l <"$W/key.log" | tr -d ' ')"
check "... the log says the key is pressed again" grep -q "pressing again" "$W/run10b.out"
check "... the proxy was never armed (no trace)" bash -c "! ls '$W/out'/trace.*.txt >/dev/null 2>&1"
check "... and the game directory is restored" same_game
rm -f "$W/not-paused"

# no such desktop (the game runs on the default one): the key is pressed on the helper's own desktop instead, and the log says so
fresh
rm -f "$W/key.log" "$W/key-refused.log" "$W/paused"
: >"$W/ticking"
: >"$W/no-desktop"
run_keys >"$W/run10f.out" 2>&1
check_eq "a missing virtual desktop does not fail the run" "0" "$?"
check "... the helper was refused the named desktop once" test -s "$W/key-refused.log"
check "... and the key was pressed without --desktop" grep -q "ee-key.exe 3d" "$W/key.log"
check "... the log says it fell back" grep -q "no desktop EmpireEarth" "$W/run10f.out"
rm -f "$W/no-desktop" "$W/key-refused.log"

# the first press is lost (a toggle that did not take, a key that landed before the form took input): the second one pauses
fresh
rm -f "$W/key.log" "$W/paused"
: >"$W/ticking"
: >"$W/needs-two-presses"
run_keys >"$W/run10e.out" 2>&1
check_eq "a lost first press is retried once and the run succeeds" "0" "$?"
check_eq "... with two key presses in total" "2" "$(wc -l <"$W/key.log" | tr -d ' ')"
check "... saying the simulation was still running after the first press" grep -q "is still running" "$W/run10e.out"
check "... and that it stopped after the second" grep -q "the simulation stopped" "$W/run10e.out"
check "... the proxy was armed only then (its trace exists)" grep -q "T1 fake" "$W/out"/trace.*.txt
check "... and the game directory is restored" same_game
rm -f "$W/needs-two-presses"

fresh
rm -f "$W/key.log" "$W/paused" "$W/ticking"
run_keys >"$W/run10c.out" 2>&1
check "a game whose loop never starts fails the run (non-zero: $?)" test $? -ne 0
check "... saying so" grep -q "game loop" "$W/run10c.out"
check "... without pressing any key" test ! -e "$W/key.log"
check "... and the game directory is restored" same_game
rm -f "$W/ticking"


# an ee-version.log left by an earlier session (with tick lines in it) must not be taken for this run's game loop
fresh
rm -f "$W/key.log" "$W/paused" "$W/ticking"
printf 'ee-version: simhash: tick 0: 11111111 over 90 objects\nee-version: simhash: tick 50: 22222222 over 90 objects\n' >"$W_GAME/ee-version.log"
gr_manifest "$W_GAME" "$W/manifest.expected"
run_keys >"$W/run10d.out" 2>&1
check "a stale log with tick lines does not start the game loop for this run (non-zero: $?)" test $? -ne 0
check "... the run says the loop never started" grep -q "game loop" "$W/run10d.out"
check "... and the stale log is back, byte for byte" same_game
rm -f "$W_GAME/ee-version.log"


# ---- 12. the gate of a scenario compares the structure form when the scenario says so ------------------------------------------------------------------
# The paused game's object ids, content hashes and vertex ring phase depend on the run before the capture window and on the tick it
# was paused at; the scenario names the normalization (SCN_NORMALIZE=structure) under which two captures must be identical.
cat >"$W/scn-struct.env" <<SCN
$(cat "$W/scn.env")
SCN_NORMALIZE=structure
SCN
run_struct() { bash "$DRIVER" --test-config "$W/config-scn.sh" --scenario "$W/scn-struct.env" "$W/proxy.dll" --gate; }
fresh
rm -f "$W/id-counter"
: >"$W/vary-ids"
run_struct >"$W/run12.out" 2>&1
check_eq "two captures that differ only in ids and hashes pass the structure gate (0)" "0" "$?"
check "... and the log says which form made them equal" grep -q "identical in structure form" "$W/run12.out"
check "... after reporting the raw difference" grep -q "traces differ raw" "$W/run12.out"
fresh
rm -f "$W/id-counter"
run_scn --gate >"$W/run12b.out" 2>&1
check "the same two captures fail a scenario gate that does not ask for the structure form (non-zero: $?)" test $? -ne 0
check "... saying they differ" grep -q "traces DIFFER" "$W/run12b.out"
rm -f "$W/vary-ids" "$W/id-counter"


run_flags() { bash "$DRIVER" --test-config "$W/config.sh" "$@" "$W/proxy.dll"; }
# ---- 13. --data: the proxy also writes the data file (P2.10.2); --out-dir: the captures go outside the repository -------------------------------------
# Default behaviour stays what it was: no DDT_DATA in the config, no data file.  --data names the raw trace path as the data prefix in
# ddraw_trace.cfg (the launcher scrubs the environment, so the config is the only way in); DDT_DATA_MAX from the caller is passed on.
fresh
run_driver >"$W/run13a.out" 2>&1
check_eq "default run: exit 0" "0" "$?"
check_not "default run: the config names no data file" grep -q DDT_DATA "$W/cfg.seen"
check_not "default run: no data file is saved" bash -c "ls '$W/out'/*.data >/dev/null 2>&1"
fresh
run_flags --data >"$W/run13b.out" 2>&1
check_eq "a --data run exits 0" "0" "$?"
check "--data: the config names the raw trace path as the data prefix" grep -qE '^DDT_DATA=Z:.*run\.raw$' "$W/cfg.seen"
check_not "--data: no cap in the config unless DDT_DATA_MAX is set" grep -q DDT_DATA_MAX "$W/cfg.seen"
check "--data: the data file is saved next to the trace" bash -c "cat '$W/out'/trace.*.data | grep -q FAKEDATA"
check "--data: the game directory is byte-for-byte what it was" same_game
fresh
DDT_DATA_MAX=123456 run_flags --data >"$W/run13c.out" 2>&1
check "--data with DDT_DATA_MAX: the cap reaches the config" grep -qxF 'DDT_DATA_MAX=123456' "$W/cfg.seen"
fresh
DDT_DATA_MAX=123456 run_driver >"$W/run13d.out" 2>&1
check_not "DDT_DATA_MAX without --data configures nothing" grep -q DDT_DATA "$W/cfg.seen"
fresh
run_flags --data --out-dir "$W/elsewhere" >"$W/run13e.out" 2>&1
check_eq "--out-dir run exits 0" "0" "$?"
check "--out-dir: trace and data are in that folder" bash -c "ls '$W/elsewhere'/trace.*.txt '$W/elsewhere'/trace.*.data >/dev/null 2>&1"
check "--out-dir: the raw trace marker is kept there (.done)" test -e "$W/elsewhere/run.raw.done"
rm -rf "$W/elsewhere"

summary
