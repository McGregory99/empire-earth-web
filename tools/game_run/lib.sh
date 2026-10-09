# Shared library of the scripts that run the real game on this Mac (tests/dx7_rasterizer/game_check.sh and
# tools/ddraw_trace/scripts/game-trace.sh).  bash 3.2 compatible (the macOS /bin/bash).
#
# The game copy, its Wine prefix and the 00xJS launcher are shared by every agent, so every run follows one protocol:
#   - the game-run lock, a directory at one fixed path, with an owner token (pid and start time) inside;
#   - never start while any program of the game (or of Art of Conquest) is running;
#   - on any exit path, including signals: stop what we started, put every file back, release the lock;
#   - if that cannot be done, keep the lock and the snapshot, say how to recover and exit non-zero.
#
# This file holds the configuration, the lock, game detection, the signal-safe teardown and the launcher job.  The
# snapshot and restore of the game directory are in snapshot.sh, the Wine wrapper in wrapper.sh (sourced below).
#
# Everything is a plain variable with a fixed default.  Nothing is read from the caller's environment: a test sets the
# variables after sourcing (tests/game_run/world.sh), a driver may load such overrides from GR_TEST_CONFIG
# (gr_test_hook), and an ambient EMPIRE_EARTH_*, EE_*, WINE* or RE_DIR changes nothing.

GR_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The lock and the state directory live at fixed places: not under RE_DIR, not in a repository's build directory, so
# two checkouts or two agents with different environments still exclude each other.
GAME_RUN_LOCK="$HOME/Games/EmpireEarth-re/locks/game-run"
GAME_RUN_STATE="$HOME/Games/EmpireEarth-re/game-run-state"
GR_SUPPORT="$HOME/Library/Application Support/EmpireEarthMac"
GR_PREFIX="$GR_SUPPORT/wineprefix"
GR_GAME="$GR_PREFIX/drive_c/GOG Games/Empire Earth Gold Edition/Empire Earth"
GR_WINE_ROOT="$GR_SUPPORT/runtime/Wine Stable.app/Contents/Resources/wine"
GR_LAUNCHER_DIR="$HOME/Games/tools/ee-macos"
GR_LOG_DIR="$GR_SUPPORT/logs"
GR_CONFIG_JSON="$GR_SUPPORT/config.json"
GR_MIN_RSS_KB=1024  # the launcher's own threshold (lib.sh game_pids): smaller is a Rosetta process stuck mid-exit
GR_LOCK_WAIT=3600   # seconds to wait for another agent's run
GR_LOCK_POLL=10
GR_STOP_WAIT=60     # seconds to wait for the game to disappear after wineserver -k (twice)
GR_TOOL="${GR_TOOL:-game-run}"
GR_LOG_FILE=""

gr_log() {
  printf '%s %s: %s\n' "$(date '+%H:%M:%S')" "$GR_TOOL" "$*" >&2
  if [ -n "$GR_LOG_FILE" ]; then
    printf '%s %s: %s\n' "$(date '+%H:%M:%S')" "$GR_TOOL" "$*" >>"$GR_LOG_FILE" 2>/dev/null
  fi
  return 0
}

# Tests (and only tests) override the fixed defaults above through a file named by GR_TEST_CONFIG.
gr_test_hook() {
  [ -n "${GR_TEST_CONFIG:-}" ] || return 0
  if [ ! -f "$GR_TEST_CONFIG" ]; then
    # A test asked for a fake world that is not there (removed, mistyped): going on would run against the real installation.
    gr_log "FAIL: GR_TEST_CONFIG=$GR_TEST_CONFIG does not exist; refusing to run against the real game"
    exit 2
  fi
  # shellcheck source=/dev/null
  . "$GR_TEST_CONFIG"
  gr_log "TEST CONFIG ACTIVE: paths come from $GR_TEST_CONFIG, not from the real installation"
  return 0
}

# Unset what could redirect or reconfigure the launcher behind the driver's back: the launcher reads
# EMPIRE_EARTH_SUPPORT_DIR / EMPIRE_EARTH_WINEPREFIX / EMPIRE_EARTH_WINE, every EE_* switch, and the standard WINE*
# variables; a stale WINEDEBUG changes the logs the checks parse.  The driver sets the ones it wants, explicitly.
gr_sanitize_env() {
  local v
  for v in $(compgen -e); do
    case "$v" in
      EE_* | EMPIRE_EARTH_* | WINE | WINELOADER | WINESERVER | WINEPREFIX | WINEDEBUG | WINEDLLOVERRIDES | WINEARCH | \
        WINEDLLPATH | DXVK_* | MVK_*)
        unset "$v"
        ;;
    esac
  done
  return 0
}

# ---- game detection ---------------------------------------------------------------------------------------------------

