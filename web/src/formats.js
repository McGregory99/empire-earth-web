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

export const TEX_FORMATS = Object.freeze(Object.keys(FORMATS));

function info(format) {
  const f = FORMATS[format];
  if (!f) throw new Error(`unsupported texture format "${format}" (supported: ${TEX_FORMATS.join(', ')})`);
  return f;
}

export function bytesPerPixel(format) {
  return info(format).bpp;
}

/** Map a DDPIXELFORMAT-like description (as printed in the trace) to a format name. */
export function formatFromPixelFormat({ rgbBitCount, rMask, gMask, bMask, aMask }) {
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
