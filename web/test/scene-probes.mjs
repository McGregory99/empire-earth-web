// Runs every scene of test/scenes.js through the real renderer in the page and compares the probe pixels (per channel, within
// the scene's tolerance, default 2). Called by headless-check.mjs with the page that has loaded index.html.

/** @returns {Promise<number>} the number of failed scenes / probes */
export async function checkScenes(page) {
  const results = await page.evaluate(async () => {
    const { SCENES } = await import('/test/scenes.js');
    const { renderer } = window.__ee;
    const out = [];
    for (const s of SCENES) {
      renderer.reset();
      let error = null;
      try {
        for (const c of s.commands()) renderer.execute(c);
      } catch (e) { error = `${e.name}: ${e.message}`; }
      const px = renderer.readPixels();
      const w = renderer.width;
      out.push({
        name: s.name, error, expectUnsupported: s.expectUnsupported ? s.expectUnsupported.source : null,
        probes: s.probes.map(([x, y, rgb, why]) => ({ x, y, rgb, why, got: Array.from(px.subarray((y * w + x) * 4, (y * w + x) * 4 + 3)) })),
        tol: s.tol ?? 2,
      });
    }
    return out;
  });
  let failures = 0;
  for (const r of results) {
    if (r.expectUnsupported) {
      const ok = r.error !== null && /^UnsupportedError/.test(r.error) && new RegExp(r.expectUnsupported).test(r.error);
      console.log(`${ok ? 'ok  ' : 'FAIL'} scene "${r.name}": must fail loudly with /${r.expectUnsupported}/, got ${r.error ?? 'no error'}`);
      if (!ok) failures++;
      continue;
    }
    if (r.error) { console.log(`FAIL scene "${r.name}": ${r.error}`); failures++; continue; }
    for (const p of r.probes) {
      const bad = p.rgb.some((v, i) => Math.abs(p.got[i] - v) > r.tol);
      console.log(`${bad ? 'FAIL' : 'ok  '} scene "${r.name}" (${p.x},${p.y}) expected ${p.rgb} got ${p.got}: ${p.why}`);
      if (bad) failures++;
    }
  }
  return failures;
}