# The launcher's own test (scripts/lib.sh game_pids): the command line must START with a Windows path to Empire
# Earth.exe or EE-AOC.exe, so an agent's `pgrep -f '...Empire Earth.exe'` loop, an editor, or the launcher's own
# `wine reg add ...\Empire Earth.exe\...` never matches; and the process must have real memory, so a Rosetta process
# that was killed and lingers in the table does not count.  Art of Conquest counts: the launcher kills it by name.
gr_game_pids() {
  ps -axo pid=,rss=,args= 2>/dev/null | awk -v min="$GR_MIN_RSS_KB" '
    { pid = $1; rss = $2; $1 = ""; $2 = ""; sub(/^  /, "") }
    rss > min && $0 ~ /^[A-Za-z]:\\.*\\(Empire Earth|EE-AOC)\.exe/ { print pid }'
}

gr_game_running() { [ -n "$(gr_game_pids)" ]; }

# Everything that blocks a start: the game, Art of Conquest, and any other program run from the GOG install
# (dxcfg.exe, rmvparse.exe, ...), which would be disturbed by the launcher's kill of the prefix.
gr_blocking_pids() {
  ps -axo pid=,rss=,args= 2>/dev/null | awk -v min="$GR_MIN_RSS_KB" '
    { pid = $1; rss = $2; $1 = ""; $2 = ""; sub(/^  /, "") }
    rss > min && ($0 ~ /^[A-Za-z]:\\.*\\(Empire Earth|EE-AOC)\.exe/ || tolower($0) ~ /^[a-z]:\\gog games\\/) { print pid }'
}

# End every process of the game's prefix with the prefix's own wineserver (nothing else is killed), and wait until no
# game process is left.  Returns 0 when the game is gone.
gr_stop_game() {
  local waited=0 round
  for round in 1 2; do
    WINEPREFIX="$GR_PREFIX" "$GR_WINE_ROOT/bin/wineserver" -k >/dev/null 2>&1
    waited=0
    while gr_game_running; do
      [ "$waited" -ge "$GR_STOP_WAIT" ] && break
      sleep 1
      waited=$((waited + 1))
    done
    gr_game_running || return 0
  done
  return 1
}

# ---- the game-run lock ------------------------------------------------------------------------------------------------

GR_HAVE_LOCK=0
GR_OWNER_PID=$$

gr_proc_lstart() { LC_ALL=C ps -o lstart= -p "$1" 2>/dev/null | sed 's/^ *//; s/ *$//'; }

gr_lock_write_owner() {
  {
    echo "pid=$$"
    echo "lstart=$(gr_proc_lstart $$)"
    echo "tool=$GR_TOOL"
    echo "time=$(date '+%F %T')"
  } >"$GAME_RUN_LOCK/owner"
}

# alive | dead | unknown, for the lock that is on disk now.  "dead" only when the owner is provably gone: its pid is
# not in the process table, or the pid now belongs to a process that started at another time.  A lock without a token
# (made by an agent that does not write one) is never judged.
gr_lock_owner_state() {
  local f="$GAME_RUN_LOCK/owner" pid lstart now
  [ -f "$f" ] || { echo unknown; return 0; }
  pid="$(sed -n 's/^pid=//p' "$f" | head -1)"
  lstart="$(sed -n 's/^lstart=//p' "$f" | head -1)"
  case "$pid" in '' | *[!0-9]*) echo unknown; return 0 ;; esac
  if ! kill -0 "$pid" 2>/dev/null; then
    # kill -0 also fails for a live process of another user: ask the process table
    if LC_ALL=C ps -p "$pid" >/dev/null 2>&1; then echo alive; else echo dead; fi
    return 0
  fi
  now="$(gr_proc_lstart "$pid")"
  if [ -n "$lstart" ] && [ -n "$now" ] && [ "$now" != "$lstart" ]; then echo dead; else echo alive; fi
}

gr_lock_describe_owner() {
  if [ -f "$GAME_RUN_LOCK/owner" ]; then
    tr '\n' ' ' <"$GAME_RUN_LOCK/owner"
  else
    printf 'no owner token (made by an agent that does not write one)'
  fi
}

# Take the lock: wait while a live owner holds it, reclaim it only when the owner is provably dead.
# Returns 0 (held), 2 (still held by someone else after the wait).
gr_lock_acquire() {
  local wait="${1:-$GR_LOCK_WAIT}" began=$SECONDS state stale announced=0
  mkdir -p "$(dirname "$GAME_RUN_LOCK")"
  while :; do
    if mkdir "$GAME_RUN_LOCK" 2>/dev/null; then
      gr_lock_write_owner
      GR_HAVE_LOCK=1
      GR_OWNER_PID=$$
      gr_log "took the game-run lock $GAME_RUN_LOCK"
      return 0
    fi
    state="$(gr_lock_owner_state)"
    if [ "$state" = dead ]; then
      gr_log "RECLAIMING STALE LOCK $GAME_RUN_LOCK: its owner is dead ($(gr_lock_describe_owner)); the run that held it did not clean up, so the game directory may need a recovery (see GAME_RUN_STATE)"
      stale="$GAME_RUN_LOCK.stale.$$.$(date +%s)"
      if mv "$GAME_RUN_LOCK" "$stale" 2>/dev/null; then
        rm -rf "$stale"
      fi
      continue
    fi
    if [ $((SECONDS - began)) -ge "$wait" ]; then
      gr_log "the game-run lock $GAME_RUN_LOCK is held by a live run ($(gr_lock_describe_owner)); gave up after ${wait}s"
      return 2
    fi
    if [ "$announced" -eq 0 ]; then
      gr_log "waiting for the game-run lock: held by $(gr_lock_describe_owner)"
      announced=1
    fi
    sleep "$GR_LOCK_POLL"
  done
}

