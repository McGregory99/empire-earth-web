#!/usr/bin/env python3
"""Normalize a ddraw trace for the determinism gate: stdin -> stdout.

Removes the software cursor's sprite blits, and only those.  The cursor is drawn by a thread of its own (T3 in the captured
traces) that races the render thread when the menu first appears, so the same blits show up in a different order, on a
different thread and in a different frame from run to run.  They are the only unreproducible lines seen so far; everything
else is compared as is.

With --structure (the scenarios that start from a game in progress) it also reduces the trace to what does not depend on the
history of the run before the capture window or on the instant the game was paused:
  - surface and vertex buffer ids are renumbered in order of first appearance (the window starts at an arbitrary object count);
  - every content hash (fnv1a64:...) is masked, because the paused state differs by the tick it was paused at (unit positions);
  - the surface bound by SetTexture is masked (which unit or terrain texture is visible depends on the same state; the texture
    is still identified where it is created, locked and blitted);
  - start_vertex of the vertex buffer draws and the flags of vertex buffer locks are masked: the game's vertex buffer is a ring,
    and its write position, and so the DISCARD / NOOVERWRITE choice at each wrap, depends on how many vertices were written
    before the window opened.
Everything else (the sequence of calls, their arguments, render and texture stage states, matrices, viewports, counts, sizes,
formats) is compared as it is.

A cursor blit has the shape the game gives it: a non-waiting Blt (DDBLT_DONOTWAIT, 0x08000000), with a 32x32 rectangle (the
sprite, or the area saved under it), whose source or target is one of the 32x32 scratch / sprite surfaces that the trace
itself shows being created.  A waiting blit, or a 32x32 blit between other surfaces, is a frame's own content and stays.
"""
import re
import sys

RECT = re.compile(r"(?:lpDestRect|lpSrcRect)=\{(-?\d+),(-?\d+),(-?\d+),(-?\d+)\}")
SMALL_SURFACE = re.compile(r"CreateSurface\(.*dwHeight=32, dwWidth=32.*lplpDDSurface->surf#(\d+)")
BLT = re.compile(r"(surf#\d+)\.Blt\(.*lpDDSrcSurface=(\w+(?:#\d+)?).*dwFlags=0x([0-9a-f]+)")
DONOTWAIT = 0x08000000


def cursor_blit(line, small):
    m = BLT.search(line)
    if not m:
        return False
    target, source, flags = m.group(1), m.group(2), int(m.group(3), 16)
    if not flags & DONOTWAIT:
        return False
    if not any(int(r.group(3)) - int(r.group(1)) == 32 and int(r.group(4)) - int(r.group(2)) == 32
               for r in RECT.finditer(line)):
        return False
    return target in small or source in small


ID = re.compile(r"\b(surf|vb)#\d+")
HASH = re.compile(r"fnv1a64:[0-9a-f]{16}")
SET_TEXTURE = re.compile(r"(SetTexture\(stage=\d+, surface=)surf#\d+")
START_VERTEX = re.compile(r"start_vertex=\d+")
VB_LOCK = re.compile(r"(\bvb#\d+\.Lock\(flags=)0x[0-9a-f]+")


def structure(lines):
    """The --structure form of the lines (cursor blits already removed)."""
    seen = {"surf": {}, "vb": {}}

    def renumber(m):
        table = seen[m.group(1)]
        return "%s#%d" % (m.group(1), table.setdefault(m.group(0), len(table) + 1))

    out = []
    for line in lines:
        line = SET_TEXTURE.sub(r"\1surf#T", line)  # before the numbering: a binding must not number a surface
        line = ID.sub(renumber, line)
        line = HASH.sub("fnv1a64:H", line)
        line = START_VERTEX.sub("start_vertex=S", line)
        line = VB_LOCK.sub(r"\1F", line)
        out.append(line)
    return out


def main():
    args = sys.argv[1:]
    if args not in ([], ["--structure"]):
        sys.stderr.write("usage: normalize.py [--structure] < trace > normalized\n")
        sys.exit(2)
    lines = sys.stdin.readlines()
    small = set()
    for line in lines:
        m = SMALL_SURFACE.search(line)
        if m:
            small.add("surf#" + m.group(1))
    kept = [line for line in lines if not cursor_blit(line, small)]
    sys.stdout.write("".join(structure(kept) if args else kept))


if __name__ == "__main__":
    main()
