#!/usr/bin/env bash
# Tests of the snapshot / restore part of the shared game-run library (tools/game_run/snapshot.sh): a verified snapshot,
# a restore that is exact and never deletes a file that was there before, refusal on leftovers, and the teardown that keeps
# the lock and the snapshot when the game cannot be stopped.  Fake world; never touches the real game.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/world.sh"
world_make
trap 'world_clean' EXIT
export W TOOL_DIR
. "$TOOL_DIR/lib.sh"
. "$W/config.sh"
GR_TOOL=test-snapshot
STATE="$GAME_RUN_STATE"

reset_game() {
  rm -rf "$W_GAME" "$STATE"
  mkdir -p "$W_GAME/Data" "$W_GAME/Users"
  printf 'launcher ddraw v1' >"$W_GAME/ddraw.dll"
  printf 'payload a' >"$W_GAME/Data/a.bin"
  printf 'payload b' >"$W_GAME/Data/b file.bin"
  printf 'setting=1' >"$W_GAME/Users/cfg"
  printf 'log0' >"$W_GAME/ee-ddraw.log"
  mkdir -p "$W_GAME/empty dir"
  : >"$W/ps.out"
  rm -f "$W/refuse-stop"
}
game_manifest() { gr_manifest "$W_GAME" "$1"; }

# ---- 1. manifest and snapshot ------------------------------------------------------------------------------------------
reset_game
gr_manifest "$W_GAME" "$W/m1"
check_eq "the manifest lists every file with its sha256 (5 files, spaces in names)" "5" "$(wc -l <"$W/m1" | tr -d ' ')"
check "the manifest has the path after a 64-hex hash" grep -q '^[0-9a-f]\{64\}  \./Data/b file.bin$' "$W/m1"

gr_state_begin
check "begin creates the state directory" test -d "$STATE"
gr_snapshot_take
check_eq "take: a snapshot identical to the game directory is made and verified" "0" "$?"
check "take: the manifest and directory list are recorded" test -s "$STATE/manifest.before" -a -s "$STATE/dirs.before"
gr_manifest "$STATE/snapshot" "$W/snap.m"
check "take: the snapshot has exactly the recorded manifest" cmp -s "$STATE/manifest.before" "$W/snap.m"

# a second begin finds the leftover
( . "$TOOL_DIR/lib.sh"; . "$W/config.sh"; gr_state_begin >"$W/begin2.out" 2>&1; echo $? >"$W/rc" )
check_eq "begin refuses when a previous state exists (a leftover run)" "1" "$(cat "$W/rc")"
check "... and names the leftover" grep -q "$STATE" "$W/begin2.out"

# an incomplete snapshot: the clone fails, the plain copy silently drops a file
reset_game
mkdir -p "$W/shadow"
cat >"$W/shadow/cp" <<'CP'
#!/bin/bash
# fake cp: -c (clone) fails; a plain recursive copy of the game loses one file but claims success
case " $* " in
  *" -cR "* | *" -c "*) exit 1 ;;
esac
/bin/cp "$@"
status=$?
last="${@: -1}"
[ -d "$last/Data" ] && rm -f "$last/Data/b file.bin"
exit $status
CP
chmod +x "$W/shadow/cp"
gr_state_begin
mkdir -p "$STATE/snapshot"
printf 'junk of a previous partial snapshot' >"$STATE/snapshot/junk"
( PATH="$W/shadow:$PATH"; gr_snapshot_take >"$W/take.out" 2>&1 )
check_eq "take: an incomplete snapshot is detected (non-zero status)" "1" "$?"
check "... and the message says the snapshot differs" grep -qi "differs" "$W/take.out"
check_not "... nothing partial is left behind to be trusted later" test -e "$STATE/snapshot"
# the fallback copy starts from nothing: a junk file of an earlier partial snapshot must not survive
reset_game
cat >"$W/shadow/cp" <<'CP'
#!/bin/bash
case " $* " in
  *" -cR "* | *" -c "*) exit 1 ;;
esac
exec /bin/cp "$@"
CP
gr_state_begin
mkdir -p "$STATE/snapshot"
printf 'junk of a previous partial snapshot' >"$STATE/snapshot/junk"
( PATH="$W/shadow:$PATH"; gr_snapshot_take >"$W/take2.out" 2>&1 )
check_eq "take: when cloning fails the plain copy gives a verified snapshot" "0" "$?"
check_not "... and the partial snapshot's junk was removed before copying" test -e "$STATE/snapshot/junk"
rm -rf "$STATE"

