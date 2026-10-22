import test from 'node:test';
import assert from 'node:assert/strict';
import { convertToRGBA8, bytesPerPixel, formatFromPixelFormat, TEX_FORMATS } from '../src/formats.js';

const px = (out, i = 0) => Array.from(out.slice(i * 4, i * 4 + 4));
const u16 = (...v) => new Uint8Array(new Uint16Array(v).buffer);

test('RGB565: pure channels and a mid grey, hand computed', () => {
  const out = convertToRGBA8('RGB565', 4, 1, u16(0xf800, 0x07e0, 0x001f, 0x8410));
  assert.deepEqual(px(out, 0), [255, 0, 0, 255]);
  assert.deepEqual(px(out, 1), [0, 255, 0, 255]);
  assert.deepEqual(px(out, 2), [0, 0, 255, 255]);
  // r=16 -> 132, g=32 -> 130, b=16 -> 132 (bit replication)
  assert.deepEqual(px(out, 3), [132, 130, 132, 255]);
});

test('RGB555 ignores bit 15 and is opaque', () => {
  const out = convertToRGBA8('RGB555', 4, 1, u16(0x7c00, 0x03e0, 0x0010, 0x8000));
  assert.deepEqual(px(out, 0), [255, 0, 0, 255]);
  assert.deepEqual(px(out, 1), [0, 255, 0, 255]);
  assert.deepEqual(px(out, 2), [0, 0, 132, 255]);
  assert.deepEqual(px(out, 3), [0, 0, 0, 255]);
});

test('ARGB1555 has a 1-bit alpha', () => {
  const out = convertToRGBA8('ARGB1555', 2, 1, u16(0xfc00, 0x7c00));
  assert.deepEqual(px(out, 0), [255, 0, 0, 255]);
  assert.deepEqual(px(out, 1), [255, 0, 0, 0]);
});

test('ARGB4444 replicates nibbles (x17)', () => {
  const out = convertToRGBA8('ARGB4444', 2, 1, u16(0x8421, 0xf123));
  assert.deepEqual(px(out, 0), [68, 34, 17, 136]);
  assert.deepEqual(px(out, 1), [17, 34, 51, 255]);
});

test('XRGB8888 (32-bit RGB) is BGRX in memory and forces alpha 255', () => {
  const out = convertToRGBA8('XRGB8888', 1, 1, Uint8Array.of(0x30, 0x20, 0x10, 0x99));
  assert.deepEqual(px(out), [0x10, 0x20, 0x30, 255]);
});

test('ARGB8888 is BGRA in memory and keeps alpha', () => {
  const out = convertToRGBA8('ARGB8888', 1, 1, Uint8Array.of(0x30, 0x20, 0x10, 0x80));
  assert.deepEqual(px(out), [0x10, 0x20, 0x30, 0x80]);
});

test('pitch: padded rows are skipped', () => {
  // 2x2 RGB565, pitch 8 bytes (4 bytes of padding per row)
  const bytes = new Uint8Array(16);
  const dv = new DataView(bytes.buffer);
  dv.setUint16(0, 0xf800, true); dv.setUint16(2, 0x07e0, true);
  dv.setUint16(8, 0x001f, true); dv.setUint16(10, 0xffff, true);
  const out = convertToRGBA8('RGB565', 2, 2, bytes, { pitch: 8 });
  assert.deepEqual(px(out, 0), [255, 0, 0, 255]);
  assert.deepEqual(px(out, 1), [0, 255, 0, 255]);
  assert.deepEqual(px(out, 2), [0, 0, 255, 255]);
  assert.deepEqual(px(out, 3), [255, 255, 255, 255]);
});

test('colour key: matching texels get alpha 0, colour bits only (X byte ignored)', () => {
  const bytes = Uint8Array.of(0xff, 0, 0, 0x99, 0xfe, 0, 0, 0x00);
  const out = convertToRGBA8('XRGB8888', 2, 1, bytes, { colorKey: { low: 0xff, high: 0xff } });
  assert.deepEqual(px(out, 0), [0, 0, 255, 0]);
  assert.deepEqual(px(out, 1), [0, 0, 254, 255]);
});

test('colour key range is inclusive; RGB565 key compares the raw 16-bit value', () => {
  const out = convertToRGBA8('RGB565', 3, 1, u16(0x001f, 0x0020, 0x0021), { colorKey: { low: 0x1f, high: 0x20 } });
  assert.deepEqual([out[3], out[7], out[11]], [0, 0, 255]);
});

test('ARGB4444 colour key ignores the alpha bits of the texel', () => {
  const out = convertToRGBA8('ARGB4444', 1, 1, u16(0xa00f), { colorKey: { low: 0x00f, high: 0x00f } });
  assert.equal(out[3], 0);
});

test('bytesPerPixel and unknown formats fail loudly', () => {
  assert.equal(bytesPerPixel('RGB565'), 2);
  assert.equal(bytesPerPixel('ARGB8888'), 4);
  assert.throws(() => bytesPerPixel('DXT1'), /unsupported texture format/i);
  assert.throws(() => convertToRGBA8('PVRTC', 4, 4, new Uint8Array(8)), /unsupported texture format/i);
});

test('too few bytes is an error, not a silent partial upload', () => {
  assert.throws(() => convertToRGBA8('RGB565', 2, 2, new Uint8Array(7)), /needs 8 bytes/);
});

test('formatFromPixelFormat identifies the six formats of the golden summary', () => {
  const f = (bits, r, g, b, a) => formatFromPixelFormat({ rgbBitCount: bits, rMask: r, gMask: g, bMask: b, aMask: a });
  assert.equal(f(16, 0x7c00, 0x3e0, 0x1f, 0), 'RGB555');
  assert.equal(f(16, 0xf800, 0x7e0, 0x1f, 0), 'RGB565');
  assert.equal(f(32, 0xff0000, 0xff00, 0xff, 0), 'XRGB8888');
  assert.equal(f(16, 0x7c00, 0x3e0, 0x1f, 0x8000), 'ARGB1555');
  assert.equal(f(16, 0xf00, 0xf0, 0xf, 0xf000), 'ARGB4444');
  assert.equal(f(32, 0xff0000, 0xff00, 0xff, 0xff000000), 'ARGB8888');
  assert.throws(() => f(24, 0xff0000, 0xff00, 0xff, 0), /unsupported pixel format/i);
  assert.deepEqual([...TEX_FORMATS].sort(), ['ARGB1555', 'ARGB4444', 'ARGB8888', 'DXT1', 'DXT3', 'DXT5', 'RGB555', 'RGB565', 'XRGB8888']);
});
