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
from code_gen.database import CodegenDatabase
from code_gen.gen_resolved_descriptor import generate_resolved_descriptor_source
from code_gen.gen_resolved_checker_descriptor import (
    generate_resolved_checker_descriptor_source,
)
from code_gen.gen_resolved_ir import generate_resolved_ir_header
from code_gen.normalize import normalize_instruction_spec
from ir.resolved_ir import (
    ResolvedFieldOrigin,
    ResolvedFieldStorage,
    ResolvedOperandAccess,
    ResolvedOperandRole,
    ResolvedOperandShape,
    ResolvedOperandTypeExpression,
    ResolvedOperandTypeExpressionKind,
    ResolvedValueKind,
    from_instruction_spec,
)
from code_gen.model import (
    InstructionSpec,
    ModifierSpec,
    ModifierValueSpec,
    OperandLayoutSpec,
    OperandSpec,
    OperandTypeExpression,
    OperandTypeExpressionKind,
    VariantSpec,
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
                (field.name, field.cpp_type, field.origin)
                for field in variants["IntegerNoSat"].fields
            ],
            [
                ("type", "WithLocs<ScalarType>", ResolvedFieldOrigin.MODIFIER),
                ("dst", "WithLocs<ResolvedRegisterRef>", ResolvedFieldOrigin.OPERAND),
                ("src1", "WithLocs<RegOrImm>", ResolvedFieldOrigin.OPERAND),
                ("src2", "WithLocs<RegOrImm>", ResolvedFieldOrigin.OPERAND),
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
        optional_sat_binding = variants[
            "PackedOptionalSatSm120"
        ].modifier_bindings[0]
        self.assertIsNotNone(optional_sat_binding.default_value)
        assert optional_sat_binding.default_value is not None
        self.assertEqual(optional_sat_binding.default_value.value_cpp_type, "bool")
        self.assertIs(optional_sat_binding.default_value.value, False)
        self.assertIsNone(
            variants["PackedOptionalSatSm120"]
            .modifier_bindings[1]
            .default_value
        )
        self.assertEqual(
            [
                (field.name, field.cpp_type, field.origin)
                for field in variants["PackedOptionalSatSm120"].fields
            ],
            [
                ("saturate", "WithLocs<bool>", ResolvedFieldOrigin.MODIFIER),
                ("type", "WithLocs<ScalarType>", ResolvedFieldOrigin.MODIFIER),
                ("dst", "WithLocs<ResolvedRegisterRef>", ResolvedFieldOrigin.OPERAND),
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
                    binding.type_expression,
                    binding.role,
                    binding.access,
                    binding.allowed_shapes,
                )
                for binding in variants["IntegerNoSat"].operand_layouts[0].bindings
            ],
            [
                (
                    "dst",
                    ResolvedOperandTypeExpression(
                        kind=ResolvedOperandTypeExpressionKind.MODIFIER_FIELD,
                        modifier_field_id="type",
                    ),
                    ResolvedOperandRole.DESTINATION,
                    ResolvedOperandAccess.WRITE,
                    (ResolvedOperandShape.REGISTER,),
                ),
                (
                    "src1",
                    ResolvedOperandTypeExpression(
                        kind=ResolvedOperandTypeExpressionKind.MODIFIER_FIELD,
                        modifier_field_id="type",
                    ),
                    ResolvedOperandRole.SOURCE,
                    ResolvedOperandAccess.READ,
                    (
                        ResolvedOperandShape.REGISTER,
                        ResolvedOperandShape.IMMEDIATE,
                    ),
                ),
                (
                    "src2",
                    ResolvedOperandTypeExpression(
                        kind=ResolvedOperandTypeExpressionKind.MODIFIER_FIELD,
                        modifier_field_id="type",
                    ),
                    ResolvedOperandRole.SOURCE,
                    ResolvedOperandAccess.READ,
                    (
                        ResolvedOperandShape.REGISTER,
                        ResolvedOperandShape.IMMEDIATE,
                    ),
                ),
            ],
        )

    def test_bar_sync_uses_distinct_modifier_variants_and_operand_layouts(
        self,
    ) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        bar = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "bar"
        )
        instruction = from_instruction_spec(bar)
        variants = {variant.cpp_name: variant for variant in instruction.variants}

        self.assertEqual(
            set(variants),
            {
                "Sync",
                "CtaSync",
                "Arrive",
                "CtaArrive",
                "RedPopcU32",
                "CtaRedPopcU32",
                "RedAndPred",
                "CtaRedAndPred",
                "RedOrPred",
                "CtaRedOrPred",
            },
        )
        self.assertEqual(
            [layout.layout_id for layout in variants["Sync"].operand_layouts],
            [
                "immediate_barrier",
                "barrier",
                "barrier_and_thread_count",
            ],
        )
        self.assertEqual(
            dict(variants["Sync"].operand_layouts[0].availability),
            {},
        )
        self.assertEqual(
            dict(variants["Sync"].operand_layouts[1].availability),
            {"ptx": "2.0", "sm": 20},
        )
        self.assertEqual(
            [field.name for field in variants["Sync"].modifier_fields],
            ["sync"],
        )
        self.assertEqual(
            [field.name for field in variants["CtaSync"].modifier_fields],
            ["cta", "sync"],
        )
        self.assertTrue(
            all(
                field.storage is ResolvedFieldStorage.STATIC_CONSTANT
                for field in variants["CtaSync"].modifier_fields
            )
        )
        self.assertEqual(
            [
                (binding.target_field_id, binding.type_expression, binding.role)
                for binding in variants["Sync"].operand_layouts[2].bindings
            ],
            [
                (
                    "barrier",
                    ResolvedOperandTypeExpression(
                        kind=ResolvedOperandTypeExpressionKind.FIXED_SCALAR,
                        scalar_type="u32",
                    ),
                    ResolvedOperandRole.BARRIER,
                ),
                (
                    "thread_count",
                    ResolvedOperandTypeExpression(
                        kind=ResolvedOperandTypeExpressionKind.FIXED_SCALAR,
                        scalar_type="u32",
                    ),
                    ResolvedOperandRole.THREAD_COUNT,
                ),
            ],
        )
        self.assertEqual(
            [layout.layout_id for layout in variants["RedPopcU32"].operand_layouts],
            ["without_thread_count", "with_thread_count"],
        )
        self.assertEqual(
            variants["RedPopcU32"].operand_layouts[0].fields[2].value_cpp_type,
            "ResolvedPredicate",
        )
        self.assertEqual(
            variants["RedAndPred"].operand_layouts[1].fields[0].value_cpp_type,
            "ResolvedPredicate",
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
        self.assertEqual(source.count("namespace checker {"), 1)
        self.assertIn("struct Add {", source)
        self.assertIn("struct Bar {", source)
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
        self.assertIn("check_modifier_value_availability(", source)
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
        self.assertEqual(source.count("namespace generated_detail {"), 1)
        self.assertIn("struct AddResolvedDescriptorStorage {", source)
        self.assertIn("struct BarResolvedDescriptorStorage {", source)
        self.assertIn("check_end::ResolvedFieldDescriptor", source)
        self.assertNotIn("ResolvedConstantDescriptor", source)
        self.assertIn("check_end::ResolvedModifierBindingDescriptor", source)
        self.assertIn("check_end::ResolvedOperandBindingDescriptor", source)
        self.assertIn("check_end::TypeExpressionDescriptor", source)
        self.assertIn(
            ".kind = check_end::OperandTypeExpressionKind::ModifierField,",
            source,
        )
        self.assertIn(
            ".kind = check_end::OperandTypeExpressionKind::FixedScalar,",
            source,
        )
        self.assertIn(".fixed_scalar_type = ScalarType::U32,", source)
        self.assertNotIn("modifier(type)", source)
        self.assertIn("_operand_layout_0_fields", source)
        self.assertIn('.layout_id = "default",', source)
        self.assertIn(".role = check_end::OperandRole::Destination,", source)
        self.assertIn(".access = check_end::OperandAccess::Write,", source)
        self.assertIn(
            ".allowed_shapes = check_end::OperandShape::Register | "
            "check_end::OperandShape::Immediate,",
            source,
        )
        self.assertIn('.target_field_id = "saturate",', source)
        self.assertIn(
            ".kind = check_end::ResolvedModifierDefaultKind::Bool,", source
        )
        self.assertIn(".bool_value = false,", source)
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
        self.assertEqual(source.count("namespace generated_detail {"), 1)
        self.assertIn("struct AddCheckerDescriptorStorage {", source)
        self.assertIn("struct BarCheckerDescriptorStorage {", source)
        self.assertIn("checker::VariantDescriptor", source)
        self.assertIn("checker::OperandLayoutDescriptor", source)
        self.assertIn('.layout_name = "immediate_barrier",', source)
        self.assertIn('.layout_name = "barrier_and_thread_count",', source)
        self.assertIn('.minimum_ptx_version = {9, 2},', source)
        self.assertIn('.minimum_sm_version = 120,', source)
        self.assertIn('.required_family = "sm_120f",', source)
        self.assertIn('.rule_id = "integer_arith.add_packed",', source)
        self.assertIn(
            "const checker::InstructionDescriptor&\n"
            "Add::get_checker_descriptor() noexcept {",
            source,
        )

    def test_modifier_value_availability_survives_normalization_and_emission(
        self,
    ) -> None:
        specs = normalize_instruction_spec(
            {
                "type_sets": {"late_scalar": ["u32", "u64"]},
                "instructions": [
                    {
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
                                        "values": [
                                            {
                                                "value": "$late_scalar",
                                                "availability": {
                                                    "ptx": "2.0",
                                                    "sm": 20,
                                                },
                                            },
                                        ],
                                    }
                                ],
                                "operands": [],
                            }
                        ],
                    }
                ]
            }
        )
        resolved = from_instruction_spec(specs[0])
        entries = resolved.variants[0].modifier_value_availabilities
        self.assertEqual([entry.source_kind_id for entry in entries], ["type", "type"])
        self.assertEqual([entry.value for entry in entries], ["u32", "u64"])
        self.assertTrue(
            all(dict(entry.availability) == {"ptx": "2.0", "sm": 20}
                for entry in entries)
        )

        database = CodegenDatabase(spec_schema="ptx-instr/v1", instructions=specs)
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir_checker_descriptor.gen.cpp"
            generate_resolved_checker_descriptor_source(
                database,
                output_path=output_path,
            )
            source = output_path.read_text(encoding="utf-8")

        self.assertIn("checker::ModifierValueAvailabilityDescriptor", source)
        self.assertIn('.kind_id = "type",', source)
        self.assertIn(".scalar_type = ScalarType::U32,", source)
        self.assertIn(".scalar_type = ScalarType::U64,", source)
        self.assertIn(".minimum_ptx_version = {2, 0},", source)

    def test_rejects_token_override_for_value_set_reference(self) -> None:
        with self.assertRaisesRegex(ValueError, "value-set reference"):
            normalize_instruction_spec(
                {
                    "type_sets": {"scalar": ["u32", "u64"]},
                    "instructions": [
                        {
                            "opcode": "sample",
                            "variants": [
                                {
                                    "name": "sample_type",
                                    "availability": {"ptx": "1.0"},
                                    "modifiers": [
                                        {
                                            "name": "type",
                                            "kind": "type",
                                            "presence": "required",
                                            "domain": "scalar_types",
                                            "values": [
                                                {
                                                    "value": "$scalar",
                                                    "token": ".scalar",
                                                }
                                            ],
                                        }
                                    ],
                                    "operands": [],
                                }
                            ],
                        }
                    ],
                }
            )

    def test_multi_layout_variant_generates_nested_operand_payload(self) -> None:
        instruction = InstructionSpec(
            opcode="sample",
            syntax=None,
            variants=(
                VariantSpec(
                    name="sample_typed",
                    availability={"ptx": "1.0", "sm": 0},
                    modifiers=(
                        ModifierSpec(
                            name="type",
                            kind="type",
                            presence="required",
                            values=(ModifierValueSpec(value="u32"),),
                        ),
                    ),
                    operand_layouts=(
                        OperandLayoutSpec(
                            name="binary",
                            operands=(
                                OperandSpec(
                                    name="dst",
                                    kind="reg",
                                    role="dst",
                                    access="write",
                                    type_expression=OperandTypeExpression(
                                        kind=OperandTypeExpressionKind.MODIFIER,
                                        modifier_name="type",
                                    ),
                                ),
                                OperandSpec(
                                    name="src",
                                    kind="reg_or_imm",
                                    role="src",
                                    access="read",
                                    type_expression=OperandTypeExpression(
                                        kind=OperandTypeExpressionKind.MODIFIER,
                                        modifier_name="type",
                                    ),
                                ),
                            ),
                        ),
                        OperandLayoutSpec(
                            name="ternary",
                            operands=(
                                OperandSpec(
                                    name="dst",
                                    kind="reg",
                                    role="dst",
                                    access="write",
                                    type_expression=OperandTypeExpression(
                                        kind=OperandTypeExpressionKind.MODIFIER,
                                        modifier_name="type",
                                    ),
                                ),
                                OperandSpec(
                                    name="src1",
                                    kind="reg_or_imm",
                                    role="src1",
                                    access="read",
                                    type_expression=OperandTypeExpression(
                                        kind=OperandTypeExpressionKind.MODIFIER,
                                        modifier_name="type",
                                    ),
                                ),
                                OperandSpec(
                                    name="src2",
                                    kind="reg_or_imm",
                                    role="src2",
                                    access="read",
                                    type_expression=OperandTypeExpression(
                                        kind=OperandTypeExpressionKind.MODIFIER,
                                        modifier_name="type",
                                    ),
                                ),
                            ),
                        ),
                    ),
                    rule="sample.typed",
                ),
            ),
        )
        database = CodegenDatabase(spec_schema="ptx-instr/v1", instructions=(instruction,))

        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir.gen.hpp"
            generate_resolved_ir_header(database, output_path=output_path)
            source = output_path.read_text(encoding="utf-8")

        self.assertIn("struct BinaryOperands {", source)
        self.assertIn("struct TernaryOperands {", source)
        self.assertIn(
            "using Operands = std::variant<BinaryOperands, TernaryOperands>;",
            source,
        )
        self.assertIn("Operands operands;", source)
        self.assertIn("check_sample_typed_binary_operands", source)
        self.assertIn("check_sample_typed_ternary_operands", source)


if __name__ == "__main__":
    unittest.main()
