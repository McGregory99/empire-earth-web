#!/usr/bin/env python3
"""Normalize a ddraw trace for the determinism gate: stdin -> stdout.

Removes the software cursor's sprite blits, and only those.  The cursor is drawn by a thread of its own (T3 in the captured
traces) that races the render thread when the menu first appears, so the same blits show up in a different order, on a
different thread and in a different frame from run to run.  They are the only unreproducible lines seen so far; everything
else is compared as is.

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


def main():
    lines = sys.stdin.readlines()
    small = set()
    for line in lines:
        m = SMALL_SURFACE.search(line)
        if m:
            small.add("surf#" + m.group(1))
    for line in lines:
        if not cursor_blit(line, small):
            sys.stdout.write(line)


if __name__ == "__main__":
    main()
