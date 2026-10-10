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
world_assert_isolated
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
check "take: the verified-snapshot marker is recorded once the snapshot matched the manifest" test -s "$STATE/snapshot.verified"

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
check_not "... and no verified-snapshot marker" test -e "$STATE/snapshot.verified"
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
# a stray GR_TEST_CONFIG in the environment must not send recover.sh to another state directory
printf 'GAME_RUN_STATE="%s/stray-state"\n' "$W" >"$W/stray.sh"
GR_TEST_CONFIG="$W/stray.sh" bash "$TOOL_DIR/recover.sh" >"$W/recover.out" 2>&1
check_eq "recover.sh succeeds once the game is gone" "0" "$?"
check "recover: the game directory is restored" test "$(cat "$W_GAME/ddraw.dll")" = "launcher ddraw v1" -a ! -e "$W_GAME/Users/new.dat"
check "recover: the state is removed" test ! -e "$STATE"
check "recover: the lock of the dead run is reclaimed and released" test ! -e "$GAME_RUN_LOCK"
check "recover: it said it reclaimed a stale lock" grep -q "RECLAIMING STALE LOCK" "$W/recover.out"
bash "$TOOL_DIR/recover.sh" >"$W/recover2.out" 2>&1
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

# a run that is interrupted (or refused) after the state directory exists but before a verified snapshot: the game directory was
# never touched, so the teardown discards the state and releases the lock (it must not demand a restore it cannot do)
reset_game
cat >"$W/drv-early.sh" <<'DRV'
. "$TOOL_DIR/lib.sh"; . "$W/config.sh"
GR_TOOL=drv
cleanup() { gr_teardown; }
gr_install_traps cleanup
gr_lock_acquire 1 || exit 2
gr_state_begin || exit 2
exit 143
DRV
bash "$W/drv-early.sh" >"$W/early.out" 2>&1
check_eq "interrupted before a verified snapshot: the status is the interruption's" "143" "$?"
check "... the lock is released and the state discarded (nothing was touched)" test ! -e "$GAME_RUN_LOCK" -a ! -e "$STATE"

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

# a driver that keeps the lock to read back what it restored (GR_KEEP_LOCK=1) and is signalled meanwhile: nothing is left to read
# back once the process exits, so the exit path releases the lock
reset_game
cat >"$W/drv-keep.sh" <<'DRV'
. "$TOOL_DIR/lib.sh"; . "$W/config.sh"
GR_TOOL=drv
cleanup() { gr_teardown; }
gr_install_traps cleanup
gr_lock_acquire 1 || exit 2
gr_state_begin || exit 2
gr_snapshot_take || exit 2
GR_KEEP_LOCK=1
gr_teardown || exit 1
sleep 60 &
echo $! >"$W/keep.sleep"
: >"$W/keep.ready"
wait $!
DRV
rm -f "$W/keep.ready"
start_job bash "$W/drv-keep.sh"
drv=$!
wait_for 100 test -e "$W/keep.ready"
check "the lock is kept during the read-back window" test -d "$GAME_RUN_LOCK"
kill -TERM "$drv"
wait "$drv"
check_eq "SIGTERM in the read-back window: status 143" "143" "$?"
check "... the lock is released on the exit path" test ! -e "$GAME_RUN_LOCK"
kill "$(cat "$W/keep.sleep" 2>/dev/null)" 2>/dev/null

# ---- 5. a hashing run that is cut off must never lead to a deletion ----------------------------------------------------------
# fake shasum: hashes the first file it is given, then dies (xargs fails: the run was cut off midway)
mkdir -p "$W/cutoff"
cat >"$W/cutoff/shasum" <<'SHA'
#!/bin/bash
/usr/bin/shasum -a 256 "$3"
exit 1
SHA
chmod +x "$W/cutoff/shasum"
reset_game
( PATH="$W/cutoff:$PATH"; gr_manifest "$W_GAME" "$W/cut.m" ) 2>/dev/null
check_eq "manifest: a hashing run cut off midway fails" "1" "$?"
check_eq "... and leaves nothing behind, no partial manifest and no temporary file" "" "$(ls "$W" | grep '^cut\.m')"
reset_game
gr_manifest "$W_GAME" "$W/untouched.m"
PATH="$W/cutoff:$PATH" bash "$W/drv-run.sh" >"$W/run4.out" 2>&1
check_eq "teardown: a snapshot cut off while hashing ends the run with the driver's status" "2" "$?"
game_manifest "$W/after.m"
check "teardown: ... the game directory is untouched" cmp -s "$W/untouched.m" "$W/after.m"
check "teardown: ... the state is finished and the lock released, as for a run interrupted before anything was touched" test ! -e "$STATE" -a ! -e "$GAME_RUN_LOCK"
check "teardown: ... and the log says nothing was touched" grep -q "never touched" "$W/run4.out"

