"""Unit tests of tests/abi_shapes/check_shapes.py, which compares the vtable layouts clang computes
(`-Xclang -fdump-vtable-layouts`) with the slot tables of the generated headers (task P2.2).

Run: python3 -I tests/abi_shapes/test_check_shapes.py

The headers under test are the generator's output for the synthetic Gadget family of
test_gen_abi_headers.py; the layout dumps are written out by hand in clang's format.
"""
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "re"))
import check_shapes as shapes  # noqa: E402
from test_gen_abi_headers import generate_gadget  # noqa: E402

BASE_BLOCK = """\
VFTable for 'Gadget' (9 entries).
   0 | Gadget RTTI
   1 | Gadget::~Gadget() [vector deleting]
   2 | long Gadget::Open(Handle *, unsigned long) const [pure]
   3 | void Gadget::Draw(Canvas &, float) [pure]
   4 | void Gadget::Draw(Canvas &) [pure]
   5 | long Gadget::Reset() [pure]
   6 | long Gadget::slot5(const Rect &) [pure]
   7 | long Gadget::slot6(const Rect &) [pure]
   8 | std::vector<U2DPoint<long>, std::allocator<U2DPoint<long>>> &Gadget::Points(Outer::Inner *) [pure]

VFTable indices for 'Gadget' (8 entries).
   0 | Gadget::~Gadget() [vector deleting]
"""

ABI_BLOCK = """\
VFTable for 'Gadget' in 'GadgetAbi' (11 entries).
   0 | GadgetAbi RTTI
   1 | GadgetAbi::~GadgetAbi() [vector deleting]
   2 | long Gadget::Open(Handle *, unsigned long) const [pure]
   3 | void Gadget::Draw(Canvas &, float) [pure]
   4 | void Gadget::Draw(Canvas &) [pure]
   5 | long Gadget::Reset() [pure]
   6 | long Gadget::slot5(const Rect &) [pure]
   7 | long Gadget::slot6(const Rect &) [pure]
   8 | std::vector<U2DPoint<long>, std::allocator<U2DPoint<long>>> &Gadget::Points(Outer::Inner *) [pure]
   9 | void GadgetAbi::Extra(float) [pure]
  10 | void GadgetAbi::slot9() [pure]
"""

GADGET_DUMP = BASE_BLOCK + "\n" + ABI_BLOCK

# A class built on the interface, as the plugin's null rasterizer will be: it overrides every slot.
IMPLEMENTATION_DUMP = (ABI_BLOCK.replace("'Gadget' in 'GadgetAbi' (11", "'Gadget' in 'GadgetAbi' in 'Null' (11")
                       .replace("0 | GadgetAbi RTTI", "0 | Null RTTI").replace(" [pure]", "")
                       .replace("GadgetAbi::~GadgetAbi()", "Null::~Null()").replace("Gadget::", "Null::").replace("GadgetAbi::", "Null::"))


def check(dump, implements=()):
    files = generate_gadget()
    return shapes.check(files["ge_interfaces.h"], files["ge_slots.h"], dump, list(implements))


class CheckShapes(unittest.TestCase):
    def test_the_layout_that_matches_the_headers_passes(self):
        self.assertEqual(check(GADGET_DUMP), [])

    def test_a_class_built_on_the_interface_must_override_every_slot_in_place(self):
        self.assertEqual(check(GADGET_DUMP + "\n" + IMPLEMENTATION_DUMP, [("Null", "GadgetAbi")]), [])

    def test_an_implementation_that_leaves_a_slot_pure_is_reported(self):
        pure = IMPLEMENTATION_DUMP.replace("Null::Reset()\n", "Null::Reset() [pure]\n")
        errors = check(GADGET_DUMP + "\n" + pure, [("Null", "GadgetAbi")])
        self.assertTrue(any("Null" in e and "pure" in e and "Reset" in e for e in errors), errors)

    def test_two_swapped_slots_are_reported_with_their_index(self):
        swapped = GADGET_DUMP.replace("   5 | long Gadget::Reset() [pure]\n   6 | long Gadget::slot5(const Rect &) [pure]",
                                      "   5 | long Gadget::slot5(const Rect &) [pure]\n   6 | long Gadget::Reset() [pure]")
        errors = check(swapped)
        self.assertTrue(any("Gadget" in e and "slot 4" in e and "Reset" in e for e in errors), errors)

    def test_overloads_in_the_wrong_order_are_reported(self):
        wrong = GADGET_DUMP.replace("   3 | void Gadget::Draw(Canvas &, float) [pure]\n   4 | void Gadget::Draw(Canvas &) [pure]",
                                    "   3 | void Gadget::Draw(Canvas &) [pure]\n   4 | void Gadget::Draw(Canvas &, float) [pure]")
        self.assertTrue(any("slot 2" in e and "Draw" in e for e in check(wrong)))

    def test_a_missing_or_extra_slot_is_reported(self):
        shorter = GADGET_DUMP.replace("   8 | std::vector<U2DPoint<long>, std::allocator<U2DPoint<long>>> &Gadget::Points(Outer::Inner *) [pure]\n",
                                      "", 1).replace("(9 entries)", "(8 entries)", 1)
        errors = check(shorter)
        self.assertTrue(any("Gadget" in e and "7 slots" in e and "8" in e for e in errors), errors)

    def test_an_interface_without_a_layout_in_the_dump_is_reported(self):
        self.assertTrue(any("GadgetAbi" in e and "no vtable layout" in e for e in check(BASE_BLOCK)))

    def test_an_implementation_that_moves_a_slot_is_reported(self):
        misplaced = IMPLEMENTATION_DUMP.replace("9 | void Null::Extra(float)\n", "9 | void Null::Other(float)\n")
        errors = check(GADGET_DUMP + "\n" + misplaced, [("Null", "GadgetAbi")])
        self.assertTrue(any("Null" in e and "slot 8" in e for e in errors), errors)

    def test_a_slot_constant_that_disagrees_with_the_layout_is_reported(self):
        files = generate_gadget()
        wrong = files["ge_slots.h"].replace("EEWEB_SLOT_Gadget_Reset = 4", "EEWEB_SLOT_Gadget_Reset = 5")
        errors = shapes.check(files["ge_interfaces.h"], wrong, GADGET_DUMP, [])
        self.assertTrue(any("EEWEB_SLOT_Gadget_Reset" in e for e in errors), errors)

    def test_a_slot_count_constant_that_disagrees_with_the_layout_is_reported(self):
        files = generate_gadget()
        wrong = files["ge_slots.h"].replace("EEWEB_SLOTS_Gadget = 8", "EEWEB_SLOTS_Gadget = 9")
        self.assertTrue(any("EEWEB_SLOTS_Gadget" in e for e in shapes.check(files["ge_interfaces.h"], wrong, GADGET_DUMP, [])))


if __name__ == "__main__":
    unittest.main()
