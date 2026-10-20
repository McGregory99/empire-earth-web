# Test support for the game-run library: a fake world with spaces in its paths, a fake ps, fake wine and a fake
# launcher.  Sourced by the tests; nothing here touches the real game, Wine or launcher.
#
# The fake game is a line in $W/ps.out (what the fake `ps -axo pid=,rss=,args=` prints), not a real process, so a test
# can never be mistaken for the real game by another agent's check.

fails=0
checks=0
check() { # check <description> <command...>
  local what="$1"
  shift
  checks=$((checks + 1))
  if "$@"; then echo "ok   $what"; else echo "FAIL $what"; fails=$((fails + 1)); fi
}
check_not() { # check_not <description> <command...>: the command must fail
  local what="$1"
  shift
  checks=$((checks + 1))
  if "$@" >/dev/null 2>&1; then echo "FAIL $what"; fails=$((fails + 1)); else echo "ok   $what"; fi
}
check_eq() { # check_eq <description> <expected> <actual>
  checks=$((checks + 1))
  if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1 (expected '$2', got '$3')"; fails=$((fails + 1)); fi
}
summary() {
  echo "$([ "$fails" -eq 0 ] && echo PASS || echo FAIL): $checks checks, $fails failed"
  [ "$fails" -eq 0 ]
}

# Wait up to N tenths of a second for a command to succeed.
wait_for() { # wait_for <tenths> <command...>
  local n=$1 i=0
  shift
  while ! "$@" 2>/dev/null; do
    i=$((i + 1))
    [ "$i" -ge "$n" ] && return 1
    sleep 0.1
  done
}

# Start a command the way a user's shell does, so that INT and QUIT are not ignored (a plain `&` in a non-interactive
# shell ignores them, and a trap cannot re-enable a signal that was ignored on entry).
start_job() { perl -e '$SIG{INT} = "DEFAULT"; $SIG{QUIT} = "DEFAULT"; exec @ARGV' "$@" & }

# The real HOME, kept for the few checks that look at the real launcher's source.  world_make then moves HOME into the fake
# world, so that the fixed default paths of the library (~/Games/EmpireEarth-re/locks/game-run, ~/Library/Application
# Support/EmpireEarthMac, ~/Games/tools/ee-macos) resolve inside it: a driver that, by some mistake, does not load the test
# config still cannot reach the real game, lock or launcher.
REAL_HOME="${REAL_HOME:-$HOME}"

TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_DIR="$(cd "$TESTS_DIR/../../tools/game_run" && pwd)"

