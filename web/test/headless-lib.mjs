// Shared helpers of the headless Chromium checks: locate playwright-core and a headless Chromium, serve web/ on localhost,
// launch the browser with software GL. A missing browser exits 77 (skipped), never 0.

import http from 'node:http';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

export const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

export const skip = (why) => { console.log(`SKIP headless check: ${why}`); process.exit(77); };

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

const types = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.json': 'application/json' };

/** Start the static server on web/ and the headless browser. Returns { browser, url, close() }; exits 77 if none is available. */
export async function startBrowser() {
  const chromium = loadPlaywright();
  if (!chromium) skip('playwright-core not found (set PLAYWRIGHT_CORE to its directory)');
  const exe = findBrowser();
  if (!exe) skip('no headless Chromium found (set EE_BROWSER to chrome-headless-shell)');
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
  return { browser, url, close: async () => { await browser.close(); server.close(); } };
}
