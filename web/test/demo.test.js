import test from 'node:test';
import assert from 'node:assert/strict';
import { buildDemoSequence, DEMO_PROBES } from '../src/demo.js';
import { validateSequence } from '../src/frame-model.js';
import { encodeSequence, decodeSequence } from '../src/sequence-json.js';
import { DeviceState } from '../src/state.js';

test('demo sequence is valid, three frames, and every state it sets is accepted by the state tracker', () => {
  const seq = buildDemoSequence();
  assert.doesNotThrow(() => validateSequence(seq));
  assert.equal(seq.frames.length, 3);
  assert.equal(seq.frames.at(-1).commands.at(-1).op, 'present');
  const st = new DeviceState();
  for (const f of seq.frames) for (const c of f.commands) {
    st.apply(c);
    if (c.op === 'draw') assert.doesNotThrow(() => st.drawConfig());
  }
  assert.ok(DEMO_PROBES.length >= 15);
});

test('JSON round trip preserves typed arrays and commands', () => {
  const seq = buildDemoSequence();
  const back = decodeSequence(encodeSequence(seq));
  assert.equal(back.frames.length, 3);
  assert.deepEqual(back.frames[1].commands, seq.frames[1].commands);
  const draw = back.frames[0].commands.find((c) => c.op === 'draw');
  assert.ok(draw.indices instanceof Uint16Array);
});

test('decodeSequence rejects an invalid sequence', () => {
  assert.throws(() => decodeSequence('{"width":1,"height":1,"frames":[{"commands":[{"op":"nope"}]}]}'), /unknown command op/);
});
