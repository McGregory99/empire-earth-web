// Builds the SYNTHETIC trace + data fixture (hand-made pixels and geometry, no game data) in the exact line syntax of
// tools/ddraw_trace and the record layout of its data file. Used by the loader tests and the headless check.
//
//   node web/test/fixtures/build.mjs          rewrite synthetic.trace / synthetic.data next to this file
//
// Scene (320x240): a 4x4 RGB565 texture on two quads (the second one shifted by a world matrix), a colour-keyed sprite
// blit, a colour fill, a partial texture Unlock, a GDI-style ReleaseDC readback, an application-array draw, a failing
// Z-buffer Clear and a Blt-onto-primary present.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const P = 0x100000001b3n, M = 0xffffffffffffffffn;
export function fnv1a64(bytes) {
  let h = 0xcbf29ce484222325n;
  for (const b of bytes) h = ((h ^ BigInt(b)) * P) & M;
  return h;
}

/** Writes records in the documented layout, content addressed like the proxy (same kind/object/params/hash = same record). */
export class DataWriter {
  constructor() {
    this.parts = [Buffer.concat([Buffer.from('DDTDATA1'), Buffer.alloc(8)])];
    this.parts[0].writeUInt32LE(1, 8);
    this.size = 16;
    this.seq = 0;
    this.seen = new Map();
  }

  /** @returns {string} `@offset+length` */
  add(kind, object, params, payload) {
    const hash = fnv1a64(payload);
    const key = `${kind}/${object}/${params.join(',')}/${payload.length}/${hash}`;
    const hit = this.seen.get(key);
    if (hit) return hit;
    const total = 96 + ((payload.length + 7) & ~7);
    const rec = Buffer.alloc(total);
    rec.writeUInt32LE(0x31524444, 0);
    rec.writeUInt32LE(kind, 4);
    rec.writeUInt32LE(total, 8);
    rec.writeUInt32LE(this.seq++, 12);
    rec.writeUInt32LE(object, 16);
    rec.writeUInt32LE(payload.length, 20);
    rec.writeBigUInt64LE(hash, 24);
    params.forEach((p, i) => rec.writeUInt32LE(p >>> 0, 32 + i * 4));
    Buffer.from(payload).copy(rec, 96);
    const ref = `@${this.size}+${total}`;
    this.parts.push(rec);
    this.size += total;
    this.seen.set(key, ref);
    return ref;
  }

  vertices(bytes, count, source = 0, start = 0) {
    return this.add(1, 0, [0x152, 36, start, count, source], bytes);
  }

  indices(arr) {
    return this.add(2, 0, [arr.length], Buffer.from(new Uint16Array(arr).buffer));
  }

  /** origin: 1 Unlock, 2 ReleaseDC, 3/4 Blt source, 5 SetTexture. fmt: {bpp, r, g, b, a}. rect: [l,t,r,b] */
  surface(id, w, h, fmt, rect, payload, origin = 1) {
    const [l, t, r, b] = rect;
    return this.add(3, id, [l, t, r, b, w, h, (r - l) * (fmt.bpp / 8), b - t, origin, fmt.a ? 0x41 : 0x40, 0, fmt.bpp, fmt.r, fmt.g, fmt.b, fmt.a], payload);
  }

  buffer() { return Buffer.concat(this.parts); }
}

export const FMT565 = { bpp: 16, r: 0xf800, g: 0x07e0, b: 0x001f, a: 0 };
export const FMTX8888 = { bpp: 32, r: 0xff0000, g: 0xff00, b: 0xff, a: 0 };

const hex = (b) => b.toString(16).padStart(8, '0');
const f32 = (v) => { const a = new Float32Array([v]); return hex(new Uint32Array(a.buffer)[0]); };
const matrix = (m) => `{f32[16]=${m.map(f32).join(' ')}}`;
const IDENT = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
const color4 = (c) => `{${c.map((v) => `f32:${f32(v)}`).join(', ')}}`;
const pf = (f) => `ddpfPixelFormat={dwSize=32, dwFlags=0x${f.a ? '41' : '40'}, dwRGBBitCount=${f.bpp}, dwRBitMask=0x${f.r.toString(16)}, dwGBitMask=0x${f.g.toString(16)}, dwBBitMask=0x${f.b.toString(16)}, dwRGBAlphaBitMask=0x${f.a.toString(16)}}`;
const caps = (c) => `ddsCaps={dwCaps=0x${c.toString(16)}, dwCaps2=0x0, dwCaps3=0x0, dwCaps4=0x0}`;