world_make() {
  W="$(mktemp -d "${TMPDIR:-/tmp}/gr world.XXXXXX")"
  W="$(cd "$W" && pwd -P)"
  W_HOME="$W/home"
  local sup="$W_HOME/Library/Application Support/EmpireEarthMac"
  local wine="$sup/runtime/Wine Stable.app/Contents/Resources/wine"
  W_SUP="$sup"
  W_PREFIX="$sup/wineprefix"
  W_GAME="$W_PREFIX/drive_c/GOG Games/Empire Earth Gold Edition/Empire Earth"
  W_WINE_ROOT="$wine"
  W_LAUNCHER="$W_HOME/Games/tools/ee-macos"
  mkdir -p "$W/bin" "$W_HOME/Games/EmpireEarth-re/locks" "$W_GAME/Data" "$W_GAME/Users" "$sup/logs" "$wine/bin" \
    "$wine/lib/wine/i386-windows" "$wine/share" "$W_LAUNCHER/scripts"
  : >"$W/ps.out"
  export FAKE_PS_FILE="$W/ps.out" FAKE_WORLD="$W"

  # the game directory
  printf 'launcher ddraw v1' >"$W_GAME/ddraw.dll"
  printf 'D7VK' >"$W_GAME/ddraw_eeorig.dll"
  printf 'shim v1' >"$W_GAME/version.dll"
  printf 'wine version' >"$W_GAME/version_eeorig.dll"
  printf 'exe' >"$W_GAME/Empire Earth.exe"
  printf 'payload a' >"$W_GAME/Data/a.bin"
  printf 'payload b' >"$W_GAME/Data/b file.bin"
  printf 'setting=1' >"$W_GAME/Users/cfg"
  printf 'log0' >"$W_GAME/ee-ddraw.log"
  printf 'wine version dll' >"$wine/lib/wine/i386-windows/version.dll"
  printf '{ "base_exe": "%s/Empire Earth.exe", "game_dir": "%s" }\n' "$W_GAME" "$W_GAME" >"$sup/config.json"

  cat >"$W/bin/ps" <<'PS'
#!/bin/bash
# fake ps: the process listing the library asks for comes from a file; everything else is the real ps
if [ "$*" = "-axo pid=,rss=,args=" ]; then
  cat "$FAKE_PS_FILE" 2>/dev/null
  exit 0
fi
exec /bin/ps "$@"
PS
  chmod +x "$W/bin/ps"

  # fake Wine: `start /unix <exe>` starts the fake game (a line in ps.out) and, when the proxy's config is present,
  # plays the proxy's part (writes the trace and its .done marker)
  cat >"$wine/bin/wine" <<'WINE'
#!/bin/bash
echo "wine $*" >>"$FAKE_WORLD/wine.log"
if [ "${1:-}" = reg ]; then
  # one file per value name; SnoopInclude keeps its historical file name (snoop.value)
  name=""
  args=("$@")
  for ((i = 0; i < ${#args[@]}; i++)); do [ "${args[$i]}" = /v ] && name="${args[$((i + 1))]}"; done
  value_file="$FAKE_WORLD/reg.$name"
  [ "$name" = SnoopInclude ] && value_file="$FAKE_WORLD/snoop.value"
  case "${2:-}" in
    query)
      # a test can stop a driver in its read-back window (after the restore: the state directory is gone) and release it
      if [ -e "$FAKE_WORLD/readback-hang" ] && [ ! -e "${FAKE_STATE:-/nonexistent}" ]; then
        : >"$FAKE_WORLD/readback.ready"
        i=0
        while [ ! -e "$FAKE_WORLD/readback-go" ] && [ "$i" -lt 100 ]; do sleep 0.1; i=$((i + 1)); done
      fi
      [ -f "$value_file" ] || exit 1
      printf '\r\nHKEY_CURRENT_USER\\Software\\Wine\\Debug\r\n    %s    REG_SZ    %s\r\n\r\n' "$name" "$(cat "$value_file")"
      ;;
    add)
      while [ $# -gt 0 ]; do [ "$1" = /d ] && { printf '%s' "$2" >"$value_file"; break; }; shift; done
      ;;
    delete) rm -f "$value_file" ;;
  esac
  exit 0
fi
case "${1:-}" in
  *ee-key.exe)
    echo "key-helper $*" >>"$FAKE_WORLD/key.log"
    [ -e "$FAKE_WORLD/not-paused" ] || : >"$FAKE_WORLD/paused"   # the fake game stops ticking when it gets its pause key
    exit 0
    ;;
  *ee-close.exe)
    echo "close-helper $*" >>"$FAKE_WORLD/close.log"
    [ -e "$FAKE_WORLD/close-needs-quit" ] && [ "${2:-}" != --quit ] && exit 0
    [ -e "$FAKE_WORLD/close-ignored" ] || { grep -v 'Empire Earth.exe\|EE-AOC.exe' "$FAKE_PS_FILE" >"$FAKE_PS_FILE.new"; mv "$FAKE_PS_FILE.new" "$FAKE_PS_FILE"; }
    exit 0
    ;;
