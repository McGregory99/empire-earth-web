#!/usr/bin/env bash
# Automated in-game check of the clang-built rasterizer plugin (task P2.1).  No user involvement.
#
#   game_check.sh <path to EEWebRasterizer.dll> <file name to install it under>
#
# It drops the DLL next to the real game in the Wine game copy, starts the game through the
# project's launcher until its first frame, and reads the Wine log (+loaddll, +snoop) to prove:
#   - the engine's PIMModule::GetModule loaded the DLL and called PIMGetType (== 'rast');
#   - GEInitialize asked PIMIsValid, got false in AL, and the engine freed the module again;
#   - RPIMGetRasterizer was therefore never called;
#   - the original DX7HRTnLDisplay.dll stayed loaded and the game rendered (BeginScene);
#   - nothing crashed (no err:seh, no Wine Program Error).
# Then it stops the game with the prefix's own wineserver, removes the DLL and every registry
# value it added, and verifies the sha256 manifest of the game directory against the one taken
# before.  Everything is restored on any exit path, and the game-run lock is always released.
#
# Protocol (shared with the other agents that use the game copy):
#   - take the lock `mkdir $RE_DIR/locks/game-run` (atomic; wait and retry while it exists);
#   - never touch the game directory while the game is running;
#   - restore the directory, then `rmdir` the lock.
set -u -o pipefail

DLL_SRC=${1:?usage: game_check.sh <dll> <file name>}
DLL_NAME=${2:?usage: game_check.sh <dll> <file name>}

RE_DIR=${RE_DIR:-$HOME/Games/EmpireEarth-re}
SUPPORT=${EMPIRE_EARTH_SUPPORT_DIR:-$HOME/Library/Application Support/EmpireEarthMac}
PREFIX=$SUPPORT/wineprefix
GAME="$PREFIX/drive_c/GOG Games/Empire Earth Gold Edition/Empire Earth"
WINE_BIN="$SUPPORT/runtime/Wine Stable.app/Contents/Resources/wine/bin"
LAUNCHER_DIR=${EE_LAUNCHER_DIR:-$HOME/Games/tools/ee-macos}
LOG_DIR="$SUPPORT/logs"
LOCK="$RE_DIR/locks/game-run"
LOCK_WAIT=${GAME_LOCK_WAIT:-3600}
OUT=${GAME_CHECK_OUT:-build/game-check-rasterizer}
SNOOP_KEY='HKCU\Software\Wine\Debug'
SNOOP_MODULE="${DLL_NAME%.dll}.*"
# Files the game or its launcher rewrites on every run: the game's logs, and the two shim DLLs
# that scripts/launch.sh (apply-launch-patches.sh) copies from its helper build into the game
# directory each time (ddraw.dll, version.dll; their content follows the helper build of the
# moment, not this check).  They are stashed before and put back afterwards, so the game
# directory ends up exactly as found; the run's own copies of the logs are kept as evidence.
RUNTIME_FILES="ee-ddraw.log ee-version.log 0_Error.log ddraw.dll version.dll"

mkdir -p "$OUT" "$RE_DIR/locks"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/game-check.XXXXXX")
LOG="$OUT/launch.log"

have_lock=0
dll_installed=0
reg_changed=0
game_started=0
restored=0
failures=0

say() { printf '[game-check] %s\n' "$*"; }
pass() { printf 'ok   %s\n' "$*"; }
fail() { printf 'FAIL %s\n' "$*"; failures=$((failures + 1)); }

