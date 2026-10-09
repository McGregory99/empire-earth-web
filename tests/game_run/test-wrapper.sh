#!/usr/bin/env bash
# Tests of the Wine wrapper that lets a driver put its DLL in place at the moment the game starts (the launcher rewrites
# the game directory on every launch), of the launcher.log check for the version.dll shim, and of swap-in.sh.
# Fake world; never touches the real game.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/world.sh"
world_make
trap 'world_clean' EXIT
export W TOOL_DIR
. "$TOOL_DIR/lib.sh"
. "$W/config.sh"
GR_TOOL=test-wrapper
SWAP_IN="$(cd "$HERE/../../tools/ddraw_trace/scripts" && pwd)/swap-in.sh"
dead_pid() { bash -c 'echo $$'; }

# ---- 1. the wrapper mirrors the Wine root ----------------------------------------------------------------------------------
printf '#!/bin/bash\necho hook-ran "$@" >>"$FAKE_WORLD/hook.log"\n' >"$W/hook.sh"
chmod +x "$W/hook.sh"
GR_OWNER_PID=$$
mkdir -p "$GAME_RUN_LOCK"
{ echo "pid=$$"; echo "lstart=$(gr_proc_lstart $$)"; echo "tool=t"; } >"$GAME_RUN_LOCK/owner"
GR_HAVE_LOCK=1
gr_make_wine_wrapper "$W/wrap dir" "$W/hook.sh" one two
wrapped="$W/wrap dir/bin/wine"
check "the wrapper is an executable file" test -x "$wrapped"
check_eq "GR_WRAPPED_WINE names it" "$wrapped" "${GR_WRAPPED_WINE:-}"
# the launcher's own lookup (apply-launch-patches.sh install_version_shim): <dirname wine>/../lib/wine/i386-windows/version.dll
launcher_orig="$(cd "$(dirname "$wrapped")/.." 2>/dev/null && pwd -P)/lib/wine/i386-windows/version.dll"
check "the launcher finds Wine's version.dll through the wrapper ($launcher_orig)" test -f "$launcher_orig"
check "wineserver sits next to the wrapper (WINESERVER=\$wine_dir/wineserver)" test -x "$W/wrap dir/bin/wineserver"
check "wineboot and the other tools of Wine's bin are reachable" test -x "$W/wrap dir/bin/wineboot"
check "share is mirrored too" test -e "$W/wrap dir/share"
: >"$W/wine.log"
"$W/wrap dir/bin/wineboot"
check "a mirrored tool runs the real one" grep -q wineboot "$W/wine.log"
"$wrapped" reg add 'HKCU\Software\X' /v a /d b
check "an ordinary call goes through to the real wine, arguments intact" grep -qF 'wine reg add HKCU\Software\X /v a /d b' "$W/wine.log"
check_not "... and does not run the hook" test -e "$W/hook.log"

# ---- 2. `start /unix` runs the hook only while the driver holds the lock -----------------------------------------------------
: >"$W/ps.out"
"$wrapped" start /unix "$W_GAME/Empire Earth.exe"
check_eq "start /unix with the driver alive: the real wine is called" "1" "$(grep -c 'wine start /unix' "$W/wine.log")"
check "... after the hook ran, with its arguments" grep -q 'hook-ran one two' "$W/hook.log"
check "... and the fake game started" grep -q 'Empire Earth.exe' "$W/ps.out"

rm -f "$W/hook.log"
: >"$W/wine.log"
: >"$W/ps.out"
# the driver is dead: an orphaned launcher must not swap in or start the game
dead=$(dead_pid)
printf '#!/bin/bash\nexit 0\n' >/dev/null
( GR_OWNER_PID=$dead; export GR_OWNER_PID; . "$TOOL_DIR/lib.sh"; . "$W/config.sh"; GR_OWNER_PID=$dead
  gr_make_wine_wrapper "$W/wrap dead" "$W/hook.sh" )
"$W/wrap dead/bin/wine" start /unix "$W_GAME/Empire Earth.exe" >"$W/dead.out" 2>&1
check_eq "an orphaned launcher is refused when the driver is dead (exit 97)" "97" "$?"
check_not "... the hook did not run" test -e "$W/hook.log"
check_eq "... the real wine was not asked to start the game" "0" "$(grep -c 'wine start' "$W/wine.log")"
check_eq "... no game started" "" "$(cat "$W/ps.out")"
check "... and the message says why" grep -q "no longer holds the game-run lock" "$W/dead.out"
# the driver's pid is alive but the lock is not (or no longer) its own
mv "$GAME_RUN_LOCK/owner" "$GAME_RUN_LOCK/owner.moved"
"$wrapped" start /unix "$W_GAME/Empire Earth.exe" >"$W/nolock.out" 2>&1
check_eq "a live driver pid without its token in the lock is refused as well" "97" "$?"
mv "$GAME_RUN_LOCK/owner.moved" "$GAME_RUN_LOCK/owner"
# a recycled pid: the driver died, another process now has its pid (it started at another time than the driver did)
sleep 30 &
other=$!
( . "$TOOL_DIR/lib.sh"; . "$W/config.sh"; GR_OWNER_PID=$other
  gr_proc_lstart() { echo "Thu Jan  1 00:00:00 1970"; }   # what the driver's start time was
  gr_make_wine_wrapper "$W/wrap reuse" "$W/hook.sh" )
