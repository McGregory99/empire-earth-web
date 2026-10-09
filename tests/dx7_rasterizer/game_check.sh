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
# Then it stops the game with the prefix's own wineserver, removes the DLL and the registry value it added,
# and verifies the sha256 manifest of the game directory against the one taken before.
#
# The protocol (the game-run lock, the anchored game detection, the verified snapshot and exact restore, the
# signal-safe teardown that keeps lock and snapshot when it cannot finish, the launcher as a background job) is the
# shared library tools/game_run/lib.sh, also used by the trace driver; tests/game_run/ tests it against a fake world.
set -u -o pipefail

DLL_SRC=${1:?usage: game_check.sh <dll> <file name>}
DLL_NAME=${2:?usage: game_check.sh <dll> <file name>}

HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=../../tools/game_run/lib.sh
. "$HERE/../../tools/game_run/lib.sh"
GR_TOOL=game-check

OUT=${GAME_CHECK_OUT:-build/game-check-rasterizer}
LAUNCH_LIMIT=900
gr_test_hook
gr_sanitize_env

SNOOP_KEY='HKCU\Software\Wine\Debug'
SNOOP_MODULE="${DLL_NAME%.dll}.*"

mkdir -p "$OUT"
GR_LOG_FILE="$OUT/game-check.log"
LOG="$OUT/launch.log"
failures=0

say() { printf '[game-check] %s\n' "$*"; }
pass() { printf 'ok   %s\n' "$*"; }
fail() { printf 'FAIL %s\n' "$*"; failures=$((failures + 1)); }

