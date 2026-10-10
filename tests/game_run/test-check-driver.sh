#!/usr/bin/env bash
# End-to-end tests of tests/dx7_rasterizer/game_check.sh against a fake world: the same invariants as the trace driver
# (restore on every exit path, lock discipline, leftovers, blockers, signals) plus its own registry value (SnoopInclude).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/world.sh"
world_make
trap 'world_clean' EXIT
export W TOOL_DIR
DRIVER="$(cd "$HERE/../dx7_rasterizer" && pwd)/game_check.sh"
if ! grep -q 'gr_test_hook "\$TEST_CONFIG"' "$DRIVER"; then
  echo "refusing to run: $DRIVER does not load --test-config, so it would touch the real game"
  exit 2
fi
. "$TOOL_DIR/lib.sh"
. "$W/config.sh"
world_assert_isolated
STATE="$GAME_RUN_STATE"
DLL=EEWebRasterizer.dll
printf 'OUR DLL' >"$W/$DLL"

# what the wine +loaddll/+snoop log of a good run says (the checks of game_check.sh parse it)
cat >"$W/launch-log-template" <<'LOG'
trace:loaddll:build_module Loaded L"C:\\GOG Games\\Empire Earth Gold Edition\\Empire Earth\\@DLL@.dll" at 10000000: native
trace:snoop:SNOOP_SetupDLL SNOOP_SetupDLL: name=@DLL@.dll
CALL @DLL@.PIMGetType() ret=0x1
RET @DLL@.PIMGetType() retval=72617374 ret=0x1
CALL @DLL@.PIMGetName() ret=0x1
RET @DLL@.PIMGetName() retval=10003000 ret=0x1
CALL @DLL@.PIMIsValid() ret=0x1
RET @DLL@.PIMIsValid() retval=0012ab00 ret=0x1
trace:loaddll:free_modref Unloaded module L"C:\\GOG Games\\Empire Earth Gold Edition\\Empire Earth\\@DLL@.dll" : native
trace:loaddll:build_module Loaded L"C:\\GOG Games\\Empire Earth Gold Edition\\Empire Earth\\DX7HRTnLDisplay.dll" at 11000000: native
LOG
export FAKE_DLL=EEWebRasterizer

fresh() {
  rm -rf "$STATE" "$GAME_RUN_LOCK" "$W/out" "$W/refuse-stop" "$W/launcher-hang" "$W/launcher-fail-before-game" \
    "$W/launch.log" "$W/launcher-child.pid" "$W/wine.log" "$W/snoop.value" "$W_GAME/$DLL" "$W_SUP/logs"/launch-*.log \
    "$W/readback-hang" "$W/readback.ready" "$W/readback-go"
  printf 'launcher ddraw v1' >"$W_GAME/ddraw.dll"
  printf 'run log 0' >"$W_GAME/ee-ddraw.log"
  : >"$W/ps.out"
  : >"$W_SUP/logs/launcher.log"
  gr_manifest "$W_GAME" "$W/manifest.expected"
}
same_game() { gr_manifest "$W_GAME" "$W/manifest.now" && cmp -s "$W/manifest.expected" "$W/manifest.now"; }
dead() { ! kill -0 "$1" 2>/dev/null; }
run_driver() { bash "$DRIVER" --test-config "$W/config.sh" "$W/$DLL" "$DLL"; }

# ---- 1. a good run ----------------------------------------------------------------------------------------------------------
fresh
run_driver >"$W/run1.out" 2>&1
check_eq "a good run exits 0" "0" "$?"
check "... and says PASS" grep -q "PASS: the engine loads" "$W/run1.out"
check "the game directory is what it was (the DLL is gone)" same_game
check "the registry value was not there before and is not there after" test ! -e "$W/snoop.value"
check "the lock is released and the state removed" test ! -e "$GAME_RUN_LOCK" -a ! -e "$STATE"
check "the evidence kept the manifests and the launch log" test -s "$W/out/manifest-before.txt" -a -s "$W/out/launch.log" -a -s "$W/out/manifest-after.txt"
check "the launcher ran with WINEDEBUG set by the driver (+snoop)" grep -q "WINEDEBUG=-all,+err,+loaddll,+snoop" "$W/launch.log"
check "the sha256 of version.dll was logged" grep -q "sha256 version.dll" "$W/run1.out"

# a SnoopInclude that was set before comes back
fresh
printf 'Other.*' >"$W/snoop.value"
run_driver >"$W/run1b.out" 2>&1
check_eq "a run with a SnoopInclude already set exits 0" "0" "$?"
check_eq "... and the value is back to what it was" "Other.*" "$(cat "$W/snoop.value")"

# ---- 2. ambient environment ---------------------------------------------------------------------------------------------------
fresh
EMPIRE_EARTH_SUPPORT_DIR=/nonexistent EE_GRAPHICS=dxmt RE_DIR=/elsewhere run_driver >"$W/run2.out" 2>&1
check_eq "hostile ambient variables change nothing (exit 0)" "0" "$?"
check "the launcher saw no ambient EE_GRAPHICS or support dir" grep -q "EE_GRAPHICS=unset .* SUPPORT=unset" "$W/launch.log"
check "RE_DIR did not move the lock" test ! -e /elsewhere

