import test from 'node:test';
import assert from 'node:assert/strict';
import { DeviceState } from '../src/state.js';
import { cmd } from '../src/frame-model.js';
import { RS, TSS, TOP, TARG, GL, UnsupportedError } from '../src/d3d-tables.js';

const IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
const run = (s, ...cmds) => cmds.forEach(c => s.apply(c));

test('D3D7 defaults', () => {
  const c = new DeviceState().drawConfig();
  assert.deepEqual(c.depth, { enable: true, write: true, func: GL.LEQUAL });
  assert.deepEqual(c.cull, { enable: true, face: GL.BACK }); // D3DCULL_CCW
  assert.equal(c.blend.enable, false);
  assert.equal(c.alphaTest.enable, false);
  assert.equal(c.fog.mode, 0);
  assert.equal(c.lighting.enable, true);
  assert.equal(c.stage0.colorOp, TOP.MODULATE);
  assert.equal(c.stage0.colorArg1, TARG.TEXTURE);
  assert.equal(c.stage0.colorArg2, TARG.CURRENT);
  assert.equal(c.stage0.alphaOp, TOP.SELECTARG1);
});

test('unsupported render state throws at apply time and is not stored', () => {
  const s = new DeviceState();
  assert.throws(() => s.apply(cmd.setRenderState(RS.ZBIAS, 1)), UnsupportedError);
  assert.equal(s.renderState(RS.ZBIAS), undefined);
});

test('golden-trace style state: alpha blend SRCALPHA/INVSRCALPHA, alpha test GREATER 0, no depth', () => {
  const s = new DeviceState();
  run(s,
    cmd.setRenderState(RS.ALPHABLENDENABLE, 1), cmd.setRenderState(RS.SRCBLEND, 5), cmd.setRenderState(RS.DESTBLEND, 6),
    cmd.setRenderState(RS.ALPHATESTENABLE, 1), cmd.setRenderState(RS.ALPHAFUNC, 5), cmd.setRenderState(RS.ALPHAREF, 0x80),
    cmd.setRenderState(RS.ZENABLE, 0), cmd.setRenderState(RS.ZWRITEENABLE, 0), cmd.setRenderState(RS.CULLMODE, 1));
  const c = s.drawConfig();
  assert.deepEqual(c.blend, { enable: true, src: GL.SRC_ALPHA, dst: GL.ONE_MINUS_SRC_ALPHA });
  assert.deepEqual(c.alphaTest, { enable: true, func: 5, ref: 0x80 / 255 });
  assert.equal(c.depth.enable, false);
  assert.equal(c.depth.write, false);
  assert.equal(c.cull.enable, false);
});

test('blend (ONE, ZERO) is the same as blending off', () => {
  const s = new DeviceState();
  run(s, cmd.setRenderState(RS.ALPHABLENDENABLE, 1), cmd.setRenderState(RS.SRCBLEND, 2), cmd.setRenderState(RS.DESTBLEND, 1));
  assert.equal(s.drawConfig().blend.enable, false);
});

test('linear vertex fog decodes the float states from their bits', () => {
  const s = new DeviceState();
  const bits = (f) => new Uint32Array(new Float32Array([f]).buffer)[0];
  run(s, cmd.setRenderState(RS.FOGENABLE, 1), cmd.setRenderState(RS.FOGVERTEXMODE, 3),
    cmd.setRenderState(RS.FOGSTART, bits(10)), cmd.setRenderState(RS.FOGEND, bits(30)), cmd.setRenderState(RS.FOGCOLOR, 0xff808080));
  const f = s.drawConfig().fog;
  assert.equal(f.mode, 3);
  assert.equal(f.start, 10);
  assert.equal(f.end, 30);
  assert.deepEqual(f.color.map(v => Math.round(v * 255)), [128, 128, 128, 255]);
  run(s, cmd.setRenderState(RS.FOGENABLE, 0));
  assert.equal(s.drawConfig().fog.mode, 0, 'FOGENABLE off disables fog whatever the mode');
});

test('transforms: world/view/projection stored; identity texture transform tolerated; others rejected', () => {
  const s = new DeviceState();
  const m = IDENTITY.slice(); m[12] = 5;
  run(s, cmd.setTransform(1, m));
  assert.equal(s.transform(1)[12], 5);
  assert.equal(s.transform(2)[0], 1);
  assert.doesNotThrow(() => s.apply(cmd.setTransform(16, IDENTITY)));
  assert.throws(() => s.apply(cmd.setTransform(16, m)), /texture transform/i);
  assert.throws(() => s.apply(cmd.setTransform(256, IDENTITY)), /world matrix 1|blend/i);
});

