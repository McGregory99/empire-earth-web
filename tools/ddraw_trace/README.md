# ddraw_trace: a tracing DirectDraw proxy

The oracle for Phase 2. The renderer plugin (`DX7HRTnLDisplay.dll`, selected by the config value
`Rasterizer Name = Direct3D Hardware TnL`; `DX7HRDisplay.dll` is the non-TnL sibling) is the only client of
DirectDraw 7 and Direct3D 7 in the game. We will reimplement it and compare the clone with the original at the
plugin <-> ddraw boundary, call by call. This proxy records that boundary as a normalized text trace.

```
make -C tools/ddraw_trace              # build/ddraw.dll
make -C tools/ddraw_trace test         # unit tests (Wine, test prefix), game-run library tests, normalizer and data reader tests
make -C tools/ddraw_trace check-tables # src/methods.inc and src/names.inc match the SDK headers
make -C tools/ddraw_trace game-trace   # one capture in the real game (takes the game-run lock)
make -C tools/ddraw_trace game-gate    # two captures and their diff: the determinism gate
```

## The ddraw chain

Configured graphics stack: `d7vk` (`~/Library/Application Support/EmpireEarthMac/config.json`). On disk the
game directory holds this chain (the "GOG DX7 to D3D9 wrapper" is the other stack, `gog-d3d9`, and is not in use):

```
before:  plugin -> ddraw.dll          00xJS "ee-ddraw" proxy (rebuilt and copied by the launcher at every launch)
                -> ddraw_eeorig.dll   D7VK (DXVK-Sarek), Direct3D 7 on Direct3D 9
                -> ddraw_.dll         Wine's builtin ddraw (D7VK's "proxied ddraw", copied from syswow64)
                   d3d9.dll           DXVK-Sarek d3d9 -> Vulkan -> MoltenVK -> Metal

after:   plugin -> ddraw.dll          THIS PROXY
                -> ddraw_next.dll     the launcher's ee-ddraw, renamed, byte for byte
                -> ddraw_eeorig.dll -> ddraw_.dll / d3d9.dll      (unchanged)
```

The proxy sits in front, so it records exactly what the plugin sent and received, including the effect of the
00xJS shims (page-flip emulation, display mode answers). A clone is compared in the same position.

### Why the proxy cannot simply be copied to `ddraw.dll`

`launch.sh` runs `apply-launch-patches.sh` on every launch. For `d7vk` (`install_d7vk`) that script rewrites
`ddraw.dll` from `~/Library/Application Support/EmpireEarthMac/patches/win32/ddraw.dll`, which
`install-compat-patches.sh` first rebuilds from `ee-ddraw.c`. It also re-copies `ddraw_eeorig.dll`, `ddraw_.dll`,
`d3d9.dll` and `dxvk.conf`. Anything installed before `launch.sh` is overwritten. Nothing is verified, it is
simply replaced.

### Insertion

`game-trace.sh` runs the unmodified launcher with `EMPIRE_EARTH_WINE` pointing at a generated wrapper, an override the
launcher documents (`find_wine`). The wrapper forwards every call to the real Wine and, only for the call that starts the
game (`wine start /unix <exe>`), runs `scripts/swap-in.sh` first:

1. copy the launcher's `ddraw.dll` to `ddraw_next.dll.tmp` and rename it to `ddraw_next.dll` (it keeps working under another name),
2. copy the proxy to `ddraw.dll.tmp` and rename it over `ddraw.dll` (so the directory is never without a `ddraw.dll`),
   and write `ddraw_trace.cfg` (next module, trace path, present cap).

At that point `apply-launch-patches.sh` has finished, so nothing overwrites the proxy. A retry after an early death (the
launcher retries up to 6 times) swaps again; `swap-in.sh` is idempotent.

