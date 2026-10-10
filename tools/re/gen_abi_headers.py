#!/usr/bin/env python3
"""Generate the engine ABI headers of the renderer plugin from the vtable inventories (task P2.2).

The renderer plugin crosses into the engine only through positional vtables (see the feature
document).  This tool turns the inventories of the original plugin (`DX7HRTnLDisplay.dll`) and of
the engine (`Low-Level Engine.dll`) into C++ abstract classes whose MSVC vtable layout is the
original's, one class per engine interface, and into the matching slot-index constants for C.

Inputs, all produced outside the repository and never copied into it:

* a vtable inventory of the plugin and one of the engine (`== const Class::`vftable' ...` blocks of
  `[slot] +offset address method` lines; several names on a line mean identical code);
* the demangled export lists of both, from which the exported virtual destructors are taken.

A slot gets its name and signature from every inventory entry that names it (the engine's base
class and each plugin implementation).  A slot that no export names is the destructor when the
class has an exported virtual destructor, else a `slotNN` placeholder; a slot that only
identical-code folding names ambiguously is a `slotNN` placeholder too, listing the candidates.

Usage: gen_abi_headers.py --tnl-vtables F --lle-vtables F --tnl-demangled F --lle-demangled F
           --out-dir DIR [--check]
"""
import argparse
import dataclasses
import difflib
import os
import re
import sys


class GenError(Exception):
    """The inventories cannot be turned into headers; the message says why."""


@dataclasses.dataclass(frozen=True)
class Method:
    access: str
    ret: str
    name: str
    params: tuple
    const: bool

    def signature(self):
        return (self.ret, self.name, self.params, self.const)


@dataclasses.dataclass(frozen=True)
class VSlot:
    """One vtable entry of the inventory; no alternatives = the code is not exported.  (The code
    address the inventory also lists is deliberately dropped: nothing generated may carry one.)"""
    index: int
    alternatives: tuple


@dataclasses.dataclass(frozen=True)
class InterfaceSpec:
    """An interface to generate.  `engine` names the engine's abstract class (its vtable gives the
    base names); `parent` names the interface a pure extension adds slots to.  `extra` is the number
    of slots the plugin classes add to the engine class (modelled by a separate extension)."""
    name: str
    engine: object
    parent: object
    impls: tuple
    extra: int = 0


@dataclasses.dataclass(frozen=True)
class ResolvedSlot:
    index: int
    kind: str          # method | destructor | ambiguous | unknown
    method: object     # Method, or None when no signature is known
    alternatives: tuple = ()


@dataclasses.dataclass(frozen=True)
class ResolvedInterface:
    name: str
    parent: object
    first_slot: int
    slot_count: int
    slots: tuple


# ---- parsing -------------------------------------------------------------------------------

_BLOCK = re.compile(r"^== const (?P<cls>.+?)::`vftable'(?P<rest>.*?)@0x(?P<addr>[0-9a-fA-F]+) slots=(?P<n>\d+)")
_SLOT = re.compile(r"^\s*\[\s*(?P<i>\d+)\]\s+\+0x[0-9a-fA-F]+\s+0x[0-9a-fA-F]+\s+(?P<text>.*?)\s*$")
_DESTRUCTOR = re.compile(r"^(public|protected|private): virtual __thiscall (\w+)::~(\w+)\(void\)\s*$")
_IDENT = re.compile(r"[A-Za-z_]\w*")


def _first_paren(text):
    """Index of the first `(` outside any <...>, or -1."""
    depth = 0
    for i, ch in enumerate(text):
        if depth == 0 and ch == "(":
            return i
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
    return -1


def _split_top(text, sep):
    parts, depth, current = [], 0, ""
    for ch in text:
        if ch in "<(":
            depth += 1
        elif ch in ">)":
            depth -= 1
        if ch == sep and depth == 0:
            parts.append(current.strip())
            current = ""
        else:
            current += ch
    if current.strip():
        parts.append(current.strip())
    return parts


