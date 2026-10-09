#!/bin/bash
# Put the tracing proxy in front of the launcher's ddraw chain.  Run by the Wine wrapper (tools/game_run/wrapper.sh) right
# before the game starts.  Idempotent (the launcher retries a launch that dies early), and it never leaves the game
# directory without a ddraw.dll: every file is copied to a temporary name first and renamed into place.
#
#   swap-in.sh <game dir> <proxy ddraw.dll> <Windows path of the trace file> <presents cap>
set -eu
game="$1" proxy="$2" trace="$3" presents="$4"
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
mv -f ddraw_trace.cfg.tmp ddraw_trace.cfg
