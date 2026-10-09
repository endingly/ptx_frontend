"""Fixed PTX 9.3 dense MX4 source and conditional known-value rules."""

from dataclasses import replace
import unittest

from ptx_frontend.spec import tcgen_mma_operations as rules
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.normalize.tcgen_mma import validate_tcgen_mma_variant
from ptx_frontend.spec.resources import packaged_spec_dir


class TcgenMmaMx4ContractTests(unittest.TestCase):
    """Lock source topology and the distinct K64/K96 scale contracts."""

    def test_source_has_two_layouts_and_preserves_omission(self) -> None:
        """An omitted selector has its own source state and default."""

        raw = load_yaml(packaged_spec_dir() / "tensor_memory_data_movement.yaml")
        mma = next(item for item in normalize_instruction_spec(raw)[0].variants
                   if item.name == "tcgen05_mma_mxf4")
        self.assertEqual({layout.name for layout in mma.operand_layouts},
                         {"shared", "tensor"})
        scale = next(item for item in mma.modifiers
                     if item.name == "scale_vector_size")
        self.assertEqual(scale.default, "absent")
        self.assertEqual(tuple(item.value for item in scale.values),
                         ("scale_vec::2X", "block32"))
        for layout in mma.operand_layouts:
            self.assertEqual(tuple(item.name for item in layout.operands),
                             ("d", "a", "b", "idesc", "scale_a", "scale_b",
                              "enable_input_d"))
        for changed in (
            replace(mma, operand_layouts=mma.operand_layouts[:1]),
            replace(mma, modifiers=tuple(
                replace(item, default="block32")
                if item.name == "scale_vector_size" else item
                for item in mma.modifiers)),
            replace(mma, modifiers=tuple(reversed(mma.modifiers))),
            replace(mma, modifiers=tuple(
                replace(item, values=(replace(
                    item.values[0], availability={}), item.values[1]))
                if item.name == "scale_vector_size" else item
                for item in mma.modifiers)),
        ):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                validate_tcgen_mma_variant(changed)

    def test_shape_and_scale_rows_keep_k96_distinct(self) -> None:
        """K96 block32 is three factors; K96 Vec2X physical row is open."""

        rules.validate_catalogue()
        self.assertEqual({(r.group, r.m_values, r.k) for r in rules.MX4_SHAPES},
                         {(1, (128, 128), 64), (2, (128, 256), 64),
                          (2, (256, 256), 96)})
        row = rules.mx4_scale_layout("a", "absent", 96)
        self.assertEqual((row.factor_count, row.subcolumn_alignment_bytes,
                          row.valid_ids, row.layout_id,
                          row.id_alignment_policy),
                         (3, 4, (0, 2), rules.MxScaleLayoutId.MX3,
                          rules.ScaleIdAlignmentPolicy.LAYOUT_DEFINED_PLACEMENT))
        self.assertEqual(rules.mx4_scale_layout("a", "absent", 64),
                         rules.mx4_scale_layout("a", "block32", 64))
        self.assertIsNone(rules.mx4_scale_layout("a", "scale_vec::2X", 96))

    def test_known_k96_and_partial_facts(self) -> None:
        """Defined IDs and prescribed packing remain distinct from live proof."""

        base = rules.Mx4KnownFacts(
            group=2, m=256, n=32, k=96, d_type="F32", a_type="E2M1",
            b_type="E2M1", sparse=False, a_shared=False,
            scale_selector="absent", scale_type="UE8M0",
            scale_a_id=2, scale_b_id=0,
            scale_a_facts=rules.MxScaleRoleFacts(rules.MxScaleLayoutId.MX3, 4),
            scale_b_facts=rules.MxScaleRoleFacts(rules.MxScaleLayoutId.THREE_X_N,
                                                4),
            a_packing=rules.MxInputPacking.TMEM_PAIRED_FOUR_BIT,
            b_packing=rules.MxInputPacking.SHARED_PAIRED_FOUR_BIT)
        report = rules.check_mx4_known_facts(base)
        self.assertTrue(report.known_facts_ok)
        self.assertEqual((report.scale_a_factor_count,
                          report.scale_b_factor_count), (3, 3))
        self.assertIn("k96_exact_target", report.obligations)
        self.assertIn("a_live_packing_contents", report.obligations)
        self.assertNotIn("scale_a_alignment", report.violations)
        stronger = rules.check_mx4_known_facts(replace(
            base, scale_a_facts=rules.MxScaleRoleFacts(
                rules.MxScaleLayoutId.MX3, 8)))
        self.assertNotIn("scale_a_alignment", stronger.violations)
        vec = rules.check_mx4_known_facts(replace(
            base, scale_selector="scale_vec::2X"))
        self.assertEqual(vec.scale_a_factor_count, 2)
        self.assertIsNone(vec.scale_a_layout)
        self.assertIn("scale_a_layout_rule", vec.obligations)
        self.assertIn("scale_b_layout_rule", vec.obligations)
        self.assertIn("scale_a_id", rules.check_mx4_known_facts(
            replace(base, scale_a_id=1)).violations)
        self.assertIn("mx4_b_transpose", rules.check_mx4_known_facts(
            replace(base, transpose_b=True)).violations)
        self.assertIn("scale_a_alignment", rules.check_mx4_known_facts(
            replace(base, scale_a_facts=rules.MxScaleRoleFacts(
                rules.MxScaleLayoutId.MX3, 2))).violations)
        k64 = rules.check_mx4_known_facts(replace(
            base, group=1, m=128, k=64, scale_a_facts=rules.MxScaleRoleFacts(
                rules.MxScaleLayoutId.MX2, 4)))
        self.assertIn("scale_a_alignment", k64.violations)
        unknown = rules.check_mx4_known_facts(replace(
            base, a_shared=None, a_type=None,
            a_packing=rules.MxInputPacking.SHARED_PAIRED_FOUR_BIT))
        self.assertIsNone(unknown.required_a_packing)
        self.assertNotIn("a_packing_fact", unknown.violations)

    def test_k64_full_path_lane_facts_use_mx4_shape(self) -> None:
        """K64 keeps its selected datapath despite the K32 base query."""

        facts = rules.Mx4KnownFacts(
            group=1, m=128, n=32, k=64, d_type="F32", a_type="E2M1",
            b_type="E2M1", sparse=False, a_shared=False,
            a_lane_half=0, d_lane_half=0)
        valid = rules.check_mx4_known_facts(facts)
        self.assertEqual(valid.layout, "D")
        self.assertNotIn("a_lane_half_invalid", valid.violations)
        for field in ("a_lane_half", "d_lane_half"):
            bad = rules.check_mx4_known_facts(replace(facts, **{field: 16}))
            self.assertIn(f"{field}_invalid", bad.violations)
            missing = rules.check_mx4_known_facts(replace(facts, **{field: None}))
            self.assertIn(field, missing.obligations)


if __name__ == "__main__":
    unittest.main()
