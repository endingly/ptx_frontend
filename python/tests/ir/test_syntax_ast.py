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
from code_gen.gen_syntax_ast_arch import (
    emit_check_end_instruction_descriptor_implementation,
    generate_syntax_descriptor_header,
)
from ir.syntax_ast import from_InstructionSpec
from ir.syntax_ast import (
    ModifierPresence,
    OperandLayoutKind,
    OperandPresence,
    OperandSyntaxShape,
    ResolvedValueKind,
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
        self.assertEqual(
            integer_no_sat.modifiers[1].resolved_value_kind,
            ResolvedValueKind.SCALAR_TYPE,
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
        self.assertEqual(
            [
                (slot.field_id, slot.allowed_syntax_shapes, slot.presence)
                for slot in layout.slots
            ],
            [
                (
                    "dst",
                    OperandSyntaxShape.IDENTIFIER_REF,
                    OperandPresence.REQUIRED,
                ),
                (
                    "src1",
                    OperandSyntaxShape.IDENTIFIER_REF | OperandSyntaxShape.IMMEDIATE,
                    OperandPresence.REQUIRED,
                ),
                (
                    "src2",
                    OperandSyntaxShape.IDENTIFIER_REF | OperandSyntaxShape.IMMEDIATE,
                    OperandPresence.REQUIRED,
                ),
            ],
        )

        self.assertTrue(layout.slots[1].allows(OperandSyntaxShape.IDENTIFIER_REF))
        self.assertTrue(layout.slots[1].allows(OperandSyntaxShape.IMMEDIATE))
        self.assertFalse(layout.slots[1].allows(OperandSyntaxShape.ADDRESS))
        self.assertEqual(
            layout.slots[0].resolved_value_kind, ResolvedValueKind.REGISTER
        )
        self.assertEqual(
            layout.slots[1].resolved_value_kind, ResolvedValueKind.REG_OR_IMM
        )
        self.assertEqual(layout.slots[1].type_expr, "$type")

    def test_emit_add_check_end_descriptor_implementation(self) -> None:
        source = emit_check_end_instruction_descriptor_implementation(self.descriptor)

        self.assertTrue(
            source.startswith(
                "namespace {\n\nstruct AddDescriptorStorage {"
            )
        )
        self.assertIn(
            "inline static constexpr std::array<std::string_view, 6>", source
        )
        self.assertIn(
            "inline static constexpr std::array<check_end::ModifierDescriptor, 2>",
            source,
        )
        self.assertIn(
            "inline static constexpr std::array<check_end::OperandSlotDescriptor, 3>",
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
            ".layout_id = check_end::OperandLayoutKind::Flat,",
            source,
        )
        self.assertIn(
            ".value_kind = check_end::ResolvedValueKind::RegOrImm,",
            source,
        )
        self.assertIn('.type_expr = "$type",', source)
        self.assertIn(
            ".allowed_shapes = check_end::OperandSyntaxShape::Identifier | "
            "check_end::OperandSyntaxShape::Immediate,",
            source,
        )
        self.assertIn(
            "const check_end::InstructionDescriptor&\n"
            "Add::get_inst_descriptor() noexcept {",
            source,
        )
        self.assertTrue(source.endswith("}"))

    def test_generate_private_syntax_descriptor_header(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )

        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "syntax_descriptor.gen.hpp"
            generate_syntax_descriptor_header(
                database,
                output_path=output_path,
            )
            source = output_path.read_text(encoding="utf-8")

        self.assertTrue(
            source.startswith("// Generated by python/scripts/gen_all.py. Do not edit.")
        )
        self.assertIn("#pragma once", source)
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
        self.assertIn("struct AddDescriptorStorage {", source)
        self.assertIn("Add::get_inst_descriptor() noexcept", source)


if __name__ == "__main__":
    unittest.main()
