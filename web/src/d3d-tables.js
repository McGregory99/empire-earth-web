// Direct3D 7 -> WebGL2 translation tables. Pure functions, unit-tested in Node.
//
// Policy: everything the golden trace uses is supported. Anything else either is a documented, harmless no-op
// ("ignored") or throws UnsupportedError, so a replay never silently draws something different from the original.

export class UnsupportedError extends Error {
  constructor(message) { super(message); this.name = 'UnsupportedError'; }
}

// WebGL constants (literal values so these modules run in Node).
export const GL = Object.freeze({
  POINTS: 0, LINES: 1, TRIANGLES: 4,
  ZERO: 0, ONE: 1, SRC_COLOR: 0x300, ONE_MINUS_SRC_COLOR: 0x301, SRC_ALPHA: 0x302, ONE_MINUS_SRC_ALPHA: 0x303,
  DST_ALPHA: 0x304, ONE_MINUS_DST_ALPHA: 0x305, DST_COLOR: 0x306, ONE_MINUS_DST_COLOR: 0x307, SRC_ALPHA_SATURATE: 0x308,
  NEVER: 0x200, LESS: 0x201, EQUAL: 0x202, LEQUAL: 0x203, GREATER: 0x204, NOTEQUAL: 0x205, GEQUAL: 0x206, ALWAYS: 0x207,
  FRONT: 0x404, BACK: 0x405,
  NEAREST: 0x2600, LINEAR: 0x2601, NEAREST_MIPMAP_NEAREST: 0x2700, LINEAR_MIPMAP_NEAREST: 0x2701,
  NEAREST_MIPMAP_LINEAR: 0x2702, LINEAR_MIPMAP_LINEAR: 0x2703,
  KEEP: 0x1e00, REPLACE: 0x1e01, INCR: 0x1e02, DECR: 0x1e03, INVERT: 0x150a, INCR_WRAP: 0x8507, DECR_WRAP: 0x8508,
  REPEAT: 0x2901, CLAMP_TO_EDGE: 0x812f, MIRRORED_REPEAT: 0x8370,
});

// D3DRENDERSTATETYPE
export const RS = Object.freeze({
  ANTIALIAS: 2, TEXTUREPERSPECTIVE: 4, ZENABLE: 7, FILLMODE: 8, SHADEMODE: 9, ZWRITEENABLE: 14, ALPHATESTENABLE: 15,
  LASTPIXEL: 16, SRCBLEND: 19, DESTBLEND: 20, CULLMODE: 22, ZFUNC: 23, ALPHAREF: 24, ALPHAFUNC: 25, DITHERENABLE: 26,
  ALPHABLENDENABLE: 27, FOGENABLE: 28, SPECULARENABLE: 29, FOGCOLOR: 34, FOGTABLEMODE: 35, FOGSTART: 36, FOGEND: 37,
  FOGDENSITY: 38, COLORKEYENABLE: 41, ZBIAS: 47, RANGEFOGENABLE: 48, FLUSHBATCH: 50, STENCILENABLE: 52, STENCILFAIL: 53, STENCILZFAIL: 54, STENCILPASS: 55, STENCILFUNC: 56, STENCILREF: 57,
  STENCILMASK: 58, STENCILWRITEMASK: 59,
  TEXTUREFACTOR: 60, WRAP0: 128, CLIPPING: 136, LIGHTING: 137, EXTENTS: 138, AMBIENT: 139, FOGVERTEXMODE: 140,
  COLORVERTEX: 141, LOCALVIEWER: 142, NORMALIZENORMALS: 143, COLORKEYBLENDENABLE: 144,
  DIFFUSEMATERIALSOURCE: 145, SPECULARMATERIALSOURCE: 146, AMBIENTMATERIALSOURCE: 147, EMISSIVEMATERIALSOURCE: 148,
  VERTEXBLEND: 151, CLIPPLANEENABLE: 152,
});

