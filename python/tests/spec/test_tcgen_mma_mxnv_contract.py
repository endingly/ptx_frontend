"""Fixed PTX 9.3 dense MX NV source and conditional known-value rules."""

from dataclasses import replace
import unittest

from ptx_frontend.spec import tcgen_mma_operations as rules
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.normalize.tcgen_mma import validate_tcgen_mma_variant
from ptx_frontend.spec.resources import packaged_spec_dir


class TcgenMmaMxNvContractTests(unittest.TestCase):
    """Lock the explicit selector and distinct K64/K96 scale contracts."""

    def test_source_has_two_placements_and_required_selector(self) -> None:
        """Canonical source keeps all four written selectors and both roles."""

        raw = load_yaml(packaged_spec_dir() / "tensor_memory_data_movement.yaml")
        mma = next(item for item in normalize_instruction_spec(raw)[0].variants
                   if item.name == "tcgen05_mma_mxf4nvf4")
        self.assertEqual({layout.name for layout in mma.operand_layouts},
                         {"shared", "tensor"})
        scale = next(item for item in mma.modifiers
                     if item.name == "scale_vector_size")
        self.assertEqual(scale.default, None)
        self.assertEqual(tuple(item.value for item in scale.values),
                         ("scale_vec::2X", "scale_vec::4X", "block32",
                          "block16"))
        for layout in mma.operand_layouts:
            self.assertEqual(tuple(item.name for item in layout.operands),
                             ("d", "a", "b", "idesc", "scale_a", "scale_b",
                              "enable_input_d"))
        for changed in (
            replace(mma, operand_layouts=mma.operand_layouts[:1]),
            replace(mma, modifiers=tuple(
                replace(item, default="absent")
                if item.name == "scale_vector_size" else item
                for item in mma.modifiers)),
            replace(mma, modifiers=tuple(reversed(mma.modifiers))),
            replace(mma, modifiers=tuple(
                replace(item, values=(replace(item.values[0],
                                             availability={}),) +
                        item.values[1:])
                if item.name == "scale_vector_size" else item
                for item in mma.modifiers)),
        ):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                validate_tcgen_mma_variant(changed)

    def test_scale_rows_and_fixed_descriptor_domains(self) -> None:
        """K96 block16 has six factors; descriptor IDs stay at zero/two."""

        rules.validate_catalogue()
        self.assertEqual(len(rules.MXNV_SCALE_LAYOUTS), 12)
        cases = (
            ("scale_vec::2X", 64, 2, 2, rules.MxScaleLayoutId.MX2,
             (0, 2)),
            ("scale_vec::4X", 64, 4, 4, rules.MxScaleLayoutId.MX4, (0,)),
            ("block32", 96, 3, 4, rules.MxScaleLayoutId.MX3, (0, 2)),
            ("block16", 96, 6, 4, rules.MxScaleLayoutId.MX6, (0, 2)),
        )
        for selector, k, factors, align, identity, ids in cases:
            row = rules.mxnv_scale_layout("a", selector, k)
            self.assertEqual((row.factor_count, row.subcolumn_alignment_bytes,
                              row.layout_id, row.valid_ids),
                             (factors, align, identity, ids))
        self.assertIsNone(rules.mxnv_scale_layout("a", "scale_vec::4X", 96))

    def test_known_k96_partial_facts_and_mixed_scale_types(self) -> None:
        """Caller facts cannot discharge an absent normative vector layout."""

        base = rules.MxNvKnownFacts(
            group=2, m=256, n=32, k=96, d_type="F32", a_type="E2M1",
            b_type="E2M1", sparse=False, a_shared=False,
            scale_selector="block16", scale_type="UE4M3",
            scale_a_id=2, scale_b_id=0,
            scale_a_facts=rules.MxScaleRoleFacts(rules.MxScaleLayoutId.MX6, 4),
            scale_b_facts=rules.MxScaleRoleFacts(rules.MxScaleLayoutId.SIX_X_N,
                                                4),
            a_packing=rules.MxInputPacking.TMEM_PAIRED_FOUR_BIT,
            b_packing=rules.MxInputPacking.SHARED_PAIRED_FOUR_BIT)
        report = rules.check_mxnv_known_facts(base)
        self.assertTrue(report.known_facts_ok)
        self.assertEqual((report.scale_a_factor_count,
                          report.scale_b_factor_count), (6, 6))
        self.assertIn("k96_exact_target", report.obligations)
        self.assertIn("a_live_packing_contents", report.obligations)
        self.assertNotIn("scale_a_alignment", report.violations)
        self.assertNotIn("scale_a_alignment", rules.check_mxnv_known_facts(
            replace(base, scale_a_facts=rules.MxScaleRoleFacts(
                rules.MxScaleLayoutId.MX6, 8))).violations)
        for selector, factors in (("scale_vec::2X", 2),
                                  ("scale_vec::4X", 4)):
            vec = rules.check_mxnv_known_facts(replace(
                base, scale_selector=selector, scale_type="UE8M0"))
            self.assertEqual(vec.scale_a_factor_count, factors)
            self.assertIsNone(vec.scale_a_layout)
            self.assertIn("scale_a_layout_rule", vec.obligations)
        self.assertIn("scale_type", rules.check_mxnv_known_facts(
            replace(base, scale_selector="block32")).violations)
        self.assertIn("scale_selector", rules.check_mxnv_known_facts(
            replace(base, scale_selector="absent")).violations)
        self.assertIn("scale_a_id", rules.check_mxnv_known_facts(
            replace(base, scale_a_id=1)).violations)
        k64_four = replace(
            base, group=1, m=128, k=64, scale_a_id=2,
            scale_a_facts=rules.MxScaleRoleFacts(rules.MxScaleLayoutId.MX4, 4),
            scale_b_facts=rules.MxScaleRoleFacts(rules.MxScaleLayoutId.FOUR_X_N,
                                                4))
        self.assertIn("scale_a_id",
                      rules.check_mxnv_known_facts(k64_four).violations)
        self.assertIn("scale_a_alignment", rules.check_mxnv_known_facts(
            replace(base, scale_a_facts=rules.MxScaleRoleFacts(
                rules.MxScaleLayoutId.MX6, 2))).violations)
        unknown = rules.check_mxnv_known_facts(replace(
            base, a_shared=None, a_type=None,
            a_packing=rules.MxInputPacking.SHARED_PAIRED_FOUR_BIT))
        self.assertIsNone(unknown.required_a_packing)
        self.assertNotIn("a_packing_fact", unknown.violations)


if __name__ == "__main__":
    unittest.main()
