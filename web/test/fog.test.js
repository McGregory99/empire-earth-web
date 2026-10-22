// Fog (D3D7): vertex fog (FOGVERTEXMODE, optionally range based), table / pixel fog (FOGTABLEMODE), FOGSTART / FOGEND / FOGDENSITY.
import test from 'node:test';
import assert from 'node:assert/strict';
import { fogFactor, classifyRenderState, RS, UnsupportedError } from '../src/d3d-tables.js';
import { DeviceState } from '../src/state.js';
import { cmd } from '../src/frame-model.js';

const bits = (f) => new Uint32Array(new Float32Array([f]).buffer)[0];
const near = (a, b, eps = 1e-6) => assert.ok(Math.abs(a - b) <= eps, `${a} != ${b}`);

test('fog factor: linear (end - d) / (end - start), clamped to 0..1', () => {
  near(fogFactor(3, 15, 10, 20, 1), 0.5);
  near(fogFactor(3, 5, 10, 20, 1), 1, 0);
  near(fogFactor(3, 25, 10, 20, 1), 0, 0);
  near(fogFactor(3, 12.5, 10, 20, 1), 0.75);
});

test('fog factor: exp is 1 / e^(d * density), exp2 is 1 / e^((d * density)^2)', () => {
  near(fogFactor(1, 10, 0, 1, 0.1), Math.exp(-1));
  near(fogFactor(2, 10, 0, 1, 0.1), Math.exp(-1));
  near(fogFactor(2, 20, 0, 1, 0.1), Math.exp(-4));
  near(fogFactor(1, 0, 0, 1, 5), 1);
});

test('fog factor: mode 0 is no fog; a degenerate linear range (start == end) is a step at that distance', () => {
  near(fogFactor(0, 100, 0, 1, 1), 1, 0);
  near(fogFactor(3, 4, 5, 5, 1), 1, 0);
  near(fogFactor(3, 6, 5, 5, 1), 0, 0);
});

test('FOGTABLEMODE 1..3 and RANGEFOGENABLE are accepted, other table modes are not', () => {
  for (const m of [0, 1, 2, 3]) assert.doesNotThrow(() => classifyRenderState(RS.FOGTABLEMODE, m));
  assert.throws(() => classifyRenderState(RS.FOGTABLEMODE, 4), UnsupportedError);
  for (const v of [0, 1]) assert.doesNotThrow(() => classifyRenderState(RS.RANGEFOGENABLE, v));
  assert.throws(() => classifyRenderState(RS.RANGEFOGENABLE, 2), UnsupportedError);
});

const fogState = (rs) => {
  const s = new DeviceState();
  for (const [id, v] of rs) s.apply(cmd.setRenderState(id, v));
  return s.drawConfig().fog;
};

test('draw config: vertex fog uses FOGVERTEXMODE, table fog (FOGTABLEMODE) wins when both are set, FOGENABLE 0 turns both off', () => {
  const vertex = fogState([[RS.FOGENABLE, 1], [RS.FOGVERTEXMODE, 1]]);
  assert.equal(vertex.mode, 1);
  assert.equal(vertex.table, false);
  const table = fogState([[RS.FOGENABLE, 1], [RS.FOGVERTEXMODE, 1], [RS.FOGTABLEMODE, 3]]);
  assert.equal(table.mode, 3);
  assert.equal(table.table, true);
  const off = fogState([[RS.FOGENABLE, 0], [RS.FOGVERTEXMODE, 1], [RS.FOGTABLEMODE, 3]]);
  assert.equal(off.mode, 0);
  assert.equal(off.table, false);
});

test('draw config: range based fog applies to vertex fog only; start, end and density are floats from their bits', () => {
  const r = fogState([[RS.FOGENABLE, 1], [RS.FOGVERTEXMODE, 3], [RS.RANGEFOGENABLE, 1], [RS.FOGSTART, bits(2)], [RS.FOGEND, bits(8)], [RS.FOGDENSITY, bits(0.25)]]);
  assert.equal(r.range, true);
  assert.equal(r.start, 2);
  assert.equal(r.end, 8);
  assert.equal(r.density, 0.25);
  const t = fogState([[RS.FOGENABLE, 1], [RS.FOGTABLEMODE, 2], [RS.RANGEFOGENABLE, 1]]);
  assert.equal(t.range, false, 'D3D7 applies range fog to vertex fog only');
  assert.equal(fogState([[RS.FOGENABLE, 1], [RS.FOGVERTEXMODE, 3]]).range, false);
});
