"""Regression coverage for explicit floating-point DIV mode cohorts."""

import unittest

from ptx_frontend.spec.database import load_codegen_database
from ptx_frontend.spec.model import ModifierPresence, OperandKind, OperandRegisterWidthPolicy
from ptx_frontend.spec.resources import packaged_spec_dir


class DivCompletenessTests(unittest.TestCase):
    """Keep typed DIV modes mutually exclusive while preserving integer forms."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the combined floating and integer DIV instruction specification."""

        instruction = next(
            item
            for item in load_codegen_database(spec_dir=packaged_spec_dir()).instructions
            if item.opcode == "div"
        )
        cls.variants = {variant.name: variant for variant in instruction.variants}

    def test_models_all_explicit_floating_modes_and_keeps_integer_variants(self) -> None:
        """Require mode-specific FP variants without removing the existing integer layouts."""

        self.assertTrue(
            {
                "div_rn_f32",
                "div_directed_f32",
                "div_approx_f32",
                "div_full_f32",
                "div_rn_f64",
                "div_directed_f64",
                "div_u32",
                "div_s64",
            }.issubset(self.variants)
        )
        for name in ("div_approx_f32", "div_full_f32"):
            self.assertEqual(dict(self.variants[name].availability), {"ptx": "1.4", "sm": 0})
        self.assertEqual(dict(self.variants["div_rn_f32"].availability), {"ptx": "1.4", "sm": 20})
        self.assertEqual(
            dict(self.variants["div_directed_f32"].availability), {"ptx": "1.4", "sm": 20}
        )
        self.assertEqual(dict(self.variants["div_rn_f64"].availability), {"ptx": "1.4", "sm": 13})
        self.assertEqual(
            dict(self.variants["div_directed_f64"].availability), {"ptx": "1.4", "sm": 20}
        )

    def test_uses_typed_mutually_exclusive_modes_and_same_width_float_operands(self) -> None:
        """Keep fixed approximation flags separate from required rounded directions."""

        expected_modes = {
            "div_approx_f32": ("approx", ModifierPresence.FIXED, True),
            "div_full_f32": ("full", ModifierPresence.FIXED, True),
            "div_rn_f32": ("rounding", ModifierPresence.FIXED, "rn"),
            "div_rn_f64": ("rounding", ModifierPresence.FIXED, "rn"),
        }
        for name, (field_name, presence, value) in expected_modes.items():
            modifiers = {modifier.name: modifier for modifier in self.variants[name].modifiers}
            self.assertIs(modifiers[field_name].presence, presence)
            self.assertEqual(modifiers[field_name].value, value)
        for name in ("div_directed_f32", "div_directed_f64"):
            rounding = {modifier.name: modifier for modifier in self.variants[name].modifiers}["rounding"]
            self.assertIs(rounding.presence, ModifierPresence.REQUIRED)
            self.assertEqual(tuple(value.value for value in rounding.values), ("rz", "rm", "rp"))

        for name in (
            "div_approx_f32",
            "div_full_f32",
            "div_rn_f32",
            "div_directed_f32",
            "div_rn_f64",
            "div_directed_f64",
        ):
            operands = self.variants[name].operand_layouts[0].operands
            self.assertEqual(operands[0].kind, OperandKind.REGISTER)
            self.assertEqual(
                tuple(operand.kind for operand in operands[1:]),
                (OperandKind.REGISTER_OR_IMMEDIATE,) * 2,
            )
            self.assertEqual(
                tuple(operand.register_width_policy for operand in operands),
                (OperandRegisterWidthPolicy.SAME_WIDTH,) * 3,
            )

        for name in ("div_rn_f32", "div_directed_f32", "div_approx_f32", "div_full_f32"):
            ftz = {modifier.name: modifier for modifier in self.variants[name].modifiers}["ftz"]
            self.assertIs(ftz.presence, ModifierPresence.OPTIONAL)
        for name in ("div_rn_f64", "div_directed_f64"):
            modifiers = {modifier.name: modifier for modifier in self.variants[name].modifiers}
            self.assertIs(modifiers["ftz"].presence, ModifierPresence.ABSENT)
            self.assertIs(modifiers["approx"].presence, ModifierPresence.ABSENT)
            self.assertIs(modifiers["full"].presence, ModifierPresence.ABSENT)


if __name__ == "__main__":
    unittest.main()
