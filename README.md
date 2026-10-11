# empire-earth-web

Experimental, incremental reverse engineering of *Empire Earth* (2001) toward a version that runs in the web browser.
Each original module is replaced only after it is shown to behave like the original, which keeps running under Wine
as the reference. Work is paused.

**No game files are included, and none are ever to be committed.** Running anything here requires your own legally
obtained copy of the game (for example *Empire Earth Gold Edition* from GOG). This project is not affiliated with or
endorsed by Rebellion, Sierra or Stainless Steel Studios; *Empire Earth* is a trademark of its respective owners.

## Contents

- `src/plugins/tcp_protocol/`: reimplementation of the network plugin (`TCP Protocol.dll`), verified call by call
  against the original in a real game.
- `src/plugins/dx7_rasterizer/`: renderer plugin skeleton (`EEWebRasterizer.dll`) that the game loads, with the
  original's 82-slot rasterizer vtable.
- `src/engine_abi/`: engine interface headers generated from vtable inventories (`tools/re/gen_abi_headers.py`).
- `tools/ddraw_trace/`: tracing `ddraw` proxy that records every DirectDraw/Direct3D 7 call the renderer makes.
- `tools/game_run/`: crash-safe library that snapshots the game directory, runs the game and restores it exactly.
- `tests/`: differential tests (harnesses under Wine, golden transcripts and traces).
- `odd/tasks/empire-earth-web.md`: plan, progress and verification evidence.

## Requirements

macOS or Linux with clang/lld (i686 MSVC target), mingw-w64, Python 3 and Wine. Paths to the game install and to the
reverse-engineering inventories are configured outside the repository.