def parse_method(text):
    """`[access: ]ret Class::Name(params) [const]` as the vtable inventory prints it."""
    access = "public"
    prefix = re.match(r"(public|protected|private): ", text)
    if prefix:
        access, text = prefix.group(1), text[prefix.end():]
    open_paren = _first_paren(text)
    close_paren = text.rfind(")")
    if open_paren < 0 or close_paren < open_paren:
        raise GenError(f"not a method signature: {text!r}")
    tail = text[close_paren + 1:].strip()
    if tail not in ("", "const"):
        raise GenError(f"unsupported qualifier {tail!r} in {text!r}")
    head = text[:open_paren]
    depth, cut = 0, -1
    for i in range(len(head) - 1, 0, -1):
        if head[i] == ">":
            depth += 1
        elif head[i] == "<":
            depth -= 1
        elif depth == 0 and head[i - 1:i + 1] == "::":
            cut = i - 1
            break
    name = head[cut + 2:]
    if cut < 0 or not _IDENT.fullmatch(name):
        raise GenError(f"unsupported method name in {text!r}")
    owner = re.search(r"[A-Za-z_]\w*$", head[:cut])
    if not owner:
        raise GenError(f"no owning class in {text!r}")
    ret = head[:owner.start()].strip()
    params = _split_top(text[open_paren + 1:close_paren], ",")
    if params == ["void"]:
        params = []
    return Method(access, ret, name, tuple(params), tail == "const")


def parse_vtables(text, wanted=None):
    """class name -> tuple of VSlot, from an inventory's vtable blocks.  With `wanted`, other
    classes are skipped unparsed (the engine inventory holds hundreds, many of them templates)."""
    tables, current, declared = {}, None, 0
    for line in text.splitlines() + ["== end"]:
        if line.startswith("== "):
            if current is not None:
                name, slots = current
                if len(slots) != declared or [s.index for s in slots] != list(range(declared)):
                    raise GenError(f"{name}: the header says {declared} slots, the block has {len(slots)}")
                tables[name] = tuple(slots)
            block = _BLOCK.match(line)
            # Secondary vtables (`{for ...}`) are not interfaces of ours.
            interesting = block and not block.group("rest").strip() and (wanted is None or block.group("cls") in wanted)
            current = (block.group("cls"), []) if interesting else None
            declared = int(block.group("n")) if block else 0
            continue
        slot = _SLOT.match(line)
        if slot and current is not None:
            text = slot.group("text")
            alternatives = () if text == "(not exported)" else tuple(parse_method(a) for a in text.split(" | "))
            current[1].append(VSlot(int(slot.group("i")), alternatives))
    return tables


def parse_destructors(text):
    """class name -> access of its exported virtual destructor, from a demangled export list."""
    found = {}
    for line in text.splitlines():
        m = _DESTRUCTOR.match(line)
        if m and m.group(2) == m.group(3):
            found[m.group(2)] = m.group(1)
    return found


# ---- resolving the slots of one interface ----------------------------------------------------

def _sources(spec, lle_tables, tnl_tables):
    base = None
    if spec.engine is not None:
        if spec.engine not in lle_tables:
            raise GenError(f"{spec.name}: the engine inventory has no vtable for {spec.engine}")
        base = lle_tables[spec.engine]
    impls = []
    for cls in spec.impls:
        if cls not in tnl_tables:
            raise GenError(f"{spec.name}: the plugin inventory has no vtable for {cls}")
        impls.append((cls, tnl_tables[cls]))
    return base, impls


def _resolve_slot(spec, index, base, impls):
    """(kind, method, alternatives) of one slot from every inventory entry that names it."""
    named = []  # (source, {signature: Method})
    if base is not None and base[index].alternatives:
        named.append((spec.engine, {m.signature(): m for m in base[index].alternatives}))
    for cls, table in impls:
        if table[index].alternatives:
            named.append((cls, {m.signature(): m for m in table[index].alternatives}))
    if not named:
        return "unnamed", None, ()
    common = set.intersection(*(set(candidates) for _, candidates in named))
    if not common:
        detail = "; ".join(f"{who}: {', '.join(sorted(m.name for m in c.values()))}" for who, c in named)
        raise GenError(f"{spec.name} slot {index}: the inventories name it differently ({detail})")
    methods = [named[0][1][sig] for sig in sorted(common)]
    if len(methods) == 1:
        return "method", methods[0], ()
    shapes = {(m.access, m.ret, m.params, m.const) for m in methods}
    placeholder = None
    if len(shapes) == 1:
        first = methods[0]
        placeholder = Method(first.access, first.ret, f"slot{index}", first.params, first.const)
    return "ambiguous", placeholder, tuple(sorted(m.name for m in methods))


def _check_overloads(spec, slots, parent):
    where = {}
    for slot in slots:
        if slot.kind == "method":
            where.setdefault(slot.method.name, []).append(slot.index)
    inherited = {s.method.name for s in parent.slots if s.kind == "method"} if parent else set()
    for name, indices in where.items():
        if indices != list(range(indices[0], indices[0] + len(indices))):
            raise GenError(f"{spec.name}: the overloads of {name} are not contiguous in the vtable "
                           f"(slots {indices}); declaration order cannot reproduce that")
        if name in inherited:
            raise GenError(f"{spec.name}: {name} overloads a method of {spec.parent}; not modelled")


