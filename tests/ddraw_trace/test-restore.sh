#!/usr/bin/env bash
# Tests of the game-directory safety net used by game-trace.sh: swap-in, manifest, byte-exact restore.
# Runs on a scratch directory, never on the game.
#   test-restore.sh <tools/ddraw_trace dir>
set -u
TOOL="$(cd "${1:?usage: test-restore.sh <tools/ddraw_trace dir>}" && pwd)"
# shellcheck source=/dev/null
. "$TOOL/scripts/lib.sh"

fails=0
checks=0
check() { # check <description> <command...>
  local what="$1"
  shift
  checks=$((checks + 1))
  if "$@"; then echo "ok   $what"; else echo "FAIL $what"; fails=$((fails + 1)); fi
}

work="$(mktemp -d "${TMPDIR:-/tmp}/ddt-restore.XXXXXX")"
trap 'rm -rf "$work"' EXIT
LOG_FILE="$work/log"
OUT="$work/out"
mkdir -p "$OUT"

make_game() {
  rm -rf "$GAME_DIR"
  mkdir -p "$GAME_DIR/Data" "$GAME_DIR/Users"
  printf 'launcher ddraw v1' >"$GAME_DIR/ddraw.dll"
  printf 'orig' >"$GAME_DIR/ddraw_eeorig.dll"
  printf 'payload' >"$GAME_DIR/Data/a.bin"
  printf 'setting=1' >"$GAME_DIR/Users/cfg"
  printf 'log0' >"$GAME_DIR/ee-ddraw.log"
}
printf 'PROXY' >"$work/proxy.dll"

# 1. swap-in puts the proxy in front, keeps the launcher's file under another name, writes the config
GAME_DIR="$work/game1"
make_game
"$TOOL/scripts/swap-in.sh" "$GAME_DIR" "$work/proxy.dll" 'Z:\tmp\t.raw' 123
check "swap-in installs the proxy as ddraw.dll" cmp -s "$GAME_DIR/ddraw.dll" "$work/proxy.dll"
check "swap-in keeps the launcher's proxy as ddraw_next.dll" test "$(cat "$GAME_DIR/ddraw_next.dll")" = "launcher ddraw v1"
check "swap-in writes the trace path" grep -q '^DDT_TRACE=Z:\\tmp\\t.raw$' "$GAME_DIR/ddraw_trace.cfg"
check "swap-in writes the present cap" grep -q '^DDT_MAX_PRESENTS=123$' "$GAME_DIR/ddraw_trace.cfg"
"$TOOL/scripts/swap-in.sh" "$GAME_DIR" "$work/proxy.dll" 'Z:\tmp\t.raw' 123
check "swap-in twice does not lose the launcher's proxy" test "$(cat "$GAME_DIR/ddraw_next.dll")" = "launcher ddraw v1"
# the launcher rewrote ddraw.dll between two attempts: the new launcher file replaces ddraw_next.dll
printf 'launcher ddraw v2' >"$GAME_DIR/ddraw.dll"
"$TOOL/scripts/swap-in.sh" "$GAME_DIR" "$work/proxy.dll" 'Z:\tmp\t.raw' 123
check "swap-in after a launcher rewrite keeps the newest launcher file" test "$(cat "$GAME_DIR/ddraw_next.dll")" = "launcher ddraw v2"

# 2. a full run's damage is undone byte for byte
GAME_DIR="$work/game2"
make_game
manifest "$GAME_DIR" >"$OUT/manifest.before"
snapshot="$OUT/snapshot"
cp -R "$GAME_DIR" "$snapshot"
swapped=1
"$TOOL/scripts/swap-in.sh" "$GAME_DIR" "$work/proxy.dll" 'Z:\tmp\t.raw' 123
printf 'log-run' >"$GAME_DIR/ee-ddraw.log"         # a log the game rewrites
printf 'new' >"$GAME_DIR/Users/new.dat"            # a file the game adds
rm "$GAME_DIR/Users/cfg"                           # a file the game removes
printf 'rebuilt ddraw' >"$GAME_DIR/ddraw_next.dll" # the launcher rewrote its own proxy
restore_game_dir
check "restore returns success" test $? -eq 0
manifest "$GAME_DIR" >"$OUT/manifest.check"
check "the manifest after the restore equals the manifest before" cmp -s "$OUT/manifest.before" "$OUT/manifest.check"
check "the launcher's ddraw.dll is back under its own name" test "$(cat "$GAME_DIR/ddraw.dll")" = "launcher ddraw v1"
check "no proxy files are left" test ! -e "$GAME_DIR/ddraw_trace.cfg" -a ! -e "$GAME_DIR/ddraw_next.dll"
check "the snapshot is cleaned up" test ! -e "$snapshot"

# 3. a restore that cannot be completed is reported, never silently accepted
GAME_DIR="$work/game3"
make_game
manifest "$GAME_DIR" >"$OUT/manifest.before"
snapshot="$OUT/snapshot3"
cp -R "$GAME_DIR" "$snapshot"
rm "$snapshot/Data/a.bin"                          # a damaged snapshot
printf 'changed' >"$GAME_DIR/Data/a.bin"
swapped=0
restore_game_dir >/dev/null 2>&1
check "restore fails when the snapshot cannot undo a change" test $? -ne 0

echo "$([ "$fails" -eq 0 ] && echo PASS || echo FAIL): $checks checks, $fails failed"
[ "$fails" -eq 0 ]
