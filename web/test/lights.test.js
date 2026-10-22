// Point and spot lights (D3D7 formulas: range cut-off, 1 / (a0 + a1 d + a2 d^2), the spot cone between theta and phi with falloff).
import test from 'node:test';
import assert from 'node:assert/strict';
import { cmd, FrameModelError } from '../src/frame-model.js';
import { attenuation, spotFactor, lightFactor } from '../src/lighting.js';
import { loadTrace } from '../src/trace-loader.js';
import { DeviceState } from '../src/state.js';

const near = (a, b, eps = 1e-6) => assert.ok(Math.abs(a - b) <= eps, `${a} != ${b}`);
const rad = (deg) => (deg * Math.PI) / 180;

test('attenuation is 1 / (a0 + a1 d + a2 d^2)', () => {
  near(attenuation([1, 0.5, 0.25], 2), 1 / 3);
  near(attenuation([1, 0, 0], 100), 1);
  near(attenuation([0, 1, 0], 4), 0.25);
});

test('spot factor: 1 inside theta, 0 outside phi, ((rho - cos(phi/2)) / (cos(theta/2) - cos(phi/2)))^falloff between', () => {
  const l = { theta: rad(40), phi: rad(80), falloff: 1 };
  near(spotFactor(l, 1), 1);
  near(spotFactor(l, Math.cos(rad(15))), 1, 0); // inside the inner cone (half angle 20)
  near(spotFactor(l, Math.cos(rad(50))), 0, 0); // outside the outer cone (half angle 40)
  const rho = Math.cos(rad(30));
  const t = (rho - Math.cos(rad(40))) / (Math.cos(rad(20)) - Math.cos(rad(40)));
  near(spotFactor(l, rho), t);
  near(spotFactor({ ...l, falloff: 2 }, rho), t * t);
  near(spotFactor({ ...l, theta: rad(80) }, Math.cos(rad(39))), 1, 0, 'theta == phi is a hard edge');
});

test('light factor: directional ignores position, point and spot use the vertex to light vector, range cuts off', () => {
  const n = [0, 0, -1];
  const dir = { type: 'directional', direction: [0, 0, 1] };
  near(lightFactor(dir, [5, 5, 5], n).diffuse, 1);
  const point = { type: 'point', position: [0, 0, -1.5], range: 10, attenuation: [1, 0.5, 0.25] };
  near(lightFactor(point, [0, 0, 0.5], n).diffuse, 1 / 3); // distance 2, N.L = 1
  near(lightFactor(point, [0, 0, 0.5], n).ambient, 1 / 3);
  near(lightFactor({ ...point, range: 1.5 }, [0, 0, 0.5], n).diffuse, 0);
  near(lightFactor({ ...point, attenuation: [1, 0, 0] }, [1.5, 0, -1.5], n).diffuse, 0, 1e-12); // light and vertex in a plane parallel to the surface: N.L = 0
  const spot = { type: 'spot', position: [0, 0, -0.1], direction: [0, 0, 1], range: 10, attenuation: [1, 0, 0], falloff: 1, theta: rad(40), phi: rad(80) };
  near(lightFactor(spot, [0, 0, 0.5], n).diffuse, 1);
  near(lightFactor(spot, [0.6, 0, 0.5], n).diffuse, 0); // 45 degrees off axis, outside the 40 degree outer half angle
});

test('setLight validates point and spot lights', () => {
  const common = { diffuse: [1, 1, 1, 1], ambient: [0, 0, 0, 0] };
  const point = { type: 'point', ...common, position: [0, 1, 2], range: 5, attenuation: [1, 0, 0] };
  assert.equal(cmd.setLight(0, point).light.type, 'point');
  const spot = { ...point, type: 'spot', direction: [0, 0, 1], falloff: 1, theta: 0.5, phi: 1 };
  assert.equal(cmd.setLight(1, spot).light.type, 'spot');
  assert.throws(() => cmd.setLight(0, { ...point, position: undefined }), FrameModelError);
  assert.throws(() => cmd.setLight(0, { ...point, attenuation: [1, 0] }), FrameModelError);
  assert.throws(() => cmd.setLight(0, { ...point, range: -1 }), FrameModelError);
  assert.throws(() => cmd.setLight(1, { ...spot, theta: undefined }), FrameModelError);
  assert.throws(() => cmd.setLight(0, { ...point, type: 'parallelpoint' }), /light type/);
});

const f32 = (v) => { const a = new Float32Array([v]); return `f32:${new Uint32Array(a.buffer)[0].toString(16).padStart(8, '0')}`; };
const col = (c) => `{${c.map(f32).join(', ')}}`;
const lightLine = (type, idx) => `T1 dev7#1.SetLight(idx=${idx}, data={dltType=${type}, diffuse=${col([1, 0.5, 0.25, 1])}, specular=${col([1, 1, 1, 1])}, ambient=${col([0, 0, 0, 0])}, ` +
  `position={${[1, 2, 3].map(f32).join(', ')}}, direction={${[0, 0, 1].map(f32).join(', ')}}, range=${f32(25)}, falloff=${f32(1)}, ` +
  `atten=[${[1, 0.5, 0.25].map(f32).join(', ')}], theta=${f32(0.5)}, phi=${f32(1)}}) = DD_OK`;

test('the loader turns POINT, SPOT (named or numbered) and DIRECTIONAL SetLight calls into lights', () => {
  const text = ['# ddraw_trace v1', lightLine('POINT', 0), lightLine(2, 1), lightLine('DIRECTIONAL', 2),
    'T1 surf#5.Flip(lpDDSurfaceTargetOverride=NULL, dwFlags=0x28) = DD_OK'].join('\n') + '\n';
  const { sequence } = loadTrace(text, null, { allowMissingData: true });
  const lights = sequence.frames[0].commands.filter((c) => c.op === 'setLight').map((c) => c.light);
  assert.deepEqual(lights.map((l) => l.type), ['point', 'spot', 'directional']);
  assert.deepEqual(lights[0].position, [1, 2, 3]);
  assert.deepEqual(lights[0].attenuation, [1, 0.5, 0.25]);
  assert.equal(lights[0].range, 25);
  assert.deepEqual([lights[1].theta, lights[1].phi, lights[1].falloff], [0.5, 1, 1]);
  assert.deepEqual(lights[1].direction, [0, 0, 1]);
  assert.deepEqual(lights[0].specular, [1, 1, 1, 1]);
});

test('an unknown light type in a trace fails loudly with the line', () => {
  const text = ['# ddraw_trace v1', lightLine('PARALLELPOINT', 0), 'T1 surf#5.Flip(lpDDSurfaceTargetOverride=NULL, dwFlags=0x28) = DD_OK'].join('\n') + '\n';
  assert.throws(() => loadTrace(text, null, { allowMissingData: true }), /light type.*trace line 2|trace line 2.*light type/);
});

test('the device state hands point and spot lights to the draw config', () => {
  const s = new DeviceState();
  s.apply(cmd.setLight(0, { type: 'point', diffuse: [1, 1, 1, 1], ambient: [0, 0, 0, 0], position: [0, 0, 0], range: 3, attenuation: [1, 0, 0] }));
  s.apply(cmd.lightEnable(0, true));
  assert.equal(s.drawConfig().lighting.lights[0].type, 'point');
});
