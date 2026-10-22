// Texture / surface pixel format converters. Pure functions, no WebGL, unit-tested in Node.
//
// The six formats are exactly the ones the original renderer offers through EnumTextureFormats and creates
// surfaces with (tests/ddraw_trace/golden/startup-menu-n60.summary.txt). Names follow the D3D convention
// "channel order from the most significant bit", memory is little-endian.

const FORMATS = {
  RGB555: { bpp: 2, r: 0x7c00, g: 0x03e0, b: 0x001f, a: 0 },
  RGB565: { bpp: 2, r: 0xf800, g: 0x07e0, b: 0x001f, a: 0 },
  // "32-bit RGB": 8 bits unused (X). Alpha is forced to 255.
  XRGB8888: { bpp: 4, r: 0xff0000, g: 0x00ff00, b: 0x0000ff, a: 0 },
  ARGB1555: { bpp: 2, r: 0x7c00, g: 0x03e0, b: 0x001f, a: 0x8000 },
  ARGB4444: { bpp: 2, r: 0x0f00, g: 0x00f0, b: 0x000f, a: 0xf000 },
  // "888A" in the task text: 32-bit with 8-bit alpha, the A8R8G8B8 layout in the summary.
  ARGB8888: { bpp: 4, r: 0xff0000, g: 0x00ff00, b: 0x0000ff, a: 0xff000000 },
};

// Block-compressed formats (DDPF_FOURCC), decoded to RGBA8 on the CPU: 4x4 texel blocks of `block` bytes.
// DXT2 / DXT4 (premultiplied alpha) are not supported.
const COMPRESSED = {
  DXT1: { block: 8, fourcc: 0x31545844 },
  DXT3: { block: 16, fourcc: 0x33545844 },
  DXT5: { block: 16, fourcc: 0x35545844 },
};

export const TEX_FORMATS = Object.freeze([...Object.keys(FORMATS), ...Object.keys(COMPRESSED)]);

export const isCompressed = (format) => format in COMPRESSED;

/** Bytes of a whole texture: block rows x block columns x block size for DXT, tightly packed rows otherwise. */
export function textureByteSize(format, width, height) {
  const c = COMPRESSED[format];
  return c ? Math.ceil(width / 4) * Math.ceil(height / 4) * c.block : width * height * info(format).bpp;
}

function info(format) {
  const f = FORMATS[format];
  if (!f) throw new Error(`unsupported texture format "${format}" (supported: ${TEX_FORMATS.join(', ')})`);
  return f;
}

export function bytesPerPixel(format) {
  return info(format).bpp;
}

/** Map a DDPIXELFORMAT-like description (as printed in the trace) to a format name. */
export function formatFromPixelFormat({ rgbBitCount, rMask, gMask, bMask, aMask, fourcc }) {
  if (fourcc) {
    const hit = Object.entries(COMPRESSED).find(([, c]) => c.fourcc === fourcc >>> 0);
    if (!hit) throw new Error(`unsupported fourcc 0x${(fourcc >>> 0).toString(16)} (DXT1, DXT3 and DXT5 only; DXT2 / DXT4 premultiplied alpha is not supported)`);
    return hit[0];
  }
  const a = aMask >>> 0;
  for (const [name, f] of Object.entries(FORMATS)) {
    if (f.bpp * 8 === rgbBitCount && f.r === rMask && f.g === gMask && f.b === bMask && f.a === a) return name;
  }
  throw new Error(`unsupported pixel format: ${rgbBitCount} bpp R=${rMask.toString(16)} G=${gMask.toString(16)} ` +
    `B=${bMask.toString(16)} A=${a.toString(16)}`);
}

// Per-channel decoder: mask -> {shift, max}. Expansion to 8 bits is round(v * 255 / max), which equals bit
// replication for 4, 5 and 6 bit channels.
function channel(mask) {
  if (!mask) return null;
  let shift = 0;
  while (!((mask >>> shift) & 1)) shift++;
  let bits = 0;
  while ((mask >>> (shift + bits)) & 1) bits++;
  const max = (2 ** bits) - 1;
  const lut = new Uint8Array(max + 1);
  for (let v = 0; v <= max; v++) lut[v] = Math.round((v * 255) / max);
  return { mask, shift, lut };
}

const decoderCache = new Map();
function decoder(format) {
  let d = decoderCache.get(format);
  if (!d) {
    const f = info(format);
    d = { bpp: f.bpp, r: channel(f.r), g: channel(f.g), b: channel(f.b), a: channel(f.a), colorMask: (f.r | f.g | f.b) >>> 0 };
    decoderCache.set(format, d);
  }
  return d;
}

/**
 * Convert pixel data to tightly packed RGBA8.
 * @param {string} format one of TEX_FORMATS
 * @param {number} width
 * @param {number} height
 * @param {Uint8Array} bytes source rows, `pitch` bytes apart (default width * bytesPerPixel)
 * @param {{pitch?: number, colorKey?: {low: number, high: number}|null}} [opts]
 *   colorKey: DirectDraw source colour key on the raw pixel value, inclusive range. Only the colour bits take
 *   part in the comparison (alpha / unused bits ignored). Matching texels get alpha 0, their RGB is kept.
 * @returns {Uint8Array} width * height * 4 bytes
 */
