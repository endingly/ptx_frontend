"""Literal source and known-fact contract checks for dense i8 TCGEN MMA.

Descriptor words are independent caller-known claims, never values inferred
from live source registers.
"""

from dataclasses import replace
import unittest

from ptx_frontend.code_gen.emit import tcgen_mma_operations as emitter
from ptx_frontend.spec import tcgen_descriptor_domains as descriptor
from ptx_frontend.spec import tcgen_mma_operations as rules
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.model import SemanticRule
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.normalize.tcgen_mma import (
    normalize_i8_known_facts,
    normalize_i8_source_topology,
)
from ptx_frontend.spec.resources import packaged_spec_dir


class TcgenI8ContractTests(unittest.TestCase):
    """Separate canonical source admission from known-word operation checks."""

    def test_closed_source_has_four_unscaled_layouts(self) -> None:
        """Both typed groups share four layouts without scale or saturation syntax."""

        raw = load_yaml(packaged_spec_dir() /
                        "tensor_memory_data_movement.yaml")
        tcgen = normalize_instruction_spec(raw)[0]
        mma = next(item for item in tcgen.variants
                   if item.name == "tcgen05_mma_i8")
        self.assertIs(mma.rule, SemanticRule.TENSOR_MEMORY_MMA)
        self.assertEqual(len(mma.operand_layouts), 4)
        self.assertEqual(mma.modifier_order_aliases,
                         (("mma", "kind", "cta_group", "ashift", "collector"),))
        self.assertEqual({layout.name for layout in mma.operand_layouts}, {
            "shared_mask", "shared_no_mask", "tensor_mask", "tensor_no_mask"
        })
        self.assertEqual({item.name for item in mma.modifiers},
                         {"mma", "cta_group", "kind", "ashift", "collector"})
        self.assertEqual({item.token for item in mma.modifiers
                          if item.name == "kind"}, {".kind::i8"})
        for layout in mma.operand_layouts:
            names = tuple(item.name for item in layout.operands)
            self.assertNotIn("scale_input_d", names)
            self.assertNotIn("saturate", names)
            self.assertEqual(names[:4], ("d", "a", "b", "idesc"))
            self.assertEqual(names[-1], "enable_input_d")
        base = {"group": 1, "a_placement": "shared", "mask_count": None}
        self.assertIsNone(normalize_i8_source_topology(base).mask_count)
        for group, count in ((1, 4), (2, 8)):
            for placement in ("shared", "tensor"):
                for mask_count in (None, count):
                    with self.subTest(group=group, placement=placement,
                                      mask_count=mask_count):
                        source = normalize_i8_source_topology({
                            "group": group, "a_placement": placement,
                            "mask_count": mask_count})
                        self.assertEqual((source.group, source.a_placement,
                                          source.mask_count),
                                         (group, placement, mask_count))
        for changed in ({"group": 3}, {"a_placement": "global"},
                        {"mask_count": 0}, {"mask_count": 5},
                        {"scale_source_value": 0}, {"saturate": True}):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                normalize_i8_source_topology({**base, **changed})

    def test_table_42_irregular_small_n_k32_and_types(self) -> None:
        """N8/16/24/32 precede the group-one 16-column cadence."""

        rules.validate_catalogue()
        self.assertEqual(len(rules.I8_SHAPES), 2)
        rows = {row.group: row for row in rules.I8_SHAPES}
        self.assertEqual(set(rows), {1, 2})
        for group, m_values, accepted_n, rejected_n in (
                (1, (64, 128), (8, 16, 24, 32, 48, 256),
                 (0, 40, 56, 257)),
                (2, (128, 256), (32, 64, 256), (8, 16, 48, 257))):
            row = rows[group]
            self.assertEqual((row.d_type, row.k), ("S32", 32))
            self.assertEqual(set(row.a_types), {"S8", "U8"})
            self.assertEqual(set(row.b_types), {"S8", "U8"})
            for m in m_values:
                for n in accepted_n:
                    self.assertTrue(row.contains(m, n, 32), (group, m, n))
                for n in rejected_n:
                    self.assertFalse(row.contains(m, n, 32), (group, m, n))
                self.assertFalse(row.contains(m, accepted_n[0], 16))
        self.assertEqual({(row.group, row.m, row.layout, row.half_path)
                          for row in rules.I8_PATHS}, {
                              (1, 64, "F", True), (1, 128, "D", False),
                              (2, 128, "B", False), (2, 256, "A", False)})

    def test_field_domain_and_mixed_pair_obligation(self) -> None:
        """Both saturation bits are valid; mixed signedness stays unresolved."""

        kind = next(item for item in descriptor.KINDS if item.name == "I8")
        self.assertTrue(kind.saturation)
        self.assertFalse(kind.negate)
        self.assertEqual(kind.d_types, ((2, "S32"),))
        base = rules.I8KnownFacts(
            group=1, m=64, n=8, k=32, d_type="S32", a_type="S8",
            b_type="S8", sparse=False, a_shared=True,
            transpose_a=False, transpose_b=False,
            a_shared_facts=rules.SharedOperandFacts("K", "B32"),
            b_shared_facts=rules.SharedOperandFacts("K", "B32"))
        same = rules.check_i8_known_facts(base)
        self.assertTrue(same.known_facts_ok)
        self.assertNotIn("mixed_i8_signedness_pair_rule", same.obligations)
        for a_type, b_type in (("S8", "U8"), ("U8", "S8")):
            report = rules.check_i8_known_facts(
                replace(base, a_type=a_type, b_type=b_type))
            self.assertNotIn("mixed_i8_signedness_pair_rule",
                             report.violations)
            self.assertIn("mixed_i8_signedness_pair_rule",
                          report.obligations)
        unknown = rules.check_i8_known_facts(rules.I8KnownFacts())
        self.assertIn("cta_group", unknown.obligations)
        self.assertIn("b_shared_word", unknown.obligations)
        self.assertEqual(normalize_i8_known_facts({"group": 1,
            "b_shared_facts": {"major": "K"}}).b_shared_facts.major, "K")
        for changed, violation in (({"d_type": "F32"}, "output_type"),
                                   ({"a_type": "TF32"}, "a_type"),
                                   ({"b_type": "BF16"}, "b_type"),
                                   ({"k": 16}, "shape_or_output_type")):
            self.assertIn(violation, rules.check_i8_known_facts(
                replace(base, **changed)).violations)

    def test_table_55_b_transpose_and_table_57_both_roles(self) -> None:
        """B N cadence and forbidden atom32 swizzle are separate rules."""

        base = rules.I8KnownFacts(
            group=1, m=64, n=16, k=32, d_type="S32", a_type="U8",
            b_type="U8", sparse=False, a_shared=True,
            transpose_a=True, transpose_b=True,
            a_shared_facts=rules.SharedOperandFacts("MN", "B128Atom16"),
            b_shared_facts=rules.SharedOperandFacts("MN", "B64"))
        self.assertTrue(rules.check_i8_known_facts(base).known_facts_ok)
        for n, valid in ((8, False), (16, True), (24, False),
                         (32, True), (48, True)):
            report = rules.check_i8_known_facts(replace(base, n=n))
            self.assertEqual("b_transpose_n" not in report.violations, valid)
        for role in ("a", "b"):
            with self.subTest(role=role):
                report = rules.check_i8_known_facts(replace(
                    base, **{f"{role}_shared_facts": rules.SharedOperandFacts(
                        "MN", "B128Atom32")}))
                self.assertIn(f"{role}_transpose_swizzle", report.violations)
        tensor_a = replace(base, a_shared=False, a_shared_facts=None,
                           transpose_a=False, n=8,
                           a_lane_half=0, d_lane_half=16)
        report = rules.check_i8_known_facts(tensor_a)
        self.assertNotIn("a_shared_word", report.obligations)
        self.assertIn("half_path_alignment", report.violations)

    def test_exact_targets_and_single_generated_catalogue(self) -> None:
        """The source gate has no f-family or scaling alternative."""

        self.assertEqual({(row.feature, row.exact, row.ptx_major,
                           row.ptx_minor) for row in rules.I8_TARGET_GATES}, {
                               ("sm_100a", True, 8, 6),
                               ("sm_110a", True, 9, 0)})
        header = emitter.render_tcgen_mma_header()
        source = emitter.render_tcgen_mma_source()
        self.assertIn("TcgenI8ShapeRow", header)
        self.assertIn("check_tcgen_i8_known_operation", source)
        self.assertIn("validate_tcgen_instruction_defined_fields", source)
        self.assertIn("validate_tcgen_shared_defined_fields", source)
        self.assertIn("TcgenMmaKind::I8", source)
        self.assertNotIn("reserved_", source)


if __name__ == "__main__":
    unittest.main()
