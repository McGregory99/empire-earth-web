// A capture armed late (DDT_ARM_FILE) never sees the CreateSurface / SetDisplayMode calls of the objects it uses. These tests use a
// hand-made synthetic trace of that kind (no game data): a 64x48 back buffer (surf#9, never created in the trace) that receives GDI
// text (ReleaseDC records of the whole surface), a cursor-sized save/restore under the cursor, a colour-keyed cursor sprite whose key
// was set before the window, and a Flip on the primary (surf#5, also never created).
import test from 'node:test';
import assert from 'node:assert/strict';
import { DataWriter, FMTX8888 } from './fixtures/build.mjs';
import { loadTrace } from '../src/trace-loader.js';

const W = 64, H = 48;

function lateTrace({ frames = 2 } = {}) {
  const d = new DataWriter();
  const solid = (rgb, n) => { const b = Buffer.alloc(n * 4); for (let i = 0; i < n; i++) b.set([rgb[2], rgb[1], rgb[0], 0], i * 4); return b; };
  const snap = solid([16, 32, 48], W * H);
  for (let y = 20; y < 24; y++) for (let x = 30; x < 34; x++) snap.set([0xff, 0xff, 0xff, 0], (y * W + x) * 4); // "text"
  const screenRef = d.surface(9, W, H, FMTX8888, [0, 0, W, H], snap, 2);
  const bgRef = d.surface(8, 4, 4, FMTX8888, [0, 0, 4, 4], solid([200, 0, 0], 16), 3);
  const cursorRef = d.surface(7, 4, 4, FMTX8888, [0, 0, 4, 4], solid([0, 200, 0], 16), 3);
  const L = ['# ddraw_trace v1', '# data: x', '# armed', `# snapshot surf#7 blob=${cursorRef}`];
  for (let f = 0; f < frames; f++) {
    L.push(`T1 surf#9.Blt(lpDestRect={8,8,12,12}, lpDDSrcSurface=surf#8, lpSrcRect={0,0,4,4}, dwFlags=0x8000000, lpDDBltFx=NULL) = DD_OK blob=${bgRef}`); // restore under the cursor
    L.push('T1 surf#9.GetDC(lphDC=P) = DD_OK');
    L.push(`T1 surf#9.ReleaseDC(hDC=P) = DD_OK blob=${screenRef}`);
    L.push('T1 surf#8.Blt(lpDestRect={0,0,4,4}, lpDDSrcSurface=surf#9, lpSrcRect={8,8,12,12}, dwFlags=0x8000000, lpDDBltFx=NULL) = DD_OK'); // save under the cursor
    L.push('T1 surf#9.Blt(lpDestRect={8,8,12,12}, lpDDSrcSurface=surf#7, lpSrcRect={0,0,4,4}, dwFlags=0x8008000, lpDDBltFx=NULL) = DD_OK'); // cursor sprite, key set before the window
    L.push('T1 surf#5.Flip(lpDDSurfaceTargetOverride=NULL, dwFlags=0x28) = DD_OK');
  }
  return { trace: L.join('\n') + '\n', data: d.buffer() };
}

const run = (o) => { const f = lateTrace(o); return loadTrace(f.trace, new Uint8Array(f.data)); };
const all = (seq) => seq.frames.flatMap((f) => f.commands);

test('no SetDisplayMode: the canvas size is the size of the surface that receives the GDI read-backs', () => {
  const { sequence } = run();
  assert.equal(sequence.width, W);
  assert.equal(sequence.height, H);
});

test('a surface the trace never created, whose whole content is read back by ReleaseDC, is the screen: GDI text becomes a gdiOverlay', () => {
  const { sequence, stats } = run();
  assert.equal(stats.gdiOverlays, 2);
  assert.equal(all(sequence).filter((c) => c.op === 'gdiOverlay').length, 2);
  assert.equal(sequence.frames.length, 2);
});

test('the restore under the cursor is a blit onto the screen, the save of the screen into a texture is ignored', () => {
  const { sequence, stats } = run();
  const blits = all(sequence).filter((c) => c.op === 'blit2D');
  assert.ok(blits.some((c) => c.srcId === 8 && c.dstRect.x === 8 && c.dstRect.y === 8), 'restore blit');
  assert.equal(stats.ignoredOffscreenBlts, 2, 'one save per frame');
});

test('a colour-keyed sprite whose key was set before the window is skipped and counted, not drawn as an opaque square', () => {
  const { sequence, stats } = run();
  assert.equal(all(sequence).filter((c) => c.op === 'blit2D' && c.srcId === 7).length, 0);
  assert.equal(stats.skippedKeyedBlits, 2);
});

