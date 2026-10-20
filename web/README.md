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
Today only recorded-sequence JSON (`src/sequence-json.js`) is accepted; the loader for the `tools/ddraw_trace` text
trace plus data file (P2.10.1) is pending and must produce the frame model below.

## Layout

| File | Role |
| --- | --- |
| `src/frame-model.js` | The frame model: command constructors, validation, constants (FVF 0x152, 36-byte vertices) |
| `src/formats.js` | Pixel converters for the six formats of the golden to RGBA8, colour keys (pure) |
| `src/d3d-tables.js` | D3D7 enum to GL translation and the support policy per render / texture-stage state (pure) |
| `src/state.js` | Device state tracker: validates state commands, resolves a draw config (pure) |
| `src/shaders.js`, `src/renderer.js` | GLSL fixed-function emulation and the WebGL2 command executor |
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
| `present()` | frame boundary |

## Supported (everything the golden startup-menu summary uses)

Vertex format 0x152; indexed triangle and line lists; Z buffer (enable, write, func, clear value, viewport depth
range); cull modes; alpha blend (all D3DBLEND factors); alpha test (all compare functions); linear, exp and exp2
vertex fog (view-space depth); fixed-function lighting with up to 4 directional lights, global ambient, material
colour sources (`COLORVERTEX`); stage 0 colour/alpha ops SELECTARG1/2, MODULATE(2X/4X), ADD, ADDSIGNED(2X),
SUBTRACT, BLENDDIFFUSE/TEXTURE/FACTORALPHA with DIFFUSE / CURRENT / TEXTURE / TFACTOR arguments; point/linear
min, mag and mip filters, wrap / mirror / clamp, mip LOD bias; texture colour keys; 2D blits and fills.

## Explicitly unsupported (throws `UnsupportedError` when the command is applied or at the draw)

Wireframe / point fill, flat shading, specular, pixel (table) fog and range fog, stencil, w-buffer, ZBIAS,
user clip planes, vertex blending, texture transforms, texcoord index other than 0, texture stage 1 enabled,
texture argument modifiers, ADDSMOOTH / bump / dot3 ops, BORDER addressing, point / spot lights, DXT formats, any
render or texture-stage state not listed in `src/d3d-tables.js`. Approximations to know about: mip levels are
generated from level 0 (the trace carries none); a stage-0 `TEXTURE` argument with no texture bound is opaque
white; lighting transforms normals by the world-view matrix without an inverse transpose (exact for uniform
scale); the canvas has no destination alpha; a keyed `blit2D` also skips source texels of alpha 0; dithering is
ignored.

## Tests

`make test-web` runs `node --test web/test/*.test.js` (47 tests: formats against hand-computed pixels, tables,
state, replay loop, frame model, demo, JSON round trip) and `web/test/headless-check.mjs`. The latter serves `web/`
on localhost, opens the page in headless Chromium with software GL, replays the demo and checks 17 pixels computed
by hand from the geometry (blend, depth, fog, light, 565 and 4444 textures, alpha test, colour-keyed blit, fill)
plus a stored regression image (`test/expected/demo-frame3.rgba.gz`, produced by this renderer, tolerance 2 per
channel; regenerate with `--update` and review). It needs `playwright-core` and a headless Chromium; it looks in
`$PLAYWRIGHT_CORE`, the global `@playwright/cli` install and the Playwright cache (`$EE_BROWSER` overrides the
browser). If none is found it exits 77 and `make` prints SKIPPED. Manual check: `make serve-web`, open the page,
press Play; the last frame must show the shapes the probes describe.