function vertexBytes(verts) {
  const b = Buffer.alloc(verts.length * 36);
  verts.forEach(([x, y, z, argb, u, v], i) => {
    const o = i * 36;
    [x, y, z, 0, 0, -1].forEach((f, k) => b.writeFloatLE(f, o + k * 4));
    b.writeUInt32LE(argb >>> 0, o + 24);
    b.writeFloatLE(u, o + 28);
    b.writeFloatLE(v, o + 32);
  });
  return b;
}

const u16le = (arr) => Buffer.from(new Uint16Array(arr).buffer);

export const FIXTURE = {
  // 4x4 RGB565: top rows red | green, bottom rows blue | white
  texture: [0xf800, 0xf800, 0x07e0, 0x07e0, 0xf800, 0xf800, 0x07e0, 0x07e0, 0x001f, 0x001f, 0xffff, 0xffff, 0x001f, 0x001f, 0xffff, 0xffff],
  centre: [0xffff, 0xffff, 0xffff, 0xffff], // the 2x2 rectangle (1,1)-(3,3) written in frame 2
  quad: [[-0.9, 0.9, 0.5, 0xffffffff, 0, 0], [-0.1, 0.9, 0.5, 0xffffffff, 1, 0], [-0.1, 0.1, 0.5, 0xffffffff, 1, 1], [-0.9, 0.1, 0.5, 0xffffffff, 0, 1]],
  triangle: [[0.2, -0.2, 0.5, 0xffff8000, 0, 0], [0.8, -0.2, 0.5, 0xffff8000, 0, 0], [0.5, -0.8, 0.5, 0xffff8000, 0, 0]],
};

