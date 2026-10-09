"""Bounded PTX 9.3 dense MX8 source, target, and known scale-row contracts."""

from dataclasses import replace
import unittest

from ptx_frontend.spec import tcgen_mma_operations as rules
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.normalize.tcgen_mma import validate_tcgen_mma_variant
from ptx_frontend.spec.resources import packaged_spec_dir


class TcgenMmaMx8ContractTests(unittest.TestCase):
    """Keep the MX8 grammar and independent known-value table bounded."""

    def test_two_source_layouts_and_optional_selector(self) -> None:
        """Block-scaled syntax has no lane mask or D-scale operand."""

        raw = load_yaml(packaged_spec_dir() / "tensor_memory_data_movement.yaml")
        mma = next(item for item in normalize_instruction_spec(raw)[0].variants
                   if item.name == "tcgen05_mma_mxf8f6f4")
        self.assertEqual({layout.name for layout in mma.operand_layouts},
                         {"shared", "tensor"})
        self.assertEqual({item.name for item in mma.modifiers},
                         {"mma", "cta_group", "kind", "block_scale",
                          "scale_vector_size"})
        for layout in mma.operand_layouts:
            self.assertEqual(tuple(item.name for item in layout.operands),
                             ("d", "a", "b", "idesc", "scale_a", "scale_b",
                              "enable_input_d"))
        scale = next(item for item in mma.modifiers
                     if item.name == "scale_vector_size")
        self.assertEqual(scale.default, "absent")
        self.assertEqual(tuple(item.value for item in scale.values),
                         ("scale_vec::1X", "block32"))
        for changed in (
            replace(mma, operand_layouts=mma.operand_layouts[:1]),
            replace(mma, operand_layouts=(replace(
                mma.operand_layouts[0], name="shared_mask"),
                mma.operand_layouts[1])),
            replace(mma, modifiers=tuple(
                replace(item, default="scale_vec::1X")
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

    def test_table_42_and_scale_rows(self) -> None:
        """Omission derives a layout while retaining distinct source state."""

        rules.validate_catalogue()
        self.assertEqual({(row.group, row.m_values, row.n_first, row.n_step,
                           row.k) for row in rules.MX8_SHAPES},
                         {(1, (128, 128), 8, 8, 32),
                          (2, (128, 256), 16, 16, 32)})
        self.assertEqual(len(rules.MX8_SCALE_LAYOUTS), 4)
        self.assertEqual(rules.mx8_scale_layout("a", "absent"),
                         rules.mx8_scale_layout("a", "scale_vec::1X"))
        self.assertIsNone(rules.mx8_scale_layout("a", "scale_vec::2X"))
        for row in rules.MX8_SCALE_LAYOUTS:
            self.assertEqual((row.factor_count,
                              row.subcolumn_alignment_bytes, row.valid_ids),
                             (1, 1, (0, 1, 2, 3)))

    def test_known_facts_independent_and_missing(self) -> None:
        """Wrong supplied scale placement fails; absent facts stay missing."""

        base = rules.Mx8KnownFacts(
            group=1, m=128, n=16, k=32, d_type="F32", a_type="E4M3",
            b_type="E5M2", sparse=False, a_shared=False,
            scale_selector="absent", scale_type="UE8M0",
            scale_a_id=1, scale_b_id=3)
        report = rules.check_mx8_known_facts(base)
        self.assertTrue(report.known_facts_ok)
        self.assertIn("scale_a_layout", report.obligations)
        self.assertEqual(report.scale_a_layout.layout_id,
                         rules.MxScaleLayoutId.MX1)
        self.assertEqual(report.scale_b_layout.layout_id,
                         rules.MxScaleLayoutId.ONE_X_N)
        bad = rules.check_mx8_known_facts(replace(
            base, scale_a_facts=rules.MxScaleRoleFacts(
                rules.MxScaleLayoutId.ONE_X_N, 2)))
        self.assertIn("scale_a_layout", bad.violations)
        self.assertIn("scale_a_alignment", bad.violations)
        strong = rules.check_mx8_known_facts(replace(
            base, scale_a_id=0, scale_a_facts=rules.MxScaleRoleFacts(
                rules.MxScaleLayoutId.MX1, 4)))
        self.assertNotIn("scale_a_alignment", strong.violations)
        zero = rules.check_mx8_known_facts(replace(
            base, scale_a_facts=rules.MxScaleRoleFacts(
                rules.MxScaleLayoutId.MX1, 0)))
        self.assertIn("scale_a_alignment", zero.violations)
        packed = rules.check_mx8_known_facts(replace(
            base, a_type="E2M1", b_type="E2M3", a_shared=False,
            a_packing=rules.MxInputPacking.TMEM_EIGHT_BIT_CONTAINER,
            b_packing=rules.MxInputPacking.SHARED_PADDED_SIX_BIT))
        self.assertEqual(packed.required_a_packing,
                         rules.MxInputPacking.TMEM_EIGHT_BIT_CONTAINER)
        self.assertEqual(packed.required_b_packing,
                         rules.MxInputPacking.SHARED_PADDED_SIX_BIT)
        self.assertNotIn("a_low_bit_packing_rule", packed.obligations)
        self.assertIn("a_live_packing_contents", packed.obligations)
        self.assertNotIn("a_packing_fact", packed.violations)
        wrong_packing = rules.check_mx8_known_facts(replace(
            base, a_type="E2M1", a_shared=True,
            a_packing=rules.MxInputPacking.SHARED_PADDED_SIX_BIT))
        self.assertIn("a_packing_fact", wrong_packing.violations)
        unknown_placement = rules.check_mx8_known_facts(replace(
            base, a_type="E2M1", a_shared=None,
            a_packing=rules.MxInputPacking.SHARED_PADDED_FOUR_BIT))
        self.assertIsNone(unknown_placement.required_a_packing)
        self.assertIn("a_packing_placement", unknown_placement.obligations)
        self.assertNotIn("a_packing_fact", unknown_placement.violations)
        unknown_type = rules.check_mx8_known_facts(replace(
            base, a_type=None, a_shared=True,
            a_packing=rules.MxInputPacking.SHARED_PADDED_FOUR_BIT))
        self.assertIsNone(unknown_type.required_a_packing)
        self.assertNotIn("a_packing_fact", unknown_type.violations)
        invalid_fact = rules.check_mx8_known_facts(replace(
            base, a_type=None, a_shared=None, a_packing="invalid"))
        self.assertIn("a_packing_fact", invalid_fact.violations)
        self.assertIn("mx8_shape", rules.check_mx8_known_facts(
            replace(base, m=64)).violations)
        self.assertIn("scale_selector", rules.check_mx8_known_facts(
            replace(base, scale_selector="scale_vec::2X")).violations)
        self.assertIn("scale_type", rules.check_mx8_known_facts(
            replace(base, scale_type="UE4M3")).violations)


if __name__ == "__main__":
    unittest.main()
