"""Regression coverage for explicit reciprocal and square-root instruction cohorts."""

import unittest

from ptx_frontend.spec.database import load_codegen_database
from ptx_frontend.spec.model import ModifierPresence, OperandKind, OperandRegisterWidthPolicy
from ptx_frontend.spec.resources import packaged_spec_dir


class UnaryFloatCompletenessTests(unittest.TestCase):
    """Preserve typed approximate and rounded unary floating forms."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the three unary floating instruction specifications by opcode."""

        cls.instructions = {
            instruction.opcode: {variant.name: variant for variant in instruction.variants}
            for instruction in load_codegen_database(spec_dir=packaged_spec_dir()).instructions
            if instruction.opcode in {"rcp", "sqrt", "rsqrt"}
        }

    def test_models_complete_rcp_sqrt_and_rsqrt_variant_sets(self) -> None:
        """Require each canonical explicit mode and the two special FP64 cohorts."""

        self.assertEqual(
            set(self.instructions["rcp"]),
            {
                "rcp_approx_f32",
                "rcp_rn_f32",
                "rcp_directed_f32",
                "rcp_rn_f64",
                "rcp_directed_f64",
                "rcp_approx_ftz_f64",
            },
        )
        self.assertEqual(
            set(self.instructions["sqrt"]),
            {
                "sqrt_approx_f32",
                "sqrt_rn_f32",
                "sqrt_directed_f32",
                "sqrt_rn_f64",
                "sqrt_directed_f64",
            },
        )
        self.assertEqual(
            set(self.instructions["rsqrt"]),
            {"rsqrt_approx_f32", "rsqrt_approx_f64", "rsqrt_approx_ftz_f64"},
        )

    def test_assigns_independent_ptx_and_target_minima(self) -> None:
        """Keep approximate, rounded, ordinary-FP64, and special-FP64 boundaries distinct."""

        for opcode in ("rcp", "sqrt"):
            variants = self.instructions[opcode]
            self.assertEqual(dict(variants[f"{opcode}_approx_f32"].availability), {"ptx": "1.4", "sm": 0})
            self.assertEqual(dict(variants[f"{opcode}_rn_f32"].availability), {"ptx": "2.0", "sm": 20})
            self.assertEqual(dict(variants[f"{opcode}_directed_f32"].availability), {"ptx": "2.0", "sm": 20})
            self.assertEqual(dict(variants[f"{opcode}_rn_f64"].availability), {"ptx": "1.4", "sm": 13})
            self.assertEqual(dict(variants[f"{opcode}_directed_f64"].availability), {"ptx": "2.0", "sm": 20})
        self.assertEqual(
            dict(self.instructions["rcp"]["rcp_approx_ftz_f64"].availability), {"ptx": "2.1", "sm": 20}
        )
        self.assertEqual(
            dict(self.instructions["rsqrt"]["rsqrt_approx_f32"].availability), {"ptx": "1.4", "sm": 0}
        )
        self.assertEqual(
            dict(self.instructions["rsqrt"]["rsqrt_approx_f64"].availability), {"ptx": "1.4", "sm": 13}
        )
        self.assertEqual(
            dict(self.instructions["rsqrt"]["rsqrt_approx_ftz_f64"].availability), {"ptx": "4.0", "sm": 20}
        )

    def test_uses_typed_modes_and_same_width_float_operands(self) -> None:
        """Ensure modes are typed and all sources retain literal and bit-container support."""

        for variants in self.instructions.values():
            for variant in variants.values():
                operands = variant.operand_layouts[0].operands
                self.assertEqual(operands[0].kind, OperandKind.REGISTER)
                self.assertEqual(operands[1].kind, OperandKind.REGISTER_OR_IMMEDIATE)
                self.assertEqual(
                    tuple(operand.register_width_policy for operand in operands),
                    (OperandRegisterWidthPolicy.SAME_WIDTH,) * 2,
                )
                modifiers = {modifier.name: modifier for modifier in variant.modifiers}
                if "approx" in variant.name:
                    self.assertIs(modifiers["approx"].presence, ModifierPresence.FIXED)
                else:
                    self.assertIs(modifiers["approx"].presence, ModifierPresence.ABSENT)

        for opcode in ("rcp", "sqrt"):
            variants = self.instructions[opcode]
            for name in (f"{opcode}_rn_f32", f"{opcode}_directed_f32"):
                self.assertIs(
                    {modifier.name: modifier for modifier in variants[name].modifiers}["ftz"].presence,
                    ModifierPresence.OPTIONAL,
                )
            directed = {modifier.name: modifier for modifier in variants[f"{opcode}_directed_f64"].modifiers}["rounding"]
            self.assertIs(directed.presence, ModifierPresence.REQUIRED)
            self.assertEqual(tuple(value.value for value in directed.values), ("rz", "rm", "rp"))

        for opcode, name in (("rcp", "rcp_approx_ftz_f64"), ("rsqrt", "rsqrt_approx_ftz_f64")):
            modifiers = {modifier.name: modifier for modifier in self.instructions[opcode][name].modifiers}
            self.assertIs(modifiers["ftz"].presence, ModifierPresence.FIXED)
            self.assertIs(modifiers["ftz"].value, True)


if __name__ == "__main__":
    unittest.main()
