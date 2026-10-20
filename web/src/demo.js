// Synthetic demo sequence, built in code: no game data. 640x480, three frames. It exercises Gouraud colours,
// a point-sampled 565 texture (updated between frames), alpha blending, the depth buffer, linear vertex fog,
// a directional light, ARGB4444 + alpha test, a colour-keyed 2D blit and a colour fill.
//
// Geometry is chosen so that at view-space depth z = 10 one world unit equals one NDC unit, which makes the
// expected pixels easy to compute by hand: pixel x = (ndc + 1) * 320, pixel y = (1 - ndc) * 240.

import { cmd, CLEAR } from './frame-model.js';
import { RS, TSS, TOP, TARG } from './d3d-tables.js';

export const DEMO_WIDTH = 640;
export const DEMO_HEIGHT = 480;
export const DEMO_BACKGROUND = 0xff203050;

const IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
const f32bits = (f) => new Uint32Array(new Float32Array([f]).buffer)[0];

/** D3D left-handed perspective, row-vector layout. xs = ys = scale, so ndc = world * scale / z. */
export function demoProjection(near = 1, far = 100, scale = 10) {
  const q = far / (far - near);
  return [scale, 0, 0, 0, 0, scale, 0, 0, 0, 0, q, 1, 0, 0, -near * q, 0];
}

class VertexList {
  constructor() { this.items = []; }
  /** world position, normal, D3DCOLOR, uv */
  push(x, y, z, nx, ny, nz, argb, u, v) { this.items.push([x, y, z, nx, ny, nz, argb, u, v]); }
  bytes() {
    const out = new Uint8Array(this.items.length * 36);
    const dv = new DataView(out.buffer);
    this.items.forEach((it, i) => {
      const o = i * 36;
      for (let k = 0; k < 6; k++) dv.setFloat32(o + k * 4, it[k], true);
      dv.setUint32(o + 24, it[6] >>> 0, true); // D3DCOLOR: B,G,R,A bytes in memory
      dv.setFloat32(o + 28, it[7], true);
      dv.setFloat32(o + 32, it[8], true);
    });
    return out;
  }
}

/** A quad given in NDC at view depth z: TL, TR, BR, BL (clockwise on screen = D3D front face). */
function quad(vl, z, x0, y0, x1, y1, argb, nz = -1) {
  const k = z / 10;
  const s = vl.items.length;
  vl.push(x0 * k, y0 * k, z, 0, 0, nz, argb, 0, 0);
  vl.push(x1 * k, y0 * k, z, 0, 0, nz, argb, 1, 0);
  vl.push(x1 * k, y1 * k, z, 0, 0, nz, argb, 1, 1);
  vl.push(x0 * k, y1 * k, z, 0, 0, nz, argb, 0, 1);
  return { startVertex: s, vertexCount: 4, indices: Uint16Array.of(0, 1, 2, 0, 2, 3) };
}

const u16 = (...v) => new Uint8Array(new Uint16Array(v).buffer);
// checker texels as RGB565: red, green, blue, white
const R = 0xf800, G = 0x07e0, B = 0x001f, W = 0xffff;
const CHECKER = [[R, G, B, W], [G, B, W, R], [B, W, R, G]];

function flat() {
  return [cmd.bindTexture(0, null),
    cmd.setTextureStageState(0, TSS.COLOROP, TOP.SELECTARG2), cmd.setTextureStageState(0, TSS.COLORARG2, TARG.DIFFUSE),
    cmd.setTextureStageState(0, TSS.ALPHAOP, TOP.SELECTARG2), cmd.setTextureStageState(0, TSS.ALPHAARG2, TARG.DIFFUSE)];
}
function textured(id, filter) {
  return [cmd.bindTexture(0, id),
    cmd.setTextureStageState(0, TSS.COLOROP, TOP.MODULATE), cmd.setTextureStageState(0, TSS.COLORARG1, TARG.TEXTURE),
    cmd.setTextureStageState(0, TSS.COLORARG2, TARG.DIFFUSE),
    cmd.setTextureStageState(0, TSS.ALPHAOP, TOP.SELECTARG1), cmd.setTextureStageState(0, TSS.ALPHAARG1, TARG.TEXTURE),
    cmd.setTextureStageState(0, TSS.MAGFILTER, filter), cmd.setTextureStageState(0, TSS.MINFILTER, filter),
    cmd.setTextureStageState(0, TSS.MIPFILTER, 1), cmd.setTextureStageState(0, TSS.ADDRESS, 3)];
}

