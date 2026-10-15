#!/usr/bin/env bash
# Test of tools/game_run/scripts/ee-close.c under Wine, in the project's own test prefix (never the game's): it must find the
# window of a named program, also on a desktop of its own, post WM_CLOSE, and leave other programs alone.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/world.sh"
SRC="$(cd "$HERE/../../tools/game_run/scripts" && pwd)/ee-close.c"
CC=i686-w64-mingw32-gcc
RE="${REAL_HOME}/Games/EmpireEarth-re"
WINE_ROOT="${REAL_HOME}/Library/Application Support/EmpireEarthMac/runtime/Wine Stable.app/Contents/Resources/wine"
if ! command -v "$CC" >/dev/null || [ ! -x "$WINE_ROOT/bin/wine" ] || [ ! -d "$RE/wineprefix-test" ]; then
  echo "SKIP: needs $CC, the project's Wine and its test prefix ($RE/wineprefix-test)"
  exit 0
fi
T="$(mktemp -d "${TMPDIR:-/tmp}/ri close.XXXXXX")"
export WINEPREFIX="$RE/wineprefix-test" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" MVK_CONFIG_LOG_LEVEL=0
cleanup() { "$WINE_ROOT/bin/wineserver" -k >/dev/null 2>&1; rm -rf "$T"; }
trap cleanup EXIT
wine() { "$WINE_ROOT/bin/wine" "$@"; }

check "ee-close.c exists" test -f "$SRC"
$CC -mcrtdll=msvcrt-os -O2 -Wall -Wextra -D_WIN32_WINNT=0x0600 -o "$T/ee-close.exe" "$SRC" -luser32 2>"$T/cc.err"
check_eq "ee-close.c compiles without warnings" "0" "$?"
check "... (compiler output is empty)" test ! -s "$T/cc.err"
$CC -mcrtdll=msvcrt-os -O2 -mwindows -o "$T/ri_victim.exe" "$HERE/ri_victim.c" -luser32 2>>"$T/cc.err"
check_eq "the test window program compiles" "0" "$?"
[ -x "$T/ee-close.exe" ] || { summary; exit 1; }

# the victim on a desktop of its own
( cd "$T" && wine ri_victim.exe EETestDesktop >/dev/null 2>&1; echo $? >"$T/victim.rc" ) &
vpid=$!
wait_for 300 test -e "$T/victim.ready"
check "the victim window is up" test -e "$T/victim.ready"

wine "$T/ee-close.exe" "other.exe" EETestDesktop Default >"$T/close0.out" 2>&1
check_eq "a program name that matches nothing: exit 3, nothing posted" "3" "$?"
sleep 1
check "... the victim is still running" test ! -e "$T/victim.rc"

wine "$T/ee-close.exe" "Ri_Victim.EXE" Default >"$T/close1.out" 2>&1
check_eq "the right program on the wrong desktop: exit 3" "3" "$?"
check "... the victim is still running" test ! -e "$T/victim.rc"

wine "$T/ee-close.exe" "Ri_Victim.EXE" Default EETestDesktop >"$T/close2.out" 2>&1
check_eq "the right program (case-insensitive) on its desktop: exit 0" "0" "$?"
wait_for 100 test -e "$T/victim.rc"
check_eq "the victim got WM_CLOSE and ended its loop (exit code 7)" "7" "$(cat "$T/victim.rc" 2>/dev/null)"
check "the helper said which window it closed" grep -q "posted WM_CLOSE" "$T/close2.out"

# --quit posts WM_QUIT to the window's thread
mkdir -p "$T/v2"
( cd "$T/v2" && wine ../ri_victim.exe EETestDesktop2 >/dev/null 2>&1; echo $? >"$T/victim2.rc" ) &
vpid2=$!
wait_for 300 test -e "$T/v2/victim.ready"
wine "$T/ee-close.exe" --quit "ri_victim.exe" EETestDesktop2 >"$T/close4.out" 2>&1
check_eq "--quit: the right program on its desktop: exit 0" "0" "$?"
check "... it says WM_QUIT" grep -q "posted WM_QUIT" "$T/close4.out"
wait_for 100 test -e "$T/victim2.rc"
check_eq "... the victim's message loop ended (exit code 0, not the WM_CLOSE path's 7)" "0" "$(cat "$T/victim2.rc" 2>/dev/null)"
wine "$T/ee-close.exe" --quit "ri_victim.exe" EETestDesktop2 >"$T/close5.out" 2>&1
check_eq "--quit with nothing left to quit: exit 3" "3" "$?"

wine "$T/ee-close.exe" >"$T/close3.out" 2>&1
check "no arguments: usage and a non-zero exit" test $? -ne 0
summary