# an ambient GR_TEST_CONFIG is not an opt-in: a driver run without --test-config reads no config file
fresh
rm -rf "$W/stray-out" "$W/ambient-out"
printf 'OUT="%s/stray-out"\n' "$W" >"$W/stray.sh"
GAME_CHECK_OUT="$W/ambient-out" GR_TEST_CONFIG="$W/stray.sh" bash "$DRIVER" "$W/$DLL" "$DLL" >"$W/run2b.out" 2>&1
check_eq "a run with only a stray ambient GR_TEST_CONFIG still succeeds in the fake world (exit 0)" "0" "$?"
check "... and did not load the stray config (its OUT was not used)" test ! -e "$W/stray-out" -a -d "$W/ambient-out"

# ---- 3. refusals -------------------------------------------------------------------------------------------------------------------
fresh
printf x >"$W_GAME/$DLL"
gr_manifest "$W_GAME" "$W/manifest.expected"
run_driver >"$W/run3.out" 2>&1
check "our DLL already in the game directory refuses the run (non-zero)" test $? -ne 0
check "... and it is not deleted" test -f "$W_GAME/$DLL"
check "... the lock is released" test ! -e "$GAME_RUN_LOCK"
fresh
echo '  902 90000 C:\GOG Games\Empire Earth - The Art of Conquest\EE-AOC.exe' >"$W/ps.out"
run_driver >"$W/run3b.out" 2>&1
check "Art of Conquest running refuses the run (non-zero)" test $? -ne 0
check "... naming the process" grep -q "902" "$W/run3b.out"
check "... and touching nothing" same_game
fresh
echo "  903 90000 bash -c while pgrep -f 'C:.GOG Games.*Empire Earth.exe'; do sleep 1; done" >"$W/ps.out"
run_driver >"$W/run3c.out" 2>&1
check_eq "an agent's pgrep loop does not block (exit 0)" "0" "$?"
fresh
start_job bash -c '. "$1/lib.sh"; . "$W/config.sh"; GR_TOOL=holder; gr_lock_acquire 1 >/dev/null 2>&1; echo ready >"$W/holder.ready"; sleep 30' _ "$TOOL_DIR"
holder=$!
wait_for 50 test -e "$W/holder.ready"
run_driver >"$W/run3d.out" 2>&1
check_eq "a live owner of the lock: the run gives up (2)" "2" "$?"
check "... without touching the game directory" same_game
kill -KILL "$holder" 2>/dev/null
wait "$holder" 2>/dev/null
rm -f "$W/holder.ready"

# ---- 4. a game that cannot be stopped ---------------------------------------------------------------------------------------------------
fresh
touch "$W/refuse-stop"
run_driver >"$W/run4.out" 2>&1
rc=$?
check "a game that refuses to stop fails the run (non-zero: $rc)" test "$rc" -ne 0
check "... the lock is kept" test -d "$GAME_RUN_LOCK"
check "... the snapshot is kept" test -d "$STATE/snapshot"
check "... our DLL is not deleted while the game may still use it" test -f "$W_GAME/$DLL"
check "... the registry undo is kept for recovery" grep -q "SnoopInclude" "$STATE/undo.sh"
check "... the recovery commands are printed" grep -q "recover.sh" "$W/run4.out"
rm -f "$W/refuse-stop"
: >"$W/ps.out"
bash "$TOOL_DIR/recover.sh" >"$W/recover.out" 2>&1
check_eq "recover.sh restores" "0" "$?"
check "... the game directory is what it was" same_game
check "... and the registry value is gone again" test ! -e "$W/snoop.value"
check "... lock and state are gone" test ! -e "$GAME_RUN_LOCK" -a ! -e "$STATE"

# ---- 5. signals ---------------------------------------------------------------------------------------------------------------------------
fresh
touch "$W/launcher-hang"
start_job bash "$DRIVER" --test-config "$W/config.sh" "$W/$DLL" "$DLL"
drv=$!
wait_for 100 test -s "$W/launcher-child.pid"
kill -TERM "$drv"
wait "$drv" 2>/dev/null
check_eq "SIGTERM during the launcher wait: status 143" "143" "$?"
wait_for 30 dead "$(cat "$W/launcher-child.pid")"
check "... the launcher group is gone" dead "$(cat "$W/launcher-child.pid")"
check "... the game directory is restored (the DLL is gone)" same_game
check "... the registry is restored" test ! -e "$W/snoop.value"
check "... lock and state are gone" test ! -e "$GAME_RUN_LOCK" -a ! -e "$STATE"

# a signal while the driver reads back what it restored (it keeps the lock for that): the lock must not leak
fresh
touch "$W/readback-hang"
start_job bash "$DRIVER" --test-config "$W/config.sh" "$W/$DLL" "$DLL"
drv=$!
wait_for 100 test -e "$W/readback.ready"
kill -TERM "$drv"
: >"$W/readback-go"
wait "$drv" 2>/dev/null
check_eq "SIGTERM during the read-back: status 143" "143" "$?"
check "... the game directory is restored" same_game
check "... the lock is released (the window of GR_KEEP_LOCK does not leak it)" test ! -e "$GAME_RUN_LOCK"
check "... and the state is gone" test ! -e "$STATE"

# ---- 6. no shim -----------------------------------------------------------------------------------------------------------------------------
fresh
rm -f "$W_WINE_ROOT/lib/wine/i386-windows/version.dll"
run_driver >"$W/run6.out" 2>&1
check "a launch that skipped the version.dll shim fails the check (non-zero)" test $? -ne 0
check "... and the game directory is restored" same_game
printf 'wine version dll' >"$W_WINE_ROOT/lib/wine/i386-windows/version.dll"

summary