test('a surface created by a CreateSurface call is not taken for the screen just because of a ReleaseDC', () => {
  const f = lateTrace({ frames: 1 });
  const text = f.trace.replace('# armed', '# armed\nT1 dd7#1.CreateSurface(lpDDSurfaceDesc={dwSize=124, dwFlags=0x7, dwHeight=48, dwWidth=64, ddpfPixelFormat={dwSize=32, dwFlags=0x40, dwRGBBitCount=32, dwRBitMask=0xff0000, dwGBitMask=0xff00, dwBBitMask=0xff, dwRGBAlphaBitMask=0x0}, ddsCaps={dwCaps=0x840, dwCaps2=0x0, dwCaps3=0x0, dwCaps4=0x0}}, lplpDDSurface->surf#9, pUnkOuter=NULL) = DD_OK');
  const { stats } = loadTrace(text, new Uint8Array(f.data));
  assert.equal(stats.gdiOverlays, 0);
});

// The game sets these once, when it creates its device (tests/ddraw_trace/golden/startup-menu-n60.trace.gz, dev7#3, lines 1189-1233),
// and never again; a capture armed after that never sees them, and the D3D defaults differ (ALPHAFUNC ALWAYS instead of GREATER: the
// transparent texels of an alpha-tested mesh wrote depth; NORMALIZENORMALS off: scaled normals lit the units too dark).
const stateTrace = (armed) => `# ddraw_trace v1\n${armed ? '# armed\n' : ''}` +
  'T1 dev7#3.SetRenderState(dwRenderStateType=ZENABLE(7), dwRenderState=0x1) = DD_OK\n' +
  'T1 surf#5.Flip(lpDDSurfaceTargetOverride=NULL, dwFlags=0x28) = DD_OK\n';
const rsValue = (seq, id) => { let v; for (const c of all(seq)) if (c.op === 'setRenderState' && c.id === id) v = c.value; return v; };

test('a late-armed capture starts its device with the render states the game set at device creation', () => {
  const { sequence } = loadTrace(stateTrace(true), null, { allowMissingData: true });
  assert.equal(rsValue(sequence, 25), 5, 'ALPHAFUNC GREATER');
  assert.equal(rsValue(sequence, 143), 1, 'NORMALIZENORMALS');
  assert.equal(rsValue(sequence, 140), undefined, 'fog is not seeded (its range is not in the capture)');
  assert.equal(rsValue(sequence, 7), 1, 'the capture\'s own ZENABLE still applies');
  const cmds = all(sequence);
  assert.ok(cmds.findIndex((c) => c.op === 'setRenderState' && c.id === 25) < cmds.findIndex((c) => c.op === 'setRenderState' && c.id === 7), 'seeded before the first captured state');
});

test('a capture that was not armed late gets no seeded state (it sees the device creation itself)', () => {
  const { sequence } = loadTrace(stateTrace(false), null, { allowMissingData: true });
  assert.equal(rsValue(sequence, 25), undefined);
  assert.equal(rsValue(sequence, 143), undefined);
});

test('a state the capture sets itself wins over the seeded one', () => {
  const t = stateTrace(true).replace('T1 surf#5', 'T1 dev7#3.SetRenderState(dwRenderStateType=ALPHAFUNC(25), dwRenderState=0x8) = DD_OK\nT1 surf#5');
  const { sequence } = loadTrace(t, null, { allowMissingData: true });
  assert.equal(rsValue(sequence, 25), 8);
});

// ---- advisories: CRLF traces, and a keyed blit without a known key ---------------------------------------------------------------------

const crlf = (t) => t.replace(/\n/g, '\r\n');

test('a trace with CRLF line ends loads like the LF one: the "# armed" header is still seen (seeded device states) and the data header too', () => {
  const lf = loadTrace(stateTrace(true), null, { allowMissingData: true });
  const cr = loadTrace(crlf(stateTrace(true)), null, { allowMissingData: true });
  assert.equal(rsValue(cr.sequence, 25), 5, 'ALPHAFUNC GREATER seeded');
  assert.deepEqual(all(cr.sequence).map((c) => c.op), all(lf.sequence).map((c) => c.op));
  const f = lateTrace();
  const full = loadTrace(crlf(f.trace), new Uint8Array(f.data));
  assert.equal(full.stats.gdiOverlays, 2);
  assert.equal(full.stats.skippedKeyedBlits, 2);
  assert.throws(() => loadTrace(crlf(f.trace), null), /data file/, 'the "# data:" header is found, so a missing data file is reported');
});

