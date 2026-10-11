from __future__ import annotations

from dataclasses import replace
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest
from typing import cast

import yaml


REPO_ROOT = Path(__file__).resolve().parents[3]
PYTHON_ROOT = REPO_ROOT / "python"

if str(PYTHON_ROOT) not in sys.path:
    sys.path.insert(0, str(PYTHON_ROOT))


from ptx_frontend.code_gen.database import get_packaged_spec_database
from ptx_frontend.code_gen.database import CodegenDatabase
from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.code_gen.emit.resolved_descriptors import (
    _emit_address_state_spaces,
    _emit_operand_binding_descriptor,
    generate_resolved_descriptor_source,
    _emit_modifier_default_descriptor
)
from ptx_frontend.code_gen.emit.checker_descriptors import (
    generate_resolved_checker_descriptor_source,
)
from ptx_frontend.code_gen.emit.resolved_dispatch import (
    generate_resolved_dispatch_source,
)
from ptx_frontend.code_gen.reference_policy import validate_reference_field_types
from ptx_frontend.code_gen.emit.resolved_model import (
    form_shards,
    generate_resolved_base_header,
    generate_resolved_identity_catalogue_header,
    generate_resolved_opcode_header,
    generate_resolved_umbrella_header,
)
from ptx_frontend.code_gen.emit.resolved_source import (
    _address_symbol_resolution_policy,
    generate_resolved_form_shard_source,
    generate_resolved_opcode_source,
)
from ptx_frontend.code_gen.normalize import normalize_instruction_spec
from ptx_frontend.code_gen.resolved_field_names import (
    field_cpp_constant_expr as _field_cpp_constant_expr,
    field_cpp_type as _field_cpp_type,
    field_value_cpp_type as _field_value_cpp_type,
)
from ptx_frontend.ir.resolved_ir import (
    ResolvedFieldOrigin,
    ResolvedFieldStorage,
    ResolvedImmediateConversionPolicy,
    ResolvedOperandAccess,
    ResolvedOperandRole,
    ResolvedOperandShape,
    ResolvedOperandTypeExpression,
    ResolvedOperandTypeExpressionKind,
    ResolvedRegisterWidthPolicy,
    ResolvedValueKind,
    ResolvedVectorTypePolicy,
    from_instruction_spec,
    ResolvedModifierBinding,
    ResolvedModifierDefault,
)
from ptx_frontend.code_gen.model import (
    ImmediateMultipleOfConstraint,
    ImmediateRangeConstraint,
    ImmediateValueConstraint,
    InstructionSpec,
    ConditionCodeEffect,
    ModifierSpec,
    ModifierValueSpec,
    OperandLayoutSpec,
    OperandImmediateConversionPolicy,
    OperandSpec,
    OperandTypeExpression,
    OperandTypeExpressionKind,
    VariantSpec,
)
from ptx_frontend.spec.model import (
    AsyncCompletionKind,
    ModifierKind,
    ModifierPresence,
    OperandAccess,
    OperandAddressBasePolicy,
    OperandAddressOffsetDomain,
    OperandKind,
    OperandRole,
    SemanticRule,
)
from ptx_frontend.ir.resolved_ir import (
    _build_modifier_value_availability,
)


BACKEND = load_cpp_backend(
    REPO_ROOT / "instructions/ptx_cpp_backend_spec/ptx_frontend.yaml"
)


def field_cpp_constant_expr(field):
    """Use this module's explicit repository backend for C++ field spelling."""

    return _field_cpp_constant_expr(field, backend=BACKEND)


def field_cpp_type(field):
    """Use this module's explicit repository backend for C++ field spelling."""

    return _field_cpp_type(field, backend=BACKEND)


def field_value_cpp_type(field):
    """Use this module's explicit repository backend for C++ field spelling."""

    return _field_value_cpp_type(field, backend=BACKEND)


def build_test_generation_context(database):
    """Allocate isolated fixture categories without changing production IDs."""

    from ptx_frontend.code_gen.context import build_generation_context

    identity = BACKEND.instruction_identity
    assert identity is not None
    fixture_backend = replace(
        BACKEND,
        instruction_identity=replace(
            identity,
            categories={**identity.categories, "test": 254, "uncategorized": 255},
        ),
    )
    return build_generation_context(database, fixture_backend)

class ResolvedIrFixture:
    """Build immutable canonical instruction fixtures for IR contract tests."""

    @classmethod
    def setUpClass(cls) -> None:
        database = get_packaged_spec_database()
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
        madc = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "madc"
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
        sin_instruction = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "sin"
        )
        cos_instruction = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "cos"
        )
        lg2_instruction = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "lg2"
        )
        ex2_instruction = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "ex2"
        )
        tanh_instruction = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "tanh"
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
        bfi_instruction = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "bfi"
        )
        bfind_instruction = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "bfind"
        )
        cls.instruction = from_instruction_spec(add)
        cls.sub_instruction = from_instruction_spec(sub)
        cls.mul_instruction = from_instruction_spec(mul)
        cls.mad_instruction = from_instruction_spec(mad)
        cls.madc_instruction = from_instruction_spec(madc)
        cls.fma_instruction = from_instruction_spec(fma)
        cls.div_instruction = from_instruction_spec(div)
        cls.rem_instruction = from_instruction_spec(rem)
        cls.min_instruction = from_instruction_spec(min_instruction)
        cls.max_instruction = from_instruction_spec(max_instruction)
        cls.abs_instruction = from_instruction_spec(abs_instruction)
        cls.neg_instruction = from_instruction_spec(neg_instruction)
        cls.sin_instruction = from_instruction_spec(sin_instruction)
        cls.cos_instruction = from_instruction_spec(cos_instruction)
        cls.lg2_instruction = from_instruction_spec(lg2_instruction)
        cls.ex2_instruction = from_instruction_spec(ex2_instruction)
        cls.tanh_instruction = from_instruction_spec(tanh_instruction)
        cls.lop3_instruction = from_instruction_spec(lop3_instruction)
        cls.shf_instruction = from_instruction_spec(shf_instruction)
        cls.bfe_instruction = from_instruction_spec(bfe_instruction)
        cls.bfi_instruction = from_instruction_spec(bfi_instruction)
        cls.bfind_instruction = from_instruction_spec(bfind_instruction)
        call = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "call"
        )
        cls.call_instruction = from_instruction_spec(call)



