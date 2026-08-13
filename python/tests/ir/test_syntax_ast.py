from __future__ import annotations

import os
import re
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

REPO_ROOT = Path(__file__).resolve().parents[3]
PYTHON_ROOT = REPO_ROOT / "python"

if str(PYTHON_ROOT) not in sys.path:
    sys.path.insert(0, str(PYTHON_ROOT))

from base.utils import generated_at_comment
from code_gen.database import load_codegen_database
from code_gen.gen_syntax_ast_arch import (
    emit_check_end_instruction_descriptor_implementation,
    generate_syntax_descriptor_source,
)
from code_gen.load_yaml import expand_value_refs
from code_gen.normalize import normalize_instruction_spec
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
        sub = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "sub"
        )
        cls.descriptor = from_InstructionSpec(add)
        cls.sub_descriptor = from_InstructionSpec(sub)

    def test_add_variants_and_modifier_constraints(self) -> None:
        self.assertEqual(self.descriptor.opcode, "add")
        self.assertEqual(
            [variant.variant_id for variant in self.descriptor.variants],
            [
                "add_float_f32",
                "add_float_f32x2",
                "add_float_f64",
                "add_half",
                "add_bfloat",
                "add_mixed_f32",
                "add_integer_no_sat",
                "add_sat",
                "add_packed_optional_sat",
            ],
        )

        variants = {
            variant.variant_id: variant for variant in self.descriptor.variants
        }
        integer_no_sat = variants["add_integer_no_sat"]
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
                    (
                        ".u16",
                        ".u32",
                        ".u64",
                        ".s16",
                        ".s32",
                        ".s64",
                        ".u16x2",
                        ".s16x2",
                    ),
                ),
            ],
        )
        sat = variants["add_sat"]
        self.assertEqual(
            [
                (modifier.kind_id, modifier.presence, modifier.allowed_spellings)
                for modifier in sat.modifiers
            ],
            [
                ("sat", ModifierPresence.REQUIRED, (".sat",)),
                (
                    "type",
                    ModifierPresence.REQUIRED,
                    (".s32", ".u16x2", ".s16x2", ".u32"),
                ),
            ],
        )

        f32 = variants["add_float_f32"]
        self.assertEqual(
            [
                (modifier.kind_id, modifier.presence, modifier.allowed_spellings)
                for modifier in f32.modifiers
            ],
            [
                (
                    "rounding",
                    ModifierPresence.OPTIONAL,
                    (".rn", ".rz", ".rm", ".rp"),
                ),
                ("ftz", ModifierPresence.OPTIONAL, (".ftz",)),
                ("sat", ModifierPresence.OPTIONAL, (".sat",)),
                ("type", ModifierPresence.REQUIRED, (".f32",)),
            ],
        )

        mixed = variants["add_mixed_f32"]
        self.assertEqual(
            [
                (modifier.kind_id, modifier.presence, modifier.allowed_spellings)
                for modifier in mixed.modifiers
            ],
            [
                (
                    "rounding",
                    ModifierPresence.OPTIONAL,
                    (".rn", ".rz", ".rm", ".rp"),
                ),
                ("result_type", ModifierPresence.REQUIRED, (".f32",)),
                ("input_type", ModifierPresence.REQUIRED, (".f16", ".bf16")),
                ("ftz", ModifierPresence.ABSENT, ()),
                ("sat", ModifierPresence.OPTIONAL, (".sat",)),
            ],
        )

    def test_add_binary_flat_operand_layout(self) -> None:
        variant = next(
            variant
            for variant in self.descriptor.variants
            if variant.variant_id == "add_integer_no_sat"
        )
        layout = variant.operand_layouts[0]

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

    def test_sub_variants_and_modifier_constraints(self) -> None:
        self.assertEqual(self.sub_descriptor.opcode, "sub")
        self.assertEqual(
            [variant.variant_id for variant in self.sub_descriptor.variants],
            [
                "sub_float_f32",
                "sub_float_f32x2",
                "sub_float_f64",
                "sub_half",
                "sub_bfloat",
                "sub_mixed_f32",
                "sub_integer_no_sat",
                "sub_optional_sat",
            ],
        )

        variants = {
            variant.variant_id: variant
            for variant in self.sub_descriptor.variants
        }
        integer = variants["sub_integer_no_sat"]
        self.assertEqual(
            [
                (modifier.kind_id, modifier.presence, modifier.allowed_spellings)
                for modifier in integer.modifiers
            ],
            [
                ("sat", ModifierPresence.ABSENT, ()),
                (
                    "type",
                    ModifierPresence.REQUIRED,
                    (".u16", ".u32", ".u64", ".s16", ".s64"),
                ),
            ],
        )
        optional_sat = variants["sub_optional_sat"]
        self.assertEqual(
            [
                (modifier.kind_id, modifier.presence, modifier.allowed_spellings)
                for modifier in optional_sat.modifiers
            ],
            [
                ("sat", ModifierPresence.OPTIONAL, (".sat",)),
                (
                    "type",
                    ModifierPresence.REQUIRED,
                    (".s32", ".u8x4", ".s8x4"),
                ),
            ],
        )
        mixed = variants["sub_mixed_f32"]
        self.assertEqual(
            [
                (slot.allowed_syntax_shapes, slot.presence)
                for slot in mixed.operand_layouts[0].slots
            ],
            [
                (OperandSyntaxShape.IDENTIFIER_REF, OperandPresence.REQUIRED),
                (OperandSyntaxShape.IDENTIFIER_REF, OperandPresence.REQUIRED),
                (OperandSyntaxShape.IDENTIFIER_REF, OperandPresence.REQUIRED),
            ],
        )

    def test_normalize_explicit_operand_layouts(self) -> None:
        raw_spec = {
            "category": "test",
            "codegen_category": "test",
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
            "category": "test",
            "codegen_category": "test",
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
            "category": "test",
            "codegen_category": "test",
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
        with self.assertRaisesRegex(ValueError, "cyclic value-set reference"):
            expand_value_refs(
                ["$first"],
                {"first": ["$second"], "second": ["$first"]},
            )

    def test_rejects_bare_operand_pattern_name(self) -> None:
        with self.assertRaisesRegex(ValueError, "must use the '\\$name' form"):
            normalize_instruction_spec(
                {
                    "category": "test",
                    "codegen_category": "test",
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
                    "category": "test",
                    "codegen_category": "test",
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
                        "default": False,
                    }
                ],
            )

    def test_optional_modifier_requires_typed_default(self) -> None:
        def normalize_modifier_entry(modifier: dict[str, object]) -> None:
            normalize_instruction_spec(
                {
                    "category": "test",
                    "codegen_category": "test",
                    "instructions": [
                        {
                            "opcode": "sample",
                            "variants": [
                                {
                                    "name": "sample_default",
                                    "availability": {"ptx": "1.0", "sm": 0},
                                    "modifiers": [modifier],
                                    "operands": [],
                                }
                            ],
                        }
                    ]
                }
            )

        with self.assertRaisesRegex(ValueError, "must define default"):
            normalize_modifier_entry(
                {
                    "name": "sat",
                    "kind": "flag",
                    "presence": "optional",
                    "token": ".sat",
                }
            )
        with self.assertRaisesRegex(ValueError, "boolean default"):
            normalize_modifier_entry(
                {
                    "name": "sat",
                    "kind": "flag",
                    "presence": "optional",
                    "token": ".sat",
                    "default": "false",
                }
            )
        with self.assertRaisesRegex(ValueError, "outside its allowed values"):
            normalize_modifier_entry(
                {
                    "name": "type",
                    "kind": "type",
                    "presence": "optional",
                    "values": ["u32", "u64"],
                    "default": "s32",
                }
            )

    def test_emit_add_check_end_descriptor_implementation(self) -> None:
        source = emit_check_end_instruction_descriptor_implementation(self.descriptor)

        self.assertTrue(source.startswith("struct AddDescriptorStorage {"))
        self.assertIn(
            "inline static constexpr std::array<std::string_view, 8>", source
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
        self.assertIn('.variant_name = "Sat",', source)
        self.assertIn('.variant_name = "PackedOptionalSat",', source)
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
            with patch.dict(os.environ, {}, clear=False):
                os.environ.pop("SOURCE_DATE_EPOCH", None)
                generate_syntax_descriptor_source(
                    database,
                    output_path=output_path,
                )
            source = output_path.read_text(encoding="utf-8")

        self.assertTrue(
            source.startswith("// Generated by python/scripts/gen_all.py. Do not edit.")
        )
        self.assertNotIn("#pragma once", source)
        self.assertIn(
            "// Generated at: omitted "
            "(set SOURCE_DATE_EPOCH for a reproducible timestamp)",
            source,
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

    def test_generation_timestamp_uses_source_date_epoch(self) -> None:
        with patch.dict(os.environ, {"SOURCE_DATE_EPOCH": "0"}):
            self.assertEqual(
                generated_at_comment(),
                "// Generated at: 1970-01-01T00:00:00+00:00",
            )


if __name__ == "__main__":
    unittest.main()