esac
if [ "${1:-}" = start ] && [ "${2:-}" = /unix ]; then
  game_dir="${FAKE_GAME:-$(dirname "$3")}"
  case "$3" in
    *ee-start.exe)
      # what the helper would start, and what the game finds in its Saved Games at that moment
      echo "scenario-start cmd=$(tr '\n' '|' <"$(dirname "$3")/ee-start.cmd" 2>/dev/null) save=$(cat "$game_dir/Data/Saved Games/"*.ees 2>/dev/null)" >>"$FAKE_WORLD/wine.log"
      ;;
  esac
  echo "4242 50000 C:\\GOG Games\\Fake\\Empire Earth.exe" >>"$FAKE_PS_FILE"
  if [ -f "$game_dir/ddraw_trace.cfg" ]; then
    trace="$(sed -n 's/^DDT_TRACE=//p' "$game_dir/ddraw_trace.cfg" | sed 's|^Z:||; s|\\|/|g')"
    arm="$(sed -n 's/^DDT_ARM_FILE=//p' "$game_dir/ddraw_trace.cfg" | sed 's|^Z:||; s|\\|/|g')"
    data="$(sed -n 's/^DDT_DATA=//p' "$game_dir/ddraw_trace.cfg" | sed 's|^Z:||; s|\\|/|g')"
    cp "$game_dir/ddraw_trace.cfg" "$FAKE_WORLD/cfg.seen"
    # the fake proxy: with an arm file it writes the trace only once the file exists
    (
      if [ -n "$arm" ]; then
        i=0
        while [ ! -e "$arm" ] && [ "$i" -lt 200 ]; do sleep 0.1; i=$((i + 1)); done
        [ -e "$arm" ] || exit 0
      fi
      id=1
      if [ -e "$FAKE_WORLD/vary-ids" ]; then   # a run history that shifts the object ids and the hashes, as the game's does
        id=$(( $(cat "$FAKE_WORLD/id-counter" 2>/dev/null || echo 100) + 7 )); echo "$id" >"$FAKE_WORLD/id-counter"
      fi
      printf '# ddraw_trace v1\nT1 fake\nT1 surf#%s.Unlock() = DD_OK locked={bytes=8, hash=fnv1a64:%016x}\n# end: 3 presents\n' "$id" "$id" >"$trace"
      [ -z "$data" ] || printf 'FAKEDATA' >"$data.data"
      : >"$trace.done"
    ) &
  fi
  case "$3" in
    *ee-start.exe)
      # the fake game's simulation: a tick line every 0.2 s until it is paused (the key helper) -- like EE_SIMHASH's lines
      rm -f "$FAKE_WORLD/paused"
      [ -e "$FAKE_WORLD/ticking" ] && (
        printf 'ee-version: simhash: tick 0: 00000000 over 90 objects\n' >>"$game_dir/ee-version.log"
        n=0
        while [ ! -e "$FAKE_WORLD/paused" ] && [ "$n" -lt 100 ] && grep -q 'Empire Earth.exe' "$FAKE_PS_FILE" 2>/dev/null; do
          sleep 0.2; n=$((n + 1))
          [ -e "$FAKE_WORLD/paused" ] || printf 'ee-version: simhash: tick %s: 00000000 over 90 objects\n' "$((n * 50))" >>"$game_dir/ee-version.log"
        done
      ) &
      ;;
  esac
fi
exit 0
WINE
  cat >"$wine/bin/wineserver" <<'WS'
#!/bin/bash
echo "wineserver $* (WINEPREFIX=${WINEPREFIX:-})" >>"$FAKE_WORLD/wine.log"
if [ "${1:-}" = -k ] && [ ! -e "$FAKE_WORLD/refuse-stop" ]; then
  grep -v 'Empire Earth.exe\|EE-AOC.exe' "$FAKE_PS_FILE" >"$FAKE_PS_FILE.new"
  mv "$FAKE_PS_FILE.new" "$FAKE_PS_FILE"
fi
exit 0
WS
  printf '#!/bin/bash\necho "wineboot" >>"$FAKE_WORLD/wine.log"\n' >"$wine/bin/wineboot"
  chmod +x "$wine/bin/wine" "$wine/bin/wineserver" "$wine/bin/wineboot"

  # fake launcher: like the real one it rewrites the game directory, looks for Wine's version.dll relative to the
  # wine it was given, and starts the game through `$EMPIRE_EARTH_WINE start /unix`
  cat >"$W_LAUNCHER/scripts/launch.sh" <<'LAUNCH'
