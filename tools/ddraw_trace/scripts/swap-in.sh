#!/bin/bash
# Put the tracing proxy in front of the launcher's ddraw chain.  Run by the Wine wrapper (tools/game_run/wrapper.sh) right
# before the game starts.  Idempotent (the launcher retries a launch that dies early), and it never leaves the game
# directory without a ddraw.dll: every file is copied to a temporary name first and renamed into place.
#
#   swap-in.sh <game dir> <proxy ddraw.dll> <Windows path of the trace file> <presents cap> [<Windows path of the arm file>
#              [<Windows path prefix of the data file> [<data file cap in bytes> [<1: store pixels before the arming>]]]]
# With an arm file the proxy traces nothing until the file exists (DDT_ARM_FILE; scenarios that capture a settled state); an
# empty argument means none.  With a data prefix the proxy also writes <prefix>.data (DDT_DATA, DDT_DATA_MAX): the launcher
# scrubs the environment, so the config file is the only way to hand these to the proxy.
set -eu
game="$1" proxy="$2" trace="$3" presents="$4" arm="${5:-}" data="${6:-}" data_max="${7:-}" early="${8:-}"
cd "$game"
if ! cmp -s ddraw.dll "$proxy"; then
  # The 00xJS proxy (written by the launcher a moment ago) keeps working under another name: it loads its target
  # (ddraw_eeorig.dll) relative to its own file, not by its own name.
  cp -p ddraw.dll ddraw_next.dll.tmp
  mv -f ddraw_next.dll.tmp ddraw_next.dll
  cp "$proxy" ddraw.dll.tmp
  mv -f ddraw.dll.tmp ddraw.dll
fi
cat >ddraw_trace.cfg.tmp <<CFG
DDT_NEXT=ddraw_next.dll
DDT_TRACE=$trace
DDT_MAX_PRESENTS=$presents
CFG
if [ -n "$arm" ]; then
  echo "DDT_ARM_FILE=$arm" >>ddraw_trace.cfg.tmp
fi
if [ -n "$data" ]; then
  echo "DDT_DATA=$data" >>ddraw_trace.cfg.tmp
  if [ -n "$data_max" ]; then
    echo "DDT_DATA_MAX=$data_max" >>ddraw_trace.cfg.tmp
  fi
  if [ "$early" = 1 ]; then
    echo "DDT_DATA_EARLY=1" >>ddraw_trace.cfg.tmp
  fi
fi
mv -f ddraw_trace.cfg.tmp ddraw_trace.cfg
