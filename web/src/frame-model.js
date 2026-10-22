// The internal frame model: what the renderer consumes. See web/README.md for the prose description.
//
// A Sequence is { width, height, frames: Frame[] }; a Frame is { commands: Command[] } and normally ends with a
// `present` command. State is cumulative across frames (exactly like a D3D device), so a loader emits one command per
// recorded call it cares about, in order. Commands are plain objects tagged by `op`, built with the `cmd`
// constructors below, which validate their arguments and throw FrameModelError on malformed input.

import { TEX_FORMATS, bytesPerPixel } from './formats.js';

export class FrameModelError extends Error {
  constructor(message) { super(message); this.name = 'FrameModelError'; }
}

/** The one vertex format of the original renderer: D3DFVF_XYZ | NORMAL | DIFFUSE | TEX1 = 0x152. */
export const FVF = 0x152;
/** 3 floats position + 3 floats normal + D3DCOLOR diffuse (BGRA bytes) + 2 floats uv. */
export const VERTEX_STRIDE = 36;

const FVF_POSITION = 0x002, FVF_NORMAL = 0x010, FVF_DIFFUSE = 0x040, FVF_SPECULAR = 0x080, FVF_TEXCOUNT_MASK = 0xf00;
const FVF_SUPPORTED = FVF_POSITION | FVF_NORMAL | FVF_DIFFUSE | FVF_SPECULAR | FVF_TEXCOUNT_MASK;

/**
 * Byte layout of a flexible vertex format (D3DFVF_*): XYZ position, then optional NORMAL, DIFFUSE and SPECULAR colours
 * (D3DCOLOR, BGRA bytes) and up to two 2-float texture coordinate sets, in that fixed order. Offsets are -1 when absent.
 * Pre-transformed (XYZRHW), point size, vertex blending and 1/3/4-component texture coordinates are not supported.
 */
export function vertexLayout(fvf) {
  if (!Number.isInteger(fvf) || !(fvf & FVF_POSITION) || (fvf & ~FVF_SUPPORTED)) {
    fail(`unsupported vertex format 0x${(fvf >>> 0).toString(16)} (supported: XYZ, NORMAL, DIFFUSE, SPECULAR and up to 2 sets of 2-float texture coordinates)`);
  }
  const sets = (fvf & FVF_TEXCOUNT_MASK) >> 8;
  if (sets > 2) fail(`unsupported vertex format 0x${fvf.toString(16)}: ${sets} texture coordinate sets (at most 2)`);
  let o = 12;
  const take = (n) => { const at = o; o += n; return at; };
  const normal = fvf & FVF_NORMAL ? take(12) : -1;
  const diffuse = fvf & FVF_DIFFUSE ? take(4) : -1;
  const specular = fvf & FVF_SPECULAR ? take(4) : -1;
  const uv = [];
  for (let i = 0; i < sets; i++) uv.push(take(8));
  return { stride: o, normal, diffuse, specular, uv, sets };
}

/**
 * Pack vertices of the given format. A vertex is { pos: [x, y, z], normal?, diffuse? (D3DCOLOR, default white),
 * specular? (default 0), uv?, uv1? } (absent attributes of the format are written as 0).
 */
export function packVertices(fvf, list) {
  const lay = vertexLayout(fvf);
  const bytes = new Uint8Array(list.length * lay.stride);
  const dv = new DataView(bytes.buffer);
  list.forEach((v, i) => {
    const base = i * lay.stride;
    v.pos.forEach((x, k) => dv.setFloat32(base + k * 4, x, true));
    if (lay.normal >= 0) (v.normal ?? [0, 0, -1]).forEach((x, k) => dv.setFloat32(base + lay.normal + k * 4, x, true));
    if (lay.diffuse >= 0) dv.setUint32(base + lay.diffuse, (v.diffuse ?? 0xffffffff) >>> 0, true);
    if (lay.specular >= 0) dv.setUint32(base + lay.specular, (v.specular ?? 0) >>> 0, true);
    [v.uv, v.uv1].forEach((t, k) => { if (lay.uv[k] !== undefined) (t ?? [0, 0]).forEach((x, j) => dv.setFloat32(base + lay.uv[k] + j * 4, x, true)); });
  });
  return bytes;
}

