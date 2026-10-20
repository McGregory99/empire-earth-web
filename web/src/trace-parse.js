// Parser for the ddraw_trace v1 text format (tools/ddraw_trace/README.md, "Trace format"). Pure, no I/O.

export class TraceError extends Error {
  constructor(message, line = null) {
    super(line === null ? message : `trace line ${line}: ${message}`);
    this.name = 'TraceError';
    this.line = line;
  }
}

/** Interface reference such as surf#12, vb#2, dev7#3. */
export class ObjRef {
  constructor(iface, id) { this.iface = iface; this.id = id; }
}

/** Split `s` at top-level commas (outside {}, [], () and double quotes). */
export function splitTopLevel(s) {
  const out = [];
  let depth = 0, inStr = false, start = 0;
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (inStr) { if (c === '\\') i++; else if (c === '"') inStr = false; continue; }
    if (c === '"') inStr = true;
    else if (c === '{' || c === '[' || c === '(') depth++;
    else if (c === '}' || c === ']' || c === ')') depth--;
    else if (c === ',' && depth === 0) { out.push(s.slice(start, i)); start = i + 1; }
  }
  const last = s.slice(start);
  if (last.trim() !== '' || out.length) out.push(last);
  return out.map((p) => p.trim());
}

const KEYED = /^([A-Za-z_][A-Za-z_0-9]*(?:\[\d+\])?)=([\s\S]*)$/;
const f32 = new Float32Array(1);
const u32 = new Uint32Array(f32.buffer);

/** Parse one value of the format into numbers, arrays, plain objects, ObjRef, null or raw strings. */
export function parseValue(text) {
  const s = text.trim();
  if (s === 'NULL') return null;
  if (s[0] === '{') return parseBraces(s.slice(1, s.lastIndexOf('}')));
  if (s[0] === '[') return splitTopLevel(s.slice(1, s.lastIndexOf(']'))).map(parseValue);
  if (s[0] === '"') return s.slice(1, -1);
  let m;
  if ((m = /^f32:([0-9a-fA-F]{8})$/.exec(s))) { u32[0] = parseInt(m[1], 16); return f32[0]; }
  if (/^-?\d+$/.test(s)) return Number(s);
  if (/^0x[0-9a-fA-F]+$/.test(s)) return Number(s);
  if ((m = /^[A-Za-z_0-9]+\((-?\d+)\)$/.exec(s))) return Number(m[1]); // ZENABLE(7), TRIANGLELIST(4)
  if ((m = /^(\w+)#(\d+)$/.exec(s))) return new ObjRef(m[1], Number(m[2]));
  return s;
}

function parseBraces(inner) {
  const items = splitTopLevel(inner);
  const obj = {};
  const list = [];
  let keyed = false;
  for (const it of items) {
    const m = KEYED.exec(it);
    if (m && !/^0x/i.test(it)) {
      keyed = true;
      if (/^f32\[\d+\]$/.test(m[1])) {
        obj[m[1]] = m[2].trim().split(/\s+/).map((h) => { u32[0] = parseInt(h, 16); return f32[0]; });
      } else obj[m[1]] = parseValue(m[2]);
    } else list.push(parseValue(it));
  }
  return keyed ? obj : list;
}

/**
 * Parse one call line. Returns null for comments, blank lines and callback lines (`T1   cb ...`).
 * { thread, obj: ObjRef|null, method, args: {name: value}, outs: {name: value}, result, suffix }
 *   args: inputs; outs: out arguments (`name->id`, `name=>value`)
 */
export function parseCall(line, lineNo = null) {
  if (!line || line[0] === '#') return null;
  const head = /^T(\d+)\s+/.exec(line);
  if (!head) throw new TraceError(`not a trace line: ${line.slice(0, 60)}`, lineNo);
  const rest = line.slice(head[0].length);
  if (rest.startsWith('cb ')) return null;
  const m = /^(?:(\w+)#(\d+)\.)?(\w+)\(/.exec(rest);
  if (!m) throw new TraceError(`cannot parse call: ${rest.slice(0, 60)}`, lineNo);
  const open = m[0].length - 1;
  let depth = 0, inStr = false, close = -1;
  for (let i = open; i < rest.length; i++) {
    const c = rest[i];
    if (inStr) { if (c === '\\') i++; else if (c === '"') inStr = false; continue; }
    if (c === '"') inStr = true;
    else if (c === '(') depth++;
    else if (c === ')' && --depth === 0) { close = i; break; }
  }
  if (close < 0) throw new TraceError('unbalanced parentheses', lineNo);
  const tail = /^\s*=\s*(\S+)([\s\S]*)$/.exec(rest.slice(close + 1));
  if (!tail) throw new TraceError('missing "= result"', lineNo);
  const args = {};
  const outs = {};
  for (const a of splitTopLevel(rest.slice(open + 1, close))) {
    if (!a) continue;
    const am = /^(\w+)(=>|->|=)([\s\S]*)$/.exec(a);
    if (!am) throw new TraceError(`cannot parse argument "${a.slice(0, 40)}"`, lineNo);
    if (am[2] === '=') args[am[1]] = parseValue(am[3]);
    else outs[am[1]] = am[3].trim() === '-' || am[3].trim() === '?' ? null : parseValue(am[3]);
  }
  return {
    thread: Number(head[1]),
    obj: m[2] ? new ObjRef(m[1], Number(m[2])) : null,
    method: m[3], args, outs, result: tail[1], suffix: tail[2].trim(),
  };
}

/**
 * Parse the ` blob=...` suffix of a call. Returns null when absent, otherwise one of
 * {state:'failed'|'unavailable'}, {offset, length} (surface calls) or {vertices, indices} (draws; each
 * {offset, length}, 'none' or absent).
 */
export function parseBlob(suffix) {
  const i = suffix.indexOf('blob=');
  if (i < 0) return null;
  const b = suffix.slice(i + 5).trim();
  if (b === 'failed' || b === 'unavailable') return { state: b };
  let m;
  if ((m = /^@(\d+)\+(\d+)/.exec(b))) return { offset: Number(m[1]), length: Number(m[2]) };
  if (b[0] === '{') {
    const out = {};
    for (const part of splitTopLevel(b.slice(1, b.indexOf('}')))) {
      const pm = /^(\w+)=(none|@(\d+)\+(\d+))$/.exec(part);
      if (!pm) throw new TraceError(`cannot parse blob part "${part}"`);
      out[pm[1]] = pm[2] === 'none' ? 'none' : { offset: Number(pm[3]), length: Number(pm[4]) };
    }
    return out;
  }
  throw new TraceError(`cannot parse blob "${b.slice(0, 40)}"`);
}

/** The path of the `# data: <file> max=<n>` header line, or null. */
export function dataFileFromHeader(text) {
  const m = /^# data: (.*?) max=\d+\s*$/m.exec(text);
  return m ? m[1] : null;
}
