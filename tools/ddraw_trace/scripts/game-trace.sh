#!/usr/bin/env bash
# Capture a normalized ddraw trace of the ORIGINAL renderer plugin from startup to the main menu, in the real game.
#
#   game-trace.sh <path-to-proxy-ddraw.dll> [--gate]
#
# One run: take the game-run lock, check the game is down, snapshot the game directory, launch through the
# unmodified 00xJS launcher (the proxy is swapped in by a Wine wrapper at the moment the game is started, because
# the launcher rewrites ddraw.dll on every launch), wait for the capture cap, stop the prefix with its own
# wineserver, restore the game directory byte for byte and release the lock.  --gate does two runs and diffs them.
# Design and safety argument: README.md ("The ddraw chain").
set -u -o pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
PROXY="$(cd "$(dirname "${1:?usage: game-trace.sh <proxy ddraw.dll> [--gate]}")" && pwd)/$(basename "$1")"
MODE="${2:-single}"

RE_DIR="${RE_DIR:-$HOME/Games/EmpireEarth-re}"
SUPPORT="$HOME/Library/Application Support/EmpireEarthMac"
PREFIX="$SUPPORT/wineprefix"
WINE_ROOT="${WINE_ROOT:-$SUPPORT/runtime/Wine Stable.app/Contents/Resources/wine}"
LAUNCHER_DIR="${LAUNCHER_DIR:-$HOME/Games/tools/ee-macos}"
GAME_DIR="$PREFIX/drive_c/GOG Games/Empire Earth Gold Edition/Empire Earth"
LOCK="$RE_DIR/locks/game-run"
OUT="$HERE/build/game-trace"
PRESENTS="${DDT_PRESENTS:-300}"
LOCK_WAIT="${DDT_LOCK_WAIT:-1800}"       # seconds to wait for another agent's game run
CAPTURE_WAIT="${DDT_CAPTURE_WAIT:-420}"  # seconds from launch to the capture cap
EXIT_WAIT=90                             # seconds for the game to disappear after wineserver -k

mkdir -p "$OUT" "$RE_DIR/locks"

log() { printf '%s game-trace: %s\n' "$(date '+%H:%M:%S')" "$*" | tee -a "$OUT/game-trace.log" >&2; }

# Live game processes: the same test as the launcher's game_pids (a Rosetta process that was killed can linger
# in the process table with a few KB resident; those do not count).
game_pids() {
  ps -axo pid=,rss=,args= | awk '{ pid = $1; rss = $2; $1 = ""; $2 = ""; sub(/^  /, "") }
    rss > 1024 && $0 ~ /^[A-Za-z]:\\.*\\(Empire Earth|EE-AOC)\.exe/ { print pid }'
}

have_lock=0
swapped=0
launched=0
snapshot=""

acquire_lock() {
  local waited=0
  while ! mkdir "$LOCK" 2>/dev/null; do
    if [ "$waited" -ge "$LOCK_WAIT" ]; then
      log "FAIL: game-run lock still held after ${LOCK_WAIT}s ($LOCK): another agent is using the game"
      return 1
    fi
    [ "$waited" -eq 0 ] && log "game-run lock held by another agent; waiting"
    sleep 10
    waited=$((waited + 10))
  done
  have_lock=1
  echo "ddraw_trace game-trace.sh pid $$ $(date '+%F %T')" >"$LOCK/owner" 2>/dev/null || true
  log "took the game-run lock"
}

release_lock() {
  if [ "$have_lock" -eq 1 ]; then
    rm -f "$LOCK/owner"
    rmdir "$LOCK" 2>/dev/null && log "released the game-run lock"
    have_lock=0
  fi
}

stop_game() {
  WINEPREFIX="$PREFIX" "$WINE_ROOT/bin/wineserver" -k >/dev/null 2>&1 || true
  local waited=0
  while [ -n "$(game_pids)" ] && [ "$waited" -lt "$EXIT_WAIT" ]; do
    sleep 2
    waited=$((waited + 2))
  done
  if [ -n "$(game_pids)" ]; then
    log "WARNING: game process(es) still alive after ${EXIT_WAIT}s: $(game_pids | tr '\n' ' ')"
    return 1
  fi
}

