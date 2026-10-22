// Stencil buffer: STENCILENABLE / FUNC / REF / MASK / WRITEMASK / FAIL / ZFAIL / PASS and the stencil part of Clear.
import test from 'node:test';
import assert from 'node:assert/strict';
import { stencilOp, classifyRenderState, RS, GL, UnsupportedError } from '../src/d3d-tables.js';
import { DeviceState } from '../src/state.js';
import { cmd, CLEAR } from '../src/frame-model.js';
import { loadTrace } from '../src/trace-loader.js';

test('D3DSTENCILOP maps to GL: INCRSAT / DECRSAT saturate (INCR / DECR), INCR / DECR wrap', () => {
  assert.equal(stencilOp(1), GL.KEEP);
  assert.equal(stencilOp(2), GL.ZERO);
  assert.equal(stencilOp(3), GL.REPLACE);
  assert.equal(stencilOp(4), GL.INCR);
  assert.equal(stencilOp(5), GL.DECR);
  assert.equal(stencilOp(6), GL.INVERT);
  assert.equal(stencilOp(7), GL.INCR_WRAP);
  assert.equal(stencilOp(8), GL.DECR_WRAP);
  assert.throws(() => stencilOp(0), UnsupportedError);
  assert.throws(() => stencilOp(9), UnsupportedError);
});

test('the stencil render states are accepted with valid values', () => {
  classifyRenderState(RS.STENCILENABLE, 1);
  for (const id of [RS.STENCILFAIL, RS.STENCILZFAIL, RS.STENCILPASS]) { classifyRenderState(id, 3); assert.throws(() => classifyRenderState(id, 9), UnsupportedError); }
  classifyRenderState(RS.STENCILFUNC, 5);
  assert.throws(() => classifyRenderState(RS.STENCILFUNC, 0), UnsupportedError);
  for (const id of [RS.STENCILREF, RS.STENCILMASK, RS.STENCILWRITEMASK]) classifyRenderState(id, 0xffffffff);
});

test('draw config: stencil defaults are D3D7 (off, ALWAYS, KEEP, ref 0, masks all ones)', () => {
  const st = new DeviceState().drawConfig().stencil;
  assert.deepEqual(st, { enable: false, func: GL.ALWAYS, ref: 0, mask: 0xff, writeMask: 0xff, fail: GL.KEEP, zfail: GL.KEEP, pass: GL.KEEP });
});

test('draw config: the states resolve to GL values, reference and masks truncated to the 8 bit buffer', () => {
  const s = new DeviceState();
  for (const [id, v] of [[RS.STENCILENABLE, 1], [RS.STENCILFUNC, 3], [RS.STENCILREF, 0x1a5], [RS.STENCILMASK, 0xf0f0], [RS.STENCILWRITEMASK, 0x0f],
    [RS.STENCILFAIL, 2], [RS.STENCILZFAIL, 6], [RS.STENCILPASS, 4]]) s.apply(cmd.setRenderState(id, v));
  assert.deepEqual(s.drawConfig().stencil, { enable: true, func: GL.EQUAL, ref: 0xa5, mask: 0xf0, writeMask: 0x0f, fail: GL.ZERO, zfail: GL.INVERT, pass: GL.INCR });
});

test('clear carries a stencil value (default 0); the loader passes the Clear call\'s stencil argument', () => {
  assert.equal(cmd.clear(CLEAR.STENCIL, 0, 1).stencil, 0);
  assert.equal(cmd.clear(CLEAR.STENCIL, 0, 1, null, 7).stencil, 7);
  const text = ['# ddraw_trace v1', 'T1 dev7#1.Clear(count=0, rects=NULL, flags=0x5, color=0xff000000, z=f32:3f800000, stencil=3) = DD_OK',
    'T1 surf#5.Flip(lpDDSurfaceTargetOverride=NULL, dwFlags=0x28) = DD_OK'].join('\n') + '\n';
  const { sequence } = loadTrace(text, null, { allowMissingData: true });
  const c = sequence.frames[0].commands.find((x) => x.op === 'clear');
  assert.deepEqual([c.flags, c.stencil], [5, 3]);
});