// D3DTEXTURESTAGESTATETYPE
export const TSS = Object.freeze({
  COLOROP: 1, COLORARG1: 2, COLORARG2: 3, ALPHAOP: 4, ALPHAARG1: 5, ALPHAARG2: 6, TEXCOORDINDEX: 11,
  ADDRESS: 12, ADDRESSU: 13, ADDRESSV: 14, BORDERCOLOR: 15, MAGFILTER: 16, MINFILTER: 17, MIPFILTER: 18,
  MIPMAPLODBIAS: 19, MAXMIPLEVEL: 20, MAXANISOTROPY: 21, TEXTURETRANSFORMFLAGS: 24,
});

// D3DTEXTUREOP / D3DTA
export const TOP = Object.freeze({
  DISABLE: 1, SELECTARG1: 2, SELECTARG2: 3, MODULATE: 4, MODULATE2X: 5, MODULATE4X: 6, ADD: 7, ADDSIGNED: 8,
  ADDSIGNED2X: 9, SUBTRACT: 10, ADDSMOOTH: 11, BLENDDIFFUSEALPHA: 12, BLENDTEXTUREALPHA: 13, BLENDFACTORALPHA: 14,
});
export const TARG = Object.freeze({ DIFFUSE: 0, CURRENT: 1, TEXTURE: 2, TFACTOR: 3, SPECULAR: 4 });

const SUPPORTED_OPS = new Set([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14]);

const unsupported = (msg) => { throw new UnsupportedError(msg); };

/** D3DBLEND pair -> [GL src factor, GL dst factor]. BOTHSRCALPHA / BOTHINVSRCALPHA set both factors. */
export function blendFactors(src, dst) {
  if (src === 12) return [GL.SRC_ALPHA, GL.ONE_MINUS_SRC_ALPHA];
  if (src === 13) return [GL.ONE_MINUS_SRC_ALPHA, GL.SRC_ALPHA];
  const map = {
    1: GL.ZERO, 2: GL.ONE, 3: GL.SRC_COLOR, 4: GL.ONE_MINUS_SRC_COLOR, 5: GL.SRC_ALPHA, 6: GL.ONE_MINUS_SRC_ALPHA,
    7: GL.DST_ALPHA, 8: GL.ONE_MINUS_DST_ALPHA, 9: GL.DST_COLOR, 10: GL.ONE_MINUS_DST_COLOR,
  };
  if (!(src in map) && src !== 11) unsupported(`unsupported D3DBLEND source factor ${src}`);
  if (!(dst in map)) unsupported(`unsupported D3DBLEND destination factor ${dst}`);
  return [src === 11 ? GL.SRC_ALPHA_SATURATE : map[src], map[dst]];
}

/**
 * D3D fog factor (1 = no fog, 0 = fully fogged) for a distance / depth `d`. mode: D3DFOG_NONE 0, EXP 1, EXP2 2, LINEAR 3.
 * The shaders implement the same formulas; this is the oracle the tests and the scenes compute their expected pixels from.
 */
export function fogFactor(mode, d, start, end, density) {
  let f = 1;
  if (mode === 3) f = end === start ? (d <= start ? 1 : 0) : (end - d) / (end - start);
  else if (mode === 1) f = Math.exp(-d * density);
  else if (mode === 2) f = Math.exp(-((d * density) ** 2));
  return Math.min(1, Math.max(0, f));
}

/** D3DSTENCILOP -> GL. INCRSAT / DECRSAT saturate (GL INCR / DECR), INCR / DECR wrap (GL INCR_WRAP / DECR_WRAP). */
export function stencilOp(v) {
  const map = [null, GL.KEEP, GL.ZERO, GL.REPLACE, GL.INCR, GL.DECR, GL.INVERT, GL.INCR_WRAP, GL.DECR_WRAP];
  return map[v] ?? unsupported(`unsupported D3DSTENCILOP ${v}`);
}

