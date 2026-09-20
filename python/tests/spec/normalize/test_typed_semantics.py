"""Boundary regressions for typed normalized PTX specification values."""

import unittest

from ptx_frontend.spec.model import (
    ModifierKind,
    ModifierPresence,
    OperandAccess,
    OperandKind,
    OperandRole,
    OperandTypeCompatibilityValueKind,
)
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.normalize.modifiers import normalize_modifier
from ptx_frontend.spec.normalize.operands import normalize_operand


class TypedSemanticNormalizationTests(unittest.TestCase):
    """Require raw YAML discriminators to become semantic enum members once."""

    def test_normalized_modifier_and_operand_fields_are_semantic_enums(self) -> None:
        """Parse modifier and operand discriminator spellings at the boundary."""

        modifier = normalize_modifier(
            {
                "name": "type",
                "kind": "type",
                "presence": "required",
                "values": ["u32"],
            },
            {},
        )
        operand = normalize_operand(
            {"name": "dst", "kind": "reg", "role": "dst", "access": "write"}
        )
        coordinate = normalize_operand(
            {
                "name": "coordinate",
                "kind": "tensor_coordinate",
                "role": "src",
                "access": "read",
                "cardinality": {"min": 1, "max": 2},
                "element_kinds": ["reg", "imm"],
            }
        )

        self.assertIs(modifier.kind, ModifierKind.TYPE)
        self.assertIs(modifier.presence, ModifierPresence.REQUIRED)
        self.assertIs(operand.kind, OperandKind.REGISTER)
        self.assertIs(operand.role, OperandRole.DESTINATION)
        self.assertIs(operand.access, OperandAccess.WRITE)
        self.assertEqual(len(coordinate.element_kinds), 2)
        self.assertIs(coordinate.element_kinds[0], OperandKind.REGISTER)
        self.assertIs(coordinate.element_kinds[1], OperandKind.IMMEDIATE)

    def test_operand_type_compatibility_value_kind_is_a_semantic_enum(self) -> None:
        """Normalize checker-rule discriminators before resolved-IR lowering."""

        instruction = normalize_instruction_spec(
            {
                "category": "test",
                "codegen_category": "test",
                "instructions": [
                    {
                        "opcode": "sample",
                        "variants": [
                            {
                                "name": "sample_default",
                                "availability": {"ptx": "1.0"},
                                "operands": [
                                    {
                                        "name": "src",
                                        "kind": "reg",
                                        "role": "src",
                                        "access": "read",
                                    }
                                ],
                                "operand_type_compatibilities": [
                                    {
                                        "operand": "src",
                                        "value_kind": "special_register",
                                        "values": ["tid"],
                                        "instruction_width": 32,
                                        "effective_type": "u32",
                                        "availability": {"ptx": "1.0"},
                                    }
                                ],
                            }
                        ],
                    }
                ],
            }
        )[0]

        compatibility = instruction.variants[0].operand_type_compatibilities[0]
        self.assertIs(
            compatibility.value_kind,
            OperandTypeCompatibilityValueKind.SPECIAL_REGISTER,
        )

    def test_invalid_discriminator_is_rejected_at_normalization(self) -> None:
        """Reject unsupported YAML category spellings before downstream use."""

        with self.assertRaisesRegex(ValueError, "unsupported modifier kind"):
            normalize_modifier(
                {"name": "type", "kind": "not_a_kind", "presence": "required"},
                {},
            )
        with self.assertRaisesRegex(ValueError, "unsupported operand role"):
            normalize_operand(
                {"name": "dst", "kind": "reg", "role": "not_a_role"}
            )


if __name__ == "__main__":
    unittest.main()
