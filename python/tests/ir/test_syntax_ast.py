from __future__ import annotations

from pathlib import Path
import re
import tempfile
import unittest
import sys

REPO_ROOT = Path(__file__).resolve().parents[3]
PYTHON_ROOT = REPO_ROOT / "python"

if str(PYTHON_ROOT) not in sys.path:
    sys.path.insert(0, str(PYTHON_ROOT))

from code_gen.database import load_codegen_database
from code_gen.load_yaml import expand_value_refs
from code_gen.normalize import normalize_instruction_spec
from code_gen.gen_syntax_ast_arch import (
    emit_check_end_instruction_descriptor_implementation,
    generate_syntax_descriptor_source,
)
from ir.syntax_ast import from_InstructionSpec
from ir.syntax_ast import (
    ModifierPresence,
    OperandLayoutKind,
    OperandPresence,
    OperandSyntaxShape,
)


class SyntaxAstDescriptorBuildTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        add = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "add"
        )
        cls.descriptor = from_InstructionSpec(add)

    def test_add_variants_and_modifier_constraints(self) -> None:
        self.assertEqual(self.descriptor.opcode, "add")
        self.assertEqual(
            [variant.variant_id for variant in self.descriptor.variants],
            [
                "add_integer_no_sat",
                "add_sat_s32",
                "add_simd_no_sat_sm90",
                "add_packed_optional_sat_sm120",
                "add_sat_sm120",
            ],
        )

        integer_no_sat = self.descriptor.variants[0]
        self.assertEqual(
            [
                (modifier.kind_id, modifier.presence, modifier.allowed_spellings)
                for modifier in integer_no_sat.modifiers
            ],
            [
                ("sat", ModifierPresence.ABSENT, ()),
                (
                    "type",
                    ModifierPresence.REQUIRED,
                    (".u16", ".u32", ".u64", ".s16", ".s32", ".s64"),
                ),
            ],
        )
        sat_s32 = self.descriptor.variants[1]
        self.assertEqual(
            [
                (modifier.kind_id, modifier.presence, modifier.allowed_spellings)
                for modifier in sat_s32.modifiers
            ],
            [
                ("sat", ModifierPresence.REQUIRED, (".sat",)),
                ("type", ModifierPresence.REQUIRED, (".s32",)),
            ],
        )

    def test_add_binary_flat_operand_layout(self) -> None:
        layout = self.descriptor.variants[0].operand_layouts[0]

        self.assertEqual(layout.kind, OperandLayoutKind.FLAT)
        self.assertEqual(layout.layout_id, "default")
        self.assertEqual(
            [
                (slot.allowed_syntax_shapes, slot.presence)
                for slot in layout.slots
            ],
            [
                (
                    OperandSyntaxShape.IDENTIFIER_REF,
                    OperandPresence.REQUIRED,
                ),
                (
                    OperandSyntaxShape.IDENTIFIER_REF | OperandSyntaxShape.IMMEDIATE,
                    OperandPresence.REQUIRED,
                ),
                (
                    OperandSyntaxShape.IDENTIFIER_REF | OperandSyntaxShape.IMMEDIATE,
                    OperandPresence.REQUIRED,
                ),
            ],
        )

        self.assertTrue(layout.slots[1].allows(OperandSyntaxShape.IDENTIFIER_REF))
        self.assertTrue(layout.slots[1].allows(OperandSyntaxShape.IMMEDIATE))
        self.assertFalse(layout.slots[1].allows(OperandSyntaxShape.ADDRESS))

    def test_normalize_explicit_operand_layouts(self) -> None:
        raw_spec = {
            "instructions": [
                {
                    "opcode": "sample",
                    "variants": [
                        {
                            "name": "sample_typed",
                            "availability": {"ptx": "1.0", "sm": 0},
                            "operand_layouts": [
                                {
                                    "name": "binary",
                                    "operands": [
                                        {
                                            "name": "dst",
                                            "kind": "reg",
                                            "role": "dst",
                                            "access": "write",
                                        },
                                    ],
                                },
                                {
                                    "name": "unary",
                                    "operands": [
                                        {
                                            "name": "src",
                                            "kind": "reg_or_imm",
                                            "role": "src",
                                            "access": "read",
                                        },
                                    ],
                                },
                            ],
                        },
                    ],
                },
            ],
        }

        instruction = normalize_instruction_spec(raw_spec)[0]
        layouts = instruction.variants[0].operand_layouts

        self.assertEqual([layout.name for layout in layouts], ["binary", "unary"])
        self.assertEqual(
            [[operand.name for operand in layout.operands] for layout in layouts],
            [["dst"], ["src"]],
        )

    def test_references_and_inline_data_normalize_identically(self) -> None:
        operands = [
            {
                "name": "dst",
                "kind": "reg",
                "role": "dst",
                "access": "write",
                "type": {"expr": "modifier(type)"},
            }
        ]
        common_instruction = {
            "opcode": "sample",
            "variants": [
                {
                    "name": "sample_type",
                    "availability": {"ptx": "1.0", "sm": 0},
                    "modifiers": [
                        {
                            "name": "type",
                            "kind": "type",
                            "presence": "required",
                            "domain": "scalar_types",
                        }
                    ],
                }
            ],
        }
        referenced = {
            "type_sets": {"numeric": ["u32", "u64"]},
            "operand_patterns": {"unary": operands},
            "instructions": [
                {
                    **common_instruction,
                    "operands": "$unary",
                    "variants": [
                        {
                            **common_instruction["variants"][0],
                            "modifiers": [
                                {
                                    **common_instruction["variants"][0]["modifiers"][0],
                                    "values": ["$numeric"],
                                }
                            ],
                        }
                    ],
                }
            ],
        }
        inline = {
            "instructions": [
                {
                    **common_instruction,
                    "operands": operands,
                    "variants": [
                        {
                            **common_instruction["variants"][0],
                            "modifiers": [
                                {
                                    **common_instruction["variants"][0]["modifiers"][0],
                                    "values": ["u32", "u64"],
                                }
                            ],
                        }
                    ],
                }
            ]
        }

        self.assertEqual(
            normalize_instruction_spec(referenced), normalize_instruction_spec(inline)
        )

    def test_value_set_references_expand_recursively_and_reject_cycles(self) -> None:
        self.assertEqual(
            expand_value_refs(
                ["$extended"],
                {"base": ["u32"], "extended": ["$base", "u64"]},
            ),
            ("u32", "u64"),
        )
        with self.assertRaisesRegex(ValueError, "cyclic type-set reference"):
            expand_value_refs(
                ["$first"],
                {"first": ["$second"], "second": ["$first"]},
            )

    def test_rejects_bare_operand_pattern_name(self) -> None:
        with self.assertRaisesRegex(ValueError, "must use the '\\$name' form"):
            normalize_instruction_spec(
                {
                    "operand_patterns": {"unary": []},
                    "instructions": [
                        {
                            "opcode": "sample",
                            "variants": [
                                {
                                    "name": "sample",
                                    "availability": {"ptx": "1.0"},
                                    "operands": "unary",
                                }
                            ],
                        }
                    ],
                }
            )

    def test_type_expression_requires_supported_active_type_modifier(self) -> None:
        def normalize_with_expr(expression: str, modifiers: list[dict[str, object]]):
            return normalize_instruction_spec(
                {
                    "instructions": [
                        {
                            "opcode": "sample",
                            "variants": [
                                {
                                    "name": "sample",
                                    "availability": {"ptx": "1.0"},
                                    "modifiers": modifiers,
                                    "operands": [
                                        {
                                            "name": "src",
                                            "kind": "reg_or_imm",
                                            "role": "src",
                                            "access": "read",
                                            "type": {"expr": expression},
                                        }
                                    ],
                                }
                            ],
                        }
                    ]
                }
            )

        modifiers = [
            {
                "name": "type",
                "kind": "type",
                "presence": "required",
                "domain": "scalar_types",
                "values": ["u32"],
            }
        ]
        self.assertTrue(normalize_with_expr("modifier(type)", modifiers))
        with self.assertRaisesRegex(ValueError, "same_as.*not supported"):
            normalize_with_expr("same_as(src1)", modifiers)
        with self.assertRaisesRegex(ValueError, "unknown modifier"):
            normalize_with_expr("modifier(missing)", modifiers)
        with self.assertRaisesRegex(ValueError, "active type modifier"):
            normalize_with_expr(
                "modifier(flag)",
                [
                    {
                        "name": "flag",
                        "kind": "flag",
                        "presence": "optional",
                    }
                ],
            )

    def test_emit_add_check_end_descriptor_implementation(self) -> None:
        source = emit_check_end_instruction_descriptor_implementation(self.descriptor)

        self.assertTrue(source.startswith("struct AddDescriptorStorage {"))
        self.assertIn(
            "inline static constexpr std::array<std::string_view, 6>", source
        )
        self.assertIn(
            "inline static constexpr std::array<check_end::SyntaxModifierDescriptor, 2>",
            source,
        )
        self.assertIn(
            "inline static constexpr std::array<check_end::SyntaxOperandSlotDescriptor, 3>",
            source,
        )
        self.assertIn('.Opcode_name = "add",', source)
        self.assertIn('.variant_name = "IntegerNoSat",', source)
        self.assertIn('.variant_name = "SatS32",', source)
        self.assertIn('.variant_name = "PackedOptionalSatSm120",', source)
        self.assertIn(
            ".allowed_values = add_integer_no_sat_modifier_1_allowed_values,",
            source,
        )
        self.assertIn(
            ".presence = check_end::PresenceRequirement::Absent,",
            source,
        )
        self.assertIn(
            ".kind = check_end::OperandLayoutKind::Flat,",
            source,
        )
        self.assertNotIn("ResolvedValueKind", source)
        self.assertNotIn(".type_expr", source)
        self.assertIn(
            ".allowed_shapes = check_end::OperandSyntaxShape::Identifier | "
            "check_end::OperandSyntaxShape::Immediate,",
            source,
        )
        self.assertIn(
            "const check_end::SyntaxInstructionDescriptor&\n"
            "Add::get_syntax_descriptor() noexcept {",
            source,
        )
        self.assertTrue(source.endswith("}"))

    def test_generate_private_syntax_descriptor_source(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )

        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "syntax_descriptor.gen.cpp"
            generate_syntax_descriptor_source(
                database,
                output_path=output_path,
            )
            source = output_path.read_text(encoding="utf-8")

        self.assertTrue(
            source.startswith("// Generated by python/scripts/gen_all.py. Do not edit.")
        )
        self.assertNotIn("#pragma once", source)
        self.assertRegex(
            source,
            r"// Generated at: \d{4}-\d{2}-\d{2}T"
            r"\d{2}:\d{2}:\d{2}.*\+00:00",
        )
        self.assertIn(
            '#include "ptx_ir/resolved/ptx_resolved_ir.hpp"',
            source,
        )
        self.assertIn("namespace ptx_frontend::resolved_ir {", source)
        self.assertEqual(source.count("namespace {"), 1)
        self.assertIn("struct AddDescriptorStorage {", source)
        self.assertIn("struct BarDescriptorStorage {", source)
        self.assertIn("Add::get_syntax_descriptor() noexcept", source)


if __name__ == "__main__":
    unittest.main()
