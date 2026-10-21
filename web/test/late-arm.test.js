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
