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
import dataclasses
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
    """One vtable entry of the inventory; no alternatives = the code is not exported."""
    index: int
    address: int
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
_SLOT = re.compile(r"^\s*\[\s*(?P<i>\d+)\]\s+\+0x[0-9a-fA-F]+\s+0x(?P<a>[0-9a-fA-F]+)\s+(?P<text>.*?)\s*$")
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
            current[1].append(VSlot(int(slot.group("i")), int(slot.group("a"), 16), alternatives))
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
