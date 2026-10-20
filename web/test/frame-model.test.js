import test from 'node:test';
import assert from 'node:assert/strict';
import { cmd, validateCommand, validateSequence, FrameModelError, VERTEX_STRIDE, FVF } from '../src/frame-model.js';

test('constants honour the golden: FVF 0x152, 36-byte vertices', () => {
  assert.equal(FVF, 0x152);
  assert.equal(VERTEX_STRIDE, 36);
});

test('constructors produce plain tagged objects that validate', () => {
  const all = [
    cmd.setDisplayMode(1024, 768),
    cmd.setRenderState(7, 1),
    cmd.setTextureStageState(0, 1, 4),
    cmd.setTransform(2, new Float32Array(16)),
    cmd.setViewport({ x: 0, y: 0, width: 1024, height: 768, minZ: 0, maxZ: 1 }),
    cmd.setMaterial({ diffuse: [1, 1, 1, 1], ambient: [1, 1, 1, 1], specular: [0, 0, 0, 0], emissive: [0, 0, 0, 0], power: 0 }),
    cmd.setLight(0, { type: 'directional', diffuse: [1, 1, 1, 1], ambient: [0, 0, 0, 0], direction: [0, 0, 1] }),
    cmd.lightEnable(0, true),
    cmd.bindTexture(0, 5),
    cmd.bindTexture(1, null),
    cmd.createTexture(5, 2, 2, 'RGB565', new Uint8Array(8)),
    cmd.updateTexture(5, new Uint8Array(8)),
    cmd.setColorKey(5, { low: 0xff, high: 0xff }),
    cmd.destroyTexture(5),
    cmd.uploadVertices(new Uint8Array(72)),
    cmd.draw({ primitive: 'triangleList', startVertex: 0, vertexCount: 3, indices: Uint16Array.of(0, 1, 2) }),
    cmd.clear(3, 0xff000000, 1),
    cmd.fillRect({ x: 0, y: 0, w: 4, h: 4 }, 0xff00ff00),
    cmd.blit2D(5, { x: 0, y: 0, w: 2, h: 2 }, { x: 10, y: 10, w: 2, h: 2 }, null),
    cmd.gdiOverlay(5),
    cmd.present(),
  ];
  for (const c of all) assert.doesNotThrow(() => validateCommand(c), c.op);
  assert.equal(all[0].op, 'setDisplayMode');
  assert.equal(new Set(all.map(c => c.op)).size, all.length - 1 /* bindTexture twice */);
});

test('malformed commands fail loudly', () => {
  assert.throws(() => validateCommand({ op: 'nope' }), FrameModelError);
  assert.throws(() => validateCommand(null), FrameModelError);
  assert.throws(() => cmd.setTransform(2, new Float32Array(15)), /16/);
  assert.throws(() => cmd.draw({ primitive: 'triangleList', startVertex: 0, vertexCount: 3, indices: Uint16Array.of(0, 1) }), /multiple of 3/);
  assert.throws(() => cmd.draw({ primitive: 'lineList', startVertex: 0, vertexCount: 3, indices: Uint16Array.of(0, 1, 2) }), /multiple of 2/);
  assert.throws(() => cmd.draw({ primitive: 'triangleList', startVertex: 0, vertexCount: 3, indices: Uint16Array.of(0, 1, 3) }), /index 3 .* vertexCount 3/);
  assert.throws(() => cmd.draw({ primitive: 'triangleList', startVertex: 0, vertexCount: 3, indices: [0, 1, 2] }), /Uint16Array/);
  assert.throws(() => cmd.createTexture(1, 2, 2, 'RGB565', new Uint8Array(7)), /needs 8 bytes/);
  assert.throws(() => cmd.createTexture(1, 2, 2, 'DXT1', new Uint8Array(8)), /unsupported texture format/i);
  assert.throws(() => cmd.uploadVertices(new Uint8Array(35 + 36)), /multiple of 36/);
  assert.throws(() => cmd.setRenderState(7, -1), /uint32/);
  assert.throws(() => cmd.bindTexture(2, 5), /stage/);
  assert.throws(() => cmd.gdiOverlay(5, -1), /threshold/);
  assert.equal(cmd.gdiOverlay(5).threshold, 24);
});

test('validateSequence: frames of commands, display size, counts', () => {
  const seq = { width: 640, height: 480, frames: [{ commands: [cmd.present()] }, { commands: [] }] };
  assert.doesNotThrow(() => validateSequence(seq));
  assert.throws(() => validateSequence({ width: 640, height: 480, frames: [{ commands: [{ op: 'x' }] }] }), /frame 0, command 0/);
  assert.throws(() => validateSequence({ width: 0, height: 480, frames: [] }), /size/);
});