/** D3DCLEAR flags. */
export const CLEAR = Object.freeze({ TARGET: 1, ZBUFFER: 2, STENCIL: 4 });
/** D3DTRANSFORMSTATETYPE values the renderer understands. */
export const TRANSFORM = Object.freeze({ WORLD: 1, VIEW: 2, PROJECTION: 3 });
export const LIGHT_TYPES = Object.freeze(['directional', 'point', 'spot']);
export const PRIMITIVES = Object.freeze(['triangleList', 'lineList']);

const fail = (m) => { throw new FrameModelError(m); };
const isUint32 = (v) => Number.isInteger(v) && v >= 0 && v <= 0xffffffff;
const isU8 = (v) => v instanceof Uint8Array;
const checkRect = (r, what) => {
  if (!r || ![r.x, r.y, r.w, r.h].every(Number.isInteger) || r.w < 0 || r.h < 0) fail(`${what} must be {x,y,w,h} integers`);
};
const checkColor4 = (a, what) => {
  if (!a || a.length !== 4) fail(`${what} must have 4 components`);
};

const CHECKS = {
  setDisplayMode(c) {
    if (!(c.width > 0 && c.height > 0)) fail('setDisplayMode: size must be positive');
  },
  setRenderState(c) {
    if (!isUint32(c.id)) fail('setRenderState: id must be a non-negative integer');
    if (!isUint32(c.value)) fail('setRenderState: value must be a uint32 (floats as their IEEE bits)');
  },
  setTextureStageState(c) {
    if (!(c.stage === 0 || c.stage === 1)) fail(`setTextureStageState: stage ${c.stage} (only stages 0 and 1)`);
    if (!isUint32(c.id) || !isUint32(c.value)) fail('setTextureStageState: id and value must be uint32');
  },
  setTransform(c) {
    if (!isUint32(c.type)) fail('setTransform: type must be an integer');
    if (!c.matrix || c.matrix.length !== 16) fail('setTransform: matrix must have 16 elements (row-major, D3D layout)');
  },
  setViewport(c) {
    for (const k of ['x', 'y', 'width', 'height', 'minZ', 'maxZ']) if (typeof c.viewport?.[k] !== 'number') fail(`setViewport: ${k} missing`);
  },
  setMaterial(c) {
    for (const k of ['diffuse', 'ambient', 'specular', 'emissive']) checkColor4(c.material?.[k], `setMaterial.${k}`);
  },
  setLight(c) {
    if (!Number.isInteger(c.index) || c.index < 0) fail('setLight: index');
    if (c.light === null) return;
    const l = c.light;
    if (!LIGHT_TYPES.includes(l?.type)) fail(`setLight: unsupported light type "${l?.type}" (directional, point, spot)`);
    checkColor4(l.diffuse, 'light.diffuse');
    checkColor4(l.ambient, 'light.ambient');
    if (l.specular !== undefined) checkColor4(l.specular, 'light.specular');
    if (l.type !== 'point' && l.direction?.length !== 3) fail('light.direction must have 3 components');
    if (l.type !== 'directional') {
      if (l.position?.length !== 3) fail('light.position must have 3 components');
      if (!(l.range >= 0)) fail('light.range must be a non-negative number');
      if (l.attenuation?.length !== 3 || !l.attenuation.every(Number.isFinite)) fail('light.attenuation must have 3 numbers');
    }
    if (l.type === 'spot' && !['falloff', 'theta', 'phi'].every((k) => Number.isFinite(l[k]))) fail('spot light needs numeric falloff, theta and phi');
  },
  lightEnable(c) {
    if (!Number.isInteger(c.index) || c.index < 0) fail('lightEnable: index');
  },
  bindTexture(c) {
    if (!(c.stage === 0 || c.stage === 1)) fail(`bindTexture: stage ${c.stage} (only stages 0 and 1)`);
    if (c.textureId !== null && !Number.isInteger(c.textureId)) fail('bindTexture: textureId must be an integer or null');
  },
  createTexture(c) {
    if (!Number.isInteger(c.id)) fail('createTexture: id');
    if (!TEX_FORMATS.includes(c.format)) fail(`createTexture: unsupported texture format "${c.format}"`);
    if (!(c.width > 0 && c.height > 0)) fail('createTexture: size must be positive');
    if (!isU8(c.bytes)) fail('createTexture: bytes must be a Uint8Array');
    const pitch = c.pitch ?? c.width * bytesPerPixel(c.format);
    const need = pitch * (c.height - 1) + c.width * bytesPerPixel(c.format);
    if (c.bytes.length < need) fail(`createTexture: ${c.format} ${c.width}x${c.height} needs ${need} bytes, got ${c.bytes.length}`);
  },
  updateTexture(c) {
    if (!Number.isInteger(c.id)) fail('updateTexture: id');
    if (!isU8(c.bytes)) fail('updateTexture: bytes must be a Uint8Array');
    if (c.rect) checkRect(c.rect, 'updateTexture.rect');
  },
  setColorKey(c) {
    if (!Number.isInteger(c.id)) fail('setColorKey: id');
    if (c.key !== null && !(isUint32(c.key?.low) && isUint32(c.key?.high))) fail('setColorKey: key must be null or {low, high}');
  },
  destroyTexture(c) {
    if (!Number.isInteger(c.id)) fail('destroyTexture: id');
  },
  uploadVertices(c) {
    if (!isU8(c.bytes)) fail('uploadVertices: bytes must be a Uint8Array');
    const stride = vertexLayout(c.fvf ?? FVF).stride;
    if (c.bytes.length % stride) fail(`uploadVertices: length ${c.bytes.length} is not a multiple of ${stride}`);
    if (!Number.isInteger(c.offset) || c.offset < 0 || c.offset % stride) fail(`uploadVertices: offset must be a multiple of ${stride}`);
  },
  draw(c) {
    if (!PRIMITIVES.includes(c.primitive)) fail(`draw: unsupported primitive "${c.primitive}"`);
    if (!(c.indices instanceof Uint16Array)) fail('draw: indices must be a Uint16Array');
    if (!Number.isInteger(c.startVertex) || c.startVertex < 0) fail('draw: startVertex');
    if (!Number.isInteger(c.vertexCount) || c.vertexCount < 0) fail('draw: vertexCount');
    const per = c.primitive === 'triangleList' ? 3 : 2;
    if (c.indices.length % per) fail(`draw: ${c.primitive} index count ${c.indices.length} is not a multiple of ${per}`);
    for (const i of c.indices) if (i >= c.vertexCount) fail(`draw: index ${i} >= vertexCount ${c.vertexCount}`);
  },
  clear(c) {
    if (!isUint32(c.flags) || !isUint32(c.color)) fail('clear: flags and color must be uint32');
    if (c.rect) checkRect(c.rect, 'clear.rect');
  },
  fillRect(c) {
    checkRect(c.rect, 'fillRect.rect');
    if (!isUint32(c.color)) fail('fillRect: color must be uint32');
  },
  blit2D(c) {
    if (!Number.isInteger(c.srcId)) fail('blit2D: srcId');
    checkRect(c.srcRect, 'blit2D.srcRect');
    checkRect(c.dstRect, 'blit2D.dstRect');
    if (c.colorKey !== null && !(isUint32(c.colorKey?.low) && isUint32(c.colorKey?.high))) fail('blit2D: colorKey must be null or {low, high}');
  },
  gdiOverlay(c) {
    if (!Number.isInteger(c.srcId)) fail('gdiOverlay: srcId');
    if (!(Number.isInteger(c.threshold) && c.threshold >= 0 && c.threshold <= 255)) fail('gdiOverlay: threshold must be 0..255');
  },
  resetDeviceState() {},
  present() {},
};

