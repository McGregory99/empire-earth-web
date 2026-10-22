// Block-compressed textures (DXT1, DXT3, DXT5): decoded to RGBA8 in JavaScript. Hand-computed blocks; the interpolated colours use the integer
// arithmetic of the DirectX documentation, e.g. (2 * c0 + c1) / 3 rounded down.
import test from 'node:test';
import assert from 'node:assert/strict';
import { convertToRGBA8, isCompressed, textureByteSize, TEX_FORMATS, formatFromPixelFormat } from '../src/formats.js';
import { cmd, FrameModelError } from '../src/frame-model.js';
import { DataWriter } from './fixtures/build.mjs';
import { loadTrace } from '../src/trace-loader.js';

const RED = 0xf800, BLUE = 0x001f, WHITE = 0xffff;
const u16 = (v) => [v & 255, v >> 8];
const colorBlock = (c0, c1, idxRows) => [...u16(c0), ...u16(c1), ...idxRows]; // idxRows: 4 bytes, one per row, texel 0 in the low bits
const row = (...idx) => idx.reduce((b, v, i) => b | (v << (2 * i)), 0);
const texel = (rgba, i) => Array.from(rgba.subarray(i * 4, i * 4 + 4));

test('formats: DXT1, DXT3 and DXT5 are compressed, with 8, 16 and 16 bytes per 4x4 block', () => {
  for (const f of ['DXT1', 'DXT3', 'DXT5']) assert.ok(TEX_FORMATS.includes(f) && isCompressed(f));
  assert.equal(isCompressed('RGB565'), false);
  assert.equal(textureByteSize('DXT1', 8, 8), 32);
  assert.equal(textureByteSize('DXT3', 8, 4), 32);
  assert.equal(textureByteSize('DXT5', 5, 5), 64, 'sizes round up to whole blocks');
  assert.equal(textureByteSize('RGB565', 4, 4), 32);
});

test('a DDPIXELFORMAT with a DXT fourCC maps to the format', () => {
  assert.equal(formatFromPixelFormat({ fourcc: 0x31545844 }), 'DXT1');
  assert.equal(formatFromPixelFormat({ fourcc: 0x33545844 }), 'DXT3');
  assert.equal(formatFromPixelFormat({ fourcc: 0x35545844 }), 'DXT5');
  assert.throws(() => formatFromPixelFormat({ fourcc: 0x32545844 }), /DXT2|premultiplied|fourcc/i);
});

test('DXT1 four colour mode (c0 > c1): c2 = (2 c0 + c1) / 3, c3 = (c0 + 2 c1) / 3', () => {
  const block = Uint8Array.from(colorBlock(RED, BLUE, [row(0, 1, 2, 3), 0, 0, 0]));
  const px = convertToRGBA8('DXT1', 4, 4, block);
  assert.deepEqual(texel(px, 0), [255, 0, 0, 255]);
  assert.deepEqual(texel(px, 1), [0, 0, 255, 255]);
  assert.deepEqual(texel(px, 2), [170, 0, 85, 255]);
  assert.deepEqual(texel(px, 3), [85, 0, 170, 255]);
  assert.deepEqual(texel(px, 15), [255, 0, 0, 255], 'index 0 everywhere else');
});

test('DXT1 three colour mode (c0 <= c1): c2 = (c0 + c1) / 2 and index 3 is transparent black', () => {
  const block = Uint8Array.from(colorBlock(BLUE, RED, [row(0, 1, 2, 3), 0, 0, 0]));
  const px = convertToRGBA8('DXT1', 4, 4, block);
  assert.deepEqual(texel(px, 0), [0, 0, 255, 255]);
  assert.deepEqual(texel(px, 1), [255, 0, 0, 255]);
  assert.deepEqual(texel(px, 2), [127, 0, 127, 255]);
  assert.deepEqual(texel(px, 3), [0, 0, 0, 0]);
});

test('DXT1 colours expand from 565 by bit replication', () => {
  const px = convertToRGBA8('DXT1', 4, 4, Uint8Array.from(colorBlock(0x8410, 0x0000, [0, 0, 0, 0])));
  assert.deepEqual(texel(px, 0), [132, 130, 132, 255]); // r5 = 16 -> 132, g6 = 32 -> 130, b5 = 16 -> 132
});

