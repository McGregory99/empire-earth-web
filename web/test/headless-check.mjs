// Headless pixel check of the synthetic demo sequence: serves web/ on localhost, opens index.html in headless
// Chromium (software GL), replays all frames, then compares (1) hand-computed probe pixels and (2) a stored
// regression image (within a small tolerance). Exit 0 = pass, 1 = fail, 77 = skipped (no browser available).
//
//   node web/test/headless-check.mjs            check
//   node web/test/headless-check.mjs --update  rewrite web/test/expected/demo-frame3.rgba.gz (review the diff!)

import http from 'node:http';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { DEMO_PROBES, DEMO_WIDTH, DEMO_HEIGHT } from '../src/demo.js';

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

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const expectedPath = path.join(root, 'test/expected/demo-frame3.rgba.gz');
const update = process.argv.includes('--update');
const TOLERANCE = 2; // per channel, software-GL differences between machines

const skip = (why) => { console.log(`SKIP headless check: ${why}`); process.exit(77); };

function loadPlaywright() {
  const candidates = [
    process.env.PLAYWRIGHT_CORE,
    '/opt/homebrew/lib/node_modules/@playwright/cli/node_modules/playwright-core',
    '/usr/local/lib/node_modules/@playwright/cli/node_modules/playwright-core',
    'playwright-core', 'playwright',
  ].filter(Boolean);
  for (const c of candidates) {
    try {
      const req = createRequire(import.meta.url);
      const m = req(c);
      if (m.chromium) return m.chromium;
    } catch { /* next */ }
  }
  return null;
}

function findBrowser() {
  if (process.env.EE_BROWSER && fs.existsSync(process.env.EE_BROWSER)) return process.env.EE_BROWSER;
  const cache = path.join(os.homedir(), 'Library/Caches/ms-playwright'); // macOS
  const linux = path.join(os.homedir(), '.cache/ms-playwright');
  for (const dir of [cache, linux]) {
    if (!fs.existsSync(dir)) continue;
    for (const d of fs.readdirSync(dir).filter((n) => n.startsWith('chromium_headless_shell')).sort().reverse()) {
      for (const sub of fs.readdirSync(path.join(dir, d))) {
        const exe = path.join(dir, d, sub, 'chrome-headless-shell');
        if (fs.existsSync(exe)) return exe;
      }
    }
  }
  return null;
}

const chromium = loadPlaywright();
if (!chromium) skip('playwright-core not found (set PLAYWRIGHT_CORE to its directory)');
const exe = findBrowser();
if (!exe) skip('no headless Chromium found (set EE_BROWSER to chrome-headless-shell)');

const types = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.json': 'application/json' };
const server = http.createServer((req, res) => {
  const p = path.join(root, decodeURIComponent(req.url.split('?')[0]).replace(/^\/$/, '/index.html'));
  if (!p.startsWith(root) || !fs.existsSync(p) || fs.statSync(p).isDirectory()) { res.writeHead(404).end(); return; }
  res.writeHead(200, { 'content-type': types[path.extname(p)] ?? 'application/octet-stream' }).end(fs.readFileSync(p));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const url = `http://127.0.0.1:${server.address().port}/index.html`;

const browser = await chromium.launch({
  executablePath: exe,
  args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
});
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

  // unsupported state must fail loudly, not draw something else
  const loud = await page.evaluate(() => {
    const { renderer } = window.__ee;
    try {
      renderer.execute({ op: 'setRenderState', id: 8, value: 2 }); // FILLMODE = WIREFRAME
      return 'no error';
    } catch (e) { return `${e.name}: ${e.message}`; }
  });
  if (!/UnsupportedError/.test(loud)) fail(`unsupported state did not fail loudly: ${loud}`);
  else console.log(`ok   unsupported state fails loudly (${loud})`);
} finally {
  await browser.close();
  server.close();
}
console.log(failures ? `FAILED (${failures})` : 'PASSED');
process.exit(failures ? 1 : 0);
