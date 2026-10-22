// Synthetic scenes for the headless pixel check (test/scene-probes.mjs) and the Node sanity test (test/scenes.test.js).
// No game data: every scene is a handful of frame-model commands whose probe pixels are computed by hand from the D3D7
// rules (the formula is in the `why` text). Runs in Node and in the page, so it imports only the pure modules.
//
// A scene: { name, commands(): Command[], probes: [[x, y, [r, g, b], why]], tol?, expectUnsupported?: RegExp }.
// expectUnsupported: replaying the scene must throw an UnsupportedError matching it (a feature that is deliberately not
// replayed), instead of drawing something else.

import { cmd } from '../src/frame-model.js';
import { RS, TSS } from '../src/d3d-tables.js';

export const SIZE = 64;
const ARGB = (a, r, g, b) => (((a << 24) | (r << 16) | (g << 8) | b) >>> 0);
const f32bits = (f) => new Uint32Array(new Float32Array([f]).buffer)[0];
export { ARGB, f32bits };

/** Unlit, untextured, uncull, no depth test: the vertex colour comes out as drawn. Clear colour is black. */
export function plain(extraStates = []) {
  const c = [cmd.setDisplayMode(SIZE, SIZE), cmd.clear(1, ARGB(255, 0, 0, 0), 1)];
  c.push(cmd.setViewport({ x: 0, y: 0, width: SIZE, height: SIZE, minZ: 0, maxZ: 1 }));
  for (const [id, v] of [[RS.LIGHTING, 0], [RS.CULLMODE, 1], [RS.ZENABLE, 0]]) c.push(cmd.setRenderState(id, v));
  for (const [id, v] of extraStates) c.push(cmd.setRenderState(id, v));
  return c;
}

/** One vertex of the 0x152 format. */
function pack152(verts) {
  const b = new ArrayBuffer(verts.length * 36);
  const f = new Float32Array(b), u = new Uint32Array(b);
  verts.forEach((v, i) => {
    const o = i * 9;
    f.set(v.pos, o);
    f.set(v.normal ?? [0, 0, -1], o + 3);
    u[o + 6] = (v.diffuse ?? 0xffffffff) >>> 0;
    f.set(v.uv ?? [0, 0], o + 7);
  });
  return new Uint8Array(b);
}

/** An axis aligned rectangle in clip space (x, y in -1..1, top is +1) at depth z, as two triangles. */
export function rectVerts(x0, y0, x1, y1, z, diffuse = 0xffffffff) {
  return [{ pos: [x0, y1, z], diffuse, uv: [0, 0] }, { pos: [x1, y1, z], diffuse, uv: [1, 0] },
    { pos: [x1, y0, z], diffuse, uv: [1, 1] }, { pos: [x0, y0, z], diffuse, uv: [0, 1] }];
}

/** Upload `verts` at the start of the vertex buffer and draw them with `indices` (default: a quad of 4 vertices). */
export function draw(verts, indices = [0, 1, 2, 0, 2, 3], primitive = 'triangleList') {
  return [cmd.uploadVertices(pack152(verts), 0),
    cmd.draw({ primitive, startVertex: 0, vertexCount: verts.length, indices: Uint16Array.from(indices) })];
}

const lerp = (a, b, t) => a + (b - a) * t;
const mix = (c, fog, f) => c.map((v, i) => Math.round(f * v + (1 - f) * fog[i]));

// Pixel (i, j) of a SIZE x SIZE target samples clip position x = 2 i / SIZE - 1, y = 1 - 2 j / SIZE (D3D pixel centres are on
// integers, the renderer shifts by half a pixel), so a quad spanning the whole target has t = i / SIZE along x.

export const SCENES = [
  {
    name: 'baseline: vertex linear fog halves the colour at mid range',
    commands: () => [
      ...plain([[RS.FOGENABLE, 1], [RS.FOGCOLOR, ARGB(255, 0, 0, 255)], [RS.FOGVERTEXMODE, 3], [RS.FOGSTART, f32bits(0)], [RS.FOGEND, f32bits(1)]]),
      ...draw(rectVerts(-0.5, -0.5, 0.5, 0.5, 0.5)),
    ],
    probes: [[32, 32, mix([255, 255, 255], [0, 0, 255], 0.5), 'linear fog (end - d) / (end - start) = 0.5 at view depth 0.5'],
      [4, 4, [0, 0, 0], 'outside the quad: the clear colour is not fogged']],
  },
];

export { lerp, mix, TSS };
