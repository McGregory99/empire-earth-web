#!/usr/bin/env bash
# Test of tools/game_run/scripts/ee-start.c under Wine, in the project's own test prefix (never the game's): `wine start`
# mangles the quotes of the game's -loadgame "<save>" argument, so a small helper starts the game with exactly the command
# line named in the file ee-start.cmd next to it.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/world.sh"
SRC="$(cd "$HERE/../../tools/game_run/scripts" && pwd)/ee-start.c"
CC=i686-w64-mingw32-gcc
RE="${REAL_HOME}/Games/EmpireEarth-re"
WINE_ROOT="${REAL_HOME}/Library/Application Support/EmpireEarthMac/runtime/Wine Stable.app/Contents/Resources/wine"
if ! command -v "$CC" >/dev/null || [ ! -x "$WINE_ROOT/bin/wine" ] || [ ! -d "$RE/wineprefix-test" ]; then
  echo "SKIP: needs $CC, the project's Wine and its test prefix ($RE/wineprefix-test)"
  exit 0
fi
T="$(mktemp -d "${TMPDIR:-/tmp}/ee start.XXXXXX")"
export WINEPREFIX="$RE/wineprefix-test" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" MVK_CONFIG_LOG_LEVEL=0
cleanup() { "$WINE_ROOT/bin/wineserver" -k >/dev/null 2>&1; rm -rf "$T"; }
trap cleanup EXIT
wine() { "$WINE_ROOT/bin/wine" "$@" 2>/dev/null; }
winpath() { printf 'Z:%s' "$(printf '%s' "$1" | tr '/' '\\')"; }

check "ee-start.c exists" test -f "$SRC"
$CC -mcrtdll=msvcrt-os -O2 -Wall -Wextra -D_WIN32_WINNT=0x0600 -o "$T/ee-start.exe" "$SRC" 2>"$T/cc.err"
check_eq "ee-start.c compiles without warnings" "0" "$?"
check "... (compiler output is empty)" test ! -s "$T/cc.err"
mkdir -p "$T/game dir"
$CC -mcrtdll=msvcrt-os -O2 -Wall -o "$T/game dir/start_victim.exe" "$HERE/start_victim.c" 2>>"$T/cc.err"
check_eq "the test program compiles" "0" "$?"
[ -x "$T/ee-start.exe" ] || { summary; exit 1; }

victim="$T/game dir/start_victim.exe"
# 1. the command line arrives as written, quotes included, and the working directory is the program's own
printf '%s\n%s\n' "$(winpath "$victim")" '-loadgame "ee_p24_skirm1"' >"$T/ee-start.cmd"
( cd "$T" && wine start /unix "$T/ee-start.exe" ) >/dev/null 2>&1
wait_for 300 test -s "$T/game dir/start_victim.out"
check "the program was started" test -s "$T/game dir/start_victim.out"
check "its command line holds the argument with its quotes: -loadgame \"ee_p24_skirm1\"" grep -qF 'CMDLINE="Z:\' "$T/game dir/start_victim.out"
check "... exactly" grep -qF -- '-loadgame "ee_p24_skirm1"' "$T/game dir/start_victim.out"
check "... and no backslash before a quote" bash -c "! grep -qF '\\\\\"' '$T/game dir/start_victim.out'"
check "its working directory is its own folder" grep -qF 'CWD=Z:\' "$T/game dir/start_victim.out"
check "... the game dir" grep -qF 'game dir' "$T/game dir/start_victim.out"
# the library finds the game by the command line of its process: it must start with a Windows path to the program
ps -axo pid=,rss=,args= | awk '$0 ~ /start_victim\.exe/ && $0 !~ /awk/' >"$T/ps.txt"
check "the process list shows it as a program whose command line starts with a Windows path" \
  bash -c "awk '{ \$1 = \"\"; \$2 = \"\"; sub(/^  /, \"\") } \$0 ~ /^[A-Za-z]:\\\\.*\\\\start_victim\\.exe/ { found = 1 } END { exit !found }' '$T/ps.txt'"
: >"$T/game dir/start_victim.stop"
wait_for 100 bash -c "! ps -axo args= | grep -q '[s]tart_victim.exe'"

# 2. failures are reported
rm -f "$T/ee-start.cmd"
wine "$T/ee-start.exe" >"$T/e1.out" 2>&1
check_eq "no command file: exit 2" "2" "$?"
printf '%s\n%s\n' "Z:\\nonexistent\\game.exe" '-x' >"$T/ee-start.cmd"
wine "$T/ee-start.exe" >"$T/e2.out" 2>&1
check_eq "a program that does not exist: exit 3" "3" "$?"
summary
