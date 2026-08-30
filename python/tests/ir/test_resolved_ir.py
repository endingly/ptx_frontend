from __future__ import annotations

from dataclasses import replace
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
from code_gen.gen_resolved_descriptor import (
    _emit_address_state_spaces,
    _emit_operand_binding_descriptor,
    generate_resolved_descriptor_source,
)
from code_gen.gen_resolved_checker_descriptor import (
    generate_resolved_checker_descriptor_source,
)
from code_gen.gen_resolved_ir import (
    generate_resolved_dispatch_source,
    generate_resolved_ir_header,
    generate_resolved_ir_source,
)
from code_gen.normalize import normalize_instruction_spec
from ir.resolved_ir import (
    ResolvedFieldOrigin,
    ResolvedFieldStorage,
    ResolvedOperandAccess,
    ResolvedOperandRole,
    ResolvedOperandShape,
    ResolvedOperandTypeExpression,
    ResolvedOperandTypeExpressionKind,
    ResolvedRegisterWidthPolicy,
    ResolvedValueKind,
    ResolvedVectorTypePolicy,
    from_instruction_spec,
)
from code_gen.model import (
    ImmediateMultipleOfConstraint,
    ImmediateRangeConstraint,
    ImmediateValueConstraint,
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
        mul = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "mul"
        )
        mad = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "mad"
        )
        fma = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "fma"
        )
        div = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "div"
        )
        rem = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "rem"
        )
        min_instruction = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "min"
        )
        max_instruction = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "max"
        )
        abs_instruction = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "abs"
        )
        neg_instruction = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "neg"
        )
        lop3_instruction = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "lop3"
        )
        shf_instruction = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "shf"
        )
        bfe_instruction = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "bfe"
        )
        cls.instruction = from_instruction_spec(add)
        cls.sub_instruction = from_instruction_spec(sub)
        cls.mul_instruction = from_instruction_spec(mul)
        cls.mad_instruction = from_instruction_spec(mad)
        cls.fma_instruction = from_instruction_spec(fma)
        cls.div_instruction = from_instruction_spec(div)
        cls.rem_instruction = from_instruction_spec(rem)
        cls.min_instruction = from_instruction_spec(min_instruction)
        cls.max_instruction = from_instruction_spec(max_instruction)
        cls.abs_instruction = from_instruction_spec(abs_instruction)
        cls.neg_instruction = from_instruction_spec(neg_instruction)
        cls.lop3_instruction = from_instruction_spec(lop3_instruction)
        cls.shf_instruction = from_instruction_spec(shf_instruction)
        cls.bfe_instruction = from_instruction_spec(bfe_instruction)
        call = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "call"
        )
        cls.call_instruction = from_instruction_spec(call)

    def test_call_has_layout_local_group_payloads(self) -> None:
        self.assertEqual(self.call_instruction.cpp_name, "Call")
        variant = self.call_instruction.variants[0]
        self.assertEqual(variant.cpp_name, "Direct")
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
            [
                [field.value_cpp_type for field in layout.fields]
                for layout in variant.operand_layouts
            ],
            [
                ["ResolvedFunctionRef"],
                ["ResolvedFunctionRef", "ResolvedCallArguments"],
                [
                    "ResolvedCallParameterRef",
                    "ResolvedFunctionRef",
                    "ResolvedCallArguments",
                ],
                ["ResolvedIndirectCallee", "ResolvedIndirectCallee"],
                [
                    "ResolvedIndirectCallee",
                    "ResolvedCallArguments",
                    "ResolvedIndirectCallee",
                ],
                [
                    "ResolvedCallParameterRef",
                    "ResolvedIndirectCallee",
                    "ResolvedCallArguments",
                    "ResolvedIndirectCallee",
                ],
            ],
        )
        self.assertEqual(
            [field.value_kind for field in variant.operand_layouts[2].fields],
            [
                ResolvedValueKind.CALL_RETURN_PARAMETER,
                ResolvedValueKind.DIRECT_CALL_TARGET,
                ResolvedValueKind.CALL_ARGUMENTS,
            ],
        )
        self.assertEqual(
            [dict(layout.availability) for layout in variant.operand_layouts[3:]],
            [
                {"ptx": "2.1", "sm": 20},
                {"ptx": "2.1", "sm": 20},
                {"ptx": "2.1", "sm": 20},
            ],
        )
        self.assertEqual(
            [field.value_kind for field in variant.operand_layouts[3].fields],
            [ResolvedValueKind.INDIRECT_CALLEE, ResolvedValueKind.INDIRECT_CALLEE],
        )

    def test_normalizes_memory_vector_cross_constraint(self) -> None:
        variant = {
            "name": "sample_vector",
            "availability": {"ptx": "1.0"},
            "modifiers": [
                {
                    "name": "type",
                    "kind": "type",
                    "presence": "required",
                    "domain": "scalar_types",
                    "values": ["u32"],
                }
            ],
            "operands": [
                {
                    "name": "dst",
                    "kind": "reg_vector",
                    "role": "dst",
                    "access": "write",
                    "type": {"expr": "modifier(type)"},
                    "vector": {"kind": "vector", "arity": [8]},
                },
                {"name": "address", "kind": "addr", "role": "addr", "access": "read"},
            ],
            "constraints": [
                {
                    "kind": "memory_vector",
                    "type_modifier": "type",
                    "vector_operand": "dst",
                    "address_operand": "address",
                    "availability": {"ptx": "8.8", "sm": 100},
                }
            ],
        }
        spec = {
            "category": "test",
            "codegen_category": "test",
            "instructions": [{"opcode": "sample", "variants": [variant]}],
        }
        normalized = normalize_instruction_spec(spec)[0].variants[0]
        assert normalized.memory_vector is not None
        self.assertEqual(normalized.memory_vector.vector_operand, "dst")
        self.assertEqual(normalized.memory_vector.availability["sm"], 100)

        invalid = dict(variant)
        invalid["constraints"] = [dict(variant["constraints"][0], availability="8.8")]
        spec["instructions"][0]["variants"] = [invalid]
        with self.assertRaisesRegex(TypeError, "availability must be an object"):
            normalize_instruction_spec(spec)

    def test_add_variant_names(self) -> None:
        self.assertEqual(self.instruction.opcode, "add")
        self.assertEqual(self.instruction.cpp_name, "Add")
        self.assertEqual(
            [variant.cpp_name for variant in self.instruction.variants],
            [
                "FloatF32",
                "FloatF32x2",
                "FloatF64",
                "Half",
                "Bfloat",
                "MixedF32",
                "IntegerNoSat",
                "Sat",
                "PackedOptionalSat",
            ],
        )

    def test_sub_variant_names_and_fields(self) -> None:
        self.assertEqual(self.sub_instruction.opcode, "sub")
        self.assertEqual(self.sub_instruction.cpp_name, "Sub")
        self.assertEqual(
            [variant.cpp_name for variant in self.sub_instruction.variants],
            [
                "FloatF32",
                "FloatF32x2",
                "FloatF64",
                "Half",
                "Bfloat",
                "MixedF32",
                "IntegerNoSat",
                "OptionalSat",
            ],
        )

        variants = {
            variant.cpp_name: variant
            for variant in self.sub_instruction.variants
        }
        optional_sat = variants["OptionalSat"]
        self.assertEqual(
            [field.name for field in optional_sat.fields],
            ["saturate", "type", "dst", "src1", "src2"],
        )
        self.assertEqual(
            optional_sat.modifier_bindings[0].default_value.value,
            False,
        )
        self.assertEqual(
            [
                (entry.value, dict(entry.availability))
                for entry in optional_sat.modifier_value_availabilities
            ],
            [
                (
                    "u8x4",
                    {"ptx": "9.2", "sm": 120, "family": "sm_120f"},
                ),
                (
                    "s8x4",
                    {"ptx": "9.2", "sm": 120, "family": "sm_120f"},
                ),
            ],
        )

        mixed = variants["MixedF32"]
        self.assertEqual(
            [field.name for field in mixed.operand_layouts[0].fields],
            ["dst", "src", "subtrahend"],
        )

    def test_floating_add_rounding_defaults_and_availability(self) -> None:
        variants = {variant.cpp_name: variant for variant in self.instruction.variants}
        f32 = variants["FloatF32"]
        self.assertEqual(
            [
                (field.name, field.cpp_type, field.storage)
                for field in f32.modifier_fields
            ],
            [
                ("rounding", "WithLocs<RoundingMode>", ResolvedFieldStorage.INSTANCE),
                ("ftz", "WithLocs<bool>", ResolvedFieldStorage.INSTANCE),
                ("saturate", "WithLocs<bool>", ResolvedFieldStorage.INSTANCE),
                ("type", "ScalarType", ResolvedFieldStorage.STATIC_CONSTANT),
            ],
        )
        rounding_default = f32.modifier_bindings[0].default_value
        self.assertIsNotNone(rounding_default)
        assert rounding_default is not None
        self.assertEqual(rounding_default.value_cpp_type, "RoundingMode")
        self.assertEqual(rounding_default.value, "rn")
        self.assertEqual(
            [
                (entry.value, dict(entry.availability))
                for entry in f32.modifier_value_availabilities
            ],
            [
                ("rm", {"ptx": "1.0", "sm": 20}),
                ("rp", {"ptx": "1.0", "sm": 20}),
            ],
        )
        self.assertEqual(variants["FloatF32"].modifier_fields[3].cpp_constant_expr,
                         "ScalarType::F32")

        mixed = variants["MixedF32"]
        self.assertEqual(
            [
                (field.name, field.cpp_type, field.storage)
                for field in mixed.modifier_fields
            ],
            [
                ("rounding", "WithLocs<RoundingMode>", ResolvedFieldStorage.INSTANCE),
                ("result_type", "ScalarType", ResolvedFieldStorage.STATIC_CONSTANT),
                ("input_type", "WithLocs<ScalarType>", ResolvedFieldStorage.INSTANCE),
                ("saturate", "WithLocs<bool>", ResolvedFieldStorage.INSTANCE),
            ],
        )
        self.assertEqual(
            [binding.type_expression.modifier_field_id for binding in mixed.operand_layouts[0].bindings],
            ["result_type", "input_type", "result_type"],
        )

    def test_mul_merges_frozen_integer_and_floating_variants(self) -> None:
        self.assertEqual(
            [variant.cpp_name for variant in self.mul_instruction.variants],
            ["RnF32", "LoU32", "HiU32", "WideU32", "WideS32"],
        )
        self.assertEqual(
            [field.name for field in self.mul_instruction.variants[0].fields],
            ["rounding", "type", "dst", "src1", "src2"],
        )
        self.assertEqual(
            [
                binding.register_width_policy
                for binding in self.mul_instruction.variants[3].operand_layouts[0].bindings
            ],
            [ResolvedRegisterWidthPolicy.EXACT] * 3,
        )
        self.assertEqual(
            [
                binding.register_width_policy
                for binding in self.mul_instruction.variants[4].operand_layouts[0].bindings
            ],
            [ResolvedRegisterWidthPolicy.SAME_WIDTH] * 3,
        )

    def test_mad_merges_frozen_integer_and_floating_ternary_layouts(self) -> None:
        self.assertEqual(
            [variant.cpp_name for variant in self.mad_instruction.variants],
            ["RnF32", "LoU32", "LoS32", "WideU32"],
        )
        self.assertEqual(
            [field.name for field in self.mad_instruction.variants[1].fields],
            ["lo", "type", "dst", "src1", "src2", "src3"],
        )
        self.assertEqual(
            self.mad_instruction.variants[1].operand_layouts[0].bindings[3].role,
            ResolvedOperandRole.SOURCE,
        )
        self.assertEqual(
            [
                binding.register_width_policy
                for binding in self.mad_instruction.variants[3].operand_layouts[0].bindings
            ],
            [ResolvedRegisterWidthPolicy.EXACT] * 4,
        )
        self.assertEqual(
            [
                binding.register_width_policy
                for binding in self.mad_instruction.variants[0].operand_layouts[0].bindings
            ],
            [ResolvedRegisterWidthPolicy.EXACT] * 4,
        )

    def test_fma_merges_frozen_rn_ternary_layouts(self) -> None:
        self.assertEqual(
            [variant.cpp_name for variant in self.fma_instruction.variants],
            ["RnF32", "RnF64", "RnF16"],
        )
        self.assertEqual(
            [field.name for field in self.fma_instruction.variants[0].fields],
            ["rounding", "type", "dst", "src1", "src2", "src3"],
        )
        self.assertEqual(
            self.fma_instruction.variants[0].operand_layouts[0].bindings[3].role,
            ResolvedOperandRole.SOURCE,
        )
        for variant in self.fma_instruction.variants[1:]:
            self.assertEqual(
                [binding.register_width_policy for binding in variant.operand_layouts[0].bindings],
                [ResolvedRegisterWidthPolicy.EXACT] * 4,
            )

    def test_div_merges_frozen_integer_and_floating_binary_layouts(self) -> None:
        self.assertEqual(
            [variant.cpp_name for variant in self.div_instruction.variants],
            ["RnF32", "RnF64", "U32", "S32"],
        )
        self.assertEqual(
            [field.name for field in self.div_instruction.variants[2].fields],
            ["type", "dst", "src1", "src2"],
        )
        for variant in self.div_instruction.variants[:2]:
            self.assertEqual(
                [binding.register_width_policy for binding in variant.operand_layouts[0].bindings],
                [ResolvedRegisterWidthPolicy.EXACT] * 3,
            )

    def test_rem_has_frozen_signed_and_unsigned_binary_variants(self) -> None:
        self.assertEqual(
            [variant.cpp_name for variant in self.rem_instruction.variants],
            ["S32", "U32"],
        )
        for variant, scalar_type in zip(
            self.rem_instruction.variants, ("S32", "U32"), strict=True
        ):
            self.assertEqual(
                [field.name for field in variant.fields],
                ["type", "dst", "src1", "src2"],
            )
            self.assertEqual(variant.fields[0].constant_value, scalar_type.lower())

    def test_min_has_frozen_signed_and_nan_binary_variants(self) -> None:
        self.assertEqual(
            [variant.cpp_name for variant in self.min_instruction.variants],
            ["S32", "NanF32"],
        )
        s32, nan_f32 = self.min_instruction.variants
        self.assertEqual(
            [field.name for field in s32.fields], ["type", "dst", "src1", "src2"]
        )
        self.assertEqual(s32.fields[0].constant_value, "s32")
        self.assertEqual(
            [field.name for field in nan_f32.fields],
            ["nan", "type", "dst", "src1", "src2"],
        )
        self.assertTrue(nan_f32.fields[0].constant_value)
        self.assertEqual(nan_f32.fields[1].constant_value, "f32")
        self.assertEqual(
            [binding.register_width_policy for binding in nan_f32.operand_layouts[0].bindings],
            [ResolvedRegisterWidthPolicy.EXACT] * 3,
        )

    def test_max_has_frozen_signed_and_nan_binary_variants(self) -> None:
        self.assertEqual(
            [variant.cpp_name for variant in self.max_instruction.variants],
            ["S32", "NanF32"],
        )
        s32, nan_f32 = self.max_instruction.variants
        self.assertEqual(
            [field.name for field in s32.fields], ["type", "dst", "src1", "src2"]
        )
        self.assertEqual(s32.fields[0].constant_value, "s32")
        self.assertEqual(
            [field.name for field in nan_f32.fields],
            ["nan", "type", "dst", "src1", "src2"],
        )
        self.assertTrue(nan_f32.fields[0].constant_value)
        self.assertEqual(nan_f32.fields[1].constant_value, "f32")
        self.assertEqual(
            [binding.register_width_policy for binding in nan_f32.operand_layouts[0].bindings],
            [ResolvedRegisterWidthPolicy.EXACT] * 3,
        )

    def test_abs_has_frozen_signed_and_float_unary_variants(self) -> None:
        self.assertEqual(
            [variant.cpp_name for variant in self.abs_instruction.variants],
            ["S32", "F32"],
        )
        for variant, scalar_type in zip(
            self.abs_instruction.variants, ("s32", "f32"), strict=True
        ):
            self.assertEqual(
                [field.name for field in variant.fields], ["type", "dst", "src"]
            )
            self.assertEqual(variant.fields[0].constant_value, scalar_type)

    def test_neg_has_frozen_scalar_and_packed_unary_variants(self) -> None:
        self.assertEqual(
            [variant.cpp_name for variant in self.neg_instruction.variants],
            ["S32", "F32", "F16x2"],
        )
        for variant, scalar_type in zip(
            self.neg_instruction.variants, ("s32", "f32", "f16x2"), strict=True
        ):
            self.assertEqual(
                [field.name for field in variant.fields], ["type", "dst", "src"]
            )
            self.assertEqual(variant.fields[0].constant_value, scalar_type)
        self.assertEqual(
            [binding.register_width_policy
             for binding in self.neg_instruction.variants[2].operand_layouts[0].bindings],
            [ResolvedRegisterWidthPolicy.EXACT] * 2,
        )

    def test_lop3_has_fixed_b32_variant_and_lut_range(self) -> None:
        variant = self.lop3_instruction.variants[0]
        self.assertEqual(variant.cpp_name, "B32")
        self.assertEqual(
            [field.name for field in variant.fields],
            ["type", "dst", "src1", "src2", "src3", "lut"],
        )
        self.assertEqual(variant.fields[0].constant_value, "b32")
        self.assertEqual(
            [(constraint.operand_field_id, constraint.minimum, constraint.maximum)
             for constraint in variant.immediate_ranges],
            [("lut", 0, 255)],
        )

    def test_bfe_has_two_immediate_ranges(self) -> None:
        variant = self.bfe_instruction.variants[0]
        self.assertEqual(variant.cpp_name, "U32")
        self.assertEqual(
            [field.name for field in variant.fields],
            ["type", "dst", "src", "offset", "width"],
        )
        self.assertEqual(
            [(constraint.operand_field_id, constraint.minimum, constraint.maximum)
             for constraint in variant.immediate_ranges],
            [("offset", 0, 255), ("width", 0, 255)],
        )
        self.assertEqual(
            [binding.register_width_policy
             for binding in variant.operand_layouts[0].bindings[:2]],
            [ResolvedRegisterWidthPolicy.EXACT] * 2,
        )

    def test_shf_has_frozen_direction_and_mode_variants(self) -> None:
        self.assertEqual(
            [variant.cpp_name for variant in self.shf_instruction.variants],
            ["LClampB32", "RWrapB32"],
        )
        for variant in self.shf_instruction.variants:
            self.assertEqual(
                [field.name for field in variant.fields],
                ["left" if variant.cpp_name == "LClampB32" else "right",
                 "clamp" if variant.cpp_name == "LClampB32" else "wrap",
                 "type", "dst", "src1", "src2", "count"],
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
            [field.name for field in variants["Sat"].fields],
            ["saturate", "type", "dst", "src1", "src2"],
        )
        self.assertEqual(
            [field.storage for field in variants["Sat"].fields[:2]],
            [
                ResolvedFieldStorage.STATIC_CONSTANT,
                ResolvedFieldStorage.INSTANCE,
            ],
        )
        self.assertEqual(
            variants["Sat"].fields[0].cpp_constant_expr,
            "true",
        )
        self.assertEqual(
            [
                (binding.source_kind_id, binding.target_field_id)
                for binding in variants["Sat"].modifier_bindings
            ],
            [("sat", "saturate"), ("type", "type")],
        )
        optional_sat_binding = variants["PackedOptionalSat"].modifier_bindings[0]
        self.assertIsNotNone(optional_sat_binding.default_value)
        assert optional_sat_binding.default_value is not None
        self.assertEqual(optional_sat_binding.default_value.value_cpp_type, "bool")
        self.assertIs(optional_sat_binding.default_value.value, False)
        self.assertIsNone(
            variants["PackedOptionalSat"].modifier_bindings[1].default_value
        )
        self.assertEqual(
            [
                (field.name, field.cpp_type, field.origin)
                for field in variants["PackedOptionalSat"].fields
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
                for binding in variants["PackedOptionalSat"].modifier_bindings
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

        self.assertEqual(
            [
                (entry.value, dict(entry.availability))
                for entry in variants["IntegerNoSat"].modifier_value_availabilities
            ],
            [
                ("u16x2", {"ptx": "8.0", "sm": 90}),
                ("s16x2", {"ptx": "8.0", "sm": 90}),
            ],
        )
        self.assertEqual(
            [
                (entry.value, dict(entry.availability))
                for entry in variants["Sat"].modifier_value_availabilities
            ],
            [
                (
                    "u16x2",
                    {"ptx": "9.2", "sm": 120, "family": "sm_120f"},
                ),
                (
                    "s16x2",
                    {"ptx": "9.2", "sm": 120, "family": "sm_120f"},
                ),
                (
                    "u32",
                    {"ptx": "9.2", "sm": 120, "family": "sm_120f"},
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
                "WarpSync",
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
        self.assertEqual(
            [field.name for field in variants["WarpSync"].modifier_fields],
            ["warp", "sync"],
        )
        self.assertEqual(
            [(field.name, field.value_cpp_type)
             for field in variants["WarpSync"].operand_layouts[0].fields],
            [("membermask", "RegOrImm")],
        )
        self.assertEqual(
            dict(variants["WarpSync"].availability),
            {"ptx": "6.0", "sm": 30},
        )

    def test_barrier_cluster_model_defaults_and_availability(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        barrier = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "barrier"
        )
        variants = {
            variant.cpp_name: variant
            for variant in from_instruction_spec(barrier).variants
        }
        self.assertEqual(set(variants), {"ClusterArrive", "ClusterWait"})
        expected_availability = {
            "any_of": [{"ptx": "7.8", "sm": 90, "capabilities": ["cluster"]}],
        }
        for name, default, values in (
            ("ClusterArrive", "release", ("release", "relaxed")),
            ("ClusterWait", "acquire", ("acquire",)),
        ):
            variant = variants[name]
            self.assertEqual(dict(variant.availability), expected_availability)
            self.assertEqual(
                [(field.name, field.cpp_type) for field in variant.modifier_fields],
                [
                    ("scope", "MemoryScope"),
                    ("arrive" if name == "ClusterArrive" else "wait", "bool"),
                    ("semantics", "WithLocs<MemoryConsistency>"),
                    ("aligned", "WithLocs<bool>"),
                ],
            )
            self.assertEqual(
                variant.modifier_bindings[2].default_value.value,
                default,
            )
            self.assertEqual(
                [entry.value for entry in variant.modifier_value_availabilities],
                list(values),
            )
            self.assertTrue(
                all(dict(entry.availability) == {"ptx": "8.0"}
                    for entry in variant.modifier_value_availabilities)
            )
            self.assertEqual(variant.operand_layouts[0].fields, ())

    def test_match_sync_model_layouts_and_availability(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        match = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "match"
        )
        variants = {
            variant.cpp_name: variant
            for variant in from_instruction_spec(match).variants
        }
        self.assertEqual(set(variants), {"AnySync", "AllSync"})
        for variant in variants.values():
            self.assertEqual(dict(variant.availability), {"ptx": "6.0", "sm": 70})
            self.assertEqual(
                [(field.name, field.cpp_type) for field in variant.modifier_fields],
                [
                    ("any" if variant.cpp_name == "AnySync" else "all", "bool"),
                    ("sync", "bool"),
                    ("type", "WithLocs<ScalarType>"),
                ],
            )
        self.assertEqual(
            [layout.layout_id for layout in variants["AnySync"].operand_layouts],
            ["default"],
        )
        self.assertEqual(
            [layout.layout_id for layout in variants["AllSync"].operand_layouts],
            ["without_predicate", "with_predicate"],
        )
        plain = variants["AllSync"].operand_layouts[0].bindings
        paired = variants["AllSync"].operand_layouts[1].bindings
        self.assertEqual(
            [binding.allowed_shapes for binding in plain],
            [
                (ResolvedOperandShape.REGISTER,),
                (ResolvedOperandShape.REGISTER,),
                (ResolvedOperandShape.REGISTER, ResolvedOperandShape.IMMEDIATE),
            ],
        )
        self.assertEqual(
            paired[0].allowed_shapes,
            (ResolvedOperandShape.SHFL_DESTINATION,),
        )
        for binding in (*plain, *paired):
            self.assertEqual(
                binding.register_width_policy, ResolvedRegisterWidthPolicy.EXACT
            )

    def test_redux_sync_model_variants_and_availability(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        redux = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "redux"
        )
        variants = {
            variant.cpp_name: variant
            for variant in from_instruction_spec(redux).variants
        }
        self.assertEqual(
            set(variants),
            {"SyncAdd", "SyncMin", "SyncMax", "SyncBoolean", "SyncMinF32", "SyncMaxF32"},
        )
        for name in ("SyncAdd", "SyncMin", "SyncMax"):
            variant = variants[name]
            self.assertEqual(dict(variant.availability), {"ptx": "7.0", "sm": 80})
            self.assertEqual(
                [(field.name, field.cpp_type) for field in variant.modifier_fields],
                [("sync", "bool"), (name.removeprefix("Sync").lower(), "bool"),
                 ("type", "WithLocs<ScalarType>")],
            )
        boolean = variants["SyncBoolean"]
        self.assertEqual(dict(boolean.availability), {"ptx": "7.0", "sm": 80})
        self.assertEqual(
            [(field.name, field.cpp_type) for field in boolean.modifier_fields],
            [("sync", "bool"), ("operation", "WithLocs<BooleanOperator>"),
             ("type", "ScalarType")],
        )
        availability = {"any_of": [
            {"ptx": "8.6", "sm": 100, "target": "sm_100a"},
            {"ptx": "8.8", "sm": 100, "family": "sm_100f"},
        ]}
        for name, operation in (("SyncMinF32", "min"), ("SyncMaxF32", "max")):
            variant = variants[name]
            self.assertEqual(dict(variant.availability), availability)
            self.assertEqual(
                [(field.name, field.cpp_type) for field in variant.modifier_fields],
                [("sync", "bool"), (operation, "bool"), ("abs", "WithLocs<bool>"),
                 ("nan", "WithLocs<bool>"), ("type", "ScalarType")],
            )
        for variant in variants.values():
            self.assertEqual(len(variant.operand_layouts), 1)
            self.assertEqual(
                [binding.register_width_policy for binding in variant.operand_layouts[0].bindings],
                [ResolvedRegisterWidthPolicy.EXACT] * 3,
            )

    def test_griddepcontrol_model_has_two_zero_operand_actions(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        griddepcontrol = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "griddepcontrol"
        )
        variants = {
            variant.cpp_name: variant
            for variant in from_instruction_spec(griddepcontrol).variants
        }
        self.assertEqual(set(variants), {"LaunchDependents", "Wait"})
        for name, action in (("LaunchDependents", "launch_dependents"),
                             ("Wait", "wait")):
            variant = variants[name]
            self.assertEqual(dict(variant.availability), {"ptx": "7.8", "sm": 90})
            self.assertEqual(
                [(field.name, field.cpp_type) for field in variant.fields],
                [(action, "bool")],
            )
            self.assertEqual(
                [(layout.layout_id, layout.bindings)
                 for layout in variant.operand_layouts],
                [("default", ())],
            )

    def test_elect_sync_model_allows_only_its_destination_sink(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        elect = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "elect"
        )
        resolved = from_instruction_spec(elect)
        self.assertEqual(resolved.cpp_name, "Elect")
        self.assertEqual([variant.cpp_name for variant in resolved.variants], ["Sync"])
        variant = resolved.variants[0]
        self.assertEqual(dict(variant.availability), {"ptx": "8.0", "sm": 90})
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [
                ("sync", "bool"),
                ("result", "WithLocs<ResolvedShflSyncDestination>"),
                ("membermask", "WithLocs<RegOrImm>"),
            ],
        )
        result, membermask = variant.operand_layouts[0].bindings
        self.assertEqual(result.allowed_shapes, (ResolvedOperandShape.SHFL_DESTINATION,))
        self.assertEqual(result.register_width_policy, ResolvedRegisterWidthPolicy.SAME_WIDTH)
        self.assertTrue(result.allow_destination_sink)
        self.assertEqual(membermask.register_width_policy, ResolvedRegisterWidthPolicy.EXACT)
        self.assertFalse(membermask.allow_destination_sink)
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_descriptor.gen.cpp"
            generate_resolved_descriptor_source(database, output_path=output_path)
            source = output_path.read_text(encoding="utf-8")
        self.assertIn('.allow_destination_sink = true,', source)

    def test_bra_uses_a_binding_aware_branch_target(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        bra = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "bra"
        )
        instruction = from_instruction_spec(bra)

        self.assertEqual(instruction.cpp_name, "Bra")
        self.assertEqual(len(instruction.variants), 1)
        variant = instruction.variants[0]
        self.assertEqual(variant.cpp_name, "Direct")
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [
                ("uni", "WithLocs<bool>"),
                ("target", "WithLocs<ResolvedBranchTarget>"),
            ],
        )
        binding = variant.operand_layouts[0].bindings[0]
        self.assertEqual(binding.role, ResolvedOperandRole.BRANCH_TARGET)
        self.assertEqual(binding.access, ResolvedOperandAccess.CONTROL)
        self.assertEqual(
            binding.allowed_shapes,
            (ResolvedOperandShape.BRANCH_TARGET,),
        )
        self.assertEqual(
            binding.type_expression.kind,
            ResolvedOperandTypeExpressionKind.NONE,
        )
        self.assertEqual(variant.rule, "control_flow.bra")

    def test_brx_uses_a_u32_register_and_branch_target_set(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        brx = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "brx"
        )
        instruction = from_instruction_spec(brx)

        self.assertEqual(instruction.cpp_name, "Brx")
        variant = instruction.variants[0]
        self.assertEqual(variant.cpp_name, "Idx")
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [
                ("idx", "bool"),
                ("uni", "WithLocs<bool>"),
                ("index", "WithLocs<ResolvedRegisterRef>"),
                ("tlist", "WithLocs<ResolvedBranchTargetSet>"),
            ],
        )
        self.assertEqual(
            variant.operand_layouts[0].bindings[0].type_expression,
            ResolvedOperandTypeExpression(
                kind=ResolvedOperandTypeExpressionKind.FIXED_SCALAR,
                scalar_type="u32",
            ),
        )
        self.assertEqual(
            variant.operand_layouts[0].bindings[1].allowed_shapes,
            (ResolvedOperandShape.BRANCH_TARGET_SET,),
        )
        self.assertEqual(variant.rule, "control_flow.brx_idx")

    def test_ret_uses_a_bare_zero_operand_variant(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        ret = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "ret"
        )
        instruction = from_instruction_spec(ret)

        self.assertEqual(instruction.cpp_name, "Ret")
        self.assertEqual(len(instruction.variants), 1)
        variant = instruction.variants[0]
        self.assertEqual(variant.cpp_name, "Bare")
        self.assertEqual(variant.fields, ())
        self.assertEqual(variant.operand_layouts[0].fields, ())
        self.assertEqual(variant.operand_layouts[0].bindings, ())

    def test_exit_uses_a_bare_zero_operand_variant(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        exit_instruction = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "exit"
        )
        instruction = from_instruction_spec(exit_instruction)

        self.assertEqual(instruction.cpp_name, "Exit")
        self.assertEqual(len(instruction.variants), 1)
        variant = instruction.variants[0]
        self.assertEqual(variant.cpp_name, "Bare")
        self.assertEqual(variant.fields, ())
        self.assertEqual(variant.operand_layouts[0].fields, ())
        self.assertEqual(variant.operand_layouts[0].bindings, ())

    def test_trap_uses_a_bare_zero_operand_variant(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        trap = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "trap"
        )
        instruction = from_instruction_spec(trap)

        self.assertEqual(instruction.cpp_name, "Trap")
        self.assertEqual(len(instruction.variants), 1)
        variant = instruction.variants[0]
        self.assertEqual(variant.cpp_name, "Bare")
        self.assertEqual(variant.fields, ())
        self.assertEqual(variant.operand_layouts[0].fields, ())
        self.assertEqual(variant.operand_layouts[0].bindings, ())

    def test_and_uses_a_fixed_b32_binary_variant(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        and_instruction = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "and"
        )
        instruction = from_instruction_spec(and_instruction)

        self.assertEqual(instruction.cpp_name, "And")
        self.assertEqual(len(instruction.variants), 1)
        variant = instruction.variants[0]
        self.assertEqual(variant.cpp_name, "B32")
        self.assertEqual(
            [binding.type_expression.scalar_type
             for binding in variant.operand_layouts[0].bindings],
            ["b32", "b32", "b32"],
        )

    def test_or_uses_a_fixed_b32_binary_variant(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        or_instruction = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "or"
        )
        instruction = from_instruction_spec(or_instruction)

        self.assertEqual(instruction.cpp_name, "Or")
        self.assertEqual(instruction.variants[0].cpp_name, "B32")
        self.assertEqual(
            [binding.type_expression.scalar_type
             for binding in instruction.variants[0].operand_layouts[0].bindings],
            ["b32", "b32", "b32"],
        )

    def test_xor_uses_a_fixed_b32_binary_variant(self) -> None:
        database = load_codegen_database(spec_dir=REPO_ROOT / "instructions/ptx_spec")
        xor = next(item for item in database.instructions if item.opcode == "xor")
        instruction = from_instruction_spec(xor)
        self.assertEqual(instruction.cpp_name, "Xor")
        self.assertEqual(instruction.variants[0].cpp_name, "B32")
        self.assertEqual(
            [binding.type_expression.scalar_type
             for binding in instruction.variants[0].operand_layouts[0].bindings],
            ["b32", "b32", "b32"],
        )

    def test_not_uses_a_fixed_b32_unary_variant(self) -> None:
        database = load_codegen_database(spec_dir=REPO_ROOT / "instructions/ptx_spec")
        not_instruction = next(item for item in database.instructions if item.opcode == "not")
        instruction = from_instruction_spec(not_instruction)
        self.assertEqual(instruction.cpp_name, "Not")
        self.assertEqual(instruction.variants[0].cpp_name, "B32")
        self.assertEqual(
            [binding.type_expression.scalar_type
             for binding in instruction.variants[0].operand_layouts[0].bindings],
            ["b32", "b32"],
        )

    def test_shl_uses_fixed_b32_data_and_u32_amount(self) -> None:
        database = load_codegen_database(spec_dir=REPO_ROOT / "instructions/ptx_spec")
        shl = next(item for item in database.instructions if item.opcode == "shl")
        instruction = from_instruction_spec(shl)
        self.assertEqual(instruction.cpp_name, "Shl")
        self.assertEqual(instruction.variants[0].cpp_name, "B32")
        self.assertEqual(
            [binding.type_expression.scalar_type
             for binding in instruction.variants[0].operand_layouts[0].bindings],
            ["b32", "b32", "u32"],
        )

    def test_shr_uses_fixed_u32_data_and_count(self) -> None:
        database = load_codegen_database(spec_dir=REPO_ROOT / "instructions/ptx_spec")
        shr = next(item for item in database.instructions if item.opcode == "shr")
        instruction = from_instruction_spec(shr)
        self.assertEqual(instruction.cpp_name, "Shr")
        self.assertEqual(instruction.variants[0].cpp_name, "U32")
        self.assertEqual(
            [binding.type_expression.scalar_type
             for binding in instruction.variants[0].operand_layouts[0].bindings],
            ["b32", "b32", "u32"],
        )

    def test_mov_uses_scalar_and_predicate_sources(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        mov = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "mov"
        )
        instruction = from_instruction_spec(mov)

        self.assertEqual(instruction.cpp_name, "Mov")
        self.assertEqual(len(instruction.variants), 3)
        self.assertEqual(
            [value.value for value in mov.variants[0].modifiers[0].values],
            [
                "b16",
                "u16",
                "s16",
                "b32",
                "u32",
                "s32",
                "f32",
                "b64",
                "u64",
                "s64",
                "b128",
                "f64",
            ],
        )
        variant = instruction.variants[0]
        self.assertEqual(variant.cpp_name, "Scalar")
        self.assertEqual(
            [layout.layout_id for layout in variant.operand_layouts],
            ["scalar", "pack", "unpack"],
        )
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [
                ("type", "WithLocs<ScalarType>"),
                ("dst", "WithLocs<ResolvedRegisterRef>"),
                ("src", "WithLocs<ResolvedMovSource>"),
                ("dst", "WithLocs<ResolvedRegisterRef>"),
                ("src", "WithLocs<ResolvedRegisterVector>"),
                ("dst", "WithLocs<ResolvedRegisterVector>"),
                ("src", "WithLocs<ResolvedRegisterRef>"),
            ],
        )
        source_binding = variant.operand_layouts[0].bindings[1]
        self.assertEqual(source_binding.role, ResolvedOperandRole.SOURCE)
        self.assertEqual(source_binding.access, ResolvedOperandAccess.READ)
        self.assertEqual(
            source_binding.allowed_shapes,
            (
                ResolvedOperandShape.REGISTER,
                ResolvedOperandShape.IMMEDIATE,
                ResolvedOperandShape.SPECIAL_REGISTER,
                ResolvedOperandShape.SYMBOL,
                ResolvedOperandShape.ADDRESS,
            ),
        )
        self.assertEqual(
            source_binding.type_expression,
            ResolvedOperandTypeExpression(
                kind=ResolvedOperandTypeExpressionKind.MODIFIER_FIELD,
                modifier_field_id="type",
            ),
        )
        self.assertEqual(
            variant.operand_layouts[1].bindings[1].allowed_vector_arities,
            (2, 4),
        )
        self.assertEqual(len(variant.operand_type_compatibilities), 6)
        self.assertEqual(
            [
                (
                    entry.target_field_id,
                    entry.special_register_kind,
                    entry.instruction_width,
                    entry.effective_type,
                    dict(entry.availability),
                )
                for entry in variant.operand_type_compatibilities
            ],
            [
                ("src", "tid", 16, "u16", {"ptx": "1.0", "sm": 0}),
                ("src", "ntid", 16, "u16", {"ptx": "1.0", "sm": 0}),
                ("src", "ctaid", 16, "u16", {"ptx": "1.0", "sm": 0}),
                ("src", "nctaid", 16, "u16", {"ptx": "1.0", "sm": 0}),
                ("src", "gridid", 16, "u16", {"ptx": "1.0", "sm": 0}),
                ("src", "gridid", 32, "u32", {"ptx": "1.3", "sm": 0}),
            ],
        )

        vector = instruction.variants[1]
        self.assertEqual(vector.cpp_name, "V4U32")
        self.assertEqual(
            [(field.name, field.cpp_type) for field in vector.fields],
            [
                ("vector", "WithLocs<VectorArity>"),
                ("type", "WithLocs<ScalarType>"),
                ("dst", "WithLocs<ResolvedVectorRegisterRef>"),
                ("src", "WithLocs<ResolvedVectorSpecialRegisterRef>"),
            ],
        )
        for binding in vector.operand_layouts[0].bindings:
            self.assertEqual(binding.allowed_shapes, (ResolvedOperandShape.VECTOR,))
            self.assertEqual(binding.vector_arity_modifier_field_id, "vector")

        predicate = instruction.variants[2]
        self.assertEqual(predicate.cpp_name, "Pred")
        self.assertEqual(
            [(field.name, field.cpp_type) for field in predicate.fields],
            [
                ("type", "ScalarType"),
                ("dst", "WithLocs<ResolvedPredicate>"),
                ("src", "WithLocs<ResolvedPredicateSource>"),
            ],
        )
        self.assertEqual(
            predicate.operand_layouts[0].bindings[0].allowed_shapes,
            (ResolvedOperandShape.PREDICATE,),
        )
        self.assertEqual(
            predicate.operand_layouts[0].bindings[1].allowed_shapes,
            (
                ResolvedOperandShape.PREDICATE,
                ResolvedOperandShape.SPECIAL_REGISTER,
            ),
        )
        for binding in predicate.operand_layouts[0].bindings:
            self.assertEqual(
                binding.type_expression,
                ResolvedOperandTypeExpression(
                    kind=ResolvedOperandTypeExpressionKind.FIXED_SCALAR,
                    scalar_type="pred",
                ),
            )

    def test_ld_and_st_scalar_model_constraints(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        ld = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "ld"
        )
        instruction = from_instruction_spec(ld)

        self.assertEqual(instruction.cpp_name, "Ld")
        self.assertEqual(
            [variant.cpp_name for variant in instruction.variants],
            [
                "GenericScalar",
                "ExplicitScalar",
                "GlobalU32L1Evict",
                "GlobalU32L2CacheHint",
                "GenericVector",
                "ExplicitVector",
                "GlobalNcL1NoAllocateU32",
            ],
        )
        self.assertEqual(ld.variants[1].modifiers[0].presence, "required")
        self.assertEqual(
            [value.value for value in ld.variants[1].modifiers[0].values],
            [
                "const",
                "global",
                "local",
                "param",
                "param::entry",
                "param::func",
                "shared",
            ],
        )
        variant = instruction.variants[0]
        explicit_variant = instruction.variants[1]
        l1_evict_variant = instruction.variants[2]
        cache_hint_variant = instruction.variants[3]
        vector_variant = instruction.variants[4]
        explicit_vector_variant = instruction.variants[5]
        self.assertEqual(variant.cpp_name, "GenericScalar")
        self.assertEqual(
            [(field.name, field.cpp_type) for field in l1_evict_variant.fields],
            [
                ("state_space", "MemoryStateSpace"),
                ("eviction_priority", "WithLocs<EvictionPriority>"),
                ("type", "ScalarType"),
                ("dst", "WithLocs<ResolvedRegisterRef>"),
                ("address", "WithLocs<ResolvedAddress>"),
            ],
        )
        self.assertEqual(
            [(field.name, field.cpp_type) for field in cache_hint_variant.fields],
            [
                ("state_space", "MemoryStateSpace"),
                ("cache_hint", "bool"),
                ("type", "ScalarType"),
                ("dst", "WithLocs<ResolvedRegisterRef>"),
                ("address", "WithLocs<ResolvedAddress>"),
                ("cache_policy", "WithLocs<ResolvedRegisterRef>"),
            ],
        )
        expected_types = [
            "b8",
            "b16",
            "b32",
            "b64",
            "u8",
            "u16",
            "u32",
            "u64",
            "s8",
            "s16",
            "s32",
            "s64",
            "f32",
            "f64",
        ]
        for syntax_variant in (
            ld.variants[0], ld.variants[1], ld.variants[4], ld.variants[5]
        ):
            self.assertEqual(
                [value.value for value in syntax_variant.modifiers[-1].values],
                expected_types,
            )
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [
                ("semantics", "WithLocs<MemoryConsistency>"),
                ("scope", "WithLocs<MemoryScope>"),
                ("mmio", "WithLocs<bool>"),
                ("cache", "WithLocs<CacheOperator>"),
                ("type", "WithLocs<ScalarType>"),
                ("dst", "WithLocs<ResolvedRegisterRef>"),
                ("address", "WithLocs<ResolvedAddress>"),
            ],
        )
        self.assertEqual(
            next(binding for binding in variant.modifier_bindings
                 if binding.source_kind_id == "cache").default_value.value_cpp_type,
            "CacheOperator",
        )
        self.assertEqual(
            next(binding for binding in variant.modifier_bindings
                 if binding.source_kind_id == "cache").default_value.value,
            "unspecified",
        )
        self.assertEqual(
            next(binding for binding in variant.modifier_bindings
                 if binding.source_kind_id == "semantics").default_value.value,
            "omitted",
        )
        self.assertEqual(
            next(binding for binding in variant.modifier_bindings
                 if binding.source_kind_id == "scope").default_value.value,
            "none",
        )
        self.assertEqual(
            [
                (entry.source_kind_id, entry.value_cpp_type, entry.value)
                for entry in variant.modifier_value_availabilities
                if entry.source_kind_id == "cache"
            ],
            [
                ("cache", "CacheOperator", "ca"),
                ("cache", "CacheOperator", "cg"),
                ("cache", "CacheOperator", "cs"),
                ("cache", "CacheOperator", "lu"),
                ("cache", "CacheOperator", "cv"),
            ],
        )
        self.assertEqual(
            [
                (entry.source_kind_id, entry.value_cpp_type, entry.value)
                for entry in variant.modifier_value_availabilities
                if entry.source_kind_id == "semantics"
            ],
            [
                ("semantics", "MemoryConsistency", "weak"),
                ("semantics", "MemoryConsistency", "volatile"),
                ("semantics", "MemoryConsistency", "relaxed"),
                ("semantics", "MemoryConsistency", "acquire"),
            ],
        )
        self.assertIn(
            ("mmio", "bool", True),
            [(entry.source_kind_id, entry.value_cpp_type, entry.value)
             for entry in variant.modifier_value_availabilities],
        )
        self.assertEqual(variant.memory_consistency.semantics_field_id, "semantics")
        self.assertEqual(variant.memory_consistency.address_field_id, "address")
        self.assertEqual(variant.address_alignment.address_field_ids, ("address",))
        self.assertEqual(variant.address_alignment.type_field_id, "type")
        self.assertIsNone(variant.address_alignment.vector_field_id)
        self.assertEqual(
            variant.operand_layouts[0].bindings[0].type_expression,
            ResolvedOperandTypeExpression(
                kind=ResolvedOperandTypeExpressionKind.MODIFIER_FIELD,
                modifier_field_id="type",
            ),
        )
        self.assertEqual(
            variant.operand_layouts[0].bindings[0].register_width_policy,
            ResolvedRegisterWidthPolicy.EQUAL_OR_WIDER,
        )
        address_binding = variant.operand_layouts[0].bindings[1]
        self.assertEqual(address_binding.role, ResolvedOperandRole.ADDRESS)
        self.assertEqual(address_binding.access, ResolvedOperandAccess.READ)
        self.assertEqual(
            address_binding.allowed_shapes,
            (ResolvedOperandShape.ADDRESS,),
        )
        self.assertEqual(
            address_binding.type_expression.kind,
            ResolvedOperandTypeExpressionKind.NONE,
        )
        self.assertEqual(
            address_binding.register_width_policy,
            ResolvedRegisterWidthPolicy.SAME_WIDTH,
        )
        self.assertEqual(
            [
                (entry.value, dict(entry.availability))
                for entry in address_binding.allowed_address_state_spaces
            ],
            [
                ("const", {"ptx": "3.1"}),
                ("global", {}),
                ("local", {}),
                ("shared", {}),
            ],
        )
        self.assertIsNone(address_binding.state_space_modifier_field_id)

        self.assertEqual(explicit_variant.cpp_name, "ExplicitScalar")
        self.assertEqual(
            (
                explicit_variant.modifier_fields[0].name,
                explicit_variant.modifier_fields[0].cpp_type,
                explicit_variant.modifier_fields[0].storage,
            ),
            (
                "state_space",
                "WithLocs<MemoryStateSpace>",
                ResolvedFieldStorage.INSTANCE,
            ),
        )
        self.assertEqual(
            (
                explicit_variant.modifier_fields[1].name,
                explicit_variant.modifier_fields[1].cpp_type,
                explicit_variant.modifier_bindings[1].default_value.value,
            ),
            ("cache", "WithLocs<CacheOperator>", "unspecified"),
        )
        self.assertEqual(
            explicit_variant.operand_layouts[0]
            .bindings[1]
            .state_space_modifier_field_id,
            "state_space",
        )
        self.assertEqual(
            explicit_variant.operand_layouts[0]
            .bindings[1]
            .allowed_address_state_spaces,
            (),
        )
        self.assertEqual(
            explicit_variant.operand_layouts[0]
            .bindings[0]
            .register_width_policy,
            ResolvedRegisterWidthPolicy.EQUAL_OR_WIDER,
        )
        self.assertEqual(
            [
                (entry.value, dict(entry.availability))
                for entry in explicit_variant.modifier_value_availabilities
                if entry.source_kind_id in {"cache", "type"}
            ],
            [
                ("ca", {"ptx": "2.0", "sm": 20}),
                ("cg", {"ptx": "2.0", "sm": 20}),
                ("cs", {"ptx": "2.0", "sm": 20}),
                ("lu", {"ptx": "2.0", "sm": 20}),
                ("cv", {"ptx": "2.0", "sm": 20}),
                ("f64", {"ptx": "1.0", "sm": 13}),
            ],
        )
        load_parameter = (
            explicit_variant.operand_layouts[0].bindings[1].parameter_constraint
        )
        self.assertEqual(load_parameter.direction, "input")
        self.assertEqual(
            dict(load_parameter.function_availability),
            {"ptx": "2.0", "sm": 20},
        )
        self.assertEqual(vector_variant.cpp_name, "GenericVector")
        self.assertEqual(
            [field.name for field in vector_variant.fields],
            ["semantics", "scope", "cache", "vector", "type", "dst", "address"],
        )
        self.assertEqual(vector_variant.memory_consistency.mmio_field_id, "")
        self.assertEqual(vector_variant.address_alignment.vector_field_id, "vector")
        self.assertEqual(
            [value.value for value in next(modifier for modifier in ld.variants[4].modifiers if modifier.name == "vector").values],
            ["v2", "v4", "v8"],
        )
        vector_binding = vector_variant.operand_layouts[0].bindings[0]
        self.assertEqual(vector_binding.allowed_vector_arities, ())
        self.assertEqual(vector_binding.vector_arity_modifier_field_id, "vector")
        self.assertEqual(
            vector_binding.vector_type_policy,
            ResolvedVectorTypePolicy.ELEMENT,
        )
        self.assertTrue(vector_binding.allow_vector_sink)
        self.assertEqual(vector_variant.memory_vector.type_field_id, "type")
        self.assertEqual(vector_variant.memory_vector.vector_field_id, "dst")
        self.assertEqual(
            dict(vector_variant.memory_vector.availability),
            {"ptx": "8.8", "sm": 100},
        )
        self.assertEqual(
            explicit_vector_variant.operand_layouts[0]
            .bindings[0]
            .vector_arity_modifier_field_id,
            "vector",
        )
        load_vector_parameter = (
            explicit_vector_variant.operand_layouts[0]
            .bindings[1]
            .parameter_constraint
        )
        self.assertEqual(load_vector_parameter.direction, "input")
        self.assertEqual(
            dict(load_vector_parameter.function_availability),
            {"ptx": "2.0", "sm": 20},
        )

        st = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "st"
        )
        store = from_instruction_spec(st)
        self.assertEqual(
            [value.value for value in st.variants[1].modifiers[0].values],
            ["global", "local", "param", "param::func", "shared"],
        )
        self.assertEqual(
            [variant.cpp_name for variant in store.variants],
            [
                "GenericScalar",
                "ExplicitScalar",
                "GlobalU32L1Evict",
                "GlobalU32L2CacheHint",
                "GenericVector",
                "ExplicitVector",
            ],
        )
        for syntax_variant in (
            st.variants[0], st.variants[1], st.variants[4], st.variants[5]
        ):
            self.assertEqual(
                [value.value for value in syntax_variant.modifiers[-1].values],
                expected_types,
            )
        self.assertEqual(
            [field.name for field in store.variants[0].fields],
            ["semantics", "scope", "mmio", "cache", "type", "address", "src"],
        )
        self.assertEqual(
            next(binding for binding in store.variants[0].modifier_bindings
                 if binding.source_kind_id == "cache").default_value.value,
            "unspecified",
        )
        self.assertEqual(
            [
                availability.value
                for availability in store.variants[0].modifier_value_availabilities
                if availability.source_kind_id == "cache"
            ],
            ["wb", "cg", "cs", "wt"],
        )
        self.assertEqual(
            [
                entry.value
                for entry in store.variants[0]
                .operand_layouts[0]
                .bindings[0]
                .allowed_address_state_spaces
            ],
            ["global", "local", "shared"],
        )
        self.assertEqual(
            store.variants[1]
            .operand_layouts[0]
            .bindings[0]
            .state_space_modifier_field_id,
            "state_space",
        )
        self.assertEqual(
            store.variants[1].modifier_bindings[1].default_value.value,
            "unspecified",
        )
        self.assertEqual(
            store.variants[0].operand_layouts[0].bindings[1].type_expression,
            ResolvedOperandTypeExpression(
                kind=ResolvedOperandTypeExpressionKind.MODIFIER_FIELD,
                modifier_field_id="type",
            ),
        )
        self.assertEqual(
            store.variants[0]
            .operand_layouts[0]
            .bindings[1]
            .register_width_policy,
            ResolvedRegisterWidthPolicy.EQUAL_OR_WIDER,
        )
        self.assertEqual(
            store.variants[1]
            .operand_layouts[0]
            .bindings[1]
            .register_width_policy,
            ResolvedRegisterWidthPolicy.EQUAL_OR_WIDER,
        )
        self.assertEqual(
            [
                (entry.value, dict(entry.availability))
                for entry in store.variants[1].modifier_value_availabilities
                if entry.source_kind_id in {"cache", "type"}
            ],
            [
                ("wb", {"ptx": "2.0", "sm": 20}),
                ("cg", {"ptx": "2.0", "sm": 20}),
                ("cs", {"ptx": "2.0", "sm": 20}),
                ("wt", {"ptx": "2.0", "sm": 20}),
                ("f64", {"ptx": "1.0", "sm": 13}),
            ],
        )
        store_parameter = (
            store.variants[1].operand_layouts[0].bindings[0].parameter_constraint
        )
        self.assertEqual(store_parameter.direction, "return")
        self.assertEqual(
            dict(store_parameter.function_availability),
            {"ptx": "2.0", "sm": 20},
        )
        store_vector = store.variants[4]
        self.assertEqual(
            [field.name for field in store_vector.fields],
            ["semantics", "scope", "cache", "vector", "type", "address", "src"],
        )
        self.assertEqual(store_vector.memory_consistency.mmio_field_id, "")
        self.assertEqual(
            [value.value for value in next(modifier for modifier in st.variants[4].modifiers if modifier.name == "vector").values],
            ["v2", "v4", "v8"],
        )
        store_vector_binding = store_vector.operand_layouts[0].bindings[1]
        self.assertEqual(store_vector_binding.allowed_vector_arities, ())
        self.assertEqual(
            store_vector_binding.vector_arity_modifier_field_id,
            "vector",
        )
        self.assertEqual(
            store_vector_binding.vector_type_policy,
            ResolvedVectorTypePolicy.ELEMENT,
        )
        self.assertTrue(store_vector_binding.allow_vector_sink)
        self.assertEqual(store_vector.memory_vector.vector_field_id, "src")
        store_vector_parameter = (
            store.variants[5].operand_layouts[0].bindings[0].parameter_constraint
        )
        self.assertEqual(store_vector_parameter.direction, "return")
        self.assertEqual(
            dict(store_vector_parameter.function_availability),
            {"ptx": "2.0", "sm": 20},
        )

    def test_ldu_global_u32_model(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        ldu = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "ldu"
        )
        resolved = from_instruction_spec(ldu)

        self.assertEqual(resolved.cpp_name, "Ldu")
        self.assertEqual(
            [variant.cpp_name for variant in resolved.variants], ["GlobalU32"]
        )
        variant = resolved.variants[0]
        self.assertEqual(dict(variant.availability), {"ptx": "2.0", "sm": 0})
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [
                ("state_space", "MemoryStateSpace"),
                ("type", "ScalarType"),
                ("dst", "WithLocs<ResolvedRegisterRef>"),
                ("address", "WithLocs<ResolvedAddress>"),
            ],
        )
        self.assertEqual(
            variant.operand_layouts[0].bindings[0].register_width_policy,
            ResolvedRegisterWidthPolicy.EQUAL_OR_WIDER,
        )
        self.assertEqual(
            variant.operand_layouts[0].bindings[1].state_space_modifier_field_id,
            "state_space",
        )

    def test_prefetch_global_l1_model(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        prefetch = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "prefetch"
        )
        resolved = from_instruction_spec(prefetch)

        self.assertEqual(resolved.cpp_name, "Prefetch")
        self.assertEqual(
            [variant.cpp_name for variant in resolved.variants], ["GlobalL1"]
        )
        variant = resolved.variants[0]
        self.assertEqual(dict(variant.availability), {"ptx": "2.0", "sm": 20})
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [
                ("state_space", "MemoryStateSpace"),
                ("l1", "bool"),
                ("address", "WithLocs<ResolvedAddress>"),
            ],
        )
        self.assertEqual(
            variant.operand_layouts[0].bindings[0].state_space_modifier_field_id,
            "state_space",
        )

    def test_prefetchu_l1_model(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        prefetchu = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "prefetchu"
        )
        resolved = from_instruction_spec(prefetchu)

        self.assertEqual(resolved.cpp_name, "Prefetchu")
        self.assertEqual([variant.cpp_name for variant in resolved.variants], ["L1"])
        variant = resolved.variants[0]
        self.assertEqual(dict(variant.availability), {"ptx": "2.0", "sm": 20})
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [("l1", "bool"), ("address", "WithLocs<ResolvedAddress>")],
        )
        self.assertEqual(
            [state_space.value for state_space in variant.operand_layouts[0]
             .bindings[0].allowed_address_state_spaces],
            ["generic"],
        )

    def test_createpolicy_fractional_l2_evict_last_model(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        createpolicy = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "createpolicy"
        )
        resolved = from_instruction_spec(createpolicy)

        self.assertEqual(resolved.cpp_name, "Createpolicy")
        self.assertEqual(
            [variant.cpp_name for variant in resolved.variants],
            ["FractionalL2EvictLastB64"],
        )
        variant = resolved.variants[0]
        self.assertEqual(dict(variant.availability), {"ptx": "7.4", "sm": 80})
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [
                ("fractional", "bool"),
                ("eviction_priority", "EvictionPriority"),
                ("type", "ScalarType"),
                ("dst", "WithLocs<ResolvedRegisterRef>"),
                ("fraction", "WithLocs<ResolvedImmediate>"),
            ],
        )
        self.assertEqual(variant.immediate_value.values, (1056964608,))
        self.assertEqual(
            variant.operand_layouts[0].bindings[0].register_width_policy,
            ResolvedRegisterWidthPolicy.SAME_WIDTH,
        )

    def test_applypriority_global_l2_evict_normal_model(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        applypriority = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "applypriority"
        )
        resolved = from_instruction_spec(applypriority)

        self.assertEqual(resolved.cpp_name, "Applypriority")
        self.assertEqual(
            [variant.cpp_name for variant in resolved.variants],
            ["GlobalL2EvictNormal"],
        )
        variant = resolved.variants[0]
        self.assertEqual(dict(variant.availability), {"ptx": "7.4", "sm": 80})
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [
                ("state_space", "MemoryStateSpace"),
                ("eviction_priority", "EvictionPriority"),
                ("address", "WithLocs<ResolvedAddress>"),
                ("size", "WithLocs<ResolvedImmediate>"),
            ],
        )
        self.assertEqual(variant.immediate_value.values, (128,))
        self.assertEqual(variant.address_alignment.alignment, 128)

    def test_discard_global_l2_model(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        discard = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "discard"
        )
        resolved = from_instruction_spec(discard)

        self.assertEqual(resolved.cpp_name, "Discard")
        self.assertEqual(
            [variant.cpp_name for variant in resolved.variants], ["GlobalL2"]
        )
        variant = resolved.variants[0]
        self.assertEqual(dict(variant.availability), {"ptx": "7.4", "sm": 80})
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [
                ("state_space", "MemoryStateSpace"),
                ("l2", "bool"),
                ("address", "WithLocs<ResolvedAddress>"),
                ("size", "WithLocs<ResolvedImmediate>"),
            ],
        )
        self.assertEqual(variant.immediate_value.values, (128,))
        self.assertEqual(variant.address_alignment.alignment, 128)

    def test_setmaxnreg_inc_sync_aligned_model_and_generator(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        setmaxnreg = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "setmaxnreg"
        )
        resolved = from_instruction_spec(setmaxnreg)
        self.assertEqual(resolved.cpp_name, "Setmaxnreg")
        self.assertEqual(
            [variant.cpp_name for variant in resolved.variants],
            ["IncSyncAlignedU32"],
        )
        variant = resolved.variants[0]
        self.assertEqual(
            dict(variant.availability),
            {"any_of": [
                {"ptx": "8.0", "sm": 90, "target": "sm_90a"},
                {"ptx": "8.6", "sm": 100, "target": "sm_100a"},
                {"ptx": "8.8", "sm": 100, "family": "sm_100f"},
                {"ptx": "8.8", "sm": 120, "family": "sm_120f"},
            ]},
        )
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [
                ("inc", "bool"),
                ("sync", "bool"),
                ("aligned", "bool"),
                ("type", "ScalarType"),
                ("count", "WithLocs<ResolvedImmediate>"),
            ],
        )
        self.assertEqual(
            [(constraint.operand_field_id, constraint.minimum, constraint.maximum)
             for constraint in variant.immediate_ranges],
            [("count", 24, 256)],
        )
        self.assertEqual(variant.immediate_multiple_of.operand_field_id, "count")
        self.assertEqual(variant.immediate_multiple_of.divisor, 8)

        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir_control_flow.gen.cpp"
            descriptor_path = Path(directory) / "resolved_ir_checker_descriptor.gen.cpp"
            generate_resolved_ir_source(
                database, category="control_flow", output_path=output_path
            )
            generate_resolved_checker_descriptor_source(
                database, output_path=descriptor_path
            )
            source = output_path.read_text(encoding="utf-8")
            descriptor = descriptor_path.read_text(encoding="utf-8")
        self.assertIn("check_immediate_multiple_of(", source)
        start = source.index("check_inc_sync_aligned_u32")
        setmaxnreg_check = source[start:source.index("static_assert", start)]
        self.assertEqual(setmaxnreg_check.count("check_immediate_multiple_of("), 1)
        self.assertEqual(setmaxnreg_check.count("check_immediate_range("), 1)
        self.assertIn("std::expected<Setmaxnreg, ResolveDiagnostic>", source)
        self.assertIn(".any_of_count = 4", descriptor)
        self.assertIn('.required_family = "sm_100f",', descriptor)
        self.assertIn('.required_family = "sm_120f",', descriptor)
        self.assertIn('.operand_field_id = "count",', descriptor)
        self.assertIn(".divisor = uint64_t{8ULL},", descriptor)

    def test_cp_async_ca_shared_global_model(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        cp = next(instruction for instruction in database.instructions if instruction.opcode == "cp")
        resolved = from_instruction_spec(cp)
        variant = resolved.variants[0]

        self.assertEqual(resolved.cpp_name, "Cp")
        self.assertEqual(
            [candidate.cpp_name for candidate in resolved.variants],
            ["AsyncCaSharedGlobal", "AsyncCommitGroup", "AsyncWaitGroup", "AsyncWaitAll"],
        )
        self.assertEqual(variant.cpp_name, "AsyncCaSharedGlobal")
        self.assertEqual(dict(variant.availability), {"ptx": "7.0", "sm": 80})
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [
                ("async", "bool"),
                ("ca", "bool"),
                ("shared", "bool"),
                ("global", "bool"),
                ("dst", "WithLocs<ResolvedAddress>"),
                ("src", "WithLocs<ResolvedAddress>"),
                ("cp_size", "WithLocs<ResolvedImmediate>"),
            ],
        )
        self.assertEqual(variant.immediate_value.operand_field_id, "cp_size")
        self.assertEqual(variant.immediate_value.values, (4, 8, 16))
        self.assertEqual(variant.address_alignment.address_field_ids, ("dst", "src"))
        self.assertEqual(variant.address_alignment.immediate_operand_field_id, "cp_size")
        self.assertEqual(
            [value.value for value in variant.operand_layouts[0].bindings[0].allowed_address_state_spaces],
            ["shared"],
        )
        self.assertEqual(
            [value.value for value in variant.operand_layouts[0].bindings[1].allowed_address_state_spaces],
            ["global"],
        )

    def test_cp_async_commit_group_model(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        cp = next(instruction for instruction in database.instructions if instruction.opcode == "cp")
        variant = from_instruction_spec(cp).variants[1]

        self.assertEqual(variant.cpp_name, "AsyncCommitGroup")
        self.assertEqual(dict(variant.availability), {"ptx": "7.0", "sm": 80})
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [("async", "bool"), ("commit_group", "bool")],
        )
        self.assertEqual(variant.operand_layouts[0].bindings, ())

    def test_cp_async_wait_group_model(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        cp = next(instruction for instruction in database.instructions if instruction.opcode == "cp")
        variant = from_instruction_spec(cp).variants[2]

        self.assertEqual(variant.cpp_name, "AsyncWaitGroup")
        self.assertEqual(dict(variant.availability), {"ptx": "7.0", "sm": 80})
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [
                ("async", "bool"),
                ("wait_group", "bool"),
                ("n", "WithLocs<ResolvedImmediate>"),
            ],
        )
        self.assertIsNone(variant.immediate_value)
        self.assertEqual(
            [(constraint.operand_field_id, constraint.minimum, constraint.maximum)
             for constraint in variant.immediate_ranges],
            [("n", 0, None)],
        )

    def test_cp_async_wait_all_model(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        cp = next(instruction for instruction in database.instructions if instruction.opcode == "cp")
        variant = from_instruction_spec(cp).variants[3]

        self.assertEqual(variant.cpp_name, "AsyncWaitAll")
        self.assertEqual(dict(variant.availability), {"ptx": "7.0", "sm": 80})
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [("async", "bool"), ("wait_all", "bool")],
        )
        self.assertEqual(variant.operand_layouts[0].bindings, ())

    def test_ldmatrix_sync_aligned_m8n8_x2_shared_b16_model(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        ldmatrix = next(
            instruction for instruction in database.instructions if instruction.opcode == "ldmatrix"
        )
        variant = from_instruction_spec(ldmatrix).variants[0]

        self.assertEqual(variant.cpp_name, "SyncAlignedM8n8X2SharedB16")
        self.assertEqual(dict(variant.availability), {"ptx": "6.5", "sm": 75})
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [
                ("sync", "bool"),
                ("aligned", "bool"),
                ("m8n8", "bool"),
                ("x2", "bool"),
                ("shared", "bool"),
                ("type", "ScalarType"),
                ("dst", "WithLocs<ResolvedRegisterVector>"),
                ("address", "WithLocs<ResolvedAddress>"),
            ],
        )
        dst = variant.operand_layouts[0].bindings[0]
        self.assertEqual(
            dst.type_expression,
            ResolvedOperandTypeExpression(
                kind=ResolvedOperandTypeExpressionKind.FIXED_SCALAR,
                scalar_type="b32",
            ),
        )
        self.assertEqual(dst.allowed_vector_arities, (2,))
        self.assertEqual(dst.vector_type_policy.value, "Element")
        self.assertEqual(dst.register_width_policy, ResolvedRegisterWidthPolicy.SAME_WIDTH)
        self.assertEqual(
            [value.value for value in variant.operand_layouts[0].bindings[1].allowed_address_state_spaces],
            ["shared"],
        )
        self.assertEqual(variant.address_alignment.address_field_ids, ("address",))
        self.assertEqual(variant.address_alignment.alignment, 16)

    def test_mma_sync_aligned_m16n8k8_row_col_f32_f16_f16_f32_model(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        mma = next(
            instruction for instruction in database.instructions if instruction.opcode == "mma"
        )
        variant = from_instruction_spec(mma).variants[0]

        self.assertEqual(variant.cpp_name, "SyncAlignedM16n8k8RowColF32F16F16F32")
        self.assertEqual(dict(variant.availability), {"ptx": "6.5", "sm": 75})
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [
                ("sync", "bool"),
                ("aligned", "bool"),
                ("m16n8k8", "bool"),
                ("row", "bool"),
                ("col", "bool"),
                ("d_type", "ScalarType"),
                ("a_type", "ScalarType"),
                ("b_type", "ScalarType"),
                ("c_type", "ScalarType"),
                ("dst", "WithLocs<ResolvedRegisterVector>"),
                ("a", "WithLocs<ResolvedRegisterVector>"),
                ("b", "WithLocs<ResolvedRegisterVector>"),
                ("c", "WithLocs<ResolvedRegisterVector>"),
            ],
        )
        bindings = variant.operand_layouts[0].bindings
        self.assertEqual(
            [binding.type_expression for binding in bindings],
            [
                ResolvedOperandTypeExpression(
                    kind=ResolvedOperandTypeExpressionKind.FIXED_SCALAR,
                    scalar_type=scalar_type,
                )
                for scalar_type in ("f32", "f16x2", "f16x2", "f32")
            ],
        )
        self.assertEqual(
            [binding.allowed_vector_arities for binding in bindings],
            [(4,), (2,), (1,), (4,)],
        )
        self.assertEqual(
            [binding.vector_type_policy for binding in bindings],
            [ResolvedVectorTypePolicy.ELEMENT] * 4,
        )
        self.assertEqual(
            [binding.register_width_policy for binding in bindings],
            [ResolvedRegisterWidthPolicy.SAME_WIDTH] * 4,
        )

        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir_matrix.gen.cpp"
            generate_resolved_ir_source(
                database, category="matrix", output_path=output_path
            )
            source = output_path.read_text(encoding="utf-8")
        self.assertIn("SyncAlignedM16n8k8RowColF32F16F16F32", source)
        self.assertIn("std::expected<Mma, ResolveDiagnostic>", source)

    def test_cp_generator_emits_immediate_value_checker(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir_data_movement.gen.cpp"
            descriptor_path = Path(directory) / "resolved_ir_checker_descriptor.gen.cpp"
            generate_resolved_ir_source(
                database, category="data_movement", output_path=output_path
            )
            generate_resolved_checker_descriptor_source(
                database, output_path=descriptor_path
            )
            source = output_path.read_text(encoding="utf-8")
            descriptor = descriptor_path.read_text(encoding="utf-8")

        self.assertIn("check_immediate_value(", source)
        self.assertIn("check_immediate_range(", source)
        self.assertIn("check_address_alignment(", source)
        start = source.index("check_async_ca_shared_global")
        cp_check = source[start:source.index("static_assert", start)]
        self.assertEqual(cp_check.count("check_address_alignment("), 1)
        self.assertEqual(cp_check.count("check_immediate_value("), 1)
        self.assertLess(
            cp_check.index("check_address_alignment("),
            cp_check.index("check_immediate_value("),
        )
        start = source.index("check_async_wait_group")
        wait_group_check = source[start:source.index("static_assert", start)]
        self.assertEqual(wait_group_check.count("check_immediate_range("), 1)
        self.assertIn("selected.cp_size.value.bits", source)
        self.assertIn("std::expected<Cp, ResolveDiagnostic>", source)
        self.assertIn("AsyncCommitGroup", source)
        self.assertIn("AsyncWaitGroup", source)
        self.assertIn("AsyncWaitAll", source)
        self.assertIn(
            "AsyncCaSharedGlobal_immediate_value_values = "
            "{{uint64_t{4ULL}, uint64_t{8ULL}, uint64_t{16ULL}}};",
            descriptor,
        )
        self.assertIn('.operand_field_id = "cp_size",', descriptor)
        self.assertIn('AsyncCaSharedGlobal_address_alignment_address_fields = {{"dst", "src"}};', descriptor)
        self.assertIn('.immediate_operand_field_id = "cp_size",', descriptor)
        self.assertIn('.operand_field_id = "n",', descriptor)
        self.assertIn('.minimum = uint64_t{0ULL},', descriptor)
        self.assertIn('.has_maximum = false,', descriptor)
        self.assertIn('.maximum = ~uint64_t{0},', descriptor)

    def test_membar_cta_model(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        membar = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "membar"
        )
        resolved = from_instruction_spec(membar)

        self.assertEqual(resolved.cpp_name, "Membar")
        self.assertEqual(
            [variant.cpp_name for variant in resolved.variants], ["Cta"]
        )
        variant = resolved.variants[0]
        self.assertEqual(dict(variant.availability), {"ptx": "1.4", "sm": 0})
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [("scope", "MemoryScope")],
        )
        self.assertEqual(variant.operand_layouts[0].bindings, ())

    def test_fence_acq_rel_cta_model(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        fence = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "fence"
        )
        resolved = from_instruction_spec(fence)

        self.assertEqual(resolved.cpp_name, "Fence")
        self.assertEqual(
            [variant.cpp_name for variant in resolved.variants], ["AcqRelCta"]
        )
        variant = resolved.variants[0]
        self.assertEqual(dict(variant.availability), {"ptx": "6.0", "sm": 70})
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [("semantics", "MemoryConsistency"), ("scope", "MemoryScope")],
        )
        self.assertEqual(variant.operand_layouts[0].bindings, ())

    def test_atom_global_relaxed_cta_add_u32_model(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        atom = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "atom"
        )
        resolved = from_instruction_spec(atom)

        self.assertEqual(resolved.cpp_name, "Atom")
        self.assertEqual(
            [variant.cpp_name for variant in resolved.variants],
            ["GlobalRelaxedCtaAddU32"],
        )
        variant = resolved.variants[0]
        self.assertEqual(dict(variant.availability), {"ptx": "6.0", "sm": 70})
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [
                ("state_space", "MemoryStateSpace"),
                ("semantics", "MemoryConsistency"),
                ("scope", "MemoryScope"),
                ("add", "bool"),
                ("type", "ScalarType"),
                ("dst", "WithLocs<ResolvedRegisterRef>"),
                ("address", "WithLocs<ResolvedAddress>"),
                ("src", "WithLocs<ResolvedRegisterRef>"),
            ],
        )
        bindings = variant.operand_layouts[0].bindings
        self.assertEqual(
            (bindings[0].register_width_policy, bindings[2].register_width_policy),
            (
                ResolvedRegisterWidthPolicy.SAME_WIDTH,
                ResolvedRegisterWidthPolicy.SAME_WIDTH,
            ),
        )
        self.assertEqual(bindings[1].state_space_modifier_field_id, "state_space")

    def test_red_global_relaxed_cta_add_u32_model(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        red = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "red"
        )
        resolved = from_instruction_spec(red)

        self.assertEqual(resolved.cpp_name, "Red")
        self.assertEqual(
            [variant.cpp_name for variant in resolved.variants],
            ["GlobalRelaxedCtaAddU32"],
        )
        variant = resolved.variants[0]
        self.assertEqual(dict(variant.availability), {"ptx": "6.0", "sm": 70})
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [
                ("state_space", "MemoryStateSpace"),
                ("semantics", "MemoryConsistency"),
                ("scope", "MemoryScope"),
                ("add", "bool"),
                ("type", "ScalarType"),
                ("address", "WithLocs<ResolvedAddress>"),
                ("src", "WithLocs<ResolvedRegisterRef>"),
            ],
        )
        bindings = variant.operand_layouts[0].bindings
        self.assertEqual(len(bindings), 2)
        self.assertEqual(
            bindings[1].register_width_policy, ResolvedRegisterWidthPolicy.SAME_WIDTH
        )
        self.assertEqual(bindings[0].state_space_modifier_field_id, "state_space")

    def test_activemask_b32_model(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        activemask = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "activemask"
        )
        resolved = from_instruction_spec(activemask)

        self.assertEqual(resolved.cpp_name, "Activemask")
        self.assertEqual(
            [variant.cpp_name for variant in resolved.variants], ["B32"]
        )
        variant = resolved.variants[0]
        self.assertEqual(dict(variant.availability), {"ptx": "6.2", "sm": 30})
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [("type", "ScalarType"), ("dst", "WithLocs<ResolvedRegisterRef>")],
        )
        self.assertEqual(
            variant.operand_layouts[0].bindings[0].register_width_policy,
            ResolvedRegisterWidthPolicy.EXACT,
        )

    def test_vote_sync_ballot_b32_model(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        vote = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "vote"
        )
        resolved = from_instruction_spec(vote)

        self.assertEqual(resolved.cpp_name, "Vote")
        self.assertEqual(
            [variant.cpp_name for variant in resolved.variants], ["SyncBallotB32"]
        )
        variant = resolved.variants[0]
        self.assertEqual(dict(variant.availability), {"ptx": "6.0", "sm": 30})
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [
                ("sync", "bool"),
                ("ballot", "bool"),
                ("type", "ScalarType"),
                ("dst", "WithLocs<ResolvedRegisterRef>"),
                ("predicate", "WithLocs<ResolvedPredicate>"),
                ("membermask", "WithLocs<RegOrImm>"),
            ],
        )
        self.assertEqual(
            variant.operand_layouts[0].bindings[0].register_width_policy,
            ResolvedRegisterWidthPolicy.EXACT,
        )

    def test_shfl_sync_idx_b32_model(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        shfl = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "shfl"
        )
        resolved = from_instruction_spec(shfl)

        self.assertEqual(resolved.cpp_name, "Shfl")
        variant = resolved.variants[0]
        self.assertEqual(variant.cpp_name, "SyncIdxB32")
        self.assertEqual(dict(variant.availability), {"ptx": "6.0", "sm": 30})
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [
                ("sync", "bool"),
                ("idx", "bool"),
                ("type", "ScalarType"),
                ("dst", "WithLocs<ResolvedShflSyncDestination>"),
                ("src", "WithLocs<ResolvedRegisterRef>"),
                ("lane", "WithLocs<RegOrImm>"),
                ("clamp", "WithLocs<RegOrImm>"),
                ("membermask", "WithLocs<RegOrImm>"),
            ],
        )
        self.assertEqual(
            variant.operand_layouts[0].bindings[0].allowed_shapes,
            (ResolvedOperandShape.SHFL_DESTINATION,),
        )
        self.assertEqual(
            variant.operand_layouts[0].bindings[0].register_width_policy,
            ResolvedRegisterWidthPolicy.SAME_WIDTH,
        )

    def test_shfl_generator_emits_pair_operand_view(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir_data_movement.gen.cpp"
            generate_resolved_ir_source(
                database,
                category="data_movement",
                output_path=output_path,
            )
            source = output_path.read_text(encoding="utf-8")

        self.assertIn("ResolvedShflSyncDestination", source)
        self.assertIn(
            ".actual_shape = check_end::OperandShape::ShflDestination,", source
        )
        self.assertIn("selected.dst.value.data\n                      ?", source)

    def test_setp_generator_emits_predicate_pair_operand_view(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir_arithmetic.gen.cpp"
            generate_resolved_ir_source(
                database,
                category="arithmetic",
                output_path=output_path,
            )
            source = output_path.read_text(encoding="utf-8")

        self.assertIn("ResolvedPredicatePair", source)
        self.assertIn(
            ".actual_shape = check_end::OperandShape::PredicatePair,", source
        )
        self.assertIn("selected.dst.value.first.register_ref.declared_type", source)
        self.assertIn("selected.dst.value.second.register_ref.declared_type", source)

    def test_ld_and_st_cache_defaults_use_unspecified_sentinel(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )

        for opcode in ("ld", "st"):
            spec = next(
                instruction
                for instruction in database.instructions
                if instruction.opcode == opcode
            )
            resolved = from_instruction_spec(spec)
            for variant in resolved.variants:
                cache_binding = next(
                    (
                        binding
                        for binding in variant.modifier_bindings
                        if binding.source_kind_id == "cache"
                    ),
                    None,
                )
                if cache_binding is None:
                    continue
                self.assertIsNotNone(cache_binding.default_value)
                assert cache_binding.default_value is not None
                self.assertEqual(
                    cache_binding.default_value.value_cpp_type,
                    "CacheOperator",
                )
                self.assertEqual(cache_binding.default_value.value, "unspecified")

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
        self.assertIn("#include <optional>", source)
        self.assertIn('#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>', source)
        self.assertIn('#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker.hpp>', source)
        self.assertIn("namespace ptx_frontend::resolved_ir {", source)
        self.assertEqual(source.count("namespace checker {"), 1)
        self.assertIn("struct Add {", source)
        self.assertIn("struct Atom {", source)
        self.assertIn("struct Activemask {", source)
        self.assertIn("struct Vote {", source)
        self.assertIn("struct Red {", source)
        self.assertIn("struct Bar {", source)
        self.assertIn("struct Membar {", source)
        self.assertIn("struct Fence {", source)
        self.assertIn("struct Bra {", source)
        self.assertIn("struct Ret {", source)
        self.assertIn("struct Exit {", source)
        self.assertIn("struct Trap {", source)
        self.assertIn("struct And {", source)
        self.assertIn("struct Or {", source)
        self.assertIn("struct Xor {", source)
        self.assertIn("struct Not {", source)
        self.assertIn("struct Shl {", source)
        self.assertIn("struct Shr {", source)
        self.assertIn("struct Set {", source)
        self.assertIn("struct Setp {", source)
        self.assertIn("struct Selp {", source)
        self.assertIn("struct Cvta {", source)
        self.assertIn("struct GlobalU64 {", source)
        self.assertIn("struct ToGlobalU64 {", source)
        self.assertIn("struct Cvt {", source)
        self.assertIn("struct Mul {", source)
        self.assertIn("struct LoU32 {", source)
        self.assertIn("struct RnF32 {", source)
        self.assertIn("struct HiU32 {", source)
        self.assertIn("struct WideU32 {", source)
        self.assertIn("struct WideS32 {", source)
        self.assertIn("struct Mad {", source)
        self.assertIn("struct LoS32 {", source)
        self.assertIn("struct Fma {", source)
        self.assertIn("struct RnF16 {", source)
        self.assertIn("struct Div {", source)
        self.assertIn("struct RnF32F64 {", source)
        self.assertIn("struct RnF32U32 {", source)
        self.assertIn("struct RziU32F32 {", source)
        self.assertIn(
            "inline static constexpr RoundingMode rounding = RoundingMode::Rn;",
            source,
        )
        self.assertIn(
            "inline static constexpr ScalarType dst_type = ScalarType::F32;",
            source,
        )
        self.assertIn(
            "inline static constexpr ScalarType src_type = ScalarType::F64;",
            source,
        )
        self.assertIn(
            "inline static constexpr RoundingMode rounding = RoundingMode::Rzi;",
            source,
        )
        self.assertIn("struct LtU32 {", source)
        self.assertIn("struct LtAndU32 {", source)
        self.assertIn("struct GeS32 {", source)
        self.assertIn("WithLocs<ComparisonOperator> comparison;", source)
        self.assertIn("WithLocs<BooleanOperator> boolean;", source)
        self.assertIn("struct Mov {", source)
        self.assertIn("struct Ld {", source)
        self.assertIn("struct Ldu {", source)
        self.assertIn("struct Prefetch {", source)
        self.assertIn("WithLocs<ResolvedBranchTarget> target;", source)
        self.assertEqual(source.count("WithLocs<ResolvedMovSource> src;"), 1)
        self.assertIn("WithLocs<ResolvedAddress> address;", source)
        self.assertIn(
            "std::optional<WithLocs<ResolvedPredicate>> execution_predicate;",
            source,
        )
        self.assertIn("using ResolvedInstruction = std::variant<", source)
        self.assertIn("struct ResolvedFunction {", source)
        self.assertIn("struct ResolvedModule {", source)
        self.assertIn("binding::SymbolTable symbols;", source)
        self.assertIn("binding::SymbolId symbol_id;", source)
        self.assertIn("resolveInstruction(", source)
        self.assertIn("const ResolveContext& context);", source)
        self.assertIn("resolveModule(const syntax_ast::AstModule& ast);", source)
        self.assertIn("enum class VariantType {", source)
        self.assertIn("struct IntegerNoSat {", source)
        self.assertIn("ResolvedOperandLayoutTag operand_layout;", source)
        self.assertIn("WithLocs<ScalarType> type;", source)
        self.assertIn("WithLocs<bool> saturate;", source)
        self.assertIn("inline static constexpr bool saturate = true;", source)
        self.assertIn("struct Sat {", source)
        self.assertNotIn("struct SatS32 {", source)
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
        self.assertNotIn("selectVariant<Add>", source)
        self.assertIn(
            "template <>\nstd::expected<Add, ResolveDiagnostic>\n"
            "resolve<Add>(const syntax_ast::AstInstruction& ast,\n"
            "    const ResolveContext* context);",
            source,
        )
        self.assertIn(
            "template <>\nCheckResult check<Add>(\n"
            "    const Add& instruction, const Context& context);",
            source,
        )
        self.assertNotIn("resolve_fields(", source)
        self.assertNotIn("const auto check_integer_no_sat =", source)
        self.assertNotIn("std::visit(detail::Overloaded{", source)
        self.assertNotIn("AddResolvedDescriptorStorage", source)
        self.assertIn("}  // namespace ptx_frontend::resolved_ir", source)

    def test_generate_resolved_instruction_dispatch_source(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )

        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir_dispatch.gen.cpp"
            generate_resolved_dispatch_source(
                database,
                output_path=output_path,
            )
            source = output_path.read_text(encoding="utf-8")

        self.assertIn('#include "resolved_ir.gen.hpp"', source)
        self.assertIn("resolveInstruction(const syntax_ast::AstInstruction& ast)", source)
        self.assertIn('ast.opcode.syntax.text == "add"', source)
        self.assertIn("resolve<Add>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "atom"', source)
        self.assertIn("resolve<Atom>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "activemask"', source)
        self.assertIn("resolve<Activemask>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "vote"', source)
        self.assertIn("resolve<Vote>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "red"', source)
        self.assertIn("resolve<Red>(ast, context)", source)
        self.assertIn("namespace {", source)
        self.assertIn('ast.opcode.syntax.text == "sub"', source)
        self.assertIn("resolve<Sub>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "mul"', source)
        self.assertIn("resolve<Mul>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "mad"', source)
        self.assertIn("resolve<Mad>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "fma"', source)
        self.assertIn("resolve<Fma>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "div"', source)
        self.assertIn("resolve<Div>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "bar"', source)
        self.assertIn('ast.opcode.syntax.text == "bra"', source)
        self.assertIn("resolve<Bra>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "ret"', source)
        self.assertIn("resolve<Ret>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "exit"', source)
        self.assertIn("resolve<Exit>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "trap"', source)
        self.assertIn("resolve<Trap>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "and"', source)
        self.assertIn("resolve<And>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "or"', source)
        self.assertIn("resolve<Or>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "xor"', source)
        self.assertIn("resolve<Xor>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "not"', source)
        self.assertIn("resolve<Not>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "shl"', source)
        self.assertIn("resolve<Shl>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "shr"', source)
        self.assertIn("resolve<Shr>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "set"', source)
        self.assertIn("resolve<Set>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "setp"', source)
        self.assertIn("resolve<Setp>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "selp"', source)
        self.assertIn("resolve<Selp>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "cvta"', source)
        self.assertIn("resolve<Cvta>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "cvt"', source)
        self.assertIn("resolve<Cvt>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "mov"', source)
        self.assertIn("resolve<Mov>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "ld"', source)
        self.assertIn("resolve<Ld>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "ldu"', source)
        self.assertIn("resolve<Ldu>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "membar"', source)
        self.assertIn("resolve<Membar>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "fence"', source)
        self.assertIn("resolve<Fence>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "prefetch"', source)
        self.assertIn("resolve<Prefetch>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "prefetchu"', source)
        self.assertIn("resolve<Prefetchu>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "createpolicy"', source)
        self.assertIn("resolve<Createpolicy>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "applypriority"', source)
        self.assertIn("resolve<Applypriority>(ast, context)", source)
        self.assertIn('ast.opcode.syntax.text == "discard"', source)
        self.assertIn("resolve<Discard>(ast, context)", source)
        self.assertIn("Unknown PTX opcode", source)

    def test_generate_control_flow_resolved_ir_source(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )

        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir_control_flow.gen.cpp"
            generate_resolved_ir_source(
                database,
                category="control_flow",
                output_path=output_path,
            )
            source = output_path.read_text(encoding="utf-8")

        self.assertIn("std::expected<Bra, ResolveDiagnostic>", source)
        self.assertIn(
            ".actual_shape = check_end::OperandShape::BranchTarget", source
        )
        self.assertIn(".target = resolved_operand<ResolvedBranchTarget>", source)
        self.assertIn("CheckResult check<Bra>(", source)
        self.assertIn("std::expected<Ret, ResolveDiagnostic>", source)
        self.assertIn("CheckResult check<Ret>(", source)
        self.assertIn("std::expected<Exit, ResolveDiagnostic>", source)
        self.assertIn("CheckResult check<Exit>(", source)
        self.assertIn("std::expected<Trap, ResolveDiagnostic>", source)
        self.assertIn("CheckResult check<Trap>(", source)

    def test_generate_data_movement_resolved_ir_source(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )

        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir_data_movement.gen.cpp"
            generate_resolved_ir_source(
                database,
                category="data_movement",
                output_path=output_path,
            )
            source = output_path.read_text(encoding="utf-8")

        self.assertIn("std::expected<Mov, ResolveDiagnostic>", source)
        self.assertIn(
            ".src = resolved_operand<ResolvedMovSource>", source
        )
        self.assertIn(
            ".actual_shape = check_end::OperandShape::SpecialRegister", source
        )
        self.assertIn(
            "base::metadata(special_register->id)",
            source,
        )
        self.assertIn(".special_register_id = special_register->id", source)
        self.assertIn(".operand_type_compatibilities, context", source)
        self.assertIn("resolved_operand<ResolvedRegisterVector>", source)
        self.assertIn(
            ".actual_shape = check_end::OperandShape::Vector", source
        )
        self.assertIn("ResolvedVectorSpecialRegisterRef", source)
        self.assertIn("ResolvedVectorRegisterRef", source)
        self.assertIn("ResolvedPredicateSource", source)
        self.assertIn(".vector_arity = static_cast<uint8_t>", source)
        self.assertIn(".value_availability = special_register_availability(info)", source)
        self.assertIn(
            ".value_availability = symbol->address_availability", source
        )
        self.assertIn(
            ".value_availability = function->address_availability", source
        )
        self.assertIn("CheckResult check<Mov>(", source)
        self.assertIn("std::expected<Ld, ResolveDiagnostic>", source)
        self.assertIn("std::expected<Ldu, ResolveDiagnostic>", source)
        self.assertIn("std::expected<Prefetch, ResolveDiagnostic>", source)
        self.assertIn(
            ".address = resolved_operand<ResolvedAddress>", source
        )
        self.assertIn(
            ".actual_shape = check_end::OperandShape::Symbol", source
        )
        self.assertIn(
            ".actual_shape = check_end::OperandShape::Address", source
        )
        self.assertIn(
            "parameter_direction = ParameterDirection::Input", source
        )
        self.assertIn(
            "parameter_direction = ParameterDirection::Return", source
        )
        self.assertIn(
            "parameter_direction = ParameterDirection::CallArgument", source
        )
        self.assertIn(
            "ParameterDirection parameter_direction = ParameterDirection::None",
            source,
        )
        self.assertIn(".enclosing_function_kind =", source)
        self.assertIn(".parameter_direction = parameter_direction", source)
        self.assertIn("CheckResult check<Ld>(", source)
        self.assertIn("CheckResult check<Ldu>(", source)
        self.assertIn("CheckResult check<Prefetch>(", source)
        self.assertIn("check_memory_consistency(", source)
        self.assertIn("check_address_alignment(", source)
        self.assertIn(".address_alignment = address_alignment", source)
        self.assertIn("check_memory_vector(", source)

    def test_generate_category_resolved_ir_source(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
        )

        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir_arithmetic.gen.cpp"
            generate_resolved_ir_source(
                database,
                category="arithmetic",
                output_path=output_path,
            )
            source = output_path.read_text(encoding="utf-8")

        self.assertNotIn("#pragma once", source)
        self.assertIn('#include "resolved_ir.gen.hpp"', source)
        self.assertNotIn(
            "std::expected<Add::VariantType, ResolveDiagnostic>", source
        )
        self.assertIn(
            "std::expected<Add, ResolveDiagnostic>\n"
            "resolve<Add>(const syntax_ast::AstInstruction& ast,\n"
            "    const ResolveContext* context) {",
            source,
        )
        self.assertIn("resolve_fields(", source)
        self.assertIn(
            ".execution_predicate = std::move(fields->execution_predicate)",
            source,
        )
        self.assertIn(
            ".register_type = selected.dst.value.declared_type", source
        )
        self.assertIn("CheckResult check<Add>(", source)
        self.assertIn("const auto check_integer_no_sat =", source)
        self.assertIn("const auto check_sat =", source)
        self.assertIn("const auto check_packed_optional_sat =", source)
        self.assertIn("detail::VariantCheckFunction<", source)
        self.assertIn("std::visit(detail::Overloaded{", source)
        self.assertIn("const auto operand_check = check_operands(", source)
        self.assertIn("const auto layout_check = check_operand_layout_tag(", source)
        self.assertIn("check_modifier_value_availability(", source)
        self.assertNotIn("check_memory_consistency(", source)
        self.assertIn("Add::get_checker_descriptor(), \"IntegerNoSat\"", source)
        self.assertIn("std::expected<And, ResolveDiagnostic>", source)
        self.assertIn("CheckResult check<And>(", source)
        self.assertIn("std::expected<Or, ResolveDiagnostic>", source)
        self.assertIn("CheckResult check<Or>(", source)
        self.assertIn("std::expected<Xor, ResolveDiagnostic>", source)
        self.assertIn("CheckResult check<Xor>(", source)
        self.assertIn("std::expected<Not, ResolveDiagnostic>", source)
        self.assertIn("CheckResult check<Not>(", source)
        self.assertIn("std::expected<Shl, ResolveDiagnostic>", source)
        self.assertIn("CheckResult check<Shl>(", source)
        self.assertIn("std::expected<Shr, ResolveDiagnostic>", source)
        self.assertIn("CheckResult check<Shr>(", source)
        self.assertNotIn("struct Bar {", source)

    def test_common_scalar_checker_contract_uses_shared_descriptor_pipeline(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            arithmetic = root / "arithmetic.gen.cpp"
            descriptor = root / "resolved_descriptor.gen.cpp"
            generate_resolved_ir_source(
                self.database, category="arithmetic", output_path=arithmetic
            )
            generate_resolved_descriptor_source(
                self.database, output_path=descriptor
            )
            checker_source = arithmetic.read_text(encoding="utf-8")
            descriptor_source = descriptor.read_text(encoding="utf-8")

        self.assertIn("check_common(", checker_source)
        self.assertIn("check_modifier_value_availability(", checker_source)
        self.assertIn("check_operands(", checker_source)
        self.assertIn("ResolvedOperandBindingDescriptor", descriptor_source)
        self.assertIn("ScalarTypeSizePolicy", descriptor_source)
        for opcode_wrapper in (
            "check_comparison(",
            "check_rounding(",
            "check_saturation(",
            "check_scalar_type(",
            "check_register_width(",
        ):
            self.assertNotIn(opcode_wrapper, checker_source)

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
        self.assertIn('#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>', source)
        self.assertIn("namespace ptx_frontend::resolved_ir {", source)
        self.assertEqual(source.count("namespace generated_detail {"), 1)
        self.assertIn("struct AddResolvedDescriptorStorage {", source)
        self.assertIn("struct BarResolvedDescriptorStorage {", source)
        self.assertIn("check_end::ResolvedFieldDescriptor", source)
        self.assertNotIn("ResolvedConstantDescriptor", source)
        self.assertIn("check_end::ResolvedModifierBindingDescriptor", source)
        self.assertIn("check_end::ResolvedOperandBindingDescriptor", source)
        self.assertIn("checker::AddressStateSpaceDescriptor", source)
        self.assertIn(".allowed_address_state_spaces =", source)
        self.assertIn(".parameter_constraint = {", source)
        self.assertIn(
            ".register_width_policy = "
            "base::ScalarTypeSizePolicy::EqualOrWider,",
            source,
        )
        self.assertIn(
            ".register_width_policy = base::ScalarTypeSizePolicy::SameWidth,",
            source,
        )
        self.assertIn(".direction = ParameterDirection::Input,", source)
        self.assertIn(".direction = ParameterDirection::Return,", source)
        self.assertIn(".function_availability = {", source)
        self.assertIn(".state_space = MemoryStateSpace::Constant,", source)
        self.assertIn(".minimum_ptx_version = {3, 1},", source)
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

    def test_state_space_and_parameter_availability_emit_dnf(self) -> None:
        dnf = (("any_of", [{"target": "sm_100a", "capabilities": ["tensor"]}]),)
        ld = next(
            instruction
            for instruction in self.database.instructions
            if instruction.opcode == "ld"
        )
        resolved = from_instruction_spec(ld)
        static_binding = next(
            binding
            for variant in resolved.variants
            for layout in variant.operand_layouts
            for binding in layout.bindings
            if binding.allowed_address_state_spaces
        )
        state_space = replace(
            static_binding.allowed_address_state_spaces[0], availability=dnf
        )
        state_source = _emit_address_state_spaces((state_space,))
        self.assertIn(".any_of_count = 1", state_source)
        self.assertIn("TargetFlavor::ArchitectureSpecific", state_source)

        parameter_binding = next(
            binding
            for variant in resolved.variants
            for layout in variant.operand_layouts
            for binding in layout.bindings
            if binding.parameter_constraint is not None
        )
        parameter_binding = replace(
            parameter_binding,
            parameter_constraint=replace(
                parameter_binding.parameter_constraint,
                function_availability=dnf,
            ),
        )
        parameter_source = _emit_operand_binding_descriptor(
            parameter_binding, "vector_arities", "address_state_spaces"
        )
        self.assertIn(".function_availability = {", parameter_source)
        self.assertIn(".any_of_count = 1", parameter_source)
        self.assertIn('.capabilities = {{"tensor"}}', parameter_source)

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

        self.assertIn('#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker.hpp>', source)
        self.assertEqual(source.count("namespace generated_detail {"), 1)
        self.assertIn("struct AddCheckerDescriptorStorage {", source)
        self.assertIn("struct BarCheckerDescriptorStorage {", source)
        self.assertIn("checker::VariantDescriptor", source)
        self.assertIn("checker::OperandLayoutDescriptor", source)
        self.assertIn("checker::OperandTypeCompatibilityDescriptor", source)
        self.assertIn(".memory_consistency = {", source)
        self.assertIn(".address_alignment = {", source)
        self.assertIn('.vector_field_id = "vector",', source)
        self.assertIn(".memory_vector = {", source)
        self.assertIn('.vector_field_id = "dst",', source)
        self.assertIn('.vector_field_id = "src",', source)
        self.assertIn(".semantics_field_id = \"semantics\",", source)
        self.assertIn(
            ".special_register_kind = base::SpecialRegisterKind::Tid,",
            source,
        )
        self.assertIn(".instruction_width = 16,", source)
        self.assertIn(".effective_type = ScalarType::U16,", source)
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
                "category": "test",
                "codegen_category": "test",
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

    def test_comparison_modifier_domain_emits_typed_availability(self) -> None:
        specs = normalize_instruction_spec(
            {
                "category": "test",
                "codegen_category": "test",
                "instructions": [
                    {
                        "opcode": "sample",
                        "variants": [
                            {
                                "name": "sample_comparison",
                                "availability": {"ptx": "1.0"},
                                "modifiers": [
                                    {
                                        "name": "comparison",
                                        "kind": "comparison",
                                        "presence": "required",
                                        "values": [
                                            {
                                                "value": "lt",
                                                "availability": {"sm": 20},
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
        resolved = from_instruction_spec(specs[0])
        field = resolved.variants[0].modifier_fields[0]
        self.assertEqual(field.value_kind, ResolvedValueKind.COMPARISON_OPERATOR)
        self.assertEqual(field.cpp_type, "WithLocs<ComparisonOperator>")
        self.assertEqual(
            resolved.variants[0].modifier_value_availabilities[0].value_cpp_type,
            "ComparisonOperator",
        )

        database = CodegenDatabase(spec_schema="ptx-instr/v1", instructions=specs)
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir_checker_descriptor.gen.cpp"
            generate_resolved_checker_descriptor_source(
                database,
                output_path=output_path,
            )
            source = output_path.read_text(encoding="utf-8")

        self.assertIn(
            ".value_kind = checker::ModifierValueKind::ComparisonOperator,",
            source,
        )
        self.assertIn(".comparison_operator = ComparisonOperator::Lt,", source)

    def test_eviction_priority_modifier_domain_emits_typed_availability(self) -> None:
        spec = {
            "category": "test",
            "codegen_category": "test",
            "instructions": [
                {
                    "opcode": "sample",
                    "variants": [
                        {
                            "name": "sample_eviction_priority",
                            "availability": {"ptx": "1.0"},
                            "modifiers": [
                                {
                                    "name": "eviction_priority",
                                    "kind": "eviction_priority",
                                    "presence": "required",
                                    "values": [
                                        {
                                            "value": "evict_last",
                                            "availability": {
                                                "ptx": "7.4",
                                                "sm": 70,
                                            },
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
        specs = normalize_instruction_spec(spec)
        resolved = from_instruction_spec(specs[0])
        self.assertEqual(
            resolved.variants[0].modifier_value_availabilities[0].value_cpp_type,
            "EvictionPriority",
        )

        database = CodegenDatabase(spec_schema="ptx-instr/v1", instructions=specs)
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir_checker_descriptor.gen.cpp"
            generate_resolved_checker_descriptor_source(
                database,
                output_path=output_path,
            )
            source = output_path.read_text(encoding="utf-8")

        self.assertIn(
            ".value_kind = checker::ModifierValueKind::EvictionPriority,",
            source,
        )
        self.assertIn(
            ".eviction_priority = EvictionPriority::EvictLast,",
            source,
        )
        self.assertIn(".minimum_ptx_version = {7, 4},", source)

        value = spec["instructions"][0]["variants"][0]["modifiers"][0][
            "values"
        ][0]
        for invalid in (0, "not_a_priority"):
            value["value"] = invalid
            with self.assertRaisesRegex(ValueError, "eviction priority"):
                from_instruction_spec(normalize_instruction_spec(spec)[0])

    def test_boolean_modifier_domain_emits_typed_availability(self) -> None:
        specs = normalize_instruction_spec(
            {
                "category": "test",
                "codegen_category": "test",
                "instructions": [
                    {
                        "opcode": "sample",
                        "variants": [
                            {
                                "name": "sample_boolean",
                                "availability": {"ptx": "1.0"},
                                "modifiers": [
                                    {
                                        "name": "boolean",
                                        "kind": "boolean_op",
                                        "presence": "required",
                                        "values": [
                                            {
                                                "value": "xor",
                                                "availability": {"sm": 20},
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
        resolved = from_instruction_spec(specs[0])
        field = resolved.variants[0].modifier_fields[0]
        self.assertEqual(field.value_kind, ResolvedValueKind.BOOLEAN_OPERATOR)
        self.assertEqual(field.cpp_type, "WithLocs<BooleanOperator>")
        self.assertEqual(
            resolved.variants[0].modifier_value_availabilities[0].value_cpp_type,
            "BooleanOperator",
        )

        database = CodegenDatabase(spec_schema="ptx-instr/v1", instructions=specs)
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir_checker_descriptor.gen.cpp"
            generate_resolved_checker_descriptor_source(
                database,
                output_path=output_path,
            )
            source = output_path.read_text(encoding="utf-8")

        self.assertIn(
            ".value_kind = checker::ModifierValueKind::BooleanOperator,",
            source,
        )
        self.assertIn(".boolean_operator = BooleanOperator::Xor,", source)

    def test_semantic_modifier_domains_share_generated_availability_path(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source_path = Path(directory) / "resolved_ir_test.gen.cpp"
            generate_resolved_ir_source(
                self.database,
                category="arithmetic",
                output_path=source_path,
            )
            source = source_path.read_text(encoding="utf-8")

        self.assertIn("check_modifier_value_availability(", source)

    def test_rejects_token_override_for_value_set_reference(self) -> None:
        with self.assertRaisesRegex(ValueError, "value-set reference"):
            normalize_instruction_spec(
                {
                    "category": "test",
                    "codegen_category": "test",
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
                                    name="src",
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
                    immediate_value=ImmediateValueConstraint("src", (4,)),
                    immediate_ranges=(ImmediateRangeConstraint("src", 1, 8),),
                    immediate_multiple_of=ImmediateMultipleOfConstraint("src", 2),
                    rule="sample.typed",
                ),
            ),
        )
        database = CodegenDatabase(spec_schema="ptx-instr/v1", instructions=(instruction,))

        with tempfile.TemporaryDirectory() as directory:
            header_path = Path(directory) / "resolved_ir.gen.hpp"
            source_path = Path(directory) / "resolved_ir_uncategorized.gen.cpp"
            generate_resolved_ir_header(database, output_path=header_path)
            generate_resolved_ir_source(
                database,
                category="uncategorized",
                output_path=source_path,
            )
            header = header_path.read_text(encoding="utf-8")
            source = source_path.read_text(encoding="utf-8")

        self.assertIn("struct BinaryOperands {", header)
        self.assertIn("struct TernaryOperands {", header)
        self.assertIn(
            "using Operands = std::variant<BinaryOperands, TernaryOperands>;",
            header,
        )
        self.assertIn("Operands operands;", header)
        self.assertNotIn("check_sample_typed_binary_operands", header)
        self.assertIn("check_sample_typed_binary_operands", source)
        self.assertIn("check_sample_typed_ternary_operands", source)
        for layout in ("binary", "ternary"):
            start = source.index(f"check_sample_typed_{layout}_operands")
            payload_check = source[start:source.index("static_assert", start)]
            calls = (
                "check_immediate_value(",
                "check_immediate_range(",
                "check_immediate_multiple_of(",
            )
            self.assertEqual([payload_check.count(call) for call in calls], [1, 1, 1])
            self.assertEqual(
                [payload_check.index(call) for call in calls],
                sorted(payload_check.index(call) for call in calls),
            )


if __name__ == "__main__":
    unittest.main()
