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
check "... and which window had the focus (the game's, when it is used)" grep -q "title 'victim'.*\[foreground\]" "$T/k1.out"

wine "$T/ee-key.exe" >"$T/k2.out" 2>&1
check "no scancode: usage and a non-zero exit" test $? -ne 0
wine "$T/ee-key.exe" zz >"$T/k3.out" 2>&1
check_eq "a scancode that is not hex: exit 2" "2" "$?"
wine "$T/ee-key.exe" --desktop NoSuchDesktop 3d >"$T/k4.out" 2>&1
check_eq "a desktop that does not exist: exit 3" "3" "$?"
# Focus: with --window-class the helper finds the game's top-level window, tries to make it the foreground window (and says
# whether it got it) and presses only with the focus confirmed; where the system will not give it, the same key goes to the
# window as WM_KEYDOWN / WM_KEYUP messages, and the output says so.  A window that does not exist is an error (exit 6).
rm -f "$T/key_victim.ready" "$T/key_victim.out" "$T/victim.rc"
( cd "$T" && wine key_victim.exe >/dev/null 2>&1; echo $? >"$T/victim.rc" ) &
wait_for 300 test -e "$T/key_victim.ready"
wine "$T/ee-key.exe" --window-class EEKeyVictim 3d 01 >"$T/k5.out" 2>&1
check_eq "--window-class: exit 0" "0" "$?"
wait_for 100 test -e "$T/victim.rc"
check_eq "the window got the keys (F3, Esc)" "3d 01" "$(tr -d '\r' <"$T/key_victim.out" | tr '\n' ' ' 2>/dev/null | sed 's/ $//')"
check "the output says how: focus confirmed, or messages posted" grep -Eq "^ee-key: (focus confirmed|focus not obtained, posting)" "$T/k5.out"
"$WINE_ROOT/bin/wine" "$T/ee-key.exe" --window-class NoSuchClassAnywhere 3d >"$T/k6.out" 2>&1
check_eq "a window class that does not exist: exit 6" "6" "$?"
check "... with a clear message" grep -q "no visible window of class 'NoSuchClassAnywhere'" "$T/k6.out"
# Extended keys (arrows: the scancode with the 0xe0 prefix), a hold time (`<key>/<ms>`), the mouse wheel and a click / drag inside
# the game's window (fractions of its client area, so they do not depend on the resolution): what a camera move, a zoom and a unit
# selection need.  The mouse tokens are relative to a window, so they need --window-class.
start_victim() {
  rm -f "$T/key_victim.ready" "$T/key_victim.out" "$T/key_victim.mouse" "$T/victim.rc"
  ( cd "$T" && wine key_victim.exe >/dev/null 2>&1; echo $? >"$T/victim.rc" ) &
  wait_for 300 test -e "$T/key_victim.ready"
}
mouse_log() { tr -d '\r' <"$T/key_victim.mouse"; }
# field <event> <n>: the position of the n-th event of that kind ("x,y")
field() { mouse_log | awk -v e="$1" -v n="$2" '$1 == e && ++c == n { print $2 }'; }
within() { # within <x,y> <want x> <want y>: both coordinates within 4 pixels
  local x="${1%,*}" y="${1#*,}"
  [ -n "$1" ] && [ "$x" -ge "$(($2 - 4))" ] && [ "$x" -le "$(($2 + 4))" ] && [ "$y" -ge "$(($3 - 4))" ] && [ "$y" -le "$(($3 + 4))" ]
}
start_victim
wine "$T/ee-key.exe" --window-class EEKeyVictim e04b/900 01 >"$T/k7.out" 2>&1
check_eq "an extended key held for 900 ms, then Esc: exit 0" "0" "$?"
wait_for 100 test -e "$T/victim.rc"
check_eq "the window got the extended left arrow (e04b), then Esc" "e04b 01" "$(tr -d '\r' <"$T/key_victim.out" | tr '\n' ' ' | sed 's/ $//')"
down="$(mouse_log | awk '$1 == "keydown" && $2 == "75,1" { print $3; exit }')"
up="$(mouse_log | awk '$1 == "keyup" && $2 == "75,1" { print $3; exit }')"
check "the key stayed down at least 800 ms (down $down, up $up)" test -n "$down" -a -n "$up" -a "$((${up:-0} - ${down:-0}))" -ge 800
check "the helper said what it pressed" grep -q "pressed e04b (held 900 ms)" "$T/k7.out"

start_victim
wine "$T/ee-key.exe" --window-class EEKeyVictim wheel:+3 click:0.5,0.25 drag:0.2,0.2,0.8,0.6 01 >"$T/k8.out" 2>&1
check_eq "wheel, click and drag in the window: exit 0" "0" "$?"
wait_for 100 test -e "$T/victim.rc"
check "the wheel notches reached the window (+3 = delta 360)" test "$(mouse_log | awk '$1 == "wheel" { print $2; exit }')" = "360,0"
read -r w h <<EOF
$(tr -d '\r' <"$T/k8.out" | sed -n 's/^ee-key: client area \([0-9]*\)x\([0-9]*\).*/\1 \2/p' | head -1)
EOF
check "the helper reported the client area of the window (${w:-?}x${h:-?})" test -n "${w:-}" -a -n "${h:-}"
w="${w:-0}"; h="${h:-0}"
check "click 0.5,0.25: pressed at that point of the client area ($(field down 1))" within "$(field down 1)" "$((w / 2))" "$((h / 4))"
check "... and released at the same point ($(field up 1))" within "$(field up 1)" "$((w / 2))" "$((h / 4))"
check "drag 0.2,0.2 -> 0.8,0.6: pressed at the first corner ($(field down 2))" within "$(field down 2)" "$((w / 5))" "$((h / 5))"
check "... released at the second ($(field up 2))" within "$(field up 2)" "$((w * 4 / 5))" "$((h * 3 / 5))"
"$WINE_ROOT/bin/wine" "$T/ee-key.exe" wheel:+1 >"$T/k9.out" 2>&1
check_eq "a mouse token without --window-class: exit 2" "2" "$?"
check "... saying it needs a window" grep -q "needs --window-class" "$T/k9.out"
wine "$T/ee-key.exe" --window-class EEKeyVictim click:2,0.5 >"$T/k10.out" 2>&1
check_eq "a fraction outside 0..1: exit 2" "2" "$?"
wine "$T/ee-key.exe" 3d/zz >"$T/k11.out" 2>&1
check_eq "a hold that is not a number of milliseconds: exit 2" "2" "$?"
wine "$T/ee-key.exe" e0 >"$T/k12.out" 2>&1
check_eq "a bare extended prefix is not a scancode: exit 2" "2" "$?"
summary
