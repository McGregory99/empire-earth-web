# Empire Earth in the browser

## Objective
Make Empire Earth (2001, Stainless Steel Studios, incl. The Art of Conquest) playable in a web browser via WebAssembly, using game files supplied by the user (never redistributed).

## Problem / why
No reimplementation or public decompilation of the Titan engine exists. Recent browser ports (Halo CE: decompilation -> WASM; StarCraft: OpenBW reimplementation -> WASI WASM; BO1 Zombies: Three.js reconstruction over extracted assets) show the viable patterns. Empire Earth has none of the prerequisites yet.

## Reference architecture (from heiner/openbw StarCraft web port)
- C++ engine compiled with WASI SDK (reactor model: JS calls init/step/render), no SDL.
- Rendering and UI in JS on a canvas; multiplayer via WebRTC copy-paste invite.
- Game data loaded client-side. Our legal model: user supplies files -> stored in OPFS.

## Scope (phase 0: discovery)
- Community reverse-engineering inventory (EnergyCube / NeoEE / EE-modders).
- Obtain GOG offline installer, extract with innoextract, inventory file formats.
- Analyze the executable (size, compiler, DirectX/DirectPlay imports, protection).
- Decide route: decompilation (faithful) vs reconstruction (visible sooner).

## Constraints
- Never commit or host game assets or binaries. Game files live outside the repo (`~/Games/EmpireEarth`).
- Legal copy only (GOG Gold Edition).

## Tasks
- [x] T1 Community RE research (EnergyCube, NeoEE, EE-modders, formats, protocol) — route: delegated (broad web research). Evidence: findings section below.
- [x] T2 Obtain GOG offline installer: `setup_empire_earth_gold_2.0.0.2974_gog_v3_(78415).exe` (667 MB) in `~/Downloads`.
- [x] T3 Extract installer to `~/Games/EmpireEarth` with innoextract 1.9 — 715 MB, both EE and AoC present. Route: inline.
- [x] T4 Inventory file formats — route: inline. Evidence below.
- [x] T5 Analyze executable — route: inline. Evidence below.
- [ ] T6 Route decision (decompile vs reconstruct) with evidence — needs user decision

## Acceptance criteria (phase 0)
- Findings documented below with sources.
- Route decision presented to the user with evidence.

## Checks
- T3: extraction completes, `Empire Earth.exe` and `.ssa` files present.
- T4/T5: reproducible commands recorded.

## TDD
Not applicable in phase 0 (research only, no source code yet). Strict TDD (global config) applies once implementation starts.

## Progress / findings
- 2026-10-08: Project created. innoextract 1.9 installed via Homebrew. Watcher waits for `~/Downloads/setup_empire_earth*.exe`.

### T1 — Community reverse-engineering inventory (2026-10-08)
No public decompilation, Ghidra/IDA database, engine clone using original assets, or WASM attempt was found. Rights holder: Rebellion (Steam page). Model: own engine + user-supplied assets.

Key repos:
- `00xJS/Empire-Earth` (Sept 2026, no license): Wine + D7VK on macOS with Win32 patches. Richest public RE: function addresses in EE.exe / EE-AOC.exe / `Low-Level Engine.dll`, draw-slot pool, `dbworld` record size, 63-unit selection message limit, x87 -> SSE2 replacement recipes, MSVC mangled symbols.
- EE-modders org (mostly GPL-3.0): Empire-Earth-Studio-2 (SSA pack/unpack, SST convert), CEM-tool (CEM v2 + Blender), DB-Editor (~40 `.dat` table schemas in `EEEditorData/*.dats`), Empire-Earth-Library / Reborn-DLL / Stats (MinHook hooks + GOG offsets), toolbox (manuals, TAI scripting wiki), eerv (Java viewer: CEM, SSA, PK, TAI, RMV, SCN, ~30 DB tables).
- `coderbot16/cem` (Rust, MIT): CEM v1/v2/v5. `coderbot16/scn` wiki: SCN header, lump table, triggers.
- `save-ee/save-ee-lobby` (VB.NET): fragments of the lobby/game network protocol.
- `plowsec/empire-earth-remake` (Rust + Godot, own art, deterministic `ee_sim`): architecture reference only.