class ResolvedIrBuildTest(ResolvedIrFixture, unittest.TestCase):
    """Check arithmetic, control, and synchronization IR contracts."""

    def test_modifier_default_descriptor_uses_only_selected_value_member(self) -> None:
        binding = ResolvedModifierBinding(
            source_kind_id="type",
            target_field_id="type",
            default_value=ResolvedModifierDefault(
                value_kind=ResolvedValueKind.SCALAR_TYPE,
                value="f32",
            ),
        )

        emitted = _emit_modifier_default_descriptor(binding, BACKEND)

        self.assertIn(
            ".kind = check_end::ResolvedModifierDefaultKind::ScalarType",
            emitted,
        )
        self.assertIn(
            ".scalar_type = ScalarType::F32",
            emitted,
        )

        unexpected_members = (
            "bool_value",
            "rounding_mode",
            "cache_operator",
            "eviction_priority",
            "prefetch_size",
            "memory_state_space",
            "memory_consistency",
            "memory_scope",
            "mbarrier_phase_type",
            "mbarrier_layout",
            "async_proxy_kind",
            "proxy_kind_pair",
        )

        for member in unexpected_members:
            with self.subTest(member=member):
                self.assertNotIn(
                    f".{member} =",
                    emitted,
                )

    def test_modifier_binding_without_default_uses_empty_descriptor(self) -> None:
        binding = ResolvedModifierBinding(
            source_kind_id="type",
            target_field_id="type",
            default_value=None,
        )
    
        self.assertEqual(
            _emit_modifier_default_descriptor(binding, BACKEND),
            "check_end::ResolvedModifierDefaultDescriptor{}",
        )

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
                [field_value_cpp_type(field) for field in layout.fields]
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
                "Cc32",
                "Cc64",
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
                "Cc32",
                "Cc64",
            ],
        )

        variants = {
            variant.cpp_name: variant
            for variant in self.sub_instruction.variants
        }
        optional_sat = variants["OptionalSat"]
        self.assertEqual(
            [field.name for field in optional_sat.fields],
            ["sat", "type", "dst", "src1", "src2"],
        )
        self.assertEqual(
            optional_sat.modifier_bindings[0].default_value.value, # pyright: ignore[reportOptionalMemberAccess]
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
                (field.name, field_cpp_type(field), field.storage)
                for field in f32.modifier_fields
            ],
            [
                ("rounding", "WithLocs<RoundingMode>", ResolvedFieldStorage.INSTANCE),
                ("ftz", "WithLocs<bool>", ResolvedFieldStorage.INSTANCE),
                ("sat", "WithLocs<bool>", ResolvedFieldStorage.INSTANCE),
                ("type", "ScalarType", ResolvedFieldStorage.STATIC_CONSTANT),
            ],
        )
        rounding_default = f32.modifier_bindings[0].default_value
        self.assertIsNotNone(rounding_default)
        assert rounding_default is not None
        self.assertEqual(rounding_default.value_kind.value, "RoundingMode")
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
        self.assertEqual(field_cpp_constant_expr(variants["FloatF32"].modifier_fields[3]),
                         "ScalarType::F32")

        mixed = variants["MixedF32"]
        self.assertEqual(
            [
                (field.name, field_cpp_type(field), field.storage)
                for field in mixed.modifier_fields
            ],
            [
                ("rounding", "WithLocs<RoundingMode>", ResolvedFieldStorage.INSTANCE),
                ("sat", "WithLocs<bool>", ResolvedFieldStorage.INSTANCE),
                ("result_type", "ScalarType", ResolvedFieldStorage.STATIC_CONSTANT),
                ("input_type", "WithLocs<ScalarType>", ResolvedFieldStorage.INSTANCE),
            ],
        )
        self.assertEqual(
            [binding.type_expression.modifier_field_id for binding in mixed.operand_layouts[0].bindings],
            ["result_type", "input_type", "result_type"],
        )

    def test_mul_merges_complete_integer_and_floating_variants(self) -> None:
        variants = {variant.cpp_name: variant for variant in self.mul_instruction.variants}
        self.assertEqual(
            set(variants),
            {
                "RnF32", "F32x2", "F64", "Half", "HalfX2", "Bfloat",
                "BfloatX2", "LoU16", "LoU32", "LoU64", "LoS16", "LoS32",
                "LoS64", "HiU16", "HiU32", "HiU64", "HiS16", "HiS32",
                "HiS64", "WideU16", "WideS16", "WideU32", "WideS32",
            },
        )
        self.assertEqual(
            [field.name for field in variants["RnF32"].fields],
            ["rounding", "ftz", "sat", "type", "dst", "src1", "src2"],
        )
        self.assertEqual(
            [
                binding.register_width_policy
                for binding in variants["WideU32"].operand_layouts[0].bindings
            ],
            [ResolvedRegisterWidthPolicy.SAME_WIDTH] * 3,
        )
        self.assertEqual(
            [
                binding.register_width_policy
                for binding in variants["WideS32"].operand_layouts[0].bindings
            ],
            [ResolvedRegisterWidthPolicy.SAME_WIDTH] * 3,
        )

    def test_mad_merges_complete_integer_and_floating_ternary_layouts(self) -> None:
        self.assertEqual(
            [variant.cpp_name for variant in self.mad_instruction.variants],
            ["RnF32", "DirectedF32", "RnF64", "DirectedF64", "LoU32", "LoS32", "WideU32", "LoU16", "LoU64",
             "LoS16", "LoS64", "HiU16", "HiU32", "HiU64", "HiS16",
             "HiS32", "HiS64", "WideU16", "WideS16", "WideS32", "HiSatS32",
             "HiCc32", "LoCc32", "HiCc64", "LoCc64"],
        )
        self.assertEqual(
            [field.name for field in self.mad_instruction.variants[4].fields],
            ["lo", "type", "dst", "src1", "src2", "src3"],
        )
        self.assertEqual(
            self.mad_instruction.variants[4].operand_layouts[0].bindings[3].role,
            ResolvedOperandRole.SOURCE,
        )
        self.assertEqual(
            [
                binding.register_width_policy
                for binding in self.mad_instruction.variants[6].operand_layouts[0].bindings
            ],
            [ResolvedRegisterWidthPolicy.SAME_WIDTH] * 4,
        )
        self.assertEqual(
            [
                binding.register_width_policy
                for binding in self.mad_instruction.variants[0].operand_layouts[0].bindings
            ],
            [ResolvedRegisterWidthPolicy.SAME_WIDTH] * 4,
        )
        self.assertEqual(
            [field.name for field in self.mad_instruction.variants[0].fields],
            ["rounding", "ftz", "sat", "type", "dst", "src1", "src2", "src3"],
        )

    def test_fma_models_all_ptx_93_ternary_layouts(self) -> None:
        self.assertEqual(
            [variant.cpp_name for variant in self.fma_instruction.variants],
            [
                "RnF32", "DirectedF32", "RnF64", "DirectedF64", "F32x2",
                "RnF16", "RnF16x2", "HalfRelu", "HalfOob", "HalfOobRelu",
                "Bf16", "Bf16x2", "Bf16Oob", "Bf16x2Oob", "MixedF32F16",
                "MixedF32Bf16",
            ],
        )
        self.assertEqual(
            [field.name for field in self.fma_instruction.variants[0].fields],
            ["rounding", "ftz", "sat", "type", "dst", "src1", "src2", "src3"],
        )
        variants = {
            variant.variant_id: variant for variant in self.fma_instruction.variants
        }
        for name in (
            "fma_rn_f32", "fma_directed_f32", "fma_rn_f64", "fma_directed_f64",
        ):
            bindings = variants[name].operand_layouts[0].bindings
            self.assertEqual(
                [binding.allowed_shapes for binding in bindings[1:]],
                [(ResolvedOperandShape.REGISTER, ResolvedOperandShape.IMMEDIATE)] * 3,
            )
            self.assertEqual(
                [binding.register_width_policy for binding in bindings],
                [ResolvedRegisterWidthPolicy.SAME_WIDTH] * 4,
            )
        self.assertEqual(
            [binding.register_width_policy
             for binding in variants["fma_f32x2"].operand_layouts[0].bindings],
            [ResolvedRegisterWidthPolicy.EXACT] * 4,
        )
        for name in ("fma_bf16", "fma_bf16_oob"):
            self.assertEqual(
                [binding.register_width_policy
                 for binding in variants[name].operand_layouts[0].bindings],
                [ResolvedRegisterWidthPolicy.EXACT] * 4,
            )
        for name in ("fma_mixed_f32_f16", "fma_mixed_f32_bf16"):
            bindings = variants[name].operand_layouts[0].bindings
            self.assertEqual(
                [binding.allowed_shapes for binding in bindings],
                [
                    (ResolvedOperandShape.REGISTER,),
                    (ResolvedOperandShape.REGISTER,),
                    (ResolvedOperandShape.REGISTER,),
                    (ResolvedOperandShape.REGISTER, ResolvedOperandShape.IMMEDIATE),
                ],
            )
            self.assertEqual(bindings[0].type_expression.modifier_field_id, "result_type")
            self.assertEqual(bindings[3].type_expression.modifier_field_id, "result_type")

    def test_div_merges_complete_integer_and_floating_binary_layouts(self) -> None:
        self.assertEqual(
            [variant.cpp_name for variant in self.div_instruction.variants],
            [
                "RnF32",
                "RnF64",
                "DirectedF32",
                "ApproxF32",
                "FullF32",
                "DirectedF64",
                "U32",
                "S32",
                "U16",
                "U64",
                "S16",
                "S64",
            ],
        )
        self.assertEqual(
            [field.name for field in self.div_instruction.variants[6].fields],
            ["type", "dst", "src1", "src2"],
        )
        for variant in self.div_instruction.variants[:6]:
            self.assertEqual(
                [binding.register_width_policy for binding in variant.operand_layouts[0].bindings],
                [ResolvedRegisterWidthPolicy.SAME_WIDTH] * 3,
            )

    def test_rem_has_all_signed_and_unsigned_binary_variants(self) -> None:
        self.assertEqual(
            [variant.cpp_name for variant in self.rem_instruction.variants],
            ["S32", "U32", "U16", "U64", "S16", "S64"],
        )
        for variant, scalar_type in zip(
            self.rem_instruction.variants, ("S32", "U32", "U16", "U64", "S16", "S64"), strict=True
        ):
            self.assertEqual(
                [field.name for field in variant.fields],
                ["type", "dst", "src1", "src2"],
            )
            self.assertEqual(variant.fields[0].constant_value, scalar_type.lower())

    def test_min_has_complete_integer_and_floating_variants(self) -> None:
        self.assertEqual(
            [variant.cpp_name for variant in self.min_instruction.variants],
            [
                "S32",
                "F32",
                "F64",
                "F16",
                "F16x2",
                "Bf16",
                "Bf16x2",
                "NonReluInteger",
                "S16x2",
                "ReluS32",
                "ReluS16x2",
                "S8x4",
                "ReluS8x4",
            ],
        )
        s32, f32 = self.min_instruction.variants[:2]
        self.assertEqual(
            [field.name for field in s32.fields], ["type", "dst", "src1", "src2"]
        )
        self.assertEqual(s32.fields[0].constant_value, "s32")
        self.assertEqual(
            [field.name for field in f32.modifier_fields],
            ["ftz", "nan", "xorsign_abs", "abs", "type"],
        )
        self.assertEqual(f32.modifier_fields[-1].constant_value, "f32")

    def test_min_keeps_binary_and_ternary_layouts_arity_selected(self) -> None:
        f32 = self.min_instruction.variants[1]
        self.assertEqual(
            [layout.layout_id for layout in f32.operand_layouts],
            ["binary", "ternary"],
        )
        self.assertEqual(
            [len(layout.fields) for layout in f32.operand_layouts], [3, 4]
        )
        self.assertEqual(
            [layout.forbidden_modifiers for layout in f32.operand_layouts],
            [("abs",), ("xorsign_abs",)],
        )
        binary, ternary = f32.operand_layouts
        self.assertEqual(
            [binding.register_width_policy for binding in binary.bindings],
            [ResolvedRegisterWidthPolicy.SAME_WIDTH] * 3,
        )
        self.assertEqual(
            [binding.register_width_policy for binding in ternary.bindings],
            [ResolvedRegisterWidthPolicy.SAME_WIDTH] * 4,
        )
        # The three-source form carries its own PTX 8.8 / SM 100 requirement.
        self.assertEqual(dict(ternary.availability), {"ptx": "8.8", "sm": 100})
        self.assertEqual(dict(binary.availability), {})

    def test_max_has_complete_integer_and_floating_variants(self) -> None:
        self.assertEqual(
            [variant.cpp_name for variant in self.max_instruction.variants],
            [
                "S32",
                "F32",
                "F64",
                "F16",
                "F16x2",
                "Bf16",
                "Bf16x2",
                "NonReluInteger",
                "S16x2",
                "ReluS32",
                "ReluS16x2",
                "S8x4",
                "ReluS8x4",
            ],
        )
        s32, f32 = self.max_instruction.variants[:2]
        self.assertEqual(
            [field.name for field in s32.fields], ["type", "dst", "src1", "src2"]
        )
        self.assertEqual(s32.fields[0].constant_value, "s32")
        self.assertEqual(
            [field.name for field in f32.modifier_fields],
            ["ftz", "nan", "xorsign_abs", "abs", "type"],
        )
        self.assertEqual(f32.modifier_fields[-1].constant_value, "f32")
        self.assertEqual(
            [layout.forbidden_modifiers for layout in f32.operand_layouts],
            [("abs",), ("xorsign_abs",)],
        )

    def test_abs_has_complete_signed_and_float_unary_variants(self) -> None:
        self.assertEqual(
            [variant.cpp_name for variant in self.abs_instruction.variants],
            ["S32", "F32", "F64", "F16", "F16x2", "Bf16", "Bf16x2", "S16", "S64"],
        )
        for variant, scalar_type, fields in zip(
            self.abs_instruction.variants,
            ("s32", "f32", "f64", "f16", "f16x2", "bf16", "bf16x2", "s16", "s64"),
            (
                ("type", "dst", "src"),
                ("ftz", "type", "dst", "src"),
                ("type", "dst", "src"),
                ("ftz", "type", "dst", "src"),
                ("ftz", "type", "dst", "src"),
                ("type", "dst", "src"),
                ("type", "dst", "src"),
                ("type", "dst", "src"),
                ("type", "dst", "src"),
            ),
            strict=True,
        ):
            self.assertEqual([field.name for field in variant.fields], list(fields))
            self.assertEqual(variant.fields[-3].constant_value, scalar_type)

    def test_neg_has_complete_scalar_and_packed_unary_variants(self) -> None:
        self.assertEqual(
            [variant.cpp_name for variant in self.neg_instruction.variants],
            ["S32", "F32", "F64", "F16", "F16x2", "Bf16", "Bf16x2", "S16", "S64", "S8x4"],
        )
        for variant, scalar_type, fields in zip(
            self.neg_instruction.variants,
            ("s32", "f32", "f64", "f16", "f16x2", "bf16", "bf16x2", "s16", "s64", "s8x4"),
            (
                ("type", "dst", "src"),
                ("ftz", "type", "dst", "src"),
                ("type", "dst", "src"),
                ("ftz", "type", "dst", "src"),
                ("ftz", "type", "dst", "src"),
                ("type", "dst", "src"),
                ("type", "dst", "src"),
                ("type", "dst", "src"),
                ("type", "dst", "src"),
                ("type", "dst", "src"),
            ),
            strict=True,
        ):
            self.assertEqual([field.name for field in variant.fields], list(fields))
            self.assertEqual(variant.fields[-3].constant_value, scalar_type)
        self.assertEqual(
            [binding.register_width_policy
             for binding in self.neg_instruction.variants[4].operand_layouts[0].bindings],
            [ResolvedRegisterWidthPolicy.EXACT] * 2,
        )
        self.assertEqual(
            [binding.type_expression.scalar_type
             for binding in self.neg_instruction.variants[4].operand_layouts[0].bindings],
            ["b32"] * 2,
        )

    def test_transcendental_models_typed_approx_and_cohort_containers(self) -> None:
        by_opcode = {
            "sin": self.sin_instruction,
            "cos": self.cos_instruction,
            "lg2": self.lg2_instruction,
            "ex2": self.ex2_instruction,
            "tanh": self.tanh_instruction,
        }
        expected = {
            "sin": (("ApproxF32", "f32", ("approx", "ftz", "type", "dst", "src")),),
            "cos": (("ApproxF32", "f32", ("approx", "ftz", "type", "dst", "src")),),
            "lg2": (("ApproxF32", "f32", ("approx", "ftz", "type", "dst", "src")),),
            "ex2": (
                ("ApproxF32", "f32", ("approx", "ftz", "type", "dst", "src")),
                ("ApproxF16", "f16", ("approx", "type", "dst", "src")),
                ("ApproxF16x2", "f16x2", ("approx", "type", "dst", "src")),
                ("ApproxFtzBf16", "bf16", ("approx", "ftz", "type", "dst", "src")),
                ("ApproxFtzBf16x2", "bf16x2", ("approx", "ftz", "type", "dst", "src")),
            ),
            "tanh": (
                ("ApproxF32", "f32", ("approx", "type", "dst", "src")),
                ("ApproxF16", "f16", ("approx", "type", "dst", "src")),
                ("ApproxF16x2", "f16x2", ("approx", "type", "dst", "src")),
                ("ApproxBf16", "bf16", ("approx", "type", "dst", "src")),
                ("ApproxBf16x2", "bf16x2", ("approx", "type", "dst", "src")),
            ),
        }
        for opcode, variants in expected.items():
            instruction = by_opcode[opcode]
            self.assertEqual(
                [variant.cpp_name for variant in instruction.variants],
                [name for name, _, _ in variants],
            )
            for variant, (cpp_name, scalar_type, fields) in zip(
                instruction.variants, variants, strict=True
            ):
                self.assertEqual([field.name for field in variant.fields], list(fields), cpp_name)
                self.assertIs(variant.fields[0].constant_value, True, cpp_name)
                self.assertEqual(variant.fields[-3].constant_value, scalar_type, cpp_name)

        for opcode in ("sin", "cos", "lg2", "ex2", "tanh"):
            variant = next(
                item for item in by_opcode[opcode].variants if item.cpp_name == "ApproxF32"
            )
            self.assertEqual(
                [binding.register_width_policy for binding in variant.operand_layouts[0].bindings],
                [ResolvedRegisterWidthPolicy.SAME_WIDTH] * 2,
                opcode,
            )

        # BF16 is an instruction-only format and binds exact bit containers; the
        # packed F16 cohort also admits .b32 through same-width compatibility.
        containers = {
            ("ex2", "ApproxF16"): ("f16", ResolvedRegisterWidthPolicy.SAME_WIDTH),
            ("ex2", "ApproxF16x2"): ("f16x2", ResolvedRegisterWidthPolicy.SAME_WIDTH),
            ("ex2", "ApproxFtzBf16"): ("b16", ResolvedRegisterWidthPolicy.EXACT),
            ("ex2", "ApproxFtzBf16x2"): ("b32", ResolvedRegisterWidthPolicy.EXACT),
            ("tanh", "ApproxF16"): ("f16", ResolvedRegisterWidthPolicy.SAME_WIDTH),
            ("tanh", "ApproxF16x2"): ("f16x2", ResolvedRegisterWidthPolicy.SAME_WIDTH),
            ("tanh", "ApproxBf16"): ("b16", ResolvedRegisterWidthPolicy.EXACT),
            ("tanh", "ApproxBf16x2"): ("b32", ResolvedRegisterWidthPolicy.EXACT),
        }
        for (opcode, cpp_name), (container, policy) in containers.items():
            variant = next(
                item for item in by_opcode[opcode].variants if item.cpp_name == cpp_name
            )
            bindings = variant.operand_layouts[0].bindings
            self.assertEqual(
                [binding.register_width_policy for binding in bindings],
                [policy] * 2,
                cpp_name,
            )
            self.assertEqual(
                [binding.type_expression.scalar_type for binding in bindings],
                [container] * 2,
                cpp_name,
            )

    def test_lop3_has_base_and_boolop_layouts_with_u8_lut_range(self) -> None:
        variant, boolop = self.lop3_instruction.variants
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
        self.assertEqual(boolop.cpp_name, "BoolopB32")
        self.assertEqual(
            [field.name for field in boolop.fields],
            ["boolean", "type", "dst", "src1", "src2", "src3", "lut", "combine"],
        )
        self.assertEqual(
            [(constraint.operand_field_id, constraint.minimum, constraint.maximum)
             for constraint in boolop.immediate_ranges],
            [("lut", 0, 255)],
        )

    def test_bit_field_controls_have_ranges_and_register_forms(self) -> None:
        for instruction, expected_variants in (
            (self.bfe_instruction, ["U32", "U64", "S32", "S64"]),
            (self.bfi_instruction, ["B32", "B64"]),
        ):
            self.assertEqual(
                [variant.cpp_name for variant in instruction.variants],
                expected_variants,
            )
            for variant in instruction.variants:
                self.assertEqual(
                    [(constraint.operand_field_id, constraint.minimum,
                      constraint.maximum)
                     for constraint in variant.immediate_ranges],
                    [("offset", 0, 255), ("width", 0, 255)],
                )
                self.assertEqual(
                    [binding.allowed_shapes for binding in
                     variant.operand_layouts[0].bindings[-2:]],
                    [(ResolvedOperandShape.REGISTER,
                      ResolvedOperandShape.IMMEDIATE)] * 2,
                )
                source_bindings = (
                    variant.operand_layouts[0].bindings[1:-2]
                    if instruction is self.bfi_instruction
                    else variant.operand_layouts[0].bindings[1:2]
                )
                self.assertEqual(
                    [binding.allowed_shapes for binding in source_bindings],
                    [(ResolvedOperandShape.REGISTER,
                      ResolvedOperandShape.IMMEDIATE)] * len(source_bindings),
                )

    def test_bfind_has_all_type_and_shift_amount_forms(self) -> None:
        self.assertEqual(
            [variant.cpp_name for variant in self.bfind_instruction.variants],
            ["ShiftamtU32", "U32", "U64", "S32", "S64", "ShiftamtU64",
             "ShiftamtS32", "ShiftamtS64"],
        )
        for variant in self.bfind_instruction.variants:
            self.assertEqual(
                variant.operand_layouts[0].bindings[1].allowed_shapes,
                (ResolvedOperandShape.REGISTER, ResolvedOperandShape.IMMEDIATE),
            )

    def test_remaining_integer_arithmetic_variants_and_control_contracts(self) -> None:
        """Keep the remaining PTX 9.3 integer forms and their special controls explicit."""

        by_opcode = {
            instruction.opcode: instruction
            for instruction in self.database.instructions
        }
        expected_variants = {
            "clmad": ["clmad_lo_u64", "clmad_hi_u64"],
            "mul24": ["mul24_lo_u32", "mul24_lo_s32", "mul24_hi_u32", "mul24_hi_s32"],
            "mad24": ["mad24_lo_u32", "mad24_lo_s32", "mad24_hi_u32", "mad24_hi_s32", "mad24_hi_sat_s32"],
            "sad": ["sad_scalar"], "fns": ["fns_b32"],
            "szext": ["szext_clamp_u32", "szext_wrap_u32", "szext_clamp_s32", "szext_wrap_s32"],
            "bmsk": ["bmsk_clamp_b32", "bmsk_wrap_b32"],
            "dp4a": ["dp4a_u32_u32", "dp4a_u32_s32", "dp4a_s32_u32", "dp4a_s32_s32"],
            "dp2a": ["dp2a_lo_u32_u32", "dp2a_lo_u32_s32", "dp2a_lo_s32_u32", "dp2a_lo_s32_s32", "dp2a_hi_u32_u32", "dp2a_hi_u32_s32", "dp2a_hi_s32_u32", "dp2a_hi_s32_s32"],
        }
        for opcode, names in expected_variants.items():
            self.assertEqual(
                [variant.name for variant in by_opcode[opcode].variants], names
            )

        min_relu_s16x2 = next(
            variant
            for variant in by_opcode["min"].variants
            if variant.name == "min_relu_s16x2"
        )
        self.assertEqual(min_relu_s16x2.modifier_order_aliases,
                         (("type", "relu"),))

        fns = by_opcode["fns"].variants[0]
        self.assertEqual(
            [(constraint.operand, constraint.minimum, constraint.maximum)
             for constraint in fns.immediate_ranges],
            [("base", 0, 31)],
        )
        self.assertEqual(
            [binding.type_expression.scalar_type # pyright: ignore[reportOptionalMemberAccess]
             for binding in fns.operand_layouts[0].operands],
            ["b32", "b32", "u32", "s32"],
        )
        for variant in by_opcode["bmsk"].variants:
            self.assertEqual(variant.immediate_ranges, ())
            self.assertEqual(
                [binding.kind for binding in variant.operand_layouts[0].operands],
            [
                OperandKind.REGISTER,
                OperandKind.REGISTER_OR_IMMEDIATE,
                OperandKind.REGISTER_OR_IMMEDIATE,
            ],
            )
        for opcode in ("dp4a", "dp2a"):
            for variant in by_opcode[opcode].variants:
                bindings = variant.operand_layouts[0].operands
                self.assertEqual(
                    [binding.kind for binding in bindings[1:]],
                    [OperandKind.REGISTER_OR_IMMEDIATE] * 3,
                )

    def test_extended_precision_multiply_add_variants(self) -> None:
        self.assertEqual(
            [variant.cpp_name for variant in self.mad_instruction.variants[-4:]],
            ["HiCc32", "LoCc32", "HiCc64", "LoCc64"],
        )
        self.assertTrue(
            all(variant.condition_code_effect is ConditionCodeEffect.CARRY_OUT
                for variant in self.mad_instruction.variants[-4:])
        )
        self.assertEqual(
            [variant.cpp_name for variant in self.madc_instruction.variants],
            ["HiPlain32", "LoPlain32", "HiPlain64", "LoPlain64",
             "HiCc32", "LoCc32", "HiCc64", "LoCc64"],
        )
        self.assertEqual(
            [variant.condition_code_effect for variant in self.madc_instruction.variants],
            [ConditionCodeEffect.CARRY_IN] * 4
            + [ConditionCodeEffect.CARRY_IN_OUT] * 4,
        )

    def test_shf_has_all_direction_and_mode_variants(self) -> None:
        self.assertEqual(
            [variant.cpp_name for variant in self.shf_instruction.variants],
            ["LClampB32", "LWrapB32", "RClampB32", "RWrapB32"],
        )
        for variant in self.shf_instruction.variants:
            self.assertEqual(
                [field.name for field in variant.fields],
                ["left" if variant.cpp_name.startswith("L") else "right",
                 "clamp" if "Clamp" in variant.cpp_name else "wrap",
                 "type", "dst", "src1", "src2", "count"],
            )

    def test_add_resolved_variant_fields(self) -> None:
        variants = {variant.cpp_name: variant for variant in self.instruction.variants}

        self.assertEqual(
            [
                (field.name, field_cpp_type(field), field.origin)
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
            ["sat", "type", "dst", "src1", "src2"],
        )
        self.assertEqual(
            [field.storage for field in variants["Sat"].fields[:2]],
            [
                ResolvedFieldStorage.STATIC_CONSTANT,
                ResolvedFieldStorage.INSTANCE,
            ],
        )
        self.assertEqual(
            field_cpp_constant_expr(variants["Sat"].fields[0]),
            "true",
        )
        self.assertEqual(
            [
                (binding.source_kind_id, binding.target_field_id)
                for binding in variants["Sat"].modifier_bindings
            ],
            [("sat", "sat"), ("type", "type")],
        )
        optional_sat_binding = variants["PackedOptionalSat"].modifier_bindings[0]
        self.assertIsNotNone(optional_sat_binding.default_value)
        assert optional_sat_binding.default_value is not None
        self.assertEqual(optional_sat_binding.default_value.value_kind.value, "Bool")
        self.assertIs(optional_sat_binding.default_value.value, False)
        self.assertIsNone(
            variants["PackedOptionalSat"].modifier_bindings[1].default_value
        )
        self.assertEqual(
            [
                (field.name, field_cpp_type(field), field.origin)
                for field in variants["PackedOptionalSat"].fields
            ],
            [
                ("sat", "WithLocs<bool>", ResolvedFieldOrigin.MODIFIER),
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
            [("sat", "sat"), ("type", "type")],
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

    def test_cpp_generation_projects_modifier_member_aliases(self) -> None:
        """Backend-only aliases do not replace semantic resolved field IDs."""

        from ptx_frontend.code_gen.resolved_field_names import (
            with_cpp_backend_field_names,
        )

        projected = with_cpp_backend_field_names(self.instruction, BACKEND)
        variant = next(
            variant for variant in projected.variants if variant.variant_id == "add_sat"
        )

        self.assertEqual(variant.modifier_fields[0].name, "saturate")
        self.assertEqual(
            variant.modifier_bindings[0].target_field_id,
            "saturate",
        )

    def test_bar_sync_uses_distinct_modifier_variants_and_operand_layouts(
        self,
    ) -> None:
        database = self.database
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
            [
                binding.immediate_conversion_policy
                for binding in variants["Sync"].operand_layouts[2].bindings
            ],
            [ResolvedImmediateConversionPolicy.REQUIRE_TARGET_RANGE] * 2,
        )
        self.assertEqual(
            [layout.layout_id for layout in variants["RedPopcU32"].operand_layouts],
            ["without_thread_count", "with_thread_count"],
        )
        self.assertEqual(
            field_value_cpp_type(variants["RedPopcU32"].operand_layouts[0].fields[2]),
            "ResolvedPredicate",
        )
        self.assertEqual(
            field_value_cpp_type(variants["RedAndPred"].operand_layouts[1].fields[0]),
            "ResolvedPredicate",
        )
        self.assertEqual(
            [field.name for field in variants["WarpSync"].modifier_fields],
            ["warp", "sync"],
        )
        self.assertEqual(
            [(field.name, field_value_cpp_type(field))
             for field in variants["WarpSync"].operand_layouts[0].fields],
            [("membermask", "RegOrImm")],
        )
        self.assertEqual(
            dict(variants["WarpSync"].availability),
            {"ptx": "6.0", "sm": 30},
        )

    def test_cta_barrier_numeric_constraints_are_resolved_for_all_cta_forms(
        self,
    ) -> None:
        bar = next(
            instruction
            for instruction in self.database.instructions
            if instruction.opcode == "bar"
        )
        variants = {
            variant.cpp_name: variant
            for variant in from_instruction_spec(bar).variants
        }

        for name in ("Sync", "CtaSync", "Arrive", "CtaArrive", "RedPopcU32",
                     "CtaRedPopcU32", "RedAndPred", "CtaRedAndPred", "RedOrPred",
                     "CtaRedOrPred"):
            with self.subTest(variant=name):
                variant = variants[name]
                ranges = [
                    (constraint.operand_field_id, constraint.minimum, constraint.maximum)
                    for constraint in variant.immediate_ranges
                ]
                self.assertIn(("barrier", 0, 15), ranges)
                self.assertEqual(
                    (variant.immediate_multiple_of.operand_field_id, # pyright: ignore[reportOptionalMemberAccess]
                     variant.immediate_multiple_of.divisor), # pyright: ignore[reportOptionalMemberAccess]
                    ("thread_count", 32),
                )

        for name in ("Arrive", "CtaArrive"):
            with self.subTest(arrive_variant=name):
                self.assertIn(
                    ("thread_count", 1, None),
                    [
                        (constraint.operand_field_id, constraint.minimum,
                         constraint.maximum)
                        for constraint in variants[name].immediate_ranges
                    ],
                )

    def test_immediate_conversion_is_independent_of_type_expression(self) -> None:
        """Fixed and modifier-derived types can select either conversion use."""

        instruction = from_instruction_spec(
            InstructionSpec(
                opcode="conversion_test",
                variants=(
                    VariantSpec(
                        name="conversion_test_default",
                        availability={"ptx": "1.0", "sm": 0},
                        modifiers=(
                            ModifierSpec(
                                name="type",
                                kind=ModifierKind.TYPE,
                                presence=ModifierPresence.REQUIRED,
                                domain="scalar_types",
                                values=(ModifierValueSpec(value="u32"),),
                            ),
                        ),
                        operand_layouts=(
                            OperandLayoutSpec(
                                name="default",
                                operands=(
                                    OperandSpec(
                                        name="fixed_data",
                                        kind=OperandKind.IMMEDIATE,
                                        role=OperandRole.SOURCE,
                                        access=OperandAccess.READ,
                                        type_expression=OperandTypeExpression(
                                            OperandTypeExpressionKind.FIXED_SCALAR,
                                            scalar_type="b32",
                                        ),
                                    ),
                                    OperandSpec(
                                        name="strict_dynamic",
                                        kind=OperandKind.REGISTER_OR_IMMEDIATE,
                                        role=OperandRole.SOURCE,
                                        access=OperandAccess.READ,
                                        type_expression=OperandTypeExpression(
                                            OperandTypeExpressionKind.MODIFIER,
                                            modifier_name="type",
                                        ),
                                        immediate_conversion_policy=(
                                            OperandImmediateConversionPolicy.REQUIRE_TARGET_RANGE
                                        ),
                                    ),
                                ),
                            ),
                        ),
                    ),
                ),
            )
        )
        bindings = instruction.variants[0].operand_layouts[0].bindings
        self.assertEqual(
            bindings[0].type_expression.kind,
            ResolvedOperandTypeExpressionKind.FIXED_SCALAR,
        )
        self.assertEqual(
            bindings[0].immediate_conversion_policy,
            ResolvedImmediateConversionPolicy.NARROW,
        )
        self.assertEqual(
            bindings[1].type_expression.kind,
            ResolvedOperandTypeExpressionKind.MODIFIER_FIELD,
        )
        self.assertEqual(
            bindings[1].immediate_conversion_policy,
            ResolvedImmediateConversionPolicy.REQUIRE_TARGET_RANGE,
        )

    def test_barrier_cta_and_cluster_model_defaults_and_availability(self) -> None:
        database = self.database
        barrier = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "barrier"
        )
        variants = {
            variant.cpp_name: variant
            for variant in from_instruction_spec(barrier).variants
        }
        self.assertEqual(
            set(variants), {"Sync", "CtaSync", "Arrive", "CtaArrive",
                            "ClusterArrive", "ClusterWait", "RedPopcU32",
                            "CtaRedPopcU32", "RedAndPred", "CtaRedAndPred",
                            "RedOrPred", "CtaRedOrPred"}
        )
        for name, ptx in (("Sync", "6.0"), ("CtaSync", "7.8")):
            variant = variants[name]
            self.assertEqual(dict(variant.availability), {"ptx": ptx, "sm": 30})
            self.assertEqual(
                [layout.layout_id for layout in variant.operand_layouts],
                ["barrier", "barrier_and_thread_count"],
            )
            expected_fields = [("sync", "bool"), ("aligned", "WithLocs<bool>")]
            if name == "CtaSync":
                expected_fields.insert(0, ("cta", "bool"))
            self.assertEqual(
                [(field.name, field_cpp_type(field)) for field in variant.modifier_fields],
                expected_fields,
            )
        for name, ptx in (("Arrive", "6.0"), ("CtaArrive", "7.8")):
            variant = variants[name]
            self.assertEqual(dict(variant.availability), {"ptx": ptx, "sm": 30})
            self.assertEqual(
                [layout.layout_id for layout in variant.operand_layouts],
                ["default"],
            )
            self.assertEqual(
                [field.name for field in variant.operand_layouts[0].fields],
                ["barrier", "thread_count"],
            )
            expected_fields = [("arrive", "bool"), ("aligned", "WithLocs<bool>")]
            if name == "CtaArrive":
                expected_fields.insert(0, ("cta", "bool"))
            self.assertEqual(
                [(field.name, field_cpp_type(field)) for field in variant.modifier_fields],
                expected_fields,
            )
            ranges = [
                (item.operand_field_id, item.minimum, item.maximum)
                for item in variant.immediate_ranges
            ]
            self.assertIn(("barrier", 0, 15), ranges)
            self.assertIn(("thread_count", 1, None), ranges)
            self.assertEqual(
                (variant.immediate_multiple_of.operand_field_id, # pyright: ignore[reportOptionalMemberAccess]
                 variant.immediate_multiple_of.divisor), # pyright: ignore[reportOptionalMemberAccess]
                ("thread_count", 32),
            )
        for name in ("RedPopcU32", "CtaRedPopcU32", "RedAndPred",
                     "CtaRedAndPred", "RedOrPred", "CtaRedOrPred"):
            variant = variants[name]
            self.assertEqual(
                dict(variant.availability),
                {"ptx": "7.8" if name.startswith("Cta") else "6.0", "sm": 30},
            )
            self.assertEqual(
                [layout.layout_id for layout in variant.operand_layouts],
                ["without_thread_count", "with_thread_count"],
            )
            self.assertEqual(
                [[field.name for field in layout.fields]
                 for layout in variant.operand_layouts],
                [["dst", "barrier", "predicate"],
                 ["dst", "barrier", "thread_count", "predicate"]],
            )
            self.assertEqual(
                [(field.name, field_cpp_type(field)) for field in variant.modifier_fields],
                ([("cta", "bool")] if name.startswith("Cta") else [])
                + [("red", "bool"), ("reduction", "bool"),
                   ("aligned", "WithLocs<bool>"), ("result_type", "ScalarType")],
            )
            ranges = [
                (item.operand_field_id, item.minimum, item.maximum)
                for item in variant.immediate_ranges
            ]
            self.assertIn(("barrier", 0, 15), ranges)
            self.assertEqual(
                (variant.immediate_multiple_of.operand_field_id, # pyright: ignore[reportOptionalMemberAccess]
                 variant.immediate_multiple_of.divisor), # pyright: ignore[reportOptionalMemberAccess]
                ("thread_count", 32),
            )
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
                [(field.name, field_cpp_type(field)) for field in variant.modifier_fields],
                [
                    ("scope", "MemoryScope"),
                    ("arrive" if name == "ClusterArrive" else "wait", "bool"),
                    ("semantics", "WithLocs<MemoryConsistency>"),
                    ("aligned", "WithLocs<bool>"),
                ],
            )
            self.assertEqual(
                variant.modifier_bindings[2].default_value.value, # pyright: ignore[reportOptionalMemberAccess]
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
        database = self.database
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
                [(field.name, field_cpp_type(field)) for field in variant.modifier_fields],
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
            field_cpp_type(variants["AllSync"].operand_layouts[0].fields[0]),
            "WithLocs<ResolvedRegisterOrSink>",
        )
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
        self.assertTrue(paired[0].allow_destination_sink)
        self.assertTrue(paired[0].allow_predicate_sink)
        self.assertFalse(variants["AnySync"].operand_layouts[0].bindings[0].allow_destination_sink)
        self.assertFalse(variants["AnySync"].operand_layouts[0].bindings[0].allow_predicate_sink)
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_descriptor.gen.cpp"
            generate_resolved_descriptor_source(build_test_generation_context(database), category="parallel_synchronization_and_communication", output_path=output_path)
            source = output_path.read_text(encoding="utf-8")
        self.assertIn('.allow_predicate_sink = true,', source)
        self.assertIn("ResolvedValueKind::RegisterOrSink", source)
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir_parallel.gen.cpp"
            generate_resolved_opcode_source(build_test_generation_context(database),
                category="parallel_synchronization_and_communication",
                opcode="match",
                output_path=output_path,
            )
            source = output_path.read_text(encoding="utf-8")
        self.assertIn(".is_sink =", source)
        self.assertIn("!(*selected.dst_register_or_sink).value.register_ref", source)
        for binding in (*variants["AnySync"].operand_layouts[0].bindings,
                        *plain, *paired):
            self.assertEqual(
                binding.register_width_policy, ResolvedRegisterWidthPolicy.SAME_WIDTH
            )

    def test_redux_sync_model_variants_and_availability(self) -> None:
        database = self.database
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
                [(field.name, field_cpp_type(field)) for field in variant.modifier_fields],
                [("sync", "bool"), (name.removeprefix("Sync").lower(), "bool"),
                 ("type", "WithLocs<ScalarType>")],
            )
        boolean = variants["SyncBoolean"]
        self.assertEqual(dict(boolean.availability), {"ptx": "7.0", "sm": 80})
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in boolean.modifier_fields],
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
                [(field.name, field_cpp_type(field)) for field in variant.modifier_fields],
                [("sync", "bool"), (operation, "bool"), ("abs", "WithLocs<bool>"),
                 ("nan", "WithLocs<bool>"), ("type", "ScalarType")],
            )
        for variant in variants.values():
            self.assertEqual(len(variant.operand_layouts), 1)
            self.assertEqual(
                [binding.register_width_policy for binding in variant.operand_layouts[0].bindings],
                [ResolvedRegisterWidthPolicy.SAME_WIDTH] * 3,
            )

    def test_griddepcontrol_model_has_two_zero_operand_actions(self) -> None:
        database = self.database
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
                [(field.name, field_cpp_type(field)) for field in variant.fields],
                [(action, "bool")],
            )
            self.assertEqual(
                [(layout.layout_id, layout.bindings)
                 for layout in variant.operand_layouts],
                [("default", ())],
            )

    def test_elect_sync_model_allows_only_its_destination_sink(self) -> None:
        database = self.database
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
            [(field.name, field_cpp_type(field)) for field in variant.fields],
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
        self.assertFalse(result.allow_predicate_sink)
        self.assertEqual(membermask.register_width_policy, ResolvedRegisterWidthPolicy.SAME_WIDTH)
        self.assertFalse(membermask.allow_destination_sink)
        self.assertFalse(membermask.allow_predicate_sink)
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_descriptor.gen.cpp"
            generate_resolved_descriptor_source(build_test_generation_context(database), category="parallel_synchronization_and_communication", output_path=output_path)
            source = output_path.read_text(encoding="utf-8")
        self.assertIn('.allow_destination_sink = true,', source)

    def test_bra_uses_a_binding_aware_branch_target(self) -> None:
        database = self.database
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
            [(field.name, field_cpp_type(field)) for field in variant.fields],
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
        self.assertIs(variant.rule, SemanticRule.CONTROL_FLOW_BRA)

    def test_brx_uses_a_u32_register_and_branch_target_set(self) -> None:
        database = self.database
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
            [(field.name, field_cpp_type(field)) for field in variant.fields],
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
        self.assertIs(variant.rule, SemanticRule.CONTROL_FLOW_BRX_IDX)

    def test_ret_preserves_optional_uniformity_in_its_zero_operand_variant(self) -> None:
        """Keep the canonical class while retaining the optional assertion."""
        database = self.database
        ret = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "ret"
        )
        instruction = from_instruction_spec(ret)

        modifier = ret.variants[0].modifiers[0]
        self.assertEqual(modifier.name, "uni")
        self.assertEqual(modifier.kind.value, "flag")
        self.assertEqual(modifier.presence.value, "optional")
        self.assertEqual(modifier.token, ".uni")
        self.assertIs(modifier.default, False)
        self.assertEqual(instruction.cpp_name, "Ret")
        self.assertEqual(len(instruction.variants), 1)
        variant = instruction.variants[0]
        self.assertEqual(variant.cpp_name, "Bare")
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in variant.fields],
            [("uni", "WithLocs<bool>")],
        )
        binding = variant.modifier_bindings[0]
        self.assertIsNotNone(binding.default_value)
        assert binding.default_value is not None
        self.assertIs(binding.default_value.value, False)
        self.assertEqual(binding.default_value.value_kind.value, "Bool")
        self.assertEqual(variant.operand_layouts[0].fields, ())
        self.assertEqual(variant.operand_layouts[0].bindings, ())

    def test_exit_uses_a_bare_zero_operand_variant(self) -> None:
        database = self.database
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
        database = self.database
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

    def test_and_models_predicate_and_all_bit_widths(self) -> None:
        database = self.database
        and_instruction = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "and"
        )
        instruction = from_instruction_spec(and_instruction)

        self.assertEqual(instruction.cpp_name, "And")
        self.assertEqual([variant.cpp_name for variant in instruction.variants],
                         ["Pred", "B16", "B32", "B64"])
        variant = instruction.variants[2]
        self.assertEqual(
            [binding.type_expression.modifier_field_id
             for binding in variant.operand_layouts[0].bindings],
            ["type", "type", "type"],
        )

    def test_or_models_predicate_and_all_bit_widths(self) -> None:
        database = self.database
        or_instruction = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "or"
        )
        instruction = from_instruction_spec(or_instruction)

        self.assertEqual(instruction.cpp_name, "Or")
        self.assertEqual([variant.cpp_name for variant in instruction.variants],
                         ["Pred", "B16", "B32", "B64"])
        self.assertEqual(
            [binding.type_expression.modifier_field_id
             for binding in instruction.variants[2].operand_layouts[0].bindings],
            ["type", "type", "type"],
        )

    def test_xor_models_predicate_and_all_bit_widths(self) -> None:
        database = self.database
        xor = next(item for item in database.instructions if item.opcode == "xor")
        instruction = from_instruction_spec(xor)
        self.assertEqual(instruction.cpp_name, "Xor")
        self.assertEqual([variant.cpp_name for variant in instruction.variants],
                         ["Pred", "B16", "B32", "B64"])
        self.assertEqual(
            [binding.type_expression.modifier_field_id
             for binding in instruction.variants[2].operand_layouts[0].bindings],
            ["type", "type", "type"],
        )

    def test_not_models_predicate_and_all_bit_widths(self) -> None:
        database = self.database
        not_instruction = next(item for item in database.instructions if item.opcode == "not")
        instruction = from_instruction_spec(not_instruction)
        self.assertEqual(instruction.cpp_name, "Not")
        self.assertEqual([variant.cpp_name for variant in instruction.variants],
                         ["Pred", "B16", "B32", "B64"])
        self.assertEqual(
            [binding.type_expression.modifier_field_id
             for binding in instruction.variants[2].operand_layouts[0].bindings],
            ["type", "type"],
        )

    def test_shl_models_all_bit_widths_with_u32_amount(self) -> None:
        database = self.database
        shl = next(item for item in database.instructions if item.opcode == "shl")
        instruction = from_instruction_spec(shl)
        self.assertEqual(instruction.cpp_name, "Shl")
        self.assertEqual([variant.cpp_name for variant in instruction.variants],
                         ["B16", "B32", "B64"])
        self.assertEqual(
            [binding.type_expression.modifier_field_id
             for binding in instruction.variants[1].operand_layouts[0].bindings],
            ["type", "type", None],
        )

    def test_shr_models_bit_signed_and_unsigned_widths(self) -> None:
        database = self.database
        shr = next(item for item in database.instructions if item.opcode == "shr")
        instruction = from_instruction_spec(shr)
        self.assertEqual(instruction.cpp_name, "Shr")
        self.assertEqual(
            [variant.cpp_name for variant in instruction.variants],
            ["B16", "B32", "B64", "U16", "U32", "U64", "S16", "S32", "S64"],
        )
        self.assertEqual(
            [binding.type_expression.modifier_field_id
             for binding in instruction.variants[4].operand_layouts[0].bindings],
            ["type", "type", None],
        )

    def test_mov_uses_scalar_and_predicate_sources(self) -> None:
        database = self.database
        mov = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "mov"
        )
        instruction = from_instruction_spec(mov)

        self.assertEqual(instruction.cpp_name, "Mov")
        self.assertEqual(len(instruction.variants), 4)
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
            [(field.name, field_cpp_type(field)) for field in variant.fields],
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
        self.assertTrue(source_binding.allow_function_symbol)
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

        pack_unpack = instruction.variants[1]
        self.assertEqual(pack_unpack.cpp_name, "B128PackUnpack")
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in pack_unpack.fields],
            [
                ("type", "ScalarType"),
                ("dst", "WithLocs<ResolvedRegisterRef>"),
                ("src", "WithLocs<ResolvedRegisterVector>"),
                ("dst", "WithLocs<ResolvedRegisterVector>"),
                ("src", "WithLocs<ResolvedRegisterRef>"),
            ],
        )
        self.assertEqual(
            [layout.layout_id for layout in pack_unpack.operand_layouts],
            ["pack", "unpack"],
        )
        self.assertIs(
            mov.variants[1].modifiers[0].presence,
            ModifierPresence.FIXED,
        )
        self.assertEqual(mov.variants[1].modifiers[0].value, "b128")
        self.assertEqual(
            pack_unpack.operand_layouts[0].bindings[1].allowed_vector_arities,
            (2, 4),
        )

        vector = instruction.variants[2]
        self.assertEqual(vector.cpp_name, "V4U32")
        self.assertEqual(sum(v.cpp_name == "V4U32" for v in instruction.variants), 1)
        self.assertEqual(tuple(value.value for value in mov.variants[2].modifiers[0].values), ("v2", "v4"))
        self.assertEqual(vector.operand_layouts[0].fields[1].value_kind,
                         ResolvedValueKind.MOV_VECTOR_SOURCE)
        self.assertTrue(vector.operand_layouts[0].bindings[0].allow_named_vector)
        self.assertTrue(vector.operand_layouts[0].bindings[0].allow_vector_sink)
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in vector.fields],
            [
                ("vector", "WithLocs<VectorArity>"),
                ("type", "WithLocs<ScalarType>"),
                ("dst", "WithLocs<ResolvedRegisterVector>"),
                ("src", "WithLocs<ResolvedMovVectorSource>"),
            ],
        )
        for binding in vector.operand_layouts[0].bindings:
            self.assertEqual(binding.allowed_shapes, (ResolvedOperandShape.VECTOR,))
            self.assertEqual(binding.vector_arity_modifier_field_id, "vector")

        predicate = instruction.variants[3]
        self.assertEqual(predicate.cpp_name, "Pred")
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in predicate.fields],
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
                ResolvedOperandShape.IMMEDIATE,
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

    def test_mapa_uses_cluster_address_without_function_symbols(self) -> None:
        database = self.database
        mapa = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "mapa"
        )
        instruction = from_instruction_spec(mapa)

        self.assertEqual(instruction.cpp_name, "Mapa")
        self.assertEqual(
            [variant.cpp_name for variant in instruction.variants],
            ["SharedCluster", "Generic"],
        )
        shared, generic = instruction.variants
        self.assertEqual(
            dict(shared.availability),
            {"any_of": [{"ptx": "7.8", "sm": 90, "capabilities": ["cluster"]}]},
        )
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in shared.fields],
            [
                ("shared_cluster", "bool"),
                ("type", "WithLocs<ScalarType>"),
                ("dst", "WithLocs<ResolvedRegisterRef>"),
                ("src", "WithLocs<ResolvedMovSource>"),
                ("rank", "WithLocs<RegOrImm>"),
            ],
        )
        shared_source = shared.operand_layouts[0].bindings[1]
        self.assertEqual(
            shared_source.allowed_shapes,
            (
                ResolvedOperandShape.REGISTER,
                ResolvedOperandShape.SYMBOL,
                ResolvedOperandShape.ADDRESS,
            ),
        )
        self.assertFalse(shared_source.allow_function_symbol)
        self.assertEqual(
            [value.value for value in shared_source.allowed_address_state_spaces],
            ["shared"],
        )
        self.assertEqual(
            [binding.register_width_policy for binding in shared.operand_layouts[0].bindings],
            [ResolvedRegisterWidthPolicy.SAME_WIDTH] * 3,
        )
        self.assertEqual(
            [binding.type_expression.scalar_type
             for binding in generic.operand_layouts[0].bindings],
            [None, None, "u32"],
        )
        self.assertEqual(
            [binding.register_width_policy for binding in generic.operand_layouts[0].bindings],
            [ResolvedRegisterWidthPolicy.SAME_WIDTH] * 3,
        )

    def test_getctarank_uses_cluster_address_and_u32_rank_destination(self) -> None:
        database = self.database
        getctarank = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "getctarank"
        )
        instruction = from_instruction_spec(getctarank)

        self.assertEqual(instruction.cpp_name, "Getctarank")
        self.assertEqual(
            [variant.cpp_name for variant in instruction.variants],
            ["SharedCluster", "Generic"],
        )
        shared, generic = instruction.variants
        self.assertEqual(
            dict(shared.availability),
            {"any_of": [{"ptx": "7.8", "sm": 90, "capabilities": ["cluster"]}]},
        )
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in shared.fields],
            [
                ("shared_cluster", "bool"),
                ("type", "WithLocs<ScalarType>"),
                ("dst", "WithLocs<ResolvedRegisterRef>"),
                ("src", "WithLocs<ResolvedMovSource>"),
            ],
        )
        dst, shared_source = shared.operand_layouts[0].bindings
        self.assertEqual(dst.type_expression.scalar_type, "u32")
        self.assertEqual(
            dst.register_width_policy, ResolvedRegisterWidthPolicy.SAME_WIDTH
        )
        self.assertEqual(
            shared_source.allowed_shapes,
            (
                ResolvedOperandShape.REGISTER,
                ResolvedOperandShape.SYMBOL,
                ResolvedOperandShape.ADDRESS,
            ),
        )
        self.assertFalse(shared_source.allow_function_symbol)
        self.assertEqual(
            [value.value for value in shared_source.allowed_address_state_spaces],
            ["shared"],
        )
        self.assertEqual(
            shared_source.register_width_policy,
            ResolvedRegisterWidthPolicy.SAME_WIDTH,
        )
        generic_dst, generic_source = generic.operand_layouts[0].bindings
        self.assertEqual(generic_dst.type_expression.scalar_type, "u32")
        self.assertEqual(
            generic_dst.register_width_policy,
            ResolvedRegisterWidthPolicy.SAME_WIDTH,
        )
        self.assertEqual(
            generic_source.register_width_policy,
            ResolvedRegisterWidthPolicy.SAME_WIDTH,
        )
        self.assertEqual(
            generic_source.type_expression.modifier_field_id, "type"
        )

    def test_cvt_and_isspacep_register_width_policies(self) -> None:
        cvt = from_instruction_spec(next(
            instruction
            for instruction in self.database.instructions
            if instruction.opcode == "cvt"
        ))
        rn_f32_s32 = next(
            variant for variant in cvt.variants if variant.cpp_name == "RnF32S32"
        )
        self.assertEqual(
            [binding.register_width_policy
             for binding in rn_f32_s32.operand_layouts[0].bindings],
            [ResolvedRegisterWidthPolicy.EQUAL_OR_WIDER,
             ResolvedRegisterWidthPolicy.EQUAL_OR_WIDER],
        )
        rn_f16x2_f32 = next(
            variant for variant in cvt.variants if variant.cpp_name == "RnF16x2F32"
        )
        self.assertEqual(
            [binding.type_expression.scalar_type
             for binding in rn_f16x2_f32.operand_layouts[0].bindings],
            ["f16x2", "f32", "f32"],
        )
        self.assertEqual(
            [binding.register_width_policy
             for binding in rn_f16x2_f32.operand_layouts[0].bindings],
            [ResolvedRegisterWidthPolicy.EQUAL_OR_WIDER] * 3,
        )

        expected_pack_variants = {
            "PackSatU8S32B32": (
                ["pack", "sat", "dst_type", "src_type", "carry_type"],
                ["u32", "s32", "s32", "b32"],
            ),
            "PackSat16S32": (
                ["pack", "sat", "dst_type", "src_type"],
                ["u32", "s32", "s32"],
            ),
            "PackSatSmallS32B32": (
                ["pack", "sat", "dst_type", "src_type", "carry_type"],
                ["u32", "s32", "s32", "b32"],
            ),
        }
        pack_variants = [
            variant for variant in cvt.variants
            if variant.cpp_name in expected_pack_variants
        ]
        self.assertEqual(
            [variant.cpp_name for variant in pack_variants],
            list(expected_pack_variants),
        )
        for variant in pack_variants:
            modifier_names, operand_types = expected_pack_variants[
                variant.cpp_name
            ]
            self.assertEqual(
                [field.name for field in variant.modifier_fields], modifier_names
            )
            self.assertEqual(
                [binding.type_expression.scalar_type
                 for binding in variant.operand_layouts[0].bindings],
                operand_types,
            )
            self.assertEqual(
                [binding.register_width_policy
                 for binding in variant.operand_layouts[0].bindings],
                [ResolvedRegisterWidthPolicy.SAME_WIDTH] * len(operand_types),
            )
        self.assertIsNone(pack_variants[1].modifier_fields[2].constant_value)
        self.assertIsNone(pack_variants[2].modifier_fields[2].constant_value)
        self.assertEqual(
            pack_variants[0].operand_layouts[0].bindings[-1].allowed_shapes,
            (ResolvedOperandShape.REGISTER, ResolvedOperandShape.IMMEDIATE),
        )
        self.assertEqual(
            pack_variants[2].operand_layouts[0].bindings[-1].allowed_shapes,
            (ResolvedOperandShape.REGISTER, ResolvedOperandShape.IMMEDIATE),
        )
        self.assertEqual(
            [(entry.value, dict(entry.availability))
             for entry in pack_variants[2].modifier_value_availabilities],
            [
                ("u4", {"ptx": "6.5", "sm": 75}),
                ("s4", {"ptx": "6.5", "sm": 75}),
                ("u2", {"ptx": "6.5", "sm": 75}),
                ("s2", {"ptx": "6.5", "sm": 75}),
            ],
        )

        isspacep = from_instruction_spec(next(
            instruction
            for instruction in self.database.instructions
            if instruction.opcode == "isspacep"
        ))
        expected_isspacep = {
            "GlobalU64": (["state_space"], {"ptx": "2.0", "sm": 20}),
            "Const": (["state_space"], {"ptx": "3.1", "sm": 20}),
            "Local": (["state_space"], {"ptx": "2.0", "sm": 20}),
            "Shared": (["state_space"], {"ptx": "2.0", "sm": 20}),
            "SharedCta": (["shared_cta"], {"ptx": "7.8", "sm": 30}),
            "SharedCluster": (
                ["shared_cluster"], {"ptx": "7.8", "sm": 90}
            ),
            "Param": (["state_space"], {"ptx": "7.7", "sm": 70}),
            "ParamEntry": (["state_space"], {"ptx": "8.3", "sm": 70}),
        }
        self.assertEqual(
            [variant.cpp_name for variant in isspacep.variants],
            list(expected_isspacep),
        )
        for variant in isspacep.variants:
            modifier_names, availability = expected_isspacep[variant.cpp_name]
            self.assertEqual(
                [field.name for field in variant.modifier_fields], modifier_names
            )
            self.assertTrue(variant.modifier_fields[0].constant_value)
            self.assertEqual(dict(variant.availability), availability)
            bindings = variant.operand_layouts[0].bindings
            self.assertEqual(
                [binding.type_expression.scalar_type for binding in bindings],
                ["pred", "u32"],
            )
            self.assertEqual(
                [binding.register_width_policy for binding in bindings],
                [ResolvedRegisterWidthPolicy.SAME_WIDTH,
                 ResolvedRegisterWidthPolicy.EQUAL_OR_WIDER],
            )

    def test_prmt_generic_and_specialized_selector_variants(self) -> None:
        prmt = from_instruction_spec(next(
            instruction
            for instruction in self.database.instructions
            if instruction.opcode == "prmt"
        ))
        expected_names = [
            "GenericB32", "F4eB32", "B4eB32", "Rc8B32", "EclB32",
            "EcrB32", "Rc16B32",
        ]
        self.assertEqual([variant.cpp_name for variant in prmt.variants],
                         expected_names)
        for variant in prmt.variants:
            self.assertEqual(dict(variant.availability), {"ptx": "2.0", "sm": 20})
            self.assertEqual(
                [field.name for field in variant.modifier_fields],
                ["type"] if variant.cpp_name == "GenericB32"
                else ["type", variant.cpp_name.removesuffix("B32").lower()],
            )
            self.assertEqual(
                [binding.type_expression.scalar_type
                 for binding in variant.operand_layouts[0].bindings],
                ["b32"] * 4,
            )
            self.assertEqual(
                [binding.register_width_policy
                 for binding in variant.operand_layouts[0].bindings],
                [ResolvedRegisterWidthPolicy.SAME_WIDTH] * 4,
            )

    def test_cvta_unqualified_state_space_variants(self) -> None:
        cvta = from_instruction_spec(next(
            instruction
            for instruction in self.database.instructions
            if instruction.opcode == "cvta"
        ))
        expected_names = [
            "GlobalU64", "ToGlobalU64", "GlobalU32", "ToGlobalU32",
            "LocalU32", "ToLocalU32", "LocalU64", "ToLocalU64",
            "SharedU32", "ToSharedU32", "SharedU64", "ToSharedU64",
            "ConstU32", "ToConstU32", "ConstU64", "ToConstU64",
            "ParamU32", "ToParamU32", "ParamU64", "ToParamU64",
            "SharedCtaU32", "ToSharedCtaU32", "SharedCtaU64",
            "ToSharedCtaU64", "SharedClusterU32", "ToSharedClusterU32",
            "SharedClusterU64", "ToSharedClusterU64", "ParamEntryU32",
            "ToParamEntryU32", "ParamEntryU64", "ToParamEntryU64",
        ]
        self.assertEqual([variant.cpp_name for variant in cvta.variants],
                         expected_names)
        expected_availability = {
            "Global": {"ptx": "2.0", "sm": 20},
            "Local": {"ptx": "2.0", "sm": 20},
            "Shared": {"ptx": "2.0", "sm": 20},
            "Const": {"ptx": "3.1", "sm": 20},
            "Param": {"ptx": "7.7", "sm": 70},
            "SharedCta": {"ptx": "7.8", "sm": 30},
            "SharedCluster": {"ptx": "7.8", "sm": 90},
            "ParamEntry": {"ptx": "8.3", "sm": 70},
        }
        for variant in cvta.variants:
            has_to = variant.cpp_name.startswith("To")
            self.assertEqual(
                [field.name for field in variant.modifier_fields],
                ["to", "state_space", "type"]
                if has_to else ["state_space", "type"],
            )
            if has_to:
                self.assertTrue(variant.modifier_fields[0].constant_value)
            state_space = next(
                field.constant_value
                for field in variant.modifier_fields
                if field.name == "state_space"
            )
            type_name = next(
                field.constant_value
                for field in variant.modifier_fields
                if field.name == "type"
            )
            self.assertEqual(type_name,
                             "u32" if variant.cpp_name.endswith("U32") else "u64")
            expected_key = {
                "global": "Global",
                "local": "Local",
                "shared": "Shared",
                "shared::cta": "SharedCta",
                "shared::cluster": "SharedCluster",
                "const": "Const",
                "param": "Param",
                "param::entry": "ParamEntry",
            }[state_space]
            self.assertEqual(dict(variant.availability),
                             expected_availability[expected_key])
            self.assertEqual(
                [binding.register_width_policy
                 for binding in variant.operand_layouts[0].bindings],
                [ResolvedRegisterWidthPolicy.SAME_WIDTH] * 2,
            )
            source = variant.operand_layouts[0].bindings[1]
            self.assertEqual(
                source.allowed_shapes,
                (ResolvedOperandShape.REGISTER,)
                if has_to
                else (
                    ResolvedOperandShape.REGISTER,
                    ResolvedOperandShape.SYMBOL,
                    ResolvedOperandShape.ADDRESS,
                ),
            )
            self.assertEqual(source.preserve_parameter_address_space, not has_to)

    def test_reference_visitor_rejects_missing_mov_source_binding(self) -> None:
        """Reference generation rejects malformed MOV-source layouts explicitly."""

        cvta = from_instruction_spec(next(
            instruction
            for instruction in self.database.instructions
            if instruction.opcode == "cvta"
        ))
        const_u32 = next(
            variant for variant in cvta.variants
            if variant.cpp_name == "ConstU32"
        )
        layout = const_u32.operand_layouts[0]
        malformed_layout = replace(
            layout,
            bindings=tuple(
                binding for binding in layout.bindings
                if binding.target_field_id != "src"
            ),
        )
        source_field = next(
            field for field in malformed_layout.fields if field.name == "src"
        )
        with self.assertRaisesRegex(
            ValueError, r"layout .* lacks a MOV_SOURCE binding for field 'src'"
        ):
            _address_symbol_resolution_policy(source_field, malformed_layout)

        mov = from_instruction_spec(next(
            instruction
            for instruction in self.database.instructions
            if instruction.opcode == "mov"
        ))
        mov_layout = next(
            layout for variant in mov.variants for layout in variant.operand_layouts
            if any(field.value_kind is ResolvedValueKind.MOV_SOURCE
                   for field in layout.fields)
        )
        mov_field = next(
            field for field in mov_layout.fields
            if field.value_kind is ResolvedValueKind.MOV_SOURCE
        )
        self.assertEqual(
            _address_symbol_resolution_policy(mov_field, mov_layout),
            "checker::AddressSymbolResolutionPolicy::MaterializeDeviceParameter",
        )
        self.assertEqual(
            _address_symbol_resolution_policy(
                source_field, const_u32.operand_layouts[0]
            ),
            "checker::AddressSymbolResolutionPolicy::PreserveDeclarationSpace",
        )
        with tempfile.TemporaryDirectory() as directory:
            context = build_test_generation_context(self.database)
            mov_source = Path(directory) / "mov.cpp"
            cvta_source = Path(directory) / "cvta.cpp"
            for opcode, path in (("mov", mov_source), ("cvta", cvta_source)):
                generate_resolved_opcode_source(
                    context, category="data_movement", opcode=opcode,
                    output_path=path,
                )
            mov_text = mov_source.read_text(encoding="utf-8")
            cvta_text = cvta_source.read_text(encoding="utf-8")
        self.assertIn("observer.mov_source(", mov_text)
        self.assertIn(
            "checker::AddressSymbolResolutionPolicy::MaterializeDeviceParameter);",
            mov_text,
        )
        self.assertIn("observer.mov_source(", cvta_text)
        self.assertIn(
            "checker::AddressSymbolResolutionPolicy::PreserveDeclarationSpace);",
            cvta_text,
        )

    def test_mbarrier_init_models_layout_space_and_count_ranges(self) -> None:
        mbarrier = next(
            instruction
            for instruction in self.database.instructions
            if instruction.opcode == "mbarrier"
        )
        instruction = from_instruction_spec(mbarrier)

        self.assertEqual(instruction.cpp_name, "Mbarrier")
        self.assertEqual(
            [variant.cpp_name for variant in instruction.variants],
            ["InitGenericV0", "InitSharedV0", "InitSharedCtaV0",
             "InitGenericV1", "InitSharedV1", "InitSharedCtaV1",
             "InvalGeneric", "InvalShared", "InvalSharedCta",
             "ExpectTxGenericOrShared", "ExpectTxSharedCta", "ExpectTxSharedCluster",
             "ExpectTxRelaxedCtaGenericOrShared", "ExpectTxRelaxedCtaSharedCta",
             "ExpectTxRelaxedCtaSharedCluster", "ExpectTxRelaxedClusterGenericOrShared",
             "ExpectTxRelaxedClusterSharedCta", "ExpectTxRelaxedClusterSharedCluster",
             "CompleteTxGenericOrShared", "CompleteTxSharedCta", "CompleteTxSharedCluster",
             "CompleteTxRelaxedCtaGenericOrShared", "CompleteTxRelaxedCtaSharedCta",
             "CompleteTxRelaxedCtaSharedCluster", "CompleteTxRelaxedClusterGenericOrShared",
             "CompleteTxRelaxedClusterSharedCta", "CompleteTxRelaxedClusterSharedCluster",
             "ArriveGenericOrShared", "ArriveSharedCta", "ArriveSharedCluster",
             "ArriveSemanticsGenericOrShared", "ArriveSemanticsSharedCta",
             "ArriveSemanticsSharedCluster", "ArriveExpectTxGenericOrShared",
             "ArriveExpectTxSharedCta", "ArriveExpectTxSharedCluster",
             "ArriveExpectTxSemanticsGenericOrShared", "ArriveExpectTxSemanticsSharedCta",
             "ArriveExpectTxSemanticsSharedCluster", "ArriveNoCompleteGenericOrShared",
             "ArriveNoCompleteSharedCta", "ArriveNoCompleteReleaseCtaGenericOrShared",
             "ArriveNoCompleteReleaseCtaSharedCta", "ArriveDropGenericOrShared",
             "ArriveDropSharedCta", "ArriveDropSharedCluster",
             "ArriveDropSemanticsGenericOrShared", "ArriveDropSemanticsSharedCta",
             "ArriveDropSemanticsSharedCluster", "ArriveDropExpectTxGenericOrShared",
             "ArriveDropExpectTxSharedCta", "ArriveDropExpectTxSharedCluster",
             "ArriveDropExpectTxSemanticsGenericOrShared",
             "ArriveDropExpectTxSemanticsSharedCta",
             "ArriveDropExpectTxSemanticsSharedCluster",
             "ArriveDropNoCompleteGenericOrShared", "ArriveDropNoCompleteSharedCta",
             "ArriveDropNoCompleteReleaseCtaGenericOrShared",
             "ArriveDropNoCompleteReleaseCtaSharedCta",
             "TestWaitTokenGenericOrShared", "TestWaitTokenSharedCta",
             "TestWaitParityGenericOrShared", "TestWaitParitySharedCta",
             "TryWaitTokenGenericOrShared", "TryWaitTokenSharedCta",
             "TryWaitParityGenericOrShared", "TryWaitParitySharedCta",
             "TestWaitTokenPrimaryGenericOrShared", "TestWaitTokenPrimarySharedCta",
             "TestWaitParityPrimaryGenericOrShared", "TestWaitParityPrimarySharedCta",
             "TestWaitParityConditionalGenericOrShared", "TestWaitParityConditionalSharedCta",
             "TryWaitTokenPrimaryGenericOrShared", "TryWaitTokenPrimarySharedCta",
             "TryWaitParityPrimaryGenericOrShared", "TryWaitParityPrimarySharedCta",
             "TryWaitParityConditionalGenericOrShared", "TryWaitParityConditionalSharedCta",
             "TestWaitTokenSemanticsGenericOrShared", "TestWaitTokenSemanticsSharedCta",
             "TestWaitParitySemanticsGenericOrShared", "TestWaitParitySemanticsSharedCta",
             "TestWaitTokenPrimarySemanticsGenericOrShared",
             "TestWaitTokenPrimarySemanticsSharedCta",
             "TestWaitParityPrimarySemanticsGenericOrShared",
             "TestWaitParityPrimarySemanticsSharedCta",
             "TestWaitParityConditionalSemanticsGenericOrShared",
             "TestWaitParityConditionalSemanticsSharedCta",
             "TryWaitTokenSemanticsGenericOrShared", "TryWaitTokenSemanticsSharedCta",
             "TryWaitParitySemanticsGenericOrShared", "TryWaitParitySemanticsSharedCta",
             "TryWaitTokenPrimarySemanticsGenericOrShared",
             "TryWaitTokenPrimarySemanticsSharedCta",
             "TryWaitParityPrimarySemanticsGenericOrShared",
             "TryWaitParityPrimarySemanticsSharedCta",
             "TryWaitParityConditionalSemanticsGenericOrShared",
             "TryWaitParityConditionalSemanticsSharedCta",
             "PendingCount", "CheckLayoutGenericV0", "CheckLayoutGenericV1",
             "CheckLayoutSharedCtaV0", "CheckLayoutSharedCtaV1"],
        )
        generic_v0, _, shared_cta_v0, generic_v1, _, _, inval_generic, _, inval_shared_cta = instruction.variants[:9]
        expect_tx_generic, _, _, expect_tx_relaxed_cta, _, _, expect_tx_relaxed_cluster, _, _ = instruction.variants[9:18]
        complete_tx_generic, _, _, complete_tx_relaxed_cta, _, _, complete_tx_relaxed_cluster, _, _ = instruction.variants[18:27]
        self.assertEqual(dict(generic_v0.availability), {"ptx": "7.0", "sm": 80})
        self.assertEqual(dict(shared_cta_v0.availability), {"ptx": "7.8", "sm": 80})
        self.assertEqual(dict(generic_v1.availability), {"ptx": "9.3", "sm": 90})
        self.assertEqual(dict(inval_generic.availability), {"ptx": "7.0", "sm": 80})
        self.assertEqual(dict(inval_shared_cta.availability), {"ptx": "7.8", "sm": 80})
        self.assertEqual(dict(expect_tx_generic.availability), {"ptx": "8.0", "sm": 90})
        self.assertEqual(dict(expect_tx_relaxed_cta.availability), {"ptx": "8.0", "sm": 90})
        cluster_availability = {
            "any_of": [{"ptx": "8.0", "sm": 90, "capabilities": ["cluster"]}],
        }
        cluster_only_variants = [
            variant for variant in instruction.variants
            if "SharedCluster" in variant.cpp_name or "RelaxedCluster" in variant.cpp_name
        ]
        self.assertEqual(len(cluster_only_variants), 18)
        self.assertTrue(all(dict(variant.availability) == cluster_availability
                            for variant in cluster_only_variants))
        self.assertEqual(dict(expect_tx_relaxed_cluster.availability), cluster_availability)
        self.assertEqual(dict(complete_tx_generic.availability), {"ptx": "8.0", "sm": 90})
        self.assertEqual(dict(complete_tx_relaxed_cta.availability), {"ptx": "8.0", "sm": 90})
        self.assertEqual(dict(complete_tx_relaxed_cluster.availability), cluster_availability)
        arrive_generic, _, arrive_cluster, arrive_semantics, _, _, arrive_expect, _, _, arrive_expect_semantics, _, _, arrive_no_complete, _, arrive_no_complete_explicit, _ = instruction.variants[27:43]
        arrive_drop_generic, _, arrive_drop_cluster, arrive_drop_semantics, _, _, arrive_drop_expect, _, _, arrive_drop_expect_semantics, _, _, arrive_drop_no_complete, _, arrive_drop_no_complete_explicit, _ = instruction.variants[43:59]
        test_wait_token, test_wait_token_cta, test_wait_parity, test_wait_parity_cta = instruction.variants[59:63]
        try_wait_token, try_wait_token_cta, try_wait_parity, try_wait_parity_cta = instruction.variants[63:67]
        test_wait_primary_token, _, test_wait_primary_parity, _, test_wait_conditional, _ = instruction.variants[67:73]
        try_wait_primary_token, _, try_wait_primary_parity, _, try_wait_conditional, _ = instruction.variants[73:79]
        paired_waits = instruction.variants[79:89]
        paired_try_waits = instruction.variants[89:99]
        pending_count = instruction.variants[99]
        check_layout_generic_v0, check_layout_generic_v1, check_layout_shared_cta_v0, check_layout_shared_cta_v1 = instruction.variants[100:104]
        arrival_count_variants = [
            variant
            for variant in instruction.variants[27:59]
            if variant.immediate_ranges
        ]
        self.assertEqual(len(arrival_count_variants), 20)
        self.assertTrue(
            all(
                [(constraint.operand_field_id, constraint.minimum,
                  constraint.maximum) for constraint in variant.immediate_ranges]
                == [("count", 0, 1048575)]
                for variant in arrival_count_variants
            )
        )
        self.assertEqual(dict(arrive_generic.availability), {"ptx": "7.0", "sm": 80})
        self.assertEqual(dict(arrive_cluster.availability), cluster_availability)
        self.assertEqual(dict(arrive_semantics.availability), {"ptx": "8.0", "sm": 90})
        self.assertEqual(dict(arrive_expect.availability), {"ptx": "8.0", "sm": 90})
        self.assertEqual(dict(arrive_expect_semantics.availability), {"ptx": "8.0", "sm": 90})
        self.assertEqual(dict(arrive_no_complete.availability), {"ptx": "7.0", "sm": 80})
        self.assertEqual(dict(arrive_no_complete_explicit.availability), {"ptx": "8.0", "sm": 90})
        self.assertEqual(dict(arrive_drop_generic.availability), {"ptx": "7.0", "sm": 80})
        self.assertEqual(dict(arrive_drop_cluster.availability), cluster_availability)
        self.assertEqual(dict(arrive_drop_semantics.availability), {"ptx": "8.0", "sm": 90})
        self.assertEqual(dict(arrive_drop_expect.availability), {"ptx": "8.0", "sm": 90})
        self.assertEqual(dict(arrive_drop_expect_semantics.availability), {"ptx": "8.0", "sm": 90})
        self.assertEqual(dict(arrive_drop_no_complete.availability), {"ptx": "7.0", "sm": 80})
        self.assertEqual(dict(arrive_drop_no_complete_explicit.availability), {"ptx": "8.0", "sm": 90})
        cluster_scope_values = [
            entry
            for variant in instruction.variants
            for entry in variant.modifier_value_availabilities
            if entry.source_kind_id == "scope" and entry.value == "cluster"
        ]
        self.assertEqual(len(cluster_scope_values), 32)
        self.assertTrue(all(dict(entry.availability) == cluster_availability
                            for entry in cluster_scope_values))
        self.assertEqual(dict(test_wait_token.availability), {"ptx": "7.0", "sm": 80})
        self.assertEqual(dict(test_wait_token_cta.availability), {"ptx": "7.8", "sm": 80})
        self.assertEqual(dict(test_wait_parity.availability), {"ptx": "7.1", "sm": 80})
        self.assertEqual(dict(test_wait_parity_cta.availability), {"ptx": "7.8", "sm": 80})
        self.assertEqual(len(paired_waits), 10)
        for index, variant in enumerate(paired_waits):
            self.assertEqual(
                dict(variant.availability),
                {"ptx": "8.0", "sm": 80} if index < 4 else {"ptx": "9.3", "sm": 90},
            )
            modifiers = [(field.name, field_cpp_type(field))
                         for field in variant.modifier_fields]
            self.assertIn(("semantics", "WithLocs<MemoryConsistency>"), modifiers)
            self.assertIn(("scope", "WithLocs<MemoryScope>"), modifiers)
            names = [name for name, _ in modifiers]
            self.assertEqual(names.index("scope"), names.index("semantics") + 1)
            self.assertEqual(
                [entry.value for entry in variant.modifier_value_availabilities
                 if entry.source_kind_id == "semantics"],
                ["acquire", "relaxed"],
            )
            self.assertEqual(
                [entry.value for entry in variant.modifier_value_availabilities
                 if entry.source_kind_id == "scope"],
                ["cta", "cluster"],
            )
            self.assertEqual(variant.address_alignments[0].alignment, 8)
            if "Parity" in variant.cpp_name:
                self.assertEqual(
                    [(item.operand_field_id, item.minimum, item.maximum)
                     for item in variant.immediate_ranges],
                    [("phase_parity", 0, 1)],
                )
            if "Primary" in variant.cpp_name:
                self.assertEqual(
                    [layout.layout_id for layout in variant.operand_layouts],
                    ["default", "report_predicate", "report_predicate_value"],
                )
            if "Conditional" in variant.cpp_name:
                self.assertEqual(
                    [layout.layout_id for layout in variant.operand_layouts],
                    ["default"],
                )
        self.assertEqual(len(paired_try_waits), 10)
        for index, variant in enumerate(paired_try_waits):
            self.assertEqual(
                dict(variant.availability),
                {"ptx": "8.0", "sm": 90} if index < 4 else {"ptx": "9.3", "sm": 90},
            )
            modifiers = [(field.name, field_cpp_type(field))
                         for field in variant.modifier_fields]
            self.assertIn(("semantics", "WithLocs<MemoryConsistency>"), modifiers)
            self.assertIn(("scope", "WithLocs<MemoryScope>"), modifiers)
            names = [name for name, _ in modifiers]
            self.assertEqual(names.index("scope"), names.index("semantics") + 1)
            self.assertEqual(
                [entry.value for entry in variant.modifier_value_availabilities
                 if entry.source_kind_id == "semantics"],
                ["acquire", "relaxed"],
            )
            self.assertEqual(
                [entry.value for entry in variant.modifier_value_availabilities
                 if entry.source_kind_id == "scope"],
                ["cta", "cluster"],
            )
            self.assertEqual(variant.address_alignments[0].alignment, 8)
            if "Parity" in variant.cpp_name:
                self.assertEqual(
                    [(item.operand_field_id, item.minimum, item.maximum)
                     for item in variant.immediate_ranges],
                    [("phase_parity", 0, 1)],
                )
            layouts = [layout.layout_id for layout in variant.operand_layouts]
            if "Primary" in variant.cpp_name:
                self.assertEqual(layouts, ["no_hint", "with_hint",
                                           "report_predicate_no_hint",
                                           "report_predicate_with_hint",
                                           "report_predicate_value_no_hint",
                                           "report_predicate_value_with_hint"])
            else:
                self.assertEqual(layouts, ["no_hint", "with_hint"])
            hint = variant.operand_layouts[1].bindings[-1]
            self.assertEqual(hint.type_expression.scalar_type, "u32")
        self.assertEqual(
            [dict(variant.availability) for variant in
             (try_wait_token, try_wait_token_cta, try_wait_parity, try_wait_parity_cta)],
            [{"ptx": "7.8", "sm": 90}] * 4,
        )
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in test_wait_token.fields],
            [("test_wait", "bool"), ("state_space", "WithLocs<MemoryStateSpace>"),
             ("type", "ScalarType"), ("wait_complete", "WithLocs<ResolvedPredicate>"),
             ("address", "WithLocs<ResolvedAddress>"),
             ("state", "WithLocs<ResolvedMbarrierStateToken>")],
        )
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in test_wait_parity.fields],
            [("test_wait", "bool"), ("parity", "bool"),
             ("state_space", "WithLocs<MemoryStateSpace>"), ("type", "ScalarType"),
             ("wait_complete", "WithLocs<ResolvedPredicate>"),
             ("address", "WithLocs<ResolvedAddress>"),
             ("phase_parity", "WithLocs<RegOrImm>")],
        )
        state = test_wait_token.operand_layouts[0].bindings[-1]
        parity = test_wait_parity.operand_layouts[0].bindings[-1]
        self.assertEqual(state.mbarrier_state_token_form.value, "register")
        self.assertEqual(
            [dict(variant.availability) for variant in
             (check_layout_generic_v0, check_layout_generic_v1,
              check_layout_shared_cta_v0, check_layout_shared_cta_v1)],
            [{"ptx": "9.3", "sm": 90}] * 4,
        )
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in check_layout_generic_v0.fields],
            [("check_layout", "bool"), ("layout", "MbarrierLayout"),
             ("type", "ScalarType"), ("result", "WithLocs<ResolvedPredicate>"),
             ("address", "WithLocs<ResolvedAddress>")],
        )
        self.assertEqual(check_layout_generic_v0.modifier_fields[1].constant_value, "layout::v0")
        self.assertEqual(check_layout_generic_v1.modifier_fields[1].constant_value, "layout::v1")
        self.assertEqual(check_layout_shared_cta_v0.modifier_fields[2].constant_value, True)
        self.assertEqual(check_layout_shared_cta_v1.modifier_fields[1].constant_value, "layout::v1")
        self.assertEqual(state.register_width_policy, ResolvedRegisterWidthPolicy.EXACT)
        self.assertEqual(parity.type_expression.scalar_type, "u32")
        self.assertEqual(parity.register_width_policy, ResolvedRegisterWidthPolicy.SAME_WIDTH)
        self.assertEqual(
            [(constraint.minimum, constraint.maximum)
             for constraint in test_wait_parity.immediate_ranges],
            [(0, 1)],
        )
        self.assertEqual(
            [layout.layout_id for layout in try_wait_token.operand_layouts],
            ["no_hint", "with_hint"],
        )
        self.assertEqual(
            [(field.name, field_cpp_type(field))
             for field in try_wait_token.operand_layouts[0].fields],
            [("wait_complete", "WithLocs<ResolvedPredicate>"),
             ("address", "WithLocs<ResolvedAddress>"),
             ("state", "WithLocs<ResolvedMbarrierStateToken>")],
        )
        self.assertEqual(
            [(field.name, field_cpp_type(field))
             for field in try_wait_token.operand_layouts[1].fields[-2:]],
            [("state", "WithLocs<ResolvedMbarrierStateToken>"),
             ("time_hint", "WithLocs<RegOrImm>")],
        )
        self.assertEqual(
            [(field.name, field_cpp_type(field))
             for field in try_wait_parity.operand_layouts[1].fields[-2:]],
            [("phase_parity", "WithLocs<RegOrImm>"),
             ("time_hint", "WithLocs<RegOrImm>")],
        )
        self.assertEqual(
            [(constraint.minimum, constraint.maximum)
             for constraint in try_wait_parity.immediate_ranges],
            [(0, 1)],
        )
        self.assertEqual(dict(test_wait_primary_token.availability), {"ptx": "9.3", "sm": 90})
        self.assertEqual(dict(test_wait_primary_parity.availability), {"ptx": "9.3", "sm": 90})
        self.assertEqual(dict(test_wait_conditional.availability), {"ptx": "9.3", "sm": 90})
        self.assertEqual(
            [layout.layout_id for layout in test_wait_primary_token.operand_layouts],
            ["default", "report_predicate", "report_predicate_value"],
        )
        self.assertEqual(
            [layout.layout_id for layout in try_wait_primary_token.operand_layouts],
            ["no_hint", "with_hint", "report_predicate_no_hint", "report_predicate_with_hint", "report_predicate_value_no_hint", "report_predicate_value_with_hint"],
        )
        self.assertEqual(
            [layout.layout_id for layout in try_wait_conditional.operand_layouts],
            ["no_hint", "with_hint"],
        )
        self.assertEqual(dict(pending_count.availability), {"ptx": "7.0", "sm": 80})
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in pending_count.fields],
            [("pending_count", "bool"), ("layout", "WithLocs<MbarrierLayout>"),
             ("type", "ScalarType"), ("count", "WithLocs<ResolvedRegisterRef>"),
             ("state", "WithLocs<ResolvedMbarrierStateToken>")],
        )
        self.assertEqual(
            pending_count.modifier_bindings[1].default_value.value, "layout::v0" # pyright: ignore[reportOptionalMemberAccess]
        )
        self.assertEqual(
            dict(pending_count.modifier_value_availabilities[0].availability),
            {"ptx": "9.3", "sm": 90},
        )
        count, state = pending_count.operand_layouts[0].bindings
        self.assertEqual(count.type_expression.scalar_type, "u32")
        self.assertEqual(count.register_width_policy, ResolvedRegisterWidthPolicy.SAME_WIDTH)
        self.assertEqual(state.register_width_policy, ResolvedRegisterWidthPolicy.EXACT)
        self.assertEqual(state.mbarrier_state_token_form.value, "register")
        self.assertEqual(
            [layout.layout_id for layout in arrive_drop_generic.operand_layouts],
            ["no_count", "with_count"],
        )
        self.assertEqual(
            [layout.layout_id for layout in arrive_generic.operand_layouts],
            ["no_count", "with_count"],
        )
        self.assertEqual(
            arrive_generic.operand_layouts[0].bindings[0].mbarrier_state_token_form.value,
            "register_or_sink",
        )
        self.assertEqual(
            dict(arrive_generic.operand_layouts[0].bindings[0].sink_availability),
            {"ptx": "7.1", "sm": 80},
        )
        self.assertEqual(
            arrive_cluster.operand_layouts[0].bindings[0].mbarrier_state_token_form.value,
            "sink",
        )
        self.assertEqual(
            arrive_generic.operand_layouts[1].bindings[-1].register_width_policy,
            ResolvedRegisterWidthPolicy.SAME_WIDTH,
        )
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in generic_v0.fields],
            [
                ("init", "bool"),
                ("layout", "WithLocs<MbarrierLayout>"),
                ("type", "ScalarType"),
                ("address", "WithLocs<ResolvedAddress>"),
                ("count", "WithLocs<RegOrImm>"),
            ],
        )
        self.assertEqual(
            generic_v0.modifier_bindings[1].default_value.value, "layout::v0" # pyright: ignore[reportOptionalMemberAccess]
        )
        self.assertEqual(
            [entry.value for entry in generic_v0.modifier_value_availabilities],
            ["layout::v0"],
        )
        self.assertEqual(
            [entry.minimum for entry in generic_v0.immediate_ranges], [1]
        )
        self.assertEqual(
            [entry.maximum for entry in generic_v0.immediate_ranges], [1048575]
        )
        self.assertEqual(
            [entry.minimum for entry in generic_v1.immediate_ranges], [1]
        )
        self.assertEqual(
            [entry.maximum for entry in generic_v1.immediate_ranges], [511]
        )
        address, count = generic_v0.operand_layouts[0].bindings
        self.assertEqual(address.allowed_shapes, (ResolvedOperandShape.ADDRESS,))
        self.assertEqual(
            [value.value for value in address.allowed_address_state_spaces], ["shared"]
        )
        self.assertEqual(count.type_expression.scalar_type, "u32")
        self.assertEqual(count.register_width_policy, ResolvedRegisterWidthPolicy.SAME_WIDTH)
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in inval_generic.fields],
            [
                ("inval", "bool"),
                ("type", "ScalarType"),
                ("address", "WithLocs<ResolvedAddress>"),
            ],
        )
        inval_address = inval_generic.operand_layouts[0].bindings[0]
        self.assertEqual(inval_address.allowed_shapes, (ResolvedOperandShape.ADDRESS,))
        self.assertEqual(
            [value.value for value in inval_address.allowed_address_state_spaces],
            ["shared"],
        )
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in expect_tx_generic.fields],
            [
                ("expect_tx", "bool"),
                ("state_space", "WithLocs<MemoryStateSpace>"),
                ("type", "ScalarType"),
                ("address", "WithLocs<ResolvedAddress>"),
                ("tx_count", "WithLocs<RegOrImm>"),
            ],
        )
        self.assertEqual(expect_tx_generic.modifier_bindings[1].default_value.value, "generic") # pyright: ignore[reportOptionalMemberAccess]
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in expect_tx_relaxed_cta.fields[:3]],
            [("expect_tx", "bool"), ("semantics", "MemoryConsistency"), ("scope", "MemoryScope")],
        )
        expect_tx_address, tx_count = expect_tx_generic.operand_layouts[0].bindings
        self.assertEqual(expect_tx_address.allowed_shapes, (ResolvedOperandShape.ADDRESS,))
        self.assertEqual(tx_count.type_expression.scalar_type, "u32")
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in complete_tx_generic.fields],
            [
                ("complete_tx", "bool"),
                ("state_space", "WithLocs<MemoryStateSpace>"),
                ("type", "ScalarType"),
                ("address", "WithLocs<ResolvedAddress>"),
                ("tx_count", "WithLocs<RegOrImm>"),
            ],
        )
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in complete_tx_relaxed_cta.fields[:3]],
            [("complete_tx", "bool"), ("semantics", "MemoryConsistency"), ("scope", "MemoryScope")],
        )
        complete_tx_address, complete_tx_count = complete_tx_generic.operand_layouts[0].bindings
        self.assertEqual(complete_tx_address.allowed_shapes, (ResolvedOperandShape.ADDRESS,))
        self.assertEqual(complete_tx_count.type_expression.scalar_type, "u32")
        self.assertEqual(complete_tx_count.register_width_policy, ResolvedRegisterWidthPolicy.SAME_WIDTH)

    def test_mbarrier_operand_domains_are_uniform_across_variants(self) -> None:
        mbarrier = from_instruction_spec(next(
            instruction for instruction in self.database.instructions
            if instruction.opcode == "mbarrier"
        ))
        self.assertTrue(mbarrier.variants)
        for prefix in (
            "Init", "Arrive", "TestWait", "TryWait", "ExpectTx", "CompleteTx",
            "PendingCount", "CheckLayout",
        ):
            self.assertTrue(any(variant.cpp_name.startswith(prefix)
                                for variant in mbarrier.variants), prefix)

        fields = [field for variant in mbarrier.variants for field in variant.fields]
        for name, cpp_type in (
            ("state", "ResolvedMbarrierStateToken"),
            ("phase_type", "MbarrierPhaseType"),
            ("layout", "MbarrierLayout"),
            ("address", "ResolvedAddress"),
        ):
            matching = [field for field in fields if field.name == name]
            self.assertTrue(matching, name)
            self.assertEqual({field_value_cpp_type(field) for field in matching}, {cpp_type})

        inputs = [
            binding
            for variant in mbarrier.variants
            for layout in variant.operand_layouts
            for binding in layout.bindings
            if binding.target_field_id in {"phase_parity", "tx_count", "count"}
            and binding.role == ResolvedOperandRole.SOURCE
            and binding.access == ResolvedOperandAccess.READ
        ]
        self.assertTrue(inputs)
        self.assertEqual(
            {binding.target_field_id for binding in inputs},
            {"phase_parity", "tx_count", "count"},
        )
        for binding in inputs:
            self.assertEqual(
                binding.allowed_shapes,
                (ResolvedOperandShape.REGISTER, ResolvedOperandShape.IMMEDIATE),
            )
            self.assertEqual(binding.type_expression.scalar_type, "u32")
            self.assertEqual(binding.register_width_policy,
                             ResolvedRegisterWidthPolicy.SAME_WIDTH)
if __name__ == "__main__":
    unittest.main()
