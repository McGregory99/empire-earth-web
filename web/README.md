# web/: browser replay viewer for recorded original-game runs

A static, dependency-free page (ES modules, WebGL2, no bundler) that replays a recorded run of the original
renderer. It never runs the game and no game asset is committed or fetched: the built-in demo is synthetic, and
local files are read in the browser only.

```
make serve-web      # http://localhost:8000/   (python3 -m http.server on web/)
make test-web       # Node unit tests + headless pixel check
```

Open the page through the local server. Opening `index.html` from `file://` does not work in Chromium (ES modules
are blocked on `null` origins; verified); the page says so. The file picker and drag and drop read local files.
The picker accepts several files at once: a `ddraw_trace` text trace (`.trace`, `.txt`, optionally `.gz`) **together
with its `.data` side file** (optionally `.gz`), or a single recorded-sequence `.json` (`src/sequence-json.js`). To try
it without game data, pick `test/fixtures/synthetic.trace` and `test/fixtures/synthetic.data` (synthetic, regenerate with
`node web/test/fixtures/build.mjs`). Captures hold the game's own textures and models: keep them outside the repository.

## Trace loader (`src/trace-loader.js`, `trace-parse.js`, `data-file.js`)

`loadTrace(text, dataBytes)` returns `{sequence, stats}`; `loadFromFiles([{name, bytes}])` picks trace and data by name and
gunzips. Every command carries the `line` of the trace line it came from, and renderer errors name it.

Parsed: `SetDisplayMode` (32-bit only), `CreateSurface`, `GetAttachedSurface`, `CreateDevice`, `SetRenderTarget`,
`SetRenderState`, `SetTextureStageState`, `SetTransform`, `SetViewport`, `SetMaterial`, `SetLight` (directional), `LightEnable`,
`SetTexture`, `Clear`, the four `Draw*Primitive*` calls, `surf.Unlock`, `ReleaseDC`, `Blt`, `BltFast`, `SetColorKey`, `Flip`.
Everything else is counted in `stats.ignoredCalls` and skipped. Calls that failed (`DDERR_*`/`D3DERR_*`, e.g. the golden's 59
Z-buffer Clears) are skipped and counted.

