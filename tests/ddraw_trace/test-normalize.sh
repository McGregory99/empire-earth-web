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

check_eq() { # check_eq <description> <expected> <actual>
  checks=$((checks + 1))
  if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1 (expected '$2', got '$3')"; fails=$((fails + 1)); fi
}

work="$(mktemp -d "${TMPDIR:-/tmp}/ddt-norm.XXXXXX")"
trap 'rm -rf "$work"' EXIT
cat >"$work/in.txt" <<'TRACE'
# ddraw_trace v1
T1 dd7#1.CreateSurface(lpDDSurfaceDesc={dwSize=124, dwFlags=0x11007, dwHeight=32, dwWidth=32, ddckCKSrcBlt={0x0, 0x0}, ddpfPixelFormat={dwSize=32, dwFlags=0x40}, ddsCaps={dwCaps=0x10004040,*}}, lplpDDSurface->surf#58, pUnkOuter=NULL) = DD_OK
T1 dd7#1.CreateSurface(lpDDSurfaceDesc={dwSize=124, dwFlags=0x1007, dwHeight=32, dwWidth=32, ddpfPixelFormat={dwSize=32, dwFlags=0x40}, ddsCaps={dwCaps=0x10004040,*}}, lplpDDSurface->surf#69, pUnkOuter=NULL) = DD_OK
T1 dd7#1.CreateSurface(lpDDSurfaceDesc={dwSize=124, dwFlags=0x1007, dwHeight=768, dwWidth=1024, ddsCaps={dwCaps=0x2040,*}}, lplpDDSurface->surf#9, pUnkOuter=NULL) = DD_OK
T2 dev7#4.BeginScene() = DD_OK
T3 surf#69.Blt(lpDestRect={0,0,32,32}, lpDDSrcSurface=surf#42, lpSrcRect={512,384,544,416}, dwFlags=0x8000000, lpDDBltFx=NULL) = DD_OK
T2 surf#42.Blt(lpDestRect={512,384,544,416}, lpDDSrcSurface=surf#58, lpSrcRect={0,0,32,32}, dwFlags=0x8008000, lpDDBltFx=NULL) = DD_OK
T2 surf#43.Blt(lpDestRect={512,384,544,416}, lpDDSrcSurface=surf#69, lpSrcRect={0,0,32,32}, dwFlags=0x8000000, lpDDBltFx=NULL) = DD_OK
T2 surf#43.Blt(lpDestRect={0,0,32,32}, lpDDSrcSurface=surf#9, lpSrcRect={0,0,32,32}, dwFlags=0x1000000, lpDDBltFx=NULL) = DD_OK
T2 surf#43.Blt(lpDestRect={0,0,32,32}, lpDDSrcSurface=surf#9, lpSrcRect=NULL, dwFlags=0x8000000, lpDDBltFx=NULL) = DD_OK
T2 surf#69.Blt(lpDestRect={0,0,32,32}, lpDDSrcSurface=surf#42, lpSrcRect={512,384,544,416}, dwFlags=0x1000000, lpDDBltFx=NULL) = DD_OK
T2 surf#43.Blt(lpDestRect={0,0,1024,768}, lpDDSrcSurface=surf#9, lpSrcRect=NULL, dwFlags=0x1000000, lpDDBltFx=NULL) = DD_OK
T2 surf#43.Blt(lpDestRect={0,0,640,480}, lpDDSrcSurface=NULL, lpSrcRect=NULL, dwFlags=0x400, lpDDBltFx={dwSize=100, dwDDFX=0x0}) = DD_OK
T2 dev7#4.SetViewport(data={dwX=0, dwY=0, dwWidth=32, dwHeight=32, dvMinZ=f32:00000000, dvMaxZ=f32:3f800000}) = DD_OK
T2 surf#42.Flip(lpDDSurfaceTargetOverride=NULL, dwFlags=0x28) = DD_OK
# end: 60 presents
TRACE
python3 -I "$TOOL/scripts/normalize.py" <"$work/in.txt" >"$work/out.txt"
# the three software-cursor blits (save under, draw, restore) are gone
check test "$(grep -c 'lpDDSrcSurface=surf#58\|lpSrcRect={512,384,544,416}, dwFlags=0x8000000\|lpDestRect={512,384,544,416}, lpDDSrcSurface=surf#69' "$work/out.txt")" -eq 0
# every other Blt stays, even with 32x32 rectangles: waiting blits, and blits that do not involve a cursor surface
check test "$(grep -c 'Blt(' "$work/out.txt")" -eq 5
check grep -q 'surf#43.Blt(lpDestRect={0,0,32,32}, lpDDSrcSurface=surf#9, lpSrcRect={0,0,32,32}, dwFlags=0x1000000' "$work/out.txt"
check grep -q 'surf#43.Blt(lpDestRect={0,0,32,32}, lpDDSrcSurface=surf#9, lpSrcRect=NULL, dwFlags=0x8000000' "$work/out.txt"
check grep -q 'surf#69.Blt(lpDestRect={0,0,32,32}, lpDDSrcSurface=surf#42, lpSrcRect={512,384,544,416}, dwFlags=0x1000000' "$work/out.txt"
check grep -q 'lpDestRect={0,0,1024,768}' "$work/out.txt"
check grep -q 'lpDDBltFx={dwSize=100' "$work/out.txt"
check grep -q 'SetViewport(data={dwX=0, dwY=0, dwWidth=32' "$work/out.txt"
check grep -q '^# end: 60 presents$' "$work/out.txt"
check test "$(wc -l <"$work/out.txt" | tr -d ' ')" -eq 13