Formats:
| Format | Status |
|---|---|
| SSA archive (`rass`) | read + write |
| PK01 (PKWARE DCL implode) | solved (C, Java, Python) |
| SST textures | read + write (TGA/JFIF/DDS body) |
| CEM v2 models (`SSMF`, vertex-frame animation, LODs) | specified |
| SCN/EES scenarios | header + lumps + triggers partial; **Terrain and Objects lumps undocumented** |
| DB `.dat` tables | read/write, some fields UNKNOWN |
| Elevation `.dat` | int16 LE raw + dims txt |
| TAI AI scripts | partial syntax |
| Sounds | WAV PCM 8-bit 22050 Hz; music via DirectMusic; video Bink |

Executable / platform:
- Win32 x86, base 0x400000, MSVC 6 (MSVCP60/msvcrt). Engine in `Low-Level Engine.dll`; rasterizer plugins `DX7HRDisplay.dll` / `DX7HRTnLDisplay.dll`.
- Direct3D 7 via DirectDraw7. Separate simulation and render threads with a world lock.
- x87 math: simulation thread at 53-bit precision (CW 0x027f), render at 24-bit; custom table-based trig (`UMCos`, `UMArcTan2`). Inference: f64 in WASM reproduces basic ops; custom trig must be replicated bit-exactly.
- Network: Sierra WON lobby (closed 2008); hosting TCP 33334-33336 / UDP 33334, 33336; command replay consistent with lockstep. Not DirectPlay (inference).
- Version lock: GOG builds (EE 2974/2193, AoC 2165/2685) are incompatible with retail/NeoEE in multiplayer.
- GOG is DRM-free; retail protection unidentified.

Biggest unknowns: simulation logic (pathfinding, combat, economy, built-in AI) lives only in the binaries; Terrain/Objects lumps; save format; UNKNOWN `.dat` fields; full network protocol.

### T4 — File inventory (GOG 2.0.0.2974)
Commands: `innoextract -e --exclude-temp`, then `scratchpad/tools/ssa_inventory.py` (SSA index reader based on EE-modders `SSA.py`).
- Loose files: `Data/data.ssa` (EE 163 MB, AoC 45 MB), campaign `.ssa` archives, 55/94 `.rmv` random map scripts (**plain text**, original SSS comments), DirectMusic music (`.sgt`, `.sty`, `.dls`), Bink movies (`.bik`), lobby assets.
- `rass` v1.0 magic confirmed on every `.ssa`; index parses cleanly.
- EE `data.ssa` (3739 files): 2095 `.sst` textures (93 MB stored / 146 MB raw), 730 `.cem` models, 758 `.wav`, 3 `.mp3`, 39 `.dat` DB tables (7.4 MB raw), 58 `.tai` unit AI scripts, 21 `.civ`, 31 `.tga`. Almost everything PK01-compressed.
- AoC `data.ssa` (826 files): mostly `.tga` textures (405), 136 `.cem`, 39 `.dat`, 72 `.tai`.
- Campaign archive (EETheGreeks.ssa): 8 `.scn` scenarios, 235 `.mp3` voice/sounds, 9 `.jpg`.
- `vssver.scc` files are Visual SourceSafe leftovers, not game data.