gr_lock_is_ours() {
  local f="$GAME_RUN_LOCK/owner"
  [ -f "$f" ] || return 1
  [ "$(sed -n 's/^pid=//p' "$f" | head -1)" = "$GR_OWNER_PID" ] || return 1
  [ "$(sed -n 's/^lstart=//p' "$f" | head -1)" = "$(gr_proc_lstart "$GR_OWNER_PID")" ]
}

# Remove the lock, but only our own.
gr_lock_release() {
  [ "$GR_HAVE_LOCK" -eq 1 ] || return 0
  if gr_lock_is_ours; then
    rm -f "$GAME_RUN_LOCK/owner"
    if rmdir "$GAME_RUN_LOCK" 2>/dev/null; then
      gr_log "released the game-run lock"
    else
      gr_log "WARNING: could not remove $GAME_RUN_LOCK"
    fi
  else
    gr_log "WARNING: $GAME_RUN_LOCK is no longer ours ($(gr_lock_describe_owner)); not removing it"
  fi
  GR_HAVE_LOCK=0
  return 0
}

# ---- signal-safe teardown ---------------------------------------------------------------------------------------------

GR_CLEANUP_FN=""

# Install the traps.  <cleanup function> undoes the run and returns non-zero when something is left behind.  A signal
# makes the shell exit with 128+n; the EXIT trap then runs the cleanup with signals ignored, so a second Ctrl-C (or a
# HUP when the terminal closes) cannot cut it short, and the interrupted status survives.
gr_install_traps() {
  GR_CLEANUP_FN="$1"
  trap 'gr_on_exit' EXIT
  trap 'exit 130' INT
  trap 'exit 143' TERM
  trap 'exit 129' HUP
  trap 'exit 131' QUIT
}

gr_on_exit() {
  local rc=$? crc=0
  trap '' INT TERM HUP QUIT
  trap - EXIT
  if [ -n "$GR_CLEANUP_FN" ]; then
    "$GR_CLEANUP_FN" || crc=$?
  fi
  if [ "$rc" -eq 0 ] && [ "$crc" -ne 0 ]; then
    rc=$crc
  fi
  exit "$rc"
}

# ---- the launcher as a background job -----------------------------------------------------------------------------------

GR_LAUNCHER_PID=""
GR_LAUNCHER_PGID=""

# Run <command...> as a background job in a process group of its own, with a time limit (SIGALRM), and wait for it.  `wait`
# is interruptible, so a trapped signal ends the wait at once; the teardown then kills the group.
gr_run_launcher() {
  local limit="$1" log="$2" rc=0
  shift 2
  # (exec { $ARGV[0] } @ARGV: a one-element list would otherwise go through the shell and split at spaces)
  perl -e 'setpgrp(0, 0); alarm shift; exec { $ARGV[0] } @ARGV; print STDERR "cannot run $ARGV[0]: $!\n"; exit 127' \
    "$limit" "$@" >"$log" 2>&1 &
  GR_LAUNCHER_PID=$!
  GR_LAUNCHER_PGID=$!
  wait "$GR_LAUNCHER_PID" || rc=$?
  GR_LAUNCHER_PID=""
  return "$rc"
}

# Kill what is left of the launcher: its process group (the launcher, its watchers, a `wine start` client).  The game
# itself is not in the group; it belongs to the prefix's wineserver and is stopped with gr_stop_game.
gr_kill_launcher() {
  local pgid="$GR_LAUNCHER_PGID" i
  [ -n "$pgid" ] || return 0
  if kill -0 -- "-$pgid" 2>/dev/null; then
    kill -TERM -- "-$pgid" 2>/dev/null
    for i in 1 2 3 4 5 6 7 8 9 10; do
      kill -0 -- "-$pgid" 2>/dev/null || break
      sleep 0.3
    done
    kill -0 -- "-$pgid" 2>/dev/null && kill -KILL -- "-$pgid" 2>/dev/null
  fi
  [ -n "$GR_LAUNCHER_PID" ] && wait "$GR_LAUNCHER_PID" 2>/dev/null
  GR_LAUNCHER_PID=""
  GR_LAUNCHER_PGID=""
  return 0
}

# shellcheck source=snapshot.sh
. "$GR_LIB_DIR/snapshot.sh"
# shellcheck source=wrapper.sh
. "$GR_LIB_DIR/wrapper.sh"
