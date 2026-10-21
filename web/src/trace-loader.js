// Trace loader: turns a ddraw_trace text trace plus its `.data` side file into a frame-model Sequence.
//
// The trace is the call log of the original renderer at the DirectDraw 7 / Direct3D 7 boundary. The loader keeps the
// small amount of state needed to turn calls into commands (which surfaces are the screen, the pixels of every
// surface, one state shadow per Direct3D device) and emits `cmd.*` commands, each tagged with its trace `line`.
// See web/README.md for what is parsed and what fails loudly.

import { cmd } from './frame-model.js';
import { formatFromPixelFormat } from './formats.js';
import { TraceError, ObjRef, parseCall, parseBlob, dataFileFromHeader } from './trace-parse.js';
import { DataFile, KIND } from './data-file.js';

const DDSCAPS = { BACKBUFFER: 0x4, FLIP: 0x10, FRONTBUFFER: 0x20, PRIMARY: 0x200, DEVICE3D: 0x2000, ZBUFFER: 0x20000, MIPMAP: 0x400000 };
// DDBLT_* flags
const BLT = { COLORFILL: 0x400, ASYNC: 0x200, KEYSRC: 0x8000, KEYSRCOVERRIDE: 0x10000, WAIT: 0x01000000, DONOTWAIT: 0x08000000 };
const BLT_SUPPORTED = BLT.COLORFILL | BLT.ASYNC | BLT.KEYSRC | BLT.KEYSRCOVERRIDE | BLT.WAIT | BLT.DONOTWAIT;
const BLT_NAMES = { 0x1: 'ALPHADEST', 0x20: 'ALPHASRC', 0x800: 'DDFX', 0x1000: 'DDROPS', 0x2000: 'KEYDEST', 0x4000: 'KEYDESTOVERRIDE',
  0x20000: 'ROP', 0x40000: 'ROTATIONANGLE', 0x80000: 'ZBUFFER', 0x02000000: 'DEPTHFILL' };
const FAILED = /^(DDERR_|D3DERR_|0x[89a-fA-F][0-9a-fA-F]{7}$)/;
const VERTEX_FVF = 0x152;
const STATE_CALLS = new Set(['SetRenderState', 'SetTextureStageState', 'SetTransform', 'SetViewport', 'SetMaterial', 'SetLight', 'LightEnable', 'SetTexture']);

const rectOf = (a) => ({ x: a[0], y: a[1], w: a[2] - a[0], h: a[3] - a[1] });
const keyOf = (a) => (Array.isArray(a) && a.length === 2 ? { low: a[0], high: a[1] } : null);
const sameKey = (a, b) => (a === b) || (a && b && a.low === b.low && a.high === b.high);

class Loader {
  constructor(text, dataBytes, opts) {
    this.opts = opts;
    this.text = text;
    this.data = dataBytes ? new DataFile(dataBytes) : null;
    this.frames = [];
    this.cur = [];
    this.surfaces = new Map();
    this.shadows = new Map();
    this.active = null;
    this.display = null;
    this.inferredDisplay = null; // late-armed capture: the size of the screen surface, from its first whole-surface ReleaseDC record
    this.gdiIds = new Set();
    this.slots = new Map();
    this.nextVertex = 0;
    this.line = 0;
    this.stats = {
      lines: 0, calls: 0, presents: 0, frames: 0, commands: 0, droppedTrailingCommands: 0, skippedFailedCalls: 0, ignoredCalls: {},
      ignoredOffscreenBlts: 0, skippedDraws: 0, skippedPixelUpdates: 0, skippedBlits: 0, uniqueVertexRecords: 0, deviceSwitches: 0,
      displayModes: [], textures: 0, gdiOverlays: 0, skippedKeyedBlits: 0,
    };
  }

  fail(msg) { throw new TraceError(msg, this.line); }

  emit(c) {
    c.line = this.line;
    this.cur.push(c);
    return c;
  }