/** D3DCMPFUNC -> GL compare function. */
export function compareFunc(v) {
  const map = [null, GL.NEVER, GL.LESS, GL.EQUAL, GL.LEQUAL, GL.GREATER, GL.NOTEQUAL, GL.GEQUAL, GL.ALWAYS];
  return map[v] ?? unsupported(`unsupported D3DCMPFUNC ${v}`);
}

/**
 * D3DCULL -> {enable, face}. D3D front faces are clockwise on screen; the renderer calls gl.frontFace(CW), so
 * D3DCULL_CW (cull clockwise = front) is GL FRONT and D3DCULL_CCW is GL BACK.
 */
export function cullMode(v) {
  if (v === 1) return { enable: false };
  if (v === 2) return { enable: true, face: GL.FRONT };
  if (v === 3) return { enable: true, face: GL.BACK };
  return unsupported(`unsupported D3DCULL ${v}`);
}

export function addressMode(v) {
  if (v === 1) return GL.REPEAT;
  if (v === 2) return GL.MIRRORED_REPEAT;
  if (v === 3) return GL.CLAMP_TO_EDGE;
  return unsupported(`texture address mode ${v} (BORDER / MIRRORONCE) is not supported`);
}

/** D3DTEXTUREMAGFILTER: POINT=1 LINEAR=2. */
export function magFilter(v) {
  if (v === 1) return GL.NEAREST;
  if (v === 2) return GL.LINEAR;
  return unsupported(`unsupported magnification filter ${v}`);
}

/** D3DTEXTUREMINFILTER (POINT=1 LINEAR=2) x D3DTEXTUREMIPFILTER (NONE=1 POINT=2 LINEAR=3). */
export function minFilter(min, mip) {
  if (min !== 1 && min !== 2) unsupported(`unsupported minification filter ${min}`);
  if (mip < 1 || mip > 3) unsupported(`unsupported mip filter ${mip}`);
  const lin = min === 2;
  if (mip === 1) return lin ? GL.LINEAR : GL.NEAREST;
  if (mip === 2) return lin ? GL.LINEAR_MIPMAP_NEAREST : GL.NEAREST_MIPMAP_NEAREST;
  return lin ? GL.LINEAR_MIPMAP_LINEAR : GL.NEAREST_MIPMAP_LINEAR;
}

export function primitiveMode(p) {
  if (p === 'triangleList') return GL.TRIANGLES;
  if (p === 'lineList') return GL.LINES;
  return unsupported(`unsupported primitive "${p}" (only indexed triangleList and lineList)`);
}

const f32 = new Float32Array(1);
const u32 = new Uint32Array(f32.buffer);
/** Reinterpret a DWORD render-state value as an IEEE float (FOGSTART, FOGEND, FOGDENSITY, MIPMAPLODBIAS). */
export function f32FromBits(bits) {
  u32[0] = bits >>> 0;
  return f32[0];
}

/** D3DCOLOR 0xAARRGGBB -> [r, g, b, a] in 0..1. */
export function unpackColor(c) {
  c >>>= 0;
  return [((c >>> 16) & 255) / 255, ((c >>> 8) & 255) / 255, (c & 255) / 255, (c >>> 24) / 255];
}

// ---- render states -------------------------------------------------------------------------------------------
// applied: the renderer reads the value. only: any other value is unsupported. ignored: accepted, no effect.
const APPLIED = (check) => ({ kind: 'applied', check });
const ONLY = (value, why) => ({ kind: 'ignored', check: (v) => v === value || unsupported(why) });
const IGNORED = { kind: 'ignored', check: () => true };
const bool = (name) => APPLIED((v) => v <= 1 || unsupported(`${name} must be 0 or 1, got ${v}`));
const range = (name, lo, hi) => APPLIED((v) => (v >= lo && v <= hi) || unsupported(`${name} value ${v} is not supported`));

