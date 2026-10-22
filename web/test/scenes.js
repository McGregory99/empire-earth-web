// Synthetic scenes for the headless pixel check (test/scene-probes.mjs) and the Node sanity test (test/scenes.test.js).
// No game data: every scene is a handful of frame-model commands whose probe pixels are computed by hand from the D3D7
// rules (the formula is in the `why` text). Runs in Node and in the page, so it imports only the pure modules.
//
// A scene: { name, commands(): Command[], probes: [[x, y, [r, g, b], why]], tol?, expectUnsupported?: RegExp }.
// expectUnsupported: replaying the scene must throw an UnsupportedError matching it (a feature that is deliberately not
// replayed), instead of drawing something else.

import { cmd } from '../src/frame-model.js';
import * as FM from '../src/frame-model.js';
import { RS, TSS } from '../src/d3d-tables.js';
import * as LIGHTING from '../src/lighting.js';

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
export function draw(verts, indices = [0, 1, 2, 0, 2, 3], primitive = 'triangleList', fvf = 0x152) {
  const bytes = fvf === 0x152 ? pack152(verts) : FM.packVertices(fvf, verts);
  return [fvf === 0x152 ? cmd.uploadVertices(bytes, 0) : cmd.uploadVertices(bytes, 0, fvf),
    cmd.draw({ primitive, startVertex: 0, vertexCount: verts.length, indices: Uint16Array.from(indices) })];
}

const lerp = (a, b, t) => a + (b - a) * t;
const mix = (c, fog, f) => c.map((v, i) => Math.round(f * v + (1 - f) * fog[i]));

// Pixel (i, j) of a SIZE x SIZE target samples clip position x = 2 i / SIZE - 1, y = 1 - 2 j / SIZE (D3D pixel centres are on
// integers, the renderer shifts by half a pixel), so a quad spanning the whole target has t = i / SIZE along x.

/** Fog render states. vertexMode / tableMode: D3DFOG_NONE 0, EXP 1, EXP2 2, LINEAR 3. */
export function fogStates({ vertexMode = 0, tableMode = 0, start = 0, end = 1, density = 1, color = ARGB(255, 0, 0, 0), range = 0 }) {
  return [[RS.FOGENABLE, 1], [RS.FOGCOLOR, color], [RS.FOGVERTEXMODE, vertexMode], [RS.FOGTABLEMODE, tableMode],
    [RS.FOGSTART, f32bits(start)], [RS.FOGEND, f32bits(end)], [RS.FOGDENSITY, f32bits(density)], [RS.RANGEFOGENABLE, range]];
}