* Frames end at a present: a successful `Flip`, or a `Blt` onto the primary with a source and no destination rectangle (the
  proxy's own definition). Commands after the last present are dropped (`stats.droppedTrailingCommands`).
* Screen surfaces (primary, back buffers reached by `GetAttachedSurface`, 3D-device surfaces) are one canvas. A `Blt`/`BltFast`
  onto them becomes `blit2D` or `fillRect`; a screen-to-screen present copy is just the present. A `Blt` onto any other surface
  is not replayed (`stats.ignoredOffscreenBlts`): the data file stores the pixels of every surface at its next use as a source or
  texture, so the result arrives as a pixel record.
* Surface pixels: records (`Unlock` rectangle, `ReleaseDC` GDI read-back, `Blt` source, `SetTexture`) are applied to a CPU copy
  of the surface, and `createTexture`/`updateTexture` (whole surface, tight rows) is emitted lazily when the surface is bound at a
  draw or used as a blit source. Mip children and never-used surfaces cost nothing. Surface colour keys come from
  `CreateSurface` and `SetColorKey`; `KEYSRC` and `KEYSRCOVERRIDE` blits carry them.
* Geometry: each draw carries its exact vertex range, so ring-buffer locks and DISCARD/NOOVERWRITE are irrelevant. Records are
  content addressed: a vertex record referenced by several draws is uploaded once and every draw uses the same `startVertex`.
  Non-indexed draws get sequential indices.
* GDI text: the original draws all menu text with GDI straight onto the 3D-device surface (`GetDC`/`ReleaseDC`), and the capture
  stores that whole surface at `ReleaseDC`. A `ReleaseDC` on a screen surface becomes `gdiOverlay`: only snapshot pixels that
  differ from our own render by more than 24 are copied, so the 3D output stays ours and only the text layer comes from data
  (`renderer.stats.gdiPixels` counts the copied pixels). An identical record referenced again is not uploaded again.
* Several Direct3D devices (the game has one per resolution phase): each device has a state shadow; when the trace switches
  device the loader emits `resetDeviceState` and replays that device's shadow.

Fails loudly (a `TraceError` naming the trace line): a trace that references a data file with none given; `blob=failed`/
`unavailable`; draws or textures without data; `vertices=none`; any vertex format other than FVF 0x152/36 bytes; primitive types
other than triangle and line lists; strided draws, `ProcessVertices`; non-32-bit display modes; `SetRenderTarget` on an offscreen
surface; `Blt` flags other than wait, async, colour fill and source colour keys (DDFX, ROP, destination keys, alpha); screen-to-screen
copies other than presents; colour keys other than `DDCKEY_SRCBLT`; non-directional lights; block-compressed or palettised
surfaces. Unsupported render or texture-stage state is rejected by the renderer when the command is applied.
Not replayed: `dev7.Load`/`PreLoad` (the capture does not store their results), palettes, mip data (the renderer generates mips).

## Layout

| File | Role |
| --- | --- |
| `src/frame-model.js` | The frame model: command constructors, validation, constants (FVF 0x152, 36-byte vertices) |
| `src/formats.js` | Pixel converters for the six formats of the golden to RGBA8, colour keys (pure) |
| `src/d3d-tables.js` | D3D7 enum to GL translation and the support policy per render / texture-stage state (pure) |
| `src/state.js` | Device state tracker: validates state commands, resolves a draw config (pure) |
| `src/shaders.js`, `src/renderer.js` | GLSL fixed-function emulation and the WebGL2 command executor |
| `src/trace-parse.js`, `data-file.js`, `trace-loader.js` | ddraw_trace text parser, data-file reader, trace to frame-model loader |
| `src/replay.js` | Play / pause / step / seek loop and frame counter (scheduler injected) |
| `src/demo.js` | Synthetic sequence plus its hand-computed probe pixels |
| `test/*.test.js` | Node unit tests; `test/headless-check.mjs` headless Chromium check |

## Frame model

`Sequence = { width, height, frames: [{ commands: [...] }] }`. State is cumulative like a D3D device; a frame ends
with `present`. Commands are plain objects built (and validated) by `cmd.*` in `src/frame-model.js`:

| Constructor | Meaning |
| --- | --- |
| `setDisplayMode(w, h)` | resize the target (SetDisplayMode) |
| `setRenderState(id, value)` | D3DRENDERSTATETYPE id, DWORD value (floats as IEEE bits) |
| `setTextureStageState(stage, id, value)` | stage 0 or 1 |
| `setTransform(type, matrix16)` | WORLD 1, VIEW 2, PROJECTION 3; D3D row-major row-vector layout |
| `setViewport({x,y,width,height,minZ,maxZ})` | top-left origin |
| `setMaterial({diffuse,ambient,specular,emissive,power})` | RGBA float arrays |
| `setLight(i, light\|null)`, `lightEnable(i, on)` | directional lights, at most 4 enabled |
| `bindTexture(stage, id\|null)` | texture or surface id |
| `createTexture(id,w,h,format,bytes,{pitch,colorKey})`, `updateTexture(id,bytes,rect?)`, `setColorKey(id,key)`, `destroyTexture(id)` | formats: `RGB555 RGB565 XRGB8888 ARGB1555 ARGB4444 ARGB8888` (`formatFromPixelFormat` maps a DDPIXELFORMAT) |
| `uploadVertices(bytes, offset)` | FVF 0x152 vertices into the vertex buffer |
| `draw({primitive, startVertex, vertexCount, indices})` | `triangleList` / `lineList`, `Uint16Array` indices relative to startVertex |
| `clear(flags, color, z, rect?)`, `fillRect(rect, color)` | D3DCLEAR TARGET 1 / ZBUFFER 2 |
| `blit2D(srcId, srcRect, dstRect, colorKey)` | Blt, nearest, optional colour key |
| `gdiOverlay(srcId, threshold)` | GDI drew on the screen surface and the capture stored it as `srcId`: copy the snapshot pixels that differ from the current render by more than `threshold` (the text), keep the rest |
| `resetDeviceState()` | back to D3D7 defaults, no textures bound (the trace switched device) |
| `present()` | frame boundary |

## Pixel conventions

D3D (and the D3D9-based stack the game ran on) places pixel centres on integer coordinates, so the game's 2D quads use vertices
at `-0.5` offsets. The vertex shader shifts every screen position by half a pixel (`uHalfPixel`) so those quads map texels to
pixels exactly (headless test: a 1:1 point-sampled quad reproduces its texture pixel for pixel). Against the real startup-to-menu
capture this took the share of pixels differing by more than 24 from the game's own surface from 10.0% to 1.1% (the rest was the
GDI text and the cursor); with the GDI overlay the mean difference to the reference surface is 0.04 per channel.