export function build() {
  const d = new DataWriter();
  const L = [];
  const add = (s) => L.push(s);
  const key = 0xff00ff;
  // sprite 4x4 XRGB8888 (B,G,R,X): left half magenta (the colour key), right half green
  const sprite = Buffer.alloc(64);
  for (let y = 0; y < 4; y++) for (let x = 0; x < 4; x++) sprite.set(x < 2 ? [0xff, 0x00, 0xff, 0x00] : [0x00, 0xff, 0x00, 0x00], (y * 4 + x) * 4);
  // GDI readback in frame 2: opposite halves
  const sprite2 = Buffer.alloc(64);
  for (let y = 0; y < 4; y++) for (let x = 0; x < 4; x++) sprite2.set(x < 2 ? [0x00, 0xff, 0x00, 0x00] : [0xff, 0x00, 0xff, 0x00], (y * 4 + x) * 4);

  const quadBytes = vertexBytes(FIXTURE.quad);
  const triBytes = vertexBytes(FIXTURE.triangle);
  const idx = [0, 1, 2, 0, 2, 3];
  const vtxQuad = d.vertices(quadBytes, 4);
  const idxQuad = d.indices(idx);
  const vtxTri = d.vertices(triBytes, 3, 1);
  const tex0 = d.surface(3, 4, 4, FMT565, [0, 0, 4, 4], u16le(FIXTURE.texture), 1);
  const spr0 = d.surface(4, 4, 4, FMTX8888, [0, 0, 4, 4], sprite, 1);
  const texC = d.surface(3, 4, 4, FMT565, [1, 1, 3, 3], u16le(FIXTURE.centre), 1);
  const spr1 = d.surface(4, 4, 4, FMTX8888, [0, 0, 4, 4], sprite2, 2);
  const h = (b) => `fnv1a64:${fnv1a64(b).toString(16).padStart(16, '0')}`;
  const fx = (fill) => `{dwSize=100, dwDDFX=0x0, dwROP=0x0, dwDDROP=0x0, dwFillColor=0x${fill.toString(16)}, ddckDestColorkey={0x0, 0x0}, ddckSrcColorkey={0x0, 0x0}}`;

  add('# ddraw_trace v1');
  add('# modules: DX7HRTnLDisplay.dll,DX7HRDisplay.dll');
  add('# data: synthetic.data max=536870912');
  add('T1 DirectDrawCreateEx(lpGuid=NULL, lplpDD->dd7#1, iid=IID_IDirectDraw7, pUnkOuter=NULL) = DD_OK');
  add('T1 dd7#1.SetDisplayMode(dwWidth=320, dwHeight=240, dwBPP=32, dwRefreshRate=0, dwFlags=0x0) = DD_OK');
  add(`T1 dd7#1.CreateSurface(lpDDSurfaceDesc={dwSize=124, dwFlags=0x21, dwBackBufferCount=1, ${caps(0x10006218)}}, lplpDDSurface->surf#1, pUnkOuter=NULL) = DD_OK`);
  add(`T1 surf#1.GetAttachedSurface(lpDDSCaps={dwCaps=0x10, dwCaps2=0x0, dwCaps3=0x0, dwCaps4=0x0}, lplpDDAttachedSurface->surf#2) = DD_OK`);
  add('T1 d3d7#1.CreateDevice(rclsid=IID_IDirect3DHALDevice, surface=surf#2, device->dev7#1) = DD_OK');
  add(`T1 dd7#1.CreateSurface(lpDDSurfaceDesc={dwSize=124, dwFlags=0x1007, dwHeight=4, dwWidth=4, ${pf(FMT565)}, ${caps(0x1000)}}, lplpDDSurface->surf#3, pUnkOuter=NULL) = DD_OK`);
  add(`T1 dd7#1.CreateSurface(lpDDSurfaceDesc={dwSize=124, dwFlags=0x11007, dwHeight=4, dwWidth=4, ddckCKSrcBlt={0x0, 0x0}, ${pf(FMTX8888)}, ${caps(0x10004040)}}, lplpDDSurface->surf#4, pUnkOuter=NULL) = DD_OK`);
  add(`T1 surf#4.SetColorKey(dwFlags=0x8, lpDDColorKey={0x${key.toString(16)}, 0x${key.toString(16)}}) = DD_OK`);
  add('T1 dev7#1.SetViewport(data={dwX=0, dwY=0, dwWidth=320, dwHeight=240, dvMinZ=f32:00000000, dvMaxZ=f32:3f800000}) = DD_OK');
  for (const [n, name] of [[1, 'WORLD'], [2, 'VIEW'], [3, 'PROJECTION']]) add(`T1 dev7#1.SetTransform(state=${name}(${n}), matrix=${matrix(IDENT)}) = DD_OK`);
  add('T1 dev7#1.SetMaterial(data={diffuse=' + color4([1, 1, 1, 1]) + ', ambient=' + color4([1, 1, 1, 1]) + ', specular=' + color4([0, 0, 0, 0]) + ', emissive=' + color4([0, 0, 0, 0]) + ', power=f32:00000000}) = DD_OK');
  for (const [id, name, v] of [[137, 'LIGHTING', 0], [22, 'CULLMODE', 1], [7, 'ZENABLE', 0]]) add(`T1 dev7#1.SetRenderState(dwRenderStateType=${name}(${id}), dwRenderState=0x${v.toString(16)}) = DD_OK`);
  for (const [id, name, v] of [[1, 'COLOROP', 4], [2, 'COLORARG1', 2], [3, 'COLORARG2', 0], [4, 'ALPHAOP', 2], [5, 'ALPHAARG1', 2], [16, 'MAGFILTER', 1], [17, 'MINFILTER', 1], [12, 'ADDRESS', 3]]) {
    add(`T1 dev7#1.SetTextureStageState(dwStage=0, d3dTexStageStateType=${name}(${id}), dwState=0x${v.toString(16)}) = DD_OK`);
  }
  add('T1 dev7#1.SetTextureStageState(dwStage=1, d3dTexStageStateType=COLOROP(1), dwState=0x1) = DD_OK');
  add('T1 dev7#1.SetTextureStageState(dwStage=1, d3dTexStageStateType=ALPHAOP(4), dwState=0x1) = DD_OK');
  const lock = (s, w, rect) => `T1 surf#${s}.Lock(lpDestRect=${rect ? `{${rect.join(',')}}` : 'NULL'}, lpDDSurfaceDesc=>{dwSize=124, dwFlags=0x100f, dwHeight=${w}, dwWidth=${w}, lPitch=${w * 4}, lpSurface=P}, dwFlags=0x0, hEvent=NULL) = DD_OK`;
  const unlock = (s, rect, bytes, ref) => `T1 surf#${s}.Unlock(lpSurfaceData=NULL) = DD_OK locked={rect=${rect ? `{${rect.join(',')}}` : 'NULL'}, bytes=${bytes.length}, hash=${h(bytes)}} blob=${ref}`;
  add(lock(3, 4)); add(unlock(3, null, u16le(FIXTURE.texture), tex0));
  add(lock(4, 4)); add(unlock(4, null, sprite, spr0));

  const drawIdx = (start) => `T1 dev7#1.DrawIndexedPrimitiveVB(primitive_type=TRIANGLELIST(4), vb=vb#1, start_vertex=${start}, vertex_count=4, indices=u16[6]:${h(Buffer.from(new Uint16Array(idx).buffer))}, index_count=6, flags=0x0) = DD_OK blob={vertices=${vtxQuad}, indices=${idxQuad}}`;
  const shifted = IDENT.slice(); shifted[12] = 0.9;
  // ---- frame 1
  add('T1 dev7#1.BeginScene() = DD_OK');
  add('T1 dev7#1.Clear(count=1, rects=[{0,0,320,240}], flags=0x1, color=0xff102030, z=f32:00000000, stencil=0) = DD_OK');
  add('T1 dev7#1.SetTexture(stage=0, surface=surf#3) = DD_OK');
  add('T1 dev7#1.SetTexture(stage=1, surface=NULL) = DD_OK');
  add(drawIdx(0));
  add(`T1 dev7#1.SetTransform(state=WORLD(1), matrix=${matrix(shifted)}) = DD_OK`);
  add(drawIdx(4000)); // same bytes read from another place of the ring: content addressed, one record
  add(`T1 dev7#1.SetTransform(state=WORLD(1), matrix=${matrix(IDENT)}) = DD_OK`);
  add('T1 dev7#1.EndScene() = DD_OK');
  add('T1 surf#2.Blt(lpDestRect={8,130,40,162}, lpDDSrcSurface=surf#4, lpSrcRect={0,0,4,4}, dwFlags=0x8008000, lpDDBltFx=NULL) = DD_OK');
  add(`T1 surf#2.Blt(lpDestRect={100,130,140,162}, lpDDSrcSurface=NULL, lpSrcRect=NULL, dwFlags=0x400, lpDDBltFx=${fx(0xffffff00)}) = DD_OK`);
  add('T1 surf#1.Flip(lpDDSurfaceTargetOverride=NULL, dwFlags=0x20) = DD_OK');
  // ---- frame 2
  add(lock(3, 4, [1, 1, 3, 3])); add(unlock(3, [1, 1, 3, 3], u16le(FIXTURE.centre), texC));
  add('T1 surf#4.GetDC(lphDC=P) = DD_OK');
  add(`T1 surf#4.ReleaseDC(hDC=P) = DD_OK blob=${spr1}`);
  add('T1 dev7#1.BeginScene() = DD_OK');
  add('T1 dev7#1.Clear(count=0, rects=NULL, flags=0x2, color=0x0, z=f32:00000000, stencil=0) = D3DERR_ZBUFFER_NOTPRESENT');
  add('T1 dev7#1.Clear(count=0, rects=NULL, flags=0x1, color=0xff102030, z=f32:00000000, stencil=0) = DD_OK');
  add(drawIdx(0));
  add('T1 dev7#1.SetTexture(stage=0, surface=NULL) = DD_OK');
  add(`T1 dev7#1.DrawPrimitive(primitive_type=TRIANGLELIST(4), fvf=0x152, vertices=P, vertex_count=3, flags=0x0) = DD_OK blob={vertices=${vtxTri}}`);
  add('T1 dev7#1.EndScene() = DD_OK');
  add('T1 surf#3.Blt(lpDestRect={0,0,4,4}, lpDDSrcSurface=surf#2, lpSrcRect={8,8,12,12}, dwFlags=0x8000000, lpDDBltFx=NULL) = DD_OK'); // screen read-back into a texture: ignored
  add('T1 surf#2.Blt(lpDestRect={8,130,40,162}, lpDDSrcSurface=surf#4, lpSrcRect={0,0,4,4}, dwFlags=0x8008000, lpDDBltFx=NULL) = DD_OK');
  add('T1 surf#1.Blt(lpDestRect=NULL, lpDDSrcSurface=surf#2, lpSrcRect=NULL, dwFlags=0x8000000, lpDDBltFx=NULL) = DD_OK');
  add('T1 dev7#1.SetRenderState(dwRenderStateType=ZENABLE(7), dwRenderState=0x1) = DD_OK'); // after the last present: dropped
  add('# end: 2 presents');
  return { trace: L.join('\n') + '\n', data: d.buffer() };
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const dir = path.dirname(fileURLToPath(import.meta.url));
  const { trace, data } = build();
  fs.writeFileSync(path.join(dir, 'synthetic.trace'), trace);
  fs.writeFileSync(path.join(dir, 'synthetic.data'), data);
  console.log(`wrote synthetic.trace (${trace.length} bytes) and synthetic.data (${data.length} bytes)`);
}
