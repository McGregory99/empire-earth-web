"""Compare the vtable layouts clang computes with the slot tables of the generated headers (task P2.2).

Usage: python3 -I check_shapes.py --header-dir DIR --dump FILE [--dump FILE ...] [--implements CLASS=INTERFACE ...]

`--dump` files are the output of `clang++ --target=i686-pc-windows-msvc -Xclang -fdump-vtable-layouts`
on translation units that use the generated headers.  For every class of ge_interfaces.h the complete
vtable layout in the dump must have the slot count of ge_slots.h and, at every slot, the method the
header declares for it; that is the check that declaring the methods in the generated order really
yields the original's positions under the Microsoft ABI (overload sets are laid out in reverse, the
destructor where it was declared).  The slot constants of ge_slots.h must name the method found at their
index.  `--implements` adds a class of ours that must fill every slot of an interface in place and
leave none pure.

Exit status 0 = everything matches.
"""
import argparse
import collections
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "re"))
import gen_abi_headers as gen  # noqa: E402

_CLASS = re.compile(r"^class (\w+)(?: : public (\w+))? \{$")
_DECL = re.compile(r"^\s+virtual (?P<text>.+?) = 0;  // slot (?P<n>\d+)")
_DTOR = re.compile(r"^\s+virtual ~(\w+)\(\) \{\}  // slot (\d+)")
_COUNT = re.compile(r"^\s+EEWEB_SLOTS_(\w+) = (\d+),", re.MULTILINE)
_CONSTANT = re.compile(r"^\s+EEWEB_SLOT_([A-Za-z0-9]+)_(\w+) = (\d+),", re.MULTILINE)
_BLOCK = re.compile(r"^VFTable for '(?P<chain>.+)' \((?P<n>\d+) entries\)\.$")
_ENTRY = re.compile(r"^\s*(\d+) \| (.*?)\s*$")
_NOISE = re.compile(r"\b(?:class|struct|union|const)\b|\s+")

DESTRUCTOR = "~"

# A complete class's layout in the dump: the slot count it declares and what sits in each slot.
Layout = collections.namedtuple("Layout", "count slots")


class Entry:
    """What sits in one slot: a destructor, or a method with a canonical signature."""

    def __init__(self, text, key, pure=False):
        self.text, self.key, self.pure = text, key, pure

    @property
    def name(self):
        return self.key[1] if self.key != DESTRUCTOR else DESTRUCTOR


def _canon(text):
    return _NOISE.sub("", text)


def _method_entry(text, pure=False):
    method = gen.parse_method(text)
    key = (_canon(method.ret), method.name, tuple(_canon(p) for p in method.params), method.const)
    return Entry(text, key, pure)


def parse_interfaces(text):
    """class -> (parent, {slot: Entry}) from the generated C++ header."""
    classes, current = {}, None
    for line in text.splitlines():
        header = _CLASS.match(line)
        if header:
            current = header.group(1)
            classes[current] = (header.group(2), {})
        elif line == "};":
            current = None
        elif current:
            dtor, decl = _DTOR.match(line), _DECL.match(line)
            if dtor:
                classes[current][1][int(dtor.group(2))] = Entry(f"~{dtor.group(1)}()", DESTRUCTOR)
            elif decl:
                body = decl.group("text")
                name = re.search(r"([A-Za-z_]\w*)\(", body).start(1)
                classes[current][1][int(decl.group("n"))] = _method_entry(f"{body[:name]}{current}::{body[name:]}")
    return classes


def expected_layout(classes, name):
    parent, own = classes[name]
    inherited = expected_layout(classes, parent) if parent else {}
    return {**inherited, **own}


def parse_dump(text):
    """complete class -> Layout, from the `VFTable for ...` blocks (RTTI is entry 0)."""
    layouts, entries = {}, None
    for line in text.splitlines() + [""]:
        block = _BLOCK.match(line)
        if block or not line.strip() or line.startswith("VFTable indices"):
            entries = None
        if block:
            layout = Layout(int(block.group("n")) - 1, {})
            layouts[block.group("chain").split("' in '")[-1]] = layout
            entries = layout.slots
            continue
        entry = _ENTRY.match(line)
        if entries is not None and entry and int(entry.group(1)) > 0:
            body = entry.group(2)
            pure = body.endswith(" [pure]")
            body = re.sub(r" \[(?:pure|vector deleting)\]$", "", body)
            slot = int(entry.group(1)) - 1
            entries[slot] = Entry(body, DESTRUCTOR) if re.search(r"::~\w+\(\)$", body) else _method_entry(body, pure)
    return layouts


