import test from 'node:test';
import assert from 'node:assert/strict';
import { SCENES, SIZE } from './scenes.js';
import { validateCommand } from '../src/frame-model.js';

// The pixel checks run in a browser; this only guards the scene definitions themselves (valid commands, probes inside the target).
test('every scene builds valid commands and probes inside the target', () => {
  const names = new Set();
  for (const s of SCENES) {
    assert.ok(!names.has(s.name), `duplicate scene name ${s.name}`);
    names.add(s.name);
    const cmds = s.commands();
    assert.ok(cmds.length > 0);
    cmds.forEach((c) => validateCommand(c));
    if (s.expectUnsupported) assert.ok(s.expectUnsupported instanceof RegExp);
    for (const [x, y, rgb, why] of s.probes) {
      assert.ok(x >= 0 && x < SIZE && y >= 0 && y < SIZE, `${s.name}: probe (${x},${y}) outside the ${SIZE}x${SIZE} target`);
      assert.equal(rgb.length, 3);
      assert.ok(why && why.length >= 6, `${s.name}: a probe needs the formula behind its expected value`);
    }
  }
});