# ---- 2. restore -----------------------------------------------------------------------------------------------------------
reset_game
gr_state_begin
gr_snapshot_take
game_manifest "$W/before.m"
printf 'launcher ddraw v2' >"$W_GAME/ddraw.dll"           # rewritten by the launcher
printf 'run log' >"$W_GAME/ee-ddraw.log"
rm "$W_GAME/Users/cfg"                                    # removed
printf 'new' >"$W_GAME/Users/new.dat"                     # added
mkdir -p "$W_GAME/added dir/sub"
printf 'x' >"$W_GAME/added dir/sub/f"                     # added directory with a file
rmdir "$W_GAME/empty dir"                                 # removed directory
gr_restore >"$W/restore.out" 2>&1
check_eq "restore returns 0" "0" "$?"
game_manifest "$W/after.m"
check "restore: the manifest is the one taken before" cmp -s "$W/before.m" "$W/after.m"
check "restore: files added by the run are gone, with their directories" test ! -e "$W_GAME/Users/new.dat" -a ! -e "$W_GAME/added dir"
check "restore: an empty directory that was removed is back" test -d "$W_GAME/empty dir"
check_eq "restore: the launcher-rewritten file is back" "launcher ddraw v1" "$(cat "$W_GAME/ddraw.dll")"

# a damaged snapshot: the file is changed in the game directory and missing from the snapshot
reset_game
gr_state_begin
gr_snapshot_take
printf 'changed by the game' >"$W_GAME/Data/a.bin"
printf 'ddraw v2' >"$W_GAME/ddraw.dll"
rm "$STATE/snapshot/Data/a.bin"                           # damaged after the snapshot was verified
gr_restore >"$W/restore2.out" 2>&1
check "restore with a damaged snapshot fails (non-zero)" test $? -ne 0
check "... no live file is deleted: the changed file is still there" test -f "$W_GAME/Data/a.bin"
check_eq "... with the content the game left (it cannot be restored, so it is reported)" "changed by the game" "$(cat "$W_GAME/Data/a.bin")"
check "... the report names the file the snapshot lacks" grep -q "Data/a.bin" "$W/restore2.out"
check_eq "... the files the snapshot has were restored" "launcher ddraw v1" "$(cat "$W_GAME/ddraw.dll")"
check "... the state is kept for recovery" test -d "$STATE"

# a file that is in the game directory but not in manifest.before is "added", whatever the snapshot has
reset_game
gr_state_begin
gr_snapshot_take
printf 'only on disk' >"$W_GAME/Data/c.bin"
printf 'only in snapshot' >"$STATE/snapshot/Data/c.bin"   # a snapshot that disagrees with the manifest about this file
gr_restore >/dev/null 2>&1
check_not "an added file is deleted by absence in manifest.before, not by absence in the snapshot" test -e "$W_GAME/Data/c.bin"

# nothing is touched while the game runs
reset_game
gr_state_begin
gr_snapshot_take
printf 'changed' >"$W_GAME/Data/a.bin"
echo '  4242 50000 C:\GOG Games\Fake\Empire Earth.exe' >"$W/ps.out"
gr_restore >"$W/restore3.out" 2>&1
check_eq "restore refuses (status 3) while the game runs" "3" "$?"
check_eq "... and changes nothing" "changed" "$(cat "$W_GAME/Data/a.bin")"
: >"$W/ps.out"

# ---- 3. leftovers and the config -------------------------------------------------------------------------------------------
reset_game
check "no leftovers in a clean world" gr_check_leftovers ddraw_next.dll ddraw_trace.cfg EEWebRasterizer.dll
for name in ddraw_next.dll ddraw_trace.cfg EEWebRasterizer.dll; do
  printf x >"$W_GAME/$name"
  ( . "$TOOL_DIR/lib.sh"; . "$W/config.sh"; gr_check_leftovers ddraw_next.dll ddraw_trace.cfg EEWebRasterizer.dll >"$W/left.out" 2>&1; echo $? >"$W/rc" )
  check_eq "a leftover $name refuses the start" "1" "$(cat "$W/rc")"
  check "... and is named" grep -q "$name" "$W/left.out"
  rm -f "$W_GAME/$name"
done
mkdir -p "$STATE"
( . "$TOOL_DIR/lib.sh"; . "$W/config.sh"; gr_check_leftovers >"$W/left.out" 2>&1; echo $? >"$W/rc" )
check_eq "a previous state directory refuses the start" "1" "$(cat "$W/rc")"
rm -rf "$STATE"
check "config.json whose base_exe is under the game directory is accepted" gr_assert_config
printf '{ "base_exe": "/somewhere/else/Empire Earth.exe" }\n' >"$W_SUP/config.json"
check_not "config.json pointing at another directory is refused" gr_assert_config
printf '{ "base_exe": "%s-copy/Empire Earth.exe" }\n' "$W_GAME" >"$W_SUP/config.json"
check_not "a sibling directory with the same prefix is not 'under' the game directory" gr_assert_config
printf '{ "base_exe": "%s/Empire Earth.exe" }\n' "$W_GAME" >"$W_SUP/config.json"