The wrapper directory mirrors the Wine root (`tools/game_run/wrapper.sh`): the launcher finds Wine's own `version.dll` at
`<dirname wine>/../lib/wine/i386-windows/version.dll`, puts `<dirname wine>` first on `PATH` and starts
`<dirname wine>/wineserver`. A bare `bin/` made the launcher skip the engine's `version.dll` shim silently, so the first
captures ran a different game than the one the user plays. Now `bin/` links every tool of Wine's `bin/`, the other
top-level entries (`lib`, `share`) are linked too, and the run fails if `launcher.log` says "version.dll shim not
installed" (the sha256 of `version.dll` and `version_eeorig.dll` is logged). The wrapper refuses to swap in or start the
game (exit 97) unless the driver that made it is still alive (pid and start time) and still holds the lock: a launcher
orphaned by a killed driver can neither put the proxy in place nor start the game.

### Why this is safe

The protocol is the shared library `tools/game_run/lib.sh`, used by this driver and by
`tests/dx7_rasterizer/game_check.sh`; `tests/game_run/` tests it against a fake world (fake `ps`, Wine, launcher and game,
a game directory with spaces in its path).

* No file of the 00xJS tree or of the launcher is modified; no launcher script is edited. The wrapper lives in
  `tools/ddraw_trace/build/`.
* The 00xJS proxy does not depend on its own file name: it loads `ddraw_eeorig.dll` from the directory of its own
  module (`GetModuleFileNameA(&__ImageBase)` in `ee-ddraw.c`), and no shim (`version.dll`, `ee-ddraw`) refers to a
  module called `ddraw.dll`. Checked in the sources; confirmed by the captures (the game reaches the menu).
* The game scans every `*.dll` of its folder for plugins at start-up, so an extra DLL is probed. `ddraw_next.dll` is
  the same code that was already probed under the name `ddraw.dll`; our proxy has no start-up side effects (it does
  nothing in `DllMain` but remember its handle).
* The lock is `~/Games/EmpireEarth-re/locks/game-run`, a fixed path (not derived from `RE_DIR`), with an owner token
  (pid and start time) inside. The lock appears with its token already in it (a private directory renamed into place), so
  a run killed at any point leaves either nothing or a complete lock. A live owner is waited for; a lock is reclaimed,
  loudly, only when its owner is provably dead (pid gone, or the pid now belongs to a process that started at another
  time). Reclaiming takes the lock over in place: the dead token is renamed to a claim name of our own (one claimant
  wins), compared with what was judged, and our token is linked in; if what was moved is not the dead owner's token it is
  put back. A lock without a token (another agent's) is never reclaimed; only our own lock is released.
* Detection is the launcher's own anchored test (`game_pids`): the command line must start with a Windows path to
  `Empire Earth.exe` or `EE-AOC.exe`, with real memory resident. An agent's `pgrep -f` loop does not match; Art of
  Conquest (which the launcher kills by name) does. A run also refuses to start while any other program of the GOG install
  runs.
* A run refuses to start over leftovers: `ddraw_next.dll`, `ddraw_trace.cfg`, or the state directory
  `~/Games/EmpireEarth-re/game-run-state` of a run that did not finish. `config.json`'s `base_exe` must be under the
  game directory that is restored. Ambient `EMPIRE_EARTH_*`, `EE_*`, `WINE*` and `DXVK_*` variables are unset, and nothing
  else is read from the environment: the tests name their fake world with `--test-config <file>`, which `recover.sh` does
  not have.
* The snapshot (`cp -c`, else a plain copy after removing any partial one) is verified against the sha256 manifest of
  the directory before the game starts; a mismatch aborts the run.
* The launcher runs as a background job in its own process group; the driver `wait`s on it with the interruptible
  builtin. Traps cover EXIT INT TERM HUP QUIT, cleanup ignores further signals and the interrupted status (130, 143, ...)
  survives.
* Teardown: kill the launcher's group; stop the prefix with its own `wineserver -k` (nothing else is killed) and wait
  for the game to disappear; restore. Restore copies back every file the manifest lists that changed or vanished
  (verifying each hash), deletes only files absent from the manifest, never deletes a file the manifest lists, and fails
  unless the final manifest and directory list equal the recorded ones. Files the launcher or the game rewrite on every
  run (`ddraw.dll` is rebuilt, `ee-ddraw.log`, `ee-version.log`) are reported and restored too.
* If the game cannot be stopped, or the restore fails, the lock and the snapshot are KEPT, the exact recovery commands
  are printed and the run exits non-zero. `tools/game_run/recover.sh` then runs the undo steps, restores from the
  snapshot, verifies the manifest, removes the state and the dead run's lock. The same script finishes the job after a
  `kill -9` of the driver.
* Not restored, because it is outside the game directory: the Wine prefix registry (the launcher re-imports the same
  values every launch; Wine flushes the registry when the server stops), the launcher logs in
  `~/Library/Application Support/EmpireEarthMac/logs/`, and the Art of Conquest directory, which the launcher also rewrites
  (`ddraw.dll`, `version.dll`, ...) on every launch.

## Which calls are traced

Calls are attributed by return address. Every wrapped method is entered through a stdcall thunk; the thunk's
return address is looked up with `GetModuleHandleExA(FROM_ADDRESS)` and `GetModuleFileNameA`, and the call is
traced only if the module's base name is in `DDT_MODULES` (default `DX7HRTnLDisplay.dll,DX7HRDisplay.dll`, matched
case-insensitively). Calls from any other module (the exe, `Low-Level Engine.dll`, the 00xJS shims) pass through
and are not logged. Objects created by untraced callers are wrapped all the same, so identity and ids stay consistent.

