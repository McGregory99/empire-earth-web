// Page glue: canvas, replay controls, file picker / drag and drop, status line. Exposes window.__ee for tests.

import { Renderer } from './renderer.js';
import { Replayer } from './replay.js';
import { buildDemoSequence } from './demo.js';
import { decodeSequence } from './sequence-json.js';
import { loadFromFiles } from './trace-loader.js';

const $ = (id) => document.getElementById(id);
const canvas = $('screen');
const status = $('status');
const counter = $('counter');
const slider = $('slider');

let renderer;
let replayer;

function show(state) {
  counter.textContent = `frame ${state.frame} / ${state.total}`;
  slider.max = String(state.total);
  slider.value = String(state.frame);
  $('play').textContent = state.playing ? 'Pause' : 'Play';
  if (state.error) status.textContent = `${state.error.name}: ${state.error.message}`;
}

function load(sequence, label) {
  renderer.reset();
  replayer?.pause();
  canvas.width = sequence.width;
  canvas.height = sequence.height;
  replayer = new Replayer(renderer, sequence, { fps: Number($('fps').value) || 30, loop: $('loop').checked, onChange: show });
  window.__ee.replayer = replayer;
  status.textContent = label;
  try { replayer.seek(Math.min(1, replayer.total)); } catch (e) { status.textContent = `${e.name}: ${e.message}`; }
}

/** Files picked or dropped: a recorded-sequence .json, or a ddraw_trace trace (.trace / .gz) together with its .data. */
async function loadFiles(fileList) {
  const files = [...fileList];
  if (!files.length) return;
  status.textContent = `reading ${files.map((f) => f.name).join(', ')} ...`;
  try {
    if (files.length === 1 && /\.json$/i.test(files[0].name)) {
      load(decodeSequence(await files[0].text()), `loaded ${files[0].name}`);
      return;
    }
    const read = await Promise.all(files.map(async (f) => ({ name: f.name, bytes: new Uint8Array(await f.arrayBuffer()) })));
    const { sequence, stats } = await loadFromFiles(read, { inferColorKeys: true });
    load(sequence, `loaded ${files.map((f) => f.name).join(' + ')}: ${stats.frames} frames, ${stats.commands} commands, ` +
      `${stats.textures} textures, ${stats.uniqueVertexRecords} vertex records`);
  } catch (e) {
    status.textContent = `${e.name}: ${e.message}`;
  }
}

try {
  renderer = new Renderer(canvas);
  window.__ee = { renderer, replayer: null, loadFiles, load };
  load(buildDemoSequence(), 'synthetic demo sequence (no game data)');
} catch (e) {
  status.textContent = e.message;
  throw e;
}

$('play').onclick = () => replayer.toggle();
$('step').onclick = () => { replayer.pause(); try { replayer.step(); } catch { /* shown through onChange */ } };
$('restart').onclick = () => { replayer.pause(); try { replayer.seek(0); } catch (e) { status.textContent = e.message; } };
slider.oninput = () => { replayer.pause(); try { replayer.seek(Number(slider.value)); } catch (e) { status.textContent = e.message; } };
$('fps').onchange = () => { replayer.fps = Number($('fps').value) || 30; };
$('loop').onchange = () => { replayer.loop = $('loop').checked; };
$('demo').onclick = () => load(buildDemoSequence(), 'synthetic demo sequence (no game data)');
$('file').onchange = (e) => loadFiles(e.target.files);
addEventListener('dragover', (e) => e.preventDefault());
addEventListener('drop', (e) => { e.preventDefault(); loadFiles(e.dataTransfer.files); });