/** A rectangle whose left edge is at depth zl and right edge at zr. */
const rectZ = (x0, y0, x1, y1, zl, zr) => [{ pos: [x0, y1, zl] }, { pos: [x1, y1, zr] }, { pos: [x1, y0, zr] }, { pos: [x0, y0, zl] }];
/** A triangle whose three corners are all 1.0 from the eye: the range based fog distance is 1, the view depth 0.8. */
const unitTriangle = [{ pos: [0.6, 0, 0.8] }, { pos: [-0.6, 0, 0.8] }, { pos: [0, 0.6, 0.8] }];
const WHITE = [255, 255, 255];
const BLACK = [0, 0, 0];
const BLUE = [0, 0, 255];
const expo = (d, density) => Math.exp(-d * density);
const expo2 = (d, density) => Math.exp(-((d * density) ** 2));

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
  {
    name: 'vertex exp fog: 1 / e^(d * density)',
    commands: () => [...plain(fogStates({ vertexMode: 1, density: 2, color: ARGB(255, 0, 0, 255) })), ...draw(rectVerts(-0.5, -0.5, 0.5, 0.5, 0.5))],
    probes: [[32, 32, mix(WHITE, BLUE, expo(0.5, 2)), 'exp fog at view depth 0.5, density 2: f = e^-1 = 0.368']],
  },
  {
    name: 'vertex exp2 fog: 1 / e^((d * density)^2)',
    commands: () => [...plain(fogStates({ vertexMode: 2, density: 1.5 })), ...draw(rectVerts(-0.5, -0.5, 0.5, 0.5, 0.8))],
    probes: [[32, 32, mix(WHITE, BLACK, expo2(0.8, 1.5)), 'exp2 fog at view depth 0.8, density 1.5: f = e^-1.44 = 0.237']],
  },
  {
    name: 'vertex fog uses the view depth, range fog the distance from the eye',
    commands: () => [...plain(fogStates({ vertexMode: 3, start: 0, end: 2 })), ...draw(unitTriangle, [0, 1, 2])],
    probes: [[32, 26, mix(WHITE, BLACK, (2 - 0.8) / 2), 'linear 0..2 at view depth 0.8: f = 0.6']],
  },
  {
    name: 'range fog (RANGEFOGENABLE with vertex fog) measures the distance from the eye',
    commands: () => [...plain(fogStates({ vertexMode: 3, start: 0, end: 2, range: 1 })), ...draw(unitTriangle, [0, 1, 2])],
    probes: [[32, 26, mix(WHITE, BLACK, (2 - 1) / 2), 'linear 0..2 at distance sqrt(0.6^2 + 0.8^2) = 1: f = 0.5, not 0.6 as with the view depth']],
  },
  {
    name: 'vertex fog interpolates the factor between the vertices',
    commands: () => [...plain(fogStates({ vertexMode: 1, density: 2 })), ...draw(rectZ(-1, -1, 1, 1, 0.1, 0.9))],
    probes: [[32, 32, mix(WHITE, BLACK, (expo(0.1, 2) + expo(0.9, 2)) / 2), 'half way between f(0.1) = 0.819 and f(0.9) = 0.165 is 0.492']],
  },
  {
    name: 'table fog (FOGTABLEMODE) evaluates the formula per pixel on the depth',
    commands: () => [...plain(fogStates({ tableMode: 1, density: 2 })), ...draw(rectZ(-1, -1, 1, 1, 0.1, 0.9))],
    probes: [[32, 32, mix(WHITE, BLACK, expo(0.5, 2)), 'pixel 32 of 64 has depth 0.1 + 0.8 * 0.5 = 0.5: f = e^-1 = 0.368 (vertex fog gives 0.492)']],
  },
  {
    name: 'table fog wins over vertex fog and uses FOGSTART / FOGEND in depth units',
    commands: () => [...plain(fogStates({ vertexMode: 1, density: 100, tableMode: 3, start: 0.2, end: 0.8 })), ...draw(rectVerts(-0.5, -0.5, 0.5, 0.5, 0.5))],
    probes: [[32, 32, mix(WHITE, BLACK, (0.8 - 0.5) / 0.6), 'table linear 0.2..0.8 at depth 0.5: f = 0.5; the vertex exp fog with density 100 would give black']],
  },
];

// ---- multitexture (stage 1) ------------------------------------------------------------------------------------------
const TOP_ = { SELECTARG1: 2, SELECTARG2: 3, MODULATE: 4, MODULATE2X: 5, ADD: 7 };
const xrgb = (...texels) => Uint8Array.from(texels.flatMap(([r, g, b]) => [b, g, r, 0]));
const texture = (id, w, h, ...texels) => cmd.createTexture(id, w, h, 'XRGB8888', xrgb(...texels));
const ts = (stage, id, v) => cmd.setTextureStageState(stage, id, v);
/** Point sampling, clamp, no mips: texel values come out exactly. */
const pointSampling = (stage) => [[16, 1], [17, 1], [18, 1], [12, 3]].map(([id, v]) => ts(stage, id, v));
const rect2 = (x0, y0, x1, y1, uv0, uv1, diffuse = 0xffffffff) => [
  { pos: [x0, y1, 0.5], diffuse, uv: uv0, uv1 }, { pos: [x1, y1, 0.5], diffuse, uv: uv0, uv1 },
  { pos: [x1, y0, 0.5], diffuse, uv: uv0, uv1 }, { pos: [x0, y0, 0.5], diffuse, uv: uv0, uv1 }];
