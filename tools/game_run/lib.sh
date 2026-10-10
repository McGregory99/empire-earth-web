# Shared library of the scripts that run the real game on this Mac: tests/dx7_rasterizer/game_check.sh and
# tools/ddraw_trace/scripts/game-trace.sh.  bash 3.2 compatible (the macOS /bin/bash).
#
# The game copy, its Wine prefix and the 00xJS launcher are shared by every agent, so every run follows one protocol:
#   - the game-run lock, a directory at one fixed path, with an owner token (pid and start time) inside;
#   - never start while any program of the game (or of Art of Conquest) is running;
#   - on any exit path, including signals: stop what we started, put every file back, release the lock;
#   - if that cannot be done, keep the lock and the snapshot, say how to recover and exit non-zero.
#
# A driver sources this file, which sources the two others at its end:
#   lib.sh       the configuration, game detection, the lock, the traps (gr_install_traps) and the launcher job;
#   snapshot.sh  the state directory, the verified snapshot and the exact restore of the game directory, and the teardown that
#                ties it all together (gr_teardown);
#   wrapper.sh   the Wine wrapper that lets game-trace.sh put its DLL in place at the moment the game starts, and the
#                launcher.log check for the version.dll shim (game_check.sh uses only the check).
# recover.sh is a script of its own, not sourced: it finishes the restore of a run that died.
#
# Everything is a plain variable with a fixed default.  Nothing is read from the caller's environment: an ambient
# EMPIRE_EARTH_*, EE_*, WINE*, RE_DIR, GR_TOOL, GR_KEEP_LOCK or GR_TEST_CONFIG changes nothing.  A test replaces defaults only by
# naming a file to gr_test_hook (the drivers' --test-config option, tests/game_run/world.sh writes the file); recover.sh has no
# such option and always runs on the defaults, which follow $HOME.

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
GR_TOOL=game-run
GR_LOG_FILE=""

gr_log() {
  printf '%s %s: %s\n' "$(date '+%H:%M:%S')" "$GR_TOOL" "$*" >&2
  if [ -n "$GR_LOG_FILE" ]; then
    printf '%s %s: %s\n' "$(date '+%H:%M:%S')" "$GR_TOOL" "$*" >>"$GR_LOG_FILE" 2>/dev/null
  fi
  return 0
}

# <config file>: a test (and only a test) replaces the fixed defaults above by sourcing a file it names; the drivers call this
# with their --test-config argument, so that a config is loaded only when somebody asked for it, never from the environment.
# Without a file nothing happens.
gr_test_hook() {
  [ -n "${1:-}" ] || return 0
  if [ ! -f "$1" ]; then
    # A test asked for a fake world that is not there (removed, mistyped): going on would run against the real installation.
    gr_log "FAIL: test config $1 does not exist; refusing to run against the real game"
    exit 2
  fi
  # shellcheck source=/dev/null
  . "$1"
  gr_log "TEST CONFIG ACTIVE: paths come from $1, not from the real installation"
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
#
# The lock is a directory at one fixed path holding the owner token (owner: pid, process start time, tool, time).  Two rules
# keep it sound when several runs start, die and reclaim at the same moment:
#   - a lock appears with its token already inside: the token is written in a private directory next to the lock and the whole
#     directory is renamed into place (rename(2) fails if the lock exists and is not empty, and a lock with a token never is),
#     so a run killed at any point leaves either nothing or a complete lock, never one that no one can judge;
#   - a dead owner's lock is taken over in place, never removed: the dead token is renamed to a name of our own (exactly one
#     claimant can do that), compared with what was judged, and our token is linked in (a hard link never replaces a token).
#     The directory stays, with something in it, from the judgement to the end, so nothing can slip in between.
# A lock without any token (a foreign agent's bare `mkdir`) is never judged: it is waited for until the time runs out.

GR_HAVE_LOCK=0
GR_OWNER_PID=$$
GR_LOCK_SEQ=0

# Test seam, inert here: a test redefines it after sourcing to stop a process at a named point of the lock protocol
# (judged-dead, pre-token), so that an interleaving of two processes can be forced without sleeps.
gr_lock_hook() { :; }

gr_proc_lstart() { LC_ALL=C ps -o lstart= -p "$1" 2>/dev/null | sed 's/^ *//; s/ *$//'; }

# This process's owner token, on stdout.
gr_lock_token() {
  echo "pid=$$"
  echo "lstart=$(gr_proc_lstart $$)"
  echo "tool=$GR_TOOL"
  echo "time=$(date '+%F %T')"
}

# <token text>: alive | dead | unknown.  "dead" only when the owner is provably gone: its pid is not in the process table, or
# the pid now belongs to a process that started at another time.  Text without a pid is never judged.
gr_token_state() {
  local pid lstart now
  pid="$(printf '%s\n' "$1" | sed -n 's/^pid=//p' | head -1)"
  lstart="$(printf '%s\n' "$1" | sed -n 's/^lstart=//p' | head -1)"
  case "$pid" in '' | *[!0-9]*) echo unknown; return 0 ;; esac
  if ! kill -0 "$pid" 2>/dev/null; then
    # kill -0 also fails for a live process of another user: ask the process table
    if LC_ALL=C ps -p "$pid" >/dev/null 2>&1; then echo alive; else echo dead; fi
    return 0
  fi
  now="$(gr_proc_lstart "$pid")"
  if [ -n "$lstart" ] && [ -n "$now" ] && [ "$now" != "$lstart" ]; then echo dead; else echo alive; fi
}

