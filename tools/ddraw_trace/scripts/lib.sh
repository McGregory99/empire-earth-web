# Shared helpers of the game driver: logging, the game-run lock, process checks, manifests and the byte-exact
# restore of the game directory.  Sourced by game-trace.sh and by tests/ddraw_trace/test-restore.sh.
# Callers set GAME_DIR, OUT, and (for the lock and process helpers) PREFIX, WINE_ROOT, LOCK, LOCK_WAIT, EXIT_WAIT.

log() { printf '%s game-trace: %s\n' "$(date '+%H:%M:%S')" "$*" | tee -a "${LOG_FILE:-$OUT/game-trace.log}" >&2; }

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
    diff "$OUT/manifest.before" "$OUT/manifest.after" | grep '^[<>]' | awk '{ $2 = ""; print }' | sed 's/^/    /' | sort -u | tee -a "${LOG_FILE:-$OUT/game-trace.log}" >&2
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
      diff "$OUT/manifest.before" "$OUT/manifest.after" | head -20 | tee -a "${LOG_FILE:-$OUT/game-trace.log}" >&2
      rm -rf "$OUT/snapshot-keep"
      mv "$snapshot" "$OUT/snapshot-keep"
      snapshot=""
      return 1
    fi
  fi
  rm -rf "$snapshot"
  snapshot=""
}