/** Stage 0: texture 1 as is. Stage 1: texture 2 combined with the stage 0 result by `op`. */
const twoStages = () => [
  ts(0, 1, TOP_.SELECTARG1), ts(0, 2, 2), ts(0, 4, TOP_.SELECTARG1), ts(0, 5, 2), ...pointSampling(0),
  ts(1, 2, 2), ts(1, 3, 1), ts(1, 4, 1), ...pointSampling(1), cmd.bindTexture(0, 1), cmd.bindTexture(1, 2)];
const A = [100, 200, 50];
const B = [150, 100, 255];
const clamp8 = (v) => Math.min(255, Math.round(v));
const per = (f) => A.map((a, i) => clamp8(f(a / 255, B[i] / 255) * 255));

SCENES.push(
  {
    name: 'stage 1 colour operations combine the stage 0 result (CURRENT) with the stage 1 texture',
    commands: () => {
      const q = (cx, cy) => draw(rect2(cx - 0.4, cy - 0.4, cx + 0.4, cy + 0.4, [0.5, 0.5], [0.5, 0.5]), undefined, undefined, 0x252);
      return [...plain(), texture(1, 1, 1, A), texture(2, 1, 1, B), ...twoStages(),
        ts(1, 1, TOP_.MODULATE), ...q(-0.5, 0.5), ts(1, 1, TOP_.MODULATE2X), ...q(0.5, 0.5),
        ts(1, 1, TOP_.ADD), ...q(-0.5, -0.5), ts(1, 1, TOP_.SELECTARG1), ...q(0.5, -0.5)];
    },
    probes: [[16, 16, per((a, b) => a * b), 'MODULATE: A * B per channel'], [48, 16, per((a, b) => 2 * a * b), 'MODULATE2X: 2 * A * B, clamped'],
      [16, 48, per((a, b) => a + b), 'ADD: A + B, clamped to 255'], [48, 48, B, 'SELECTARG1 (the stage 1 texture): B']],
  },
  {
    name: 'stage 1 SELECTARG2 passes the stage 0 result through',
    commands: () => [...plain(), texture(1, 1, 1, A), texture(2, 1, 1, B), ...twoStages(), ts(1, 1, TOP_.SELECTARG2),
      ...draw(rect2(-0.5, -0.5, 0.5, 0.5, [0.5, 0.5], [0.5, 0.5]), undefined, undefined, 0x252)],
    probes: [[32, 32, A, 'SELECTARG2 = COLORARG2 = CURRENT = the stage 0 texture A']],
  },
  {
    name: 'stage 1 reads the second texture coordinate set (TEXCOORDINDEX 1) and stage 0 can read it too',
    commands: () => [...plain(), texture(1, 2, 1, [255, 255, 255], [255, 255, 255]), texture(2, 2, 1, [255, 0, 0], [0, 0, 255]), ...twoStages(),
      ts(1, 1, TOP_.MODULATE), ts(1, 11, 1),
      ...draw(rect2(-0.9, -0.4, -0.1, 0.4, [0.25, 0.5], [0.75, 0.5]), undefined, undefined, 0x252),
      ts(1, 11, 0),
      ...draw(rect2(0.1, -0.4, 0.9, 0.4, [0.25, 0.5], [0.75, 0.5]), undefined, undefined, 0x252)],
    probes: [[16, 32, [0, 0, 255], 'set 1 is (0.75, 0.5): the right texel of the 2x1 texture, blue; white * blue'],
      [48, 32, [255, 0, 0], 'TEXCOORDINDEX 0 reads set 0 (0.25, 0.5): the left texel, red']],
  },
  {
    name: 'a disabled stage 0 ends the cascade: stage 1 is not processed',
    commands: () => [...plain(), texture(1, 1, 1, A), texture(2, 1, 1, B), ...twoStages(), ts(0, 1, 1), ts(1, 1, TOP_.MODULATE),
      ...draw(rect2(-0.5, -0.5, 0.5, 0.5, [0.5, 0.5], [0.5, 0.5], ARGB(255, 200, 100, 50)), undefined, undefined, 0x252)],
    probes: [[32, 32, [200, 100, 50], 'COLOROP DISABLE at stage 0: the vertex colour is the output, whatever stage 1 says']],
  },
);

