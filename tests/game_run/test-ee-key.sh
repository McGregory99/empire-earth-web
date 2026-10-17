#!/usr/bin/env bash
# Test of tools/game_run/scripts/ee-key.c under Wine, in the project's own test prefix (never the game's): it must press the
# scancodes it is given, in order, on the desktop it is told to use (the game reads its keyboard from DirectInput, which Wine
# feeds from the same low-level events as SendInput).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/world.sh"
SRC="$(cd "$HERE/../../tools/game_run/scripts" && pwd)/ee-key.c"
CC=i686-w64-mingw32-gcc
RE="${REAL_HOME}/Games/EmpireEarth-re"
WINE_ROOT="${REAL_HOME}/Library/Application Support/EmpireEarthMac/runtime/Wine Stable.app/Contents/Resources/wine"
if ! command -v "$CC" >/dev/null || [ ! -x "$WINE_ROOT/bin/wine" ] || [ ! -d "$RE/wineprefix-test" ]; then
  echo "SKIP: needs $CC, the project's Wine and its test prefix ($RE/wineprefix-test)"
  exit 0
fi
T="$(mktemp -d "${TMPDIR:-/tmp}/ee key.XXXXXX")"
export WINEPREFIX="$RE/wineprefix-test" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" MVK_CONFIG_LOG_LEVEL=0
cleanup() { "$WINE_ROOT/bin/wineserver" -k >/dev/null 2>&1; rm -rf "$T"; }
trap cleanup EXIT
wine() { "$WINE_ROOT/bin/wine" "$@" 2>/dev/null; }

check "ee-key.c exists" test -f "$SRC"
$CC -mcrtdll=msvcrt-os -O2 -Wall -Wextra -D_WIN32_WINNT=0x0600 -o "$T/ee-key.exe" "$SRC" -luser32 2>"$T/cc.err"
check_eq "ee-key.c compiles without warnings" "0" "$?"
check "... (compiler output is empty)" test ! -s "$T/cc.err"
$CC -mcrtdll=msvcrt-os -O2 -mwindows -o "$T/key_victim.exe" "$HERE/key_victim.c" -luser32 2>>"$T/cc.err"
check_eq "the test window program compiles" "0" "$?"
[ -x "$T/ee-key.exe" ] || { summary; exit 1; }

( cd "$T" && wine key_victim.exe >/dev/null 2>&1; echo $? >"$T/victim.rc" ) &
wait_for 300 test -e "$T/key_victim.ready"
check "the focused test window is up" test -e "$T/key_victim.ready"

wine "$T/ee-key.exe" 3d 23 01 >"$T/k1.out" 2>&1
check_eq "pressing three scancodes: exit 0" "0" "$?"
wait_for 100 test -e "$T/victim.rc"
check_eq "the window got them, in order (F3, H, Esc)" "3d 23 01" "$(tr -d '\r' <"$T/key_victim.out" | tr '\n' ' ' 2>/dev/null | sed 's/ $//')"
check "the helper said what it pressed" grep -q "pressed 3d" "$T/k1.out"

# --hold <release file>: the key goes down at once and stays down until the file exists (a key already down when the game's loop
# starts is what makes the game pause at its very first poll, not at a tick that depends on timing)
rm -f "$T/key_victim.out" "$T/victim.rc" "$T/key_victim.ready"
( cd "$T" && wine key_victim.exe >/dev/null 2>&1; echo $? >"$T/victim.rc" ) &
wait_for 300 test -e "$T/key_victim.ready"
wine "$T/ee-key.exe" --hold "$T/release.flag" 3d >"$T/h1.out" 2>&1 &
holder=$!
wait_for 100 test -s "$T/key_victim.out"
check_eq "--hold: the key is down before the release file exists" "3d" "$(tr -d '\r' <"$T/key_victim.out" 2>/dev/null | head -1)"
check "... and the helper is still running" kill -0 "$holder"
: >"$T/release.flag"
wait "$holder"
check_eq "... it ends with exit 0 once the file exists" "0" "$?"
check "... and said it released the key" grep -q "released 3d" "$T/h1.out"
wine "$T/ee-key.exe" 01 >/dev/null 2>&1
wait_for 100 test -e "$T/victim.rc"
wine "$T/ee-key.exe" --hold "$T/never.flag" >"$T/h2.out" 2>&1
check_eq "--hold without a scancode: exit 2" "2" "$?"

wine "$T/ee-key.exe" >"$T/k2.out" 2>&1
check "no scancode: usage and a non-zero exit" test $? -ne 0
wine "$T/ee-key.exe" zz >"$T/k3.out" 2>&1
check_eq "a scancode that is not hex: exit 2" "2" "$?"
wine "$T/ee-key.exe" --desktop NoSuchDesktop 3d >"$T/k4.out" 2>&1
check_eq "a desktop that does not exist: exit 3" "3" "$?"
summary