const RS_SPEC = {
  [RS.ANTIALIAS]: ONLY(0, 'ANTIALIAS other than NONE is not supported'),
  [RS.TEXTUREPERSPECTIVE]: IGNORED, // always perspective-correct
  [RS.ZENABLE]: APPLIED((v) => v <= 1 || unsupported('ZENABLE = USEW (w-buffer) is not supported')),
  [RS.FILLMODE]: range('FILLMODE', 1, 3),   // POINT 1, WIREFRAME 2, SOLID 3
  [RS.SHADEMODE]: range('SHADEMODE', 1, 3),  // FLAT 1, GOURAUD 2, PHONG 3 (shaded like Gouraud in D3D7)
  [RS.ZWRITEENABLE]: bool('ZWRITEENABLE'),
  [RS.ALPHATESTENABLE]: bool('ALPHATESTENABLE'),
  [RS.LASTPIXEL]: IGNORED,
  [RS.SRCBLEND]: range('SRCBLEND', 1, 13),
  [RS.DESTBLEND]: range('DESTBLEND', 1, 10),
  [RS.CULLMODE]: range('CULLMODE', 1, 3),
  [RS.ZFUNC]: range('ZFUNC', 1, 8),
  [RS.ALPHAREF]: APPLIED(() => true),
  [RS.ALPHAFUNC]: range('ALPHAFUNC', 1, 8),
  [RS.DITHERENABLE]: IGNORED, // output is 8 bits per channel, no dithering
  [RS.ALPHABLENDENABLE]: bool('ALPHABLENDENABLE'),
  [RS.FOGENABLE]: bool('FOGENABLE'),
  [RS.SPECULARENABLE]: bool('SPECULARENABLE'),
  [RS.FOGCOLOR]: APPLIED(() => true),
  [RS.FOGTABLEMODE]: range('FOGTABLEMODE', 0, 3),
  [RS.FOGSTART]: APPLIED(() => true),
  [RS.FOGEND]: APPLIED(() => true),
  [RS.FOGDENSITY]: APPLIED(() => true),
  [RS.COLORKEYENABLE]: bool('COLORKEYENABLE'),
  [RS.ZBIAS]: ONLY(0, 'ZBIAS is not supported'),
  [RS.RANGEFOGENABLE]: bool('RANGEFOGENABLE'),
  [RS.FLUSHBATCH]: IGNORED,
  [RS.STENCILENABLE]: bool('STENCILENABLE'),
  [RS.STENCILFAIL]: APPLIED((v) => { stencilOp(v); return true; }),
  [RS.STENCILZFAIL]: APPLIED((v) => { stencilOp(v); return true; }),
  [RS.STENCILPASS]: APPLIED((v) => { stencilOp(v); return true; }),
  [RS.STENCILFUNC]: range('STENCILFUNC', 1, 8),
  [RS.STENCILREF]: APPLIED(() => true),
  [RS.STENCILMASK]: APPLIED(() => true),
  [RS.STENCILWRITEMASK]: APPLIED(() => true),
  [RS.TEXTUREFACTOR]: APPLIED(() => true),
  [RS.WRAP0]: ONLY(0, 'texture coordinate wrapping (WRAP0) is not supported'),
  [RS.CLIPPING]: IGNORED, // WebGL always clips
  [RS.LIGHTING]: bool('LIGHTING'),
  [RS.EXTENTS]: IGNORED, // screen-extent hint
  [RS.AMBIENT]: APPLIED(() => true),
  [RS.FOGVERTEXMODE]: range('FOGVERTEXMODE', 0, 3),
  [RS.COLORVERTEX]: bool('COLORVERTEX'),
  [RS.LOCALVIEWER]: bool('LOCALVIEWER'),
  [RS.NORMALIZENORMALS]: bool('NORMALIZENORMALS'),
  [RS.COLORKEYBLENDENABLE]: ONLY(0, 'COLORKEYBLENDENABLE is not supported'),
  [RS.DIFFUSEMATERIALSOURCE]: range('DIFFUSEMATERIALSOURCE', 0, 2), // D3DMCS_MATERIAL 0, COLOR1 (vertex diffuse) 1, COLOR2 (vertex specular) 2
  [RS.SPECULARMATERIALSOURCE]: range('SPECULARMATERIALSOURCE', 0, 2),
  [RS.AMBIENTMATERIALSOURCE]: range('AMBIENTMATERIALSOURCE', 0, 2),
  [RS.EMISSIVEMATERIALSOURCE]: range('EMISSIVEMATERIALSOURCE', 0, 2),
  [RS.VERTEXBLEND]: ONLY(0, 'vertex blending is not supported'),
  [RS.CLIPPLANEENABLE]: ONLY(0, 'user clip planes are not supported'),
};