/** @returns {{width: number, height: number, frames: {commands: object[]}[]}} */
export function buildDemoSequence() {
  const vl = new VertexList();
  const draws = {};
  // A: Gouraud triangle, vertex colours red / green / blue
  const a = vl.items.length;
  vl.push(-0.8, 0.8, 10, 0, 0, -1, 0xffff0000, 0, 0);
  vl.push(-0.2, 0.8, 10, 0, 0, -1, 0xff00ff00, 0, 0);
  vl.push(-0.5, 0.2, 10, 0, 0, -1, 0xff0000ff, 0, 0);
  draws.gouraud = { startVertex: a, vertexCount: 3, indices: Uint16Array.of(0, 1, 2) };
  draws.checker = quad(vl, 10, 0.0, 0.8, 0.8, 0.2, 0xffffffff);
  draws.blend = quad(vl, 10, -0.8, -0.2, -0.2, -0.8, 0x80ff0000);
  draws.depthNear = quad(vl, 8, 0.1, -0.3, 0.5, -0.7, 0xffffff00);
  draws.depthFar = quad(vl, 12, 0.3, -0.5, 0.7, -0.9, 0xff00ffff);
  draws.fog = quad(vl, 20, 0.82, 0.9, 0.98, 0.1, 0xffff0000);
  draws.lit = quad(vl, 10, -0.15, -0.3, 0.05, -0.8, 0xffffffff);
  draws.alphaTest = quad(vl, 10, -0.1, 0.98, 0.5, 0.85, 0xffffffff);
  const vertexBytes = vl.bytes();

  const surface = new Uint8Array(8 * 8 * 4); // XRGB8888 (B,G,R,X): top-left 4x4 green, the rest magenta = key
  for (let y = 0; y < 8; y++) {
    for (let x = 0; x < 8; x++) {
      const green = x < 4 && y < 4;
      surface.set(green ? [0x00, 0xff, 0x00, 0x00] : [0xff, 0x00, 0xff, 0x00], (y * 8 + x) * 4);
    }
  }

  const frames = [];
  for (let f = 0; f < 3; f++) {
    const c = [];
    if (f === 0) {
      c.push(cmd.setDisplayMode(DEMO_WIDTH, DEMO_HEIGHT));
      c.push(cmd.createTexture(1, 2, 2, 'RGB565', u16(...CHECKER[0])));
      // texel 0 opaque orange (A=F R=F G=8 B=0), texel 1 fully transparent
      c.push(cmd.createTexture(2, 2, 1, 'ARGB4444', u16(0xff80, 0x0f00)));
      c.push(cmd.createTexture(3, 8, 8, 'XRGB8888', surface));
      c.push(cmd.setTransform(2, IDENTITY), cmd.setTransform(3, demoProjection()), cmd.setTransform(1, IDENTITY));
    } else {
      c.push(cmd.updateTexture(1, u16(...CHECKER[f])));
    }
    c.push(cmd.setViewport({ x: 0, y: 0, width: DEMO_WIDTH, height: DEMO_HEIGHT, minZ: 0, maxZ: 1 }));
    c.push(cmd.clear(CLEAR.TARGET | CLEAR.ZBUFFER, DEMO_BACKGROUND, 1));
    c.push(cmd.uploadVertices(vertexBytes));
    const draw = (d) => c.push(cmd.draw({ primitive: 'triangleList', ...d }));
    const rs = (id, v) => c.push(cmd.setRenderState(id, v));

    // fixed-function baseline: no lighting, no fog, no blend, depth LEQUAL, cull CCW
    rs(RS.LIGHTING, 0); rs(RS.FOGENABLE, 0); rs(RS.ALPHABLENDENABLE, 0); rs(RS.ALPHATESTENABLE, 0);
    rs(RS.ZENABLE, 1); rs(RS.ZWRITEENABLE, 1); rs(RS.ZFUNC, 4); rs(RS.CULLMODE, 3);

    c.push(...flat()); draw(draws.gouraud);
    c.push(...textured(1, 1)); draw(draws.checker);

    c.push(...flat());
    rs(RS.ALPHABLENDENABLE, 1); rs(RS.SRCBLEND, 5); rs(RS.DESTBLEND, 6);
    draw(draws.blend);
    rs(RS.ALPHABLENDENABLE, 0);

    draw(draws.depthNear); // nearer quad first: the farther one must not overwrite the overlap
    draw(draws.depthFar);

    rs(RS.FOGENABLE, 1); rs(RS.FOGVERTEXMODE, 3); rs(RS.FOGCOLOR, 0xff0000ff);
    rs(RS.FOGSTART, f32bits(10)); rs(RS.FOGEND, f32bits(30));
    draw(draws.fog);
    rs(RS.FOGENABLE, 0);

    rs(RS.LIGHTING, 1); rs(RS.AMBIENT, 0xff000000); rs(RS.COLORVERTEX, 0);
    c.push(cmd.setMaterial({ diffuse: [0.5, 0.25, 1, 1], ambient: [0, 0, 0, 1], specular: [0, 0, 0, 0], emissive: [0, 0, 0, 0], power: 0 }));
    c.push(cmd.setLight(0, { type: 'directional', diffuse: [1, 1, 1, 1], ambient: [0, 0, 0, 1], direction: [0, 0, 1] }));
    c.push(cmd.lightEnable(0, true));
    draw(draws.lit);
    rs(RS.LIGHTING, 0);

    c.push(...textured(2, 1));
    rs(RS.ALPHATESTENABLE, 1); rs(RS.ALPHAFUNC, 5); rs(RS.ALPHAREF, 0x80);
    draw(draws.alphaTest);
    rs(RS.ALPHATESTENABLE, 0);

    c.push(cmd.blit2D(3, { x: 0, y: 0, w: 8, h: 8 }, { x: 20, y: 20, w: 32, h: 32 }, { low: 0xff00ff, high: 0xff00ff }));
    c.push(cmd.fillRect({ x: 20, y: 60, w: 8, h: 8 }, 0xffffff00));
    c.push(cmd.present());
    frames.push({ commands: c });
  }
  return { width: DEMO_WIDTH, height: DEMO_HEIGHT, frames };
}