// ---- specular --------------------------------------------------------------------------------------------------------
const at = (cx, cy) => [Math.round(((cx + 1) / 2) * SIZE), Math.round(((1 - cy) / 2) * SIZE)];
const COS = Math.cos(Math.PI / 6); // N.H for a light 60 degrees off the normal seen by a viewer on the normal: cos(theta / 2)
const MAT_SPEC = [1, 0.5, 0.25];
const quad = (cx, cy, h, extra = {}) => rectVerts(cx - h, cy - h, cx + h, cy + h, 0.5).map((v) => ({ ...v, ...extra }));
const material = (power, specular = [...MAT_SPEC, 1]) => cmd.setMaterial({ diffuse: [0, 0, 0, 1], ambient: [0, 0, 0, 1], specular, emissive: [0, 0, 0, 0], power });
/** A light at 60 degrees from the surface normal (0, 0, -1): the vector to the light is (sin 60, 0, -cos 60). Only its specular colour is non-black. */
const specLight = () => [cmd.setLight(0, { type: 'directional', diffuse: [0, 0, 0, 1], ambient: [0, 0, 0, 1], specular: [1, 1, 1, 1], direction: [-Math.sin(Math.PI / 3), 0, 0.5] }), cmd.lightEnable(0, true)];
const specStates = (extra = []) => plain([[RS.LIGHTING, 1], [RS.COLORVERTEX, 0], [RS.SPECULARENABLE, 1], [RS.LOCALVIEWER, 0], [RS.AMBIENT, 0], ...extra]);
const scaled = (rgb, k) => rgb.map((v) => Math.round(v * k * 255));

SCENES.push(
  {
    name: 'specular highlight is (N.H)^power x light specular x material specular',
    commands: () => [...specStates(), ...specLight(),
      material(4), ...draw(quad(-0.5, 0, 0.3)), material(2), ...draw(quad(0.5, 0, 0.3))],
    probes: [[...at(-0.5, 0), scaled(MAT_SPEC, COS ** 4), 'N.H = cos 30 = 0.866, power 4: 0.5625 of the material specular (1, 0.5, 0.25)'],
      [...at(0.5, 0), scaled(MAT_SPEC, COS ** 2), 'power 2: 0.75 of the material specular']],
  },
  {
    name: 'SPECULARENABLE 0 adds no specular colour',
    commands: () => [...specStates([[RS.SPECULARENABLE, 0]]), ...specLight(), material(4), ...draw(quad(0, 0, 0.3))],
    probes: [[...at(0, 0), [0, 0, 0], 'the lit colour is black (no diffuse, no ambient) and the specular is not added']],
  },
  {
    name: 'LOCALVIEWER: the view vector points from the vertex to the eye',
    commands: () => [...specStates([[RS.LOCALVIEWER, 1]]), ...specLight(), material(4), ...draw(rectVerts(0.7, -0.02, 0.9, 0.02, 0.5))],
    probes: [[...at(0.8, 0), scaled(MAT_SPEC, 0.997), 'at x = 0.8, depth 0.5 the eye vector is (-0.85, 0, -0.53), almost the half angle of the light: N.H = 0.9998 (an infinite viewer would give 0.5625)']],
    tol: 3,
  },
  {
    name: 'the vertex specular colour is added after texturing when SPECULARENABLE is on (unlit)',
    commands: () => [...plain([[RS.SPECULARENABLE, 1]]),
      ...draw(quad(-0.5, 0, 0.3, { diffuse: ARGB(255, 50, 50, 50), specular: ARGB(0, 100, 150, 200) }), undefined, undefined, 0x1d2),
      cmd.setRenderState(RS.SPECULARENABLE, 0),
      ...draw(quad(0.5, 0, 0.3, { diffuse: ARGB(255, 50, 50, 50), specular: ARGB(0, 100, 150, 200) }), undefined, undefined, 0x1d2)],
    probes: [[...at(-0.5, 0), [150, 200, 250], 'diffuse 50 + specular (100, 150, 200)'], [...at(0.5, 0), [50, 50, 50], 'SPECULARENABLE 0: the vertex specular colour is ignored']],
  },
  {
    name: 'SPECULARMATERIALSOURCE COLOR2 lights with the vertex specular colour instead of the material',
    commands: () => [...specStates([[RS.COLORVERTEX, 1], [RS.SPECULARMATERIALSOURCE, 2]]), ...specLight(), material(4, [1, 1, 1, 1]),
      ...draw(quad(0, 0, 0.3, { specular: ARGB(0, 200, 100, 0) }), undefined, undefined, 0x1d2)],
    probes: [[...at(0, 0), scaled([200 / 255, 100 / 255, 0], COS ** 4), 'material source COLOR2: (200, 100, 0) * 0.5625, not the white material specular']],
  },
);

