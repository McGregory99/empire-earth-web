# Ask the game to close the way the user's close button does, part of the game-run library (sourced by lib.sh).
#
# A run that ends with `wineserver -k` shows the kill, not the game's own shutdown; the scenarios end the game cleanly first.
# ee-close.exe (scripts/ee-close.c) posts WM_CLOSE to the game's windows, then, if asked, WM_QUIT to the window thread.

GR_CLOSE_VIA=""

# <ee-close.exe (unix path)> <seconds to wait after WM_CLOSE> <seconds to wait after WM_QUIT> <desktop of the game>
# Sets GR_CLOSE_VIA to wm_close, wm_quit or killed.  Returns 0 when the game left by itself, 1 when it did not (the caller then
# stops the prefix, which gr_teardown does anyway).
gr_close_game() {
  local exe="$1" grace="$2" quit_wait="$3" desktop="$4"
  GR_CLOSE_VIA=wm_close
  gr_close_post "$exe" "$desktop"
  gr_wait_game_exit "$grace" && return 0
  gr_log "the game is still there ${grace}s after WM_CLOSE; posting WM_QUIT to its window thread"
  GR_CLOSE_VIA=wm_quit
  gr_close_post "$exe" "$desktop" --quit
  gr_wait_game_exit "$quit_wait" && return 0
  GR_CLOSE_VIA=killed
  gr_log "UNCLEAN: the game did not leave within ${quit_wait}s of WM_QUIT"
  return 1
}

gr_close_post() { # <exe> <desktop> [--quit]
  local exe="$1" desktop="$2" out
  shift 2
  out="$(WINEPREFIX="$GR_PREFIX" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" "$GR_WINE_ROOT/bin/wine" "$exe" "$@" "Empire Earth.exe" "$desktop" Default 2>&1 | tr '\n' ' ')"
  gr_log "ee-close.exe $*: ${out:-no output}"
}

gr_wait_game_exit() { # <seconds>: 0 when no game process is left
  local waited=0
  while gr_game_running; do
    [ "$waited" -ge "$1" ] && return 1
    sleep 1
    waited=$((waited + 1))
  done
  return 0
}