wine_env() { WINEPREFIX="$PREFIX" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" "$@"; }

# pgrep as the project's protocol prescribes, minus Rosetta processes left stuck mid-exit with a
# few KB resident (they match the pattern for minutes and are not a running game).
game_pids() {
  local pid rss
  for pid in $(pgrep -f 'C:.GOG Games.*Empire Earth.exe' 2>/dev/null); do
    rss=$(ps -o rss= -p "$pid" 2>/dev/null | tr -d ' ')
    if [ -n "$rss" ] && [ "$rss" -gt 1024 ]; then
      echo "$pid"
    fi
  done
}

wait_until_game_gone() {
  local seconds=$1 waited=0
  while [ -n "$(game_pids)" ]; do
    if [ "$waited" -ge "$seconds" ]; then
      return 1
    fi
    sleep 1
    waited=$((waited + 1))
  done
  return 0
}

stop_game() {
  # The prefix's own wineserver ends every process of this prefix and nothing else.
  WINEPREFIX="$PREFIX" "$WINE_BIN/wineserver" -k >/dev/null 2>&1
  if ! wait_until_game_gone 60; then
    WINEPREFIX="$PREFIX" "$WINE_BIN/wineserver" -k >/dev/null 2>&1
    wait_until_game_gone 30
  fi
}

manifest() {
  (cd "$GAME" && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 shasum -a 256)
}

# ---- restoring ----------------------------------------------------------------------------------

restore_registry() {
  if [ "$reg_changed" -eq 1 ]; then
    if [ -n "${prev_snoop-}" ]; then
      wine_env "$WINE_BIN/wine" reg add "$SNOOP_KEY" /v SnoopInclude /t REG_SZ /d "$prev_snoop" /f >/dev/null 2>&1
    else
      wine_env "$WINE_BIN/wine" reg delete "$SNOOP_KEY" /v SnoopInclude /f >/dev/null 2>&1
    fi
    reg_changed=0
    # Let the short-lived wineserver that served `wine reg` write the registry and exit.
    WINEPREFIX="$PREFIX" "$WINE_BIN/wineserver" -k >/dev/null 2>&1
  fi
}

restore_all() {
  [ "$restored" -eq 1 ] && return 0
  if [ "$game_started" -eq 1 ]; then # only a game this script launched is ever stopped
    stop_game
  fi
  if [ "$dll_installed" -eq 1 ] && [ -z "$(game_pids)" ]; then
    rm -f "$GAME/$DLL_NAME"
    dll_installed=0
  fi
  restore_registry
  if [ -d "$WORK/stash" ] && [ -z "$(game_pids)" ]; then
    local f
    for f in $RUNTIME_FILES; do
      if [ -e "$WORK/stash/$f" ]; then
        cp -p "$WORK/stash/$f" "$GAME/$f"
      elif [ -e "$WORK/stash/$f.absent" ]; then
        rm -f "$GAME/$f"
      fi
    done
  fi
  restored=1
}

cleanup() {
  restore_all
  if [ "$have_lock" -eq 1 ]; then
    rmdir "$LOCK" 2>/dev/null && say "released $LOCK"
    have_lock=0
  fi
  rm -rf "$WORK"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

# ---- 1. the game-run lock ---------------------------------------------------------------------

waited=0
until mkdir "$LOCK" 2>/dev/null; do
  if [ "$waited" -ge "$LOCK_WAIT" ]; then
    say "could not take $LOCK within ${LOCK_WAIT}s: another agent is using the game"
    exit 2
  fi
  say "waiting for the game-run lock ($LOCK exists; another agent is using the game)"
  sleep 15
  waited=$((waited + 15))
done
have_lock=1
say "took $LOCK"

# ---- 2. preconditions -------------------------------------------------------------------------

[ -f "$DLL_SRC" ] || { say "no such DLL: $DLL_SRC"; exit 2; }
[ -d "$GAME" ] || { say "no game copy at $GAME"; exit 2; }
[ -x "$LAUNCHER_DIR/scripts/launch.sh" ] || { say "no launcher at $LAUNCHER_DIR"; exit 2; }
[ ! -e "$GAME/$DLL_NAME" ] || { say "$GAME/$DLL_NAME already exists; refusing to overwrite it"; exit 2; }
if [ -n "$(game_pids)" ]; then
  say "the game is already running (pgrep 'C:.GOG Games.*Empire Earth.exe'); not touching it"
  exit 2
fi

# ---- 3. manifest, stash, registry ---------------------------------------------------------------

say "taking the sha256 manifest of the game directory"
manifest > "$OUT/manifest-before.txt" || { say "manifest failed"; exit 2; }
say "$(wc -l < "$OUT/manifest-before.txt" | tr -d ' ') files"

mkdir -p "$WORK/stash"
for f in $RUNTIME_FILES; do
  if [ -e "$GAME/$f" ]; then cp -p "$GAME/$f" "$WORK/stash/$f"; else : > "$WORK/stash/$f.absent"; fi
done

prev_snoop=$(wine_env "$WINE_BIN/wine" reg query "$SNOOP_KEY" /v SnoopInclude 2>/dev/null |
  sed -n 's/^[[:space:]]*SnoopInclude[[:space:]]*REG_SZ[[:space:]]*//p' | tr -d '\r')
new_snoop=$SNOOP_MODULE
[ -n "$prev_snoop" ] && new_snoop="$prev_snoop;$SNOOP_MODULE"
wine_env "$WINE_BIN/wine" reg add "$SNOOP_KEY" /v SnoopInclude /t REG_SZ /d "$new_snoop" /f >/dev/null 2>&1 || {
  say "could not set SnoopInclude"; exit 2; }
reg_changed=1
# Make the wineserver of that `wine reg` call exit (writing the registry) before the launcher starts one.
WINEPREFIX="$PREFIX" "$WINE_BIN/wineserver" -k >/dev/null 2>&1
say "SnoopInclude = $new_snoop"

# ---- 4. install and launch ----------------------------------------------------------------------

cp "$DLL_SRC" "$GAME/$DLL_NAME"
dll_installed=1
say "installed $DLL_NAME ($(shasum -a 256 "$GAME/$DLL_NAME" | cut -c1-16)...)"

ls "$LOG_DIR"/launch-*.log 2>/dev/null | sort > "$WORK/logs-before.txt"
game_started=1
say "launching the game (returns at the first frame)"
(
  cd "$LAUNCHER_DIR" &&
    EE_BIG_ARMIES=0 EE_BIG_CROWDS=0 EE_X87T=0 EE_SSE_MATH=0 EE_MEMFILE_DOUBLE=0 EE_POINTLIST_GUARD=0 \
    EE_ANIMATION_SMOOTHING=0 WINEDEBUG='-all,+err,+loaddll,+snoop' \
    perl -e 'alarm shift; exec @ARGV' 900 ./scripts/launch.sh
) > "$OUT/launch.stdout" 2>&1
launch_status=$?
say "launch.sh exit status $launch_status"

newest=$(ls -t "$LOG_DIR"/launch-*.log 2>/dev/null | head -1)
if [ -z "$newest" ] || grep -qxF "$newest" "$WORK/logs-before.txt"; then
  fail "launch.sh produced no new log"
  exit 1
fi
cp "$newest" "$LOG"
cp "$GAME/ee-ddraw.log" "$OUT/ee-ddraw.log" 2>/dev/null || : > "$OUT/ee-ddraw.log"
say "log: $newest"

# ---- 5. what the log must show ------------------------------------------------------------------

dll_re=$(printf '%s' "$DLL_NAME" | sed 's/\./\\./g')
mod_re=$(printf '%s' "${DLL_NAME%.dll}" | sed 's/\./\\./g')
has() { grep -qiE "$1" "$LOG"; }
first_line() { grep -niE "$1" "$LOG" | head -1 | cut -d: -f1; }

[ "$launch_status" -eq 0 ] && pass "launch.sh reached the game's first frame" || fail "launch.sh failed (status $launch_status)"

has "Loaded L\"[^\"]*\\\\$dll_re\" at" && pass "the engine loaded $DLL_NAME" || fail "no 'Loaded ... $DLL_NAME' line"
has "SNOOP_SetupDLL .*name=$dll_re" && pass "Wine snoop is tracing the module" || fail "snoop did not attach to $DLL_NAME"

t_call=$(first_line "CALL +$mod_re\\.PIMGetType")
t_ret=$(first_line "RET +$mod_re\\.PIMGetType\\(\\) retval=72617374 ")
[ -n "$t_call" ] && [ -n "$t_ret" ] && pass "PIMGetType() was called and returned 'rast' (0x72617374)" ||
  fail "PIMGetType was not called or did not return 0x72617374"

has "CALL +$mod_re\\.PIMGetName" && pass "PIMGetName() was called" || fail "PIMGetName was never called"

v_call=$(first_line "CALL +$mod_re\\.PIMIsValid")
v_ret=$(first_line "RET +$mod_re\\.PIMIsValid\\(\\) retval=[0-9a-f]{6}00 ")
[ -n "$v_call" ] && [ -n "$v_ret" ] && pass "PIMIsValid() was called and returned false in AL" ||
  fail "PIMIsValid was not called or returned true"
if has "RET +$mod_re\\.PIMIsValid\\(\\) retval=[0-9a-f]{6}(0[1-9a-f]|[1-9a-f][0-9a-f]) "; then
  fail "PIMIsValid returned true at least once"
fi

has "CALL +$mod_re\\.RPIMGetRasterizer" && fail "RPIMGetRasterizer was called although PIMIsValid is false" ||
  pass "RPIMGetRasterizer was never called"

u_line=$(first_line "free_modref Unloaded module L\"[^\"]*\\\\$dll_re\"")
[ -n "$u_line" ] && pass "the engine freed $DLL_NAME again" || fail "no 'Unloaded module ... $DLL_NAME' line"
if [ -n "$t_call" ] && [ -n "$v_ret" ] && [ -n "$u_line" ] && [ "$t_call" -lt "$v_call" ] && [ "$v_ret" -lt "$u_line" ]; then
  pass "order: PIMGetType, then PIMIsValid, then unload"
else
  fail "unexpected order of PIMGetType ($t_call) / PIMIsValid ($v_call..$v_ret) / unload ($u_line)"
fi

has 'Loaded L"[^"]*\\DX7HRTnLDisplay\.dll" at' && ! has 'Unloaded module L"[^"]*\\DX7HRTnLDisplay\.dll"' &&
  pass "the original DX7HRTnLDisplay.dll was loaded and kept (the TnL renderer is still selected)" ||
  fail "DX7HRTnLDisplay.dll was not loaded, or was freed"
frames=$(grep -c 'BeginScene ENTER' "$OUT/ee-ddraw.log")
[ "$frames" -ge 10 ] && pass "the game rendered frames with it (ee-ddraw.log: $frames x BeginScene)" ||
  fail "ee-ddraw.log shows only $frames BeginScene calls"

if grep -qE 'err:seh|Program Error|Unhandled (exception|page fault)|Game process (crashed|exited|stalled)' "$LOG"; then
  fail "the log shows a crash:"; grep -nE 'err:seh|Program Error|Unhandled (exception|page fault)|Game process (crashed|exited|stalled)' "$LOG" | head -5
else
  pass "no err:seh / Program Error / unhandled exception in the log"
fi
if grep -iE "err:.*$mod_re" "$LOG" | grep -q .; then
  fail "err: lines mention the module:"; grep -iE "err:.*$mod_re" "$LOG" | head -5
else
  pass "no err: line mentions the module"
fi

# ---- 6. stop the game and put everything back -------------------------------------------------------------

say "stopping the game with the prefix's wineserver"
stop_game
game_started=0
if [ -z "$(game_pids)" ]; then pass "the game process is gone"; else fail "the game is still running"; fi

restore_all
[ ! -e "$GAME/$DLL_NAME" ] && pass "$DLL_NAME removed from the game directory" || fail "$DLL_NAME is still installed"

now_snoop=$(wine_env "$WINE_BIN/wine" reg query "$SNOOP_KEY" /v SnoopInclude 2>/dev/null |
  sed -n 's/^[[:space:]]*SnoopInclude[[:space:]]*REG_SZ[[:space:]]*//p' | tr -d '\r')
WINEPREFIX="$PREFIX" "$WINE_BIN/wineserver" -k >/dev/null 2>&1
if [ "$now_snoop" = "$prev_snoop" ]; then
  pass "registry restored (SnoopInclude ${prev_snoop:+was }${prev_snoop:-absent before and is absent again})"
else
  fail "registry differs: SnoopInclude was '$prev_snoop', is '$now_snoop'"
fi

manifest > "$OUT/manifest-after.txt"
if diff -u "$OUT/manifest-before.txt" "$OUT/manifest-after.txt" > "$OUT/manifest.diff"; then
  pass "the game directory matches the manifest taken before ($(wc -l < "$OUT/manifest-after.txt" | tr -d ' ') files, sha256)"
else
  fail "the game directory differs from the manifest taken before:"; head -20 "$OUT/manifest.diff"
fi

if [ "$failures" -eq 0 ]; then
  say "PASS: the engine loads $DLL_NAME, calls PIMGetType and PIMIsValid (false), frees it, and keeps the original TnL renderer"
  exit 0
fi
say "FAIL: $failures check(s) failed; evidence in $OUT/"
exit 1