export const OPS = Object.freeze(Object.keys(CHECKS));

export function validateCommand(c) {
  if (!c || typeof c !== 'object') fail(`command must be an object, got ${c}`);
  const check = CHECKS[c.op];
  if (!check) fail(`unknown command op "${c.op}"`);
  check(c);
  return c;
}

const make = (c) => validateCommand(c);

/** Command constructors. Each returns a validated plain object. */
export const cmd = {
  /** Resize the render target / canvas (IDirectDraw7::SetDisplayMode). Contents are cleared. */
  setDisplayMode: (width, height) => make({ op: 'setDisplayMode', width, height }),
  /** D3DRENDERSTATETYPE id and DWORD value (floats as IEEE bits). Unsupported states throw at replay time. */
  setRenderState: (id, value) => make({ op: 'setRenderState', id, value }),
  /** Stage 0 or 1; D3DTEXTURESTAGESTATETYPE id and DWORD value. */
  setTextureStageState: (stage, id, value) => make({ op: 'setTextureStageState', stage, id, value }),
  /** type: WORLD 1, VIEW 2, PROJECTION 3. matrix: 16 numbers, D3D row-major row-vector layout. */
  setTransform: (type, matrix) => make({ op: 'setTransform', type, matrix: Float32Array.from(matrix) }),
  /** x, y from the top-left corner, in pixels; depth range minZ..maxZ. */
  setViewport: (viewport) => make({ op: 'setViewport', viewport }),
  /** diffuse/ambient/specular/emissive as [r,g,b,a] floats, power as a float. */
  setMaterial: (material) => make({ op: 'setMaterial', material }),
  /**
   * Light {type: 'directional' | 'point' | 'spot', diffuse, ambient, specular?, direction (directional, spot), position, range, attenuation: [a0, a1, a2]
   * (point, spot), falloff, theta, phi (spot)} in world space; null removes it.
   */
  setLight: (index, light) => make({ op: 'setLight', index, light }),
  lightEnable: (index, enabled) => make({ op: 'lightEnable', index, enabled: !!enabled }),
  /** Bind (or unbind with null) a texture / surface id to stage 0 or 1. */
  bindTexture: (stage, textureId) => make({ op: 'bindTexture', stage, textureId }),
  /** New texture or 2D surface. bytes: rows `pitch` apart. colorKey: DDraw source key on raw pixel values. */
  createTexture: (id, width, height, format, bytes, opts = {}) =>
    make({ op: 'createTexture', id, width, height, format, bytes, pitch: opts.pitch ?? null, colorKey: opts.colorKey ?? null }),
  /** Replace the texture contents; with rect, `bytes` holds just that rectangle, tightly packed. */
  updateTexture: (id, bytes, rect = null) => make({ op: 'updateTexture', id, bytes, rect }),
  setColorKey: (id, key) => make({ op: 'setColorKey', id, key }),
  destroyTexture: (id) => make({ op: 'destroyTexture', id }),
  /**
   * Write vertices of format `fvf` (default 0x152) into the vertex buffer at byte `offset`, a multiple of that format's stride:
   * vertex number offset / stride is what `draw.startVertex` counts, so one run of vertex numbers must not mix formats.
   */
  uploadVertices: (bytes, offset = 0, fvf = FVF) => make({ op: 'uploadVertices', bytes, offset, fvf }),
  /** DrawIndexedPrimitive: indices are relative to startVertex and must be < vertexCount. */
  draw: ({ primitive, startVertex, vertexCount, indices }) => make({ op: 'draw', primitive, startVertex, vertexCount, indices }),
  /** flags: CLEAR bits; color: D3DCOLOR; z: depth; rect: optional {x,y,w,h} (top-left origin). */
  clear: (flags, color, z, rect = null) => make({ op: 'clear', flags, color, z, rect }),
  /** Blt colour fill of the render target. */
  fillRect: (rect, color) => make({ op: 'fillRect', rect, color }),
  /** Blt from a surface to the render target (nearest neighbour). colorKey drops matching source texels. */
  blit2D: (srcId, srcRect, dstRect, colorKey = null) => make({ op: 'blit2D', srcId, srcRect, dstRect, colorKey }),
  /**
   * GDI drew onto the screen surface (GetDC/ReleaseDC) and the capture stored the whole surface as `srcId`. Pixels of that
   * snapshot that differ from the current render by more than `threshold` (max channel difference) are copied over it; the
   * rest keeps the renderer's own output, so the 3D result stays visible and only the GDI layer (text) is taken from data.
   */
  gdiOverlay: (srcId, threshold = 24) => make({ op: 'gdiOverlay', srcId, threshold }),
  /** Back to D3D7 default device state and no bound textures: the trace switched to another Direct3D device. */
  resetDeviceState: () => make({ op: 'resetDeviceState' }),
  /** End of frame. */
  present: () => make({ op: 'present' }),
};

export function validateSequence(seq) {
  if (!(seq?.width > 0 && seq?.height > 0)) fail('sequence size must be positive');
  if (!Array.isArray(seq.frames)) fail('sequence.frames must be an array');
  seq.frames.forEach((f, fi) => {
    if (!Array.isArray(f?.commands)) fail(`frame ${fi}: commands must be an array`);
    f.commands.forEach((c, ci) => {
      try { validateCommand(c); } catch (e) { throw new FrameModelError(`frame ${fi}, command ${ci}: ${e.message}`); }
    });
  });
  return seq;
}
