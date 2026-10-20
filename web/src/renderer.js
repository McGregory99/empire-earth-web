// WebGL2 renderer: executes frame-model commands. See web/README.md for what is and is not supported.
// Everything that can be decided without a GL context lives in state.js / d3d-tables.js / formats.js.

import { MAIN_VS, MAIN_FS, BLIT_VS, BLIT_FS } from './shaders.js';
import { DeviceState } from './state.js';
import { convertToRGBA8, bytesPerPixel } from './formats.js';
import { primitiveMode, unpackColor, UnsupportedError } from './d3d-tables.js';
import { VERTEX_STRIDE, CLEAR, validateCommand } from './frame-model.js';

export class Renderer {
  /** @param {HTMLCanvasElement|OffscreenCanvas} canvas */
  constructor(canvas) {
    this.canvas = canvas;
    const gl = canvas.getContext('webgl2', { alpha: false, antialias: false, depth: true, stencil: false, preserveDrawingBuffer: true });
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
    this.state = new DeviceState();
    this.vbData = new Uint8Array(VERTEX_STRIDE * 1024);
    this.vbCapacityUploaded = 0;
    this.dirty = null;
    gl.disable(gl.SCISSOR_TEST);
    gl.colorMask(true, true, true, true);
    gl.depthMask(true);
    gl.clearColor(0, 0, 0, 1);
    gl.clearDepth(1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
  }

  executeFrame(frame) {
    for (const c of frame.commands) this.execute(c);
  }

  execute(c) {
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
    const bpp = bytesPerPixel(c.format);
    const pitch = c.pitch ?? c.width * bpp;
    const raw = new Uint8Array(c.width * c.height * bpp); // tightly packed copy of the source
    for (let y = 0; y < c.height; y++) raw.set(c.bytes.subarray(y * pitch, y * pitch + c.width * bpp), y * c.width * bpp);
    const old = this.textures.get(c.id);
    if (old) this._freeVariants(old);
    this.textures.set(c.id, { width: c.width, height: c.height, format: c.format, bpp, raw, key: c.colorKey, variants: new Map() });
  }

  _updateTexture(c) {
    const t = this._tex(c.id);
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
    const end = c.offset + c.bytes.length;
    if (end > this.vbData.length) {
      let n = this.vbData.length;
      while (n < end) n *= 2;
      const grown = new Uint8Array(n);
      grown.set(this.vbData);
      this.vbData = grown;
      this.vbCapacityUploaded = 0; // force a full re-upload with the new size
    }
    this.vbData.set(c.bytes, c.offset);
    this.dirty = this.dirty ? [Math.min(this.dirty[0], c.offset), Math.max(this.dirty[1], end)] : [c.offset, end];
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
  }

  _draw(c) {
    const gl = this.gl;
    if (c.indices.length === 0) return;
    const cfg = this.state.drawConfig(); // throws UnsupportedError before any GL state changes
    if ((c.startVertex + c.vertexCount) * VERTEX_STRIDE > this.vbData.length) throw new Error('draw: vertex range outside the vertex buffer');
    const mode = primitiveMode(c.primitive);
    this._flushVertices();

    const prog = this.main;
    gl.useProgram(prog.p);
    gl.bindVertexArray(this.vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.vbo);
    const base = c.startVertex * VERTEX_STRIDE;
    gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, VERTEX_STRIDE, base);
    gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 3, gl.FLOAT, false, VERTEX_STRIDE, base + 12);
    gl.enableVertexAttribArray(2); gl.vertexAttribPointer(2, 4, gl.UNSIGNED_BYTE, true, VERTEX_STRIDE, base + 24);
    gl.enableVertexAttribArray(3); gl.vertexAttribPointer(3, 2, gl.FLOAT, false, VERTEX_STRIDE, base + 28);
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.ibo);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, c.indices, gl.STREAM_DRAW);

    // fixed-function state -> GL state
    this._viewport();
    gl.disable(gl.SCISSOR_TEST);
    gl.colorMask(true, true, true, true);
    gl.frontFace(gl.CW);
    if (cfg.cull.enable) { gl.enable(gl.CULL_FACE); gl.cullFace(cfg.cull.face); } else gl.disable(gl.CULL_FACE);
    if (cfg.depth.enable) { gl.enable(gl.DEPTH_TEST); gl.depthFunc(cfg.depth.func); } else gl.disable(gl.DEPTH_TEST);
    gl.depthMask(cfg.depth.write);
    if (cfg.blend.enable) { gl.enable(gl.BLEND); gl.blendFunc(cfg.blend.src, cfg.blend.dst); } else gl.disable(gl.BLEND);

    const u = (n) => this._u(prog, n);
    const m = this.state;
    gl.uniformMatrix4fv(u('uWorld'), false, m.transform(1));
    gl.uniformMatrix4fv(u('uView'), false, m.transform(2));
    gl.uniformMatrix4fv(u('uProj'), false, m.transform(3));
    const L = cfg.lighting;
    gl.uniform1i(u('uLighting'), L.enable);
    gl.uniform1i(u('uNormalize'), cfg.normalizeNormals);
    gl.uniform4fv(u('uGlobalAmbient'), L.ambient);
    gl.uniform4fv(u('uMatDiffuse'), L.material.diffuse);
    gl.uniform4fv(u('uMatAmbient'), L.material.ambient);
    gl.uniform4fv(u('uMatEmissive'), L.material.emissive);
    gl.uniform1i(u('uDiffuseFromVertex'), L.diffuseFromVertex);
    gl.uniform1i(u('uAmbientFromVertex'), L.ambientFromVertex);
    gl.uniform1i(u('uEmissiveFromVertex'), L.emissiveFromVertex);
    gl.uniform1i(u('uNumLights'), L.lights.length);
    const dirs = new Float32Array(12), dif = new Float32Array(16), amb = new Float32Array(16);
    L.lights.forEach((l, i) => { dirs.set(l.direction, i * 3); dif.set(l.diffuse, i * 4); amb.set(l.ambient, i * 4); });
    gl.uniform3fv(u('uLightDir'), dirs);
    gl.uniform4fv(u('uLightDiffuse'), dif);
    gl.uniform4fv(u('uLightAmbient'), amb);
    gl.uniform1i(u('uFogMode'), cfg.fog.mode);
    gl.uniform1f(u('uFogStart'), cfg.fog.start);
    gl.uniform1f(u('uFogEnd'), cfg.fog.end);
    gl.uniform1f(u('uFogDensity'), cfg.fog.density);
    gl.uniform4fv(u('uFogColor'), cfg.fog.color);

    const s = cfg.stage0;
    gl.uniform1i(u('uColorOp'), s.colorOp); gl.uniform1i(u('uColorArg1'), s.colorArg1); gl.uniform1i(u('uColorArg2'), s.colorArg2);
    gl.uniform1i(u('uAlphaOp'), s.alphaOp); gl.uniform1i(u('uAlphaArg1'), s.alphaArg1); gl.uniform1i(u('uAlphaArg2'), s.alphaArg2);
    gl.uniform4fv(u('uTFactor'), cfg.textureFactor);
    gl.uniform1i(u('uAlphaTest'), cfg.alphaTest.enable);
    gl.uniform1i(u('uAlphaFunc'), cfg.alphaTest.func);
    gl.uniform1f(u('uAlphaRef'), cfg.alphaTest.ref);
    gl.uniform1f(u('uLodBias'), cfg.lodBias);

    // A stage-0 TEXTURE argument with no texture bound evaluates to opaque white.
    gl.activeTexture(gl.TEXTURE0);
    if (cfg.texture0 !== null) {
      const t = this._tex(cfg.texture0);
      gl.bindTexture(gl.TEXTURE_2D, this._glTexture(cfg.texture0, cfg.colorKeyEnable ? t.key : null));
      gl.bindSampler(0, this._sampler(cfg.sampler));
      gl.uniform1i(u('uHasTex'), true);
    } else {
      gl.uniform1i(u('uHasTex'), false);
    }
    gl.uniform1i(u('uTex'), 0);

    gl.drawElements(mode, c.indices.length, gl.UNSIGNED_SHORT, 0);
    gl.bindSampler(0, null);
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
    if (c.flags & CLEAR.STENCIL) throw new UnsupportedError('clearing the stencil buffer is not supported');
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
