from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[3]
PYTHON_ROOT = REPO_ROOT / "python"

if str(PYTHON_ROOT) not in sys.path:
    sys.path.insert(0, str(PYTHON_ROOT))


from code_gen.database import load_codegen_database
from code_gen.gen_resolved_descriptor import generate_resolved_descriptor_source
from code_gen.gen_resolved_checker_descriptor import (
    generate_resolved_checker_descriptor_source,
)
from code_gen.gen_resolved_ir import generate_resolved_ir_header
from ir.resolved_ir import (
    ResolvedFieldOrigin,
    ResolvedFieldStorage,
    ResolvedOperandAccess,
    ResolvedOperandRole,
    ResolvedOperandShape,
    ResolvedValueKind,
    from_instruction_spec,
)


class ResolvedIrBuildTest(unittest.TestCase):
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
        cls.instruction = from_instruction_spec(add)

    def test_add_variant_names(self) -> None:
        self.assertEqual(self.instruction.opcode, "add")
        self.assertEqual(self.instruction.cpp_name, "Add")
        self.assertEqual(
            [variant.cpp_name for variant in self.instruction.variants],
            [
                "IntegerNoSat",
                "SatS32",
                "SimdNoSatSm90",
                "PackedOptionalSatSm120",
                "SatSm120",
            ],
        )
    def test_add_resolved_variant_fields(self) -> None:
        variants = {variant.cpp_name: variant for variant in self.instruction.variants}

        self.assertEqual(
            [
                (field.name, field.cpp_type, field.origin, field.type_expr)
                for field in variants["IntegerNoSat"].fields
            ],
            [
                ("type", "WithLocs<ScalarType>", ResolvedFieldOrigin.MODIFIER, None),
                ("dst", "WithLocs<ResolvedRegisterId>", ResolvedFieldOrigin.OPERAND, "$type"),
                ("src1", "WithLocs<RegOrImm>", ResolvedFieldOrigin.OPERAND, "$type"),
                ("src2", "WithLocs<RegOrImm>", ResolvedFieldOrigin.OPERAND, "$type"),
            ],
        )
        self.assertEqual(
            [field.name for field in variants["SatS32"].fields],
            ["saturate", "type", "dst", "src1", "src2"],
        )
        self.assertEqual(
            [field.storage for field in variants["SatS32"].fields[:2]],
            [
                ResolvedFieldStorage.STATIC_CONSTANT,
                ResolvedFieldStorage.STATIC_CONSTANT,
            ],
        )
        self.assertEqual(
            [field.cpp_constant_expr for field in variants["SatS32"].fields[:2]],
            ["true", "ScalarType::S32"],
        )
        self.assertEqual(
            [
                (binding.source_kind_id, binding.target_field_id)
                for binding in variants["SatS32"].modifier_bindings
            ],
            [("sat", "saturate"), ("type", "type")],
        )
        self.assertEqual(
            [
                (field.name, field.cpp_type, field.origin)
                for field in variants["PackedOptionalSatSm120"].fields
            ],
            [
                ("saturate", "WithLocs<bool>", ResolvedFieldOrigin.MODIFIER),
                ("type", "WithLocs<ScalarType>", ResolvedFieldOrigin.MODIFIER),
                ("dst", "WithLocs<ResolvedRegisterId>", ResolvedFieldOrigin.OPERAND),
                ("src1", "WithLocs<RegOrImm>", ResolvedFieldOrigin.OPERAND),
                ("src2", "WithLocs<RegOrImm>", ResolvedFieldOrigin.OPERAND),
            ],
        )
        self.assertEqual(
            [field.value_kind for field in variants["IntegerNoSat"].fields],
            [
                ResolvedValueKind.SCALAR_TYPE,
                ResolvedValueKind.REGISTER,
                ResolvedValueKind.REG_OR_IMM,
                ResolvedValueKind.REG_OR_IMM,
            ],
        )
        self.assertEqual(
            [
                (binding.source_kind_id, binding.target_field_id)
                for binding in variants["PackedOptionalSatSm120"].modifier_bindings
            ],
            [("sat", "saturate"), ("type", "type")],
        )
        self.assertEqual(
            [
                (
                    binding.target_field_id,
                    binding.type_expr,
                    binding.role,
                    binding.access,
                    binding.allowed_shapes,
                )
                for binding in variants["IntegerNoSat"].operand_layouts[0].bindings
            ],
            [
                (
                    "dst",
                    "$type",
                    ResolvedOperandRole.DESTINATION,
                    ResolvedOperandAccess.WRITE,
                    (ResolvedOperandShape.REGISTER,),
                ),
                (
                    "src1",
                    "$type",
                    ResolvedOperandRole.SOURCE,
                    ResolvedOperandAccess.READ,
                    (
                        ResolvedOperandShape.REGISTER,
                        ResolvedOperandShape.IMMEDIATE,
                    ),
                ),
                (
                    "src2",
                    "$type",
                    ResolvedOperandRole.SOURCE,
                    ResolvedOperandAccess.READ,
                    (
                        ResolvedOperandShape.REGISTER,
                        ResolvedOperandShape.IMMEDIATE,
                    ),
                ),
            ],
        )

    def test_generate_resolved_ir_header(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )

        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir.gen.hpp"
            generate_resolved_ir_header(database, output_path=output_path)
            source = output_path.read_text(encoding="utf-8")

        self.assertTrue(
            source.startswith("// Generated by python/scripts/gen_all.py. Do not edit.")
        )
        self.assertIn("// Generated at: ", source)
        self.assertIn("#pragma once", source)
        self.assertIn('#include "ptx_ir/resolved/ptx_resolved_ir.hpp"', source)
        self.assertIn('#include "ptx_ir/ptx_resolved_ir_checker.hpp"', source)
        self.assertIn("namespace ptx_frontend::resolved_ir {", source)
        self.assertIn("struct Add {", source)
        self.assertIn("enum class VariantType {", source)
        self.assertIn("struct IntegerNoSat {", source)
        self.assertIn("ResolvedOperandLayoutTag operand_layout;", source)
        self.assertIn("WithLocs<ScalarType> type;", source)
        self.assertIn("WithLocs<bool> saturate;", source)
        self.assertIn("inline static constexpr bool saturate = true;", source)
        self.assertIn(
            "inline static constexpr ScalarType type = ScalarType::S32;",
            source,
        )
        self.assertIn("using Variant = std::variant<", source)
        self.assertIn(
            "static const check_end::SyntaxInstructionDescriptor&\n"
            "  get_syntax_descriptor() noexcept;",
            source,
        )
        self.assertIn(
            "static const check_end::ResolvedInstructionDescriptor&\n"
            "  get_resolved_descriptor() noexcept;",
            source,
        )
        self.assertIn(
            "static const checker::InstructionDescriptor&\n"
            "  get_checker_descriptor() noexcept;",
            source,
        )
        self.assertNotIn(
            "template <>\nstd::expected<Add, ResolveDiagnostic>\n"
            "resolve<Add>(const syntax_ast::AstInstruction& ast);",
            source,
        )
        self.assertIn(
            "template <>\ninline std::expected<Add, ResolveDiagnostic>\n"
            "resolve<Add>(const syntax_ast::AstInstruction& ast) {",
            source,
        )
        self.assertIn("resolve_fields(", source)
        self.assertIn("inline CheckResult check<Add>(", source)
        self.assertIn("const auto check_integer_no_sat =", source)
        self.assertIn("const auto check_packed_optional_sat_sm120 =", source)
        self.assertIn("detail::VariantCheckFunction<", source)
        self.assertIn("std::visit(detail::Overloaded{", source)
        self.assertIn("const auto operand_check = check_operands(", source)
        self.assertIn("const auto layout_check = check_operand_layout_tag(", source)
        self.assertIn("Add::get_checker_descriptor(), \"IntegerNoSat\"", source)
        self.assertNotIn("AddResolvedDescriptorStorage", source)
        self.assertIn("}  // namespace ptx_frontend::resolved_ir", source)

    def test_generate_private_resolved_descriptor_source(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )

        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_descriptor.gen.cpp"
            generate_resolved_descriptor_source(
                database,
                output_path=output_path,
            )
            source = output_path.read_text(encoding="utf-8")

        self.assertTrue(
            source.startswith("// Generated by python/scripts/gen_all.py. Do not edit.")
        )
        self.assertNotIn("#pragma once", source)
        self.assertIn('#include "ptx_ir/resolved/ptx_resolved_ir.hpp"', source)
        self.assertIn("namespace ptx_frontend::resolved_ir {", source)
        self.assertIn("struct AddResolvedDescriptorStorage {", source)
        self.assertIn("check_end::ResolvedFieldDescriptor", source)
        self.assertNotIn("ResolvedConstantDescriptor", source)
        self.assertIn("check_end::ResolvedModifierBindingDescriptor", source)
        self.assertIn("check_end::ResolvedOperandBindingDescriptor", source)
        self.assertIn(".role = check_end::OperandRole::Destination,", source)
        self.assertIn(".access = check_end::OperandAccess::Write,", source)
        self.assertIn(
            ".allowed_shapes = check_end::OperandShape::Register | "
            "check_end::OperandShape::Immediate,",
            source,
        )
        self.assertIn('.target_field_id = "saturate",', source)
        self.assertNotIn("ResolvedConstantDescriptor", source)
        self.assertIn(
            "const check_end::ResolvedInstructionDescriptor&\n"
            "Add::get_resolved_descriptor() noexcept {",
            source,
        )
        self.assertNotIn("resolve<Add>", source)
        self.assertNotIn("resolve_fields(", source)

    def test_generate_private_checker_descriptor_source(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )

        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir_checker_descriptor.gen.cpp"
            generate_resolved_checker_descriptor_source(
                database,
                output_path=output_path,
            )
            source = output_path.read_text(encoding="utf-8")

        self.assertIn('#include "ptx_ir/ptx_resolved_ir_checker.hpp"', source)
        self.assertIn("struct AddCheckerDescriptorStorage {", source)
        self.assertIn("checker::VariantDescriptor", source)
        self.assertIn('.minimum_ptx_version = {9, 2},', source)
        self.assertIn('.minimum_sm_version = 120,', source)
        self.assertIn('.required_family = "sm_120f",', source)
        self.assertIn('.rule_id = "integer_arith.add_packed",', source)
        self.assertIn(
            "const checker::InstructionDescriptor&\n"
            "Add::get_checker_descriptor() noexcept {",
            source,
        )


if __name__ == "__main__":
    unittest.main()
