"""Regression coverage for floating-point ABS and NEG cohort contracts."""

import unittest

from ptx_frontend.spec.database import load_codegen_database
from ptx_frontend.spec.model import (
    ModifierPresence,
    OperandKind,
    OperandRegisterWidthPolicy,
)
from ptx_frontend.spec.resources import packaged_spec_dir


class AbsNegCompletenessTests(unittest.TestCase):
    """Keep floating unary forms, target minima, flags, and containers distinct."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the packaged instruction catalogue and select unary opcodes."""

        instructions = {
            item.opcode
            : item
            for item in load_codegen_database(spec_dir=packaged_spec_dir()).instructions
        }
        cls.abs_variants = {variant.name: variant for variant in instructions["abs"].variants}
        cls.neg_variants = {variant.name: variant for variant in instructions["neg"].variants}

    def test_models_all_requested_float_cohorts_and_minima(self) -> None:
        """Require scalar, half, packed, and bfloat forms without changing integer forms."""

        for variants in (self.abs_variants, self.neg_variants):
            self.assertTrue(
                {
                    "f32", "f64", "f16", "f16x2", "bf16", "bf16x2"
                }.issubset(
                    {variant.name.removeprefix("abs_").removeprefix("neg_") for variant in variants.values()}
                )
            )
        for name in ("abs_f64", "neg_f64"):
            self.assertEqual(dict(self._variant(name).availability), {"ptx": "1.0", "sm": 13})
        for name in ("abs_f16", "abs_f16x2"):
            self.assertEqual(dict(self.abs_variants[name].availability), {"ptx": "6.5", "sm": 53})
        for name in ("neg_f16", "neg_f16x2"):
            self.assertEqual(dict(self.neg_variants[name].availability), {"ptx": "6.0", "sm": 53})
        for name in ("abs_bf16", "abs_bf16x2", "neg_bf16", "neg_bf16x2"):
            self.assertEqual(dict(self._variant(name).availability), {"ptx": "7.0", "sm": 80})

    def test_limits_ftz_and_encodes_each_physical_container(self) -> None:
        """Allow FTZ only for FP32/half and retain each documented storage rule."""

        for name in ("abs_f32", "abs_f16", "abs_f16x2", "neg_f32", "neg_f16", "neg_f16x2"):
            modifiers = {modifier.name: modifier for modifier in self._variant(name).modifiers}
            self.assertIs(modifiers["ftz"].presence, ModifierPresence.OPTIONAL)
        for name in ("abs_f32", "neg_f32"):
            ftz_values = {value.value: value for value in self._variant(name).modifiers[0].values}
            self.assertEqual(dict(ftz_values[True].availability), {"ptx": "1.4"})
        for name in ("abs_f64", "abs_bf16", "abs_bf16x2", "neg_f64", "neg_bf16", "neg_bf16x2"):
            modifiers = {modifier.name: modifier for modifier in self._variant(name).modifiers}
            self.assertIs(modifiers["ftz"].presence, ModifierPresence.ABSENT)

        for name, container, policy in (
            ("abs_f16", "f16", OperandRegisterWidthPolicy.SAME_WIDTH),
            ("abs_f16x2", "f16x2", OperandRegisterWidthPolicy.SAME_WIDTH),
            ("neg_f16", "f16", OperandRegisterWidthPolicy.SAME_WIDTH),
            ("neg_f16x2", "b32", OperandRegisterWidthPolicy.EXACT),
            ("abs_bf16", "b16", OperandRegisterWidthPolicy.EXACT),
            ("abs_bf16x2", "b32", OperandRegisterWidthPolicy.EXACT),
            ("neg_bf16", "b16", OperandRegisterWidthPolicy.EXACT),
            ("neg_bf16x2", "b32", OperandRegisterWidthPolicy.EXACT),
        ):
            operands = self._variant(name).operand_layouts[0].operands
            self.assertEqual(tuple(operand.kind for operand in operands), (OperandKind.REGISTER,) * 2)
            self.assertEqual(
                tuple(operand.type_expression.scalar_type for operand in operands),
                (container,) * 2,
            )
            self.assertEqual(tuple(operand.register_width_policy for operand in operands), (policy,) * 2)

        for name in ("abs_f32", "abs_f64", "neg_f32", "neg_f64"):
            operands = self._variant(name).operand_layouts[0].operands
            self.assertEqual(operands[0].kind, OperandKind.REGISTER)
            self.assertEqual(operands[1].kind, OperandKind.REGISTER_OR_IMMEDIATE)
            self.assertEqual(
                tuple(operand.register_width_policy for operand in operands),
                (OperandRegisterWidthPolicy.SAME_WIDTH,) * 2,
            )

    def _variant(self, name: str):
        """Return one ABS or NEG variant by its stable YAML identifier."""

        return (self.abs_variants | self.neg_variants)[name]


if __name__ == "__main__":
    unittest.main()
