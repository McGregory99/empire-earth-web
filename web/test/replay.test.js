import test from 'node:test';
import assert from 'node:assert/strict';
import { Replayer } from '../src/replay.js';
import { cmd } from '../src/frame-model.js';

function mockRenderer() {
  const log = [];
  return { log, reset: () => log.push('reset'), executeFrame: (f) => log.push(f.commands[0].id) };
}
const seq = (n) => ({ width: 4, height: 4, frames: Array.from({ length: n }, (_, i) => ({ commands: [cmd.setRenderState(i, 0), cmd.present()] })) });

function clock() {
  let t = 0, pending = null;
  return {
    now: () => t,
    schedule: (fn) => { pending = fn; return 1; },
    cancel: () => { pending = null; },
    tick(ms) { t += ms; const fn = pending; pending = null; fn?.(); },
    get armed() { return pending !== null; },
  };
}

test('step executes one frame at a time and counts them', () => {
  const r = mockRenderer();
  const rp = new Replayer(r, seq(3));
  assert.equal(rp.frame, 0);
  assert.equal(rp.total, 3);
  assert.equal(rp.step(), true);
  assert.equal(rp.frame, 1);
  rp.step(); rp.step();
  assert.equal(rp.frame, 3);
  assert.equal(rp.step(), false, 'past the end is a no-op');
  assert.deepEqual(r.log, [0, 1, 2]);
});

test('play advances at the requested frame rate and stops at the end', () => {
  const r = mockRenderer(); const c = clock(); const changes = [];
  const rp = new Replayer(r, seq(3), { fps: 10, ...c, onChange: (s) => changes.push(s) });
  rp.play();
  assert.equal(rp.playing, true);
  c.tick(50);  assert.equal(rp.frame, 0, 'not due yet at 50 ms');
  c.tick(60);  assert.equal(rp.frame, 1);
  c.tick(100); assert.equal(rp.frame, 2);
  c.tick(100); assert.equal(rp.frame, 3);
  assert.equal(rp.playing, false);
  assert.equal(c.armed, false);
  assert.deepEqual(changes.at(-1), { frame: 3, total: 3, playing: false, error: null });
});

test('pause stops advancing; play resumes; loop wraps around', () => {
  const r = mockRenderer(); const c = clock();
  const rp = new Replayer(r, seq(2), { fps: 10, loop: true, ...c });
  rp.play(); c.tick(100); rp.pause();
  assert.equal(rp.playing, false);
  assert.equal(c.armed, false);
  assert.equal(rp.frame, 1);
  rp.play(); c.tick(100); assert.equal(rp.frame, 2);
  c.tick(100);
  assert.equal(rp.frame, 1, 'wrapped: reset then frame 0 replayed');
  assert.deepEqual(r.log, [0, 1, 'reset', 0]);
});

test('seek forward is incremental, seek backward resets and replays', () => {
  const r = mockRenderer();
  const rp = new Replayer(r, seq(5));
  rp.seek(3);
  assert.deepEqual(r.log, [0, 1, 2]);
  rp.seek(5);
  assert.deepEqual(r.log, [0, 1, 2, 3, 4]);
  rp.seek(1);
  assert.deepEqual(r.log.slice(5), ['reset', 0]);
  assert.equal(rp.frame, 1);
  assert.throws(() => rp.seek(6), /out of range/);
  assert.throws(() => rp.seek(-1), /out of range/);
});

test('a renderer error stops playback, is exposed, and is rethrown by step', () => {
  const r = { reset() {}, executeFrame() { throw new Error('unsupported thing'); } };
  const c = clock();
  const rp = new Replayer(r, seq(2), { ...c });
  rp.play();
  assert.doesNotThrow(() => c.tick(100));
  assert.equal(rp.playing, false);
  assert.match(rp.error.message, /unsupported thing/);
  assert.equal(rp.frame, 0, 'the failed frame is not counted');
  assert.throws(() => rp.step(), /unsupported thing/);
});

test('empty sequence', () => {
  const rp = new Replayer(mockRenderer(), { width: 1, height: 1, frames: [] });
  assert.equal(rp.step(), false);
  assert.equal(rp.total, 0);
});
