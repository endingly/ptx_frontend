"""Independent literal vectors for prepared dense f16 MMA operational rows."""

from dataclasses import replace
from copy import deepcopy
import unittest

from ptx_frontend.code_gen.emit import tcgen_mma_operations as emitter
from ptx_frontend.spec import tcgen_mma_operations as rules
from ptx_frontend.spec.normalize.tcgen_mma import (
    normalize_f16_known_facts, normalize_f16_source_topology,
)
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.resources import packaged_spec_dir
from ptx_frontend.spec.model import SemanticRule


class TcgenF16OperationTests(unittest.TestCase):
    """Check supplied facts without inferring bits from source registers."""

    def test_table_42_literal_boundaries(self) -> None:
        """Both group rows retain the exact M/N steps and implicit K16."""

        rules.validate_catalogue()
        self.assertEqual(len(rules.F16_SHAPES), 4)
        for group, m, first, step in ((1, 64, 8, 8), (1, 128, 8, 8),
                                       (2, 128, 16, 16), (2, 256, 16, 16)):
            row = next(item for item in rules.F16_SHAPES
                       if item.group == group and item.d_type == "F16")
            self.assertTrue(row.contains(m, first, 16))
            self.assertTrue(row.contains(m, 256, 16))
            self.assertFalse(row.contains(m, first - step, 16))
            self.assertFalse(row.contains(m, first + 1, 16))
            self.assertFalse(row.contains(m, first, 32))
        self.assertEqual({(row.group, row.m, row.layout, row.half_path)
                          for row in rules.F16_PATHS}, {
                              (1, 64, "F", True), (1, 128, "D", False),
                              (2, 128, "B", False), (2, 256, "A", False)})
        self.assertEqual(len(rules.F16_TARGET_GATES), 5)
        self.assertEqual({gate.feature for gate in rules.F16_TARGET_GATES
                          if gate.scaled_d}, {"sm_100a", "sm_100f"})

    def test_source_topology_keeps_optional_presence(self) -> None:
        """Absence differs from empty mask; original scale cannot wrap."""

        raw = {"group": 1, "a_placement": "shared", "mask_count": None,
               "scale_source_value": None}
        self.assertIsNone(normalize_f16_source_topology(raw).mask_count)
        self.assertEqual(normalize_f16_source_topology({
            **raw, "mask_count": 4, "scale_source_value": 15}).mask_count, 4)
        for changed in ({"mask_count": 0}, {"mask_count": 5},
                        {"scale_source_value": -1},
                        {"scale_source_value": 16},
                        {"scale_source_value": 1 << 32},
                        {"group": 3}, {"a_placement": "global"}):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                normalize_f16_source_topology({**raw, **changed})
        self.assertEqual(normalize_f16_source_topology({
            **raw, "group": 2, "a_placement": "tensor", "mask_count": 8})
                         .mask_count, 8)

    def test_known_types_and_mixed_pair_obligation(self) -> None:
        """D=f16 requires two f16 sources; mixed f16/bf16 remains unresolved."""

        base = rules.F16KnownFacts(
            group=1, m=64, n=8, k=16, d_type="F16", a_type="F16",
            b_type="F16", sparse=False, a_shared=False,
            transpose_b=False,
            b_shared_facts=rules.SharedOperandFacts("K", "B32"),
            a_lane_half=0, d_lane_half=0)
        report = rules.check_f16_known_facts(base)
        self.assertTrue(report.known_facts_ok)
        self.assertEqual(report.layout, "F")
        self.assertNotIn("half_path_alignment", report.violations)
        self.assertIn("a_type", rules.check_f16_known_facts(
            replace(base, a_type="BF16")).violations)
        self.assertIn("a_type", rules.check_f16_known_facts(
            rules.F16KnownFacts(d_type="F16", a_type="BF16")).violations)
        mixed = rules.check_f16_known_facts(replace(
            base, d_type="F32", a_type="F16", b_type="BF16"))
        self.assertTrue(mixed.known_facts_ok)
        self.assertIn("mixed_f16_bf16_pair_rule", mixed.obligations)
        equal = rules.check_f16_known_facts(replace(
            base, d_type="F32", a_type="BF16", b_type="BF16"))
        self.assertNotIn("mixed_f16_bf16_pair_rule", equal.obligations)

    def test_independent_shared_transpose_and_path_alignment(self) -> None:
        """Known A/B role facts and F-path lane halves remain separate."""

        base = rules.F16KnownFacts(
            group=1, m=64, n=256, k=16, d_type="F32", a_type="F16",
            b_type="F16", sparse=False, a_shared=True,
            transpose_a=True, transpose_b=False,
            a_shared_facts=rules.SharedOperandFacts("MN", "B128Atom32"),
            b_shared_facts=rules.SharedOperandFacts("K", "B32"))
        report = rules.check_f16_known_facts(base)
        self.assertIn("a_transpose_swizzle", report.violations)
        self.assertNotIn("b_transpose_swizzle", report.violations)
        tensor = replace(base, a_shared=False, a_shared_facts=None,
                         a_lane_half=0, d_lane_half=16)
        report = rules.check_f16_known_facts(tensor)
        self.assertNotIn("a_transpose_swizzle", report.violations)
        self.assertIn("half_path_alignment", report.violations)
        unknown = rules.check_f16_known_facts(replace(
            tensor, a_lane_half=None, d_lane_half=None))
        self.assertIn("a_lane_half", unknown.obligations)
        self.assertIn("d_lane_half", unknown.obligations)

    def test_missing_and_malformed_facts(self) -> None:
        """Unknown context is an obligation; known invalid bits diagnose."""

        empty = rules.check_f16_known_facts(rules.F16KnownFacts())
        self.assertTrue(empty.known_facts_ok)
        self.assertIn("cta_group", empty.obligations)
        bad = rules.check_f16_known_facts(rules.F16KnownFacts(
            group=3, sparse=True, a_shared=False,
            b_shared_facts=rules.SharedOperandFacts("bogus", "B32")))
        self.assertIn("cta_group_invalid", bad.violations)
        self.assertIn("dense_sparse_bit", bad.violations)
        self.assertIn("b_major_invalid", bad.violations)
        self.assertEqual(normalize_f16_known_facts({"group": 1,
            "b_shared_facts": {"major": "K"}}).b_shared_facts.major, "K")
        for raw in ({"group": True}, {"a_shared": 1},
                    {"b_shared_facts": {"unknown": "K"}},
                    {"unknown": 1}):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                normalize_f16_known_facts(raw)

    def test_emitter_uses_one_catalogue(self) -> None:
        """Generated C++ rows mirror the Python facts without a bit map."""

        header = emitter.render_tcgen_mma_header()
        source = emitter.render_tcgen_mma_source()
        self.assertIn("TcgenF16ShapeRow", header)
        self.assertIn("tcgen_f16_row_contains", source)
        self.assertIn("check_tcgen_f16_known_operation", source)
        self.assertIn("validate_tcgen_instruction_defined_fields", source)
        self.assertIn("validate_tcgen_shared_defined_fields", source)
        self.assertIn("MixedInputPair", header)
        self.assertIn("InvalidContext", header)
        self.assertIn("TcgenCtaGroup::One", source)
        self.assertIn("TcgenCtaGroup::Two", source)
        self.assertIn("'F', true", source)
        self.assertNotIn("reserved_", source)
        self.assertNotIn("tcgen05.mma", source)

    def test_canonical_eight_layouts_and_alias(self) -> None:
        """One form owns all structural source choices and one written alias."""

        raw = load_yaml(packaged_spec_dir() /
                        "tensor_memory_data_movement.yaml")
        mma = next(item for item in normalize_instruction_spec(raw)[0].variants
                   if item.name == "tcgen05_mma_f16")
        self.assertIs(mma.rule, SemanticRule.TENSOR_MEMORY_MMA)
        self.assertEqual(len(mma.operand_layouts), 8)
        self.assertEqual(mma.modifier_order_aliases,
                         (("mma", "kind", "cta_group"),))
        self.assertEqual({layout.name for layout in mma.operand_layouts}, {
            f"{a}_{m}_{s}" for a in ("shared", "tensor")
            for m in ("mask", "no_mask") for s in ("scale", "no_scale")})
        self.assertEqual({len(layout.operands)
                          for layout in mma.operand_layouts}, {5, 6, 7})
        self.assertEqual(len([layout for layout in mma.operand_layouts
                              if layout.availability]), 4)

    def test_canonical_drift_rejected(self) -> None:
        """Action, alias, operand role and source order stay closed."""

        source = load_yaml(packaged_spec_dir() /
                           "tensor_memory_data_movement.yaml")
        def change(mutation):
            raw = deepcopy(source)
            variant = next(item for item in raw["instructions"][0]["variants"]
                           if item["name"] == "tcgen05_mma_f16")
            mutation(variant)
            with self.assertRaises((ValueError, TypeError)):
                normalize_instruction_spec(raw)
        change(lambda variant: variant.update(rule="tensor_memory.copy"))
        change(lambda variant: variant["modifiers"][2].update(token=".kind::tf32"))
        change(lambda variant: variant["modifier_order_aliases"].append(
            ["cta_group", "mma", "kind"]))
        change(lambda variant: variant["operand_layouts"][0]["operands"][2].
               update(kind="reg_or_imm"))
        change(lambda variant: variant["operand_layouts"][0]["operands"].
               append({"name": "extra", "kind": "reg", "role": "src",
                       "access": "read"}))


if __name__ == "__main__":
    unittest.main()
