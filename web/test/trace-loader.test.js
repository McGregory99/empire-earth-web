import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import zlib from 'node:zlib';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { build, DataWriter, FMT565, FMTX8888, FIXTURE } from './fixtures/build.mjs';
import { parseCall, parseValue, parseBlob, splitTopLevel, ObjRef, TraceError } from '../src/trace-parse.js';
import { DataFile } from '../src/data-file.js';
import { loadTrace, loadFromFiles } from '../src/trace-loader.js';
import { validateSequence } from '../src/frame-model.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const ops = (frame) => frame.commands.map((c) => c.op);
const count = (frame, op) => frame.commands.filter((c) => c.op === op).length;

test('parseValue: numbers, enums, floats, refs, nested structures, matrices', () => {
  assert.equal(parseValue('0xff000000'), 0xff000000);
  assert.equal(parseValue('ZENABLE(7)'), 7);
  assert.equal(parseValue('f32:3f800000'), 1);
  assert.equal(parseValue('NULL'), null);
  const r = parseValue('surf#12');
  assert.ok(r instanceof ObjRef);
  assert.deepEqual([r.iface, r.id], ['surf', 12]);
  assert.deepEqual(parseValue('{0,0,640,480}'), [0, 0, 640, 480]);
  assert.deepEqual(parseValue('[{0,0,2,2}]'), [[0, 0, 2, 2]]);
  assert.deepEqual(parseValue('{a=1, b={c=0x10, d=f32:bf800000}}'), { a: 1, b: { c: 16, d: -1 } });
  assert.deepEqual(parseValue('{f32[16]=3f800000 00000000 bf800000 40000000}'), { 'f32[16]': [1, 0, -1, 2] });
  assert.deepEqual(splitTopLevel('a=1, b={2,3}, c="x,y"'), ['a=1', 'b={2,3}', 'c="x,y"']);
});

test('parseCall: object calls, exports, out arguments, results and suffixes', () => {
  const c = parseCall('T2 dev7#4.DrawIndexedPrimitiveVB(primitive_type=TRIANGLELIST(4), vb=vb#2, start_vertex=0, vertex_count=4, indices=u16[6]:fnv1a64:6089678b29f3e9d6, index_count=6, flags=0x0) = DD_OK blob={vertices=@16+240, indices=@256+104}', 7);
  assert.equal(c.thread, 2);
  assert.equal(c.obj.iface, 'dev7');
  assert.equal(c.obj.id, 4);
  assert.equal(c.method, 'DrawIndexedPrimitiveVB');
  assert.equal(c.args.primitive_type, 4);
  assert.equal(c.args.vertex_count, 4);
  assert.equal(c.args.vb.id, 2);
  assert.equal(c.result, 'DD_OK');
  assert.deepEqual(parseBlob(c.suffix), { vertices: { offset: 16, length: 240 }, indices: { offset: 256, length: 104 } });
  const e = parseCall('T1 DirectDrawCreateEx(lpGuid=NULL, lplpDD->dd7#1, iid=IID_IDirectDraw7, pUnkOuter=NULL) = DD_OK');
  assert.equal(e.obj, null);
  assert.equal(e.outs.lplpDD.id, 1);
  const s = parseCall('T1 surf#7.Unlock(lpSurfaceData=NULL) = DD_OK locked={rect=NULL, bytes=1024, hash=fnv1a64:b0d7679340715c10} blob=@1000+1120');
  assert.deepEqual(parseBlob(s.suffix), { offset: 1000, length: 1120 });
  assert.deepEqual(parseBlob('blob=failed'), { state: 'failed' });
  assert.equal(parseBlob(''), null);
  assert.equal(parseCall('# comment'), null);
  assert.equal(parseCall('T1   cb EnumDisplayModes(lpDDSurfaceDesc={dwSize=124}) = 1'), null);
  assert.throws(() => parseCall('garbage', 9), /trace line 9/);
});

