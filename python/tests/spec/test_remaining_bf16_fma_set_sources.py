"""Six ordinary BF16 source permissions retain exact logical metadata and public payloads."""

import unittest
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import (
    OperandKind, OperandRegisterWidthPolicy, OperandTypeExpressionKind,
)

KEYS = (
    ("fma", "fma_mixed_f32_bf16", "src1"),
    ("fma", "fma_mixed_f32_bf16", "src2"),
    ("set", "set_half_integer_bf16", "src1"),
    ("set", "set_half_integer_bf16", "src2"),
    ("set", "set_half_integer_bf16_boolean", "src1"),
    ("set", "set_half_integer_bf16_boolean", "src2"),
)


class RemainingBf16FmaSetSourcesTests(unittest.TestCase):
    """Pin exact source changes without widening destinations or sibling forms."""

    def test_six_logical_bf16_sources_use_existing_reg_or_imm(self) -> None:
        """FMA uses its fixed input_type; SET uses fixed BF16 with no integer grant."""
        database = get_packaged_spec_database()
        variants = {(instruction.opcode, variant.name): variant
                    for instruction in database.instructions for variant in instruction.variants}
        for opcode, name, field in KEYS:
            variant = variants[opcode, name]
            self.assertEqual(len(variant.operand_layouts), 1)
            layout = variant.operand_layouts[0]
            self.assertEqual(layout.name, "default")
            operand = next(item for item in layout.operands if item.name == field)
            with self.subTest(opcode=opcode, variant=name, field=field):
                self.assertIs(operand.kind, OperandKind.REGISTER_OR_IMMEDIATE)
                self.assertIs(operand.register_width_policy, OperandRegisterWidthPolicy.SAME_WIDTH)
                self.assertIsNone(operand.integer_immediate_type)
                if opcode == "fma":
                    self.assertIs(operand.type_expression.kind, OperandTypeExpressionKind.MODIFIER)
                    self.assertEqual(operand.type_expression.modifier_name, "input_type")
                    control = next(item for item in variant.modifiers if item.name == "input_type")
                    self.assertEqual(control.value, "bf16")
                    self.assertEqual(variant.availability, {"ptx": "8.6", "sm": 100})
                else:
                    self.assertIs(operand.type_expression.kind, OperandTypeExpressionKind.FIXED_SCALAR)
                    self.assertEqual(operand.type_expression.scalar_type, "bf16")
                    self.assertEqual(variant.availability, {"ptx": "7.8", "sm": 90})
                self.assertIs(layout.operands[0].kind, OperandKind.REGISTER)
        for name in ("set_half_integer_bf16x2", "set_half_integer_bf16x2_boolean"):
            for operand in variants["set", name].operand_layouts[0].operands[1:3]:
                self.assertIs(operand.kind, OperandKind.REGISTER)
                self.assertEqual(operand.type_expression.scalar_type, "b32")
                self.assertIs(operand.register_width_policy, OperandRegisterWidthPolicy.EXACT)