test('a keyed blit from a surface with no known key is an error unless the capture was armed late and the surface predates it', () => {
  const f = lateTrace({ frames: 1 });
  const notLate = f.trace.replace('# armed\n', '');
  assert.throws(() => loadTrace(notLate, new Uint8Array(f.data)), /colou?r key/i, 'a capture that saw the whole run has no excuse for an unknown key');
  const created = f.trace.replace('# armed', '# armed\nT1 dd7#1.CreateSurface(lpDDSurfaceDesc={dwSize=124, dwFlags=0x7, dwHeight=4, dwWidth=4, ddpfPixelFormat={dwSize=32, dwFlags=0x40, dwRGBBitCount=32, dwRBitMask=0xff0000, dwGBitMask=0xff00, dwBBitMask=0xff, dwRGBAlphaBitMask=0x0}, ddsCaps={dwCaps=0x840, dwCaps2=0x0, dwCaps3=0x0, dwCaps4=0x0}}, lplpDDSurface->surf#7, pUnkOuter=NULL) = DD_OK');
  assert.throws(() => loadTrace(created, new Uint8Array(f.data)), /colou?r key/i, 'created inside the window without a key: the key cannot have been set earlier');
  assert.equal(loadTrace(f.trace, new Uint8Array(f.data)).stats.skippedKeyedBlits, 1, 'late armed, surface from before the window: skipped and counted');
});

test('BltFast with the source colour key flag from a surface with an unknown key is skipped too, not drawn as an opaque square', () => {
  const f = lateTrace({ frames: 1 });
  const text = f.trace.replace('T1 surf#5.Flip', 'T1 surf#9.BltFast(dwX=20, dwY=20, lpDDSrcSurface=surf#7, lpSrcRect={0,0,4,4}, dwTrans=0x11) = DD_OK\nT1 surf#5.Flip');
  const { sequence, stats } = loadTrace(text, new Uint8Array(f.data));
  assert.equal(all(sequence).filter((c) => c.op === 'blit2D' && c.dstRect.x === 20).length, 0);
  assert.equal(stats.skippedKeyedBlits, 2);
});

// A late-armed trace whose only keyed blit is a 4x4 cursor sprite: `nBackground` texels of blue (the background), the rest white.
function cursorTrace(nBackground) {
  const d = new DataWriter();
  const px = Buffer.alloc(64);
  for (let i = 0; i < 16; i++) px.set(i < 16 - nBackground ? [0xff, 0xff, 0xff, 0] : [0xff, 0x00, 0x00, 0], i * 4); // B, G, R, X
  const cursorRef = d.surface(7, 4, 4, FMTX8888, [0, 0, 4, 4], px, 3);
  const screen = d.surface(9, W, H, FMTX8888, [0, 0, W, H], Buffer.alloc(W * H * 4), 2);
  const L = ['# ddraw_trace v1', '# data: x', '# armed', `# snapshot surf#7 blob=${cursorRef}`,
    'T1 surf#9.GetDC(lphDC=P) = DD_OK', `T1 surf#9.ReleaseDC(hDC=P) = DD_OK blob=${screen}`,
    'T1 surf#9.Blt(lpDestRect={8,8,12,12}, lpDDSrcSurface=surf#7, lpSrcRect={0,0,4,4}, dwFlags=0x8008000, lpDDBltFx=NULL) = DD_OK',
    'T1 surf#5.Flip(lpDDSurfaceTargetOverride=NULL, dwFlags=0x28) = DD_OK'];
  return { trace: L.join('\n') + '\n', data: new Uint8Array(d.buffer()) };
}

test('inferColorKeys: the colour that fills more than half of the sprite becomes its key (blue = 0x0000ff); off by default', () => {
  const f = cursorTrace(12);
  const off = loadTrace(f.trace, f.data);
  assert.equal(off.stats.skippedKeyedBlits, 1);
  assert.equal(all(off.sequence).filter((c) => c.op === 'blit2D').length, 0);
  const on = loadTrace(f.trace, f.data, { inferColorKeys: true });
  const blit = all(on.sequence).find((c) => c.op === 'blit2D' && c.srcId === 7);
  assert.deepEqual(blit.colorKey, { low: 0xff, high: 0xff });
  assert.equal(on.stats.inferredKeys, 1);
  assert.equal(on.stats.skippedKeyedBlits, 0);
});

test('inferColorKeys needs a strict majority: a sprite without a dominant colour is still skipped', () => {
  const f = cursorTrace(8); // 8 blue, 8 white
  const { stats } = loadTrace(f.trace, f.data, { inferColorKeys: true });
  assert.equal(stats.skippedKeyedBlits, 1);
  assert.equal(stats.inferredKeys ?? 0, 0);
});
