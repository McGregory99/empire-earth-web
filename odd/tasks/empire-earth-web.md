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
- [ ] T2 Obtain GOG offline installer (user action: the downloaded `GOG_Galaxy_*.exe` is a 492 KB Galaxy stub, not the game)
- [ ] T3 Extract installer to `~/Games/EmpireEarth` with innoextract 1.9 (installed)
- [ ] T4 Inventory file formats (extensions, sizes, counts, magic bytes)
- [ ] T5 Analyze executable (PE headers, compiler, imports, protection)
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

## Next step
T2: user downloads the GOG offline installer (current file is the Galaxy stub). Then T3–T5.