def _labels(own):
    """The suffixes ge_slots.h gives the slots of a class: names, `_n` for overloads, Destructor."""
    counts = {}
    for entry in own.values():
        counts[entry.name] = counts.get(entry.name, 0) + 1
    seen, labels = {}, {}
    for slot in sorted(own):
        name = own[slot].name
        if name == DESTRUCTOR:
            labels[slot] = "Destructor"
        elif counts[name] > 1:
            seen[name] = seen.get(name, 0) + 1
            labels[slot] = f"{name}_{seen[name]}"
        else:
            labels[slot] = name
    return labels


def _compare(who, layout, want, errors, what):
    if layout.count != len(want):
        errors.append(f"{who}: the layout has {layout.count} slots, {what} {len(want)}")
    for slot in sorted(want):
        have = layout.slots.get(slot)
        if have is None:
            errors.append(f"{who} slot {slot}: {what} {want[slot].text}, the layout has nothing")
        elif have.key != want[slot].key:
            errors.append(f"{who} slot {slot}: {what} {want[slot].text}, the layout has {have.text}")
    for slot in sorted(s for s in layout.slots if s not in want):
        errors.append(f"{who} slot {slot}: the layout has {layout.slots[slot].text}, beyond the {len(want)} expected slots")


def check(interfaces_h, slots_h, dump, implements):
    """The list of mismatches; empty when the layouts, the header and the constants agree."""
    classes = parse_interfaces(interfaces_h)
    layouts = parse_dump(dump)
    counts = {name: int(n) for name, n in _COUNT.findall(slots_h)}
    constants = {}
    for cls, label, n in _CONSTANT.findall(slots_h):
        constants.setdefault(cls, {})[label] = int(n)
    errors = []
    for name in classes:
        want = expected_layout(classes, name)
        if counts.get(name) != len(want):
            errors.append(f"EEWEB_SLOTS_{name} is {counts.get(name)}, the header declares {len(want)} slots")
        layout = layouts.get(name)
        if layout is None:
            errors.append(f"{name}: no vtable layout in the dump")
            continue
        _compare(name, layout, want, errors, "the header declares")
        own = {s: e for s, e in want.items() if s in classes[name][1]}
        labels = _labels(own)
        for label, n in constants.get(name, {}).items():
            if labels.get(n) != label:
                errors.append(f"EEWEB_SLOT_{name}_{label} = {n}, but the layout has {labels.get(n)} at slot {n}")
        for n, label in labels.items():
            if label not in constants.get(name, {}):
                errors.append(f"EEWEB_SLOT_{name}_{label} (slot {n}) is missing from ge_slots.h")
    for impl, interface in implements:
        layout = layouts.get(impl)
        if layout is None or interface not in classes:
            errors.append(f"{impl}: no vtable layout in the dump" if layout is None else f"{interface}: not an interface")
            continue
        want = expected_layout(classes, interface)
        _compare(impl, layout, want, errors, f"{interface} declares")
        errors += [f"{impl} slot {s}: still pure ({e.text})" for s, e in sorted(layout.slots.items()) if e.pure]
    return errors


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--header-dir", required=True)
    parser.add_argument("--dump", action="append", required=True)
    parser.add_argument("--implements", action="append", default=[], metavar="CLASS=INTERFACE")
    args = parser.parse_args(argv)
    with open(os.path.join(args.header_dir, gen.INTERFACES_HEADER), encoding="utf-8") as f:
        interfaces_h = f.read()
    with open(os.path.join(args.header_dir, gen.SLOTS_HEADER), encoding="utf-8") as f:
        slots_h = f.read()
    dump = ""
    for path in args.dump:
        with open(path, encoding="utf-8") as f:
            dump += f.read() + "\n"
    implements = [tuple(item.split("=", 1)) for item in args.implements]
    errors = check(interfaces_h, slots_h, dump, implements)
    classes = parse_interfaces(interfaces_h)
    for error in errors:
        print(f"FAIL: {error}")
    if errors:
        return 1
    for name in classes:
        print(f"ok   {name}: {len(expected_layout(classes, name))} slots, layout and constants match the header")
    for impl, interface in implements:
        print(f"ok   {impl} fills all {len(expected_layout(classes, interface))} slots of {interface} in place, none pure")
    print("PASS: clang's vtable layouts match the generated slot tables")
    return 0


if __name__ == "__main__":
    sys.exit(main())