test('DataFile: reads records, rejects bad magic, bad offsets and wrong lengths', () => {
  const d = new DataWriter();
  const ref = d.vertices(Buffer.from(new Uint8Array(72).map((_, i) => i)), 2);
  const [, off, len] = /@(\d+)\+(\d+)/.exec(ref);
  const f = new DataFile(new Uint8Array(d.buffer()));
  const r = f.record(Number(off), Number(len));
  assert.equal(r.kind, 1);
  assert.equal(r.params[0], 0x152);
  assert.equal(r.payload.length, 72);
  assert.equal(r.payload[5], 5);
  assert.deepEqual(f.verifyHashes(), []);
  assert.throws(() => f.record(Number(off), Number(len) + 8), /line says length/);
  assert.throws(() => f.record(17, 8), /not the start of a record|outside/);
  assert.throws(() => new DataFile(new Uint8Array(32)), /bad magic/);
  const bad = new Uint8Array(d.buffer()); bad[16 + 96 + 3] ^= 0xff;
  assert.equal(new DataFile(bad).verifyHashes().length, 1);
});

test('the committed fixture files are what build.mjs produces', () => {
  const { trace, data } = build();
  assert.equal(fs.readFileSync(path.join(here, 'fixtures/synthetic.trace'), 'utf8'), trace);
  assert.ok(fs.readFileSync(path.join(here, 'fixtures/synthetic.data')).equals(data));
});

const load = (opts) => { const f = build(); return loadTrace(f.trace, new Uint8Array(f.data), opts); };

test('synthetic trace: a valid two-frame sequence at the display size', () => {
  const { sequence, stats } = load();
  assert.doesNotThrow(() => validateSequence(sequence));
  assert.equal(sequence.width, 320);
  assert.equal(sequence.height, 240);
  assert.equal(sequence.frames.length, 2);
  assert.equal(stats.presents, 2);
  assert.equal(stats.droppedTrailingCommands, 1, 'the SetRenderState after the last present');
  assert.equal(ops(sequence.frames[0]).at(-1), 'present');
  assert.equal(ops(sequence.frames[1]).at(-1), 'present');
});

test('frame 1: textures are created lazily with the right format, pixels and colour key', () => {
  const { sequence } = load();
  const f1 = sequence.frames[0].commands;
  const tex = f1.find((c) => c.op === 'createTexture' && c.id === 3);
  assert.equal(tex.format, 'RGB565');
  assert.deepEqual([tex.width, tex.height], [4, 4]);
  assert.deepEqual(Array.from(new Uint16Array(tex.bytes.buffer, tex.bytes.byteOffset, 16)), FIXTURE.texture);
  const spr = f1.find((c) => c.op === 'createTexture' && c.id === 4);
  assert.equal(spr.format, 'XRGB8888');
  assert.deepEqual(spr.colorKey, { low: 0xff00ff, high: 0xff00ff });
  assert.equal(f1.filter((c) => c.op === 'createTexture').length, 2);
  assert.ok(f1.indexOf(tex) < f1.findIndex((c) => c.op === 'draw'), 'the texture exists before the first draw');
});

test('frame 1: state, clear, content-addressed vertices, draws, blit, fill', () => {
  const { sequence, stats } = load();
  const f1 = sequence.frames[0];
  assert.equal(count(f1, 'setDisplayMode'), 1);
  assert.equal(count(f1, 'setViewport'), 1);
  assert.equal(count(f1, 'setTransform'), 5); // 3 initial + shift + unshift
  const clear = f1.commands.find((c) => c.op === 'clear');
  assert.deepEqual([clear.flags, clear.color, clear.rect], [1, 0xff102030, { x: 0, y: 0, w: 320, h: 240 }]);
  const draws = f1.commands.filter((c) => c.op === 'draw');
  assert.equal(draws.length, 2);
  assert.deepEqual(Array.from(draws[0].indices), [0, 1, 2, 0, 2, 3]);
  assert.equal(draws[0].primitive, 'triangleList');
  assert.equal(draws[0].vertexCount, 4);
  assert.equal(draws[0].startVertex, draws[1].startVertex, 'same record, same slot, although the ring offset differed');
  assert.equal(count(f1, 'uploadVertices'), 1, 'two draws of one record upload once');
  assert.equal(f1.commands.find((c) => c.op === 'uploadVertices').bytes.length, 144);
  const bind = f1.commands.filter((c) => c.op === 'bindTexture');
  assert.deepEqual(bind.map((c) => [c.stage, c.textureId]), [[0, 3], [1, null]]);
  const blit = f1.commands.find((c) => c.op === 'blit2D');
  assert.deepEqual([blit.srcId, blit.srcRect, blit.dstRect, blit.colorKey],
    [4, { x: 0, y: 0, w: 4, h: 4 }, { x: 8, y: 130, w: 32, h: 32 }, { low: 0xff00ff, high: 0xff00ff }]);
  const fill = f1.commands.find((c) => c.op === 'fillRect');
  assert.deepEqual([fill.rect, fill.color], [{ x: 100, y: 130, w: 40, h: 32 }, 0xffffff00]);
  assert.equal(stats.uniqueVertexRecords, 2);
});

