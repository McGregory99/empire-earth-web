#!/bin/bash
# Put the tracing proxy in front of the launcher's ddraw chain.  Called by the Wine wrapper that game-trace.sh
# generates, right before the game starts.  Idempotent (the launcher retries a launch that dies early).
#
#   swap-in.sh <game dir> <proxy ddraw.dll> <Windows path of the trace file> <presents cap>
set -eu
game="$1" proxy="$2" trace="$3" presents="$4"
cd "$game"
if ! cmp -s ddraw.dll "$proxy"; then
  # The 00xJS proxy (written by the launcher a moment ago) keeps working under another name: it loads its
  # target (ddraw_eeorig.dll) relative to its own file, not by its own name.
  mv -f ddraw.dll ddraw_next.dll
  cp "$proxy" ddraw.dll
fi
cat >ddraw_trace.cfg <<CFG
DDT_NEXT=ddraw_next.dll
DDT_TRACE=$trace
DDT_MAX_PRESENTS=$presents
CFG
