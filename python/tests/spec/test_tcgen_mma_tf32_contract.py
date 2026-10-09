"""Literal checks for the dense tf32 MMA source and known facts.

These tests assert the fixed Table 42/57 contract without deriving descriptor
bits from live PTX source registers.
"""

from dataclasses import replace
import unittest

from ptx_frontend.code_gen.emit import tcgen_mma_operations as emitter
from ptx_frontend.spec import tcgen_mma_operations as rules
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.model import SemanticRule
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.normalize.tcgen_mma import (
    normalize_tf32_known_facts,
    normalize_tf32_source_topology,
)
from ptx_frontend.spec.resources import packaged_spec_dir


class TcgenTf32ContractTests(unittest.TestCase):
    """Keep tf32 source admission distinct from caller-known word reports."""

    def test_table_42_shape_and_datapath_literals(self) -> None:
        """TF32 inputs, F32 output and implicit K8 have four path rows."""

        rules.validate_catalogue()
        self.assertEqual(len(rules.TF32_SHAPES), 2)
        for group, m, first, step in ((1, 64, 8, 8), (1, 128, 8, 8),
                                      (2, 128, 16, 16), (2, 256, 16, 16)):
            row = next(item for item in rules.TF32_SHAPES
                       if item.group == group)
            self.assertEqual((row.d_type, row.a_types, row.b_types),
                             ("F32", ("TF32",), ("TF32",)))
            self.assertTrue(row.contains(m, first, 8))
            self.assertTrue(row.contains(m, 256, 8))
            self.assertFalse(row.contains(m, first - step, 8))
            self.assertFalse(row.contains(m, first + 1, 8))
            self.assertFalse(row.contains(m, first, 16))
        self.assertEqual({(row.group, row.m, row.layout, row.half_path)
                          for row in rules.TF32_PATHS}, {
                              (1, 64, "F", True), (1, 128, "D", False),
                              (2, 128, "B", False), (2, 256, "A", False)})

    def test_eight_typed_layouts_preserve_optional_presence(self) -> None:
        """One kind owns eight layouts; source scale keeps original value."""

        raw = load_yaml(packaged_spec_dir() /
                        "tensor_memory_data_movement.yaml")
        tcgen = normalize_instruction_spec(raw)[0]
        mma = next(item for item in tcgen.variants
                   if item.name == "tcgen05_mma_tf32")
        self.assertIs(mma.rule, SemanticRule.TENSOR_MEMORY_MMA)
        self.assertEqual(len(mma.operand_layouts), 8)
        self.assertEqual(mma.modifier_order_aliases,
                         (("mma", "kind", "cta_group", "ashift", "collector"),))
        self.assertEqual({layout.name for layout in mma.operand_layouts}, {
            f"{a}_{m}_{s}" for a in ("shared", "tensor")
            for m in ("mask", "no_mask") for s in ("scale", "no_scale")})
        base = {"group": 1, "a_placement": "shared", "mask_count": None,
                "scale_source_value": None}
        self.assertIsNone(normalize_tf32_source_topology(base).mask_count)
        self.assertEqual(normalize_tf32_source_topology({
            **base, "group": 2, "a_placement": "tensor", "mask_count": 8,
            "scale_source_value": 15}).mask_count, 8)
        for changed in ({"mask_count": 0}, {"mask_count": 5},
                        {"scale_source_value": -1},
                        {"scale_source_value": 16},
                        {"scale_source_value": 1 << 32},
                        {"group": 3}, {"a_placement": "global"}):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                normalize_tf32_source_topology({**base, **changed})

    def test_known_type_row_and_missing_facts(self) -> None:
        """Only TF32×TF32 to F32 is settled; unknown words remain unknown."""

        facts = rules.Tf32KnownFacts(
            group=1, m=64, n=8, k=8, d_type="F32", a_type="TF32",
            b_type="TF32", sparse=False, a_shared=False,
            b_shared_facts=rules.SharedOperandFacts("K", "B32"),
            a_lane_half=0, d_lane_half=0)
        report = rules.check_tf32_known_facts(facts)
        self.assertTrue(report.known_facts_ok)
        self.assertEqual(report.layout, "F")
        self.assertNotIn("mixed_f16_bf16_pair_rule", report.obligations)
        for changed, rule in (({"d_type": "F16"}, "shape_or_output_type"),
                              ({"a_type": "F16"}, "a_type"),
                              ({"b_type": "BF16"}, "b_type"),
                              ({"k": 16}, "shape_or_output_type")):
            with self.subTest(changed=changed):
                self.assertIn(rule, rules.check_tf32_known_facts(
                    replace(facts, **changed)).violations)
        missing = rules.check_tf32_known_facts(rules.Tf32KnownFacts())
        self.assertIn("cta_group", missing.obligations)
        self.assertIn("b_shared_word", missing.obligations)
        self.assertEqual(normalize_tf32_known_facts({"group": 1,
            "b_shared_facts": {"major": "K"}}).b_shared_facts.major, "K")

    def test_independent_32_bit_transpose_rules(self) -> None:
        """A and B transposes each require 128B/32B-atomic swizzle."""

        facts = rules.Tf32KnownFacts(
            group=1, m=64, n=8, k=8, d_type="F32", a_type="TF32",
            b_type="TF32", sparse=False, a_shared=True,
            transpose_a=True, transpose_b=False,
            a_shared_facts=rules.SharedOperandFacts("MN", "B128Atom32"),
            b_shared_facts=rules.SharedOperandFacts("K", "B32"))
        accepted = rules.check_tf32_known_facts(facts)
        self.assertNotIn("a_transpose_swizzle", accepted.violations)
        self.assertNotIn("b_transpose_swizzle", accepted.violations)
        self.assertIn("a_transpose_swizzle", rules.check_tf32_known_facts(
            replace(facts, a_shared_facts=rules.SharedOperandFacts(
                "MN", "B128Atom16"))).violations)
        b_transposed = replace(facts, transpose_a=False, transpose_b=True,
                               a_shared_facts=rules.SharedOperandFacts("K", "B32"),
                               b_shared_facts=rules.SharedOperandFacts(
                                   "MN", "B128Atom32"))
        self.assertNotIn("b_transpose_swizzle",
                         rules.check_tf32_known_facts(b_transposed).violations)
        self.assertIn("b_transpose_swizzle", rules.check_tf32_known_facts(
            replace(b_transposed, b_shared_facts=rules.SharedOperandFacts(
                "MN", "B64"))).violations)
        tensor_a = replace(facts, a_shared=False, a_shared_facts=None,
                           a_lane_half=0, d_lane_half=16)
        self.assertNotIn("a_shared_facts",
                         rules.check_tf32_known_facts(tensor_a).obligations)
        self.assertIn("half_path_alignment",
                      rules.check_tf32_known_facts(tensor_a).violations)

    def test_target_rows_and_generated_query_share_catalogue(self) -> None:
        """Scaled target rows are narrower and encoded fields stay external."""

        self.assertEqual({gate.feature for gate in rules.TF32_TARGET_GATES
                          if gate.scaled_d}, {"sm_100a", "sm_100f"})
        header = emitter.render_tcgen_mma_header()
        source = emitter.render_tcgen_mma_source()
        self.assertIn("TcgenTf32ShapeRow", header)
        self.assertIn("check_tcgen_tf32_known_operation", source)
        self.assertIn("validate_tcgen_instruction_defined_fields", source)
        self.assertIn("validate_tcgen_shared_defined_fields", source)
        self.assertIn("TcgenMmaKind::Tf32", source)
        self.assertNotIn("reserved_", source)


if __name__ == "__main__":
    unittest.main()