## Trace format (v1)

A line-oriented text file, UTF-8/ASCII, `\n` endings. Comment lines start with `#`:

```
# ddraw_trace v1
# modules: DX7HRTnLDisplay.dll,DX7HRDisplay.dll
...
# end: 60 presents          (written when the present cap is reached; a <trace>.done marker file appears)
```

Every other line is one call, written when the call returns:

```
T<n> <object>.<Method>(<arg>, <arg>, ...) = <result>[ <suffix>]
T<n> <Export>(<arg>, ...) = <result>
T<n>   cb <Method>(<arg>, ...) = <result>          callback invoked from inside an enumeration (indented, before the call line)
```

* `T<n>`: logical thread, numbered in order of first traced call (`T1`, `T2`, ...).
* `<object>`: `dd7#1`, `d3d7#1`, `dev7#1`, `surf#12`, `vb#1`: the interface and a stable id assigned in creation order.
  A new object at the address of a released one gets a new id. Interfaces obtained from each other with
  `QueryInterface` retire together, because Wine shares their reference count.
* Arguments are `name=value` with the SDK's parameter names. Values:
  integers decimal or `0x` hex (counts, sizes and indices decimal; flags and masks hex); floats as IEEE bits `f32:3f800000`;
  opaque pointers `P` or `NULL`, never addresses; interface arguments by id (`surf#3`), `raw` for an interface we do not
  wrap; GUIDs by name (`IID_IDirect3D7`) or `{...}`; enumerations by name (`ZENABLE(7)`, `TRIANGLELIST(4)`,
  `COLOROP(1)`); strings quoted.
* Out arguments after the call: `name->id` (interface out), `name=>value` (scalar or structure out; `-` if the call
  failed, `?` for timing-dependent calls).
* Structures field by field in `{...}`, flag-gated members only when their flag is set (`DDSURFACEDESC2`); matrices as
  `{f32[16]=...}` (row major). The ROP tables of `DDCAPS` are folded into `rops_hash`.
* `<result>`: the HRESULT by name (`DD_OK`, `DDERR_WASSTILLDRAWING`, `D3DERR_ZBUFFER_NOTPRESENT`) or `0x...`; for
  `AddRef`/`Release` the reference count; for callbacks the returned integer.