# The file that holds the owner's token: owner, or, while a reclaim is under way (or was cut off), the claim file it moved it to.
gr_lock_token_file() {
  local f
  if [ -f "$GAME_RUN_LOCK/owner" ]; then
    echo "$GAME_RUN_LOCK/owner"
    return 0
  fi
  for f in "$GAME_RUN_LOCK"/owner.claim.*; do
    if [ -f "$f" ]; then
      echo "$f"
      return 0
    fi
  done
  return 1
}

gr_lock_describe_owner() {
  local f
  if f="$(gr_lock_token_file)"; then
    tr '\n' ' ' <"$f"
  else
    printf 'no owner token (made by an agent that does not write one)'
  fi
}

# Build our lock privately: <dir> holding our token.  0 only if the token was written completely.
gr_lock_stage() {
  rm -rf "$1"
  mkdir "$1" 2>/dev/null || return 1
  gr_lock_token >"$1/owner" 2>/dev/null && [ -s "$1/owner" ] || {
    rm -rf "$1"
    return 1
  }
}

# Private staging directories of processes that died before they could publish or remove them.
gr_lock_sweep() {
  local d
  for d in "$GAME_RUN_LOCK".new.*; do
    [ -f "$d/owner" ] || continue
    [ "$(gr_token_state "$(cat "$d/owner" 2>/dev/null)")" = dead ] && rm -rf "$d"
  done
  return 0
}

# Create the lock with our token in it.  0: it is ours.  1: there is a lock already.  2: it cannot be made.
gr_lock_create() {
  local stage="$GAME_RUN_LOCK.new.$$.$GR_LOCK_SEQ"
  [ -e "$GAME_RUN_LOCK" ] && return 1
  gr_lock_stage "$stage" || return 2
  gr_lock_hook pre-token
  # rename(2) fails over a lock with a token (never empty) but would replace an EMPTY directory, such as a foreign agent's bare
  # `mkdir` lock.  The test above and the rename are not one step, so such a lock made in these few microseconds is lost;
  # accepted, since no tool of this repository makes one, and the alternative (mkdir, then the token) is the tokenless window.
  if [ ! -e "$GAME_RUN_LOCK" ] && perl -e 'rename($ARGV[0], $ARGV[1]) or exit 1' "$stage" "$GAME_RUN_LOCK" 2>/dev/null; then
    return 0
  fi
  rm -rf "$stage"
  [ -e "$GAME_RUN_LOCK" ] && return 1
  return 2
}

