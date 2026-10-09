#!/usr/bin/env python3
"""Normalize a ddraw trace for the determinism gate: stdin -> stdout.

Removes the software cursor's sprite blits: Blt calls with a 32x32 source or destination rectangle. The cursor is
drawn by a thread of its own (T3 in the captured traces) that races the render thread when the menu first appears, so
the same blits show up in a different order, on a different thread and in a different frame from run to run. They are
the only unreproducible lines seen so far; everything else is compared as is.
"""
import re
import sys

RECT = re.compile(r"(?:lpDestRect|lpSrcRect)=\{(-?\d+),(-?\d+),(-?\d+),(-?\d+)\}")


def is_cursor_blit(line):
    if ".Blt(" not in line:
        return False
    return any(int(m.group(3)) - int(m.group(1)) == 32 and int(m.group(4)) - int(m.group(2)) == 32
               for m in RECT.finditer(line))


def main():
    for line in sys.stdin:
        if not is_cursor_blit(line):
            sys.stdout.write(line)


if __name__ == "__main__":
    main()