* Suffix at `Unlock`: ` locked={rect=NULL|{l,t,r,b}, bytes=N, hash=fnv1a64:<16 hex>}` for surfaces (the locked rows
  without pitch padding; block-compressed surfaces as one run) and ` locked={bytes=N, hash=...}` for vertex buffers
  (the whole locked range). The hash is 64-bit FNV-1a of the bytes the plugin wrote, taken before the real `Unlock`.
* Draw calls hash the plugin-owned arrays they carry: `indices=u16[6]:fnv1a64:<hash>` (index array, `index_count` x 2 bytes)
  and `vertices=fvf[3x36]:fnv1a64:<hash>` (application vertices, `vertex_count` x the FVF's vertex size).
* Timing-dependent calls (`GetFlipStatus`, `GetBltStatus`, `WaitForVerticalBlank`, `GetScanLine`,
  `GetVerticalBlankStatus`) are written as `... = ? [timing]` with masked results, and any call that returns
  `DDERR_WASSTILLDRAWING` (the caller retries until the driver is ready) as `... = DDERR_WASSTILLDRAWING [timing]`. A run of
  identical timing lines collapses to one, per thread: another thread's lines in between do not break it (polling and
  retry loops have no reproducible length).
* A "present" is a successful `IDirectDrawSurface7::Flip`, or a successful `Blt` onto a surface created with
  `DDSCAPS_PRIMARYSURFACE` that has a source surface and no destination rectangle (a whole-surface copy; sprite blits, colour
  fills and `BltFast` are not frames; a call that reports `DDERR_WASSTILLDRAWING` was not a frame). After `DDT_MAX_PRESENTS`
  of them the trace gets its end marker and stops; the game keeps running untraced.
* Input structures of `Get*` methods (the `DDSCAPS2` asked for by `GetAttachedSurface` and `GetAvailableVidMem`) are printed
  as inputs (`name={...}`), outputs as `name=>{...}`.

## Configuration

`ddraw_trace.cfg` next to the proxy (`KEY=VALUE` lines), overridden by environment variables of the same name:

| key | meaning | default |
| --- | --- | --- |
| `DDT_NEXT` | module the proxy forwards to (relative: next to the proxy) | `ddraw_next.dll` |
| `DDT_TRACE` | trace file (Windows path); empty: pure pass-through, nothing is logged | empty |
| `DDT_MODULES` | comma separated base names whose calls are traced | the two renderer plugins |
| `DDT_MAX_PRESENTS` | present cap, 0 = unlimited | 0 |
| `DDT_FLUSH` | `1`: flush after every line (the unit tests) | 0 |
| `DDT_DATA` | path prefix of the optional data file `<DDT_DATA>.data` (see "Data file"); empty: no data is captured and the trace is exactly what it was without the feature | empty |
| `DDT_DATA_MAX` | cap on the data file in bytes (at most 4,294,967,040); when the next record would pass it, the capture fails loudly | 536,870,912 |
| `DDT_ARM_FILE` | nothing is traced, and no present counted, until this file exists; the present that first sees it is the last outside the trace, the window opens after it and the trace gets a `# armed` line. Object ids are assigned all the same. Used by the scenarios that capture a settled state | empty |

## How the wrapping works

`gen_tables.py` reads the interface declarations in the mingw-w64 `ddraw.h` and `d3d.h` and writes
`src/methods.inc` (slot order, argument count, SDK types and names of the 145 methods of `IDirectDraw7`,
`IDirect3D7`, `IDirect3DDevice7`, `IDirectDrawSurface7`, `IDirect3DVertexBuffer7`) and `src/names.inc` (HRESULT names
and enumerations). The argument count decides how many bytes a stdcall thunk pops, so it is generated, and
`make check-tables` regenerates and compares. Each slot gets a thunk with the exact arity; `dispatch()` unwraps
interface arguments, calls the real method with the right number of arguments, wraps interface results (including
`QueryInterface` by IID, `GetDDInterface`, enumeration callbacks that hand out surfaces) and writes the line.
Interfaces that are not wrapped (`IDirectDrawClipper`, `IDirectDrawPalette`, other `IDirectDraw*` versions) are handed
out as the real objects and appear as `raw`; they work, but calls on them are not traced. Exports other than
`DirectDrawCreateEx`, `DirectDrawEnumerateA` and `DirectDrawEnumerateExA` are forwarded by a tail jump, untraced.