test('DXT3: explicit 4 bit alpha (low nibble first), colour block always in four colour mode', () => {
  const alpha = [0x50, 0xfa, 0xff, 0xff, 0, 0, 0, 0]; // row 0: 0, 5, 10, 15; row 1: 15 x 4; rows 2, 3: 0
  const block = Uint8Array.from([...alpha, ...colorBlock(BLUE, RED, [row(0, 1, 2, 3), 0, 0, 0])]);
  const px = convertToRGBA8('DXT3', 4, 4, block);
  assert.deepEqual([0, 1, 2, 3].map((i) => texel(px, i)[3]), [0, 85, 170, 255]);
  assert.deepEqual(texel(px, 4).slice(3), [255]);
  assert.deepEqual(texel(px, 8).slice(3), [0]);
  assert.deepEqual(texel(px, 2).slice(0, 3), [85, 0, 170], 'c0 < c1 but still c2 = (2 c0 + c1) / 3');
});

const alphaBlock = (a0, a1, indices) => {
  let bits = 0n;
  indices.forEach((v, i) => { bits |= BigInt(v) << BigInt(3 * i); });
  return [a0, a1, ...Array.from({ length: 6 }, (_, i) => Number((bits >> BigInt(8 * i)) & 255n))];
};

test('DXT5 alpha with a0 > a1: eight values, (k a0 + (7 - k) a1) / 7 rounded down', () => {
  const block = Uint8Array.from([...alphaBlock(255, 0, [0, 1, 2, 3, 4, 5, 6, 7]), ...colorBlock(WHITE, WHITE, [0, 0, 0, 0])]);
  const px = convertToRGBA8('DXT5', 4, 4, block);
  assert.deepEqual([0, 1, 2, 3, 4, 5, 6, 7].map((i) => texel(px, i)[3]), [255, 0, 218, 182, 145, 109, 72, 36]);
});

test('DXT5 alpha with a0 <= a1: six values and the constants 0 and 255', () => {
  const block = Uint8Array.from([...alphaBlock(10, 200, [0, 1, 2, 3, 4, 5, 6, 7]), ...colorBlock(WHITE, WHITE, [0, 0, 0, 0])]);
  const px = convertToRGBA8('DXT5', 4, 4, block);
  assert.deepEqual([0, 1, 2, 3, 4, 5, 6, 7].map((i) => texel(px, i)[3]), [10, 200, 48, 86, 124, 162, 0, 255]);
});

test('textures that are not a multiple of 4 are cropped from whole blocks; every block is placed in raster order', () => {
  const b0 = colorBlock(RED, RED, [0, 0, 0, 0]);
  const b1 = colorBlock(BLUE, BLUE, [0, 0, 0, 0]);
  const px = convertToRGBA8('DXT1', 6, 2, Uint8Array.from([...b0, ...b1]));
  assert.equal(px.length, 6 * 2 * 4);
  assert.deepEqual(texel(px, 3), [255, 0, 0, 255]);
  assert.deepEqual(texel(px, 4), [0, 0, 255, 255]);
  assert.deepEqual(texel(px, 6 + 5), [0, 0, 255, 255]);
});

test('createTexture accepts compressed data (no pitch) and rejects a short block run', () => {
  const ok = cmd.createTexture(1, 8, 8, 'DXT1', new Uint8Array(32));
  assert.equal(ok.format, 'DXT1');
  assert.throws(() => cmd.createTexture(1, 8, 8, 'DXT1', new Uint8Array(24)), FrameModelError);
});

test('the loader turns a DXT surface record into a compressed texture', () => {
  const d = new DataWriter();
  const blocks = Uint8Array.from(colorBlock(RED, BLUE, [row(0, 1, 2, 3), 0, 0, 0]));
  const ref = d.surfaceFourCC(3, 4, 4, 0x31545844, blocks, 5);
  const vtx = d.vertices(Buffer.alloc(3 * 36), 3);
  const text = ['# ddraw_trace v1', '# data: x',
    `T1 dev7#1.SetTexture(stage=0, surface=surf#3) = DD_OK blob=${ref}`,
    `T1 dev7#1.DrawPrimitive(primitive_type=TRIANGLELIST(4), fvf=0x152, vertices=P, vertex_count=3, flags=0x0) = DD_OK blob={vertices=${vtx}}`,
    'T1 surf#5.Flip(lpDDSurfaceTargetOverride=NULL, dwFlags=0x28) = DD_OK'].join('\n') + '\n';
  const { sequence } = loadTrace(text, new Uint8Array(d.buffer()));
  const c = sequence.frames[0].commands.find((x) => x.op === 'createTexture');
  assert.equal(c.format, 'DXT1');
  assert.deepEqual([c.width, c.height], [4, 4]);
  assert.deepEqual(Array.from(c.bytes), Array.from(blocks));
});
