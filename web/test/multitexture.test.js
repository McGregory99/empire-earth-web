// Vertex formats beyond 0x152 (a second texture coordinate set, a specular colour) and texture stage 1 (multitexture).
import test from 'node:test';
import assert from 'node:assert/strict';
import { DataWriter } from './fixtures/build.mjs';
import { cmd, vertexLayout, packVertices, FrameModelError } from '../src/frame-model.js';
import { DeviceState } from '../src/state.js';
import { TSS, TOP, TARG, GL, UnsupportedError, classifyTextureStageState } from '../src/d3d-tables.js';
import { loadTrace } from '../src/trace-loader.js';

test('vertex layout: fixed order position, normal, diffuse, specular, texture sets', () => {
  assert.deepEqual(vertexLayout(0x152), { stride: 36, normal: 12, diffuse: 24, specular: -1, uv: [28], sets: 1 });
  assert.deepEqual(vertexLayout(0x252), { stride: 44, normal: 12, diffuse: 24, specular: -1, uv: [28, 36], sets: 2 });
  assert.deepEqual(vertexLayout(0x1d2), { stride: 40, normal: 12, diffuse: 24, specular: 28, uv: [32], sets: 1 });
  assert.deepEqual(vertexLayout(0x142), { stride: 24, normal: -1, diffuse: 12, specular: -1, uv: [16], sets: 1 });
});

test('vertex layout rejects what the renderer does not understand', () => {
  assert.throws(() => vertexLayout(0x004), FrameModelError); // XYZRHW (pre-transformed)
  assert.throws(() => vertexLayout(0x000), FrameModelError); // no position
  assert.throws(() => vertexLayout(0x352), FrameModelError); // three texture sets
  assert.throws(() => vertexLayout(0x10152), FrameModelError); // 3-component texture coordinates
});

test('packVertices writes each vertex in the layout of its format', () => {
  const b = packVertices(0x252, [{ pos: [1, 2, 3], normal: [0, 0, 1], diffuse: 0x80402010, uv: [0.25, 0.5], uv1: [0.75, 1] }]);
  assert.equal(b.length, 44);
  const dv = new DataView(b.buffer);
  assert.equal(dv.getFloat32(8, true), 3);
  assert.equal(dv.getUint32(24, true), 0x80402010);
  assert.equal(dv.getFloat32(36, true), 0.75);
  assert.equal(dv.getFloat32(40, true), 1);
});

test('uploadVertices defaults to 0x152 and checks length and offset against the layout of its format', () => {
  assert.equal(cmd.uploadVertices(new Uint8Array(72), 36).fvf, 0x152);
  const c = cmd.uploadVertices(new Uint8Array(88), 44, 0x252);
  assert.equal(c.fvf, 0x252);
  assert.throws(() => cmd.uploadVertices(new Uint8Array(72), 0, 0x252), FrameModelError);
  assert.throws(() => cmd.uploadVertices(new Uint8Array(44), 36, 0x252), FrameModelError);
});

test('the loader accepts every supported vertex format; each record keeps its format and gets its own vertex slots', () => {
  const d = new DataWriter();
  const v252 = packVertices(0x252, [0, 1, 2].map((i) => ({ pos: [i, 0, 0.5], uv: [0, 0], uv1: [i, i] })));
  const v152 = packVertices(0x152, [0, 1, 2].map((i) => ({ pos: [i, 0, 0.5], uv: [0, 0] })));
  const r252 = d.vertices(Buffer.from(v252), 3, 0, 0, 0x252, 44);
  const r152 = d.vertices(Buffer.from(v152), 3, 1, 0, 0x152, 36);
  const L = ['# ddraw_trace v1', '# data: x'];
  L.push(`T1 dev7#1.DrawPrimitive(primitive_type=TRIANGLELIST(4), fvf=0x252, vertices=P, vertex_count=3, flags=0x0) = DD_OK blob={vertices=${r252}}`);
  L.push(`T1 dev7#1.DrawPrimitive(primitive_type=TRIANGLELIST(4), fvf=0x152, vertices=P, vertex_count=3, flags=0x0) = DD_OK blob={vertices=${r152}}`);
  L.push('T1 surf#5.Flip(lpDDSurfaceTargetOverride=NULL, dwFlags=0x28) = DD_OK');
  const { sequence } = loadTrace(L.join('\n') + '\n', new Uint8Array(d.buffer()), { allowMissingData: false });
  const cmds = sequence.frames.flatMap((f) => f.commands);
  const ups = cmds.filter((c) => c.op === 'uploadVertices');
  assert.deepEqual(ups.map((c) => [c.fvf, c.offset, c.bytes.length]), [[0x252, 0, 132], [0x152, 3 * 36, 108]]);
  assert.deepEqual(cmds.filter((c) => c.op === 'draw').map((c) => c.startVertex), [0, 3]);
});

const run = (s, ...cmds) => cmds.forEach((c) => s.apply(c));

test('stage 1 is a real stage: a draw with it enabled resolves its operation, arguments, texture and sampler', () => {
  const s = new DeviceState();
  run(s, cmd.setTextureStageState(1, TSS.COLOROP, TOP.MODULATE2X), cmd.setTextureStageState(1, TSS.COLORARG1, TARG.TEXTURE),
    cmd.setTextureStageState(1, TSS.COLORARG2, TARG.CURRENT), cmd.setTextureStageState(1, TSS.ADDRESS, 3),
    cmd.setTextureStageState(1, TSS.MAGFILTER, 2), cmd.bindTexture(0, 4), cmd.bindTexture(1, 9));
  const c = s.drawConfig();
  assert.equal(c.numStages, 2);
  assert.deepEqual([c.stage1.colorOp, c.stage1.colorArg1, c.stage1.colorArg2], [TOP.MODULATE2X, TARG.TEXTURE, TARG.CURRENT]);
  assert.equal(c.texture1, 9);
  assert.equal(c.sampler1.mag, GL.LINEAR);
  assert.equal(c.sampler1.wrapS, GL.CLAMP_TO_EDGE);
  assert.equal(c.texCoordIndex[1], 1, 'D3D default: stage N reads texture coordinate set N');
  assert.equal(c.texCoordIndex[0], 0);
});

test('the stage cascade ends at the first disabled stage: stage 1 is not processed when stage 0 is disabled', () => {
  const s = new DeviceState();
  assert.equal(s.drawConfig().numStages, 1, 'stage 1 defaults to disabled');
  run(s, cmd.setTextureStageState(1, TSS.COLOROP, TOP.ADD));
  assert.equal(s.drawConfig().numStages, 2);
  run(s, cmd.setTextureStageState(0, TSS.COLOROP, TOP.DISABLE));
  assert.equal(s.drawConfig().numStages, 0);
});

test('TEXCOORDINDEX 0 and 1 select a texture coordinate set; coordinate generation flags are unsupported', () => {
  for (const st of [0, 1]) for (const v of [0, 1]) assert.doesNotThrow(() => classifyTextureStageState(st, TSS.TEXCOORDINDEX, v));
  assert.throws(() => classifyTextureStageState(0, TSS.TEXCOORDINDEX, 2), UnsupportedError);
  assert.throws(() => classifyTextureStageState(0, TSS.TEXCOORDINDEX, 0x20000), /generation|CAMERASPACE/i);
  const s = new DeviceState();
  run(s, cmd.setTextureStageState(0, TSS.TEXCOORDINDEX, 1));
  assert.equal(s.drawConfig().texCoordIndex[0], 1);
});