# a partial manifest.before and dirs.before without the marker (the snapshot was never verified): every file and directory
# the partial lists lack must stay, whether gr_restore is called directly or by recover.sh
reset_game
gr_state_begin
gr_manifest "$W_GAME" "$W/full.m"
head -2 "$W/full.m" >"$STATE/manifest.before"
printf '.\n' >"$STATE/dirs.before"
printf 'extra' >"$W_GAME/Data/c.bin"
game_manifest "$W/b.m"
gr_dirlist "$W_GAME" "$W/b.d"
gr_restore >"$W/restore4.out" 2>&1
check_eq "restore without the verified-snapshot marker returns 0: there is nothing to restore" "0" "$?"
game_manifest "$W/b2.m"
gr_dirlist "$W_GAME" "$W/b2.d"
check "... it deletes and copies no file" cmp -s "$W/b.m" "$W/b2.m"
check "... and removes no directory" cmp -s "$W/b.d" "$W/b2.d"
check "... and says that the game directory was never touched" grep -q "never touched" "$W/restore4.out"
bash "$TOOL_DIR/recover.sh" >"$W/recover3.out" 2>&1
check_eq "recover.sh on such a state succeeds" "0" "$?"
game_manifest "$W/b3.m"
check "recover: ... deletes nothing" cmp -s "$W/b.m" "$W/b3.m"
check "recover: ... and removes the state" test ! -e "$STATE"

# ---- 6. the state of one run does not leak into the next ------------------------------------------------------------------
# game-trace.sh --gate runs the whole protocol twice in one process.
mkdir -p "$W/shadow-lossy"
cat >"$W/shadow-lossy/cp" <<'CP'
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
chmod +x "$W/shadow-lossy/cp"
reset_game
gr_state_begin
gr_snapshot_take
check_eq "a verified snapshot is recorded" "1" "$GR_SNAPSHOT_OK"
gr_state_finish
check_eq "finishing the run forgets it" "0" "$GR_SNAPSHOT_OK"
reset_game
GR_SNAPSHOT_OK=1 # what an earlier run of this process left behind
gr_state_begin
check_eq "beginning a run forgets it" "0" "$GR_SNAPSHOT_OK"
GR_SNAPSHOT_OK=1
( PATH="$W/shadow-lossy:$PATH"; gr_snapshot_take >/dev/null 2>&1; echo "$GR_SNAPSHOT_OK" >"$W/ok" )
check_eq "a snapshot that fails does not leave an earlier run's verdict standing" "0" "$(cat "$W/ok")"
rm -rf "$STATE"

cat >"$W/drv-two.sh" <<'DRV'
. "$TOOL_DIR/lib.sh"; . "$W/config.sh"
GR_TOOL=drv
cleanup() { gr_teardown; }
gr_install_traps cleanup
one_run() {
  gr_lock_acquire 1 || return 2
  gr_state_begin || return 2
  gr_snapshot_take || { gr_teardown; return 3; }
  printf 'launcher ddraw v2' >"$GR_GAME/ddraw.dll"
  echo '  4242 50000 C:\GOG Games\Fake\Empire Earth.exe' >>"$FAKE_PS_FILE"
  gr_teardown
}
one_run || exit $?
: >"$W/wine.log"
PATH="$W/shadow-lossy:$PATH" one_run
echo $? >"$W/second.rc"
exit 0
DRV
reset_game
bash "$W/drv-two.sh" >"$W/two.out" 2>&1
check_eq "two runs in one process: the first completes and the game directory is back" "launcher ddraw v1" "$(cat "$W_GAME/ddraw.dll")"
check_eq "... the second, whose snapshot is incomplete, is refused (3)" "3" "$(cat "$W/second.rc" 2>/dev/null)"
check_eq "... and its teardown does not stop the game it never touched (no wineserver -k)" "0" "$(grep -c 'wineserver -k' "$W/wine.log")"
check "... the lock is released and the state discarded" test ! -e "$GAME_RUN_LOCK" -a ! -e "$STATE"

# the log line that says the game directory was never touched has one source
check_eq "the 'never touched' message is written once, by one helper" "1" "$(grep -c 'gr_log.*was never touched' "$TOOL_DIR/snapshot.sh")"

summary
