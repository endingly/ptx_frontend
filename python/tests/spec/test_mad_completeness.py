"""Regression coverage for the explicit floating-point MAD cohorts."""

import unittest

from ptx_frontend.spec.database import load_codegen_database
from ptx_frontend.spec.model import ModifierPresence, OperandKind, OperandRegisterWidthPolicy
from ptx_frontend.spec.resources import packaged_spec_dir


class MadCompletenessTests(unittest.TestCase):
    """Keep explicit floating MAD distinct from integer and legacy forms."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the canonical MAD variants from the packaged specification."""

        instruction = next(
            item
            for item in load_codegen_database(spec_dir=packaged_spec_dir()).instructions
            if item.opcode == "mad"
        )
        cls.variants = {variant.name: variant for variant in instruction.variants}

    def test_models_all_explicit_rounding_and_preserves_integer_variants(self) -> None:
        """Require two FP cohorts while retaining the existing integer and carry forms."""

        self.assertTrue(
            {
                "mad_rn_f32",
                "mad_directed_f32",
                "mad_rn_f64",
                "mad_directed_f64",
                "mad_lo_u32",
                "mad_hi_cc_32",
            }.issubset(self.variants)
        )
        for name in ("mad_rn_f32", "mad_directed_f32"):
            self.assertEqual(dict(self.variants[name].availability), {"ptx": "2.0", "sm": 20})
        for name in ("mad_rn_f64", "mad_directed_f64"):
            self.assertEqual(dict(self.variants[name].availability), {"ptx": "1.0", "sm": 13})

    def test_limits_modifiers_and_float_operand_shapes(self) -> None:
        """Enforce required rounding, FP32-only FTZ/saturation, and float immediates."""

        for name, rounding_values in (
            ("mad_rn_f32", ()),
            ("mad_directed_f32", ("rz", "rm", "rp")),
            ("mad_rn_f64", ()),
            ("mad_directed_f64", ("rz", "rm", "rp")),
        ):
            variant = self.variants[name]
            modifiers = {modifier.name: modifier for modifier in variant.modifiers}
            rounding = modifiers["rounding"]
            if rounding_values:
                self.assertIs(rounding.presence, ModifierPresence.REQUIRED)
                self.assertEqual(tuple(value.value for value in rounding.values), rounding_values)
            else:
                self.assertIs(rounding.presence, ModifierPresence.FIXED)
                self.assertEqual(rounding.value, "rn")
            bindings = variant.operand_layouts[0].operands
            self.assertEqual(bindings[0].kind, OperandKind.REGISTER)
            self.assertEqual(
                tuple(binding.kind for binding in bindings[1:]),
                (OperandKind.REGISTER_OR_IMMEDIATE,) * 3,
            )
            self.assertEqual(
                tuple(binding.register_width_policy for binding in bindings),
                (OperandRegisterWidthPolicy.SAME_WIDTH,) * 4,
            )

        for name in ("mad_rn_f32", "mad_directed_f32"):
            modifiers = {modifier.name: modifier for modifier in self.variants[name].modifiers}
            self.assertIs(modifiers["ftz"].presence, ModifierPresence.OPTIONAL)
            self.assertIs(modifiers["sat"].presence, ModifierPresence.OPTIONAL)
        for name in ("mad_rn_f64", "mad_directed_f64"):
            self.assertEqual(
                {modifier.name for modifier in self.variants[name].modifiers},
                {"rounding", "type"},
            )


if __name__ == "__main__":
    unittest.main()