printf 'pid=%s\nlstart=Thu Jan  1 00:00:00 1970\n' "$other" >"$GAME_RUN_LOCK/owner"
"$W/wrap reuse/bin/wine" start /unix "$W_GAME/Empire Earth.exe" >"$W/reuse.out" 2>&1
check_eq "a pid that was recycled by another process is not the driver (exit 97)" "97" "$?"
kill "$other" 2>/dev/null
wait "$other" 2>/dev/null
{ echo "pid=$$"; echo "lstart=$(gr_proc_lstart $$)"; echo "tool=t"; } >"$GAME_RUN_LOCK/owner"
# a hook that fails stops the start
printf '#!/bin/bash\nexit 1\n' >"$W/hook-fail.sh"
chmod +x "$W/hook-fail.sh"
gr_make_wine_wrapper "$W/wrap fail" "$W/hook-fail.sh"
: >"$W/wine.log"
"$W/wrap fail/bin/wine" start /unix "$W_GAME/Empire Earth.exe" >/dev/null 2>&1
check_eq "a failing hook refuses the start (exit 97)" "97" "$?"
check_eq "... the game is not started" "0" "$(grep -c 'wine start' "$W/wine.log")"

# ---- 3. the fake launcher through the wrapper ----------------------------------------------------------------------------------
: >"$W_SUP/logs/launcher.log"
gr_launcher_log_mark
EMPIRE_EARTH_WINE="$wrapped" "$W_LAUNCHER/scripts/launch.sh" >/dev/null 2>&1
check "launcher through the mirrored wrapper installs the version.dll shim (no warning)" gr_check_launcher_log
check "... and the check logged the sha256 of version.dll and version_eeorig.dll" test "$(gr_check_launcher_log 2>&1 | grep -c 'sha256')" -ge 2
# the bare wrapper (what the first version of the trace driver generated) loses the shim
mkdir -p "$W/bare/bin"
ln -s "$W_WINE_ROOT/bin/wineserver" "$W/bare/bin/wineserver"
cp "$wrapped" "$W/bare/bin/wine"
: >"$W_SUP/logs/launcher.log"
gr_launcher_log_mark
EMPIRE_EARTH_WINE="$W/bare/bin/wine" "$W_LAUNCHER/scripts/launch.sh" >/dev/null 2>&1
gr_check_launcher_log >"$W/shim.out" 2>&1
check_eq "a launch without the shim fails the check" "1" "$?"
check "... and the message quotes the warning" grep -q "version.dll shim not installed" "$W/shim.out"
# only lines written after the mark count
: >"$W_SUP/logs/launcher.log"
echo "[old] WARNING: version.dll shim not installed in x" >>"$W_SUP/logs/launcher.log"
gr_launcher_log_mark
echo "[new] Installed the version.dll shim in x" >>"$W_SUP/logs/launcher.log"
check "a warning from an earlier launch (before the mark) is ignored" gr_check_launcher_log
: >"$W_SUP/logs/launcher.log"
gr_launcher_log_mark
echo "[new] Launch patches applied" >>"$W_SUP/logs/launcher.log"
check_not "a launch that says nothing about the shim cannot be confirmed: fails" gr_check_launcher_log

# ---- 4. swap-in copies then renames ---------------------------------------------------------------------------------------------
reset() {
  rm -f "$W_GAME/ddraw_next.dll" "$W_GAME/ddraw_trace.cfg" "$W_GAME"/*.tmp
  printf 'launcher ddraw v1' >"$W_GAME/ddraw.dll"
  printf 'PROXY' >"$W/proxy.dll"
}
reset
bash "$SWAP_IN" "$W_GAME" "$W/proxy.dll" 'Z:\tmp\t.raw' 77
check "swap-in: our proxy is ddraw.dll" cmp -s "$W_GAME/ddraw.dll" "$W/proxy.dll"
check_eq "swap-in: the launcher's file is kept as ddraw_next.dll" "launcher ddraw v1" "$(cat "$W_GAME/ddraw_next.dll")"
check "swap-in: the config names the trace and the cap" grep -q '^DDT_MAX_PRESENTS=77$' "$W_GAME/ddraw_trace.cfg"
check_not "swap-in: no temporary file is left" ls "$W_GAME"/*.tmp
# the order of operations: never move the live ddraw.dll away (a window in which it is missing)
reset
mkdir -p "$W/shadow2"
for tool in cp mv; do
  cat >"$W/shadow2/$tool" <<SHADOW
#!/bin/bash
echo "$tool \$*" >>"\$FAKE_WORLD/ops.log"
exec /bin/$tool "\$@"
SHADOW
  chmod +x "$W/shadow2/$tool"
done
: >"$W/ops.log"
PATH="$W/shadow2:$PATH" bash "$SWAP_IN" "$W_GAME" "$W/proxy.dll" 'Z:\tmp\t.raw' 77
check_not "swap-in never renames the live ddraw.dll away" grep -qE '^mv (-f )?[^ ]*ddraw\.dll [^ ]*ddraw_next' "$W/ops.log"
check "swap-in renames a finished temporary file into place" grep -qE '^mv .*\.tmp .*ddraw' "$W/ops.log"
# a copy that fails (disk full) leaves the launcher's ddraw.dll alone
reset
cat >"$W/shadow2/cp" <<'SHADOW'
#!/bin/bash
case "$*" in *.tmp*) exit 1 ;; esac
exec /bin/cp "$@"
SHADOW
PATH="$W/shadow2:$PATH" bash "$SWAP_IN" "$W_GAME" "$W/proxy.dll" 'Z:\tmp\t.raw' 77 >/dev/null 2>&1
check "a failed copy makes swap-in fail" test $? -ne 0
check_eq "... and the launcher's ddraw.dll is untouched" "launcher ddraw v1" "$(cat "$W_GAME/ddraw.dll")"

summary