test('stage 1 is processed when enabled; stage 0 still validates its operations', () => {
  const s = new DeviceState();
  assert.equal(s.drawConfig().numStages, 1);
  run(s, cmd.setTextureStageState(1, TSS.COLOROP, TOP.MODULATE));
  assert.equal(s.drawConfig().numStages, 2);
  assert.throws(() => s.apply(cmd.setTextureStageState(0, TSS.COLOROP, 11)), UnsupportedError);
});

test('texture sampler state resolves from stage 0', () => {
  const s = new DeviceState();
  run(s, cmd.setTextureStageState(0, TSS.MAGFILTER, 2), cmd.setTextureStageState(0, TSS.MINFILTER, 2),
    cmd.setTextureStageState(0, TSS.MIPFILTER, 2), cmd.setTextureStageState(0, TSS.ADDRESS, 3),
    cmd.setTextureStageState(0, TSS.MIPMAPLODBIAS, 0xbf400000), cmd.bindTexture(0, 7));
  const c = s.drawConfig();
  assert.deepEqual(c.sampler, { mag: GL.LINEAR, min: GL.LINEAR_MIPMAP_NEAREST, wrapS: GL.CLAMP_TO_EDGE, wrapT: GL.CLAMP_TO_EDGE });
  assert.equal(c.lodBias, -0.75);
  assert.equal(c.texture0, 7);
  run(s, cmd.setTextureStageState(0, TSS.ADDRESSU, 1));
  assert.equal(s.drawConfig().sampler.wrapS, GL.REPEAT);
  assert.equal(s.drawConfig().sampler.wrapT, GL.CLAMP_TO_EDGE);
});

test('lighting: material sources honour COLORVERTEX, lights limited to four enabled', () => {
  const s = new DeviceState();
  run(s, cmd.setRenderState(RS.COLORVERTEX, 1), cmd.setRenderState(RS.DIFFUSEMATERIALSOURCE, 1),
    cmd.setRenderState(RS.AMBIENTMATERIALSOURCE, 1), cmd.setRenderState(RS.EMISSIVEMATERIALSOURCE, 0));
  let l = s.drawConfig().lighting;
  assert.deepEqual([l.diffuseFromVertex, l.ambientFromVertex, l.emissiveFromVertex], [true, true, false]);
  run(s, cmd.setRenderState(RS.COLORVERTEX, 0));
  l = s.drawConfig().lighting;
  assert.deepEqual([l.diffuseFromVertex, l.ambientFromVertex, l.emissiveFromVertex], [false, false, false]);

  const light = { type: 'directional', diffuse: [1, 0, 0, 1], ambient: [0.1, 0.1, 0.1, 1], direction: [0, 0, 1] };
  for (let i = 0; i < 5; i++) run(s, cmd.setLight(i, light));
  for (let i = 0; i < 4; i++) run(s, cmd.lightEnable(i, true));
  assert.equal(s.drawConfig().lighting.lights.length, 4);
  run(s, cmd.lightEnable(4, true));
  assert.throws(() => s.drawConfig(), /at most 4 lights/);
});

test('enabling a light that was never set is an error', () => {
  const s = new DeviceState();
  run(s, cmd.lightEnable(0, true));
  assert.throws(() => s.drawConfig(), /light 0/);
});

test('viewport and material are kept', () => {
  const s = new DeviceState();
  run(s, cmd.setViewport({ x: 1, y: 2, width: 3, height: 4, minZ: 0, maxZ: 1 }),
    cmd.setMaterial({ diffuse: [1, 0, 0, 1], ambient: [0, 1, 0, 1], specular: [0, 0, 0, 0], emissive: [0, 0, 1, 0], power: 0 }));
  assert.deepEqual(s.viewport, { x: 1, y: 2, width: 3, height: 4, minZ: 0, maxZ: 1 });
  assert.deepEqual(s.drawConfig().lighting.material.emissive, [0, 0, 1, 0]);
});

test('apply returns false for commands that are not device state', () => {
  const s = new DeviceState();
  assert.equal(s.apply(cmd.present()), false);
  assert.equal(s.apply(cmd.setRenderState(RS.ZENABLE, 1)), true);
});

test('resetDeviceState restores the defaults and unbinds textures', () => {
  const s = new DeviceState();
  run(s, cmd.setRenderState(RS.CULLMODE, 1), cmd.bindTexture(0, 4), cmd.setTransform(1, [2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1]),
    cmd.setViewport({ x: 1, y: 1, width: 2, height: 2, minZ: 0, maxZ: 1 }));
  assert.equal(s.apply(cmd.resetDeviceState()), true);
  const c = s.drawConfig();
  assert.equal(c.cull.enable, true);
  assert.equal(c.texture0, null);
  assert.equal(s.transform(1)[0], 1);
  assert.equal(s.viewport, null);
});