test('frame 2: partial Unlock and GDI ReleaseDC update the textures; failed calls are skipped; app-array draw', () => {
  const { sequence, stats } = load();
  const f2 = sequence.frames[1].commands;
  const upd3 = f2.find((c) => c.op === 'updateTexture' && c.id === 3);
  const px = Array.from(new Uint16Array(upd3.bytes.buffer, upd3.bytes.byteOffset, 16));
  const expected = FIXTURE.texture.slice();
  for (const i of [5, 6, 9, 10]) expected[i] = 0xffff;
  assert.deepEqual(px, expected, 'the centre 2x2 rectangle was replaced, the rest kept');
  const upd4 = f2.find((c) => c.op === 'updateTexture' && c.id === 4);
  assert.deepEqual(Array.from(upd4.bytes.slice(0, 4)), [0x00, 0xff, 0x00, 0x00], 'GDI readback replaced the whole sprite');
  assert.equal(f2.filter((c) => c.op === 'clear').length, 1, 'the failing Z-buffer Clear is skipped');
  const draws = f2.filter((c) => c.op === 'draw');
  assert.equal(draws.length, 2);
  assert.deepEqual(Array.from(draws[1].indices), [0, 1, 2], 'non-indexed draw gets sequential indices');
  assert.equal(draws[1].vertexCount, 3);
  assert.equal(f2.filter((c) => c.op === 'blit2D').length, 1, 'the screen-to-texture Blt is ignored, the sprite blit kept');
  assert.equal(stats.ignoredOffscreenBlts, 1);
  assert.equal(stats.skippedFailedCalls, 1);
  assert.equal(f2.at(-1).op, 'present', 'Blt onto the primary without a destination rectangle is a present');
  assert.equal(f2.filter((c) => c.op === 'blit2D' && c.srcId === 2).length, 0, 'screen-to-screen copy is not replayed as a blit');
});

