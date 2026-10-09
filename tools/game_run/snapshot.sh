# Snapshot and restore of the game directory, part of the game-run library (sourced by lib.sh).
#
# The truth is the sha256 manifest of the directory taken before the run (manifest.before) and its list of directories.
# The snapshot is a copy kept to restore from; it is verified against the manifest before anything is touched, so a
# partial copy aborts the run instead of becoming the restore source.  Restore copies back every file that changed or
# disappeared (verifying each against the manifest), deletes only files that are absent from manifest.before, and never
# deletes a file that the manifest lists, whatever the snapshot holds.  Everything lives in GAME_RUN_STATE, at a fixed
# path: a run that dies leaves it behind and the next run, of any tool, refuses to start (gr_check_leftovers) until
# recover.sh has restored it.

GR_STATE_OPEN=0

# <dir> <out>: "sha256  ./path" of every regular file, sorted bytewise.  0 only if every step worked.
gr_manifest() {
  local dir="$1" out="$2" files
  [ -d "$dir" ] || return 1
  files="$(cd "$dir" && find . -type f | wc -l | tr -d ' ')" || return 1
  if [ "$files" -eq 0 ]; then
    : >"$out"
    return 0
  fi
  ( set -o pipefail; cd "$dir" && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 shasum -a 256 ) >"$out" 2>/dev/null
}

# <dir> <out>: every directory, sorted bytewise.
gr_dirlist() {
  ( set -o pipefail; cd "$1" && find . -type d | LC_ALL=C sort ) >"$2" 2>/dev/null
}

gr_manifest_paths() { cut -c67- "$1" | LC_ALL=C sort; }

# Refuse to start over the leftovers of an earlier run: files of ours in the game directory (names given), and the state
# directory of a run that did not finish its restore.
gr_check_leftovers() {
  local f bad=0
  for f in "$@"; do
    if [ -e "$GR_GAME/$f" ]; then
      gr_log "leftover: $GR_GAME/$f exists"
      bad=1
    fi
  done
  if [ -e "$GAME_RUN_STATE" ]; then
    gr_log "leftover: $GAME_RUN_STATE exists (a run did not finish restoring the game directory)"
    bad=1
  fi
  if [ "$bad" -eq 1 ]; then
    gr_log "refusing to start. If a run was interrupted: bash '$GR_LIB_DIR/recover.sh'; otherwise check the leftovers and remove them by hand"
    return 1
  fi
  return 0
}