## Supported (everything the golden startup-menu summary uses)

Vertex format 0x152; indexed triangle and line lists; Z buffer (enable, write, func, clear value, viewport depth
range); cull modes; alpha blend (all D3DBLEND factors); alpha test (all compare functions); linear, exp and exp2
fog: vertex fog (`FOGVERTEXMODE`, view-space depth, or the distance from the eye with `RANGEFOGENABLE`) and table / pixel
fog (`FOGTABLEMODE`, wins over vertex fog like in D3D7, evaluated per pixel on the depth-buffer value, so `FOGSTART`/`FOGEND`
are in 0..1 depth units; the W-fog variant of devices that offer it is not emulated); fixed-function lighting with up to 4 directional lights, global ambient, material
colour sources (`COLORVERTEX`); stage 0 colour/alpha ops SELECTARG1/2, MODULATE(2X/4X), ADD, ADDSIGNED(2X),
SUBTRACT, BLENDDIFFUSE/TEXTURE/FACTORALPHA with DIFFUSE / CURRENT / TEXTURE / TFACTOR arguments; point/linear
min, mag and mip filters, wrap / mirror / clamp, mip LOD bias; texture colour keys; 2D blits and fills.

## Explicitly unsupported (throws `UnsupportedError` when the command is applied or at the draw)

Wireframe / point fill, flat shading, specular, stencil, w-buffer, ZBIAS,
user clip planes, vertex blending, texture transforms, texcoord index other than 0, texture stage 1 enabled,
texture argument modifiers, ADDSMOOTH / bump / dot3 ops, BORDER addressing, point / spot lights, DXT formats, any
render or texture-stage state not listed in `src/d3d-tables.js`. Approximations to know about: mip levels are
generated from level 0 (the trace carries none); a stage-0 `TEXTURE` argument with no texture bound is opaque
white; lighting transforms normals by the world-view matrix without an inverse transpose (exact for uniform
scale); `MIPMAPLODBIAS` is ignored by default (the reference game under Wine / macOS GL does not apply it; `new Renderer(canvas, { applyLodBias: true })` applies it); the canvas has no destination alpha; a keyed `blit2D` also skips source texels of alpha 0; dithering is
ignored.

## Late-armed captures

A capture armed after the game started (`# armed` in the header) never sees the `SetDisplayMode` / `CreateSurface` calls of the screen or
the device-creation render states. The loader infers: the surface that GDI reads back whole (`ReleaseDC` with a pixel record) and that
the trace never creates is the screen, and its size is the canvas size; the render states the game sets once at device creation
(ALPHAFUNC GREATER, NORMALIZENORMALS, SHADEMODE, ...; not fog) seed the device before the capture's own calls. A colour-keyed blit whose
key was set before the window (the software cursor) is skipped and counted (`stats.skippedKeyedBlits`).

## Tests

`make test-web` runs `node --test web/test/*.test.js` (65 tests: formats against hand-computed pixels, tables,
state, replay loop, frame model, demo, JSON round trip, trace parser, data file, loader on the synthetic fixture and on the golden) and `web/test/headless-check.mjs`. The latter serves `web/`
on localhost, opens the page in headless Chromium with software GL, replays the demo and checks 17 pixels computed
by hand from the geometry (blend, depth, fog, light, 565 and 4444 textures, alpha test, colour-keyed blit, fill)
plus the synthetic trace fixture through the real loader and renderer (18 more probe pixels over 2 frames), plus a stored regression image (`test/expected/demo-frame3.rgba.gz`, produced by this renderer, tolerance 2 per
channel; regenerate with `--update` and review). It needs `playwright-core` and a headless Chromium; it looks in
`$PLAYWRIGHT_CORE`, the global `@playwright/cli` install and the Playwright cache (`$EE_BROWSER` overrides the
browser). If none is found it exits 77 and `make` prints SKIPPED. Manual check: `make serve-web`, open the page,
press Play; the last frame must show the shapes the probes describe.
