#!/usr/bin/env python3
"""Turn the REC lines the harness prints under EEWEB_LIFE_RECORD=1 into the entries of a *_expected.inc file.

    EEWEB_LIFE_RECORD=1 wine rasterizer_harness.exe original DX7HRTnLDisplay.dll | python3 -I record_to_inc.py "terrain: " > terrain_expected.inc

Only the steps whose name starts with one of the given prefixeses are written, in the order they ran.
"""
import sys

prefixes = tuple(sys.argv[1:])
for line in sys.stdin:
    line = line.rstrip("\r\n")
    if not line.startswith("REC\t"):
        continue
    parts = line.split("\t", 2)
    if len(parts) != 3 or not parts[1].startswith(prefixes):
        continue

    def quote(text):
        return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'

    print("{%s,\n %s}," % (quote(parts[1]), quote(parts[2])))