// ---- point and spot lights -------------------------------------------------------------------------------------------
const white = (l) => ({ diffuse: [1, 1, 1, 1], specular: [0, 0, 0, 0], ambient: [0, 0, 0, 0], ...l });
const litStates = () => plain([[RS.LIGHTING, 1], [RS.COLORVERTEX, 0], [RS.AMBIENT, 0]]);
const whiteMaterial = () => cmd.setMaterial({ diffuse: [1, 1, 1, 1], ambient: [0, 0, 0, 1], specular: [0, 0, 0, 0], emissive: [0, 0, 0, 0], power: 0 });
const lightOn = (i, l) => [cmd.setLight(i, l), cmd.lightEnable(i, true)];
const gray = (k) => { const v = Math.round(Math.min(1, Math.max(0, k)) * 255); return [v, v, v]; };
/** A tiny square around (cx, 0) at depth 0.5, normal (0, 0, -1). */
const speck = (cx, h = 0.02) => rectVerts(cx - h, -h, cx + h, h, 0.5);
/** The lit value at the pixel centre of a speck: the centre lies on the diagonal v0-v2, so it is the mean of those two vertices. */
const expectedLit = (light, cx, h = 0.02) => {
  const v = speck(cx, h);
  return (lightFactor(light, v[0].pos, [0, 0, -1]).diffuse + lightFactor(light, v[2].pos, [0, 0, -1]).diffuse) / 2;
};
const lightFactor = (...a) => LIGHTING.lightFactor(...a);
const clipX = (px) => (2 * px) / SIZE - 1;

const pointLight = (z, extra) => white({ type: 'point', position: [0, 0, z], range: 100, attenuation: [1, 0, 0], ...extra });
const spotLight = white({ type: 'spot', position: [0, 0, -0.1], direction: [0, 0, 1], range: 100, attenuation: [1, 0, 0], falloff: 1, theta: Math.PI * 40 / 180, phi: Math.PI * 80 / 180 });

SCENES.push(
  {
    name: 'point light: attenuation 1 / (a0 + a1 d + a2 d^2) and the range cut-off',
    commands: () => {
      const a = pointLight(-1.5, { attenuation: [1, 0.5, 0.25] }); // distance 2 straight ahead of the quads below
      const quads = (cx) => draw(speck(cx));
      return [...litStates(), whiteMaterial(),
        ...lightOn(0, { ...a, position: [clipX(16), 0, -1.5] }), ...quads(clipX(16)),
        ...lightOn(0, { ...a, position: [clipX(32), 0, -1.5], range: 1.5 }), ...quads(clipX(32)),
        ...lightOn(0, { ...a, position: [clipX(48), 0, -1.5], attenuation: [1, 0, 0] }), ...quads(clipX(48))];
    },
    probes: [[16, 32, gray(1 / 3), 'distance 2, attenuation (1, 0.5, 0.25): 1 / (1 + 1 + 1) = 0.333, N.L = 1'],
      [32, 32, [0, 0, 0], 'the vertex is 2 away, the range is 1.5: no light'], [48, 32, gray(1), 'attenuation (1, 0, 0): full diffuse']],
  },
  {
    name: 'spot light: full inside theta, none outside phi, the falloff curve between, times N.L',
    commands: () => [...litStates(), whiteMaterial(), ...lightOn(0, spotLight),
      ...draw(speck(clipX(32))), ...draw(speck(clipX(43))), ...draw(speck(clipX(52)))],
    probes: [[32, 32, gray(expectedLit(spotLight, clipX(32))), 'on the axis: spot 1, N.L 1'],
      [43, 32, gray(expectedLit(spotLight, clipX(43))), 'about 30 degrees off axis (between the 20 and 40 degree half angles): spot (rho - cos 40) / (cos 20 - cos 40), times N.L = cos 30'],
      [52, 32, [0, 0, 0], 'more than 40 degrees off axis: outside the cone']],
  },
);

