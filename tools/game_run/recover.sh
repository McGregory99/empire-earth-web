#!/usr/bin/env bash
# Finish the restore of a run that did not (the driver was killed, or the game could not be stopped): run the undo steps,
# restore the game directory from the snapshot in GAME_RUN_STATE, verify the manifest, remove the state and the lock of the
# dead run.  Refuses while the game runs or while a live run holds the lock.
#
#   recover.sh
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib.sh
. "$HERE/lib.sh" # the defaults only: no test config, no environment (the tests run it with HOME in a fake world)
GR_TOOL=recover
gr_sanitize_env

if [ ! -e "$GAME_RUN_STATE" ]; then
  gr_log "nothing to recover: $GAME_RUN_STATE does not exist"
  exit 0
fi
state_game="$(sed -n 's/^game=//p' "$GAME_RUN_STATE/state.env" 2>/dev/null | head -1)"
if [ "$state_game" != "$GR_GAME" ]; then
  gr_log "the state belongs to another game directory ($state_game), not $GR_GAME; not touching it"
  exit 2
fi
gr_lock_acquire 5 || exit 2 # reclaims the lock of a dead run (loudly), refuses a live one
if gr_game_running; then
  gr_log "the game is running: stop it first with  WINEPREFIX='$GR_PREFIX' '$GR_WINE_ROOT/bin/wineserver' -k"
  gr_lock_release
  exit 2
fi
GR_STATE_OPEN=1
gr_run_undo || gr_log "WARNING: an undo step failed"
if gr_restore; then
  gr_state_finish
  gr_lock_release
  exit 0
fi
gr_log "recovery failed: the lock and the state are kept; see the messages above"
exit 1
