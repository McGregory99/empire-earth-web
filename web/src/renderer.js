// WebGL2 renderer: executes frame-model commands. See web/README.md for what is and is not supported.
// Everything that can be decided without a GL context lives in state.js / d3d-tables.js / formats.js.

import { MAIN_VS, MAIN_FS, BLIT_VS, BLIT_FS } from './shaders.js';
import { DeviceState } from './state.js';
import { convertToRGBA8, bytesPerPixel, isCompressed, textureByteSize } from './formats.js';
import { primitiveMode, unpackColor, UnsupportedError, GL } from './d3d-tables.js';
import { flatIndices, wireframeIndices, pointIndices, cullIndices, projectToNdc } from './fill-modes.js';
import { FVF, CLEAR, validateCommand, vertexLayout } from './frame-model.js';

// The vertex buffer holds every vertex in one canonical layout, whatever format it was uploaded in:
// position (3 floats), normal (3), diffuse (BGRA bytes), specular (BGRA bytes), texture sets 0 and 1 (2 floats each).
const CANON = 48;
const CANON_WHITE = 0xffffffff;

export class Renderer {
  /**
   * @param {HTMLCanvasElement|OffscreenCanvas} canvas
   * @param {{applyLodBias?: boolean}} [opts] applyLodBias (default false): the original, as captured under Wine / macOS OpenGL, does
   *   not apply TSS_MIPMAPLODBIAS (measured against skirmish-7: whole frame mean abs difference 2.77 with it, 0.92 without)
   */
  constructor(canvas, opts = {}) {
    this.canvas = canvas;
    this.applyLodBias = opts.applyLodBias ?? false;
    const gl = canvas.getContext('webgl2', { alpha: false, antialias: false, depth: true, stencil: true, preserveDrawingBuffer: true });
    if (!gl) throw new Error('WebGL2 is not available in this browser');
    this.gl = gl;
    this.main = this._program(MAIN_VS, MAIN_FS);
    this.blit = this._program(BLIT_VS, BLIT_FS);
    this.vao = gl.createVertexArray();
    this.vbo = gl.createBuffer();
    this.ibo = gl.createBuffer();
    this.emptyVao = gl.createVertexArray();
    this.samplers = new Map();
    this.nearest = gl.createSampler();
    gl.samplerParameteri(this.nearest, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.samplerParameteri(this.nearest, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.samplerParameteri(this.nearest, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.samplerParameteri(this.nearest, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    this.textures = new Map();
    this.stats = { gdiPixels: 0 };
    this.reset();
  }

  get width() { return this.canvas.width; }
  get height() { return this.canvas.height; }

  _program(vs, fs) {
    const gl = this.gl;
    const compile = (type, src) => {
      const s = gl.createShader(type);
      gl.shaderSource(s, src);
      gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(`shader compile failed: ${gl.getShaderInfoLog(s)}`);
      return s;
    };
    const p = gl.createProgram();
    gl.attachShader(p, compile(gl.VERTEX_SHADER, vs));
    gl.attachShader(p, compile(gl.FRAGMENT_SHADER, fs));
    gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(`program link failed: ${gl.getProgramInfoLog(p)}`);
    return { p, loc: new Map() };
  }

  _u(prog, name) {
    let l = prog.loc.get(name);
    if (l === undefined) { l = this.gl.getUniformLocation(prog.p, name); prog.loc.set(name, l); }
    return l;
  }

  /** Drop all device state, textures and vertices; clear the target. Used by backwards seeks. */
  reset() {
    const gl = this.gl;
    for (const t of this.textures.values()) this._freeVariants(t);
    this.textures.clear();
    this.state = new DeviceState({ applyLodBias: this.applyLodBias });
    this.vbData = new Uint8Array(CANON * 1024);
    this.vbHas = []; // per vertex: bit 0 the format had a diffuse colour, bit 1 a specular one
    this.vbCapacityUploaded = 0;
    this.dirty = null;
    gl.disable(gl.SCISSOR_TEST);
    gl.colorMask(true, true, true, true);
    gl.depthMask(true);
    gl.clearColor(0, 0, 0, 1);
    gl.clearDepth(1);
    gl.stencilMask(0xff);
    gl.clearStencil(0);
    gl.disable(gl.STENCIL_TEST);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT | gl.STENCIL_BUFFER_BIT);
  }

  executeFrame(frame) {
    for (const c of frame.commands) this.execute(c);
  }

  /** Execute one command. Errors name the trace line when the command carries one (`line`, set by the trace loader). */
  execute(c) {
    try {
      this._execute(c);
    } catch (e) {
      if (c.line != null && !e.traceLine) { e.traceLine = c.line; e.message += ` [trace line ${c.line}]`; }
      throw e;
    }
  }

  _execute(c) {
    validateCommand(c);
    if (this.state.apply(c)) return;
    switch (c.op) {
      case 'setDisplayMode':
        this.canvas.width = c.width;
        this.canvas.height = c.height;
        this.gl.clearColor(0, 0, 0, 1);
        this.gl.clear(this.gl.COLOR_BUFFER_BIT | this.gl.DEPTH_BUFFER_BIT);
        break;
      case 'createTexture': this._createTexture(c); break;
      case 'updateTexture': this._updateTexture(c); break;
      case 'setColorKey': this._tex(c.id).key = c.key; break;
      case 'destroyTexture': {
        const t = this._tex(c.id);
        this._freeVariants(t);
        this.textures.delete(c.id);
        break;
      }
      case 'uploadVertices': this._uploadVertices(c); break;
      case 'draw': this._draw(c); break;
      case 'clear': this._clear(c); break;
      case 'fillRect': this._fillRect(c); break;
      case 'blit2D': this._blit(c); break;
      case 'gdiOverlay': this._gdiOverlay(c); break;
      case 'present': this.gl.flush(); break;
      default: throw new Error(`renderer: unhandled op ${c.op}`);
    }
  }

  // ---- textures ----------------------------------------------------------------------------------------------
  _tex(id) {
    const t = this.textures.get(id);
    if (!t) throw new Error(`unknown texture / surface id ${id}`);
    return t;
  }

  _freeVariants(t) {
    for (const v of t.variants.values()) this.gl.deleteTexture(v);
    t.variants.clear();
  }

  _createTexture(c) {
    const compressed = isCompressed(c.format);
    const bpp = compressed ? 0 : bytesPerPixel(c.format);
    const pitch = c.pitch ?? c.width * bpp;
    // tightly packed copy of the source (compressed: the whole run of blocks)
    const raw = new Uint8Array(textureByteSize(c.format, c.width, c.height));
    if (compressed) raw.set(c.bytes.subarray(0, raw.length));
    else for (let y = 0; y < c.height; y++) raw.set(c.bytes.subarray(y * pitch, y * pitch + c.width * bpp), y * c.width * bpp);
    const old = this.textures.get(c.id);
    if (old) this._freeVariants(old);
    this.textures.set(c.id, { width: c.width, height: c.height, format: c.format, bpp, raw, key: c.colorKey, variants: new Map() });
  }

  _updateTexture(c) {
    const t = this._tex(c.id);
    if (isCompressed(t.format)) {
      if (c.rect || c.bytes.length < t.raw.length) throw new Error(`updateTexture ${c.id}: a ${t.format} texture is replaced whole (${t.raw.length} bytes)`);
      t.raw.set(c.bytes.subarray(0, t.raw.length));
      this._freeVariants(t);
      return;
    }
    const r = c.rect ?? { x: 0, y: 0, w: t.width, h: t.height };
    if (r.x < 0 || r.y < 0 || r.x + r.w > t.width || r.y + r.h > t.height) throw new Error(`updateTexture ${c.id}: rect outside the texture`);
    if (c.bytes.length < r.w * r.h * t.bpp) throw new Error(`updateTexture ${c.id}: needs ${r.w * r.h * t.bpp} bytes`);
    for (let y = 0; y < r.h; y++) {
      t.raw.set(c.bytes.subarray(y * r.w * t.bpp, (y + 1) * r.w * t.bpp), ((r.y + y) * t.width + r.x) * t.bpp);
    }
    this._freeVariants(t);
  }

  /** GL texture for an id, with the given colour key applied (null = none). Built lazily, cached per key. */
  _glTexture(id, key) {
    const t = this._tex(id);
    const k = key ? `${key.low}-${key.high}` : '-';
    let v = t.variants.get(k);
    if (!v) {
      const gl = this.gl;
      const rgba = convertToRGBA8(t.format, t.width, t.height, t.raw, { colorKey: key });
      v = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, v);
      gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, t.width, t.height, 0, gl.RGBA, gl.UNSIGNED_BYTE, rgba);
      gl.generateMipmap(gl.TEXTURE_2D); // the trace carries no mip data; levels are derived from level 0
      t.variants.set(k, v);
    }
    return v;
  }

  _sampler(s) {
    const key = `${s.mag}/${s.min}/${s.wrapS}/${s.wrapT}`;
    let o = this.samplers.get(key);
    if (!o) {
      const gl = this.gl;
      o = gl.createSampler();
      gl.samplerParameteri(o, gl.TEXTURE_MAG_FILTER, s.mag);
      gl.samplerParameteri(o, gl.TEXTURE_MIN_FILTER, s.min);
      gl.samplerParameteri(o, gl.TEXTURE_WRAP_S, s.wrapS);
      gl.samplerParameteri(o, gl.TEXTURE_WRAP_T, s.wrapT);
      this.samplers.set(key, o);
    }
    return o;
  }

  // ---- vertices ----------------------------------------------------------------------------------------------
  _uploadVertices(c) {
    const lay = vertexLayout(c.fvf ?? FVF);
    const n = c.bytes.length / lay.stride;
    const first = c.offset / lay.stride;
    const end = (first + n) * CANON;
    if (end > this.vbData.length) {
      let len = this.vbData.length;
      while (len < end) len *= 2;
      const grown = new Uint8Array(len);
      grown.set(this.vbData);
      this.vbData = grown;
      this.vbCapacityUploaded = 0; // force a full re-upload with the new size
    }
    const src = new DataView(c.bytes.buffer, c.bytes.byteOffset, c.bytes.byteLength);
    const dst = new DataView(this.vbData.buffer);
    for (let i = 0; i < n; i++) {
      const s = i * lay.stride, d = (first + i) * CANON;
      for (let k = 0; k < 12; k += 4) dst.setUint32(d + k, src.getUint32(s + k, true), true);
      for (let k = 0; k < 12; k += 4) dst.setUint32(d + 12 + k, lay.normal >= 0 ? src.getUint32(s + lay.normal + k, true) : 0, true);
      this.vbHas[first + i] = (lay.diffuse >= 0 ? 1 : 0) | (lay.specular >= 0 ? 2 : 0);
      dst.setUint32(d + 24, lay.diffuse >= 0 ? src.getUint32(s + lay.diffuse, true) : CANON_WHITE, true);
      dst.setUint32(d + 28, lay.specular >= 0 ? src.getUint32(s + lay.specular, true) : 0, true);
      for (let t = 0; t < 2; t++) {
        for (let k = 0; k < 8; k += 4) dst.setUint32(d + 32 + t * 8 + k, lay.uv[t] !== undefined ? src.getUint32(s + lay.uv[t] + k, true) : 0, true);
      }
    }
    const lo = first * CANON;
    this.dirty = this.dirty ? [Math.min(this.dirty[0], lo), Math.max(this.dirty[1], end)] : [lo, end];
  }

  _flushVertices() {
    const gl = this.gl;
    gl.bindBuffer(gl.ARRAY_BUFFER, this.vbo);
    if (this.vbCapacityUploaded !== this.vbData.length) {
      gl.bufferData(gl.ARRAY_BUFFER, this.vbData, gl.DYNAMIC_DRAW);
      this.vbCapacityUploaded = this.vbData.length;
    } else if (this.dirty) {
      gl.bufferSubData(gl.ARRAY_BUFFER, this.dirty[0], this.vbData, this.dirty[0], this.dirty[1] - this.dirty[0]);
    }
    this.dirty = null;
  }

  // ---- drawing -----------------------------------------------------------------------------------------------
  _viewport() {
    const v = this.state.viewport ?? { x: 0, y: 0, width: this.width, height: this.height, minZ: 0, maxZ: 1 };
    this.gl.viewport(v.x, this.height - v.y - v.height, v.width, v.height);
    this.gl.depthRange(v.minZ, v.maxZ);
    return v;
  }

  _draw(c) {
    const gl = this.gl;
    if (c.indices.length === 0) return;
    const cfg = this.state.drawConfig(); // throws UnsupportedError before any GL state changes
    if ((c.startVertex + c.vertexCount) * CANON > this.vbData.length) throw new Error('draw: vertex range outside the vertex buffer');
    let mode = primitiveMode(c.primitive);
    let indices = c.indices;
    if (cfg.fill !== 'solid') {
      // GL has no polygon mode: draw the edges / vertices of the triangles that survive D3D's culling
      if (c.primitive === 'triangleList') {
        const f = new Float32Array(this.vbData.buffer, this.vbData.byteOffset, this.vbData.byteLength / 4);
        const memo = new Map();
        const ndcOf = (i) => {
          if (!memo.has(i)) { const o = ((c.startVertex + i) * CANON) / 4; memo.set(i, projectToNdc([f[o], f[o + 1], f[o + 2]], this.state.transform(1), this.state.transform(2), this.state.transform(3))); }
          return memo.get(i);
        };
        indices = cullIndices(indices, ndcOf, cfg.cullSide);
      }
      indices = cfg.fill === 'wireframe' && c.primitive === 'triangleList' ? wireframeIndices(indices) : cfg.fill === 'point' ? pointIndices(indices) : indices;
      mode = cfg.fill === 'point' ? GL.POINTS : GL.LINES;
      if (indices.length === 0) return;
    } else if (cfg.flat) {
      indices = flatIndices(c.primitive, indices);
    }
    this._flushVertices();

    const prog = this.main;
    gl.useProgram(prog.p);
    gl.bindVertexArray(this.vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.vbo);
    const base = c.startVertex * CANON;
    gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, CANON, base);
    gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 3, gl.FLOAT, false, CANON, base + 12);
    gl.enableVertexAttribArray(2); gl.vertexAttribPointer(2, 4, gl.UNSIGNED_BYTE, true, CANON, base + 24);
    gl.enableVertexAttribArray(4); gl.vertexAttribPointer(4, 4, gl.UNSIGNED_BYTE, true, CANON, base + 28);
    gl.enableVertexAttribArray(3); gl.vertexAttribPointer(3, 2, gl.FLOAT, false, CANON, base + 32);
    gl.enableVertexAttribArray(5); gl.vertexAttribPointer(5, 2, gl.FLOAT, false, CANON, base + 40);
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.ibo);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, indices, gl.STREAM_DRAW);

    // fixed-function state -> GL state
    const vp = this._viewport();
    gl.disable(gl.SCISSOR_TEST);
    gl.colorMask(true, true, true, true);
    gl.frontFace(gl.CW);
    if (cfg.cull.enable) { gl.enable(gl.CULL_FACE); gl.cullFace(cfg.cull.face); } else gl.disable(gl.CULL_FACE);
    if (cfg.depth.enable) { gl.enable(gl.DEPTH_TEST); gl.depthFunc(cfg.depth.func); } else gl.disable(gl.DEPTH_TEST);
    gl.depthMask(cfg.depth.write);
    const sten = cfg.stencil;
    if (sten.enable) {
      gl.enable(gl.STENCIL_TEST);
      gl.stencilFunc(sten.func, sten.ref, sten.mask);
      gl.stencilOp(sten.fail, sten.zfail, sten.pass);
      gl.stencilMask(sten.writeMask);
    } else gl.disable(gl.STENCIL_TEST);
    if (cfg.blend.enable) { gl.enable(gl.BLEND); gl.blendFunc(cfg.blend.src, cfg.blend.dst); } else gl.disable(gl.BLEND);

    const u = (n) => this._u(prog, n);
    const m = this.state;
    gl.uniform2f(u('uHalfPixel'), 1 / vp.width, -1 / vp.height);
    gl.uniformMatrix4fv(u('uWorld'), false, m.transform(1));
    gl.uniformMatrix4fv(u('uView'), false, m.transform(2));
    gl.uniformMatrix4fv(u('uProj'), false, m.transform(3));
    const L = cfg.lighting;
    // D3D7 uses the material colour when the selected vertex component is not in the format (formats are not mixed within a draw)
    const has = this.vbHas[c.startVertex + c.indices[0]] ?? 3;
    const src = (v) => (v === 1 && !(has & 1)) || (v === 2 && !(has & 2)) ? 0 : v;
    gl.uniform1i(u('uLighting'), L.enable);
    gl.uniform1i(u('uNormalize'), cfg.normalizeNormals);
    gl.uniform1i(u('uFlat'), cfg.flat);
    gl.uniform4fv(u('uGlobalAmbient'), L.ambient);
    gl.uniform4fv(u('uMatDiffuse'), L.material.diffuse);
    gl.uniform4fv(u('uMatAmbient'), L.material.ambient);
    gl.uniform4fv(u('uMatEmissive'), L.material.emissive);
    gl.uniform1i(u('uDiffuseSrc'), src(L.diffuseSource));
    gl.uniform1i(u('uAmbientSrc'), src(L.ambientSource));
    gl.uniform1i(u('uEmissiveSrc'), src(L.emissiveSource));
    gl.uniform1i(u('uSpecularSrc'), src(L.specularSource));
    gl.uniform1i(u('uSpecular'), L.specularEnable);
    gl.uniform1i(u('uLocalViewer'), L.localViewer);
    gl.uniform4fv(u('uMatSpecular'), L.material.specular);
    gl.uniform1f(u('uMatPower'), L.material.power);
    gl.uniform1i(u('uNumLights'), L.lights.length);
    const dirs = new Float32Array(12), dif = new Float32Array(16), amb = new Float32Array(16), spc = new Float32Array(16);
    const types = new Int32Array(4), pos = new Float32Array(12), att = new Float32Array(12), range = new Float32Array(4), cone = new Float32Array(12);
    L.lights.forEach((l, i) => {
      dirs.set(l.direction ?? [0, 0, 1], i * 3); dif.set(l.diffuse, i * 4); amb.set(l.ambient, i * 4); spc.set(l.specular, i * 4);
      types[i] = { directional: 0, point: 1, spot: 2 }[l.type];
      if (l.type !== 'directional') {
        pos.set(l.position, i * 3); att.set(l.attenuation, i * 3); range[i] = l.range;
        if (l.type === 'spot') cone.set([Math.cos(l.phi / 2), Math.cos(l.theta / 2), l.falloff], i * 3);
      }
    });
    gl.uniform1iv(u('uLightType'), types);
    gl.uniform3fv(u('uLightPos'), pos);
    gl.uniform3fv(u('uLightAtt'), att);
    gl.uniform1fv(u('uLightRange'), range);
    gl.uniform3fv(u('uLightSpot'), cone);
    gl.uniform3fv(u('uLightDir'), dirs);
    gl.uniform4fv(u('uLightDiffuse'), dif);
    gl.uniform4fv(u('uLightAmbient'), amb);
    gl.uniform4fv(u('uLightSpecular'), spc);
    gl.uniform1i(u('uFogMode'), cfg.fog.mode);
    gl.uniform1i(u('uFogTable'), cfg.fog.table);
    gl.uniform1i(u('uFogRange'), cfg.fog.range);
    gl.uniform1f(u('uFogStart'), cfg.fog.start);
    gl.uniform1f(u('uFogEnd'), cfg.fog.end);
    gl.uniform1f(u('uFogDensity'), cfg.fog.density);
    gl.uniform4fv(u('uFogColor'), cfg.fog.color);

    const st = [cfg.stage0, cfg.stage1];
    const iv = (name, f) => gl.uniform1iv(u(name), st.map(f));
    gl.uniform1i(u('uNumStages'), cfg.numStages);
    iv('uColorOp', (x) => x.colorOp); iv('uColorArg1', (x) => x.colorArg1); iv('uColorArg2', (x) => x.colorArg2);
    iv('uAlphaOp', (x) => x.alphaOp); iv('uAlphaArg1', (x) => x.alphaArg1); iv('uAlphaArg2', (x) => x.alphaArg2);
    gl.uniform1iv(u('uTexSet'), cfg.texCoordIndex);
    gl.uniform1fv(u('uLodBias'), [cfg.lodBias, cfg.lodBias1]);
    gl.uniform4fv(u('uTFactor'), cfg.textureFactor);
    gl.uniform1i(u('uAlphaTest'), cfg.alphaTest.enable);
    gl.uniform1i(u('uAlphaFunc'), cfg.alphaTest.func);
    gl.uniform1f(u('uAlphaRef'), cfg.alphaTest.ref);

    // A TEXTURE argument with no texture bound evaluates to opaque white (uHasTexN false).
    [[cfg.texture0, cfg.sampler, 0], [cfg.texture1, cfg.sampler1, 1]].forEach(([id, sampler, unit]) => {
      gl.activeTexture(gl.TEXTURE0 + unit);
      if (id !== null && cfg.numStages > unit) {
        const t = this._tex(id);
        gl.bindTexture(gl.TEXTURE_2D, this._glTexture(id, cfg.colorKeyEnable ? t.key : null));
        gl.bindSampler(unit, this._sampler(sampler));
        gl.uniform1i(u(`uHasTex${unit}`), true);
      } else {
        gl.uniform1i(u(`uHasTex${unit}`), false);
      }
      gl.uniform1i(u(`uTex${unit}`), unit);
    });
    gl.activeTexture(gl.TEXTURE0);

    gl.drawElements(mode, indices.length, gl.UNSIGNED_SHORT, 0);
    gl.bindSampler(0, null);
    gl.bindSampler(1, null);
  }

  _scissor(rect) {
    const gl = this.gl;
    if (rect) {
      gl.enable(gl.SCISSOR_TEST);
      gl.scissor(rect.x, this.height - rect.y - rect.h, rect.w, rect.h);
    } else gl.disable(gl.SCISSOR_TEST);
  }

  _clear(c) {
    const gl = this.gl;
    gl.viewport(0, 0, this.width, this.height);
    this._scissor(c.rect);
    let bits = 0;
    if (c.flags & CLEAR.TARGET) {
      const col = unpackColor(c.color);
      gl.colorMask(true, true, true, true);
      gl.clearColor(col[0], col[1], col[2], 1);
      bits |= gl.COLOR_BUFFER_BIT;
    }
    if (c.flags & CLEAR.ZBUFFER) { gl.depthMask(true); gl.clearDepth(c.z); bits |= gl.DEPTH_BUFFER_BIT; }
    if (c.flags & CLEAR.STENCIL) { gl.stencilMask(0xff); gl.clearStencil((c.stencil ?? 0) & 0xff); bits |= gl.STENCIL_BUFFER_BIT; }
    if (bits) gl.clear(bits);
    gl.disable(gl.SCISSOR_TEST);
  }

  _fillRect(c) {
    const gl = this.gl;
    gl.viewport(0, 0, this.width, this.height);
    this._scissor(c.rect);
    const col = unpackColor(c.color);
    gl.colorMask(true, true, true, true);
    gl.clearColor(col[0], col[1], col[2], 1);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.disable(gl.SCISSOR_TEST);
  }

  /**
   * Blt with nearest sampling. With a colour key the source texels equal to it are skipped. Limitation: a keyed
   * blit also skips texels whose own alpha is 0 (the converter encodes "keyed" as alpha 0).
   */
  _blit(c) {
    const gl = this.gl;
    const t = this._tex(c.srcId);
    const prog = this.blit;
    gl.useProgram(prog.p);
    gl.bindVertexArray(this.emptyVao);
    gl.viewport(0, 0, this.width, this.height);
    gl.disable(gl.SCISSOR_TEST);
    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.STENCIL_TEST);
    gl.disable(gl.CULL_FACE);
    gl.disable(gl.BLEND);
    gl.colorMask(true, true, true, true);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this._glTexture(c.srcId, c.colorKey));
    gl.bindSampler(0, this.nearest);
    gl.uniform1i(this._u(prog, 'uTex'), 0);
    gl.uniform1i(this._u(prog, 'uKeyed'), c.colorKey !== null);
    gl.uniform4f(this._u(prog, 'uDst'), c.dstRect.x, c.dstRect.y, c.dstRect.w, c.dstRect.h);
    const r = c.srcRect;
    gl.uniform4f(this._u(prog, 'uSrc'), r.x / t.width, r.y / t.height, (r.x + r.w) / t.width, (r.y + r.h) / t.height);
    gl.uniform2f(this._u(prog, 'uTarget'), this.width, this.height);
    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
    gl.bindSampler(0, null);
  }

  /** Copy the pixels of a stored screen snapshot that differ from the current render (see cmd.gdiOverlay). */
  _gdiOverlay(c) {
    const gl = this.gl;
    const t = this._tex(c.srcId);
    const w = this.width, h = this.height;
    if (t.width !== w || t.height !== h) throw new Error(`gdiOverlay: snapshot ${t.width}x${t.height} does not match the ${w}x${h} target`);
    const snap = convertToRGBA8(t.format, w, h, t.raw);
    const cur = this.readPixels();
    let n = 0;
    for (let i = 0; i < snap.length; i += 4) {
      const d = Math.max(Math.abs(snap[i] - cur[i]), Math.abs(snap[i + 1] - cur[i + 1]), Math.abs(snap[i + 2] - cur[i + 2]));
      if (d > c.threshold) { snap[i + 3] = 255; n++; } else snap[i + 3] = 0;
    }
    this.stats.gdiPixels += n;
    if (!n) return;
    const tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, w, h, 0, gl.RGBA, gl.UNSIGNED_BYTE, snap);
    const prog = this.blit;
    gl.useProgram(prog.p);
    gl.bindVertexArray(this.emptyVao);
    gl.viewport(0, 0, w, h);
    gl.disable(gl.SCISSOR_TEST); gl.disable(gl.DEPTH_TEST); gl.disable(gl.STENCIL_TEST); gl.disable(gl.CULL_FACE); gl.disable(gl.BLEND);
    gl.colorMask(true, true, true, true);
    gl.bindSampler(0, this.nearest);
    gl.uniform1i(this._u(prog, 'uTex'), 0);
    gl.uniform1i(this._u(prog, 'uKeyed'), true);
    gl.uniform4f(this._u(prog, 'uDst'), 0, 0, w, h);
    gl.uniform4f(this._u(prog, 'uSrc'), 0, 0, 1, 1);
    gl.uniform2f(this._u(prog, 'uTarget'), w, h);
    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
    gl.bindSampler(0, null);
    gl.deleteTexture(tex);
  }

  /** The current target as top-down RGBA8 (width * height * 4 bytes). */
  readPixels() {
    const gl = this.gl;
    const w = this.width, h = this.height;
    const flipped = new Uint8Array(w * h * 4);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.readPixels(0, 0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, flipped);
    const out = new Uint8Array(w * h * 4);
    for (let y = 0; y < h; y++) out.set(flipped.subarray((h - 1 - y) * w * 4, (h - y) * w * 4), y * w * 4);
    return out;
  }
}