// ---- block-compressed textures ---------------------------------------------------------------------------------------
const dxtRow = (...idx) => idx.reduce((m, v, i) => m | (v << (2 * i)), 0);
const dxtColors = (c0, c1, rows) => [c0 & 255, c0 >> 8, c1 & 255, c1 >> 8, ...rows];
const dxtAlpha5 = (a0, a1, indices) => {
  let bits = 0n;
  indices.forEach((v, i) => { bits |= BigInt(v) << BigInt(3 * i); });
  return [a0, a1, ...Array.from({ length: 6 }, (_, i) => Number((bits >> BigInt(8 * i)) & 255n))];
};
/** Clip coordinates of a rectangle given in D3D screen coordinates (pixel centres on integers), with texture coordinates 0..1. */
const screenRect = (sx0, sy0, sx1, sy1) => {
  const cx = (x) => (2 * x) / SIZE - 1, cy = (y) => 1 - (2 * y) / SIZE;
  return rectVerts(cx(sx0), cy(sy1), cx(sx1), cy(sy0), 0.5);
};
const blendOn = [[RS.ALPHABLENDENABLE, 1], [RS.SRCBLEND, 5], [RS.DESTBLEND, 6]];
const oneTexture = (id) => [ts(0, 1, 2), ts(0, 2, 2), ts(0, 4, 2), ts(0, 5, 2), ...pointSampling(0), cmd.bindTexture(0, id)];

SCENES.push(
  {
    name: 'DXT1 texture: four colour block decoded on the CPU, one texel per pixel',
    commands: () => [...plain(), cmd.createTexture(1, 4, 4, 'DXT1', Uint8Array.from(dxtColors(0xf800, 0x001f, [dxtRow(0, 1, 2, 3), 0, 0, 0]))),
      ...oneTexture(1), ...draw(screenRect(7.5, 7.5, 11.5, 11.5))],
    probes: [[8, 8, [255, 0, 0], 'index 0 is c0 (red)'], [9, 8, [0, 0, 255], 'index 1 is c1 (blue)'],
      [10, 8, [170, 0, 85], 'index 2 is (2 c0 + c1) / 3'], [11, 8, [85, 0, 170], 'index 3 is (c0 + 2 c1) / 3'], [8, 11, [255, 0, 0], 'the other rows use index 0']],
  },
  {
    name: 'DXT5 texture: interpolated alpha through alpha blending',
    commands: () => [...plain(blendOn), cmd.createTexture(1, 4, 4, 'DXT5', Uint8Array.from([...dxtAlpha5(255, 0, [0, 1, 2, 3, 4, 5, 6, 7]), ...dxtColors(0xffff, 0xffff, [0, 0, 0, 0])])),
      ...oneTexture(1), ...draw(screenRect(7.5, 7.5, 11.5, 11.5))],
    probes: [[8, 8, gray(1), 'alpha index 0 is a0 = 255: white over black stays white'], [9, 8, [0, 0, 0], 'alpha index 1 is a1 = 0'],
      [10, 8, [218, 218, 218], 'alpha index 2 is (6 a0 + a1) / 7 = 218'], [8, 9, [145, 145, 145], 'second row, index 4: (3 a0 + 4 a1) / 7 = 145'],
      [11, 9, [36, 36, 36], 'index 7: (a0 + 6 a1) / 7 = 36']],
  },
);

