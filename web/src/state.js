// D3D7 device state tracker. Pure (no WebGL): applies state-changing commands with validation and resolves the
// state into a plain "draw config" the renderer translates into GL state and shader uniforms. Unit-tested in Node.

import {
  RS, TSS, TOP, TARG, GL, UnsupportedError, classifyRenderState, classifyTextureStageState,
  blendFactors, compareFunc, cullMode, addressMode, minFilter, magFilter, f32FromBits, unpackColor,
} from './d3d-tables.js';

const IDENTITY = Object.freeze([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);
const MAX_LIGHTS = 4;
const bits = (f) => { const a = new Float32Array([f]); return new Uint32Array(a.buffer)[0]; };

// D3D7 documented defaults (D3DZENABLE assumes a z-buffer is attached, as in the original).
const RS_DEFAULTS = {
  [RS.ZENABLE]: 1, [RS.FILLMODE]: 3, [RS.SHADEMODE]: 2, [RS.ZWRITEENABLE]: 1, [RS.ALPHATESTENABLE]: 0,
  [RS.SRCBLEND]: 2, [RS.DESTBLEND]: 1, [RS.CULLMODE]: 3, [RS.ZFUNC]: 4, [RS.ALPHAREF]: 0, [RS.ALPHAFUNC]: 8,
  [RS.ALPHABLENDENABLE]: 0, [RS.FOGENABLE]: 0, [RS.SPECULARENABLE]: 0, [RS.FOGCOLOR]: 0, [RS.FOGTABLEMODE]: 0, [RS.RANGEFOGENABLE]: 0,
  [RS.FOGSTART]: bits(0), [RS.FOGEND]: bits(1), [RS.FOGDENSITY]: bits(1), [RS.COLORKEYENABLE]: 0,
  [RS.TEXTUREFACTOR]: 0xffffffff, [RS.LIGHTING]: 1, [RS.AMBIENT]: 0, [RS.FOGVERTEXMODE]: 0, [RS.COLORVERTEX]: 1, [RS.LOCALVIEWER]: 1, [RS.SPECULARMATERIALSOURCE]: 2,
  [RS.NORMALIZENORMALS]: 0, [RS.DIFFUSEMATERIALSOURCE]: 1, [RS.AMBIENTMATERIALSOURCE]: 0, [RS.EMISSIVEMATERIALSOURCE]: 0,
};

function stageDefaults(stage) {
  return {
    [TSS.COLOROP]: stage === 0 ? TOP.MODULATE : TOP.DISABLE, [TSS.COLORARG1]: TARG.TEXTURE, [TSS.COLORARG2]: TARG.CURRENT,
    [TSS.ALPHAOP]: stage === 0 ? TOP.SELECTARG1 : TOP.DISABLE, [TSS.ALPHAARG1]: TARG.TEXTURE, [TSS.ALPHAARG2]: TARG.CURRENT,
    [TSS.TEXCOORDINDEX]: stage, // D3D default: stage N reads texture coordinate set N
    [TSS.ADDRESS]: 1, [TSS.MAGFILTER]: 1, [TSS.MINFILTER]: 1, [TSS.MIPFILTER]: 1, [TSS.MIPMAPLODBIAS]: 0,
  };
}

const isIdentity = (m) => m.length === 16 && m.every((v, i) => v === IDENTITY[i]);

export class DeviceState {
  /** @param {{applyLodBias?: boolean}} [opts] applyLodBias (default true): honour TSS_MIPMAPLODBIAS; false resolves it to 0 */
  constructor(opts = {}) {
    this.applyLodBias = opts.applyLodBias ?? true;
    this._init();
  }

  _init() {
    this.rs = new Map();
    this.stages = [stageDefaults(0), stageDefaults(1)];
    this.transforms = new Map([[1, IDENTITY], [2, IDENTITY], [3, IDENTITY]]);
    this.viewport = null;
    this.material = {
      diffuse: [1, 1, 1, 1], ambient: [1, 1, 1, 1], specular: [0, 0, 0, 0], emissive: [0, 0, 0, 0], power: 0,
    };
    this.lights = new Map();
    this.lightsOn = new Set();
    this.textures = [null, null];
  }

  renderState(id) { return this.rs.has(id) ? this.rs.get(id) : RS_DEFAULTS[id]; }
  stageState(stage, id) { return this.stages[stage][id]; }
  transform(type) { return this.transforms.get(type); }

  /** Apply a state command. Returns true if the command was device state, false otherwise (draw, texture, ...). */
  apply(c) {
    switch (c.op) {
      case 'resetDeviceState': this._init(); return true;
      case 'setRenderState':
        classifyRenderState(c.id, c.value);
        this.rs.set(c.id, c.value >>> 0);
        return true;
      case 'setTextureStageState':
        classifyTextureStageState(c.stage, c.id, c.value);
        if (c.id === TSS.ADDRESS) { this.stages[c.stage][TSS.ADDRESSU] = c.value; this.stages[c.stage][TSS.ADDRESSV] = c.value; }
        this.stages[c.stage][c.id] = c.value >>> 0;
        return true;
      case 'setTransform':
        if (c.type >= 1 && c.type <= 3) this.transforms.set(c.type, Array.from(c.matrix));
        else if (c.type >= 16 && c.type < 24) {
          if (!isIdentity(c.matrix)) throw new UnsupportedError('a non-identity texture transform is not supported');
        } else if (c.type >= 256) throw new UnsupportedError(`world matrix ${c.type - 256 + 1} (vertex blending) is not supported`);
        else throw new UnsupportedError(`unsupported transform type ${c.type}`);
        return true;
      case 'setViewport': this.viewport = { ...c.viewport }; return true;
      case 'setMaterial': this.material = { ...c.material }; return true;
      case 'setLight':
        if (c.light === null) { this.lights.delete(c.index); this.lightsOn.delete(c.index); } else this.lights.set(c.index, c.light);
        return true;
      case 'lightEnable':
        if (c.enabled) this.lightsOn.add(c.index); else this.lightsOn.delete(c.index);
        return true;
      case 'bindTexture': this.textures[c.stage] = c.textureId; return true;
      default: return false;
    }
  }

  /** Resolve everything a draw needs. Throws UnsupportedError for a combination the renderer cannot reproduce. */
  drawConfig() {
    const r = (id) => this.renderState(id);
    const s0 = this.stages[0];
    const s1 = this.stages[1];
    // the stage cascade ends at the first stage whose colour operation is DISABLE
    const numStages = s0[TSS.COLOROP] === TOP.DISABLE ? 0 : s1[TSS.COLOROP] === TOP.DISABLE ? 1 : 2;
    const stageCfg = (st) => ({
      colorOp: st[TSS.COLOROP], colorArg1: st[TSS.COLORARG1], colorArg2: st[TSS.COLORARG2],
      alphaOp: st[TSS.ALPHAOP], alphaArg1: st[TSS.ALPHAARG1], alphaArg2: st[TSS.ALPHAARG2],
    });
    const samplerOf = (st) => ({
      mag: magFilter(st[TSS.MAGFILTER]), min: minFilter(st[TSS.MINFILTER], st[TSS.MIPFILTER]),
      wrapS: addressMode(st[TSS.ADDRESSU] ?? st[TSS.ADDRESS]), wrapT: addressMode(st[TSS.ADDRESSV] ?? st[TSS.ADDRESS]),
    });
    const [src, dst] = blendFactors(r(RS.SRCBLEND), r(RS.DESTBLEND));
    const lighting = r(RS.LIGHTING) === 1;
    const colorVertex = r(RS.COLORVERTEX) === 1;
    let lights = [];
    if (lighting) {
      if (this.lightsOn.size > MAX_LIGHTS) throw new UnsupportedError(`at most ${MAX_LIGHTS} lights are supported, ${this.lightsOn.size} enabled`);
      lights = [...this.lightsOn].sort((a, b) => a - b).map((i) => {
        const l = this.lights.get(i);
        if (!l) throw new UnsupportedError(`light ${i} is enabled but was never set`);
        return { specular: [0, 0, 0, 0], ...l };
      });
    }
    // Table (pixel) fog wins over vertex fog when both are set; range based fog is a vertex fog feature in D3D7.
    const fogOn = r(RS.FOGENABLE) === 1;
    const fogTable = fogOn && r(RS.FOGTABLEMODE) !== 0;
    const fogMode = !fogOn ? 0 : fogTable ? r(RS.FOGTABLEMODE) : r(RS.FOGVERTEXMODE);
    const cull = cullMode(r(RS.CULLMODE));
    return {
      depth: { enable: r(RS.ZENABLE) === 1, write: r(RS.ZWRITEENABLE) === 1, func: compareFunc(r(RS.ZFUNC)) },
      cull,
      blend: { enable: r(RS.ALPHABLENDENABLE) === 1 && !(src === GL.ONE && dst === GL.ZERO), src, dst },
      alphaTest: { enable: r(RS.ALPHATESTENABLE) === 1, func: r(RS.ALPHAFUNC), ref: (r(RS.ALPHAREF) & 0xff) / 255 },
      fog: {
        mode: fogMode, table: fogTable, range: fogMode !== 0 && !fogTable && r(RS.RANGEFOGENABLE) === 1, start: f32FromBits(r(RS.FOGSTART)), end: f32FromBits(r(RS.FOGEND)),
        density: f32FromBits(r(RS.FOGDENSITY)), color: unpackColor(r(RS.FOGCOLOR)),
      },
      colorKeyEnable: r(RS.COLORKEYENABLE) === 1,
      normalizeNormals: r(RS.NORMALIZENORMALS) === 1,
      textureFactor: unpackColor(r(RS.TEXTUREFACTOR)),
      lighting: {
        enable: lighting, ambient: unpackColor(r(RS.AMBIENT)), material: this.material, lights,
        specularEnable: r(RS.SPECULARENABLE) === 1, localViewer: r(RS.LOCALVIEWER) === 1,
        // D3DMCS: 0 material, 1 vertex diffuse (COLOR1), 2 vertex specular (COLOR2); all 0 when COLORVERTEX is off
        diffuseSource: colorVertex ? r(RS.DIFFUSEMATERIALSOURCE) : 0, ambientSource: colorVertex ? r(RS.AMBIENTMATERIALSOURCE) : 0,
        specularSource: colorVertex ? r(RS.SPECULARMATERIALSOURCE) : 0, emissiveSource: colorVertex ? r(RS.EMISSIVEMATERIALSOURCE) : 0,
        diffuseFromVertex: colorVertex && r(RS.DIFFUSEMATERIALSOURCE) === 1,
        ambientFromVertex: colorVertex && r(RS.AMBIENTMATERIALSOURCE) === 1,
        emissiveFromVertex: colorVertex && r(RS.EMISSIVEMATERIALSOURCE) === 1,
      },
      numStages,
      stage0: stageCfg(s0),
      stage1: stageCfg(s1),
      texCoordIndex: [s0[TSS.TEXCOORDINDEX], s1[TSS.TEXCOORDINDEX]],
      sampler: samplerOf(s0),
      sampler1: samplerOf(s1),
      lodBias: this.applyLodBias ? f32FromBits(s0[TSS.MIPMAPLODBIAS]) : 0,
      lodBias1: this.applyLodBias ? f32FromBits(s1[TSS.MIPMAPLODBIAS]) : 0,
      texture1: this.textures[1],
      texture0: this.textures[0],
    };
  }
}
