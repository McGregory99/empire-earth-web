#!/usr/bin/env bash
# Tests of scripts/normalize.py, the normalization the determinism gate falls back to when two raw traces differ.
#   test-normalize.sh <tools/ddraw_trace dir>
set -u
TOOL="$(cd "${1:?usage: test-normalize.sh <tools/ddraw_trace dir>}" && pwd)"
fails=0
checks=0
check() {
  checks=$((checks + 1))
  if "$@"; then echo "ok   $1 ..."; else echo "FAIL $*"; fails=$((fails + 1)); fi
}

work="$(mktemp -d "${TMPDIR:-/tmp}/ddt-norm.XXXXXX")"
trap 'rm -rf "$work"' EXIT
cat >"$work/in.txt" <<'TRACE'
# ddraw_trace v1
T2 dev7#4.BeginScene() = DD_OK
T3 surf#69.Blt(lpDestRect={0,0,32,32}, lpDDSrcSurface=surf#42, lpSrcRect={512,384,544,416}, dwFlags=0x8000000, lpDDBltFx=NULL) = DD_OK
T2 surf#43.Blt(lpDestRect={512,384,544,416}, lpDDSrcSurface=surf#58, lpSrcRect={0,0,32,32}, dwFlags=0x8008000, lpDDBltFx=NULL) = DD_OK
T2 surf#43.Blt(lpDestRect={0,0,1024,768}, lpDDSrcSurface=surf#9, lpSrcRect=NULL, dwFlags=0x1000000, lpDDBltFx=NULL) = DD_OK
T2 surf#43.Blt(lpDestRect={0,0,640,480}, lpDDSrcSurface=NULL, lpSrcRect=NULL, dwFlags=0x400, lpDDBltFx={dwSize=100, dwDDFX=0x0}) = DD_OK
T2 dev7#4.SetViewport(data={dwX=0, dwY=0, dwWidth=32, dwHeight=32, dvMinZ=f32:00000000, dvMaxZ=f32:3f800000}) = DD_OK
T2 surf#42.Flip(lpDDSurfaceTargetOverride=NULL, dwFlags=0x28) = DD_OK
# end: 60 presents
TRACE
python3 -I "$TOOL/scripts/normalize.py" <"$work/in.txt" >"$work/out.txt"
check test "$(grep -c 'Blt(' "$work/out.txt")" -eq 2
check grep -q 'lpDestRect={0,0,1024,768}' "$work/out.txt"
check grep -q 'lpDDBltFx={dwSize=100' "$work/out.txt"
check test "$(grep -c '32x32\|{0,0,32,32}' "$work/out.txt")" -eq 0
check grep -q 'SetViewport(data={dwX=0, dwY=0, dwWidth=32' "$work/out.txt"
check grep -q '^# end: 60 presents$' "$work/out.txt"
check test "$(wc -l <"$work/out.txt" | tr -d ' ')" -eq 7
echo "$([ "$fails" -eq 0 ] && echo PASS || echo FAIL): $checks checks, $fails failed"
[ "$fails" -eq 0 ]
