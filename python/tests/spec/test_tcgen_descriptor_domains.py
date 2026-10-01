"""Independent fixed-table vectors for the immutable TCGEN descriptor catalogue."""

from dataclasses import replace
import unittest

from ptx_frontend.spec import tcgen_descriptor_domains as domain


class TcgenDescriptorDomainTests(unittest.TestCase):
    """Check encoded facts independently of the C++ emitter's rendering."""

    def test_table_43_and_48_field_partitions(self) -> None:
        """Known offsets and the two unclassified bits stay distinct."""

        domain.validate_catalogue()
        self.assertEqual((domain.SHARED.field("start").first,
                          domain.SHARED.field("start").width), (0, 14))
        self.assertEqual((domain.SHARED.field("leading").first,
                          domain.SHARED.field("leading").width), (16, 14))
        self.assertEqual(domain.SHARED.field("fixed_code").fixed, 1)
        self.assertEqual(domain.SHARED.field("swizzle").first, 61)
        self.assertEqual(domain.ZERO_COLUMN.field("generate_mask").first, 39)
        self.assertIsNone(domain.ZERO_COLUMN.field("generate_mask").fixed)
        self.assertEqual(domain.ZERO_COLUMN.field("shift").first, 56)
        self.assertEqual(domain.ZERO_COLUMN.unclassified, 0xC000000000000000)
        self.assertEqual({code for code, _ in domain.SWIZZLES},
                         {0, 1, 2, 4, 6})

    def test_instruction_tables_and_kind_selection(self) -> None:
        """Kind is explicit and Table 47 differs from the other maps."""

        self.assertEqual(len(domain.KINDS), 7)
        self.assertEqual(domain.INSTRUCTION_45.field("reuse").first, 30)
        self.assertEqual(domain.INSTRUCTION_46.field("scale_type").fixed, 1)
        self.assertEqual(domain.INSTRUCTION_47.field("b_type").width, 2)
        self.assertEqual(domain.INSTRUCTION_47.field("k_choice").first, 31)
        self.assertEqual(next(item for item in domain.KINDS
                              if item.name == "MxF4").scale_ids, (0, 2))
        self.assertEqual(next(item for item in domain.KINDS
                              if item.name == "MxF4NvF4").scale_types,
                         ((0, "UE4M3"), (1, "UE8M0")))

    def test_relative_rows_are_symbolic_and_closed(self) -> None:
        """All eight rows retain T rather than truncating low-bit packing."""

        self.assertEqual(len(domain.RELATIVE_LAYOUTS), 8)
        self.assertEqual({(row.major, row.swizzle)
                          for row in domain.RELATIVE_LAYOUTS}, {
                              (major, swizzle)
                              for major in ("K", "MN")
                              for swizzle in ("None", "B32", "B64",
                                              "B128Atom16")})
        row = next(item for item in domain.RELATIVE_LAYOUTS
                   if item.major == "K" and item.swizzle == "B32")
        self.assertEqual(row.stride[0],
                         (domain.LayoutTerm(2, "T"),
                          domain.LayoutTerm(1, "SBO")))
        self.assertEqual(domain.SPECIAL_ATOM, ("MN", 1, 8, 4))
        self.assertEqual(domain.ABSOLUTE_TARGET, ("sm_103a", 8, 8))

    def test_bad_catalogue_partition_is_rejected(self) -> None:
        """A mutation cannot silently move a bit into two fields."""

        original = domain.WORDS
        try:
            changed = replace(domain.SHARED, fields=(
                replace(domain.SHARED.fields[0], width=15),
                *domain.SHARED.fields[1:],
            ))
            domain.WORDS = (changed, *original[1:])
            with self.assertRaises(ValueError):
                domain.validate_catalogue()
        finally:
            domain.WORDS = original


if __name__ == "__main__":
    unittest.main()
