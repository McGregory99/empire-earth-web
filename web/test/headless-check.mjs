// Headless pixel check of the synthetic demo sequence: serves web/ on localhost, opens index.html in headless
// Chromium (software GL), replays all frames, then compares (1) hand-computed probe pixels and (2) a stored
// regression image (within a small tolerance). Exit 0 = pass, 1 = fail, 77 = skipped (no browser available).
//
//   node web/test/headless-check.mjs            check
//   node web/test/headless-check.mjs --update  rewrite web/test/expected/demo-frame3.rgba.gz (review the diff!)

import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { DEMO_PROBES, DEMO_WIDTH, DEMO_HEIGHT } from '../src/demo.js';
import { root, startBrowser } from './headless-lib.mjs';
import { checkScenes } from './scene-probes.mjs';

// Hand-computed pixels of test/fixtures/synthetic.trace, per frame: [x, y, [r, g, b], why]. Background 0x102030 = (16, 32, 48).
const FIXTURE_PROBES = [
  [[10, 10, [16, 32, 48], 'clear colour'], [32, 24, [255, 0, 0], 'texel (0,0) red'], [112, 36, [0, 255, 0], 'texel (3,0) green'],
    [48, 84, [0, 0, 255], 'texel (0,3) blue'], [112, 84, [255, 255, 255], 'texel (3,3) white'],
    [192, 36, [255, 0, 0], 'second draw, shifted by the world matrix'], [256, 84, [255, 255, 255], 'second draw, white texel'],
    [16, 146, [16, 32, 48], 'colour-keyed sprite: key half skipped'], [32, 146, [0, 255, 0], 'sprite: green half'],
    [120, 146, [255, 255, 0], 'colour fill']],
  [[32, 24, [255, 0, 0], 'texel (0,0) still red'], [64, 48, [255, 255, 255], 'partial Unlock: texel (1,1) is now white'],
    [96, 48, [255, 255, 255], 'partial Unlock: texel (2,1) is now white'], [32, 72, [0, 0, 255], 'texel (0,2) untouched'],
    [240, 168, [255, 128, 0], 'application-array triangle'], [192, 36, [16, 32, 48], 'cleared: second quad gone'],
    [16, 146, [0, 255, 0], 'GDI ReleaseDC: left half green now'], [32, 146, [16, 32, 48], 'sprite right half is key now']],
];

const expectedPath = path.join(root, 'test/expected/demo-frame3.rgba.gz');
const update = process.argv.includes('--update');
const TOLERANCE = 2; // per channel, software-GL differences between machines

