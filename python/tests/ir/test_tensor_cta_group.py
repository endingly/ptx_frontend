"""Explicit tensor CTA-group forms retain typed source and topology contracts."""

from dataclasses import replace
import unittest

from ptx_frontend.ir.resolved_ir import from_instruction_spec
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import ModifierKind, ModifierPresence


class TensorCtaGroupTests(unittest.TestCase):
    """Check all grouped load cohorts and reject malformed canonical controls."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.cp = next(item for item in get_packaged_spec_database().instructions
                      if item.opcode == "cp")

    def test_census_and_parent_preservation(self) -> None:
        resolved = from_instruction_spec(self.cp)
        new = [v for v in resolved.variants
               if v.variant_id.endswith("_cta_group")]
        old = [v for v in resolved.variants
               if not v.variant_id.endswith("_cta_group")]
        self.assertEqual(len(resolved.variants), 295)
        self.assertEqual(len(new), 45)
        self.assertEqual(sum(len(v.operand_layouts) for v in new), 72)
        self.assertEqual(len(old), 250)
        self.assertEqual(sum(v.tensor_access_mode is not None for v in old), 133)
        self.assertEqual(sum(len(v.operand_layouts) for v in old
                             if v.tensor_access_mode is not None), 169)
        self.assertEqual(sum(v.tensor_cta_group_applicable for v in new), 45)
        self.assertEqual(sum(v.tensor_cta_group_applicable for v in old), 45)
        self.assertEqual(sum(v.tensor_multicast for v in new), 15)
        self.assertEqual(sum(len(v.operand_layouts) for v in new
                             if v.tensor_multicast), 24)

    def test_required_closed_group_and_adjacent_alias(self) -> None:
        for variant in self.cp.variants:
            if not variant.name.endswith("_cta_group"):
                self.assertFalse(any(m.name == "cta_group"
                                     for m in variant.modifiers))
                continue
            with self.subTest(variant=variant.name):
                group = next(m for m in variant.modifiers
                             if m.name == "cta_group")
                self.assertIs(group.kind, ModifierKind.CTA_GROUP)
                self.assertIs(group.presence, ModifierPresence.REQUIRED)
                self.assertEqual(tuple(v.value for v in group.values),
                                 ("cta_group::1", "cta_group::2"))
                names = tuple(m.name for m in variant.modifiers)
                self.assertEqual(names[-1], "cta_group")
                if "multicast" in names:
                    self.assertEqual(names[-2], "multicast")
                    self.assertEqual(variant.modifier_order_aliases,
                                     (names[:-2] + ("cta_group", "multicast"),))
                    for layout in variant.operand_layouts:
                        self.assertEqual(layout.operands[-1].name, "cta_mask")
                else:
                    self.assertEqual(names[-2], "completion")
                    self.assertFalse(variant.modifier_order_aliases)

    def test_malformed_group_metadata(self) -> None:
        selected = next(v for v in self.cp.variants
                        if v.name.endswith("_multicast_cta_group"))
        group = selected.modifiers[-1]
        for damaged in (
            replace(group, kind=ModifierKind.FLAG),
            replace(group, presence=ModifierPresence.OPTIONAL,
                    default="cta_group::1"),
            replace(group, domain="scalar_types"),
            replace(group, values=group.values[:1]),
        ):
            with self.subTest(damaged=damaged):
                candidate = replace(selected,
                                    modifiers=selected.modifiers[:-1] + (damaged,))
                with self.assertRaises(ValueError):
                    from_instruction_spec(replace(self.cp, variants=(candidate,)))
        with self.assertRaises(ValueError):
            from_instruction_spec(replace(self.cp, variants=(replace(
                selected, modifier_order_aliases=()),)))


if __name__ == "__main__":
    unittest.main()