# ---- --structure: the form of a trace that does not depend on run history (ids, content hashes, ring buffer phase) ----------
cat >"$work/in2.txt" <<'TRACE'
# ddraw_trace v1
# armed
T1 dev7#5.SetTexture(stage=0, surface=surf#583) = DD_OK
T1 dev7#5.SetTexture(stage=1, surface=surf#12) = DD_OK
T1 surf#583.Unlock(lpSurfaceData=NULL) = DD_OK locked={rect=NULL, bytes=8192, hash=fnv1a64:d024786954267fbb}
T1 vb#9.Lock(flags=0x1020, data=>P, data_size=>NULL) = DD_OK
T1 vb#9.Unlock() = DD_OK locked={bytes=87912, hash=fnv1a64:20f3452b3687bda9}
T1 dev7#5.DrawPrimitiveVB(primitive_type=TRIANGLELIST(4), vb=vb#9, start_vertex=1512, vertex_count=15, flags=0x0) = DD_OK
T1 dev7#5.DrawIndexedPrimitive(primitive_type=TRIANGLELIST(4), fvf=0x152, vertices=fvf[3x36]:fnv1a64:0123456789abcdef, vertex_count=3, indices=u16[3]:fnv1a64:fedcba9876543210, index_count=3, flags=0x0) = DD_OK
T1 surf#12.Blt(lpDestRect={0,0,8,8}, lpDDSrcSurface=surf#583, lpSrcRect=NULL, dwFlags=0x1000000, lpDDBltFx=NULL) = DD_OK
T1 vb#9.Lock(flags=0x2020, data=>P, data_size=>NULL) = DD_OK
# end: 2 presents
TRACE
python3 -I "$TOOL/scripts/normalize.py" --structure <"$work/in2.txt" >"$work/out2.txt"
check grep -q 'SetTexture(stage=0, surface=surf#T)' "$work/out2.txt"
check grep -q 'SetTexture(stage=1, surface=surf#T)' "$work/out2.txt"
check grep -q '^T1 surf#1.Unlock(lpSurfaceData=NULL) = DD_OK locked={rect=NULL, bytes=8192, hash=fnv1a64:H}$' "$work/out2.txt"
check grep -q '^T1 surf#2.Blt(lpDestRect={0,0,8,8}, lpDDSrcSurface=surf#1,' "$work/out2.txt"
check grep -q '^T1 vb#1.Unlock() = DD_OK locked={bytes=87912, hash=fnv1a64:H}$' "$work/out2.txt"
check grep -q 'vb=vb#1, start_vertex=S, vertex_count=15' "$work/out2.txt"
check grep -q 'vertices=fvf\[3x36\]:fnv1a64:H, vertex_count=3, indices=u16\[3\]:fnv1a64:H, index_count=3' "$work/out2.txt"
check test "$(grep -c '^T1 vb#1.Lock(flags=F, data=>P' "$work/out2.txt")" -eq 2
check grep -q '^dev7\|dev7#5.DrawPrimitiveVB' "$work/out2.txt"
check grep -q '^# end: 2 presents$' "$work/out2.txt"
# ids are numbered by first appearance: a run whose ids are shifted by a constant gives the same text
sed -E 's/surf#583/surf#601/g; s/surf#12\b/surf#30/g; s/vb#9/vb#77/g; s/start_vertex=1512/start_vertex=276/; s/hash=fnv1a64:[0-9a-f]+/hash=fnv1a64:00ff00ff00ff00ff/g; s/flags=0x1020/flags=0x2020/' "$work/in2.txt" >"$work/in3.txt"
python3 -I "$TOOL/scripts/normalize.py" --structure <"$work/in3.txt" | cmp -s - "$work/out2.txt"
check_eq "a trace with other ids, hashes, ring phase and lock flags normalizes to the same text" "0" "$?"
# but the structure itself is kept: another vertex count, a different order of calls, a different render state differ
sed -E 's/vertex_count=15/vertex_count=16/' "$work/in2.txt" | python3 -I "$TOOL/scripts/normalize.py" --structure | cmp -s - "$work/out2.txt"
check_eq "a different vertex count is still a difference" "1" "$?"
# the default form is untouched: no renumbering, hashes kept
python3 -I "$TOOL/scripts/normalize.py" <"$work/in2.txt" | cmp -s - "$work/in2.txt"
check_eq "without --structure only the cursor blits are removed (this input has none)" "0" "$?"
# the cursor rule still applies in structure form
python3 -I "$TOOL/scripts/normalize.py" --structure <"$work/in.txt" >"$work/out1s.txt"
check_eq "--structure also removes the cursor blits" "0" "$(grep -c 'lpSrcRect={512,384,544,416}, dwFlags=0x8000000' "$work/out1s.txt")"
python3 -I "$TOOL/scripts/normalize.py" --nonsense </dev/null >/dev/null 2>&1
check_eq "an unknown option is refused (exit 2)" "2" "$?"