def resolve_interface(spec, lle_tables, tnl_tables, destructors, parent=None):
    base, impls = _sources(spec, lle_tables, tnl_tables)
    if base is not None:
        first, count = 0, len(base)
        for cls, table in impls:
            if len(table) != count + spec.extra:
                raise GenError(f"{cls} has {len(table)} slots, {spec.name} has {count}"
                               + (f" (+{spec.extra} expected)" if spec.extra else ""))
    else:
        if parent is None:
            raise GenError(f"{spec.name}: neither an engine class nor a parent interface")
        first = parent.slot_count
        sizes = {len(table) for _, table in impls}
        if len(sizes) != 1 or min(sizes) <= first:
            raise GenError(f"{spec.name}: the implementations must all have more than {first} slots (got {sorted(sizes)})")
        count = sizes.pop()
    slots = []
    for index in range(first, count):
        kind, method, alternatives = _resolve_slot(spec, index, base, impls)
        slots.append(ResolvedSlot(index, kind, method, alternatives))
    unnamed = [s for s in slots if s.kind == "unnamed"]
    access = destructors.get(spec.engine) if spec.engine else None
    if access is not None:
        if len(unnamed) != 1:
            raise GenError(f"{spec.name}: {len(unnamed)} unexported slots, so the destructor cannot be told apart")
        slots[slots.index(unnamed[0])] = ResolvedSlot(unnamed[0].index, "destructor",
                                                      Method(access, "", "~" + spec.name, (), False))
    else:
        for slot in unnamed:
            slots[slots.index(slot)] = ResolvedSlot(slot.index, "unknown", None)
    _check_overloads(spec, slots, parent)
    return ResolvedInterface(spec.name, spec.parent, first, count, tuple(slots))


# ---- the interfaces of the renderer plugin ---------------------------------------------------

# GERasterizer is the engine's abstract class (78 slots); the plugin's DX7Rasterizer adds four
# more, modelled as the extension DX7RasterizerAbi.  The others are implemented one to one.
INTERFACES = (
    InterfaceSpec("GERasterizer", "GERasterizer", None, ("DX7Rasterizer", "DX7TnLRasterizer"), extra=4),
    InterfaceSpec("DX7RasterizerAbi", None, "GERasterizer", ("DX7Rasterizer", "DX7TnLRasterizer")),
    InterfaceSpec("GEScreen", "GEScreen", None, ("DX7Screen",)),
    InterfaceSpec("GESurface", "GESurface", None, ("DX7Surface",)),
    InterfaceSpec("GEMovie", "GEMovie", None, ("DX7Movie",)),
    InterfaceSpec("GEVertexArray", "GEVertexArray", None, ("DX7StandardVertexArray", "DX7StdColorVertexArray")),
    InterfaceSpec("GEPolygonArray", "GEPolygonArray", None, ("DX7PolygonArray",)),
)

INTERFACES_HEADER = "ge_interfaces.h"
SLOTS_HEADER = "ge_slots.h"


def resolve_all(tnl_vtables, lle_vtables, tnl_demangled, lle_demangled, specs):
    lle = parse_vtables(lle_vtables, {s.engine for s in specs if s.engine})
    tnl = parse_vtables(tnl_vtables, {c for s in specs for c in s.impls})
    destructors = {**parse_destructors(tnl_demangled), **parse_destructors(lle_demangled)}
    resolved = {}
    for spec in specs:
        if spec.parent and spec.parent not in resolved:
            raise GenError(f"{spec.name}: its parent {spec.parent} must be listed first")
        resolved[spec.name] = resolve_interface(spec, lle, tnl, destructors, resolved.get(spec.parent))
    return [resolved[s.name] for s in specs]


# ---- emitting --------------------------------------------------------------------------------

_KEY = re.compile(r"\b(class|struct|union)\s+([A-Za-z_][\w:]*)(<)?")
_KEYWORD = re.compile(r"\b(?:class|struct|union)\s+")


def _type(text):
    """A type as the inventory prints it, without the elaborated class keys."""
    return _KEYWORD.sub("", text)


def _arity(text, open_angle):
    depth, commas = 0, 0
    for i in range(open_angle, len(text)):
        if text[i] == "<":
            depth += 1
        elif text[i] == ">":
            depth -= 1
            if depth == 0:
                return commas + 1
        elif text[i] == "," and depth == 1:
            commas += 1
    raise GenError(f"unbalanced template in {text!r}")


def _signature_texts(interfaces):
    for iface in interfaces:
        for slot in iface.slots:
            if slot.method is not None and slot.kind != "destructor":
                yield slot.method.ret
                yield from slot.method.params


