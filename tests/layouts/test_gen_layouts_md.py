"""Tests for tools/re/gen_layouts_md.py: the Markdown tables are generated from the comments of ge_layouts.h."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "re"))
import gen_layouts_md as gen  # noqa: E402

SAMPLE = """\
/* @class GEPair size=0xc size-confidence=confirmed | two floats and a pad */
typedef struct GEPairLayout {
    float a; /* @0x0 confirmed | P Draw; L GEPair::GetA */
    uint8_t pad_0x4[0x4];
    float b; /* @0x8 lle-only | L GEPair::GetB */
} GEPairLayout;
EEL_AT(GEPairLayout, a, 0x0);
EEL_AT(GEPairLayout, b, 0x8);
EEL_SIZE(GEPairLayout, 0xc);

/* @class GEHolder size=0x14 size-confidence=guess | inferred */
typedef struct GEHolderLayout {
    struct GEPairLayout pair; /* @0x0 plugin-only | P Draw */
    struct GEPairLayout *next; /* @0xc guess | P Draw */
    uint8_t flags[2]; /* @0x10 confirmed | P Draw */
    uint8_t pad_0x12[0x2];
} GEHolderLayout;
EEL_AT(GEHolderLayout, pair, 0x0);
EEL_AT(GEHolderLayout, next, 0xc);
EEL_AT(GEHolderLayout, flags, 0x10);
EEL_SIZE(GEHolderLayout, 0x14);
"""


class ParseTests(unittest.TestCase):
    def test_classes_fields_and_widths(self):
        classes = gen.parse(SAMPLE)
        self.assertEqual([c.name for c in classes], ["GEPair", "GEHolder"])
        pair, holder = classes
        self.assertEqual((pair.size, pair.size_confidence), (0xc, "confirmed"))
        self.assertEqual([(f.offset, f.width, f.name, f.confidence) for f in pair.fields],
                         [(0x0, 4, "a", "confirmed"), (0x8, 4, "b", "lle-only")])
        self.assertEqual([(f.offset, f.width, f.type) for f in holder.fields],
                         [(0x0, 0xc, "struct GEPairLayout"), (0xc, 4, "struct GEPairLayout *"), (0x10, 2, "uint8_t[2]")])

    def test_markdown_has_a_table_row_per_field_and_the_counts(self):
        text = gen.render(gen.parse(SAMPLE))
        self.assertIn("| 0x0 | 4 | `float` | `a` | confirmed | P Draw; L GEPair::GetA |", text)
        self.assertIn("| 0x10 | 2 | `uint8_t[2]` | `flags` | confirmed | P Draw |", text)
        self.assertIn("### GEHolder (0x14 bytes, size guess)", text)
        self.assertIn("| confirmed | 2 |", text)
        self.assertIn("| lle-only | 1 |", text)
        self.assertIn("| guess | 1 |", text)

    def test_offset_disagreeing_with_the_assertion_is_an_error(self):
        broken = SAMPLE.replace("EEL_AT(GEPairLayout, b, 0x8);", "EEL_AT(GEPairLayout, b, 0xc);")
        with self.assertRaises(gen.LayoutError):
            gen.parse(broken)

    def test_size_disagreeing_with_the_assertion_is_an_error(self):
        broken = SAMPLE.replace("EEL_SIZE(GEPairLayout, 0xc);", "EEL_SIZE(GEPairLayout, 0x10);")
        with self.assertRaises(gen.LayoutError):
            gen.parse(broken)

    def test_unknown_confidence_is_an_error(self):
        broken = SAMPLE.replace("@0x0 confirmed | P Draw; L", "@0x0 certain | P Draw; L")
        with self.assertRaises(gen.LayoutError):
            gen.parse(broken)


if __name__ == "__main__":
    unittest.main()