## Data file (optional, `DDT_DATA`)

The text trace only hashes what the plugin writes (surface pixels, vertex buffers, index and vertex arrays). With `DDT_DATA`
set, the proxy also stores those bytes in a binary side file, `<DDT_DATA>.data` (use the trace path as the prefix to get
`<trace>.data`), so that a replayer can rebuild textures, 2D surfaces and geometry. Off by default: without `DDT_DATA` no
file is made and the trace is byte-identical to a trace of the same run with the feature absent (the unit tests compare a run
with and without it). With it on, the trace differs only by the `# data: <file> max=<n>` header line and the ` blob=...`
suffixes below, which are always the last text of a line (strip from ` blob=` to remove them).

In the real game, `game-trace.sh --data [--out-dir <dir>]` does this: the launcher scrubs the environment, so `swap-in.sh`
writes `DDT_DATA=<raw trace path>` (and `DDT_DATA_MAX` when the caller's environment has it) into `ddraw_trace.cfg`. The run
keeps the data as `trace.<tag>.data` next to `trace.<tag>.txt`; it fails if the data file was not written. `--out-dir` sends all
outputs to a folder outside the repository, e.g. `~/Games/EmpireEarth-re/captures/replay/<name>/`.

**The data contains the game's textures and models, the user's own copy's content. Never commit it.** Captures go to
`~/Games/EmpireEarth-re/`, outside the repository (`.gitignore` also refuses `*.data` next to the tool and its tests). Tests
use synthetic data from the mock ddraw only.

### Layout (all integers little endian)

```
file   := file_header record*
file_header (16 bytes):  "DDTDATA1" (8 bytes) | version u32 = 1 | reserved u32 = 0
record := record_header (96 bytes) payload pad
record_header:
    0  u32  magic       0x31524444 (bytes "DDR1")
    4  u32  kind        1 vertices, 2 indices, 3 surface
    8  u32  total       length of the whole record: 96 + payload_length rounded up to a multiple of 8 (= the L in "@O+L")
   12  u32  seq         0, 1, 2, ... in file order
   16  u32  object      kind 1: the vertex buffer id (vb#N), 0 for an application array; kind 2: 0; kind 3: the surface id (surf#N)
   20  u32  payload_length   payload bytes without padding
   24  u64  payload_hash    FNV-1a 64 of the payload (the same function as the trace's hash=fnv1a64:...)
   32  u32[16] params      kind specific, below; unused entries are 0
payload := payload_length bytes; pad := zero bytes up to total
```

Records follow each other without gaps: the first starts at offset 16, the next at `offset + total`. A trace line refers to a
record by its **start offset** and **total length**: `@<offset>+<length>`. The file ends with a whole record; a capture that
failed (cap) still holds only whole records.

**Content addressing.** A record is stored once: if a record with the same kind, object, params, payload length and payload hash
exists, the new reference points at the old record (so a texture that is rewritten with the same pixels, or a quad drawn from
the same vertices, costs nothing; a surface whose content goes A, B, A refers to A's record again). The key is the 64-bit hash,
a collision is not checked (at about 10^5 records the chance is about 10^-9).

**Kind 1, vertices** (a draw's vertex range). Payload: `vertex_count * stride` bytes, the vertices exactly as the call reads them.
params: `p0` FVF, `p1` stride in bytes, `p2` start_vertex (0 for an application array), `p3` vertex_count, `p4` source (0
vertex buffer, 1 application memory). Vertex buffers: the plugin's ring buffer is locked whole (the pointer is invalid after
`Unlock`, and many draws read different ranges of one lock, DISCARD or NOOVERWRITE), so the proxy keeps a shadow copy of the
buffer, refreshed at every `Unlock`, and a draw stores the exact range `[start_vertex, start_vertex + vertex_count)` of the shadow
at call time. A replayer does not need lock flags or ring offsets: every draw carries the bytes it drew. Draws that read a range
the shadow does not cover (never locked, or out of range) get `vertices=none`.

**Kind 2, indices.** Payload: `index_count` 16-bit indices. params: `p0` index_count.

**Kind 3, surface pixels.** Payload: `p7` rows of `p6` bytes each, packed without the driver's pitch padding, covering the
rectangle `(p0,p1)-(p2,p3)` (left, top, right, bottom, right/bottom exclusive) of the surface; for a block-compressed format
(`p10` fourcc, DXT) one contiguous run (`p7` = 1, `p6` = the run length). params: `p4` surface width, `p5` surface height,
`p8` origin (below), then the pixel format words: `p9` DDPIXELFORMAT flags, `p10` fourCC, `p11` bits per pixel, `p12` red mask,
`p13` green mask, `p14` blue mask, `p15` alpha mask. The payload hash equals the `hash=` of the `Unlock` line for origin 1.

| origin | when | trace line |
| --- | --- | --- |
| 1 | `surf#N.Unlock`, unless the Lock had `DDLOCK_READONLY`: the locked rectangle (whole surface if `lpDestRect` was NULL) | the `Unlock` line |
| 2 | `surf#N.ReleaseDC`: the whole surface, read after the real `ReleaseDC` returned (what GDI drew, e.g. text) | the `ReleaseDC` line |
| 3, 4 | the source of a `Blt` / `BltFast` that an earlier `Blt` / `BltFast` wrote to and that has not been stored since: the whole surface | the `Blt` / `BltFast` line |
| 5 | the texture of a `SetTexture` that an earlier `Blt` / `BltFast` wrote to and that has not been stored since: the whole surface | the `SetTexture` line |

Origins 2 to 5 read the pixels with a read-only `Lock` on the real surface that the plugin cannot see (it is not traced). Primary,
back-buffer, 3D-device and Z-buffer surfaces are never stored. A surface's current pixels at any point of the trace are the
records of its lines applied in trace order, each over the rectangle it names (a later `Blt` onto a surface that is never used as
a source or texture again is not read back; the replayer replays or ignores it).

### Trace lines

* `Unlock`, `ReleaseDC`, `Blt`, `BltFast`, `SetTexture`: ` blob=@<offset>+<length>` at the end of the line, only when pixels were
  stored or found already stored by this call (an unchanged surface: no suffix on `Blt`/`SetTexture`; an `Unlock` of unchanged
  content refers to the old record). `blob=failed`: the cap was exceeded; `blob=unavailable`: the read-back lock failed.
* Draw calls (`DrawPrimitive`, `DrawIndexedPrimitive`, `DrawPrimitiveVB`, `DrawIndexedPrimitiveVB`): ` blob={vertices=@O+L,
  indices=@O+L}` (no `indices` for the non-indexed calls; `vertices=none` as above). Strided draws are not captured.
* Vertex-buffer `Lock` and `Unlock` lines carry no blob: the data is stored by the draws that read it.
* A texture binding is `dev7#N.SetTexture(stage=S, surface=surf#K)`; `surf#K`'s format is in its records and in its `CreateSurface`
  and `Lock` lines. Mip chains are separate surfaces linked by `GetAttachedSurface` lines.

### Cap and failure

The file never grows past `DDT_DATA_MAX` bytes (default 512 MiB; the golden's counts, 74 surfaces, 301 locks, 182 blits and 1593
draws over 60 presents, should need a small fraction of that; most draws are 4-vertex quads of 144 bytes). When a record would
pass the cap the proxy writes `# ERROR: data cap of <n> bytes exceeded (...)` to the trace, ends the trace (no more lines or data),
creates `<trace>.failed` instead of `<trace>.done`, and calls `OutputDebugString`. `game-trace.sh` stops waiting when it sees
`.failed` and fails the run. A data file that cannot be created is an error line and the same failure. A failed or short write to the data file (`# ERROR: data write failed (...)`) and an allocation failure of the capture itself
(`# ERROR: data allocation failed (record index | vertex shadow buffer ...)`) end the trace the same way, with the real reason.
A surface whose read-back could not lock it stays marked as changed (`blob=unavailable` on that line) and is stored at its next use;
a rectangle Unlock stores only the rectangle and leaves a Blt-changed surface marked until a whole-surface record is stored.
`DDT_TEST_FAULT` (`data-write`, `data-alloc`, `shadow-alloc`) injects those failures for the offline tests; nothing sets it in a
game run. `scripts/read_data.py` refuses a negative record offset.

### Reference reader

`scripts/read_data.py FILE summary [--json]` prints the counts and bytes per kind and checks every payload hash;
`scripts/read_data.py FILE extract OFFSET OUT [--raw]` writes one record's payload (or the whole record) to `OUT` and prints its
header as JSON. It is also the shortest description of the parser (about 40 lines). `tests/ddraw_trace/test-read-data.sh` tests it.

Not captured: palettes (`IDirectDrawPalette` is not wrapped, so 8-bit surfaces have no colours), lock-less pixel writes through
`GetDC` on a surface that cannot be locked, strided draws, `ProcessVertices`, state before a `DDT_ARM_FILE` window opens (the
records start empty, so use the unarmed startup scenario for replays).

## Side files

Next to the trace (not part of the normalized trace, so they never break a diff):

* `<trace>.times`: `<present number> <milliseconds since the first traced call>` per present.
* `<trace>.callers`: `<module> <calls> traced|untraced`, the attribution evidence. In the game, only the two plugins
  call ddraw (the exe and the engine DLL make no direct ddraw calls), so nothing is excluded.
* `<trace>.done`: empty marker, created when the present cap is reached.

## Determinism

Gate: two captures with the original renderer must be identical. Results on this machine (Mac display 1710x1107,
Wine Stable 11.0, d7vk stack, the launcher's `EE_*` switches off as in the task):

* Cap of 60 presents (the default): **identical**. Final build: two pairs (the gate, and two `make game-trace` runs),
  11,439 lines each, with the hashes of every locked surface and vertex buffer and of every index and vertex array. Earlier
  builds gave three identical pairs of 10,980 lines (before index hashing and the present rule below).
* With the version.dll shim now really installed (the first captures ran without it, see "Insertion"), the gate's pair: the raw
  traces differed in 8 lines, all cursor-race lines (a `T3` blit before or after the render thread's own cursor blits, and the
  alternating scratch surface), and were identical after `scripts/normalize.py`; the trace content is otherwise the same as
  without the shim. `make game-check` compares normalized traces for the same reason.
* A race seen before the present rule changed: the software cursor is drawn by a third
  thread (`T3`) that races the render thread when the menu first appears. Its 32x32 sprite blits then came in a different
  order and, because Blts to the primary used to count as presents, moved the cap by one frame. Now only a whole-surface
  `Blt` (no destination rectangle, a source surface) or a `Flip` is a present, and the gate, if two raw traces differ,
  compares them again after `scripts/normalize.py` removed the software cursor's blits: non-waiting Blts (`DDBLT_DONOTWAIT`)
  with a 32x32 rectangle whose source or target is a 32x32 surface that the trace shows being created. A waiting blit, or a
  32x32 blit between other surfaces, stays.
* Cap of 300: the traces differ, always at the same kind of event. After 111 to 152 frames (five runs: 111, 119, 126, 128, 152;
  frames are paced at 60 Hz, so about 2 s after the menu appears) two extra untextured quads appear in each frame, and
  they toggle on and off later. The frame goes from 153 to 164 lines. Cause: a wall-clock-driven element of the menu, not
  the proxy. The default cap ends well before it. Under heavy load the event arrives in fewer frames, so lower
  `DDT_PRESENTS` if a pair ever differs; `<trace>.times` and a frame-size histogram show where.
* Environment effects seen: the launcher's first attempt dies before its first frame in roughly half of the runs here
  (it retries up to 6 times; this also happened in launches without our proxy). One run stalled for 4 minutes inside
  Present while the idle display slept, and the game quit when the display woke; `game-trace.sh` now runs `caffeinate`.
* The display mode list (`EnumDisplayModes`) and the adapter names come from the Mac's display and Wine, so the golden
  trace in `tests/ddraw_trace/golden/` is machine specific: `make game-check` is meaningful on this Mac only.

Golden: `tests/ddraw_trace/golden/startup-menu-n60.trace.gz` (the 60-present trace, call metadata and hashes only, no game
data) and `startup-menu-n60.summary.txt` (call counts per method, render states, texture formats).

## Scenarios

`game-trace.sh --scenario <file>` captures a scenario instead of the startup (a saved game loaded with `-loadgame`, paused, then a
window of presents); `tests/ddraw_trace/scenarios.md` lists them with their command lines, save checksums, windows, normalization and
goldens. `normalize.py --structure` is the form of a trace that does not depend on the history of the run before the window or on
the tick the game was paused at (see there for what it masks and why).

## Self-check

`dispatch_init` checks the argument classification: every argument type string that looks like a COM interface (`LPDIRECT*`,
`IDirect*`, `IUnknown*`) must be classified as an interface, or be one of the interfaces the proxy deliberately does not wrap
(`LPDIRECTDRAW`, clipper, palette, `IUnknown*`). A mismatch, for example after regenerating `src/methods.inc` from other
headers, would hand wrapper pointers to the real object. The proxy exports `DDT_SelfCheck` (the number of problems), puts a
`# ERROR:` line in the trace header and calls `OutputDebugString`; `make test` runs the check against the real build (0
problems) and against a build without the `LPDIRECTDRAWSURFACE7` map entry (it must notice), and depends on `check-tables`.

## Known gaps

* Only five interfaces are wrapped. `IDirectDrawClipper`, `IDirectDrawPalette`, gamma/colour control and other
  `IDirectDraw*` versions pass through raw and untraced (the game never obtained one in the captured window).
* Vertex buffer hashes cover the whole locked range, which for the game's ring buffer is all 87,912 bytes at every
  `Unlock`. That is deterministic here but says nothing about which part changed; a dirty-range hash or a hash of the
  vertex range at draw time would localize differences.
* Pixels drawn through a surface DC (`GetDC`/`ReleaseDC` with GDI text) are not hashed in the trace (59 pairs in the captured window); with `DDT_DATA` the `ReleaseDC` line refers to the pixels in the data file.
* The Bink movie path is not exercised (the launcher skips the intro movies).
* No normalization of the wall-clock menu element: the capture ends before it.
* `DDCAPS` prints `dwVidMemFree`; it was constant here. Other volatile fields may exist outside the captured window.
* Strided draws, clip planes, `ComputeSphereVisibility` arrays and `GetDC` handles print as `P` only.
* The insertion needs the launcher's `EMPIRE_EARTH_WINE` override and its `wine start /unix` call. If the launcher is
  restructured, `swap-in.sh` has to be triggered another way (the byte-exact restore does not depend on it).
* The Wine prefix registry and the launcher logs are not restored.