def _forward_declarations(interfaces):
    plain, templates, nested = {}, {}, {}
    for text in _signature_texts(interfaces):
        for m in _KEY.finditer(text):
            key, name = m.group(1), m.group(2)
            if m.group(3):
                arity = _arity(text, m.end() - 1)
                if templates.setdefault(name, (key, arity))[1] != arity:
                    raise GenError(f"template {name} is used with {templates[name][1]} and {arity} arguments")
            elif name.startswith("std::"):
                raise GenError(f"unsupported standard type {name} in {text!r}")
            elif "::" in name:
                outer, inner = name.rsplit("::", 1)
                if "::" in outer:
                    raise GenError(f"unsupported nesting depth in {name}")
                nested.setdefault(outer, {})[inner] = key
            else:
                plain.setdefault(name, key)
    lines = [f"{plain[n]} {n};" for n in sorted(plain)]
    for group, in_std in ((lambda n: not n.startswith("std::"), False), (lambda n: n.startswith("std::"), True)):
        decls = [f"template <{', '.join(['class'] * templates[n][1])}> {templates[n][0]} {n.split('::')[-1]};"
                 for n in sorted(templates) if group(n)]
        if decls and in_std:
            lines += ["", "namespace std {"] + decls + ["}"]
        elif decls:
            lines += [""] + decls
    for outer in sorted(nested):
        members = " ".join(f"{nested[outer][i]} {i};" for i in sorted(nested[outer]))
        guard = f"EEWEB_ENGINE_{outer.upper()}_DEFINED"
        lines += ["", f"// Stand-in until the real layout of {outer} is defined (it then defines {guard}).",
                  f"#ifndef {guard}", f"class {outer} {{ public: {members} }};", "#endif"]
    return lines


def _declaration_order(slots):
    """MSVC lays an overload set out in reverse declaration order: declare it in reverse slot order."""
    ordered, run = [], []
    for slot in slots:
        if run and slot.kind == "method" and run[0].kind == "method" and slot.method.name == run[0].method.name:
            run.append(slot)
            continue
        ordered += reversed(run)
        run = [slot]
    return ordered + list(reversed(run))


def _plain_signature(method):
    params = ", ".join(_type(p) for p in method.params)
    return f"{_type(method.ret)} {method.name}({params}){' const' if method.const else ''}"


def _slot_declaration(iface, slot):
    note = f"slot {slot.index}"
    if slot.kind == "destructor":
        return f"virtual ~{iface.name}() {{}}  // {note}"
    if slot.kind == "unknown":
        return f"virtual void slot{slot.index}() = 0;  // {note}: not exported by the plugin or the engine"
    if slot.kind == "ambiguous":
        candidates = ", ".join(slot.alternatives)
        note += f": one of {candidates} (identical code in the original)"
        if slot.method is None:
            return f"virtual void slot{slot.index}() = 0;  // slot {slot.index}: signature unknown, one of {candidates} (identical code in the original)"
    return f"virtual {_plain_signature(slot.method)} = 0;  // {note}"


def _render_class(iface):
    lines = [f"class {iface.name}" + (f" : public {iface.parent}" if iface.parent else "") + " {"]
    access = None
    for slot in _declaration_order(iface.slots):
        wanted = slot.method.access if slot.method is not None else "public"
        if wanted != access:
            lines.append(f"{wanted}:")
            access = wanted
        lines.append("    " + _slot_declaration(iface, slot))
    return lines + ["};"]


def _generated_line(names):
    return f"Generated by tools/re/gen_abi_headers.py from {', '.join(names)}; do not edit."


_INTERFACES_PREAMBLE = """\
// Positional vtable shapes of the engine interfaces a renderer plugin implements.  The classes carry
// no data members, only the virtual methods in the slot order of the original plugin's MSVC 6
// vtables, so a build for i686-pc-windows-msvc lays out the same vtable (engine field layouts are
// a separate concern).  Rules the order encodes: the destructor sits at the slot it was declared
// at; MSVC lays an overload set out in reverse declaration order, so each set is declared in
// reverse slot order; member functions are __thiscall on this target.  `slotNN` stands for a slot
// whose method the inventories do not name (the comment on it says what is known).
"""


def render_interfaces(interfaces, names):
    lines = [f"// {_generated_line(names)}", _INTERFACES_PREAMBLE.rstrip("\n"), "#ifndef EEWEB_GE_INTERFACES_H",
             "#define EEWEB_GE_INTERFACES_H", ""]
    lines += _forward_declarations(interfaces)
    for iface in interfaces:
        lines += ["", f"// {iface.name}: {iface.slot_count} virtual slots"
                  + (f" ({iface.first_slot} inherited)" if iface.parent else "")]
        lines += _render_class(iface)
    return "\n".join(lines + ["", "#endif", ""])