test('commands carry their trace line number', () => {
  const { sequence } = load();
  const c = sequence.frames[0].commands.find((x) => x.op === 'clear');
  const text = build().trace.split('\n');
  assert.match(text[c.line - 1], /dev7#1\.Clear\(/);
});

test('every texture bound at a draw has been created before it', () => {
  const { sequence } = load();
  const made = new Set(); let bound = [null, null];
  for (const f of sequence.frames) for (const c of f.commands) {
    if (c.op === 'createTexture') made.add(c.id);
    if (c.op === 'bindTexture') bound[c.stage] = c.textureId;
    if (c.op === 'draw' && bound[0] !== null) assert.ok(made.has(bound[0]), `texture ${bound[0]} used at line ${c.line}`);
  }
});

function mutate(fn, opts) {
  const f = build();
  return () => loadTrace(fn(f.trace), new Uint8Array(f.data), opts);
}

test('fails loudly: a trace without its data file', () => {
  const f = build();
  assert.throws(() => loadTrace(f.trace, null), /data file/i);
});

test('fails loudly: draws without data, failed blobs, bad record references', () => {
  assert.throws(mutate((t) => t.replace(/ blob=\{vertices=@\d+\+\d+, indices=@\d+\+\d+\}/, ' blob={vertices=none, indices=none}')), TraceError);
  assert.throws(mutate((t) => t.replace(/ blob=\{vertices=@\d+\+\d+, indices=@\d+\+\d+\}/, '')), /without vertex data/i);
  assert.throws(mutate((t) => t.replace(/(Unlock.*) blob=@\d+\+\d+/, '$1 blob=failed')), /blob=failed/);
  assert.throws(mutate((t) => t.replace(/(surf#3\.Unlock.*blob=@\d+\+)\d+/, '$18')), /line says length/);
});

test('fails loudly: unsupported features, with the trace line in the message', () => {
  assert.throws(mutate((t) => t.replace('dwFlags=0x8008000', 'dwFlags=0x8000800')), /DDBLT_DDFX|Blt flag/i);
  assert.throws(mutate((t) => t.replace('dwBPP=32', 'dwBPP=16')), /32-bit/);
  assert.throws(mutate((t) => t.replace('primitive_type=TRIANGLELIST(4), vb=vb#1', 'primitive_type=TRIANGLESTRIP(5), vb=vb#1')), /primitive type 5/);
  assert.throws(mutate((t) => t.replace('T1 dev7#1.SetTexture(stage=0, surface=surf#3) = DD_OK', 'T1 dev7#1.SetRenderTarget(surface=surf#4, flags=0x0) = DD_OK')), /render target/i);
  assert.throws(mutate((t) => t.replace('DrawPrimitive(primitive_type', 'DrawPrimitiveStrided(primitive_type')), /Strided/);
  try {
    mutate((t) => t.replace('primitive_type=TRIANGLELIST(4), vb=vb#1', 'primitive_type=TRIANGLEFAN(6), vb=vb#1'))();
    assert.fail('should throw');
  } catch (e) { assert.match(e.message, /trace line \d+/); }
});

test('a texture used at a draw without any captured pixels is an error', () => {
  const f = build();
  const t = f.trace.replace(/T1 surf#3\.Unlock.*\n/, '');
  assert.throws(() => loadTrace(t, new Uint8Array(f.data)), /surf#3|texture 3/);
});

test('device switches reset the device state and replay the shadow of the other device', () => {
  const f = build();
  // insert use of a second device in the middle of frame 1: it must not inherit dev 1 state and dev 1 must get its state back
  const lines = f.trace.split('\n');
  const at = lines.findIndex((l) => l.includes('dev7#1.EndScene')) ;
  lines.splice(at, 0,
    'T2 d3d7#1.CreateDevice(rclsid=IID_IDirect3DHALDevice, surface=surf#2, device->dev7#2) = DD_OK',
    'T2 dev7#2.SetRenderState(dwRenderStateType=CULLMODE(22), dwRenderState=0x3) = DD_OK',
    'T1 dev7#1.SetRenderState(dwRenderStateType=ALPHAREF(24), dwRenderState=0x7) = DD_OK');
  const { sequence, stats } = loadTrace(lines.join('\n'), new Uint8Array(f.data));
  const c = sequence.frames[0].commands;
  const resets = c.map((x, i) => (x.op === 'resetDeviceState' ? i : -1)).filter((i) => i >= 0);
  assert.equal(resets.length, 2, 'dev1 -> dev2 -> dev1');
  assert.equal(stats.deviceSwitches, 2);
  const afterSecond = c.slice(resets[1]);
  assert.ok(afterSecond.some((x) => x.op === 'setRenderState' && x.id === 22 && x.value === 1), 'dev 1 culling restored');
  assert.ok(afterSecond.some((x) => x.op === 'bindTexture' && x.stage === 0 && x.textureId === 3), 'dev 1 texture binding restored');
  const between = c.slice(resets[0], resets[1]);
  assert.ok(between.some((x) => x.op === 'setRenderState' && x.id === 22 && x.value === 3));
  assert.ok(!between.some((x) => x.op === 'bindTexture' && x.textureId === 3), 'dev 2 has no texture bound');
});

test('golden trace (call metadata only, no data): parses completely, draws and pixel updates are skipped and counted', () => {
  const golden = path.join(here, '../../tests/ddraw_trace/golden/startup-menu-n60.trace.gz');
  if (!fs.existsSync(golden)) return;
  const text = zlib.gunzipSync(fs.readFileSync(golden)).toString('utf8');
  const { sequence, stats } = loadTrace(text, null, { allowMissingData: true });
  assert.equal(stats.presents, 60);
  assert.equal(sequence.frames.length, 60);
  assert.equal(stats.skippedDraws, 1593);
  assert.equal(stats.skippedFailedCalls, 59, 'the Z-buffer Clears');
  assert.equal(sequence.width, 640);
  assert.equal(stats.displayModes.join(' '), '640x480 1024x768');
  assert.doesNotThrow(() => validateSequence(sequence));
  assert.throws(() => loadTrace(text, null), /DDT_DATA|data file|vertex data/i);
});

test('loadFromFiles: picks trace and data by name, accepts .gz, and tells what is missing', async () => {
  const f = build();
  const files = [{ name: 'run.trace.gz', bytes: new Uint8Array(zlib.gzipSync(f.trace)) }, { name: 'run.trace.data', bytes: new Uint8Array(f.data) }];
  const r = await loadFromFiles(files);
  assert.equal(r.sequence.frames.length, 2);
  const r2 = await loadFromFiles([{ name: 'a.trace', bytes: new TextEncoder().encode(f.trace) }, { name: 'a.data.gz', bytes: new Uint8Array(zlib.gzipSync(f.data)) }]);
  assert.equal(r2.sequence.frames.length, 2);
  await assert.rejects(loadFromFiles([{ name: 'a.data', bytes: new Uint8Array(f.data) }]), /trace/i);
  await assert.rejects(loadFromFiles([{ name: 'a.trace', bytes: new TextEncoder().encode(f.trace) }]), /\.data/);
});

test('GDI read-back of a screen surface becomes a gdiOverlay of its snapshot (stored once, referenced twice)', () => {
  const f = build({ gdi: true });
  const { sequence, stats } = loadTrace(f.trace, new Uint8Array(f.data));
  const f2 = sequence.frames[1].commands;
  const overlays = f2.filter((c) => c.op === 'gdiOverlay');
  assert.equal(overlays.length, 2);
  assert.equal(overlays[0].srcId, 2);
  const creates = f2.filter((c) => c.op === 'createTexture' && c.id === 2);
  assert.equal(creates.length, 1, 'the snapshot texture is created once');
  assert.equal(creates[0].format, 'XRGB8888');
  assert.equal(f2.filter((c) => c.op === 'updateTexture' && c.id === 2).length, 0, 'the identical second record is not uploaded again');
  assert.ok(f2.indexOf(creates[0]) < f2.indexOf(overlays[0]));
  assert.equal(stats.gdiOverlays, 2);
  assert.equal(sequence.frames[0].commands.filter((c) => c.op === 'gdiOverlay').length, 0);
});

test('GDI read-back of a non-screen surface stays a plain texture update (no overlay)', () => {
  const { sequence } = load();
  assert.equal(sequence.frames.flatMap((f) => f.commands).filter((c) => c.op === 'gdiOverlay').length, 0);
});

// A capture armed late has no Lock/Unlock for textures filled before the window: the arming stores each one once and names it
// in a "# snapshot surf#N blob=..." comment line (tools/ddraw_trace/README.md, "Trace lines").
function withSnapshot(blobFor) {
  const f = build();
  const lines = f.trace.split('\n');
  const lock = lines.findIndex((l) => /^T1 surf#3\.Lock\(/.test(l));
  const unlock = lines.findIndex((l, i) => i > lock && /^T1 surf#3\.Unlock\(/.test(l));
  const ref = lines[unlock].match(/blob=(@\d+\+\d+)/)[1];
  lines.splice(lock, unlock - lock + 1, '# armed', `# snapshot surf#3 blob=${blobFor(ref)}`);
  return { text: lines.join('\n'), data: new Uint8Array(f.data), original: f };
}

test('a "# snapshot surf#N blob=@O+L" line gives the texture its pixels, as an Unlock would', () => {
  const s = withSnapshot((ref) => ref);
  const { sequence } = loadTrace(s.text, s.data);
  const ref = loadTrace(s.original.trace, s.data).sequence;
  const create = (seq) => seq.frames[0].commands.find((c) => c.op === 'createTexture' && c.id === 3);
  assert.ok(create(sequence), 'texture 3 is created');
  assert.deepEqual(create(sequence), { ...create(ref), line: create(sequence).line });
});

test('a snapshot that could not be read is not an error until the texture is used', () => {
  const s = withSnapshot(() => 'unavailable');
  assert.throws(() => loadTrace(s.text, s.data), /texture surf#3 is used but the trace carries no pixels/);
  const unused = s.text.replace(/^T1 dev7#1\.SetTexture\(stage=0, surface=surf#3\).*$/gm, 'T1 dev7#1.SetTexture(stage=0, surface=NULL) = DD_OK');
  assert.doesNotThrow(() => loadTrace(unused, s.data));
});

test('a failed snapshot fails the load loudly', () => {
  const s = withSnapshot(() => 'failed');
  assert.throws(() => loadTrace(s.text, s.data), /surf#3: blob=failed/);
});

test('a snapshot line that names another surface than its record is rejected', () => {
  const s = withSnapshot((ref) => ref);
  assert.throws(() => loadTrace(s.text.replace('# snapshot surf#3', '# snapshot surf#4'), s.data), /belongs to surf#3, not surf#4/);
});
