"""Regression coverage for the PTX `testp` and `copysign` specification slice."""

import unittest

from ptx_frontend.spec.database import load_codegen_database
from ptx_frontend.spec.model import (
    ModifierKind,
    OperandKind,
    OperandRole,
    OperandRegisterWidthPolicy,
)
from ptx_frontend.spec.resources import packaged_spec_dir


class TestpCopysignCompletenessTests(unittest.TestCase):
    """Keep floating classification and sign-copy contracts typed and ordered."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the packaged catalogue once and select this slice's opcodes."""

        database = load_codegen_database(spec_dir=packaged_spec_dir())
        instructions = {item.opcode: item for item in database.instructions}
        cls.testp = instructions["testp"]
        cls.copysign = instructions["copysign"]

    def test_models_all_typed_test_properties_and_historical_minima(self) -> None:
        """Require exactly the six ISA classifications on both supported widths."""

        variants = {variant.name: variant for variant in self.testp.variants}
        self.assertEqual(set(variants), {"testp_f32", "testp_f64"})
        for name, type_name in (("testp_f32", "f32"), ("testp_f64", "f64")):
            variant = variants[name]
            modifiers = {modifier.name: modifier for modifier in variant.modifiers}
            self.assertIs(modifiers["property"].kind, ModifierKind.TEST_PROPERTY)
            self.assertEqual(
                tuple(value.value for value in modifiers["property"].values),
                ("finite", "infinite", "number", "notanumber", "normal", "subnormal"),
            )
            self.assertEqual(modifiers["type"].value, type_name)
            self.assertEqual(dict(variant.availability), {"ptx": "2.0", "sm": 20})
            operands = variant.operand_layouts[0].operands
            self.assertIs(operands[0].kind, OperandKind.PREDICATE)
            self.assertIs(operands[1].kind, OperandKind.REGISTER_OR_IMMEDIATE)
            self.assertIs(operands[1].role, OperandRole.SOURCE_1)
            self.assertIs(operands[1].register_width_policy, OperandRegisterWidthPolicy.SAME_WIDTH)

    def test_preserves_copysign_sign_then_magnitude_roles(self) -> None:
        """Keep the first source as sign and the second as magnitude for both widths."""

        variants = {variant.name: variant for variant in self.copysign.variants}
        self.assertEqual(set(variants), {"copysign_f32", "copysign_f64"})
        for name, type_name in (("copysign_f32", "f32"), ("copysign_f64", "f64")):
            variant = variants[name]
            self.assertEqual(dict(variant.availability), {"ptx": "2.0", "sm": 20})
            self.assertEqual(
                {modifier.name: modifier.value for modifier in variant.modifiers}["type"],
                type_name,
            )
            operands = variant.operand_layouts[0].operands
            self.assertEqual(
                tuple(operand.name for operand in operands),
                ("dst", "sign_source", "magnitude_source"),
            )
            self.assertEqual(
                tuple(operand.role for operand in operands),
                (OperandRole.DESTINATION, OperandRole.SOURCE_1, OperandRole.SOURCE_2),
            )
            self.assertEqual(
                tuple(operand.kind for operand in operands[1:]),
                (OperandKind.REGISTER_OR_IMMEDIATE, OperandKind.REGISTER_OR_IMMEDIATE),
            )
            self.assertEqual(
                tuple(operand.register_width_policy for operand in operands),
                (OperandRegisterWidthPolicy.SAME_WIDTH,) * 3,
            )


if __name__ == "__main__":
    unittest.main()