export function convertToRGBA8(format, width, height, bytes, opts = {}) {
  if (isCompressed(format)) return decodeBlocks(format, width, height, bytes); // colour keys do not apply to compressed formats
  const d = decoder(format);
  const pitch = opts.pitch ?? width * d.bpp;
  const need = pitch * (height - 1) + width * d.bpp;
  if (height > 0 && bytes.length < need) {
    throw new Error(`${format} ${width}x${height} pitch ${pitch} needs ${need} bytes, got ${bytes.length}`);
  }
  const key = opts.colorKey ?? null;
  const out = new Uint8Array(width * height * 4);
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let o = 0;
  for (let y = 0; y < height; y++) {
    let p = y * pitch;
    for (let x = 0; x < width; x++, p += d.bpp, o += 4) {
      const raw = d.bpp === 2 ? dv.getUint16(p, true) : dv.getUint32(p, true);
      out[o] = d.r.lut[(raw & d.r.mask) >>> d.r.shift >>> 0];
      out[o + 1] = d.g.lut[(raw & d.g.mask) >>> d.g.shift >>> 0];
      out[o + 2] = d.b.lut[(raw & d.b.mask) >>> d.b.shift >>> 0];
      out[o + 3] = d.a ? d.a.lut[((raw & d.a.mask) >>> 0) >>> d.a.shift] : 255;
      if (key) {
        const c = (raw & d.colorMask) >>> 0;
        if (c >= key.low && c <= key.high) out[o + 3] = 0;
      }
    }
  }
  return out;
}

// ---- DXT decoding ------------------------------------------------------------------------------------------------------
const expand565 = (v) => [((v >> 11) & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31].map(Math.round);
const third = (a, b) => a.map((v, i) => Math.floor((2 * v + b[i]) / 3)); // (2 a + b) / 3, rounded down like the DirectX documentation

/** The four colours of a DXT colour block (8 bytes at `o`). fourColor forces the c0 > c1 interpolation (DXT3 / DXT5). */
function colorTable(bytes, o, fourColor) {
  const v0 = bytes[o] | (bytes[o + 1] << 8);
  const v1 = bytes[o + 2] | (bytes[o + 3] << 8);
  const c0 = expand565(v0), c1 = expand565(v1);
  if (fourColor || v0 > v1) return [[...c0, 255], [...c1, 255], [...third(c0, c1), 255], [...third(c1, c0), 255]];
  return [[...c0, 255], [...c1, 255], [...c0.map((v, i) => Math.floor((v + c1[i]) / 2)), 255], [0, 0, 0, 0]];
}

/** Alpha table of a DXT5 block: 8 values (a0 > a1) or 6 values plus 0 and 255. */
function alpha5Table(a0, a1) {
  const t = [a0, a1];
  if (a0 > a1) for (let k = 1; k <= 6; k++) t.push(Math.floor(((7 - k) * a0 + k * a1) / 7));
  else { for (let k = 1; k <= 4; k++) t.push(Math.floor(((5 - k) * a0 + k * a1) / 5)); t.push(0, 255); }
  return t;
}

function decodeBlocks(format, width, height, bytes) {
  const need = textureByteSize(format, width, height);
  if (bytes.length < need) throw new Error(`${format} ${width}x${height} needs ${need} bytes, got ${bytes.length}`);
  const bw = Math.ceil(width / 4), bh = Math.ceil(height / 4);
  const size = COMPRESSED[format].block;
  const out = new Uint8Array(width * height * 4);
  for (let by = 0; by < bh; by++) {
    for (let bx = 0; bx < bw; bx++) {
      const o = (by * bw + bx) * size;
      const co = format === 'DXT1' ? o : o + 8;
      const colors = colorTable(bytes, co, format !== 'DXT1');
      const alphas = format === 'DXT5' ? alpha5Table(bytes[o], bytes[o + 1]) : null;
      let abits = 0n;
      if (alphas) for (let i = 0; i < 6; i++) abits |= BigInt(bytes[o + 2 + i]) << BigInt(8 * i);
      for (let ty = 0; ty < 4; ty++) {
        const idxRow = bytes[co + 4 + ty];
        for (let tx = 0; tx < 4; tx++) {
          const x = bx * 4 + tx, y = by * 4 + ty;
          if (x >= width || y >= height) continue;
          const c = colors[(idxRow >> (2 * tx)) & 3];
          let a = c[3];
          if (format === 'DXT3') a = ((bytes[o + ty * 2 + (tx >> 1)] >> (4 * (tx & 1))) & 15) * 17;
          else if (alphas) a = alphas[Number((abits >> BigInt(3 * (ty * 4 + tx))) & 7n)];
          out.set([c[0], c[1], c[2], a], (y * width + x) * 4);
        }
      }
    }
  }
  return out;
}
