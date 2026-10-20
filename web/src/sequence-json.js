// JSON (de)serialisation of a Sequence, so the page can load a recorded sequence from a file picker today.
// Typed arrays become {"$t": "Uint8Array"|"Uint16Array"|"Float32Array", "b64": "..."}.

import { validateSequence } from './frame-model.js';

const TYPES = { Uint8Array, Uint16Array, Float32Array };

function toB64(u8) {
  let s = '';
  for (let i = 0; i < u8.length; i += 0x8000) s += String.fromCharCode(...u8.subarray(i, i + 0x8000));
  return btoa(s);
}

function fromB64(b64) {
  const s = atob(b64);
  const u8 = new Uint8Array(s.length);
  for (let i = 0; i < s.length; i++) u8[i] = s.charCodeAt(i);
  return u8;
}

export function encodeSequence(seq) {
  return JSON.stringify(seq, (_k, v) => {
    for (const [name, T] of Object.entries(TYPES)) {
      if (v instanceof T) return { $t: name, b64: toB64(new Uint8Array(v.buffer, v.byteOffset, v.byteLength)) };
    }
    return v;
  });
}

export function decodeSequence(text) {
  const seq = JSON.parse(text, (_k, v) => {
    if (v && typeof v === 'object' && v.$t) {
      const T = TYPES[v.$t];
      if (!T) throw new Error(`unknown typed array "${v.$t}"`);
      const u8 = fromB64(v.b64);
      return new T(u8.buffer, 0, u8.length / T.BYTES_PER_ELEMENT);
    }
    return v;
  });
  return validateSequence(seq);
}