### T5 — Binaries (GOG 2.0.0.2974)
Commands: `objdump -p`, `scratchpad/tools/pe_imports.py`, `pe_exports.py`, `strings`.
- `Empire Earth.exe`: PE32, 6.3 MB, `.text` 4.3 MB, MSVC 6 linker, timestamp 2017-06-23 (GOG rebuild/patch), image base 0x400000. **Not packed, no DRM imports.** Extra sections `.cms_t` / `.cms_d` (origin unknown).
- Imports **1293 C++ functions from `Low-Level Engine.dll`** by mangled name; plus wsock32 (by ordinal), kernel32, user32, gdi32, imm32, ole32, advapi32, version, shell32. No DirectPlay import (GOG's `directplay.cmd` is a generic script).
- `Low-Level Engine.dll` (2002): **4432 exports with full MSVC-mangled C++ signatures across 366 classes**. Subsystems by prefix: GE (graphics: GEModel, GETerrainMesh, GERasterizer…), UI (UIForm, UIManager…), SE (sound: SESoundManager, SEMixer…), NE (network: NESynchronousServer/Client, NEProtocol → lockstep), FS (files: FSArchive, FSMemoryFile…), TS (threads/scheduler), U (utilities, `UMCos` math). Imports DirectInput, WinMM.
- `DX7HRTnLDisplay.dll` (106 KB, 255 exports): Direct3D 7 renderer plugin via DirectDraw; uses Bink. Small, well-delimited interface → replaceable by a WebGL implementation.
- `TCP Protocol.dll` (37 KB, 33 exports): Winsock 2 transport → replaceable by WebRTC/WebSocket.
- `ddraw.dll` (2024, imports `d3d9`): GOG's DX7→D3D9 wrapper, not original.
- Audio: Miles Sound System (`Mss32.dll`); music: DirectMusic; video: Bink.
- **Exe has 1866 MSVC RTTI class names** (e.g. `EEMAttackUnit`, `EEMGatherResource` command messages; `EEAIAStar*` pathfinding; `EECPASGU*` computer-player tactics; calamities) and **107 original source paths** (`C:\sssi development\Empire Earth\…`, `…\RTS Engine\…`) from asserts → original module layout is recoverable (Units, Physics Thread, Graphics Thread, Communications, Random Map, Tech Tree, Scenario Editor, WONLobby…).

### Assessment (input for T6)
Decompilation is far more tractable than assumed: unpacked MSVC 6 binaries, engine API fully named (4432 symbols), game classes named via RTTI, source tree layout known, and a plugin architecture (renderer, transport) that can be swapped module by module. Main cost: ~4.3 MB of unnamed game-logic code in the exe. Suggested route: incremental decompilation verified against the original (replace functions/modules while the original still runs under Wine, OpenRCT2-style), then compile the full C++ to WASM with WebGL and WebRTC replacements for the renderer and transport plugins.

- [x] T6 Route decision: **incremental decompilation** (user decision, 2026-10-08).

## Phase 1 — First decompiled module: `TCP Protocol.dll`

### Why this module
Smallest plugin (37 KB, 4 sections). Pure C ABI: 30 `NPIM*` + 3 `PIM*` exports, resolved by the engine's `PIMModule::GetEntry` (GetProcAddress by name). Imports only MSVCRT, WS2_32, iphlpapi, kernel32, sxlrt233 and 39 engine symbols. A drop-in replacement proves the whole loop (decompile → clean C → differential test → in-game) without MSVC C++ ABI work. The renderer (`DX7HRTnLDisplay.dll`) exports MSVC-mangled C++ classes and comes later.

### Method
1. Ghidra headless decompiles the original module. Raw decompiler output stays outside git (`~/Games/EmpireEarth-re/`).
2. Clean C reimplementation in `src/plugins/tcp_protocol/` exporting the same symbols.
3. Differential test harness (Win32 console exe run under Wine): loads a protocol DLL by path and checks behavior; the original DLL is the oracle, ours must match.
4. Drop-in test in a working copy of the game under Wine.

### Tasks
- [x] P1.1 Toolchain: Rosetta 2, Wine (Gcenx 11.0_1 via 00xJS scripts), `mingw-w64` (i686), Ghidra + JDK 21. Versions in the toolchain log below. — route: inline (installs) + delegated script audit
- [x] P1.2 Baseline: original game reaches the main menu under Wine on macOS — **user confirmed visually (2026-10-08)**. Working copy installed by the GOG installer inside the 00xJS prefix (`~/Library/Application Support/EmpireEarthMac/wineprefix/drive_c/GOG Games/Empire Earth Gold Edition/`); binaries sha256-identical to the pristine extract. Launch command: `cd ~/Games/tools/ee-macos && EE_BIG_ARMIES=0 EE_BIG_CROWDS=0 EE_X87T=0 EE_SSE_MATH=0 EE_MEMFILE_DOUBLE=0 EE_POINTLIST_GUARD=0 EE_ANIMATION_SMOOTHING=0 ./scripts/launch.sh` (gameplay-affecting patches off; window/graphics hooks and D7VK remain).
- [x] P1.3 Ghidra headless project for `TCP Protocol.dll`; pseudo-C of all 137 functions (33 exports) outside git. Evidence: `~/Games/EmpireEarth-re/decomp/tcp_protocol.annotated.c` (3780 lines, 0 decompile failures; IAT slots and Winsock ordinals renamed by `scripts/annotate_decomp.py`). Engine call sites (`NEProtocol`, `PIMModule`) being decompiled from `Low-Level Engine.dll`.
- [x] P1.4 Differential harness, RED: built with mingw i686, runs under Wine; passes against the original DLL, fails against a stub of ours. — route: delegated writer. Evidence: `93cfa67`, `f5453b0`. The harness loads the plugin like `PIMModule::GetModule` (`LoadLibraryEx`, altered search path) and prints a normalized transcript of all 33 exports, wire bytes as hex, decrypted datagrams, event signalling and injected Winsock faults; `tests/tcp_protocol/golden.txt` is recorded from the original (`make golden` is stable, `make determinism` identical). RED observed against the 33-export stub: `make test` → `Error 1`, first diff `PIMGetName -> "TCP/IP Protocol"` vs `"(null)"`.
- [x] P1.5 Clean C reimplementation, GREEN then REFACTOR. — route: delegated writer (2+ non-trivial files). Evidence: GREEN `ad60a2d`, REFACTOR `9f9d729`, then `59a874c`, `b1b0c07`, `d6a0318`, `c755531`, `32d5061`. Parent spot check (2026-10-09): `make test` → `PASS: transcript of our DLL equals the golden transcript` (17.6 s); `make test-stub` → stub detected; `make check-abi` → exports and ordinals identical, 39 engine imports identical, 24 ws2_32 imports identical, msvcrt (no UCRT); sxlrt233 not imported (no STLport strings), extra kernel32/msvcrt imports come from the mingw CRT. Writer reports ~40 injected mutants killed except API-equivalent ones. Deviations not visible to the engine: zero-padded discovery datagrams, bounded name copies, correct free in adapter enumeration, no cipher lock (no remote deadlock), listener handle reset after close, sockets closed on resolve failure. Original defects kept because observable (`d6a0318`): a failed LAN scan or publish leaves port 33334 bound.
- [ ] P1.6 In-game drop-in: game loads our DLL; LAN host/join over localhost between two Wine instances if feasible. Progress 2026-10-09: our DLL (sha256 `5ef83473…`) installed in the Wine game copy (original backed up in `~/Games/EmpireEarth-re/bin/`); `WINEDEBUG=+loaddll` log shows the engine probing it twice and keeping it loaded; user hosted a LAN game (Other Multiplayer → LAN → Host Game → pre-game lobby, TCP/UDP 33336 bound). Open: thread `0278` waited ~3 min on critical section `25960D80` held by the main thread `011c` during the LAN screens (4 Wine 60 s timeouts in total, then resolved; clean exit). A/B with the original DLL on the same steps (`launch-20261009-100103.log`): the same pattern appears (thread `0270` blocked by main thread `011c`, 1 timeout), so the stall is pre-existing engine/plugin behavior; the longer wait with ours (4 vs 1) is not ruled out, because time spent on each screen was not controlled. Follow-up: a multi-threaded harness scenario (searcher running while the main thread publishes or closes) to compare lock hold times. Join not tested: two Wine instances on one host would both bind 33334/33336, so LAN join needs a second machine or an isolated network namespace. The game copy is back on the original DLL. **Correction:** the plugin binds only UDP 33334 and TCP 33335 (`server.c:122`, `:156`); the observed 33336 sockets were opened by something else (the exe imports `wsock32` directly), and 33334/33335 were not seen bound. So the host flow is NOT yet shown to go through our DLL. Next: one run with Wine relay tracing limited to `TCP Protocol.dll` (`HKCU\Software\Wine\Debug\RelayInclude`) to list which NPIM exports the game actually calls in menus, LAN search and hosting; run it only while no test suite is running. Verifier lock analysis: every lock region in ours matches the original function for function; ours never leaves a lock entered (the original's cipher lock can); likely causes of the main-thread hold are DNS under `game_lock` or a scan on the UI thread, both shared with the original.
- [ ] P1.7 Corrections from the independent review (native review refused: `lens_context_budget_exceeded` on the 6290-line candidate, so the RDD off path applied: high tier → independent verifier). Introduced by us: (a) Makefile `PLUGIN_HEADERS` misses the plugin headers, so header edits do not rebuild the DLL and `make test` can pass on a stale build; (b) `WINEPREFIX ?=` picks up an ambient value; use `:=`; (c) reply/probe names of 65–68 characters: the original's buffer is 69 bytes and sends them intact, ours truncates to 64 (wire-visible). No remotely triggerable memory corruption found; ABI and cipher-lock removal confirmed safe.

- [x] P1.8 Host path: which code opens 33336 and whether LAN search/host call the plugin (static, exe + LLE); relay-trace recipe and parser for an in-game run. — route: delegated mapper (read-only, Ghidra projects `ee_lle`/`ee_exe`), user authorized up to 4 parallel agents (2026-10-09). Result: the 33336 sockets belong to the exe's own Sierra WONAPI lobby (`LanLogic`, ports from `WONLobby.cfg`: `LobbyPort` 33336, `EEFileTransferPort` 33335, `LanBroadcastPort` 8585); LLE opens no sockets. **LAN search, Host Game and Dissolve Game never call the plugin.** The plugin is used only from the lobby → game hand-off: host `EELobbyGame` → `EEServer` → `NESynchronousServer` → `NPIMPublish` (then `AllowNewConnections`, `Accept*`, `ReceiveFrom`, `SendTo`, `IsAlive`, `Close*`); client `NESynchronousClient::Connect` → `NPIMConnect`/`Send`/`Receive`; `NPIMSendSingle` after a game; `NPIMActivate` once at start. Never called by exe/LLE: `EnumerateNetworkAdapters`, `Get/SetConfiguration`, `GetLocalAddress`, `GetStatus`, `GetProtocolType`. Note: exe and plugin both use TCP 33335 (pre-existing). Wine relay does not cover native DLLs; use `SnoopInclude=TCP Protocol.*` (scripts `~/Games/EmpireEarth-re/scripts/snoop_on.sh`, `snoop_off.sh`, `ee_mark.sh`, `relay_summary.py`; untested on a real snoop log). `annotate_decomp.py` uses ws2_32 ordinals; the exe imports wsock32 (use `annotate_wsock32.py`).
- [x] P1.9 Decompile the original searcher thread (`LAB_100032c4`) and compare with `search.c`. — route: delegated mapper (read-only, project `ee_tcp`). Result: **equivalent**. Loop, 50 ms `WaitForSingleObject`, FP precision/rounding calls (`UM*`, no FPU control instructions anywhere in the DLL), stop sequence, lock regions and detected-list update match. The scan's second parameter is always 0 (both callers: `NPIMPublish` 10001fdf and the thread 1000336d), so broadcast-only is correct. Differences not observable: missing `ESVerify("inService != NULL")` log line, handle read order, malloc vs `operator new`, NULL-lock guard. Decompile outside git: `~/Games/EmpireEarth-re/decomp/tcp_searcher.annotated.c`.
- [x] P2.0 Map the renderer plugin (`DX7HRTnLDisplay.dll`/`DX7HRDisplay.dll`): exports, engine contract, D3D7 usage, MSVC C++ ABI strategy, proposed Phase 2 tasks. — route: delegated mapper (read-only, project `ee_render`). Result in "Phase 2 — plan" below; raw outputs in `~/Games/EmpireEarth-re/{decomp,inventory,scripts}/`.

### Known original defects kept for fidelity (follow-ups, not fixed)
From the independent review; all present in the original and reproduced on purpose. Revisit when the transport is replaced (WebRTC) or if they bite in play: failed LAN scan leaks its reply socket and keeps 33334 bound (~20 handles/s while the searcher retries); `NPIMPublish` restarts Winsock and recreates locks/key table while other threads may use them; `NPIMXferSendTo` and datagram receive can loop indefinitely on a stalled or chatty peer while holding `transfer_lock`; unbounded client registration from unencrypted `0x22`/`0x37` datagrams; `NPIMReceiveFrom` returns success without writing outputs; aliased transfer connection record (possible double free); `NPIMSendSingle` can read past `h_addr_list`. Untested: multi-threaded NPIM calls, oversized datagrams, publish during a search; searcher thread body (`LAB_100032c4`) not decompiled.

### Risks
- Possible copy-protection remnants in the exe: sections `.cms_t`/`.cms_d` and strings `CmdLineExt.DLL`, `SIntf32.dll` (inference: SecuROM). If game code lives in `.cms_t`, the exe needs extra unpacking work. Does not affect Phase 1.
- Wine on Apple Silicon runs x86 via Rosetta 2; the game may need GOG's `ddraw.dll` wrapper or D7VK (see `00xJS/Empire-Earth`).
- Legal: decompiled code derives from Rebellion's binaries. Keep the repo local/private; never commit game files or raw decompiler output.

### Toolchain log (P1.1)
- Rosetta 2 OK, JDK 21.0.2, LLVM (Homebrew), mingw-w64 GCC 16.2.0 (`i686-w64-mingw32-gcc`), winetricks 20260125, binutils.
- Ghidra 12.1.4 in `~/Games/tools/ghidra_12.1.4_PUBLIC`. The release has no macOS natives: built `ghidra_opt` from `Ghidra/Features/Decompiler/src/decompile/cpp` with `CPATH=/opt/homebrew/opt/binutils/include make ghidra_opt ARCH_TYPE="-arch arm64"` and copied it to `os/mac_arm_64/decompile`.
- Homebrew Wine casks are disabled (Gatekeeper, 2026-09-01). Using `00xJS/Empire-Earth` scripts (`~/Games/tools/ee-macos`, commit 1942b3b): Gcenx Wine 11.0_1 (sha256-pinned) under `~/Library/Application Support/EmpireEarthMac/`. Script audit: no sudo, no telemetry; runtime patches are in-memory via its `version.dll` shim and mostly toggleable (`EE_*=0`); it never touches `TCP Protocol.dll`.

### TDD
Strict TDD (global config). Runner: `make test` (builds with `i686-w64-mingw32-gcc -mcrtdll=msvcrt-os`, runs `build/harness.exe` under the 00xJS Wine in `~/Games/EmpireEarth-re/testbed/ours`, diffs against `tests/tcp_protocol/golden.txt`). One-time setup: `make wine-prefix` (otherwise a harness crash attaches `winedbg` and hangs). Do not run two suites at once: the scenarios bind port 33334. The golden needs a LAN interface with UDP broadcast.

### Build notes
- Homebrew's `-mcrtdll=msvcrt` still links the UCRT (`libmsvcrt.a` is a copy of `libucrt.a`); use `-mcrtdll=msvcrt-os`.
- Engine C++ methods are imported by mangled name: thiscall declarations with quoted asm labels, import library from `src/engine_abi/low_level_engine.def` via `dlltool --no-leading-underscore`.
- Wine reports more adapters than the plugin's fixed 6400-byte `GetAdaptersInfo` buffer and reorders `gethostbyname` results, so the harness fakes both.

## Phase 2 — plan: renderer plugin (`DX7HRTnLDisplay.dll`)
Not started; proposal from the P2.0 mapping (2026-10-09), pending user go-ahead after Phase 1 closes.

### Contract (evidence: LLE `GEInitialize` 0x1004a2cc → `PIMModule::GetModule` 0x100575d2)
- Engine-facing surface is 4 plain C exports: `PIMGetName`, `PIMGetType` (must return `'rast'` 0x72617374), `PIMIsValid` (activates, checks screens, deactivates), `RPIMGetRasterizer` (returns a static `DX7TnLRasterizer`, "Direct3D Hardware TnL"). The other 251 mangled exports are unused class-export side effects (no exe/LLE references).
- Everything else crosses through positional vtables: GERasterizer 78 slots (DX7Rasterizer extends to 82), GEScreen 20, GESurface 32, GEMovie 8, GEVertexArray 16, GEPolygonArray 10. The plugin also reads private fields of ~12 engine classes (GEModel, GEMaterial, GETerrainMesh, GETexture, GELight…) inline.
- Selected by config `"Rasterizer Name"="Direct3D Hardware TnL"` in the Wine prefix, so TnL is the oracle. Non-TnL is the same source plus 4 differing vtable slots.
- D3D7 usage is narrow: one FVF `0x152` (36-byte vertex), one ring vertex buffer per screen (2442 vertices), indexed triangle/line lists, fixed-function lighting and linear vertex fog, texture stage 0 only, textures are DDraw surfaces (12 formats incl. DXT1), GDI text on a surface DC, Bink to a locked surface (3 intro movies only). Heaviest: `TransformToCamera` (CPU, float-sensitive), terrain draw.

### Recommended route
Clean C++ clone built with clang `--target=i686-pc-windows-msvc` (MSVC vtable layout, mangling and overload slot order confirmed by a compile-only probe; link via `lld-link` not yet proven; fallback: hand-written C vtables under mingw). Front-end written against a `Dx7Device` seam: prove it trace-identical on real D3D7 under Wine, then swap in a WebGL2 backend. Oracle: our own tracing `ddraw` proxy in front of the existing 00xJS shim chain (pointers → ids, floats as hex bits, buffer contents hashed).

### Proposed tasks (renumbered after P2.0)
- P2.1 Toolchain spike: clang MSVC-target DLL with the 4 exports and a null rasterizer; engine loads it, `PIMIsValid` returns false cleanly.
- P2.2 ABI headers generated from the vtable inventories; test that our vtable shapes match.
- P2.3 Tracing `ddraw` proxy + golden recorder; original gives identical traces twice.
- P2.4 Deterministic scenarios (activation, 2D menu, GE micro-harness, paused skirmish with fixed camera).
- P2.5 Engine class layouts the plugin reads inline, with canary asserts.
- P2.6 Reimplementation by area, RED→GREEN on traces: screen/surface/formats; state setters; scene and 2D draws; models/lights/textures; terrain/water; Bink.
- P2.7 TnL subclass + non-TnL build from one source.
- P2.8 In-game drop-in: A/B traces + screenshots.
- P2.9 Extract the `Dx7Device` seam; trace diff unchanged.
- P2.10 WebGL2 backend driven by recorded traces, compared to reference frames.
Estimate (not measured): ~3–4 weeks to a verified drop-in clone. Risks: silent corruption from wrong engine layouts, scenario determinism (FPS counters, time-based animation), x87 vs SSE 1-ULP differences in culling, unproven link path.

## Next step
After P1.7 closes (no suite running): P1.6 in-game run with our DLL and snoop tracing, host a LAN game and press **Start Game** (that is where `NPIMPublish` runs), mark steps with `ee_mark.sh`, summarize with `relay_summary.py`; A/B the same steps with the original DLL. Then review the P1.7 commits as one candidate.
