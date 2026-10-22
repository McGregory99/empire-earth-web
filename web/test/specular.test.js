// Specular highlights (SPECULARENABLE, material specular + power, light specular, LOCALVIEWER, the vertex specular colour and the
// material colour sources including COLOR2).
import test from 'node:test';
import assert from 'node:assert/strict';
import { cmd } from '../src/frame-model.js';
import { DeviceState } from '../src/state.js';
import { RS, TSS, TARG, UnsupportedError, classifyRenderState, classifyTextureStageState } from '../src/d3d-tables.js';

test('specular render states are accepted: SPECULARENABLE, LOCALVIEWER, material sources 0..2', () => {
  for (const id of [RS.SPECULARENABLE, RS.LOCALVIEWER]) for (const v of [0, 1]) assert.doesNotThrow(() => classifyRenderState(id, v));
  for (const id of [RS.DIFFUSEMATERIALSOURCE, RS.AMBIENTMATERIALSOURCE, RS.SPECULARMATERIALSOURCE, RS.EMISSIVEMATERIALSOURCE]) {
    for (const v of [0, 1, 2]) assert.doesNotThrow(() => classifyRenderState(id, v));
    assert.throws(() => classifyRenderState(id, 3), UnsupportedError);
  }
  assert.throws(() => classifyRenderState(RS.SPECULARENABLE, 2), UnsupportedError);
});

test('the SPECULAR texture argument is accepted, the argument modifiers still are not', () => {
  assert.doesNotThrow(() => classifyTextureStageState(0, TSS.COLORARG1, TARG.SPECULAR));
  assert.throws(() => classifyTextureStageState(0, TSS.COLORARG1, TARG.SPECULAR | 0x10), UnsupportedError);
});

const lighting = (rs = []) => {
  const s = new DeviceState();
  for (const [id, v] of rs) s.apply(cmd.setRenderState(id, v));
  return s.drawConfig().lighting;
};

test('D3D7 defaults: specular off, local viewer on, colour sources DIFFUSE=COLOR1 AMBIENT=material SPECULAR=COLOR2 EMISSIVE=material', () => {
  const l = lighting();
  assert.equal(l.specularEnable, false);
  assert.equal(l.localViewer, true);
  assert.deepEqual([l.diffuseSource, l.ambientSource, l.specularSource, l.emissiveSource], [1, 0, 2, 0]);
});

test('COLORVERTEX 0 makes every colour source the material; COLOR2 (2) selects the vertex specular colour', () => {
  const off = lighting([[RS.COLORVERTEX, 0], [RS.DIFFUSEMATERIALSOURCE, 2]]);
  assert.deepEqual([off.diffuseSource, off.ambientSource, off.specularSource, off.emissiveSource], [0, 0, 0, 0]);
  const on = lighting([[RS.SPECULARENABLE, 1], [RS.LOCALVIEWER, 0], [RS.DIFFUSEMATERIALSOURCE, 2], [RS.SPECULARMATERIALSOURCE, 0]]);
  assert.equal(on.specularEnable, true);
  assert.equal(on.localViewer, false);
  assert.equal(on.diffuseSource, 2);
  assert.equal(on.specularSource, 0);
  assert.equal(on.diffuseFromVertex, false, 'the boolean of the vertex DIFFUSE colour (COLOR1) stays for COLOR1 only');
});

test('lights carry a specular colour (default black), validated like the others', () => {
  const base = { type: 'directional', diffuse: [1, 1, 1, 1], ambient: [0, 0, 0, 0], direction: [0, 0, 1] };
  assert.deepEqual(cmd.setLight(0, { ...base, specular: [1, 0.5, 0.25, 1] }).light.specular, [1, 0.5, 0.25, 1]);
  assert.throws(() => cmd.setLight(0, { ...base, specular: [1, 1, 1] }), /specular/);
  const s = new DeviceState();
  s.apply(cmd.setLight(0, base));
  s.apply(cmd.lightEnable(0, true));
  assert.deepEqual(s.drawConfig().lighting.lights[0].specular ?? [0, 0, 0, 0], [0, 0, 0, 0]);
});
