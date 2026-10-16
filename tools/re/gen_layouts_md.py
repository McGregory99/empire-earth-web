#!/usr/bin/env python3
"""Generate src/engine_abi/LAYOUTS.md from the comments of src/engine_abi/ge_layouts.h (task P2.5).

The header is the source of truth: a comment "@class <Name> size=<n> size-confidence=<level> | <evidence>"
above every struct and a trailing comment "@<offset> <level> | <evidence>" on every field.  This tool
parses them, checks them against the EEL_AT / EEL_SIZE assertions of the same header (a comment that
disagrees with an assertion is an error), and renders one table per class.

Usage:
    gen_layouts_md.py --header H --out F      write F
    gen_layouts_md.py --header H --check F    exit 1 when F is not what the header generates
"""
import argparse
import re
import sys

LEVELS = ("confirmed", "plugin-only", "lle-only", "guess")
BASE_SIZES = {"uint8_t": 1, "uint32_t": 4, "float": 4}


class LayoutError(Exception):
    pass


class Field:
    def __init__(self, name, type_, offset, width, confidence, evidence):
        self.name, self.type, self.offset, self.width = name, type_, offset, width
        self.confidence, self.evidence = confidence, evidence


class Class:
    def __init__(self, name, size, size_confidence, evidence):
        self.name, self.size, self.size_confidence, self.evidence = name, size, size_confidence, evidence
        self.fields = []


CLASS_RE = re.compile(r"^/\* @class (\w+) size=(0x[0-9a-f]+) size-confidence=(\S+) \| (.*) \*/$")
FIELD_RE = re.compile(r"^\s+(.+?)\s*(\w+)(\[(\d+)\])?;\s*/\* @(0x[0-9a-f]+) (\S+) \| (.*) \*/$")
AT_RE = re.compile(r"^EEL_AT\((\w+)Layout, (\w+), (0x[0-9a-f]+)\);$")
SIZE_RE = re.compile(r"^EEL_SIZE\((\w+)Layout, (0x[0-9a-f]+)\);$")


NOT_ESTABLISHED = [
    "## Not established", "",
    "- `GETerrainMesh` total size: it is embedded in another class, so no `operator new(size)` gives it; at least 0x181692 bytes. The 0x34bc0 bytes at 0x58b2c and the 0x1104 bytes at 0x17b5a8 are not named by any accessor the plugin or the engine's exports use.",
    "- `GEText.bgColor`, `GEViewport` plane contents and the internals of `GETMMaterial` (fields 0x4 and 0x24): the plugin reaches them only through x87 helper calls or whole-object passes.",
    "- Plugin-owned objects whose layout the original chose and the engine never reads (`DX7Screen`, `DX7Surface`, `DX7Movie`, `DX7PolygonArray`, `DX7StandardVertexArray`, `DX7StdColorVertexArray`): a reimplementation may lay them out differently, so they are not part of this contract. `DX7Rasterizer` and `DX7ScreenRecord` are listed because the vtable shape and the engine-side base class make their first fields visible.",
    "- Classes of the `GEFontBitmap`/Bink path (`DX7Movie`, `BINK`) are not covered (the three intro movies only).",
    "",
]


def parse(text):
    classes, by_name, at, sizes = [], {}, {}, {}
    current = None
    for line in text.splitlines():
        m = CLASS_RE.match(line)
        if m:
            if m.group(3) not in LEVELS:
                raise LayoutError("unknown confidence %r on class %s" % (m.group(3), m.group(1)))
            current = Class(m.group(1), int(m.group(2), 16), m.group(3), m.group(4))
            classes.append(current)
            by_name[current.name] = current
            continue
        m = AT_RE.match(line)
        if m:
            at[(m.group(1), m.group(2))] = int(m.group(3), 16)
            continue
        m = SIZE_RE.match(line)
        if m:
            sizes[m.group(1)] = int(m.group(2), 16)
            continue
        m = FIELD_RE.match(line)
        if m and current is not None:
            decl, name, _, count, offset, level, evidence = m.groups()
            if level not in LEVELS:
                raise LayoutError("unknown confidence %r on %s.%s" % (level, current.name, name))
            type_, width = decl.strip(), None
            if type_.endswith("*"):
                width = 4
            elif type_.startswith("struct "):
                width = by_name[type_.split()[1][:-len("Layout")]].size
            else:
                width = BASE_SIZES[type_]
            if count:
                width *= int(count)
                type_ = "%s[%s]" % (type_, count)
            type_ = re.sub(r"\s+\*", " *", type_).replace("* *", "**") if "*" in type_ else type_
            current.fields.append(Field(name, type_, int(offset, 16), width, level, evidence))
    for c in classes:
        if sizes.get(c.name) != c.size:
            raise LayoutError("%s: size comment %#x but EEL_SIZE says %s" % (c.name, c.size, sizes.get(c.name)))
        for f in c.fields:
            if at.get((c.name, f.name)) != f.offset:
                raise LayoutError("%s.%s: offset comment %#x but EEL_AT says %s" % (c.name, f.name, f.offset, at.get((c.name, f.name))))
    return classes


def render(classes):
    out = ["# Engine class layouts the renderer plugin reads inline (P2.5)", "",
           "Generated from `ge_layouts.h` by `tools/re/gen_layouts_md.py` (`make check-layouts` fails if this file is stale). Facts only: names and types are",
           "inferred from usage; offsets and sizes are asserted at compile time and checked against the real `Low-Level Engine.dll` by `tests/layouts/`.", "",
           "Confidence: `confirmed` = plugin and engine agree; `plugin-only` = only the plugin reads it (or the object is plugin-owned); `lle-only` = the engine",
           "names it but the plugin does not touch it; `guess` = offset certain, type or meaning inferred. Evidence: `P` = plugin function, `L` = engine function.", ""]
    totals = dict.fromkeys(LEVELS, 0)
    for c in classes:
        for f in c.fields:
            totals[f.confidence] += 1
    out += ["## Summary", "", "%d classes and structs, %d fields." % (len(classes), sum(totals.values())), "",
            "| confidence | fields |", "|---|---|"] + ["| %s | %d |" % (k, totals[k]) for k in LEVELS] + [""]
    for c in classes:
        out += ["### %s (%#x bytes, size %s)" % (c.name, c.size, c.size_confidence), "", "Size evidence: %s." % c.evidence.rstrip("."), "",
                "| offset | width | type | field | confidence | evidence |", "|---|---|---|---|---|---|"]
        for f in c.fields:
            out.append("| %#x | %d | `%s` | `%s` | %s | %s |" % (f.offset, f.width, f.type, f.name, f.confidence, f.evidence.replace("|", "\\|")))
        out.append("")
    out += NOT_ESTABLISHED
    return "\n".join(out) + "\n"


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--header", required=True)
    group = ap.add_mutually_exclusive_group(required=True)
    group.add_argument("--out")
    group.add_argument("--check")
    args = ap.parse_args(argv)
    try:
        text = render(parse(open(args.header).read()))
    except LayoutError as error:
        print("FAIL: %s" % error)
        return 1
    if args.out:
        open(args.out, "w").write(text)
        return 0
    if open(args.check).read() != text:
        print("FAIL: %s is not what %s generates; run `make gen-layouts-md`" % (args.check, args.header))
        return 1
    print("PASS: %s is what %s generates" % (args.check, args.header))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
