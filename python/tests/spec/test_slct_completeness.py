"""Regression coverage for the complete PTX 9.3 SLCT specification."""

import unittest

from ptx_frontend.spec.database import load_codegen_database
from ptx_frontend.spec.model import (
    ModifierPresence,
    OperandKind,
    OperandRegisterWidthPolicy,
)
from ptx_frontend.spec.resources import packaged_spec_dir


class SlctCompletenessTests(unittest.TestCase):
    """Keep data type, selector, FTZ, and target rules in two disjoint variants."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the canonical SLCT descriptor after same-opcode merging."""

        database = load_codegen_database(spec_dir=packaged_spec_dir())
        instruction = next(item for item in database.instructions if item.opcode == "slct")
        cls.variants = {variant.name: variant for variant in instruction.variants}

    def test_models_all_ptx93_types_and_selector_controls(self) -> None:
        """Both selector kinds accept every data type with an F64 SM gate."""

        self.assertEqual(set(self.variants), {"slct_s32", "slct_f32"})
        expected_types = {
            "b16", "b32", "b64", "u16", "u32", "u64",
            "s16", "s32", "s64", "f32", "f64",
        }
        for name, selector, ftz in (
            ("slct_s32", "s32", ModifierPresence.ABSENT),
            ("slct_f32", "f32", ModifierPresence.OPTIONAL),
        ):
            with self.subTest(name=name):
                variant = self.variants[name]
                self.assertEqual(variant.availability, {"ptx": "1.0", "sm": 0})
                controls = {modifier.name: modifier for modifier in variant.modifiers}
                self.assertIs(controls["ftz"].presence, ftz)
                self.assertEqual(controls["stype"].value, selector)
                self.assertEqual(
                    {value.value for value in controls["dtype"].values},
                    expected_types,
                )
                f64 = next(value for value in controls["dtype"].values if value.value == "f64")
                self.assertEqual(f64.availability, {"sm": 13})
                self.assertTrue(
                    all(not value.availability for value in controls["dtype"].values if value.value != "f64")
                )

    def test_binds_data_to_dtype_and_selector_to_stype(self) -> None:
        """All data and selector slots preserve numeric register-or-immediate shape."""

        for variant in self.variants.values():
            with self.subTest(name=variant.name):
                operands = variant.operand_layouts[0].operands
                self.assertEqual(
                    tuple(operand.kind for operand in operands),
                    (OperandKind.REGISTER,) + (OperandKind.REGISTER_OR_IMMEDIATE,) * 3,
                )
                self.assertEqual(
                    tuple(operand.type_expression.modifier_name for operand in operands),
                    ("dtype", "dtype", "dtype", "stype"),
                )
                self.assertTrue(
                    all(
                        operand.register_width_policy is OperandRegisterWidthPolicy.SAME_WIDTH
                        for operand in operands
                    )
                )


if __name__ == "__main__":
    unittest.main()