  run() {
    const header = dataFileFromHeader(this.text);
    const hasBlobs = /\sblob=[@{f]/.test(this.text);
    if (!this.data && (header || hasBlobs) && !this.opts.allowMissingData) {
      throw new TraceError(`this trace references a data file${header ? ` (${header})` : ''}: select the .data file together with the trace`);
    }
    // A capture armed late never sees the CreateSurface / SetDisplayMode calls of the screen. The surface whose whole content
    // GDI reads back (ReleaseDC with a pixel record) and that the trace never creates is the 2D back buffer: treat it as the screen.
    for (const m of this.text.matchAll(/surf#(\d+)\.ReleaseDC\([^\n]*blob=@/g)) this.gdiIds.add(Number(m[1]));
    const lines = this.text.split('\n');
    this.stats.lines = lines.length;
    for (let i = 0; i < lines.length; i++) {
      this.line = i + 1;
      if (lines[i].startsWith('# snapshot ')) { this.snapshot(lines[i]); continue; }
      const call = parseCall(lines[i].replace(/\r$/, ''), this.line);
      if (!call) continue;
      this.stats.calls++;
      try {
        this.handle(call);
      } catch (e) {
        if (e instanceof TraceError && e.line !== null) throw e;
        throw new TraceError(e.message, this.line);
      }
    }
    this.stats.droppedTrailingCommands = this.cur.length;
    this.stats.frames = this.frames.length;
    this.stats.commands = this.frames.reduce((n, f) => n + f.commands.length, 0);
    this.stats.textures = [...this.surfaces.values()].filter((s) => s.created).length;
    return {
      sequence: { width: (this.firstDisplay ?? this.inferredDisplay)?.w ?? 640, height: (this.firstDisplay ?? this.inferredDisplay)?.h ?? 480, frames: this.frames },
      stats: this.stats,
    };
  }

  // ---- dispatch ----------------------------------------------------------------------------------------------
  handle(c) {
    if (c.result === '?' || c.suffix.includes('[timing]')) return;
    const iface = c.obj?.iface;
    if (FAILED.test(c.result)) { this.stats.skippedFailedCalls++; return; }
    const fn = this[`${iface}_${c.method}`];
    if (fn) { fn.call(this, c); return; }
    if (iface === 'dev7' && /^Draw\w*Strided$/.test(c.method)) this.fail(`unsupported: ${c.method} (strided draws are not captured)`);
    if (iface === 'dev7' && /^(ProcessVertices|ComputeSphereVisibility)$/.test(c.method)) this.fail(`unsupported: ${c.method}`);
    const k = `${iface ?? 'export'}.${c.method}`;
    this.stats.ignoredCalls[k] = (this.stats.ignoredCalls[k] ?? 0) + 1;
  }

  // ---- surfaces ----------------------------------------------------------------------------------------------
  surf(id) {
    let s = this.surfaces.get(id);
    if (!s) {
      s = { id, width: 0, height: 0, caps: 0, screen: false, primary: false, zbuffer: false, key: null, pixels: null, bpp: 0,
        masks: null, dirty: false, created: false, format: null, keyEmitted: null };
      if (this.gdiIds.has(id)) { s.screen = true; s.inferredScreen = true; }
      this.surfaces.set(id, s);
    }
    return s;
  }

  dd7_SetDisplayMode(c) {
    const { dwWidth: w, dwHeight: h, dwBPP: bpp } = c.args;
    if (bpp !== 32) this.fail(`unsupported display mode ${w}x${h}x${bpp}: only 32-bit modes are supported`);
    this.display = { w, h };
    this.firstDisplay = this.firstDisplay ?? { w, h };
    this.stats.displayModes.push(`${w}x${h}`);
    this.emit(cmd.setDisplayMode(w, h));
  }

  dd7_CreateSurface(c) {
    const desc = c.args.lpDDSurfaceDesc ?? {};
    const out = c.outs.lplpDDSurface;
    if (!(out instanceof ObjRef)) this.fail('CreateSurface without a surface id');
    const s = this.surf(out.id);
    s.inferredScreen = false;
    s.caps = desc.ddsCaps?.dwCaps ?? 0;
    s.width = desc.dwWidth ?? 0;
    s.height = desc.dwHeight ?? 0;
    s.primary = !!(s.caps & DDSCAPS.PRIMARY);
    s.screen = !!(s.caps & (DDSCAPS.PRIMARY | DDSCAPS.BACKBUFFER | DDSCAPS.DEVICE3D));
    s.zbuffer = !!(s.caps & DDSCAPS.ZBUFFER);
    if (s.screen && this.display) { s.width = s.width || this.display.w; s.height = s.height || this.display.h; }
    s.key = keyOf(desc.ddckCKSrcBlt);
  }

  surf_GetAttachedSurface(c) {
    const parent = this.surf(c.obj.id);
    const out = c.outs.lplpDDAttachedSurface;
    if (!(out instanceof ObjRef)) return;
    const child = this.surf(out.id);
    const want = c.args.lpDDSCaps?.dwCaps ?? 0;
    if (parent.screen && (want & (DDSCAPS.BACKBUFFER | DDSCAPS.FLIP | DDSCAPS.FRONTBUFFER))) child.screen = true;
    if (want & DDSCAPS.ZBUFFER) child.zbuffer = true;
    if (child.screen) { child.width = child.width || parent.width; child.height = child.height || parent.height; }
  }

  d3d7_CreateDevice(c) {
    if (c.args.surface instanceof ObjRef) this.surf(c.args.surface.id).screen = true;
  }

  dev7_SetRenderTarget(c) {
    const ref = c.args.surface;
    if (!(ref instanceof ObjRef)) return;
    const s = this.surf(ref.id);
    if (!s.screen) this.fail(`unsupported: render target surf#${ref.id} is an offscreen surface (render-to-texture)`);
  }

  surf_SetColorKey(c) {
    const flags = c.args.dwFlags;
    if (flags !== 0x8) this.fail(`unsupported colour key flags 0x${flags.toString(16)} (only DDCKEY_SRCBLT)`);
    const s = this.surf(c.obj.id);
    s.key = keyOf(c.args.lpDDColorKey);
    if (s.created && !sameKey(s.key, s.keyEmitted)) { this.emit(cmd.setColorKey(s.id, s.key)); s.keyEmitted = s.key; }
  }

  // Apply the pixel record a line refers to (origin 1 Unlock, 2 ReleaseDC, 3/4 Blt source, 5 SetTexture) to a surface.
  applyBlob(s, blob) {
    if (!blob) return;
    if (blob.state) this.fail(`surf#${s.id}: blob=${blob.state} (the capture could not store the pixels)`);
    if (blob.offset === undefined) this.fail('unexpected blob form for a surface call');
    if (!this.data) { this.stats.skippedPixelUpdates++; return; }
    const r = this.data.record(blob.offset, blob.length);
    if (r.kind !== KIND.SURFACE) this.fail(`record at ${blob.offset} is not a surface record`);
    if (r.object !== s.id) this.fail(`record at ${blob.offset} belongs to surf#${r.object}, not surf#${s.id}`);
    const [l, t, rt, b, w, h, rowBytes, rows, , , fourcc, bpp, rm, gm, bm, am] = r.params;
    if (fourcc) this.fail(`unsupported: block-compressed surface surf#${s.id} (fourcc 0x${fourcc.toString(16)})`);
    const px = bpp / 8;
    if (!Number.isInteger(px) || px < 1 || rowBytes !== (rt - l) * px || rows !== b - t || r.payload.length !== rowBytes * rows) {
      this.fail(`surf#${s.id}: inconsistent surface record at ${blob.offset}`);
    }
    const whole = l === 0 && t === 0 && rt === w && b === h;
    if (whole && s.pixels && s.lastWhole === blob.offset && s.width === w && s.height === h && s.bpp === bpp) return; // same content again
    s.lastWhole = whole ? blob.offset : null;
    if (!s.pixels || s.width !== w || s.height !== h || s.bpp !== bpp) {
      s.width = w; s.height = h; s.bpp = bpp;
      s.pixels = new Uint8Array(w * h * px);
    }
    s.masks = { rgbBitCount: bpp, rMask: rm, gMask: gm, bMask: bm, aMask: am };
    for (let y = 0; y < rows; y++) s.pixels.set(r.payload.subarray(y * rowBytes, (y + 1) * rowBytes), ((t + y) * w + l) * px);
    s.dirty = true;
  }

  // "# snapshot surf#N blob=...": the pixels a surface had when the capture was armed (a surface filled before the window
  // has no Lock/Unlock in it). blob=unavailable is not an error yet: the surface may never be used, or a later Blt source /
  // SetTexture line stores it; using it without pixels still fails in flush().
  snapshot(line) {
    const m = /^# snapshot surf#(\d+)(.*)$/.exec(line.replace(/\r$/, ''));
    if (!m) this.fail(`malformed snapshot line: ${line}`);
    const blob = parseBlob(m[2]);
    if (blob && blob.state === 'unavailable') { this.stats.unavailableSnapshots = (this.stats.unavailableSnapshots ?? 0) + 1; return; }
    this.applyBlob(this.surf(Number(m[1])), blob);
    this.stats.snapshots = (this.stats.snapshots ?? 0) + 1;
  }

  surf_Unlock(c) { this.applyBlob(this.surf(c.obj.id), parseBlob(c.suffix)); }
  surf_ReleaseDC(c) {
    const s = this.surf(c.obj.id);
    const blob = parseBlob(c.suffix);
    this.applyBlob(s, blob);
    if (s.inferredScreen && s.width && s.height && !this.inferredDisplay) {
      this.inferredDisplay = { w: s.width, h: s.height };
      this.display ??= this.inferredDisplay;
    }
    // GDI (text) drawn straight onto the screen surface: the capture stored the whole surface, which includes the 3D
    // output. Take only the pixels that differ from our own render (cmd.gdiOverlay).
    if (s.screen && blob && this.data && s.pixels) {
      this.flush(s, false);
      this.emit(cmd.gdiOverlay(s.id));
      this.stats.gdiOverlays++;
    }
  }

  /** Make the renderer's copy of a surface current. With `must`, a surface without captured pixels is an error. */
  flush(s, must) {
    if (!s.pixels) {
      if (must && !this.opts.allowMissingData) {
        this.fail(`texture surf#${s.id} is used but the trace carries no pixels for it (captured without DDT_DATA, or written by a call the capture does not store)`);
      }
      return false;
    }
    let format;
    try { format = formatFromPixelFormat(s.masks); } catch (e) { this.fail(`surf#${s.id}: ${e.message}`); }
    if (!s.created || s.format !== format) {
      this.emit(cmd.createTexture(s.id, s.width, s.height, format, s.pixels.slice(), { colorKey: s.key }));
      s.created = true; s.format = format; s.keyEmitted = s.key;
    } else if (s.dirty) {
      this.emit(cmd.updateTexture(s.id, s.pixels.slice()));
    }
    s.dirty = false;
    return true;
  }

  // ---- screen output: Blt, BltFast, Flip ---------------------------------------------------------------------------
  present() {
    this.emit(cmd.present());
    this.frames.push({ commands: this.cur });
    this.cur = [];
    this.stats.presents++;
  }

  surf_Flip() { this.present(); }

  surf_Blt(c) {
    const dst = this.surf(c.obj.id);
    const srcRef = c.args.lpDDSrcSurface;
    const src = srcRef instanceof ObjRef ? this.surf(srcRef.id) : null;
    const flags = c.args.dwFlags >>> 0;
    if (src) this.applyBlob(src, parseBlob(c.suffix));
    const isPresent = dst.primary && c.args.lpDestRect === null && src !== null;
    if (!dst.screen) { this.stats.ignoredOffscreenBlts++; return; } // later uses of dst bring their own pixel records
    const bad = flags & ~BLT_SUPPORTED;
    if (bad) {
      const names = Object.entries(BLT_NAMES).filter(([bit]) => bad & bit).map(([, n]) => `DDBLT_${n}`).join(', ');
      this.fail(`unsupported Blt flag(s) 0x${bad.toString(16)}${names ? ` (${names})` : ''}`);
    }
    const dstRect = c.args.lpDestRect ? rectOf(c.args.lpDestRect) : { x: 0, y: 0, w: dst.width || this.display?.w, h: dst.height || this.display?.h };
    if (flags & BLT.COLORFILL) {
      this.emit(cmd.fillRect(dstRect, (c.args.lpDDBltFx?.dwFillColor ?? 0) >>> 0));
      return;
    }
    if (!src) this.fail('Blt onto the screen without a source surface or colour fill');
    if (src.screen) {
      if (isPresent) { this.present(); return; }
      this.fail('unsupported: screen-to-screen Blt');
    }
    const srcRect = c.args.lpSrcRect ? rectOf(c.args.lpSrcRect) : { x: 0, y: 0, w: src.width, h: src.height };
    let key = null;
    if (flags & BLT.KEYSRC) {
      key = src.key;
      // the key was set before the window opened (late arming), so it is unknown: an opaque square would be wrong
      if (!key) { this.stats.skippedKeyedBlits++; return; }
    }
    if (flags & BLT.KEYSRCOVERRIDE) key = keyOf(c.args.lpDDBltFx?.ddckSrcColorkey);
    if (this.blit(src, srcRect, dstRect, key) && isPresent) this.present();
    else if (isPresent) this.present();
  }

  surf_BltFast(c) {
    const dst = this.surf(c.obj.id);
    const srcRef = c.args.lpDDSrcSurface;
    if (!(srcRef instanceof ObjRef)) return;
    const src = this.surf(srcRef.id);
    this.applyBlob(src, parseBlob(c.suffix));
    if (!dst.screen) { this.stats.ignoredOffscreenBlts++; return; }
    const trans = c.args.dwTrans >>> 0;
    if (trans & ~0x1 & ~0x10 & ~0x20) this.fail(`unsupported BltFast flags 0x${trans.toString(16)}`);
    if (src.screen) this.fail('unsupported: screen-to-screen BltFast');
    const srcRect = c.args.lpSrcRect ? rectOf(c.args.lpSrcRect) : { x: 0, y: 0, w: src.width, h: src.height };
    this.blit(src, srcRect, { x: c.args.dwX, y: c.args.dwY, w: srcRect.w, h: srcRect.h }, trans & 0x1 ? src.key : null);
  }

  blit(src, srcRect, dstRect, key) {
    if (!this.flush(src, true)) { this.stats.skippedBlits++; return false; }
    this.emit(cmd.blit2D(src.id, srcRect, dstRect, key));
    return true;
  }

  // ---- Direct3D device ---------------------------------------------------------------------------------------------
  shadow(id) {
    let s = this.shadows.get(id);
    if (!s) { s = new Map(); this.shadows.set(id, s); }
    return s;
  }

  /** Make `id` the device whose state the renderer holds: reset it and replay that device's shadow. */
  activate(id) {
    if (this.active === id) return;
    if (this.active !== null) { this.emit(cmd.resetDeviceState()); this.stats.deviceSwitches++; }
    this.active = id;
    for (const sc of this.shadow(id).values()) {
      if (sc.op === 'bindTexture' && sc.textureId !== null) this.flush(this.surf(sc.textureId), false);
      this.emit({ ...sc });
    }
  }

  setState(c, key, command) {
    const id = c.obj.id;
    this.shadow(id).set(key, command);
    if (this.active === id) this.emit({ ...command });
    else this.activate(id);
  }

  dev7_SetRenderState(c) { this.setState(c, `rs:${c.args.dwRenderStateType}`, cmd.setRenderState(c.args.dwRenderStateType, c.args.dwRenderState)); }
  dev7_SetTextureStageState(c) {
    const { dwStage: st, d3dTexStageStateType: id, dwState: v } = c.args;
    this.setState(c, `tss:${st}:${id}`, cmd.setTextureStageState(st, id, v));
  }
  dev7_SetTransform(c) { this.setState(c, `tf:${c.args.state}`, cmd.setTransform(c.args.state, c.args.matrix['f32[16]'])); }
  dev7_SetViewport(c) {
    const d = c.args.data;
    this.setState(c, 'viewport', cmd.setViewport({ x: d.dwX, y: d.dwY, width: d.dwWidth, height: d.dwHeight, minZ: d.dvMinZ, maxZ: d.dvMaxZ }));
  }
  dev7_SetMaterial(c) {
    const d = c.args.data;
    this.setState(c, 'material', cmd.setMaterial({ diffuse: d.diffuse, ambient: d.ambient, specular: d.specular, emissive: d.emissive, power: d.power }));
  }
  dev7_SetLight(c) {
    const d = c.args.data;
    if (d.dltType !== 3 && !/DIRECTIONAL/.test(String(d.dltType))) this.fail(`unsupported light type ${d.dltType} (only directional lights)`);
    this.setState(c, `light:${c.args.idx}`, cmd.setLight(c.args.idx, { type: 'directional', diffuse: d.diffuse, ambient: d.ambient, direction: d.direction }));
  }
  dev7_LightEnable(c) {
    const on = c.args.bEnable !== 0 && c.args.bEnable !== 'FALSE';
    this.setState(c, `lightEnable:${c.args.dwLightIndex}`, cmd.lightEnable(c.args.dwLightIndex, on));
  }
  dev7_SetTexture(c) {
    const ref = c.args.surface;
    const stage = c.args.stage;
    const s = ref instanceof ObjRef ? this.surf(ref.id) : null;
    if (s) this.applyBlob(s, parseBlob(c.suffix));
    this.setState(c, `tex:${stage}`, cmd.bindTexture(stage, s ? s.id : null));
  }

  dev7_Clear(c) {
    this.activate(c.obj.id);
    const rects = Array.isArray(c.args.rects) && c.args.count > 0 ? c.args.rects : [null];
    for (const r of rects) this.emit(cmd.clear(c.args.flags, c.args.color, c.args.z, r ? rectOf(r) : null));
  }

  drawCommon(c, indexed, fromVb) {
    const id = c.obj.id;
    this.activate(id);
    const prim = c.args.primitive_type;
    if (prim !== 4 && prim !== 2) this.fail(`unsupported primitive type ${prim} (only TRIANGLELIST and LINELIST)`);
    const blob = parseBlob(c.suffix);
    if (!blob || !blob.vertices) {
      if (this.opts.allowMissingData && !this.data) { this.stats.skippedDraws++; return; }
      this.fail('draw call without vertex data (the trace was captured without DDT_DATA)');
    }
    if (blob.state) this.fail(`draw blob=${blob.state} (the capture could not store the vertices)`);
    if (blob.vertices === 'none') this.fail('draw with vertices=none: the capture had no vertex data for this range');
    if (indexed && (!blob.indices || blob.indices === 'none')) this.fail('indexed draw without index data');
    if (!this.data) { this.stats.skippedDraws++; return; }

    const vr = this.data.record(blob.vertices.offset, blob.vertices.length);
    if (vr.kind !== KIND.VERTICES) this.fail('vertices blob does not point at a vertex record');
    const [fvf, stride, , count] = vr.params;
    if (fvf !== VERTEX_FVF || stride !== 36) this.fail(`unsupported vertex format FVF 0x${fvf.toString(16)} stride ${stride} (only 0x152, 36 bytes)`);
    if (vr.payload.length !== count * 36) this.fail('vertex record length does not match its vertex count');
    let slot = this.slots.get(blob.vertices.offset);
    if (slot === undefined) {
      slot = this.nextVertex;
      this.slots.set(blob.vertices.offset, slot);
      this.nextVertex += count;
      this.stats.uniqueVertexRecords++;
      this.emit(cmd.uploadVertices(vr.payload, slot * 36));
    }
    let indices;
    if (indexed) {
      const ir = this.data.record(blob.indices.offset, blob.indices.length);
      if (ir.kind !== KIND.INDICES) this.fail('indices blob does not point at an index record');
      if (ir.payload.length !== ir.params[0] * 2) this.fail('index record length does not match its index count');
      indices = new Uint16Array(ir.params[0]);
      new Uint8Array(indices.buffer).set(ir.payload);
    } else {
      indices = new Uint16Array(count);
      for (let i = 0; i < count; i++) indices[i] = i;
    }
    const bound = this.shadow(id);
    for (const st of [0, 1]) {
      const b = bound.get(`tex:${st}`);
      if (b && b.textureId !== null) this.flush(this.surf(b.textureId), true);
    }
    this.emit(cmd.draw({ primitive: prim === 4 ? 'triangleList' : 'lineList', startVertex: slot, vertexCount: count, indices }));
  }

  dev7_DrawIndexedPrimitiveVB(c) { this.drawCommon(c, true, true); }
  dev7_DrawPrimitiveVB(c) { this.drawCommon(c, false, true); }
  dev7_DrawIndexedPrimitive(c) { this.drawCommon(c, true, false); }
  dev7_DrawPrimitive(c) { this.drawCommon(c, false, false); }
}

/**
 * @param {string} traceText the text trace
 * @param {Uint8Array|null} dataBytes the `.data` side file
 * @param {{allowMissingData?: boolean}} [opts] allowMissingData: parse a trace captured without data (draws and pixel
 *   updates are skipped and counted instead of being an error); for inspection and tests only
 * @returns {{sequence: object, stats: object}}
 */
export function loadTrace(traceText, dataBytes, opts = {}) {
  return new Loader(traceText, dataBytes, opts).run();
}

async function gunzip(bytes) {
  const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'));
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

/**
 * Load from the files the user picked: one trace (`.trace`, `.txt`, optionally `.gz`) and its `.data` file (optionally
 * `.gz`). Names decide which is which.
 * @param {{name: string, bytes: Uint8Array}[]} files
 */
export async function loadFromFiles(files) {
  let trace = null, data = null;
  for (const f of files) {
    const gz = /\.gz$/i.test(f.name);
    const base = f.name.replace(/\.gz$/i, '');
    const bytes = gz || (f.bytes[0] === 0x1f && f.bytes[1] === 0x8b) ? await gunzip(f.bytes) : f.bytes;
    if (/\.data$/i.test(base)) data = bytes; else trace = { name: f.name, bytes };
  }
  if (!trace) throw new TraceError('no trace file selected (expected a .trace or .trace.gz file)');
  const text = new TextDecoder().decode(trace.bytes);
  return loadTrace(text, data);
}