def _slot_labels(iface):
    counts = {}
    for slot in iface.slots:
        if slot.kind == "method":
            counts[slot.method.name] = counts.get(slot.method.name, 0) + 1
    seen, labels = {}, []
    for slot in iface.slots:
        if slot.kind == "destructor":
            labels.append("Destructor")
        elif slot.kind == "method" and counts[slot.method.name] > 1:
            seen[slot.method.name] = seen.get(slot.method.name, 0) + 1
            labels.append(f"{slot.method.name}_{seen[slot.method.name]}")
        elif slot.kind == "method":
            labels.append(slot.method.name)
        else:
            labels.append(f"slot{slot.index}")
    return labels


def render_slots(interfaces, names):
    lines = [f"/* {_generated_line(names)} */",
             "/* Slot indices (vtable positions, the destructor included) of the classes in ge_interfaces.h,",
             "   as plain C so that the test harness can use them.  Overloads are numbered in slot order. */",
             "#ifndef EEWEB_GE_SLOTS_H", "#define EEWEB_GE_SLOTS_H"]
    for iface in interfaces:
        lines += ["", f"/* {iface.name}: {iface.slot_count} slots"
                  + (f" (the first {iface.first_slot} are {iface.parent}'s)" if iface.parent else "") + " */",
                  "enum {", f"    EEWEB_SLOTS_{iface.name} = {iface.slot_count},"]
        for slot, label in zip(iface.slots, _slot_labels(iface)):
            overload = f"  /* {_plain_signature(slot.method)} */" if re.search(r"_\d+$", label) else ""
            lines.append(f"    EEWEB_SLOT_{iface.name}_{label} = {slot.index},{overload}")
        lines.append("};")
    return "\n".join(lines + ["", "#endif", ""])


def generate(tnl_vtables, lle_vtables, tnl_demangled, lle_demangled, names, specs=INTERFACES):
    """file name -> text of the generated headers."""
    interfaces = resolve_all(tnl_vtables, lle_vtables, tnl_demangled, lle_demangled, specs)
    return {INTERFACES_HEADER: render_interfaces(interfaces, names), SLOTS_HEADER: render_slots(interfaces, names)}


# ---- command line ----------------------------------------------------------------------------

def _read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def _check(generated, out_dir):
    failed = False
    for name, text in generated.items():
        path = os.path.join(out_dir, name)
        if not os.path.isfile(path):
            print(f"FAIL: {path} does not exist")
            failed = True
            continue
        with open(path, "rb") as f:
            committed = f.read()
        if committed != text.encode("utf-8"):
            failed = True
            sys.stdout.writelines(difflib.unified_diff(
                committed.decode("utf-8", "replace").splitlines(keepends=True), text.splitlines(keepends=True),
                f"committed {name}", f"generated {name}"))
    if failed:
        print("FAIL: the committed headers are not what the inventories generate; "
              "rerun tools/re/gen_abi_headers.py without --check to regenerate them")
        return 1
    print("PASS: the committed headers are exactly what the inventories generate")
    return 0


def main(argv=None, specs=INTERFACES):
    parser = argparse.ArgumentParser(description="Generate the engine ABI headers from the vtable inventories.")
    for option in ("tnl-vtables", "lle-vtables", "tnl-demangled", "lle-demangled"):
        parser.add_argument(f"--{option}", required=True, metavar="FILE")
    parser.add_argument("--out-dir", required=True, metavar="DIR")
    parser.add_argument("--check", action="store_true",
                        help="compare the committed headers with the generated ones instead of writing them")
    args = parser.parse_args(argv)
    paths = [args.tnl_vtables, args.lle_vtables, args.tnl_demangled, args.lle_demangled]
    missing = [os.path.basename(p) for p in paths if not os.path.isfile(p)]
    if missing:
        if args.check:
            print(f"SKIP: inventory file(s) not found: {', '.join(missing)}; "
                  "the committed headers were not verified against the inventories")
            return 0
        print(f"error: inventory file(s) not found: {', '.join(missing)}", file=sys.stderr)
        return 2
    try:
        generated = generate(*(_read(p) for p in paths), [os.path.basename(p) for p in paths], specs)
    except GenError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    if args.check:
        return _check(generated, args.out_dir)
    os.makedirs(args.out_dir, exist_ok=True)
    for name, text in generated.items():
        with open(os.path.join(args.out_dir, name), "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        print(f"wrote {os.path.join(args.out_dir, name)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
