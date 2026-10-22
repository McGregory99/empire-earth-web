// Flat shading (SHADEMODE) and wireframe / point fill (FILLMODE): the index rewrites and the back-face rule the renderer applies, plus the state side.
import test from 'node:test';
import assert from 'node:assert/strict';
import { flatIndices, wireframeIndices, pointIndices, cullIndices, projectToNdc } from '../src/fill-modes.js';
import { DeviceState } from '../src/state.js';
import { cmd } from '../src/frame-model.js';
import { RS, classifyRenderState, UnsupportedError } from '../src/d3d-tables.js';

const arr = (a) => Array.from(a);

test('flat shading: D3D takes the first vertex of a primitive, GL the last, so each primitive is rotated to end with its first vertex', () => {
  assert.deepEqual(arr(flatIndices('triangleList', Uint16Array.of(0, 1, 2, 3, 4, 5))), [1, 2, 0, 4, 5, 3]);
  assert.deepEqual(arr(flatIndices('lineList', Uint16Array.of(0, 1, 2, 3))), [1, 0, 3, 2]);
});

test('flat shading keeps the winding of every triangle (a cyclic rotation)', () => {
  const area = (p, [a, b, c]) => (p[b][0] - p[a][0]) * (p[c][1] - p[a][1]) - (p[c][0] - p[a][0]) * (p[b][1] - p[a][1]);
  const p = [[0, 0], [4, 0], [0, 3]];
  assert.equal(Math.sign(area(p, [0, 1, 2])), Math.sign(area(p, arr(flatIndices('triangleList', Uint16Array.of(0, 1, 2))))));
});

test('wireframe: each triangle becomes its three edges, an edge shared by two triangles once', () => {
  assert.deepEqual(arr(wireframeIndices(Uint16Array.of(0, 1, 2))), [0, 1, 1, 2, 2, 0]);
  assert.deepEqual(arr(wireframeIndices(Uint16Array.of(0, 1, 2, 2, 1, 3))), [0, 1, 1, 2, 2, 0, 1, 3, 3, 2]);
});

test('point fill: every vertex of the triangles once, in first-seen order', () => {
  assert.deepEqual(arr(pointIndices(Uint16Array.of(0, 1, 2, 2, 1, 3))), [0, 1, 2, 3]);
});

test('culling by screen winding: D3D front faces are clockwise, CULL_CW removes clockwise and CULL_CCW counter-clockwise triangles', () => {
  // NDC, y up: (0,0) -> (1,0) -> (0,1) is counter-clockwise on screen; the reverse order is clockwise
  const ndc = [[0, 0], [1, 0], [0, 1], [0, 0]];
  const ccw = Uint16Array.of(0, 1, 2);
  const cw = Uint16Array.of(0, 2, 1);
  assert.deepEqual(arr(cullIndices(ccw, (i) => ndc[i], 'ccw')), []);
  assert.deepEqual(arr(cullIndices(ccw, (i) => ndc[i], 'cw')), [0, 1, 2]);
  assert.deepEqual(arr(cullIndices(cw, (i) => ndc[i], 'cw')), []);
  assert.deepEqual(arr(cullIndices(cw, (i) => ndc[i], 'ccw')), [0, 2, 1]);
  assert.deepEqual(arr(cullIndices(ccw, (i) => ndc[i], 'none')), [0, 1, 2]);
  assert.deepEqual(arr(cullIndices(Uint16Array.of(0, 1, 3), (i) => ndc[i], 'cw')), [0, 1, 3], 'a degenerate triangle has no facing: kept');
  assert.deepEqual(arr(cullIndices(ccw, (i) => (i === 1 ? null : ndc[i]), 'ccw')), [0, 1, 2], 'a vertex behind the eye: no decision, kept');
});

test('projectToNdc multiplies by world, view and projection (row vectors) and divides by w', () => {
  const I = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
  const move = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 2, 0, 0, 1]; // translate x by 2
  const proj = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0]; // w = z
  assert.deepEqual(projectToNdc([1, 4, 2], I, move, proj), [1.5, 2]);
  assert.equal(projectToNdc([1, 4, 0], I, move, proj), null, 'w <= 0');
});

test('state: FILLMODE 1..3 and SHADEMODE 1..3 are accepted, others are not; the draw config says how to fill and shade', () => {
  for (const v of [1, 2, 3]) { classifyRenderState(RS.FILLMODE, v); classifyRenderState(RS.SHADEMODE, v); }
  assert.throws(() => classifyRenderState(RS.FILLMODE, 4), UnsupportedError);
  assert.throws(() => classifyRenderState(RS.SHADEMODE, 0), UnsupportedError);
  const s = new DeviceState();
  assert.deepEqual([s.drawConfig().fill, s.drawConfig().flat], ['solid', false]);
  s.apply(cmd.setRenderState(RS.FILLMODE, 2));
  s.apply(cmd.setRenderState(RS.SHADEMODE, 1));
  assert.deepEqual([s.drawConfig().fill, s.drawConfig().flat], ['wireframe', true]);
  s.apply(cmd.setRenderState(RS.FILLMODE, 1));
  s.apply(cmd.setRenderState(RS.SHADEMODE, 3));
  assert.deepEqual([s.drawConfig().fill, s.drawConfig().flat], ['point', false], 'PHONG shades like Gouraud in D3D7');
});