wine_env() { WINEPREFIX="$GR_PREFIX" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" "$@"; }
snoop_value() {
  wine_env "$GR_WINE_ROOT/bin/wine" reg query "$SNOOP_KEY" /v SnoopInclude 2>/dev/null |
    sed -n 's/^[[:space:]]*SnoopInclude[[:space:]]*REG_SZ[[:space:]]*//p' | tr -d '\r'
}
# The wineserver of a `wine reg` call must exit (and write the registry) before the launcher starts one.
flush_registry() { WINEPREFIX="$GR_PREFIX" "$GR_WINE_ROOT/bin/wineserver" -k >/dev/null 2>&1; }

cleanup() { gr_teardown; }
gr_install_traps cleanup

# ---- 1. the game-run lock -------------------------------------------------------------------------------------------

gr_lock_acquire || exit 2

# ---- 2. preconditions -----------------------------------------------------------------------------------------------

[ -f "$DLL_SRC" ] || { say "no such DLL: $DLL_SRC"; exit 2; }
[ -d "$GR_GAME" ] || { say "no game copy at $GR_GAME"; exit 2; }
[ -x "$GR_LAUNCHER_DIR/scripts/launch.sh" ] || { say "no launcher at $GR_LAUNCHER_DIR"; exit 2; }
gr_check_leftovers "$DLL_NAME" || exit 2
gr_assert_config || exit 2
blocking="$(gr_blocking_pids | tr '\n' ' ')"
if [ -n "$blocking" ]; then
  say "the game, Art of Conquest or another program of the GOG install is running (pid $blocking); not touching it"
  exit 2
fi

# ---- 3. manifest, snapshot, registry --------------------------------------------------------------------------------

say "taking the sha256 manifest and a verified snapshot of the game directory"
gr_state_begin && gr_snapshot_take || exit 2
cp "$GAME_RUN_STATE/manifest.before" "$OUT/manifest-before.txt"
say "$(wc -l <"$OUT/manifest-before.txt" | tr -d ' ') files"

prev_snoop="$(snoop_value)"
flush_registry
# the undo steps run before the restore, also from recover.sh after an interrupted run
if [ -n "$prev_snoop" ]; then
  gr_undo_add env "WINEPREFIX=$GR_PREFIX" WINEDEBUG=-all "WINEDLLOVERRIDES=mscoree,mshtml=" "$GR_WINE_ROOT/bin/wine" \
    reg add "$SNOOP_KEY" /v SnoopInclude /t REG_SZ /d "$prev_snoop" /f
else
  gr_undo_add env "WINEPREFIX=$GR_PREFIX" WINEDEBUG=-all "WINEDLLOVERRIDES=mscoree,mshtml=" "$GR_WINE_ROOT/bin/wine" \
    reg delete "$SNOOP_KEY" /v SnoopInclude /f
fi
gr_undo_add env "WINEPREFIX=$GR_PREFIX" "$GR_WINE_ROOT/bin/wineserver" -k
new_snoop=$SNOOP_MODULE
[ -n "$prev_snoop" ] && new_snoop="$prev_snoop;$SNOOP_MODULE"
wine_env "$GR_WINE_ROOT/bin/wine" reg add "$SNOOP_KEY" /v SnoopInclude /t REG_SZ /d "$new_snoop" /f >/dev/null 2>&1 || {
  say "could not set SnoopInclude"; exit 2; }
flush_registry
say "SnoopInclude = $new_snoop"

# ---- 4. install and launch --------------------------------------------------------------------------------------------

cp "$DLL_SRC" "$GR_GAME/$DLL_NAME"
say "installed $DLL_NAME ($(shasum -a 256 "$GR_GAME/$DLL_NAME" | cut -c1-16)...)"

mkdir -p "$GR_LOG_DIR"
ls "$GR_LOG_DIR"/launch-*.log 2>/dev/null | sort >"$GAME_RUN_STATE/logs-before.txt"
gr_launcher_log_mark
say "launching the game (returns at the first frame)"
gr_run_launcher "$LAUNCH_LIMIT" "$OUT/launch.stdout" \
  bash -c 'cd "$1" && shift && exec "$@"' _ "$GR_LAUNCHER_DIR" \
  env EE_BIG_ARMIES=0 EE_BIG_CROWDS=0 EE_X87T=0 EE_SSE_MATH=0 EE_MEMFILE_DOUBLE=0 EE_POINTLIST_GUARD=0 \
  EE_ANIMATION_SMOOTHING=0 WINEDEBUG='-all,+err,+loaddll,+snoop' \
  "$GR_LAUNCHER_DIR/scripts/launch.sh"
launch_status=$?
say "launch.sh exit status $launch_status"

newest=$(ls -t "$GR_LOG_DIR"/launch-*.log 2>/dev/null | head -1)
if [ -z "$newest" ] || grep -qxF "$newest" "$GAME_RUN_STATE/logs-before.txt"; then
  fail "launch.sh produced no new log"
  exit 1
fi
cp "$newest" "$LOG"
cp "$GR_GAME/ee-ddraw.log" "$OUT/ee-ddraw.log" 2>/dev/null || : >"$OUT/ee-ddraw.log"
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

# ---- 6. stop the game and put everything back --------------------------------------------------------------------------

if gr_check_launcher_log; then
  pass "the launcher installed the version.dll shim (the game is the one the user plays)"
else
  fail "the launcher did not install the version.dll shim"
fi

say "stopping the game with the prefix's wineserver"
if gr_stop_game; then pass "the game process is gone"; else fail "the game is still running"; fi

# Undo steps, restore and verification; the lock is kept until the registry and the manifest have been read back.
GR_KEEP_LOCK=1
if ! gr_teardown; then
  fail "the teardown did not finish: the lock and the snapshot are kept (see the recovery commands above)"
  say "FAIL: $failures check(s) failed; evidence in $OUT/"
  exit 1
fi
pass "everything was put back: the undo steps ran and the game directory matches its manifest"
[ ! -e "$GR_GAME/$DLL_NAME" ] && pass "$DLL_NAME removed from the game directory" || fail "$DLL_NAME is still installed"

now_snoop="$(snoop_value)"
flush_registry
if [ "$now_snoop" = "$prev_snoop" ]; then
  pass "registry restored (SnoopInclude ${prev_snoop:+was }${prev_snoop:-absent before and is absent again})"
else
  fail "registry differs: SnoopInclude was '$prev_snoop', is '$now_snoop'"
fi

gr_manifest "$GR_GAME" "$OUT/manifest-after.txt"
if diff -u "$OUT/manifest-before.txt" "$OUT/manifest-after.txt" >"$OUT/manifest.diff"; then
  pass "the game directory matches the manifest taken before ($(wc -l <"$OUT/manifest-after.txt" | tr -d ' ') files, sha256)"
else
  fail "the game directory differs from the manifest taken before:"
  head -20 "$OUT/manifest.diff"
fi
GR_KEEP_LOCK=0
gr_lock_release

if [ "$failures" -eq 0 ]; then
  say "PASS: the engine loads $DLL_NAME, calls PIMGetType and PIMIsValid (false), frees it, and keeps the original TnL renderer"
  exit 0
fi
say "FAIL: $failures check(s) failed; evidence in $OUT/"
exit 1