# Take over the lock of an owner that was judged dead: <token file> and <its text>, as they were when it was judged.  The lock
# may have changed hands since (reclaimed by another waiter, released, taken by a live run), so nothing is done on the strength
# of the judgement: what we move is compared with it, and anything else is put back.  0: the lock is ours.
gr_lock_reclaim() {
  local tok="$1" judged="$2" claim stage moved
  claim="$GAME_RUN_LOCK/owner.claim.$$.$GR_LOCK_SEQ"
  stage="$GAME_RUN_LOCK.new.$$.$GR_LOCK_SEQ"
  mv "$tok" "$claim" 2>/dev/null || return 1 # another claimant was first, or the lock is gone
  [ -f "$claim" ] || return 1                # ... or took the claim file we were healing from
  moved="$(cat "$claim" 2>/dev/null)"
  if [ "$moved" != "$judged" ] || [ "$(gr_token_state "$moved")" != dead ]; then
    gr_log "the game-run lock changed hands while it was being reclaimed (it now holds: $(printf '%s' "$moved" | tr '\n' ' ')); putting its token back"
    if ln "$claim" "$tok" 2>/dev/null; then
      rm -f "$claim"
    else
      gr_log "WARNING: could not put the token back; it is kept as $claim"
    fi
    return 1
  fi
  gr_log "RECLAIMING STALE LOCK $GAME_RUN_LOCK: its owner is dead ($(printf '%s' "$judged" | tr '\n' ' ')); the run that held it did not clean up, so the game directory may need a recovery (see GAME_RUN_STATE)"
  if gr_lock_stage "$stage" && ln "$stage/owner" "$GAME_RUN_LOCK/owner" 2>/dev/null; then
    rm -rf "$stage"
    rm -f "$claim"
    return 0
  fi
  rm -rf "$stage"
  # somebody linked their token first: the claim file is only a copy of the dead one; otherwise it stays, and keeps the lock reclaimable
  [ -f "$GAME_RUN_LOCK/owner" ] && rm -f "$claim"
  return 1
}

# Take the lock: wait while a live owner holds it, take it over only when the owner is provably dead.
# Returns 0 (held), 1 (cannot be made), 2 (still held by someone else after the wait).
gr_lock_acquire() {
  local wait="${1:-$GR_LOCK_WAIT}" began=$SECONDS tok text state rc announced=0
  mkdir -p "$(dirname "$GAME_RUN_LOCK")" || return 1
  gr_lock_sweep
  while :; do
    GR_LOCK_SEQ=$((GR_LOCK_SEQ + 1))
    gr_lock_create
    rc=$?
    if [ "$rc" -eq 0 ]; then
      GR_HAVE_LOCK=1
      GR_OWNER_PID=$$
      gr_log "took the game-run lock $GAME_RUN_LOCK"
      return 0
    fi
    if [ "$rc" -eq 2 ]; then
      gr_log "FAIL: cannot create the game-run lock $GAME_RUN_LOCK"
      return 1
    fi
    state=unknown
    if tok="$(gr_lock_token_file)"; then
      text="$(cat "$tok" 2>/dev/null)"
      state="$(gr_token_state "$text")"
    fi
    if [ "$state" = dead ]; then
      gr_lock_hook judged-dead
      if gr_lock_reclaim "$tok" "$text"; then
        GR_HAVE_LOCK=1
        GR_OWNER_PID=$$
        gr_log "took the game-run lock $GAME_RUN_LOCK over from the dead run"
        return 0
      fi
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
# A driver that still has to read back what the teardown restored sets this to 1 so that gr_teardown keeps the lock a little
# longer.  The exit path never keeps it (gr_on_exit): once the process is leaving, nothing is left to read back.
GR_KEEP_LOCK=0

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
  GR_KEEP_LOCK=0
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
# The child makes the group as its first act, a moment after the fork: until then there is no group to signal, only the child,
# which must be stopped before it starts anything.  So the child is signalled first, then the group (which may exist by now).
gr_kill_launcher() {
  local pgid="$GR_LAUNCHER_PGID" i
  [ -n "$pgid" ] || return 0
  [ -n "$GR_LAUNCHER_PID" ] && kill -TERM "$GR_LAUNCHER_PID" 2>/dev/null
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