manifest() { (cd "$1" && find . -type f -print0 | sort -z | xargs -0 shasum -a 256); }

# Undo everything a run did to the game directory, byte for byte.
restore_game_dir() {
  [ -n "$snapshot" ] || return 0
  # our files first: the launcher's own ddraw.dll goes back to its name
  rm -f "$GAME_DIR/ddraw_trace.cfg"
  if [ "$swapped" -eq 1 ] && [ -f "$GAME_DIR/ddraw_next.dll" ]; then
    mv -f "$GAME_DIR/ddraw_next.dll" "$GAME_DIR/ddraw.dll"
  fi
  manifest "$GAME_DIR" >"$OUT/manifest.after"
  if cmp -s "$OUT/manifest.before" "$OUT/manifest.after"; then
    log "game directory identical to the pre-run manifest"
  else
    log "files changed by the launcher/game during the run (restoring them):"
    diff "$OUT/manifest.before" "$OUT/manifest.after" | grep '^[<>]' | awk '{ $2 = ""; print }' | sed 's/^/    /' | sort -u | tee -a "$OUT/game-trace.log" >&2
    # changed or removed: copy back from the snapshot; added: delete
    diff "$OUT/manifest.before" "$OUT/manifest.after" | grep '^<' | sed 's/^< [0-9a-f]*  //' | while IFS= read -r f; do
      mkdir -p "$(dirname "$GAME_DIR/$f")"
      cp -pf "$snapshot/$f" "$GAME_DIR/$f"
    done
    diff "$OUT/manifest.before" "$OUT/manifest.after" | grep '^>' | sed 's/^> [0-9a-f]*  //' | while IFS= read -r f; do
      [ -f "$snapshot/$f" ] || rm -f "$GAME_DIR/$f"
    done
    manifest "$GAME_DIR" >"$OUT/manifest.after"
    if cmp -s "$OUT/manifest.before" "$OUT/manifest.after"; then
      log "game directory restored: manifest identical to the pre-run manifest"
    else
      log "FAIL: game directory does NOT match the pre-run manifest after the restore"
      diff "$OUT/manifest.before" "$OUT/manifest.after" | head -20 | tee -a "$OUT/game-trace.log" >&2
      rm -rf "$OUT/snapshot-keep"
      mv "$snapshot" "$OUT/snapshot-keep"
      snapshot=""
      return 1
    fi
  fi
  rm -rf "$snapshot"
  snapshot=""
}

cleanup() {
  local rc=$?
  trap - EXIT INT TERM
  if [ "$launched" -eq 1 ] && [ -n "$(game_pids)" ]; then
    log "cleanup: stopping the prefix"
    stop_game
  fi
  restore_game_dir || rc=6
  release_lock
  exit "$rc"
}
trap cleanup EXIT INT TERM