# The launcher reads the game from config.json (base_exe) and rewrites that directory.  Assert it is the one we restore.
gr_assert_config() {
  local base
  base="$(python3 -I -c 'import json, sys; print(json.load(open(sys.argv[1]))["base_exe"])' "$GR_CONFIG_JSON" 2>/dev/null)" || {
    gr_log "cannot read base_exe from $GR_CONFIG_JSON"
    return 1
  }
  case "$base" in
    "$GR_GAME"/*) return 0 ;;
  esac
  gr_log "config.json base_exe ($base) is not under the game directory this run restores ($GR_GAME); refusing"
  return 1
}

# Create the state directory.  Fails if one exists: that is the leftover of a run that did not finish.
gr_state_begin() {
  if [ -e "$GAME_RUN_STATE" ]; then
    gr_log "leftover: $GAME_RUN_STATE exists (a run did not finish restoring the game directory); not starting"
    return 1
  fi
  mkdir -p "$(dirname "$GAME_RUN_STATE")" && mkdir "$GAME_RUN_STATE" || return 1
  {
    echo "game=$GR_GAME"
    echo "tool=$GR_TOOL"
    echo "pid=$$"
    echo "time=$(date '+%F %T')"
  } >"$GAME_RUN_STATE/state.env"
  : >"$GAME_RUN_STATE/undo.sh"
  GR_STATE_OPEN=1
  return 0
}

# Manifest, directory list and a snapshot of the game directory, verified.  Non-zero (and no snapshot left) if the copy
# does not match the manifest: the caller must not start the game then.
gr_snapshot_take() {
  local s="$GAME_RUN_STATE"
  gr_manifest "$GR_GAME" "$s/manifest.before" && gr_dirlist "$GR_GAME" "$s/dirs.before" || {
    gr_log "FAIL: cannot take the manifest of $GR_GAME"
    return 1
  }
  rm -rf "$s/snapshot"
  if ! cp -cR "$GR_GAME" "$s/snapshot" 2>/dev/null; then
    rm -rf "$s/snapshot" # a failed clone can leave a partial copy: start the plain copy from nothing
    if ! cp -R "$GR_GAME" "$s/snapshot" 2>/dev/null; then
      rm -rf "$s/snapshot"
      gr_log "FAIL: cannot copy $GR_GAME to the snapshot"
      return 1
    fi
  fi
  if ! gr_manifest "$s/snapshot" "$s/manifest.snapshot" || ! cmp -s "$s/manifest.before" "$s/manifest.snapshot"; then
    gr_log "FAIL: the snapshot differs from the manifest of the game directory (an incomplete copy); aborting before anything is touched"
    rm -rf "$s/snapshot"
    return 1
  fi
  gr_log "snapshot of $GR_GAME verified ($(wc -l <"$s/manifest.before" | tr -d ' ') files)"
  return 0
}

# Put the game directory back exactly as the manifest says.  0 only when the final manifest and directory list equal the
# recorded ones.  3: the game is running, nothing was touched.  Otherwise 1, after doing everything that could be done.
gr_restore() {
  local s="$GAME_RUN_STATE" snap="$GAME_RUN_STATE/snapshot" fail=0 line want got path src
  [ -s "$s/manifest.before" ] && [ -f "$s/dirs.before" ] || {
    gr_log "FAIL: no manifest to restore from in $s"
    return 1
  }
  if gr_game_running; then
    gr_log "restore refused: the game is running"
    return 3
  fi
  gr_manifest "$GR_GAME" "$s/manifest.after" && gr_dirlist "$GR_GAME" "$s/dirs.after" || {
    gr_log "FAIL: cannot take the manifest of $GR_GAME"
    return 1
  }
  if cmp -s "$s/manifest.before" "$s/manifest.after" && cmp -s "$s/dirs.before" "$s/dirs.after"; then
    gr_log "the game directory already matches the manifest taken before the run"
    return 0
  fi
  gr_log "restoring $GR_GAME; what the run changed:"
  diff "$s/manifest.before" "$s/manifest.after" | sed -n 's/^[<>] [0-9a-f]*  /    /p' | LC_ALL=C sort -u >&2

  # directories that were removed
  LC_ALL=C comm -23 "$s/dirs.before" "$s/dirs.after" | while IFS= read -r path; do
    mkdir -p "$GR_GAME/$path"
  done
  # files the manifest lists that changed or disappeared: copy back from the snapshot, never delete
  while IFS= read -r line; do
    want="${line%%  *}"
    path="${line#*  }"
    src="$snap/$path"
    if [ ! -f "$src" ]; then
      gr_log "CANNOT RESTORE $path: the snapshot lacks it (the live file, if any, is left as it is)"
      fail=1
      continue
    fi
    mkdir -p "$(dirname "$GR_GAME/$path")"
    if ! cp -pf "$src" "$GR_GAME/$path"; then
      gr_log "CANNOT RESTORE $path: copy failed"
      fail=1
      continue
    fi
    got="$(shasum -a 256 "$GR_GAME/$path" | cut -c1-64)"
    if [ "$got" != "$want" ]; then
      gr_log "CANNOT RESTORE $path: the copy's hash is not the recorded one"
      fail=1
    fi
  done < <(diff "$s/manifest.before" "$s/manifest.after" | sed -n 's/^< //p')
  # files that were not there before: absent from manifest.before (not from the snapshot)
  while IFS= read -r path; do
    rm -f "$GR_GAME/$path"
  done < <(LC_ALL=C comm -13 <(gr_manifest_paths "$s/manifest.before") <(gr_manifest_paths "$s/manifest.after"))
  # directories that were added, deepest first
  while IFS= read -r path; do
    rmdir "$GR_GAME/$path" 2>/dev/null || {
      gr_log "CANNOT REMOVE the added directory $path (not empty)"
      fail=1
    }
  done < <(LC_ALL=C comm -13 "$s/dirs.before" "$s/dirs.after" | LC_ALL=C sort -r)

  gr_manifest "$GR_GAME" "$s/manifest.final" && gr_dirlist "$GR_GAME" "$s/dirs.final" || fail=1
  if [ "$fail" -eq 0 ] && cmp -s "$s/manifest.before" "$s/manifest.final" && cmp -s "$s/dirs.before" "$s/dirs.final"; then
    gr_log "game directory restored: manifest and directory list identical to the ones taken before the run"
    return 0
  fi
  gr_log "FAIL: the game directory does NOT match the manifest taken before the run"
  diff "$s/manifest.before" "$s/manifest.final" 2>/dev/null | head -20 >&2
  return 1
}

# Extra undo steps of a driver (a registry value, ...), run before the restore and by recover.sh.
gr_undo_add() {
  {
    printf '%q ' "$@"
    echo
  } >>"$GAME_RUN_STATE/undo.sh"
}

gr_run_undo() {
  [ -s "$GAME_RUN_STATE/undo.sh" ] || return 0
  bash "$GAME_RUN_STATE/undo.sh"
}

gr_state_finish() {
  rm -rf "$GAME_RUN_STATE"
  GR_STATE_OPEN=0
}

gr_print_recovery() {
  gr_log "RECOVERY NEEDED: the run could not put everything back. The game-run lock $GAME_RUN_LOCK and the snapshot in $GAME_RUN_STATE are KEPT."
  gr_log "  1. stop the game:  WINEPREFIX='$GR_PREFIX' '$GR_WINE_ROOT/bin/wineserver' -k"
  gr_log "  2. restore:        bash '$GR_LIB_DIR/recover.sh'"
  gr_log "  recover.sh runs the undo steps, restores from the snapshot, verifies the manifest, removes the state and the dead run's lock."
}

# The teardown every driver uses (as, or from, its cleanup function): stop the launcher, stop the game, undo, restore,
# release the lock.  Returns 0 only if everything is back.  If the game cannot be stopped, or the restore fails, the lock
# and the snapshot are kept, the recovery commands are printed and the result is non-zero.
gr_teardown() {
  local rc=0
  gr_kill_launcher
  if [ "$GR_STATE_OPEN" -eq 1 ]; then
    if ! gr_stop_game; then
      gr_log "FAIL: the game could not be stopped"
      gr_print_recovery
      return 1
    fi
    gr_run_undo || {
      gr_log "FAIL: an undo step failed"
      rc=1
    }
    gr_restore || rc=1
    if [ "$rc" -ne 0 ]; then
      gr_print_recovery
      return 1
    fi
    gr_state_finish
  fi
  gr_lock_release
  return 0
}
