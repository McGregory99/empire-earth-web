# Trace scenarios

Deterministic scenarios whose Direct3D / DirectDraw call traces of the ORIGINAL renderer are reproducible across runs, so that they
can act as golden oracles for the renderer reimplementation (P2.6). Each is captured by `tools/ddraw_trace/scripts/game-trace.sh`
through the shared game-run library (`tools/game_run`: lock, verified snapshot, exact restore). The trace format is in
`tools/ddraw_trace/README.md`. Goldens are specific to this Mac (display modes, adapter names).

| # | scenario | state | golden |
| --- | --- | --- | --- |
| 1 | startup to the main menu (also the 2D menu) | done | `golden/startup-menu-n60.trace.gz` |
| 2 | 2D screens other than the menu, GE micro-harness | not done | none |
| 3 | paused skirmish from a save | done, structure form only (see below) | `golden/skirmish-paused-n60.structure.trace.gz` |

## 1. Startup to the main menu

* Command: `make -C tools/ddraw_trace game-gate` (two captures and their comparison), `game-check` (one capture against the golden).
* Window: the first 60 presents from process start (`DDT_PRESENTS=60`); after about 110 presents a wall-clock menu element breaks determinism.
* Normalization: `normalize.py` (software-cursor blits removed, nothing else).
* Golden: 11,439 lines; sha256 of the decompressed text `a1bb39a6dedb555aebfa9d38e492ea421c849b3e07718cd106d68c8d1b003f0a`; after normalization `1b83148a7877b92a763aaf9fb806217228c560a8879ba6e88e56691166413453`.
* Result: identical in two runs (raw, or after the cursor normalization).

## 3. Paused skirmish

* Scenario file: `scenarios/skirmish-paused.env` (data only: command line, save checksum, window, environment, keys, normalization).
* Command line: `Empire Earth.exe -loadgame "ee_p24_skirm1.ees"`, started by `tools/game_run/scripts/ee-start.exe` (the Wine wrapper starts it
  in place of the game: `wine start` rewrites the quotes the game's parser needs). The save name needs its `.ees`: without it the game falls back to the main menu.
* Save: `ee_p24_skirm1.ees`, 79,654 bytes, sha256 `dce7741eb29f7dde374ff4357e863be6e6349deae44a3f14d96ebc7ed67e4d11`. User data, never committed:
  it lives in `~/Games/EmpireEarth-re/scenarios/`, the driver refuses to run if the checksum differs, copies it into the game's
  `Data/Saved Games` for the run only and the snapshot restore removes it again (a copy of the user's own is put back byte for byte).
* Environment: `EE_SIMHASH=1` (per-tick object hash lines in `ee-version.log`, used to see the simulation stop), `EE_SIM_FIXED_MS=33`.
* Pause: the save does NOT come up paused (the simulation runs at about 28 ticks a second). The driver waits for the first EE_SIMHASH
  tick line (the game loop), waits `PRE_KEY_DELAY` (9 s) more so that the in-game form takes input (two real runs that pressed 2-4 s after the
  loop came up never paused), presses F3 (scancode `3d`, `ee-key.exe --window-class "SSSI Empire Earth"`: it gives the game window the foreground focus and presses with SendInput once that is confirmed, otherwise posts WM_KEYDOWN/WM_KEYUP to the window, and logs which; it logs the visible windows of its desktop and the foreground one, so a lost key shows where the focus was), waits 6 s, and requires that no new simulation tick line
  appears for 5 s. If the tick lines keep rising it presses F3 once more and checks again; if they still rise the run fails. The log records
  the tick counts and the last tick number at each check.
  Only then does it create the arm file the proxy waits for (`DDT_ARM_FILE`): the capture window opens at the next frame boundary.
  Holding the key from the moment the game's window exists (so that the pause lands at the loop's first poll) was tried in two real runs
  and had no effect; that code was reverted.
* Reference screenshots: `SCN_SHOTS=3`, `SCN_SHOT_GAP=4` (seconds): after the capture window, while the game is still paused, the driver runs `screencapture -x` (read-only, the whole display, the game window scaled) three times into `<out dir>/shot-0.png` .. `shot-2.png`. A failing shot fails the run after the game was closed and the directory restored.
* Window: the first 60 presents after the arming (`SCN_PRESENTS=60`), 84,844 lines, 1,406 lines a frame after the first 50 frames of loading; then WM_CLOSE (`ee-close.exe`) and the game leaves by itself.
* Command: `make -C tools/ddraw_trace game-gate-skirmish` (two captures and their comparison), `game-check-skirmish` (one against the golden).

### What is not deterministic, and what is done about it

The pause lands on a tick that depends on when the key press arrives (two pairs of runs paused at 1 to 2 and at 8 simulation tick lines).
Four captures, raw: 84,844 lines each, 17,328 (pair 1) and 29,734 (pair 2) lines differ position by position. The differences are:

| class | lines per run | cause |
| --- | --- | --- |
| surface / vertex buffer ids | everywhere | ids are assigned in creation order; the window opens at an arbitrary object count |
| `fnv1a64` content hashes | vertex buffer unlocks (12,120), a 64x64 texture upload every frame, draw arrays | unit positions and animation at the tick of the pause |
| `SetTexture` surface | 880 | which unit textures are visible, same cause |
| `start_vertex` of `DrawPrimitiveVB` / `DrawIndexedPrimitiveVB` (12,120) | | the vertex buffer is a ring; its write position at the window start depends on the history |
| flags of `vb.Lock` (DISCARD 0x1020 vs NOOVERWRITE 0x2020) | 210 | where the ring wraps, same cause |

The golden is therefore defined over the deterministic subset, the **structure form** (`normalize.py --structure`): ids renumbered by first
appearance, all content hashes masked, the texture bound by `SetTexture` masked, `start_vertex` and the vertex buffer lock flags masked.
Everything else (every call and its arguments in order, render and texture stage states, matrices, viewports, counts, sizes, formats,
surface blits and locks) is compared as is. All four captures are identical in this form (sha256 of the text `852c752f...a19c9163`),
including the two pairs paused at different ticks. A renderer that matches the golden has the same call structure but this scenario
does not check vertex or texture content: the startup scenario (full hashes) and the unit tests cover content.

* Golden: `golden/skirmish-paused-n60.structure.trace.gz` (201,566 bytes, plain git, no LFS), 84,844 lines, 60 presents; sha256 of the decompressed text
  `852c752f05a38edf1f2095081781b6289d1ab778d47dbde122ead314a19c9163`; summary of call counts in `golden/skirmish-paused-n60.summary.txt`.
  It holds call names, arguments, structure and masked hashes only, no game data.
* Regenerate: `make -C tools/ddraw_trace game-check-skirmish` captures once; to replace the golden after an intended change:
  `python3 -I tools/ddraw_trace/scripts/normalize.py --structure < tools/ddraw_trace/build/game-trace-skirmish-paused/last.txt | gzip -9 -n > tests/ddraw_trace/golden/skirmish-paused-n60.structure.trace.gz`,
  then update the checksum here and in `tests/ddraw_trace/test-normalize.sh`.