#!/bin/bash
game="$FAKE_GAME"
log="$FAKE_SUPPORT/logs/launcher.log"
echo "launch.sh pid $$ EMPIRE_EARTH_WINE=${EMPIRE_EARTH_WINE:-} WINEDEBUG=${WINEDEBUG:-unset} EE_GRAPHICS=${EE_GRAPHICS:-unset} EE_BIG_ARMIES=${EE_BIG_ARMIES:-unset} EE_SIMHASH=${EE_SIMHASH:-unset} EE_SIM_FIXED_MS=${EE_SIM_FIXED_MS:-unset} SUPPORT=${EMPIRE_EARTH_SUPPORT_DIR:-unset}" >>"$FAKE_WORLD/launch.log"
for v in "$FAKE_WORLD"/reg.*; do [ -f "$v" ] && printf '%s=%s\n' "$(basename "$v")" "$(cat "$v")" >>"$FAKE_WORLD/reg-at-launch.txt"; done
wine_path="${EMPIRE_EARTH_WINE:-$FAKE_WINE}"   # the real launcher finds its own Wine when none is given
orig="$(cd "$(dirname "$wine_path")/.." 2>/dev/null && pwd -P)/lib/wine/i386-windows/version.dll"
printf 'launcher ddraw %s' "$$" >"$game/ddraw.dll"
printf 'run log %s' "$$" >"$game/ee-ddraw.log"
if [ -f "$orig" ]; then
  echo "[t] Installed the version.dll shim in $game" >>"$log"
else
  echo "[t] WARNING: version.dll shim not installed in $game (Wine's copy: $orig)" >>"$log"
fi
if [ -f "$FAKE_WORLD/launch-log-template" ]; then
  sed "s/@DLL@/${FAKE_DLL:-EEWebRasterizer}/g" "$FAKE_WORLD/launch-log-template" >"$FAKE_SUPPORT/logs/launch-$(date +%Y%m%d-%H%M%S)-$$.log"
  for i in 1 2 3 4 5 6 7 8 9 10 11 12; do echo "BeginScene ENTER $i" >>"$game/ee-ddraw.log"; done
fi
[ -e "$FAKE_WORLD/launcher-fail-before-game" ] && exit 3
if [ -e "$FAKE_WORLD/launcher-late-start" ]; then
  # an orphan: it asks for the game only when the test says so (launcher-go), long after the driver may have died
  i=0
  while [ ! -e "$FAKE_WORLD/launcher-go" ] && [ "$i" -lt 300 ]; do sleep 0.1; i=$((i + 1)); done
fi
"$wine_path" start /unix "$game/Empire Earth.exe"
echo "wine start exit status $?" >>"$FAKE_WORLD/launch.log"
[ -e "$FAKE_WORLD/launcher-hang" ] && { sleep 300 & echo $! >"$FAKE_WORLD/launcher-child.pid"; wait; }
exit 0
LAUNCH
  chmod +x "$W_LAUNCHER/scripts/launch.sh"

  # the overrides a driver applies after sourcing the library (its --test-config argument)
  cat >"$W/config.sh" <<CONF
GAME_RUN_LOCK="$W_HOME/Games/EmpireEarth-re/locks/game-run"
GAME_RUN_STATE="$W_HOME/Games/EmpireEarth-re/game-run-state"
GR_SUPPORT="$sup"
GR_PREFIX="$W_PREFIX"
GR_GAME="$W_GAME"
GR_WINE_ROOT="$wine"
GR_LAUNCHER_DIR="$W_LAUNCHER"
GR_LOG_DIR="$sup/logs"
GR_CONFIG_JSON="$sup/config.json"
GR_MIN_RSS_KB=0
GR_STOP_WAIT=2
GR_LOCK_WAIT=2
GR_LOCK_POLL=0.2
OUT="$W/out"
PRESENTS=3
CAPTURE_WAIT=6
LAUNCH_LIMIT=60
export FAKE_GAME="$W_GAME" FAKE_SUPPORT="$sup" FAKE_WINE="$wine/bin/wine" FAKE_STATE="$W_HOME/Games/EmpireEarth-re/game-run-state"
CONF
  : >"$W_SUP/logs/launcher.log"
  export PATH="$W/bin:$PATH"
  export HOME="$W_HOME"
}

# Refuse to go on unless every path the library will use is inside the fake world.
world_assert_isolated() {
  case "$GAME_RUN_LOCK:$GAME_RUN_STATE:$GR_GAME:$GR_LAUNCHER_DIR:$GR_WINE_ROOT" in
    *"$W"/*:*"$W"/*:*"$W"/*:*"$W"/*:*"$W"/*) return 0 ;;
  esac
  echo "refusing to run: a path of the library points outside the fake world ($W)"
  exit 2
}

world_clean() {
  [ -n "${W:-}" ] && [ -d "$W" ] && rm -rf "$W"
}