const { browser, url, close } = await startBrowser();
let failures = 0;
const fail = (m) => { failures++; console.log(`FAIL ${m}`); };
try {
  const page = await browser.newPage();
  const pageErrors = [];
  page.on('pageerror', (e) => pageErrors.push(e.message));
  await page.goto(url);
  await page.waitForFunction(() => window.__ee?.replayer);
  const result = await page.evaluate(() => {
    const { replayer, renderer } = window.__ee;
    replayer.seek(replayer.total);
    const px = renderer.readPixels();
    let s = '';
    for (let i = 0; i < px.length; i += 0x8000) s += String.fromCharCode(...px.subarray(i, i + 0x8000));
    return { frame: replayer.frame, total: replayer.total, error: replayer.error?.message ?? null, b64: btoa(s), w: renderer.width, h: renderer.height };
  });
  if (pageErrors.length) fail(`page errors: ${pageErrors.join('; ')}`);
  if (result.error) fail(`replay error: ${result.error}`);
  if (result.frame !== 3 || result.total !== 3) fail(`expected 3/3 frames, got ${result.frame}/${result.total}`);
  if (result.w !== DEMO_WIDTH || result.h !== DEMO_HEIGHT) fail(`canvas is ${result.w}x${result.h}`);
  const px = Buffer.from(result.b64, 'base64');

  for (const [x, y, rgb, tol, why] of DEMO_PROBES) {
    const o = (y * DEMO_WIDTH + x) * 4;
    const got = [px[o], px[o + 1], px[o + 2]];
    const bad = got.some((v, i) => Math.abs(v - rgb[i]) > tol);
    console.log(`${bad ? 'FAIL' : 'ok  '} probe (${x},${y}) expected ${rgb} got ${got}: ${why}`);
    if (bad) failures++;
  }

  if (update) {
    fs.mkdirSync(path.dirname(expectedPath), { recursive: true });
    fs.writeFileSync(expectedPath, zlib.gzipSync(px, { level: 9 }));
    console.log(`wrote ${expectedPath}`);
  } else {
    const exp = zlib.gunzipSync(fs.readFileSync(expectedPath));
    let worst = 0, differing = 0;
    for (let i = 0; i < exp.length; i++) {
      const d = Math.abs(exp[i] - px[i]);
      if (d > worst) worst = d;
      if (d > TOLERANCE) differing++;
    }
    console.log(`regression image: ${exp.length} bytes, worst channel difference ${worst}, ${differing} channels beyond tolerance ${TOLERANCE}`);
    if (differing) fail('rendered frame differs from the stored regression image');
  }

  // the synthetic ddraw_trace fixture through the real loader and renderer, in the page
  const fx = (n) => fs.readFileSync(path.join(root, 'test/fixtures', n));
  const fixture = await page.evaluate(async ({ trace, data, probes }) => {
    const { loadFromFiles } = await import('/src/trace-loader.js');
    const bin = (b64) => Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
    const { sequence } = await loadFromFiles([{ name: 'synthetic.trace', bytes: bin(trace) }, { name: 'synthetic.data', bytes: bin(data) }]);
    window.__ee.load(sequence, 'fixture');
    const { replayer, renderer } = window.__ee;
    const out = { frames: replayer.total, error: null, shots: [] };
    for (let f = 1; f <= replayer.total; f++) {
      replayer.seek(f);
      out.error = replayer.error?.message ?? out.error;
      const px = renderer.readPixels();
      out.shots.push(probes[f - 1].map(([x, y]) => Array.from(px.subarray((y * renderer.width + x) * 4, (y * renderer.width + x) * 4 + 3))));
    }
    return out;
  }, { trace: fx('synthetic.trace').toString('base64'), data: fx('synthetic.data').toString('base64'), probes: FIXTURE_PROBES.map((f) => f.map((p) => p.slice(0, 2))) });
  if (fixture.error) fail(`fixture replay error: ${fixture.error}`);
  if (fixture.frames !== 2) fail(`fixture: expected 2 frames, got ${fixture.frames}`);
  FIXTURE_PROBES.forEach((frame, f) => frame.forEach(([x, y, rgb, why], i) => {
    const got = fixture.shots[f]?.[i] ?? [];
    const bad = rgb.some((v, k) => Math.abs((got[k] ?? -99) - v) > 1);
    console.log(`${bad ? 'FAIL' : 'ok  '} fixture frame ${f + 1} (${x},${y}) expected ${rgb} got ${got}: ${why}`);
    if (bad) failures++;
  }));

  // gdiOverlay: a screen snapshot equal to the render except a white square (GDI text) and a +5 tweak below the threshold
  const gdi = await page.evaluate(() => {
    const { renderer, replayer } = window.__ee;
    const w = renderer.width, h = renderer.height;
    const before = renderer.readPixels();
    const snap = new Uint8Array(w * h * 4);
    for (let i = 0; i < w * h; i++) { snap[i * 4] = before[i * 4 + 2]; snap[i * 4 + 1] = before[i * 4 + 1]; snap[i * 4 + 2] = before[i * 4]; }
    for (let y = 200; y < 210; y++) for (let x = 200; x < 210; x++) snap.set([255, 255, 255, 0], (y * w + x) * 4);
    snap[(10 * w + 10) * 4] = Math.min(255, snap[(10 * w + 10) * 4] + 5);
    renderer.execute({ op: 'createTexture', id: 9000, width: w, height: h, format: 'XRGB8888', bytes: snap, pitch: null, colorKey: null });
    renderer.execute({ op: 'gdiOverlay', srcId: 9000, threshold: 24 });
    const after = renderer.readPixels();
    let changed = 0;
    for (let i = 0; i < before.length; i += 4) if (before[i] !== after[i] || before[i + 1] !== after[i + 1] || before[i + 2] !== after[i + 2]) changed++;
    const at = (x, y) => Array.from(after.subarray((y * w + x) * 4, (y * w + x) * 4 + 3));
    return { changed, gdiPixels: renderer.stats.gdiPixels, square: at(205, 205), tweak: at(10, 10), tweakBefore: Array.from(before.subarray((10 * w + 10) * 4, (10 * w + 10) * 4 + 3)), frames: replayer.frame };
  });
  const gdiOk = gdi.square.join() === '255,255,255' && gdi.tweak.join() === gdi.tweakBefore.join() && gdi.changed <= 100 && gdi.gdiPixels === 100;
  console.log(`${gdiOk ? 'ok  ' : 'FAIL'} gdiOverlay copies only the 10x10 square (${gdi.gdiPixels} px copied, ${gdi.changed} changed, tweak below threshold kept)`);
  if (!gdiOk) failures++;

  // D3D pixel centres sit on integers: a quad with vertices at -0.5 offsets, drawn 1:1 with point sampling, must reproduce
  // the texture pixel for pixel (the original renders its 2D menu this way)
  const halfPixel = await page.evaluate(() => {
    const { renderer } = window.__ee;
    renderer.reset();
    const run = (c) => renderer.execute(c);
    const W = 128, H = 128;
    run({ op: 'setDisplayMode', width: W, height: H });
    const texels = [];
    for (let i = 0; i < 16; i++) texels.push(((i * 2 + 1) & 31) << 11 | ((i * 4 + 3) & 63) << 5 | ((i * 7 + 2) & 31)); // distinct RGB565
    const bytes = new Uint8Array(new Uint16Array(texels).buffer);
    run({ op: 'createTexture', id: 1, width: 4, height: 4, format: 'RGB565', bytes, pitch: null, colorKey: null });
    run({ op: 'setTransform', type: 3, matrix: Float32Array.from([2 / W, 0, 0, 0, 0, -2 / H, 0, 0, 0, 0, 1, 0, -1, 1, 0, 1]) });
    for (const [id, v] of [[137, 0], [7, 0], [22, 1]]) run({ op: 'setRenderState', id, value: v });
    for (const [id, v] of [[1, 4], [2, 2], [3, 0], [4, 2], [5, 2], [16, 1], [17, 1], [12, 3]]) run({ op: 'setTextureStageState', stage: 0, id, value: v });
    run({ op: 'bindTexture', stage: 0, textureId: 1 });
    const x0 = 99.5, y0 = 49.5, x1 = 103.5, y1 = 53.5;
    const f = new Float32Array(4 * 9); const u = new Uint32Array(f.buffer);
    [[x0, y0, 0, 0], [x1, y0, 1, 0], [x1, y1, 1, 1], [x0, y1, 0, 1]].forEach(([x, y, tu, tv], i) => {
      f.set([x, y, 0, 0, 0, -1], i * 9); u[i * 9 + 6] = 0xffffffff; f.set([tu, tv], i * 9 + 7);
    });
    run({ op: 'uploadVertices', bytes: new Uint8Array(f.buffer), offset: 0 });
    run({ op: 'draw', primitive: 'triangleList', startVertex: 0, vertexCount: 4, indices: Uint16Array.of(0, 1, 2, 0, 2, 3) });
    const px = renderer.readPixels();
    const e8 = (v, n) => Math.round(v * 255 / ((1 << n) - 1));
    let bad = 0; const first = [];
    for (let j = 0; j < 4; j++) for (let i = 0; i < 4; i++) {
      const t = texels[j * 4 + i];
      const want = [e8(t >> 11, 5), e8((t >> 5) & 63, 6), e8(t & 31, 5)];
      const o = ((50 + j) * W + 100 + i) * 4;
      if (want.some((v, k) => Math.abs(px[o + k] - v) > 1)) { bad++; if (first.length < 2) first.push([i, j, want, Array.from(px.subarray(o, o + 3))]); }
    }
    const outside = [px.subarray((50 * W + 99) * 4, (50 * W + 99) * 4 + 3), px.subarray((50 * W + 104) * 4, (50 * W + 104) * 4 + 3)].map((a) => Array.from(a).join());
    return { bad, first, outside };
  });
  const hpOk = halfPixel.bad === 0 && halfPixel.outside.every((v) => v === '0,0,0');
  console.log(`${hpOk ? 'ok  ' : 'FAIL'} 1:1 point-sampled quad at -0.5 offsets reproduces the texture pixel for pixel (${halfPixel.bad} of 16 texels wrong ${JSON.stringify(halfPixel.first)}, neighbours ${halfPixel.outside.join(' | ')})`);
  if (!hpOk) failures++;

  // synthetic scenes of test/scenes.js: hand-computed pixels per feature
  failures += await checkScenes(page);

  // unsupported state must fail loudly, not draw something else
  const loud = await page.evaluate(() => {
    const { renderer } = window.__ee;
    try {
      renderer.execute({ op: 'setRenderState', id: 47, value: 1 }); // ZBIAS
      return 'no error';
    } catch (e) { return `${e.name}: ${e.message}`; }
  });
  if (!/UnsupportedError/.test(loud)) fail(`unsupported state did not fail loudly: ${loud}`);
  else console.log(`ok   unsupported state fails loudly (${loud})`);
} finally {
  await close();
}
console.log(failures ? `FAILED (${failures})` : 'PASSED');
process.exit(failures ? 1 : 0);
