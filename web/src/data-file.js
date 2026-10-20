// Reader for the ddraw_trace binary side file (tools/ddraw_trace/README.md, "Data file"; reference parser
// tools/ddraw_trace/scripts/read_data.py). All integers little endian.

import { TraceError } from './trace-parse.js';

const FILE_HEADER = 16;
const RECORD_HEADER = 96;
const RECORD_MAGIC = 0x31524444; // "DDR1"
export const KIND = Object.freeze({ VERTICES: 1, INDICES: 2, SURFACE: 3 });

export class DataFile {
  /** @param {Uint8Array} bytes */
  constructor(bytes) {
    const magic = String.fromCharCode(...bytes.subarray(0, 8));
    if (bytes.length < FILE_HEADER || magic !== 'DDTDATA1') throw new TraceError('not a ddraw_trace data file (bad magic)');
    this.bytes = bytes;
    this.dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const version = this.dv.getUint32(8, true);
    if (version !== 1) throw new TraceError(`unsupported data file version ${version}`);
    this.cache = new Map();
  }

  /**
   * The record a trace line refers to with `@offset+length`.
   * @returns {{kind: number, seq: number, object: number, params: number[], payload: Uint8Array, hash: bigint}}
   */
  record(offset, length) {
    const hit = this.cache.get(offset);
    if (hit) {
      if (hit.total !== length) throw new TraceError(`data record at ${offset}: line says length ${length}, record is ${hit.total}`);
      return hit;
    }
    if (offset < FILE_HEADER || offset + RECORD_HEADER > this.bytes.length) throw new TraceError(`data offset ${offset} is outside the data file`);
    const dv = this.dv;
    if (dv.getUint32(offset, true) !== RECORD_MAGIC) throw new TraceError(`data offset ${offset} is not the start of a record`);
    const total = dv.getUint32(offset + 8, true);
    const plen = dv.getUint32(offset + 20, true);
    if (total !== length) throw new TraceError(`data record at ${offset}: line says length ${length}, record is ${total}`);
    if (total < RECORD_HEADER + plen || total % 8 || offset + total > this.bytes.length) {
      throw new TraceError(`data record at ${offset} is truncated or inconsistent`);
    }
    const params = [];
    for (let i = 0; i < 16; i++) params.push(dv.getUint32(offset + 32 + i * 4, true));
    const rec = {
      kind: dv.getUint32(offset + 4, true), total, seq: dv.getUint32(offset + 12, true), object: dv.getUint32(offset + 16, true),
      params, payload: this.bytes.subarray(offset + RECORD_HEADER, offset + RECORD_HEADER + plen),
      hash: dv.getBigUint64(offset + 24, true),
    };
    this.cache.set(offset, rec);
    return rec;
  }

  /** Walk every record from the start. Yields {offset, length}. Throws on a malformed file. */
  * offsets() {
    let off = FILE_HEADER;
    while (off < this.bytes.length) {
      if (off + RECORD_HEADER > this.bytes.length) throw new TraceError(`truncated record header at ${off}`);
      const total = this.dv.getUint32(off + 8, true);
      this.record(off, total);
      yield { offset: off, length: total };
      off += total;
    }
  }

  /** Recompute the FNV-1a 64 hash of every payload. Returns the offsets whose hash differs (slow for big files). */
  verifyHashes() {
    const bad = [];
    const P = 0x100000001b3n, M = 0xffffffffffffffffn;
    for (const { offset, length } of this.offsets()) {
      const r = this.record(offset, length);
      let h = 0xcbf29ce484222325n;
      for (const b of r.payload) h = ((h ^ BigInt(b)) * P) & M;
      if (h !== r.hash) bad.push(offset);
    }
    return bad;
  }
}