const RS_FEATURE_NAMES = Object.fromEntries(Object.entries(RS).map(([k, v]) => [v, k]));

/** Validate a SetRenderState. Returns 'applied' or 'ignored'; throws UnsupportedError otherwise. */
export function classifyRenderState(id, value) {
  const spec = RS_SPEC[id];
  if (!spec) unsupported(`unknown render state ${id}: not supported by the replay renderer`);
  try {
    spec.check(value >>> 0);
  } catch (e) {
    if (e instanceof UnsupportedError) throw new UnsupportedError(`${RS_FEATURE_NAMES[id] ?? id}: ${e.message}`);
    throw e;
  }
  return spec.kind;
}

// ---- texture stage states ------------------------------------------------------------------------------------
function checkArg(v) {
  if (v & 0x10) unsupported('texture argument modifier COMPLEMENT is not supported');
  if (v & 0x20) unsupported('texture argument modifier ALPHAREPLICATE is not supported');
  if (v > TARG.SPECULAR) unsupported(`unknown texture argument ${v}`);
}

const TSS_SPEC = {
  [TSS.COLOROP]: (v) => { if (!SUPPORTED_OPS.has(v)) unsupported(`texture operation ${v} (ADDSMOOTH, bump, dot3, ...) is not supported`); },
  [TSS.ALPHAOP]: (v) => { if (!SUPPORTED_OPS.has(v)) unsupported(`texture operation ${v} (ADDSMOOTH, bump, dot3, ...) is not supported`); },
  [TSS.COLORARG1]: checkArg, [TSS.COLORARG2]: checkArg, [TSS.ALPHAARG1]: checkArg, [TSS.ALPHAARG2]: checkArg,
  [TSS.TEXCOORDINDEX]: (v) => {
    if (v >>> 16) unsupported('texture coordinate generation (TEXCOORDINDEX CAMERASPACE* flags) is not supported');
    if (v > 1) unsupported(`TEXCOORDINDEX ${v}: only the texture coordinate sets 0 and 1 are supported`);
  },
  [TSS.ADDRESS]: (v) => addressMode(v), [TSS.ADDRESSU]: (v) => addressMode(v), [TSS.ADDRESSV]: (v) => addressMode(v),
  [TSS.BORDERCOLOR]: () => {},
  [TSS.MAGFILTER]: (v) => magFilter(v),
  [TSS.MINFILTER]: (v) => minFilter(v, 1),
  [TSS.MIPFILTER]: (v) => minFilter(1, v),
  [TSS.MIPMAPLODBIAS]: () => {},
  [TSS.MAXMIPLEVEL]: () => {},
  [TSS.MAXANISOTROPY]: () => {},
  [TSS.TEXTURETRANSFORMFLAGS]: (v) => { if (v !== 0) unsupported('texture coordinate transforms (TEXTURETRANSFORMFLAGS) are not supported'); },
};

/** Validate a SetTextureStageState. Throws UnsupportedError for anything the renderer would draw wrongly. */
export function classifyTextureStageState(stage, id, value) {
  const check = TSS_SPEC[id];
  if (!check) unsupported(`unknown texture stage state ${id}: not supported by the replay renderer`);
  check(value >>> 0);
}
