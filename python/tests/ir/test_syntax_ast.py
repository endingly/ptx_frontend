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

from ptx_frontend.base.utils import generated_at_comment
from ptx_frontend.code_gen.cpp_backend import configure_cpp_backend
from ptx_frontend.code_gen.database import load_codegen_database
from ptx_frontend.code_gen.gen_syntax_ast_arch import (
    emit_check_end_instruction_descriptor_implementation,
    generate_syntax_descriptor_source,
)
from ptx_frontend.code_gen.load_yaml import expand_value_refs
from ptx_frontend.code_gen.model import (
    MbarrierStateTokenForm,
    OperandRegisterWidthPolicy,
    OperandVectorTypePolicy,
)
from ptx_frontend.code_gen.normalize import normalize_instruction_spec
from ptx_frontend.ir.syntax_ast import from_InstructionSpec
from ptx_frontend.ir.syntax_ast import (
    ModifierPresence,
    OperandLayoutKind,
    OperandPresence,
    OperandSyntaxShape,
)


def setUpModule() -> None:
    configure_cpp_backend(REPO_ROOT / "instructions/ptx_cpp_backend_spec/ptx_frontend.yaml")


class SyntaxAstDescriptorBuildTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        # Repository specs remain unchanged.
        # Tests needing mutation load their own fixture.
        cls.database = database
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
        call = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "call"
        )
        cls.call_descriptor = from_InstructionSpec(call)
        shfl = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "shfl"
        )
        cls.shfl_descriptor = from_InstructionSpec(shfl)

    def test_call_uses_fixed_non_flat_group_layouts(self) -> None:
        variant = self.call_descriptor.variants[0]
        self.assertEqual(variant.variant_id, "call_direct")
        self.assertEqual(
            [layout.layout_id for layout in variant.operand_layouts],
            [
                "target",
                "target_input",
                "return_target_input",
                "target_metadata",
                "target_input_metadata",
                "return_target_input_metadata",
            ],
        )
        self.assertEqual(
            [layout.kind for layout in variant.operand_layouts],
            [
                OperandLayoutKind.CALL,
                OperandLayoutKind.CALL,
                OperandLayoutKind.CALL,
                OperandLayoutKind.INDIRECT_CALL,
                OperandLayoutKind.INDIRECT_CALL,
                OperandLayoutKind.INDIRECT_CALL,
            ],
        )
        self.assertEqual(
            [
                [slot.allowed_syntax_shapes for slot in layout.slots]
                for layout in variant.operand_layouts
            ],
            [
                [OperandSyntaxShape.CALL_TARGET],
                [
                    OperandSyntaxShape.CALL_TARGET,
                    OperandSyntaxShape.CALL_PARAMETER_LIST,
                ],
                [
                    OperandSyntaxShape.CALL_PARAMETER_LIST,
                    OperandSyntaxShape.CALL_TARGET,
                    OperandSyntaxShape.CALL_PARAMETER_LIST,
                ],
                [
                    OperandSyntaxShape.CALL_TARGET,
                    OperandSyntaxShape.CALL_TARGET_SET,
                ],
                [
                    OperandSyntaxShape.CALL_TARGET,
                    OperandSyntaxShape.CALL_PARAMETER_LIST,
                    OperandSyntaxShape.CALL_TARGET_SET,
                ],
                [
                    OperandSyntaxShape.CALL_PARAMETER_LIST,
                    OperandSyntaxShape.CALL_TARGET,
                    OperandSyntaxShape.CALL_PARAMETER_LIST,
                    OperandSyntaxShape.CALL_TARGET_SET,
                ],
            ],
        )

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

    def test_control_flow_shapes_have_distinct_descriptor_flags(self) -> None:
        self.assertEqual(OperandSyntaxShape.CALL_PARAMETER_LIST.value, 1 << 6)
        self.assertEqual(OperandSyntaxShape.CALL_TARGET.value, 1 << 7)
        self.assertEqual(OperandSyntaxShape.CALL_TARGET_SET.value, 1 << 8)
        self.assertEqual(OperandSyntaxShape.BRANCH_TARGET.value, 1 << 9)

    def test_register_predicate_pair_uses_dedicated_single_operand_shape(self) -> None:
        variant = self.shfl_descriptor.variants[0]
        self.assertEqual(variant.variant_id, "shfl_sync_idx_b32")
        self.assertEqual(
            variant.operand_layouts[0].slots[0].allowed_syntax_shapes,
            OperandSyntaxShape.REGISTER_PREDICATE_PAIR,
        )

    def test_mov_source_layout_covers_data_and_address_forms(self) -> None:
        database = self.database
        mov = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "mov"
        )
        descriptor = from_InstructionSpec(mov)
        layout = descriptor.variants[0].operand_layouts[0]

        self.assertEqual(
            layout.slots[0].allowed_syntax_shapes,
            OperandSyntaxShape.IDENTIFIER_REF,
        )
        self.assertEqual(
            layout.slots[1].allowed_syntax_shapes,
            OperandSyntaxShape.IDENTIFIER_REF
            | OperandSyntaxShape.IMMEDIATE
            | OperandSyntaxShape.ADDRESS
            | OperandSyntaxShape.VECTOR_MEMBER,
        )
        vector_layout = descriptor.variants[1].operand_layouts[0]
        self.assertEqual(
            [slot.allowed_syntax_shapes for slot in vector_layout.slots],
            [OperandSyntaxShape.IDENTIFIER_REF, OperandSyntaxShape.IDENTIFIER_REF],
        )

    def test_mapa_cluster_source_layout_allows_register_symbol_or_address_forms(self) -> None:
        database = self.database
        mapa = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "mapa"
        )
        descriptor = from_InstructionSpec(mapa)

        shared_layout = descriptor.variants[0].operand_layouts[0]
        self.assertEqual(
            [slot.allowed_syntax_shapes for slot in shared_layout.slots],
            [
                OperandSyntaxShape.IDENTIFIER_REF,
                OperandSyntaxShape.IDENTIFIER_REF | OperandSyntaxShape.ADDRESS,
                OperandSyntaxShape.IDENTIFIER_REF | OperandSyntaxShape.IMMEDIATE,
            ],
        )
        generic_layout = descriptor.variants[1].operand_layouts[0]
        self.assertEqual(
            [slot.allowed_syntax_shapes for slot in generic_layout.slots],
            [
                OperandSyntaxShape.IDENTIFIER_REF,
                OperandSyntaxShape.IDENTIFIER_REF,
                OperandSyntaxShape.IDENTIFIER_REF | OperandSyntaxShape.IMMEDIATE,
            ],
        )

    def test_ld_layout_requires_address_syntax(self) -> None:
        database = self.database
        ld = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "ld"
        )
        descriptor = from_InstructionSpec(ld)
        layout = descriptor.variants[0].operand_layouts[0]

        self.assertEqual(
            layout.slots[0].allowed_syntax_shapes,
            OperandSyntaxShape.IDENTIFIER_REF,
        )
        self.assertEqual(
            layout.slots[1].allowed_syntax_shapes,
            OperandSyntaxShape.ADDRESS,
        )

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

    def test_normalize_call_layout_rejects_non_call_shape(self) -> None:
        raw_spec = {
            "category": "test",
            "codegen_category": "test",
            "instructions": [
                {
                    "opcode": "sample",
                    "variants": [
                        {
                            "name": "sample_call",
                            "availability": {"ptx": "1.0", "sm": 0},
                            "operand_layouts": [
                                {
                                    "name": "bad",
                                    "kind": "call",
                                    "operands": [
                                        {
                                            "name": "target",
                                            "kind": "direct_call_target",
                                            "role": "label",
                                            "access": "control",
                                        },
                                        {
                                            "name": "return_value",
                                            "kind": "call_return_param",
                                            "role": "dst",
                                            "access": "write",
                                        },
                                    ],
                                }
                            ],
                        }
                    ],
                }
            ],
        }
        with self.assertRaisesRegex(ValueError, "call operand layout"):
            normalize_instruction_spec(raw_spec)

    def test_normalize_indirect_call_layout_rejects_missing_metadata(self) -> None:
        raw_spec = {
            "category": "test",
            "codegen_category": "test",
            "instructions": [
                {
                    "opcode": "sample",
                    "variants": [
                        {
                            "name": "sample_indirect_call",
                            "availability": {"ptx": "1.0", "sm": 0},
                            "operand_layouts": [
                                {
                                    "name": "missing_metadata",
                                    "kind": "indirect_call",
                                    "operands": [
                                        {
                                            "name": "target",
                                            "kind": "indirect_call_target",
                                            "role": "label",
                                            "access": "control",
                                        }
                                    ],
                                }
                            ],
                        }
                    ],
                }
            ],
        }
        with self.assertRaisesRegex(ValueError, "indirect call operand layout"):
            normalize_instruction_spec(raw_spec)

    def test_normalize_rejects_call_operands_in_flat_layout(self) -> None:
        raw_spec = {
            "category": "test",
            "codegen_category": "test",
            "instructions": [
                {
                    "opcode": "sample",
                    "variants": [
                        {
                            "name": "sample_flat",
                            "availability": {"ptx": "1.0", "sm": 0},
                            "operand_layouts": [
                                {
                                    "name": "default",
                                    "operands": [
                                        {
                                            "name": "target",
                                            "kind": "direct_call_target",
                                            "role": "label",
                                            "access": "control",
                                        }
                                    ],
                                }
                            ],
                        }
                    ],
                }
            ],
        }
        with self.assertRaisesRegex(ValueError, "require kind 'call'"):
            normalize_instruction_spec(raw_spec)

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

    def test_state_space_expression_requires_active_state_space_modifier(self) -> None:
        def normalize_with_modifier(modifier: dict[str, object]):
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
                                    "modifiers": [modifier],
                                    "operands": [
                                        {
                                            "name": "address",
                                            "kind": "addr",
                                            "role": "addr",
                                            "access": "read",
                                            "state_space": {
                                                "expr": "modifier(state_space)"
                                            },
                                        }
                                    ],
                                }
                            ],
                        }
                    ],
                }
            )

        instruction = normalize_with_modifier(
            {
                "name": "state_space",
                "kind": "state_space",
                "presence": "fixed",
                "domain": "state_spaces",
                "value": "global",
            }
        )[0]
        expression = instruction.variants[0].operand_layouts[0].operands[
            0
        ].state_space_expression
        self.assertIsNotNone(expression)
        self.assertEqual(expression.modifier_name, "state_space")

        with self.assertRaisesRegex(ValueError, "active state-space modifier"):
            normalize_with_modifier(
                {
                    "name": "state_space",
                    "kind": "type",
                    "presence": "fixed",
                    "domain": "scalar_types",
                    "value": "u32",
                }
            )

    def test_operand_state_space_allowlist_normalization(self) -> None:
        def normalize_state_space(state_space: object, kind: str = "addr"):
            instruction = normalize_instruction_spec(
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
                                    "operands": [
                                        {
                                            "name": "address",
                                            "kind": kind,
                                            "role": "addr",
                                            "access": "read",
                                            "state_space": state_space,
                                        }
                                    ],
                                }
                            ],
                        }
                    ],
                }
            )[0]
            return instruction.variants[0].operand_layouts[0].operands[0]

        scalar = normalize_state_space("global")
        self.assertEqual(
            [(entry.value, entry.availability) for entry in scalar.state_space_values],
            [("global", {})],
        )
        self.assertIsNone(scalar.state_space_expression)

        cluster_address = normalize_state_space("shared", kind="cluster_address")
        self.assertEqual(
            [(entry.value, entry.availability)
             for entry in cluster_address.state_space_values],
            [("shared", {})],
        )

        values = normalize_state_space(
            [
                "global",
                {"value": "const", "availability": {"ptx": "3.1"}},
            ]
        ).state_space_values
        self.assertEqual(
            [(entry.value, entry.availability) for entry in values],
            [("global", {}), ("const", {"ptx": "3.1"})],
        )

        with self.assertRaisesRegex(ValueError, "duplicate operand state space"):
            normalize_state_space(
                [
                    "global",
                    {"value": "global", "availability": {"ptx": "2.0"}},
                ]
            )
        with self.assertRaisesRegex(ValueError, "unknown operand state space"):
            normalize_state_space(["missing"])
        with self.assertRaisesRegex(ValueError, "value and availability"):
            normalize_state_space([{"value": "const"}])
        with self.assertRaisesRegex(ValueError, "modifier expression"):
            normalize_state_space(
                {"expr": "modifier(state_space)", "value": "global"}
            )
        for non_address_state_space in (
            "global",
            {"expr": "modifier(state_space)"},
        ):
            with self.subTest(state_space=non_address_state_space):
                with self.assertRaisesRegex(
                    ValueError, "address constraints.*kind 'addr'"
                ):
                    normalize_state_space(non_address_state_space, kind="reg")
        with self.assertRaisesRegex(ValueError, "must not be empty"):
            normalize_state_space([])

    def test_parameter_address_constraint_normalization(self) -> None:
        default_state_space = object()
        default_parameter = object()

        def normalize_parameter(
            modifier: dict[str, object],
            *,
            kind: str = "addr",
            state_space: object = default_state_space,
            parameter: object = default_parameter,
        ):
            if state_space is default_state_space:
                state_space = {"expr": "modifier(state_space)"}
            if parameter is default_parameter:
                parameter = {
                    "direction": "input",
                    "function_availability": {"ptx": "2.0", "sm": 20},
                }
            instruction = normalize_instruction_spec(
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
                                    "modifiers": [modifier],
                                    "operands": [
                                        {
                                            "name": "address",
                                            "kind": kind,
                                            "role": "addr",
                                            "access": "read",
                                            "state_space": state_space,
                                            "parameter": parameter,
                                        }
                                    ],
                                }
                            ],
                        }
                    ],
                }
            )[0]
            return instruction.variants[0].operand_layouts[0].operands[0]

        required_modifier = {
            "name": "state_space",
            "kind": "state_space",
            "presence": "required",
            "domain": "state_spaces",
            "values": ["global", "param"],
        }
        operand = normalize_parameter(required_modifier)
        self.assertEqual(operand.parameter_constraint.direction, "input")
        self.assertEqual(
            operand.parameter_constraint.function_availability,
            {"ptx": "2.0", "sm": 20},
        )

        fixed_modifier = {
            "name": "state_space",
            "kind": "state_space",
            "presence": "fixed",
            "domain": "state_spaces",
            "value": "param",
        }
        self.assertIsNotNone(normalize_parameter(fixed_modifier).parameter_constraint)

        with self.assertRaisesRegex(ValueError, "address constraints.*kind 'addr'"):
            normalize_parameter(required_modifier, kind="reg")
        with self.assertRaisesRegex(ValueError, "requires a state_space modifier"):
            normalize_parameter(required_modifier, state_space=None)
        without_param = dict(required_modifier, values=["global"])
        with self.assertRaisesRegex(ValueError, "allow \\.param"):
            normalize_parameter(without_param)
        with self.assertRaisesRegex(ValueError, "unsupported parameter direction"):
            normalize_parameter(
                required_modifier,
                parameter={
                    "direction": "both",
                    "function_availability": {"ptx": "2.0"},
                },
            )
        with self.assertRaisesRegex(ValueError, "must contain direction"):
            normalize_parameter(
                required_modifier,
                parameter={"direction": "input"},
            )

    def test_register_width_policy_normalization(self) -> None:
        default_type = object()

        def normalize_width(
            *,
            kind: str = "reg",
            operand_type: object = default_type,
            register_width: object = "equal_or_wider",
        ):
            if operand_type is default_type:
                operand_type = {"expr": "modifier(type)"}
            instruction = normalize_instruction_spec(
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
                                    "modifiers": [
                                        {
                                            "name": "type",
                                            "kind": "type",
                                            "domain": "scalar_types",
                                            "presence": "fixed",
                                            "value": "u32",
                                        }
                                    ],
                                    "operands": [
                                        {
                                            "name": "value",
                                            "kind": kind,
                                            "role": "src",
                                            "access": "read",
                                            "type": operand_type,
                                            "register_width": register_width,
                                        }
                                    ],
                                }
                            ],
                        }
                    ],
                }
            )[0]
            return instruction.variants[0].operand_layouts[0].operands[0]

        operand = normalize_width()
        self.assertEqual(
            operand.register_width_policy,
            OperandRegisterWidthPolicy.EQUAL_OR_WIDER,
        )
        self.assertEqual(
            normalize_width(register_width="same_width").register_width_policy,
            OperandRegisterWidthPolicy.SAME_WIDTH,
        )
        with self.assertRaisesRegex(ValueError, "only valid for kind 'reg'"):
            normalize_width(kind="reg_or_imm")
        with self.assertRaisesRegex(ValueError, "requires a type expression"):
            normalize_width(operand_type=None)
        with self.assertRaisesRegex(ValueError, "unsupported register_width"):
            normalize_width(register_width="wider")

    def test_address_alignment_constraint_normalization_rejects_invalid_links(self) -> None:
        def normalize_constraint(
            constraint: object, *, operand_kind: str = "addr"
        ) -> None:
            normalize_instruction_spec(
                {
                    "category": "test",
                    "codegen_category": "test",
                    "instructions": [
                        {
                            "opcode": "sample",
                            "variants": [
                                {
                                    "name": "sample_alignment",
                                    "availability": {"ptx": "1.0"},
                                    "modifiers": [
                                        {
                                            "name": "type",
                                            "kind": "type",
                                            "domain": "scalar_types",
                                            "presence": "fixed",
                                            "value": "u32",
                                        },
                                        {
                                            "name": "vector",
                                            "kind": "vector",
                                            "domain": "vector_arities",
                                            "presence": "fixed",
                                            "value": "v2",
                                        },
                                    ],
                                    "operands": [
                                        {
                                            "name": "address",
                                            "kind": operand_kind,
                                            "role": "addr",
                                            "access": "read",
                                        }
                                    ],
                                    "constraints": (
                                        constraint
                                        if isinstance(constraint, list)
                                        else [constraint]
                                    ),
                                }
                            ],
                        }
                    ],
                }
            )

        valid = {
            "kind": "address_alignment",
            "address_operand": "address",
            "type_modifier": "type",
        }
        cases = (
            ({"kind": "address_alignment", "address_operand": "address"},
             "requires exactly one"),
            ({**valid, "type_modifier": "missing"}, "inactive modifier"),
            ({**valid, "type_modifier": "vector"}, "must name a 'type'"),
            (valid, "kind 'addr'"),
            ([valid, valid], "at most one constraint"),
        )
        for constraint, message in cases:
            with self.subTest(message=message):
                with self.assertRaisesRegex(ValueError, message):
                    normalize_constraint(
                        constraint,
                        operand_kind="reg" if message == "kind 'addr'" else "addr",
                    )

    def test_address_alignment_requires_one_address_in_each_layout(self) -> None:
        def normalize_layouts(layouts: list[list[dict[str, object]]]) -> None:
            normalize_instruction_spec(
                {
                    "category": "test",
                    "codegen_category": "test",
                    "instructions": [
                        {
                            "opcode": "sample",
                            "variants": [
                                {
                                    "name": "sample_alignment_layouts",
                                    "availability": {"ptx": "1.0"},
                                    "modifiers": [],
                                    "operand_layouts": [
                                        {"name": f"layout_{index}", "operands": operands}
                                        for index, operands in enumerate(layouts)
                                    ],
                                    "constraints": [
                                        {
                                            "kind": "address_alignment",
                                            "address_operand": "address",
                                            "alignment": 8,
                                        }
                                    ],
                                }
                            ],
                        }
                    ],
                }
            )

        address = {"name": "address", "kind": "addr", "role": "addr", "access": "read"}
        count = {"name": "count", "kind": "imm", "role": "src", "access": "read", "type": "u32"}
        normalize_layouts([[address], [address, count]])
        for invalid_layouts in (
            [[address], [count]],
            [[address], [address, address]],
            [[address], [{**address, "kind": "reg"}, count]],
        ):
            with self.subTest(layouts=invalid_layouts):
                with self.assertRaisesRegex(ValueError, "kind 'addr' operand"):
                    normalize_layouts(invalid_layouts)

    def test_immediate_value_constraint_normalization(self) -> None:
        def normalize_constraint(constraint: object, *, operand_kind: str = "imm") -> None:
            normalize_instruction_spec(
                {
                    "category": "test",
                    "codegen_category": "test",
                    "instructions": [
                        {
                            "opcode": "sample",
                            "variants": [
                                {
                                    "name": "sample_immediate",
                                    "availability": {"ptx": "1.0"},
                                    "operands": [
                                        {
                                            "name": "size",
                                            "kind": operand_kind,
                                            "role": "src",
                                            "access": "read",
                                            "type": "u32",
                                        }
                                    ],
                                    "constraints": [constraint],
                                }
                            ],
                        }
                    ],
                }
            )

        normalize_constraint(
            {"kind": "immediate_value", "operand": "size", "values": [4, 8, 16]}
        )
        for constraint, message, kind in (
            ({"kind": "immediate_value", "operand": "missing", "values": [4]}, "kind 'imm'", "imm"),
            ({"kind": "immediate_value", "operand": "size", "values": [4, 4]}, "unique", "imm"),
            ({"kind": "immediate_value", "operand": "size", "values": [4.0]}, "uint64 integer", "imm"),
            ({"kind": "immediate_value", "operand": "size", "values": [4]}, "kind 'imm'", "reg"),
        ):
            with self.subTest(message=message):
                with self.assertRaisesRegex(ValueError, message):
                    normalize_constraint(constraint, operand_kind=kind)

    def test_immediate_multiple_of_constraint_normalization(self) -> None:
        def normalize_constraint(constraint: object, *, operand_kind: str = "imm") -> None:
            normalize_instruction_spec(
                {
                    "category": "test",
                    "codegen_category": "test",
                    "instructions": [
                        {
                            "opcode": "sample",
                            "variants": [
                                {
                                    "name": "sample_immediate",
                                    "availability": {"ptx": "1.0"},
                                    "operands": [
                                        {
                                            "name": "count",
                                            "kind": operand_kind,
                                            "role": "src",
                                            "access": "read",
                                            "type": "u32",
                                        }
                                    ],
                                    "constraints": [constraint],
                                }
                            ],
                        }
                    ],
                }
            )

        normalize_constraint(
            {"kind": "immediate_multiple_of", "operand": "count", "divisor": 8}
        )
        for constraint, message, kind in (
            ({"kind": "immediate_multiple_of", "operand": "missing", "divisor": 8}, "kind 'imm'", "imm"),
            ({"kind": "immediate_multiple_of", "operand": "count", "divisor": 0}, "positive integer", "imm"),
            ({"kind": "immediate_multiple_of", "operand": "count", "divisor": 8.0}, "positive integer", "imm"),
            ({"kind": "immediate_multiple_of", "operand": "count", "divisor": 8}, "kind 'imm'", "reg"),
        ):
            with self.subTest(message=message):
                with self.assertRaisesRegex(ValueError, message):
                    normalize_constraint(constraint, operand_kind=kind)

    def test_immediate_constraints_enforce_uint64_bounds(self) -> None:
        def normalize_constraints(constraints: list[dict[str, object]]) -> None:
            normalize_instruction_spec(
                {
                    "category": "test",
                    "codegen_category": "test",
                    "instructions": [{"opcode": "sample", "variants": [{
                        "name": "sample_immediate",
                        "availability": {"ptx": "1.0"},
                        "operands": [{
                            "name": "count", "kind": "imm", "role": "src",
                            "access": "read", "type": "u64",
                        }],
                        "constraints": constraints,
                    }]}],
                }
            )

        uint64_max = 2**64 - 1
        normalize_constraints([
            {"kind": "immediate_value", "operand": "count",
             "values": [2**63, uint64_max]},
            {"kind": "immediate_range", "operand": "count",
             "minimum": 2**63, "maximum": uint64_max},
            {"kind": "immediate_multiple_of", "operand": "count",
             "divisor": uint64_max},
        ])

        for constraint, field in (
            ({"kind": "immediate_value", "operand": "count",
              "values": [2**64]}, "values[0]"),
            ({"kind": "immediate_range", "operand": "count",
              "minimum": 2**64}, "minimum"),
            ({"kind": "immediate_range", "operand": "count",
              "minimum": 0, "maximum": 2**64}, "maximum"),
            ({"kind": "immediate_multiple_of", "operand": "count",
              "divisor": 2**64}, "divisor"),
        ):
            with self.subTest(field=field):
                with self.assertRaisesRegex(
                    ValueError, rf"constraint field '{re.escape(field)}'.*{2**64}"
                ):
                    normalize_constraints([constraint])

        with self.assertRaisesRegex(ValueError, "positive integer"):
            normalize_constraints([{
                "kind": "immediate_multiple_of", "operand": "count", "divisor": 0,
            }])
        with self.assertRaisesRegex(ValueError, "maximum >= minimum"):
            normalize_constraints([{
                "kind": "immediate_range", "operand": "count",
                "minimum": 1, "maximum": 0,
            }])

    def _normalize_multilayout_immediate_constraint(
        self,
        constraint: object,
        operand_name: str,
        other_layout_name: str,
        other_operands: list[dict[str, str]],
    ) -> None:
        normalize_instruction_spec(
            {
                "category": "test",
                "codegen_category": "test",
                "instructions": [{"opcode": "sample", "variants": [{
                    "name": "sample_immediate",
                    "availability": {"ptx": "1.0"},
                    "operand_layouts": [
                        {"name": "immediate", "operands": [
                            {"name": operand_name, "kind": "imm"},
                        ]},
                        {"name": other_layout_name, "operands": other_operands},
                    ],
                    "constraints": [constraint],
                }]}],
            }
        )

    def test_immediate_constraints_allow_a_missing_layout_operand(self) -> None:
        for kind, constraint, operand_name in (
            ("immediate_value", {
                "kind": "immediate_value", "operand": "size", "values": [4],
            }, "size"),
            ("immediate_range", {
                "kind": "immediate_range", "operand": "count", "minimum": 1,
            }, "count"),
            ("immediate_multiple_of", {
                "kind": "immediate_multiple_of", "operand": "stride", "divisor": 4,
            }, "stride"),
        ):
            with self.subTest(kind=kind):
                self._normalize_multilayout_immediate_constraint(
                    constraint,
                    operand_name,
                    "missing",
                    [{"name": "dst", "kind": "reg"}],
                )

    def test_immediate_constraint_rejects_unknown_operand(self) -> None:
        with self.assertRaisesRegex(
            ValueError, r"immediate_range operand 'missing'.*at least one operand layout"
        ):
            normalize_instruction_spec(
                {
                    "category": "test",
                    "codegen_category": "test",
                    "instructions": [{"opcode": "sample", "variants": [{
                        "name": "sample_immediate",
                        "availability": {"ptx": "1.0"},
                        "operand_layouts": [
                            {"name": "first", "operands": [
                                {"name": "count", "kind": "imm"},
                            ]},
                            {"name": "second", "operands": [
                                {"name": "dst", "kind": "reg"},
                            ]},
                        ],
                        "constraints": [{
                            "kind": "immediate_range", "operand": "missing",
                            "minimum": 0,
                        }],
                    }]}],
                }
            )

    def test_immediate_constraints_name_non_immediate_layout_and_kind(self) -> None:
        for kind, constraint, operand_name in (
            ("immediate_value", {
                "kind": "immediate_value", "operand": "size", "values": [4],
            }, "size"),
            ("immediate_range", {
                "kind": "immediate_range", "operand": "count", "minimum": 1,
            }, "count"),
            ("immediate_multiple_of", {
                "kind": "immediate_multiple_of", "operand": "stride", "divisor": 4,
            }, "stride"),
        ):
            with self.subTest(kind=kind):
                with self.assertRaisesRegex(
                    ValueError,
                    rf"{kind} operand {operand_name!r}.*operand layout "
                    r"'register'.*kind 'imm'.*'reg'",
                ):
                    self._normalize_multilayout_immediate_constraint(
                        constraint,
                        operand_name,
                        "register",
                        [
                            {"name": "dst", "kind": "reg"},
                            {"name": operand_name, "kind": "reg"},
                        ],
                    )

    def test_immediate_constraints_accept_immediates_in_every_layout(self) -> None:
        for kind, constraint, operand_name in (
            ("immediate_value", {
                "kind": "immediate_value", "operand": "size", "values": [4],
            }, "size"),
            ("immediate_range", {
                "kind": "immediate_range", "operand": "count", "minimum": 1,
            }, "count"),
            ("immediate_multiple_of", {
                "kind": "immediate_multiple_of", "operand": "stride", "divisor": 4,
            }, "stride"),
        ):
            with self.subTest(kind=kind):
                self._normalize_multilayout_immediate_constraint(
                    constraint,
                    operand_name,
                    "with_dst",
                    [
                        {"name": "dst", "kind": "reg"},
                        {"name": operand_name, "kind": "imm"},
                    ],
                )

    def test_immediate_range_alone_accepts_reg_or_imm(self) -> None:
        def normalize_constraint(constraint: dict[str, object]) -> None:
            normalize_instruction_spec(
                {
                    "category": "test",
                    "codegen_category": "test",
                    "instructions": [{"opcode": "sample", "variants": [{
                        "name": "sample_count",
                        "availability": {"ptx": "1.0"},
                        "operands": [{
                            "name": "count", "kind": "reg_or_imm",
                            "role": "src", "access": "read", "type": "u32",
                        }],
                        "constraints": [constraint],
                    }]}],
                }
            )

        normalize_constraint(
            {"kind": "immediate_range", "operand": "count", "minimum": 1}
        )
        for constraint in (
            {"kind": "immediate_value", "operand": "count", "values": [1]},
            {"kind": "immediate_multiple_of", "operand": "count", "divisor": 1},
        ):
            with self.assertRaisesRegex(ValueError, r"kind 'imm'.*'reg_or_imm'"):
                normalize_constraint(constraint)

    def test_register_vector_arity_expression_normalization(self) -> None:
        instruction = normalize_instruction_spec(
            {
                "category": "test",
                "codegen_category": "test",
                "instructions": [
                    {
                        "opcode": "sample",
                        "variants": [
                            {
                                "name": "sample_vector",
                                "availability": {"ptx": "1.0"},
                                "modifiers": [
                                    {
                                        "name": "vector",
                                        "kind": "vector",
                                        "domain": "vector_arities",
                                        "presence": "required",
                                        "values": ["v2", "v4"],
                                    },
                                    {
                                        "name": "type",
                                        "kind": "type",
                                        "domain": "scalar_types",
                                        "presence": "required",
                                        "values": ["u32"],
                                    },
                                ],
                                "operands": [
                                    {
                                        "name": "dst",
                                        "kind": "reg_vector",
                                        "role": "dst",
                                        "access": "write",
                                        "type": {"expr": "modifier(type)"},
                                        "register_width": "equal_or_wider",
                                        "vector": {
                                            "kind": "vector",
                                            "arity": {
                                                "expr": "modifier(vector)",
                                            },
                                            "type_policy": "element",
                                        },
                                    }
                                ],
                            }
                        ],
                    }
                ],
            }
        )[0]
        operand = instruction.variants[0].operand_layouts[0].operands[0]
        self.assertEqual(operand.vector_arities, ())
        self.assertEqual(operand.vector_arity_expression.modifier_name, "vector")
        self.assertEqual(
            operand.vector_type_policy,
            OperandVectorTypePolicy.ELEMENT,
        )

        with self.assertRaisesRegex(ValueError, "use modifier"):
            normalize_instruction_spec(
                {
                    "category": "test",
                    "codegen_category": "test",
                    "instructions": [
                        {
                            "opcode": "bad",
                            "variants": [
                                {
                                    "name": "bad_vector",
                                    "availability": {"ptx": "1.0"},
                                    "modifiers": [],
                                    "operands": [
                                        {
                                            "name": "dst",
                                            "kind": "reg_vector",
                                            "role": "dst",
                                            "access": "write",
                                            "vector": {
                                                "kind": "vector",
                                                "arity": {"expr": "v2"},
                                            },
                                        }
                                    ],
                                }
                            ],
                        }
                    ],
                }
            )

        with self.assertRaisesRegex(TypeError, "vector\\.allow_sink"):
            normalize_instruction_spec(
                {
                    "category": "test",
                    "codegen_category": "test",
                    "instructions": [
                        {
                            "opcode": "bad_sink_type",
                            "variants": [
                                {
                                    "name": "bad_sink_type",
                                    "availability": {"ptx": "1.0"},
                                    "modifiers": [],
                                    "operands": [
                                        {
                                            "name": "dst",
                                            "kind": "reg_vector",
                                            "role": "dst",
                                            "access": "write",
                                            "type": {"expr": "modifier(type)"},
                                            "register_width": "equal_or_wider",
                                            "vector": {
                                                "kind": "vector",
                                                "arity": 2,
                                                "allow_sink": 1,
                                            },
                                        }
                                    ],
                                }
                            ],
                        }
                    ],
                }
            )

    def test_destination_sink_is_shfl_dest_specific(self) -> None:
        def normalize_operand(
            kind: str, role: str = "dst", access: str = "write", **extra: object
        ):
            return normalize_instruction_spec(
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
                                    "modifiers": [],
                                    "operands": [
                                        {
                                            "name": "dst",
                                            "kind": kind,
                                            "role": role,
                                            "access": access,
                                            "type": "u32",
                                            **extra,
                                        }
                                    ],
                                }
                            ],
                        }
                    ],
                }
            )[0].variants[0].operand_layouts[0].operands[0]

        self.assertFalse(normalize_operand("shfl_dest").allow_destination_sink)
        self.assertTrue(
            normalize_operand("shfl_dest", allow_destination_sink=True)
            .allow_destination_sink
        )
        self.assertFalse(normalize_operand("shfl_dest").allow_predicate_sink)
        self.assertTrue(
            normalize_operand("shfl_dest", allow_predicate_sink=True)
            .allow_predicate_sink
        )
        with self.assertRaisesRegex(ValueError, "allow_destination_sink.*shfl_dest"):
            normalize_operand("reg", allow_destination_sink=False)
        with self.assertRaisesRegex(TypeError, "allow_destination_sink"):
            normalize_operand("shfl_dest", allow_destination_sink=1)
        with self.assertRaisesRegex(ValueError, "allow_predicate_sink.*shfl_dest"):
            normalize_operand("reg", allow_predicate_sink=False)
        with self.assertRaisesRegex(TypeError, "allow_predicate_sink"):
            normalize_operand("shfl_dest", allow_predicate_sink=1)
        self.assertEqual(normalize_operand("reg_or_sink").kind, "reg_or_sink")
        with self.assertRaisesRegex(ValueError, "reg_or_sink.*write destination"):
            normalize_operand("reg_or_sink", role="src")
        with self.assertRaisesRegex(ValueError, "reg_or_sink.*write destination"):
            normalize_operand("reg_or_sink", access="read")

    def test_mbarrier_state_token_sink_policy_is_destination_specific_and_gated(self) -> None:
        def normalize_token(**extra: object):
            return normalize_instruction_spec(
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
                                    "modifiers": [],
                                    "operands": [
                                        {
                                            "name": "state",
                                            "kind": "mbarrier_state_token",
                                            "role": "dst",
                                            "access": "write",
                                            "type": "b64",
                                            **extra,
                                        }
                                    ],
                                }
                            ],
                        }
                    ],
                }
            )[0].variants[0].operand_layouts[0].operands[0]

        self.assertEqual(
            normalize_token().mbarrier_state_token_form,
            MbarrierStateTokenForm.REGISTER,
        )
        self.assertEqual(
            normalize_token(
                mbarrier_state_token_form="register_or_sink",
                sink_availability={"ptx": "7.1"},
            ).mbarrier_state_token_form,
            MbarrierStateTokenForm.REGISTER_OR_SINK,
        )
        with self.assertRaisesRegex(ValueError, "requires sink_availability"):
            normalize_token(mbarrier_state_token_form="sink")
        with self.assertRaisesRegex(ValueError, "register-only.*sink_availability"):
            normalize_token(sink_availability={"ptx": "7.1"})
        with self.assertRaisesRegex(ValueError, "only valid"):
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
                                    "availability": {"ptx": "1.0"},
                                    "modifiers": [],
                                    "operands": [
                                        {
                                            "name": "dst",
                                            "kind": "reg",
                                            "role": "dst",
                                            "access": "write",
                                            "type": "b64",
                                            "mbarrier_state_token_form": "sink",
                                            "sink_availability": {"ptx": "7.1"},
                                        }
                                    ],
                                }
                            ],
                        }
                    ],
                }
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
        with self.assertRaisesRegex(
            ValueError, "cache sentinel 'unspecified' is not a syntax value"
        ):
            normalize_modifier_entry(
                {
                    "name": "cache",
                    "kind": "cache",
                    "presence": "optional",
                    "domain": "cache_operators",
                    "values": ["unspecified", "ca"],
                    "default": "unspecified",
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
        database = self.database

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
            '#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>',
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
