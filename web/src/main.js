// Page glue: canvas, replay controls, file picker / drag and drop, status line. Exposes window.__ee for tests.

import { Renderer } from './renderer.js';
import { Replayer } from './replay.js';
import { buildDemoSequence } from './demo.js';
import { decodeSequence } from './sequence-json.js';

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

async function loadFile(file) {
  if (!/\.json$/i.test(file.name)) {
    status.textContent = `"${file.name}": only recorded-sequence JSON files can be loaded so far. ` +
      'The loader for the ddraw_trace text trace + data file (P2.10.1) is not implemented yet.';
    return;
  }
  try {
    load(decodeSequence(await file.text()), `loaded ${file.name}`);
  } catch (e) {
    status.textContent = `${file.name}: ${e.message}`;
  }
}

try {
  renderer = new Renderer(canvas);
  window.__ee = { renderer, replayer: null };
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
$('file').onchange = (e) => { if (e.target.files[0]) loadFile(e.target.files[0]); };
addEventListener('dragover', (e) => e.preventDefault());
addEventListener('drop', (e) => { e.preventDefault(); const f = e.dataTransfer.files[0]; if (f) loadFile(f); });