# ---- 4. the teardown ---------------------------------------------------------------------------------------------------------
cat >"$W/drv-run.sh" <<'DRV'
. "$TOOL_DIR/lib.sh"; . "$W/config.sh"
GR_TOOL=drv
cleanup() { gr_teardown; }
gr_install_traps cleanup
gr_lock_acquire 1 || exit 2
gr_state_begin || exit 2
gr_snapshot_take || exit 2
printf 'launcher ddraw v2' >"$GR_GAME/ddraw.dll"
printf 'new' >"$GR_GAME/Users/new.dat"
echo '  4242 50000 C:\GOG Games\Fake\Empire Earth.exe' >>"$FAKE_PS_FILE"
exit "${BODY_RC:-0}"
DRV
reset_game
bash "$W/drv-run.sh" >"$W/run1.out" 2>&1
check_eq "teardown: a normal run ends with status 0" "0" "$?"
game_manifest "$W/after.m"
gr_manifest "$W_GAME" "$W/final.m"
check_eq "teardown: the game was stopped (nothing lists)" "" "$(gr_game_pids)"
check "teardown: the game directory is restored" test "$(cat "$W_GAME/ddraw.dll")" = "launcher ddraw v1" -a ! -e "$W_GAME/Users/new.dat"
check "teardown: the lock is released" test ! -e "$GAME_RUN_LOCK"
check "teardown: the state is removed" test ! -e "$STATE"
check "teardown: the prefix's own wineserver was used" grep -q "wineserver -k (WINEPREFIX=$W_PREFIX)" "$W/wine.log"

# a game that refuses to stop: lock and snapshot are kept, the status is non-zero, the recovery commands are printed
reset_game
: >"$W/wine.log"
touch "$W/refuse-stop"
BODY_RC=0 bash "$W/drv-run.sh" >"$W/run2.out" 2>&1
rc=$?
check "teardown: a game that cannot be stopped makes the run fail (non-zero)" test "$rc" -ne 0
check "teardown: ... the lock is KEPT" test -d "$GAME_RUN_LOCK"
check "teardown: ... the snapshot is KEPT" test -d "$STATE/snapshot"
check_eq "teardown: ... the game directory is not touched while the game runs" "launcher ddraw v2" "$(cat "$W_GAME/ddraw.dll")"
check "teardown: ... the exact recovery commands are printed" grep -q "wineserver' -k" "$W/run2.out"
check "teardown: ... including the recovery script" grep -q "recover.sh" "$W/run2.out"

# recovery: the game is stopped by hand, recover.sh restores, removes the state and (the owner being dead) the lock
rm -f "$W/refuse-stop"
: >"$W/ps.out"
GR_TEST_CONFIG="$W/config.sh" bash "$TOOL_DIR/recover.sh" >"$W/recover.out" 2>&1
check_eq "recover.sh succeeds once the game is gone" "0" "$?"
check "recover: the game directory is restored" test "$(cat "$W_GAME/ddraw.dll")" = "launcher ddraw v1" -a ! -e "$W_GAME/Users/new.dat"
check "recover: the state is removed" test ! -e "$STATE"
check "recover: the lock of the dead run is reclaimed and released" test ! -e "$GAME_RUN_LOCK"
check "recover: it said it reclaimed a stale lock" grep -q "RECLAIMING STALE LOCK" "$W/recover.out"
GR_TEST_CONFIG="$W/config.sh" bash "$TOOL_DIR/recover.sh" >"$W/recover2.out" 2>&1
check_eq "recover.sh with nothing to recover is a no-op (0)" "0" "$?"

# a restore that fails (damaged snapshot) keeps the lock and the state as well
reset_game
cat >"$W/drv-damage.sh" <<'DRV'
. "$TOOL_DIR/lib.sh"; . "$W/config.sh"
GR_TOOL=drv
cleanup() { gr_teardown; }
gr_install_traps cleanup
gr_lock_acquire 1 || exit 2
gr_state_begin || exit 2
gr_snapshot_take || exit 2
printf 'changed' >"$GR_GAME/Data/a.bin"
rm "$GAME_RUN_STATE/snapshot/Data/a.bin"
exit 0
DRV
bash "$W/drv-damage.sh" >"$W/run3.out" 2>&1
rc=$?
check "teardown: a failed restore makes the run fail" test "$rc" -ne 0
check "teardown: ... lock and snapshot are kept" test -d "$GAME_RUN_LOCK" -a -d "$STATE/snapshot"
check_eq "teardown: ... the live file was not deleted" "changed" "$(cat "$W_GAME/Data/a.bin")"
rm -rf "$GAME_RUN_LOCK" "$STATE"

# SIGTERM at any point of a run still restores and releases
reset_game
cat >"$W/drv-term.sh" <<'DRV'
. "$TOOL_DIR/lib.sh"; . "$W/config.sh"
GR_TOOL=drv
cleanup() { gr_teardown; }
gr_install_traps cleanup
gr_lock_acquire 1 || exit 2
gr_state_begin || exit 2
gr_snapshot_take || exit 2
printf 'launcher ddraw v2' >"$GR_GAME/ddraw.dll"
echo ready >"$W/term.ready"
gr_run_launcher 120 "$W/launcher.out" bash -c 'sleep 300'
DRV
rm -f "$W/term.ready"
start_job bash "$W/drv-term.sh"
drv=$!
wait_for 80 test -e "$W/term.ready"
kill -TERM "$drv"
wait "$drv"
check_eq "SIGTERM mid-run: status 143" "143" "$?"
check "SIGTERM mid-run: the directory is restored and the lock released" test "$(cat "$W_GAME/ddraw.dll")" = "launcher ddraw v1" -a ! -e "$GAME_RUN_LOCK" -a ! -e "$STATE"

summary
