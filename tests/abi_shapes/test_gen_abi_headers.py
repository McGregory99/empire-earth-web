"""Unit tests of tools/re/gen_abi_headers.py, the generator of the engine ABI headers (task P2.2).

Run: python3 -I tests/abi_shapes/test_gen_abi_headers.py

The inventories the generator reads live outside the repository and are never copied in, so every
fixture here is synthetic: invented classes (Gadget and its plugin-side implementations) written in
the inventories' text format.  The check against the real inventories is `--check` of the generator
itself (make check-abi-shapes), which runs only where the inventories exist.
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "re"))
import gen_abi_headers as gen  # noqa: E402

PURE = 0x10081668  # the address the engine's _purecall thunk has in the engine inventory


def vtable_text(cls, rows, addr=0x10010000):
    """One inventory vtable block; rows are (code address, slot text) pairs."""
    lines = [f"== const {cls}::`vftable'  @0x{addr:08x} slots={len(rows)} (section .rdata)"]
    for index, (code, text) in enumerate(rows):
        lines.append(f"  [{index:3d}] +0x{4 * index:03x}  0x{code:08x}  {text}")
    return "\n".join(lines) + "\n"


def one_signature(text):
    """The Method that a single vtable entry text parses to."""
    slots = gen.parse_vtables(vtable_text("Impl", [(0x10002000, text)]))["Impl"]
    return slots[0].alternatives[0]


class ParseVtables(unittest.TestCase):
    def test_slots_keep_index_and_distinguish_unnamed_ones(self):
        text = vtable_text("Impl", [
            (0x10002000, "(not exported)"),
            (0x10002010, "long Impl::Open(class Handle *, unsigned long) const"),
        ]) + vtable_text("Other", [(0x10003000, "void Other::Stop(void)")])
        tables = gen.parse_vtables(text)
        self.assertEqual(sorted(tables), ["Impl", "Other"])
        self.assertEqual([s.index for s in tables["Impl"]], [0, 1])
        self.assertEqual(tables["Impl"][0].alternatives, ())
        self.assertEqual(tables["Impl"][1].address, 0x10002010)

    def test_signature_parts(self):
        method = one_signature("long Impl::Open(class Handle *, unsigned long) const")
        self.assertEqual((method.access, method.ret, method.name, method.params, method.const),
                         ("public", "long", "Open", ("class Handle *", "unsigned long"), True))

    def test_void_parameter_list_is_empty_and_protected_prefix_is_access(self):
        method = one_signature("protected: long Impl::Reset(void)")
        self.assertEqual((method.access, method.params, method.const), ("protected", (), False))

    def test_templates_keep_their_commas_inside_one_parameter(self):
        method = one_signature(
            "class std::vector<struct std::pair<class A, float>, class std::allocator<struct std::pair<class A, float>>> "
            "Impl::Lines(class std::vector<class U2DPoint<long>, class std::allocator<class U2DPoint<long>>> &, float)")
        self.assertEqual(method.name, "Lines")
        self.assertEqual(method.ret, "class std::vector<struct std::pair<class A, float>, "
                                     "class std::allocator<struct std::pair<class A, float>>>")
        self.assertEqual(method.params, ("class std::vector<class U2DPoint<long>, class std::allocator<class U2DPoint<long>>> &",
                                         "float"))

    def test_identical_code_lists_every_alternative(self):
        slots = gen.parse_vtables(vtable_text("Impl", [
            (0x10002000, "void Impl::SetA(float) | void Impl::SetB(unsigned long)")]))["Impl"]
        self.assertEqual([m.name for m in slots[0].alternatives], ["SetA", "SetB"])

    def test_a_block_whose_slot_count_disagrees_with_its_header_is_rejected(self):
        text = vtable_text("Impl", [(1, "void Impl::A(void)"), (2, "void Impl::B(void)")]).replace("slots=2", "slots=3")
        with self.assertRaisesRegex(gen.GenError, "Impl.*3.*2"):
            gen.parse_vtables(text)

    def test_destructors_are_the_exported_virtual_ones_with_their_access(self):
        text = "\n".join([
            "protected: virtual __thiscall Gadget::~Gadget(void)",
            "public: virtual __thiscall Wheel::~Wheel(void)",
            "public: __thiscall Gadget::Gadget(class Gadget const &)",
            "public: virtual long __thiscall Gadget::Open(void)",
        ])
        self.assertEqual(gen.parse_destructors(text), {"Gadget": "protected", "Wheel": "public"})


def interface(spec, lle, impls, dtors=None):
    """Resolve one interface from synthetic vtable rows; `impls` maps class -> rows."""
    lle_tables = gen.parse_vtables(vtable_text(spec.engine, lle)) if lle is not None else {}
    tnl_tables = gen.parse_vtables("".join(vtable_text(c, rows) for c, rows in impls.items()))
    return gen.resolve_interface(spec, lle_tables, tnl_tables, dtors or {})


SPEC = gen.InterfaceSpec("Gadget", "Gadget", None, ("DXGadget", "DXGadget2"))


class ResolveInterface(unittest.TestCase):
    def test_pure_slots_take_name_and_access_from_the_implementation(self):
        resolved = interface(SPEC, [(PURE, "(not exported)")] * 2,
                             {"DXGadget": [(1, "long DXGadget::Open(unsigned long)"), (2, "protected: void DXGadget::Reset(void)")],
                              "DXGadget2": [(3, "long DXGadget2::Open(unsigned long)"), (4, "protected: void DXGadget2::Reset(void)")]})
        self.assertEqual([(s.index, s.kind, s.method.name, s.method.access) for s in resolved.slots],
                         [(0, "method", "Open", "public"), (1, "method", "Reset", "protected")])
        self.assertEqual(resolved.slot_count, 2)

    def test_the_base_class_access_wins_over_the_implementation(self):
        resolved = interface(SPEC, [(5, "void Gadget::Reset(void)")],
                             {"DXGadget": [(1, "protected: void DXGadget::Reset(void)")], "DXGadget2": [(2, "void DXGadget2::Reset(void)")]})
        self.assertEqual(resolved.slots[0].method.access, "public")

    def test_a_sibling_implementation_resolves_identical_code(self):
        resolved = interface(SPEC, [(PURE, "(not exported)")] * 2,
                             {"DXGadget": [(7, "void DXGadget::SetA(float) | void DXGadget::SetB(unsigned long)")] * 2,
                              "DXGadget2": [(8, "void DXGadget2::SetA(float)"), (9, "void DXGadget2::SetB(unsigned long)")]})
        self.assertEqual([s.method.name for s in resolved.slots], ["SetA", "SetB"])

    def test_unresolvable_identical_code_is_a_placeholder_that_lists_the_candidates(self):
        same = [(7, "long DXGadget::Make(class T *) | long DXGadget::Drop(class T *)")] * 2
        same2 = [(8, "long DXGadget2::Make(class T *) | long DXGadget2::Drop(class T *)")] * 2
        resolved = interface(SPEC, [(PURE, "(not exported)")] * 2, {"DXGadget": same, "DXGadget2": same2})
        slot = resolved.slots[1]
        self.assertEqual((slot.kind, slot.alternatives), ("ambiguous", ("Drop", "Make")))
        self.assertEqual((slot.method.ret, slot.method.params), ("long", ("class T *",)))

    def test_ambiguous_candidates_with_different_signatures_have_no_signature(self):
        rows = [(7, "void DXGadget::SetA(float) | void DXGadget::SetB(unsigned long)")]
        resolved = interface(SPEC, [(PURE, "(not exported)")], {"DXGadget": rows, "DXGadget2": rows})
        self.assertEqual((resolved.slots[0].kind, resolved.slots[0].method), ("ambiguous", None))

    def test_the_single_unnamed_slot_is_the_destructor_when_the_class_has_one(self):
        rows = [(1, "(not exported)"), (2, "void DXGadget::Stop(void)")]
        resolved = interface(SPEC, [(5, "(not exported)"), (PURE, "(not exported)")],
                             {"DXGadget": rows, "DXGadget2": rows}, {"Gadget": "protected"})
        self.assertEqual((resolved.slots[0].kind, resolved.slots[0].method.access), ("destructor", "protected"))

    def test_an_unnamed_slot_without_a_known_destructor_stays_unknown(self):
        rows = [(1, "(not exported)")]
        resolved = interface(SPEC, [(5, "(not exported)")], {"DXGadget": rows, "DXGadget2": rows})
        self.assertEqual(resolved.slots[0].kind, "unknown")

    def test_two_unnamed_slots_with_a_destructor_cannot_be_told_apart(self):
        rows = [(1, "(not exported)"), (2, "(not exported)")]
        with self.assertRaisesRegex(gen.GenError, "Gadget.*destructor"):
            interface(SPEC, [(5, "(not exported)")] * 2, {"DXGadget": rows, "DXGadget2": rows}, {"Gadget": "public"})

    def test_sources_that_name_a_slot_differently_are_a_conflict(self):
        with self.assertRaisesRegex(gen.GenError, "Gadget.*slot 0"):
            interface(SPEC, [(PURE, "(not exported)")],
                      {"DXGadget": [(1, "void DXGadget::A(void)")], "DXGadget2": [(2, "void DXGadget2::B(void)")]})

    def test_an_overload_set_must_be_contiguous(self):
        rows = [(1, "void DXGadget::Draw(int)"), (2, "void DXGadget::Other(void)"), (3, "void DXGadget::Draw(float)")]
        with self.assertRaisesRegex(gen.GenError, "Draw.*contiguous"):
            interface(SPEC, [(PURE, "(not exported)")] * 3, {"DXGadget": rows, "DXGadget2": rows})

    def test_an_implementation_with_another_slot_count_is_rejected(self):
        rows = [(1, "void DXGadget::A(void)"), (2, "void DXGadget::B(void)")]
        with self.assertRaisesRegex(gen.GenError, "DXGadget.*2.*1"):
            interface(SPEC, [(PURE, "(not exported)")], {"DXGadget": rows, "DXGadget2": rows[:1]})

    def test_an_extension_adds_the_slots_behind_its_parent(self):
        parent = interface(SPEC, [(PURE, "(not exported)")] * 2,
                           {"DXGadget": [(1, "void DXGadget::A(void)"), (2, "void DXGadget::B(void)")],
                            "DXGadget2": [(3, "void DXGadget2::A(void)"), (4, "void DXGadget2::B(void)")]})
        ext = gen.InterfaceSpec("GadgetAbi", None, "Gadget", ("DXGadget", "DXGadget2"))
        rows = [(1, "void DXGadget::A(void)"), (2, "void DXGadget::B(void)"), (3, "protected: long DXGadget::C(float)")]
        tables = gen.parse_vtables("".join(vtable_text(c, rows) for c in ("DXGadget", "DXGadget2")))
        resolved = gen.resolve_interface(ext, {}, tables, {}, parent)
        self.assertEqual((resolved.first_slot, resolved.slot_count, [s.index for s in resolved.slots]), (2, 3, [2]))
        self.assertEqual(resolved.slots[0].method.name, "C")

    def test_an_extension_may_not_overload_a_method_of_its_parent(self):
        parent = interface(SPEC, [(PURE, "(not exported)")],
                           {"DXGadget": [(1, "void DXGadget::A(int)")], "DXGadget2": [(2, "void DXGadget2::A(int)")]})
        ext = gen.InterfaceSpec("GadgetAbi", None, "Gadget", ("DXGadget",))
        tables = gen.parse_vtables(vtable_text("DXGadget", [(1, "void DXGadget::A(int)"), (2, "void DXGadget::A(float)")]))
        with self.assertRaisesRegex(gen.GenError, "GadgetAbi.*A.*overloads"):
            gen.resolve_interface(ext, {}, tables, {}, parent)


if __name__ == "__main__":
    unittest.main()
