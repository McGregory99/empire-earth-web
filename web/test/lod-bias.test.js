// MIPMAPLODBIAS: the game sets -1.0 on its terrain and unit textures. The reference captures (the game under Wine on macOS OpenGL) show
// it is NOT applied: skirmish-7, whole frame mean abs difference 2.77 with the bias, 0.92 without it (terrain 2.9 -> 0.33). The renderer
// therefore ignores it by default and can be asked to apply it (D3D7 semantics) with { applyLodBias: true }.
import test from 'node:test';
import assert from 'node:assert/strict';
import { DeviceState } from '../src/state.js';
import { Renderer } from '../src/renderer.js';
import { cmd } from '../src/frame-model.js';
import { TSS } from '../src/d3d-tables.js';

const NEG1 = 0xbf800000;
const bias = (s) => { s.apply(cmd.setTextureStageState(0, TSS.MIPMAPLODBIAS, NEG1)); return s.drawConfig().lodBias; };

test('DeviceState: the stage bias is applied by default (D3D7 semantics) and can be switched off', () => {
  assert.equal(bias(new DeviceState()), -1);
  assert.equal(bias(new DeviceState({ applyLodBias: false })), 0);
});

// a WebGL2 context that accepts everything: enough to build a Renderer and look at its device state
const glStub = () => new Proxy({}, { get: (_t, k) => (k === 'getShaderParameter' || k === 'getProgramParameter' ? () => true : typeof k === 'string' && /^[A-Z_0-9]+$/.test(k) ? 1 : () => ({})) });
const canvasStub = () => ({ width: 8, height: 8, getContext: () => glStub() });

test('Renderer: ignores MIPMAPLODBIAS by default, applies it with applyLodBias', () => {
  assert.equal(bias(new Renderer(canvasStub()).state), 0);
  assert.equal(bias(new Renderer(canvasStub(), { applyLodBias: true }).state), -1);
  const r = new Renderer(canvasStub());
  r.reset();
  assert.equal(bias(r.state), 0, 'still ignored after a reset (backwards seek)');
});