// ---- wireframe, point fill, flat shading -----------------------------------------------------------------------------
/** A vertex at D3D screen coordinates (pixel centres on integers) at depth 0.5. */
const sv = (sx, sy, extra = {}) => ({ pos: [(2 * sx) / SIZE - 1, 1 - (2 * sy) / SIZE, 0.5], ...extra });
const FRONT = [sv(8, 8), sv(40, 8), sv(8, 40)]; // right, then down-left: clockwise on screen = a front face in D3D
const BACK = [sv(48, 56), sv(48, 24), sv(16, 56)]; // counter-clockwise
const LIT = [255, 255, 255];
const DARK = [0, 0, 0];

SCENES.push(
  {
    name: 'FILLMODE WIREFRAME draws the edges only and culls back faces like a solid draw',
    commands: () => [...plain([[RS.FILLMODE, 2], [RS.CULLMODE, 3]]), ...draw([...FRONT, ...BACK], [0, 1, 2, 3, 4, 5])],
    probes: [[24, 8, LIT, 'top edge of the front triangle'], [8, 24, LIT, 'left edge'], [24, 24, LIT, 'hypotenuse x + y = 48 passes through (24, 24)'],
      [12, 12, DARK, 'the inside of the triangle is not filled'], [30, 30, DARK, 'outside'], [48, 40, DARK, 'the counter-clockwise triangle is culled (CULL_CCW): its edge x = 48 is not drawn']],
  },
  {
    name: 'FILLMODE WIREFRAME without culling draws the back face edges too',
    commands: () => [...plain([[RS.FILLMODE, 2], [RS.CULLMODE, 1]]), ...draw([...FRONT, ...BACK], [0, 1, 2, 3, 4, 5])],
    probes: [[24, 8, LIT, 'front triangle edge'], [48, 40, LIT, 'right edge x = 48 of the back triangle'], [40, 50, DARK, 'inside the back triangle (x + y = 90 is below its hypotenuse x + y = 72)']],
  },
  {
    name: 'FILLMODE POINT draws the vertices only',
    commands: () => [...plain([[RS.FILLMODE, 1]]), ...draw(FRONT, [0, 1, 2])],
    probes: [[8, 8, LIT, 'first vertex'], [40, 8, LIT, 'second vertex'], [8, 40, LIT, 'third vertex'], [24, 8, DARK, 'the edges are not drawn'], [12, 12, DARK, 'nor the inside']],
  },
  {
    name: 'SHADEMODE FLAT colours a triangle with its first vertex',
    commands: () => {
      const col = [0xffff0000, 0xff00ff00, 0xff0000ff];
      const tri = (dx) => [sv(8 + dx, 8, { diffuse: col[0] }), sv(28 + dx, 8, { diffuse: col[1] }), sv(8 + dx, 28, { diffuse: col[2] })];
      return [...plain([[RS.SHADEMODE, 1]]), ...draw(tri(0), [0, 1, 2]), ...draw(tri(32), [1, 2, 0])];
    },
    probes: [[12, 12, [255, 0, 0], 'indices 0, 1, 2: the first vertex is red, the whole triangle is red'],
      [44, 12, [0, 255, 0], 'indices 1, 2, 0: the first vertex is green']],
  },
  {
    name: 'SHADEMODE GOURAUD interpolates (the default)',
    commands: () => [...plain(), ...draw([sv(8, 8, { diffuse: 0xffff0000 }), sv(28, 8, { diffuse: 0xff00ff00 }), sv(8, 28, { diffuse: 0xff0000ff })], [0, 1, 2])],
    probes: [[10, 10, [204, 26, 26], 'barycentric weights red 0.8, green 0.1, blue 0.1 at (10, 10) of the corners (8, 8), (28, 8), (8, 28)'],
      [18, 10, [102, 128, 26], 'weights red 0.4, green 0.5, blue 0.1 at (18, 10)']],
    tol: 4,
  },
);

export { lerp, mix, TSS };
