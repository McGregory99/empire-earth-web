#!/bin/sh
# Summarize a normalized ddraw trace: call counts per interface method, render states, texture formats, presents.
#   summarize.sh <trace.txt>
f="${1:?usage: summarize.sh <trace.txt>}"
echo "== lines: $(grep -vc '^#' "$f") calls, $(grep -c 'cb ' "$f") callback lines"
echo "== calls per interface.method"
sed -n 's/^T[0-9]* \([a-z0-9]*\)#[0-9]*\.\([A-Za-z0-9_]*\)(.*/\1.\2/p' "$f" | sort | uniq -c | sort -rn
echo "== exports"
sed -n 's/^T[0-9]* \(DirectDraw[A-Za-z]*\)(.*/\1/p' "$f" | sort | uniq -c
echo "== distinct render states set"
sed -n 's/.*SetRenderState(dwRenderStateType=\([A-Z0-9_]*([0-9]*)\),.*/\1/p' "$f" | sort | uniq -c | sort -rn
echo "== distinct texture stage state types set"
sed -n 's/.*SetTextureStageState(dwStage=[0-9]*, d3dTexStageStateType=\([A-Z0-9_]*([0-9]*)\),.*/\1/p' "$f" | sort | uniq -c | sort -rn
echo "== texture formats offered by EnumTextureFormats"
sed -n 's/.*cb EnumTextureFormats(lpDDPixFmt=\(.*\), lpContext=P).*/\1/p' "$f" | sort | uniq -c
echo "== surfaces created (distinct descriptions, ids stripped)"
sed -n 's/.*CreateSurface(lpDDSurfaceDesc=\(.*\), lplpDDSurface->.*/\1/p' "$f" | sort | uniq -c | sort -rn | head -40
echo "== presents: $(grep -c -E '\.(Flip|Blt|BltFast)\(' "$f") flip/blit calls; end marker: $(grep '^# end' "$f")"