# ---- the scenario golden (structure form): what the manifest says it is, and already in normal form --------------------------
GOLDEN="$TOOL/../../tests/ddraw_trace/golden/skirmish-paused-n60.structure.trace.gz"
gzip -dc "$GOLDEN" >"$work/golden.txt"
check_eq "the skirmish golden has the sha256 that tests/ddraw_trace/scenarios.md records" "852c752f05a38edf1f2095081781b6289d1ab778d47dbde122ead314a19c9163" "$(shasum -a 256 "$work/golden.txt" | cut -c1-64)"
check_eq "... 84,844 lines and 60 presents" "84844 60" "$(wc -l <"$work/golden.txt" | tr -d ' ') $(grep -c '\.Flip(' "$work/golden.txt")"
python3 -I "$TOOL/scripts/normalize.py" --structure <"$work/golden.txt" | cmp -s - "$work/golden.txt"
check_eq "... it is already in structure form (normalizing it again changes nothing)" "0" "$?"
check_eq "... it holds no content hash (only the mask)" "0" "$(grep -E -c 'fnv1a64:[0-9a-f]{16}' "$work/golden.txt")"
echo "$([ "$fails" -eq 0 ] && echo PASS || echo FAIL): $checks checks, $fails failed"
[ "$fails" -eq 0 ]
