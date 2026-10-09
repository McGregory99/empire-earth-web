# ddraw_trace: a tracing DirectDraw proxy

The oracle for Phase 2. The renderer plugin (`DX7HRTnLDisplay.dll`, selected by the config value
`Rasterizer Name = Direct3D Hardware TnL`; `DX7HRDisplay.dll` is the non-TnL sibling) is the only client of
DirectDraw 7 and Direct3D 7 in the game. We will reimplement it and compare the clone with the original at the
plugin <-> ddraw boundary, call by call. This proxy records that boundary as a normalized text trace.

```
make -C tools/ddraw_trace              # build/ddraw.dll
make -C tools/ddraw_trace test         # unit tests (Wine, test prefix) and restore tests
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

`game-trace.sh` runs the unmodified launcher with `EMPIRE_EARTH_WINE` pointing at a one-file wrapper, an override the
launcher documents (`find_wine`). The wrapper forwards every call to the real Wine and, only for the call that
starts the game (`wine start /unix <exe>`), runs `scripts/swap-in.sh` first:

1. `mv ddraw.dll ddraw_next.dll` (the launcher's file, written a moment ago, keeps working under another name),
2. `cp proxy ddraw.dll`, and write `ddraw_trace.cfg` (next module, trace path, present cap).

At that point `apply-launch-patches.sh` has finished, so nothing overwrites the proxy. A retry after an early
death (the launcher retries up to 6 times) swaps again; `swap-in.sh` is idempotent.

### Why this is safe

* No file of the 00xJS tree or of the launcher is modified; no launcher script is edited. The wrapper lives in
  `tools/ddraw_trace/build/`.
* The 00xJS proxy does not depend on its own file name: it loads `ddraw_eeorig.dll` from the directory of its own
  module (`GetModuleFileNameA(&__ImageBase)` in `ee-ddraw.c`), and no shim (`version.dll`, `ee-ddraw`) refers to a
  module called `ddraw.dll`. Checked in the sources; confirmed by the captures (the game reaches the menu).
* The game scans every `*.dll` of its folder for plugins at start-up, so an extra DLL is probed. `ddraw_next.dll` is
  the same code that was already probed under the name `ddraw.dll`; our proxy has no start-up side effects (it does
  nothing in `DllMain` but remember its handle).
* Before anything is touched the script takes the game-run lock (`mkdir ~/Games/EmpireEarth-re/locks/game-run`,
  waits if another agent holds it, released by a trap), checks that no game process exists, records a sha256
  manifest of the whole game directory and a copy-on-write snapshot (`cp -c`).
* After the capture the prefix is stopped with its own `wineserver -k` (nothing else is killed) and the script waits
  for the game to disappear. Then `restore_game_dir` removes our files, puts the launcher's `ddraw.dll` back,
  copies back every file whose hash differs from the manifest and deletes added ones, and fails loudly if the
  manifest still differs. Files the launcher or the game rewrite on every run (`ddraw.dll` is rebuilt, `ee-ddraw.log`,
  `ee-version.log`) are reported and restored too. `tests/ddraw_trace/test-restore.sh` tests this on a scratch copy.
* Not restored, because it is outside the game directory: the Wine prefix registry (the launcher re-imports the same
  values every launch; Wine flushes the registry when the server stops) and the launcher logs in
  `~/Library/Application Support/EmpireEarthMac/logs/`.

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
* Timing-dependent calls (`GetFlipStatus`, `GetBltStatus`, `WaitForVerticalBlank`, `GetScanLine`,
  `GetVerticalBlankStatus`) are written as `... = ? [timing]` with masked results, and a run of identical lines
  collapses to one (polling loops have no reproducible length).
* A "present" is `IDirectDrawSurface7::Flip`, or `Blt`/`BltFast` whose destination was created with
  `DDSCAPS_PRIMARYSURFACE`. After `DDT_MAX_PRESENTS` of them the trace gets its end marker and stops; the game
  keeps running untraced.

## Configuration

`ddraw_trace.cfg` next to the proxy (`KEY=VALUE` lines), overridden by environment variables of the same name:

| key | meaning | default |
| --- | --- | --- |
| `DDT_NEXT` | module the proxy forwards to (relative: next to the proxy) | `ddraw_next.dll` |
| `DDT_TRACE` | trace file (Windows path); empty: pure pass-through, nothing is logged | empty |
| `DDT_MODULES` | comma separated base names whose calls are traced | the two renderer plugins |
| `DDT_MAX_PRESENTS` | present cap, 0 = unlimited | 0 |
| `DDT_FLUSH` | `1`: flush after every line (the unit tests) | 0 |

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

## Known gaps

See the report that came with this slice; the short list is at the end of `odd/tasks/empire-earth-web.md` once the
parent has folded it in.
