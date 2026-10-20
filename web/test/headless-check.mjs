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