one_run() {
  local tag="$1" raw="$OUT/run.raw" win_raw wrapper="$OUT/wrap/bin"

  acquire_lock || return 2
  if pgrep -f 'C:.GOG Games.*Empire Earth.exe' >/dev/null; then
    log "FAIL: the game is running (pgrep matched); refusing to touch the game directory"
    return 3
  fi
  [ -d "$GAME_DIR" ] || { log "FAIL: no game directory at $GAME_DIR"; return 3; }

  log "recording the pre-run manifest of $GAME_DIR"
  manifest "$GAME_DIR" >"$OUT/manifest.before"
  snapshot="$OUT/snapshot"
  rm -rf "$snapshot"
  cp -cR "$GAME_DIR" "$snapshot" 2>/dev/null || cp -R "$GAME_DIR" "$snapshot"

  # Wine wrapper: the launcher honours EMPIRE_EARTH_WINE.  It forwards everything to the real Wine and, right before the
  # game is started (`wine start /unix <exe>`), swaps the proxy in.  By then apply-launch-patches.sh has finished
  # writing ddraw.dll, so nothing overwrites the proxy afterwards.
  win_raw="Z:$(printf '%s' "$raw" | tr '/' '\\')"
  rm -rf "$OUT/wrap" && mkdir -p "$wrapper"
  rm -f "$raw" "$raw.done"
  ln -s "$WINE_ROOT/bin/wineserver" "$wrapper/wineserver"
  cat >"$wrapper/wine" <<WRAP
#!/bin/bash
if [ "\${1:-}" = start ] && [ "\${2:-}" = /unix ]; then
  '$HERE/scripts/swap-in.sh' '$GAME_DIR' '$PROXY' '$win_raw' '$PRESENTS' || exit 97
fi
exec '$WINE_ROOT/bin/wine' "\$@"
WRAP
  chmod +x "$wrapper/wine"

  swapped=1
  launched=1
  log "launching the game through the launcher (presents cap: $PRESENTS)"
  ( cd "$LAUNCHER_DIR" && EMPIRE_EARTH_WINE="$wrapper/wine" \
      EE_BIG_ARMIES=0 EE_BIG_CROWDS=0 EE_X87T=0 EE_SSE_MATH=0 EE_MEMFILE_DOUBLE=0 EE_POINTLIST_GUARD=0 EE_ANIMATION_SMOOTHING=0 \
      ./scripts/launch.sh ) >"$OUT/launch.log" 2>&1
  local rc=$?
  if [ "$rc" -ne 0 ]; then
    log "FAIL: launch.sh exited with $rc (see $OUT/launch.log)"
    tail -5 "$OUT/launch.log" | sed 's/^/    /' >&2
    return 4
  fi
  log "launcher reports the first frames are up; waiting for the capture cap"

  local waited=0
  while [ ! -f "$raw.done" ]; do
    if [ -z "$(game_pids)" ]; then
      log "FAIL: the game exited before the capture cap"
      return 5
    fi
    if [ "$waited" -ge "$CAPTURE_WAIT" ]; then
      log "FAIL: capture cap not reached after ${CAPTURE_WAIT}s"
      return 5
    fi
    sleep 2
    waited=$((waited + 2))
  done
  log "capture complete ($(wc -l <"$raw" | tr -d ' ') lines); stopping the prefix"
  stop_game || return 7
  launched=0
  cp "$raw" "$OUT/trace.$tag.txt"
  log "trace saved: $OUT/trace.$tag.txt"
  restore_game_dir || return 6
  release_lock
  return 0
}

stamp="$(date '+%Y%m%d-%H%M%S')"
if [ "$MODE" = "--gate" ]; then
  one_run "A.$stamp" || exit $?
  # give the filesystem and Wine a moment, then the second run
  sleep 5
  one_run "B.$stamp" || exit $?
  if diff -u "$OUT/trace.A.$stamp.txt" "$OUT/trace.B.$stamp.txt" >"$OUT/diff.$stamp.txt"; then
    log "GATE PASS: the two traces are identical ($(wc -l <"$OUT/trace.A.$stamp.txt" | tr -d ' ') lines)"
  else
    log "GATE DIFFERENCES: $(grep -c '^[-+][^-+]' "$OUT/diff.$stamp.txt") changed lines, see $OUT/diff.$stamp.txt"
    exit 1
  fi
else
  one_run "$stamp" || exit $?
  [ -f "$OUT/last.txt" ] && mv -f "$OUT/last.txt" "$OUT/prev.txt"
  cp "$OUT/trace.$stamp.txt" "$OUT/last.txt"
  if [ -f "$OUT/prev.txt" ]; then
    if diff -q "$OUT/prev.txt" "$OUT/last.txt" >/dev/null; then
      log "identical to the previous run's trace"
    else
      log "DIFFERS from the previous run's trace: diff -u $OUT/prev.txt $OUT/last.txt"
    fi
  fi
fi