/**
 * Hand-computed expectations for the LAST frame (index 2), as [x, y, [r, g, b], tolerance, why].
 * Background is 0x203050 = (32, 48, 80).
 */
export const DEMO_PROBES = [
  [10, 10, [32, 48, 80], 0, 'clear colour'],
  [160, 96, [85, 85, 85], 3, 'Gouraud centroid: equal parts of red, green and blue'],
  [384, 84, [0, 0, 255], 0, 'checker texel 0 after two updates: blue (565 0x001F)'],
  [512, 84, [255, 255, 255], 0, 'checker texel 1: white'],
  [384, 156, [255, 0, 0], 0, 'checker texel 2: red'],
  [512, 156, [0, 255, 0], 0, 'checker texel 3: green'],
  [160, 360, [144, 24, 40], 2, 'SRCALPHA blend of 0x80ff0000 over the background: 255*a+32*(1-a) etc, a=128/255'],
  [448, 384, [255, 255, 0], 0, 'depth test: nearer yellow quad wins the overlap although the cyan one is drawn later'],
  [512, 432, [0, 255, 255], 0, 'far quad outside the overlap is cyan'],
  [384, 336, [255, 255, 0], 0, 'near quad outside the overlap is yellow'],
  [608, 120, [128, 0, 128], 2, 'linear fog at z=20 between 10 and 30: f=0.5 of red toward blue'],
  [304, 372, [128, 64, 255], 2, 'directional light facing the quad: diffuse (0.5, 0.25, 1) * N.-L (=1)'],
  [336, 20, [255, 136, 0], 0, 'ARGB4444 opaque texel: R=15 G=8 B=0 -> (255, 136, 0)'],
  [432, 20, [32, 48, 80], 0, 'ARGB4444 transparent texel removed by the alpha test'],
  [28, 28, [0, 255, 0], 0, 'colour-keyed blit: green block copied'],
  [44, 44, [32, 48, 80], 0, 'colour-keyed blit: magenta key texels skipped'],
  [24, 64, [255, 255, 0], 0, 'fillRect'],
];
