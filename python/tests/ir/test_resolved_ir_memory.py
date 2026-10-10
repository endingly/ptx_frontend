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


from .test_resolved_ir import BACKEND, ResolvedIrFixture


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

class ResolvedIrMemoryAndEmissionTest(ResolvedIrFixture, unittest.TestCase):
    """Check memory operations and generated resolved-IR contracts."""

    def test_ld_and_st_scalar_model_constraints(self) -> None:
        database = self.database
        ld = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "ld"
        )
        instruction = from_instruction_spec(ld)

        self.assertEqual(instruction.cpp_name, "Ld")
        variants = {variant.cpp_name: variant for variant in instruction.variants}
        self.assertTrue(
            {
                "GenericScalar",
                "ExplicitScalar",
                "SharedCtaScalar",
                "SharedClusterScalar",
                "GlobalU32L1Evict",
                "GlobalU32L2CacheHint",
                "GlobalL2PrefetchScalar",
                "GlobalL2PrefetchVector",
                "GenericVector",
                "ExplicitVector",
                "GlobalNcL1NoAllocateU32",
                "GlobalNcL2Evict",
                "GlobalNcL2PrefetchScalar",
                "GlobalNcL2PrefetchVector",
            }.issubset(variants)
        )
        explicit_state_space = next(
            modifier
            for modifier in ld.variants[1].modifiers
            if modifier.name == "state_space"
        )
        self.assertIs(explicit_state_space.presence, ModifierPresence.REQUIRED)
        self.assertEqual(
            [value.value for value in explicit_state_space.values],
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
        variant = variants["GenericScalar"]
        explicit_variant = variants["ExplicitScalar"]
        l1_evict_variant = variants["GlobalU32L1Evict"]
        cache_hint_variant = variants["GlobalU32L2CacheHint"]
        vector_variant = variants["GenericVector"]
        explicit_vector_variant = variants["ExplicitVector"]
        self.assertEqual(variant.cpp_name, "GenericScalar")
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in l1_evict_variant.fields],
            [
                ("state_space", "MemoryStateSpace"),
                ("semantics", "WithLocs<MemoryConsistency>"),
                ("scope", "WithLocs<MemoryScope>"),
                ("eviction_priority", "WithLocs<EvictionPriority>"),
                ("prefetch_size", "WithLocs<PrefetchSize>"),
                ("type", "WithLocs<ScalarType>"),
                ("dst", "WithLocs<ResolvedRegisterRef>"),
                ("address", "WithLocs<ResolvedAddress>"),
            ],
        )
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in cache_hint_variant.fields],
            [
                ("state_space", "MemoryStateSpace"),
                ("semantics", "WithLocs<MemoryConsistency>"),
                ("scope", "WithLocs<MemoryScope>"),
                ("cache", "WithLocs<CacheOperator>"),
                ("cache_hint", "bool"),
                ("prefetch_size", "WithLocs<PrefetchSize>"),
                ("type", "WithLocs<ScalarType>"),
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
            "b128",
            "f64",
        ]
        for syntax_variant in (
            next(variant for variant in ld.variants if variant.name == "ld_generic_scalar"),
            next(variant for variant in ld.variants if variant.name == "ld_explicit_scalar"),
            next(variant for variant in ld.variants if variant.name == "ld_generic_vector"),
            next(variant for variant in ld.variants if variant.name == "ld_explicit_vector"),
        ):
            self.assertEqual(
                [
                    value.value
                    for value in next(
                        modifier
                        for modifier in syntax_variant.modifiers
                        if modifier.name == "type"
                    ).values
                ],
                expected_types,
            )
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in variant.fields],
            [
                ("mmio", "WithLocs<bool>"),
                ("semantics", "WithLocs<MemoryConsistency>"),
                ("scope", "WithLocs<MemoryScope>"),
                ("cache", "WithLocs<CacheOperator>"),
                ("type", "WithLocs<ScalarType>"),
                ("dst", "WithLocs<ResolvedRegisterRef>"),
                ("address", "WithLocs<ResolvedAddress>"),
            ],
        )
        self.assertEqual(
            next(binding for binding in variant.modifier_bindings
                 if binding.source_kind_id == "cache").default_value.value_kind.value, # pyright: ignore[reportOptionalMemberAccess]
            "CacheOperator",
        )
        self.assertEqual(
            next(binding for binding in variant.modifier_bindings
                 if binding.source_kind_id == "cache").default_value.value, # pyright: ignore[reportOptionalMemberAccess]
            "unspecified",
        )
        self.assertEqual(
            next(binding for binding in variant.modifier_bindings
                 if binding.source_kind_id == "semantics").default_value.value, # pyright: ignore[reportOptionalMemberAccess]
            "omitted",
        )
        self.assertEqual(
            next(binding for binding in variant.modifier_bindings
                 if binding.source_kind_id == "scope").default_value.value, # pyright: ignore[reportOptionalMemberAccess]
            "none",
        )
        self.assertEqual(
            dict(next(entry.availability
                      for entry in variant.modifier_value_availabilities
                      if entry.source_kind_id == "scope" and entry.value == "cluster")),
            {"any_of": [{"ptx": "7.8", "sm": 90, "capabilities": ["cluster"]}]},
        )
        self.assertEqual(
            [
                (entry.source_kind_id, entry.value_kind.value, entry.value)
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
                (entry.source_kind_id, entry.value_kind.value, entry.value)
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
            ("mmio", "Bool", True),
            [(entry.source_kind_id, entry.value_kind.value, entry.value)
             for entry in variant.modifier_value_availabilities],
        )
        self.assertEqual(variant.memory_consistency.semantics_field_id, "semantics") # pyright: ignore[reportOptionalMemberAccess]
        self.assertEqual(variant.memory_consistency.address_field_id, "address") # pyright: ignore[reportOptionalMemberAccess]
        (alignment,) = variant.address_alignments
        self.assertEqual(alignment.address_field_ids, ("address",))
        self.assertEqual(alignment.type_field_id, "type")
        self.assertIsNone(alignment.vector_field_id)
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
        explicit_state_space_field = next(
            field
            for field in explicit_variant.modifier_fields
            if field.name == "state_space"
        )
        self.assertEqual(
            (
                explicit_state_space_field.name,
                field_cpp_type(explicit_state_space_field),
                explicit_state_space_field.storage,
            ),
            (
                "state_space",
                "WithLocs<MemoryStateSpace>",
                ResolvedFieldStorage.INSTANCE,
            ),
        )
        explicit_cache_field = next(
            field
            for field in explicit_variant.modifier_fields
            if field.name == "cache"
        )
        explicit_cache_binding = next(
            binding
            for binding in explicit_variant.modifier_bindings
            if binding.source_kind_id == "cache"
        )
        self.assertEqual(
            (
                explicit_cache_field.name,
                field_cpp_type(explicit_cache_field),
                explicit_cache_binding.default_value.value, # pyright: ignore[reportOptionalMemberAccess]
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
                ("b128", {"ptx": "8.3", "sm": 70}),
                ("f64", {"ptx": "1.0", "sm": 13}),
            ],
        )
        load_parameter = (
            explicit_variant.operand_layouts[0].bindings[1].parameter_constraint
        )
        self.assertEqual(load_parameter.direction, "input") # pyright: ignore[reportOptionalMemberAccess]
        self.assertEqual(
            dict(load_parameter.function_availability), # pyright: ignore[reportOptionalMemberAccess]
            {"ptx": "2.0", "sm": 20},
        )
        self.assertEqual(vector_variant.cpp_name, "GenericVector")
        self.assertEqual(
            [field.name for field in vector_variant.fields],
            ["semantics", "scope", "cache", "vector", "type", "dst", "address"],
        )
        self.assertEqual(vector_variant.memory_consistency.mmio_field_id, "") # pyright: ignore[reportOptionalMemberAccess]
        self.assertEqual(vector_variant.address_alignments[0].vector_field_id, "vector")
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
        self.assertEqual(vector_binding.vector_sink_payload_bits, 256)
        self.assertEqual(vector_variant.memory_vector.type_field_id, "type") # pyright: ignore[reportOptionalMemberAccess]
        self.assertEqual(vector_variant.memory_vector.vector_field_id, "dst") # pyright: ignore[reportOptionalMemberAccess]
        self.assertEqual(
            dict(vector_variant.memory_vector.availability), # pyright: ignore[reportOptionalMemberAccess]
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
        self.assertEqual(load_vector_parameter.direction, "input") # pyright: ignore[reportOptionalMemberAccess]
        self.assertEqual(
            dict(load_vector_parameter.function_availability), # pyright: ignore[reportOptionalMemberAccess]
            {"ptx": "2.0", "sm": 20},
        )

        st = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "st"
        )
        store = from_instruction_spec(st)
        explicit_store_state_space = next(
            modifier
            for modifier in st.variants[1].modifiers
            if modifier.name == "state_space"
        )
        self.assertEqual(
            [value.value for value in explicit_store_state_space.values],
            ["global", "local", "param", "param::func", "shared"],
        )
        store_variants = {variant.cpp_name: variant for variant in store.variants}
        self.assertTrue(
            {
                "GenericScalar", "ExplicitScalar", "SharedCtaScalar",
                "SharedClusterScalar", "SharedCtaVector", "SharedClusterVector",
                "GlobalU32L1Evict", "GlobalU32L2CacheHint",
                "GlobalL1EvictVector", "GlobalL2EvictVector",
                "GlobalL2CacheHintVector", "GenericVector", "ExplicitVector",
            }.issubset(store_variants)
        )
        for syntax_variant in (
            next(variant for variant in st.variants if variant.name == "st_generic_scalar"),
            next(variant for variant in st.variants if variant.name == "st_explicit_scalar"),
            next(variant for variant in st.variants if variant.name == "st_generic_vector"),
            next(variant for variant in st.variants if variant.name == "st_explicit_vector"),
        ):
            self.assertEqual(
                [
                    value.value
                    for value in next(
                        modifier
                        for modifier in syntax_variant.modifiers
                        if modifier.name == "type"
                    ).values
                ],
                expected_types,
            )
        self.assertEqual(
            [field.name for field in store_variants["GenericScalar"].fields],
            ["mmio", "semantics", "scope", "cache", "type", "address", "src"],
        )
        self.assertEqual(
            next(binding for binding in store_variants["GenericScalar"].modifier_bindings
                 if binding.source_kind_id == "cache").default_value.value, # pyright: ignore[reportOptionalMemberAccess]
            "unspecified",
        )
        self.assertEqual(
            [
                availability.value
                for availability in store_variants["GenericScalar"].modifier_value_availabilities
                if availability.source_kind_id == "cache"
            ],
            ["wb", "cg", "cs", "wt"],
        )
        self.assertEqual(
            [
                entry.value
                for entry in store_variants["GenericScalar"]
                .operand_layouts[0]
                .bindings[0]
                .allowed_address_state_spaces
            ],
            ["global", "local", "shared"],
        )
        self.assertEqual(
            store_variants["ExplicitScalar"]
            .operand_layouts[0]
            .bindings[0]
            .state_space_modifier_field_id,
            "state_space",
        )
        self.assertEqual(
            next(
                binding.default_value.value # pyright: ignore[reportOptionalMemberAccess]
                for binding in store_variants["ExplicitScalar"].modifier_bindings
                if binding.source_kind_id == "cache"
            ),
            "unspecified",
        )
        self.assertEqual(
            store_variants["GenericScalar"].operand_layouts[0].bindings[1].type_expression,
            ResolvedOperandTypeExpression(
                kind=ResolvedOperandTypeExpressionKind.MODIFIER_FIELD,
                modifier_field_id="type",
            ),
        )
        self.assertEqual(
            store_variants["GenericScalar"]
            .operand_layouts[0]
            .bindings[1]
            .register_width_policy,
            ResolvedRegisterWidthPolicy.EQUAL_OR_WIDER,
        )
        self.assertEqual(
            store_variants["ExplicitScalar"]
            .operand_layouts[0]
            .bindings[1]
            .register_width_policy,
            ResolvedRegisterWidthPolicy.EQUAL_OR_WIDER,
        )
        self.assertEqual(
            [
                (entry.value, dict(entry.availability))
                for entry in store_variants["ExplicitScalar"].modifier_value_availabilities
                if entry.source_kind_id in {"cache", "type"}
            ],
            [
                ("wb", {"ptx": "2.0", "sm": 20}),
                ("cg", {"ptx": "2.0", "sm": 20}),
                ("cs", {"ptx": "2.0", "sm": 20}),
                ("wt", {"ptx": "2.0", "sm": 20}),
                ("b128", {"ptx": "8.3", "sm": 70}),
                ("f64", {"ptx": "1.0", "sm": 13}),
            ],
        )
        store_parameter = (
            store_variants["ExplicitScalar"].operand_layouts[0].bindings[0].parameter_constraint
        )
        self.assertEqual(store_parameter.direction, "return") # pyright: ignore[reportOptionalMemberAccess]
        self.assertEqual(
            dict(store_parameter.function_availability), # pyright: ignore[reportOptionalMemberAccess]
            {"ptx": "2.0", "sm": 20},
        )
        store_vector = store_variants["GenericVector"]
        self.assertEqual(
            [field.name for field in store_vector.fields],
            ["semantics", "scope", "cache", "vector", "type", "address", "src"],
        )
        self.assertEqual(store_vector.memory_consistency.mmio_field_id, "") # pyright: ignore[reportOptionalMemberAccess]
        self.assertEqual(
            [
                value.value
                for value in next(
                    modifier
                    for modifier in next(
                        variant
                        for variant in st.variants
                        if variant.name == "st_generic_vector"
                    ).modifiers
                    if modifier.name == "vector"
                ).values
            ],
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
        self.assertEqual(store_vector_binding.vector_sink_payload_bits, 256)
        self.assertEqual(store_vector.memory_vector.vector_field_id, "src") # pyright: ignore[reportOptionalMemberAccess]
        store_vector_parameter = (
            store_variants["ExplicitVector"].operand_layouts[0].bindings[0].parameter_constraint
        )
        self.assertEqual(store_vector_parameter.direction, "return") # pyright: ignore[reportOptionalMemberAccess]
        self.assertEqual(
            dict(store_vector_parameter.function_availability), # pyright: ignore[reportOptionalMemberAccess]
            {"ptx": "2.0", "sm": 20},
        )

    def test_ldu_scalar_and_vector_models(self) -> None:
        database = self.database
        ldu = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "ldu"
        )
        resolved = from_instruction_spec(ldu)

        self.assertEqual(resolved.cpp_name, "Ldu")
        self.assertEqual(
            [variant.cpp_name for variant in resolved.variants],
            [
                "GenericScalar",
                "ExplicitScalar",
                "GenericV2",
                "ExplicitV2",
                "GenericV4",
                "ExplicitV4",
            ],
        )
        (
            generic_scalar,
            explicit_scalar,
            generic_v2,
            explicit_v2,
            generic_v4,
            explicit_v4,
        ) = resolved.variants
        self.assertEqual(dict(generic_scalar.availability), {"ptx": "2.0", "sm": 20})
        self.assertEqual(dict(explicit_scalar.availability), {"ptx": "2.0", "sm": 0})
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in generic_scalar.fields],
            [
                ("type", "WithLocs<ScalarType>"),
                ("dst", "WithLocs<ResolvedRegisterRef>"),
                ("address", "WithLocs<ResolvedAddress>"),
            ],
        )
        self.assertEqual(
            generic_scalar.operand_layouts[0].bindings[0].register_width_policy,
            ResolvedRegisterWidthPolicy.EQUAL_OR_WIDER,
        )
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in explicit_scalar.fields],
            [
                ("state_space", "MemoryStateSpace"),
                ("type", "WithLocs<ScalarType>"),
                ("dst", "WithLocs<ResolvedRegisterRef>"),
                ("address", "WithLocs<ResolvedAddress>"),
            ],
        )
        self.assertEqual(
            explicit_scalar.operand_layouts[0].bindings[1].state_space_modifier_field_id,
            "state_space",
        )
        for variant in (generic_v2, explicit_v2, generic_v4, explicit_v4):
            vector_binding = variant.operand_layouts[0].bindings[0]
            self.assertEqual(
                vector_binding.register_width_policy,
                ResolvedRegisterWidthPolicy.EQUAL_OR_WIDER,
            )
            self.assertEqual(
                vector_binding.vector_arity_modifier_field_id,
                "vector",
            )
            self.assertEqual(
                vector_binding.vector_type_policy,
                ResolvedVectorTypePolicy.ELEMENT,
            )
            self.assertFalse(vector_binding.allow_vector_sink)
        self.assertEqual(
            explicit_v4.operand_layouts[0].bindings[1].state_space_modifier_field_id,
            "state_space",
        )

    def test_prefetch_ordinary_and_tensormap_models(self) -> None:
        database = self.database
        prefetch = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "prefetch"
        )
        resolved = from_instruction_spec(prefetch)

        self.assertEqual(resolved.cpp_name, "Prefetch")
        self.assertEqual(
            [variant.cpp_name for variant in resolved.variants],
            [
                "GenericL1", "GenericL2", "GlobalL1", "GlobalL2",
                "LocalL1", "LocalL2", "GlobalL2Evict",
                "ConstTensormap", "ParamTensormap", "GenericTensormap",
            ],
        )
        variants = {variant.cpp_name: variant for variant in resolved.variants}
        variant = variants["GlobalL1"]
        self.assertEqual(dict(variant.availability), {"ptx": "2.0", "sm": 20})
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in variant.fields],
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
        self.assertEqual(
            [space.value for space in variants["GenericL1"].operand_layouts[0]
             .bindings[0].allowed_address_state_spaces],
            ["global", "local", "shared"],
        )
        self.assertEqual(
            dict(variants["GlobalL2Evict"].availability),
            {"ptx": "7.4", "sm": 80},
        )
        for name in ("ConstTensormap", "ParamTensormap", "GenericTensormap"):
            self.assertEqual(
                dict(variants[name].availability), {"ptx": "8.0", "sm": 90}
            )
        generic_tensormap = variants["GenericTensormap"]
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in generic_tensormap.fields],
            [("tensormap", "bool"), ("address", "WithLocs<ResolvedAddress>")],
        )
        generic_address = generic_tensormap.operand_layouts[0].bindings[0]
        self.assertEqual(
            [space.value for space in generic_address.allowed_address_state_spaces],
            ["global", "shared"],
        )
        self.assertIsNone(generic_address.parameter_constraint)
        param = variants["ParamTensormap"].operand_layouts[0].bindings[0]
        self.assertEqual(param.state_space_modifier_field_id, "state_space")
        self.assertEqual(param.parameter_constraint.direction, "input")

    def test_prefetchu_l1_model(self) -> None:
        database = self.database
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
            [(field.name, field_cpp_type(field)) for field in variant.fields],
            [("l1", "bool"), ("address", "WithLocs<ResolvedAddress>")],
        )
        self.assertEqual(
            [state_space.value for state_space in variant.operand_layouts[0]
             .bindings[0].allowed_address_state_spaces],
            ["generic"],
        )

    def test_createpolicy_topologies(self) -> None:
        database = self.database
        createpolicy = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "createpolicy"
        )
        resolved = from_instruction_spec(createpolicy)

        self.assertEqual(resolved.cpp_name, "Createpolicy")
        self.assertEqual(
            [variant.cpp_name for variant in resolved.variants],
            [
                "FractionalL2B64", "FractionalL2SecondaryB64",
                "RangeGenericL2B64", "RangeGenericL2SecondaryB64",
                "RangeGlobalL2B64", "RangeGlobalL2SecondaryB64",
                "CvtL2B64",
            ],
        )
        variant = resolved.variants[0]
        self.assertEqual(dict(variant.availability), {"ptx": "7.4", "sm": 80})
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in variant.modifier_fields],
            [
                ("fractional", "bool"),
                ("primary_priority", "WithLocs<EvictionPriority>"),
                ("type", "ScalarType"),
            ],
        )
        self.assertEqual(
            [layout.cpp_name for layout in variant.operand_layouts],
            ["Default", "WithFraction"],
        )
        self.assertEqual(
            [field.name for field in resolved.variants[1].modifier_fields],
            ["fractional", "primary_priority", "secondary_priority", "type"],
        )
        for range_variant in resolved.variants[2:6]:
            size_bindings = [
                binding
                for binding in range_variant.operand_layouts[0].bindings
                if binding.target_field_id in {"primary_size", "total_size"}
            ]
            self.assertEqual(len(size_bindings), 2)
            self.assertTrue(
                all(
                    binding.immediate_conversion_policy
                    is ResolvedImmediateConversionPolicy.REQUIRE_TARGET_RANGE
                    for binding in size_bindings
                )
            )
        self.assertEqual(
            variant.operand_layouts[0].bindings[0].register_width_policy,
            ResolvedRegisterWidthPolicy.SAME_WIDTH,
        )

    def test_applypriority_global_l2_evict_normal_model(self) -> None:
        database = self.database
        applypriority = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "applypriority"
        )
        resolved = from_instruction_spec(applypriority)

        self.assertEqual(resolved.cpp_name, "Applypriority")
        self.assertEqual(
            [variant.cpp_name for variant in resolved.variants],
            ["GlobalL2EvictNormal", "GenericL2EvictNormal"],
        )
        variant = resolved.variants[0]
        self.assertEqual(dict(variant.availability), {"ptx": "7.4", "sm": 80})
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in variant.fields],
            [
                ("state_space", "MemoryStateSpace"),
                ("eviction_priority", "EvictionPriority"),
                ("address", "WithLocs<ResolvedAddress>"),
                ("size", "WithLocs<ResolvedImmediate>"),
            ],
        )
        self.assertEqual(variant.immediate_value.values, (128,)) # pyright: ignore[reportOptionalMemberAccess]
        self.assertEqual(variant.address_alignments[0].alignment, 128)
        generic = resolved.variants[1]
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in generic.fields],
            [
                ("eviction_priority", "EvictionPriority"),
                ("address", "WithLocs<ResolvedAddress>"),
                ("size", "WithLocs<ResolvedImmediate>"),
            ],
        )

    def test_discard_global_l2_model(self) -> None:
        database = self.database
        discard = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "discard"
        )
        resolved = from_instruction_spec(discard)

        self.assertEqual(resolved.cpp_name, "Discard")
        self.assertEqual(
            [variant.cpp_name for variant in resolved.variants],
            ["GlobalL2", "GenericL2"],
        )
        variant = resolved.variants[0]
        self.assertEqual(dict(variant.availability), {"ptx": "7.4", "sm": 80})
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in variant.fields],
            [
                ("state_space", "MemoryStateSpace"),
                ("l2", "bool"),
                ("address", "WithLocs<ResolvedAddress>"),
                ("size", "WithLocs<ResolvedImmediate>"),
            ],
        )
        self.assertEqual(variant.immediate_value.values, (128,)) # pyright: ignore[reportOptionalMemberAccess]
        self.assertEqual(variant.address_alignments[0].alignment, 128)
        generic = resolved.variants[1]
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in generic.fields],
            [
                ("l2", "bool"),
                ("address", "WithLocs<ResolvedAddress>"),
                ("size", "WithLocs<ResolvedImmediate>"),
            ],
        )

    def test_setmaxnreg_actions_model_and_generator(self) -> None:
        database = self.database
        setmaxnreg = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "setmaxnreg"
        )
        resolved = from_instruction_spec(setmaxnreg)
        self.assertEqual(resolved.cpp_name, "Setmaxnreg")
        self.assertEqual(
            [variant.cpp_name for variant in resolved.variants],
            ["IncSyncAlignedU32", "DecSyncAlignedU32"],
        )
        expected_availability = {"any_of": [
            {"ptx": "8.0", "sm": 90, "target": "sm_90a"},
            {"ptx": "8.6", "sm": 100, "target": "sm_100a"},
            {"ptx": "8.7", "sm": 120, "target": "sm_120a"},
            {"ptx": "8.8", "sm": 100, "family": "sm_100f"},
            {"ptx": "9.0", "sm": 110, "family": "sm_110f"},
            {"ptx": "8.8", "sm": 120, "family": "sm_120f"},
        ]}
        for variant, action in zip(resolved.variants, ("inc", "dec"), strict=True):
            self.assertEqual(dict(variant.availability), expected_availability)
            self.assertEqual(
                [(field.name, field_cpp_type(field)) for field in variant.fields],
                [
                    (action, "bool"),
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
            self.assertEqual(variant.immediate_multiple_of.operand_field_id, "count") # pyright: ignore[reportOptionalMemberAccess]
            self.assertEqual(variant.immediate_multiple_of.divisor, 8) # pyright: ignore[reportOptionalMemberAccess]

        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir_control_flow.gen.cpp"
            descriptor_path = Path(directory) / "resolved_ir_checker_descriptor.gen.cpp"
            generate_resolved_opcode_source(build_test_generation_context(database), category="control_flow", opcode="setmaxnreg", output_path=output_path
            )
            generate_resolved_checker_descriptor_source(build_test_generation_context(database), category="control_flow", output_path=descriptor_path
            )
            source = output_path.read_text(encoding="utf-8")
            descriptor = descriptor_path.read_text(encoding="utf-8")
        self.assertIn("check_immediate_multiple_of(", source)
        for action in ("Inc", "Dec"):
            start = source.index(f"Setmaxnreg{action}SyncAlignedU32::check(")
            setmaxnreg_check = source[start:source.index("::visit_references(", start)]
            self.assertEqual(setmaxnreg_check.count("check_immediate_multiple_of("), 1)
            self.assertEqual(setmaxnreg_check.count("check_immediate_range("), 1)
        self.assertIn("std::expected<std::unique_ptr<Instruction>, ResolveDiagnostic> resolveSetmaxnreg(", source)
        self.assertIn(".any_of_count = 6", descriptor)
        self.assertIn('.required_family = "sm_100f",', descriptor)
        self.assertIn('.required_family = "sm_110f",', descriptor)
        self.assertIn('.required_family = "sm_120f",', descriptor)
        self.assertIn('.operand_field_id = "count",', descriptor)
        self.assertIn(".divisor = uint64_t{8ULL},", descriptor)

    def test_cp_async_ca_shared_global_model(self) -> None:
        database = self.database
        cp = next(instruction for instruction in database.instructions if instruction.opcode == "cp")
        resolved = from_instruction_spec(cp)
        variant = resolved.variants[0]

        self.assertEqual(resolved.cpp_name, "Cp")
        self.assertEqual(
            [candidate.cpp_name for candidate in resolved.variants[:35]],
            ["AsyncCaSharedGlobal", "AsyncCommitGroup", "AsyncWaitGroup", "AsyncWaitAll",
             "AsyncMbarrierArriveGenericOrShared", "AsyncMbarrierArriveSharedCta",
             "AsyncMbarrierArriveNoincGenericOrShared",
             "AsyncMbarrierArriveNoincSharedCta", "AsyncCgSharedGlobal",
             "AsyncCaSharedCtaGlobal", "AsyncCgSharedCtaGlobal",
             "AsyncCaSharedGlobalControl", "AsyncCgSharedGlobalControl",
             "AsyncCaSharedCtaGlobalControl", "AsyncCgSharedCtaGlobalControl",
             "AsyncCaSharedGlobalPrefetchBase", "AsyncCaSharedGlobalPrefetchControl", "AsyncCaSharedGlobalCacheHintBase",
             "AsyncCaSharedGlobalCacheHintControl", "AsyncCaSharedGlobalCacheHintControlPolicy", "AsyncCaSharedCtaGlobalPrefetchBase",
             "AsyncCaSharedCtaGlobalPrefetchControl", "AsyncCaSharedCtaGlobalCacheHintBase", "AsyncCaSharedCtaGlobalCacheHintControl",
             "AsyncCaSharedCtaGlobalCacheHintControlPolicy", "AsyncCgSharedGlobalPrefetchBase", "AsyncCgSharedGlobalPrefetchControl",
             "AsyncCgSharedGlobalCacheHintBase", "AsyncCgSharedGlobalCacheHintControl", "AsyncCgSharedGlobalCacheHintControlPolicy",
             "AsyncCgSharedCtaGlobalPrefetchBase", "AsyncCgSharedCtaGlobalPrefetchControl", "AsyncCgSharedCtaGlobalCacheHintBase",
             "AsyncCgSharedCtaGlobalCacheHintControl", "AsyncCgSharedCtaGlobalCacheHintControlPolicy"],
        )
        self.assertEqual(variant.cpp_name, "AsyncCaSharedGlobal")
        self.assertEqual(variant.completion_kind, AsyncCompletionKind.ASYNC_GROUP)
        self.assertTrue(all(
            candidate.completion_kind is AsyncCompletionKind.ASYNC_GROUP
            for candidate in resolved.variants[1:4]
        ))
        self.assertTrue(all(
            candidate.completion_kind is AsyncCompletionKind.NONE
            for candidate in resolved.variants[4:8]
        ))
        self.assertTrue(all(
            candidate.completion_kind is AsyncCompletionKind.ASYNC_GROUP
            for candidate in resolved.variants[8:35]
        ))
        self.assertEqual(dict(variant.availability), {"ptx": "7.0", "sm": 80})
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in variant.fields],
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
        self.assertEqual(variant.immediate_value.operand_field_id, "cp_size") # pyright: ignore[reportOptionalMemberAccess]
        self.assertEqual(variant.immediate_value.values, (4, 8, 16)) # pyright: ignore[reportOptionalMemberAccess]
        (alignment,) = variant.address_alignments
        self.assertEqual(alignment.address_field_ids, ("dst", "src"))
        self.assertEqual(alignment.immediate_operand_field_id, "cp_size")
        self.assertEqual(
            [value.value for value in variant.operand_layouts[0].bindings[0].allowed_address_state_spaces],
            ["shared"],
        )
        self.assertEqual(
            [value.value for value in variant.operand_layouts[0].bindings[1].allowed_address_state_spaces],
            ["global"],
        )

    def test_non_tensor_bulk_copy_completion_and_qualifier_matrix(self) -> None:
        cp = next(item for item in self.database.instructions if item.opcode == "cp")
        variants = {item.cpp_name: item for item in from_instruction_spec(cp).variants}
        mbar = variants["AsyncBulkGlobalSharedCluster"]
        group = variants["AsyncBulkSharedCtaGlobal"]
        self.assertEqual(mbar.completion_kind, AsyncCompletionKind.MBARRIER_COMPLETE_TX_BYTES)
        self.assertEqual(group.completion_kind, AsyncCompletionKind.BULK_GROUP)
        self.assertEqual(field_cpp_type(next(field for field in mbar.fields if field.name == "mbar")),
                         "WithLocs<ResolvedAddress>")
        self.assertEqual([binding.target_field_id for binding in variants[
            "AsyncBulkGlobalSharedCtaCacheHintIgnoreOob"].operand_layouts[0].bindings],
                         ["dst", "src", "size", "ignore_bytes_left", "ignore_bytes_right",
                          "mbar"])
        self.assertEqual([binding.target_field_id for binding in variants[
            "AsyncBulkGlobalSharedCtaCacheHintIgnoreOob"].operand_layouts[1].bindings],
                         ["dst", "src", "size", "ignore_bytes_left", "ignore_bytes_right",
                          "mbar", "cache_policy"])
        self.assertEqual([binding.target_field_id for binding in variants[
            "AsyncBulkSharedCtaGlobalCacheHintCpMask"].operand_layouts[0].bindings],
                         ["dst", "src", "size", "byte_mask"])
        self.assertEqual([binding.target_field_id for binding in variants[
            "AsyncBulkSharedCtaGlobalCacheHintCpMask"].operand_layouts[1].bindings],
                         ["dst", "src", "size", "cache_policy", "byte_mask"])
        relaxed = variants["AsyncBulkGlobalSharedCtaRelaxed"]
        self.assertEqual(relaxed.completion_kind, AsyncCompletionKind.MBARRIER_COMPLETE_TX_BYTES)
        relaxed_availability = dict(relaxed.availability)
        self.assertEqual(len(relaxed_availability["any_of"]), 3)
        self.assertEqual(dict(relaxed_availability["any_of"][0]),
                         {"ptx": "9.3", "sm": 90, "target": "sm_90a"})
        masked_relaxed = variants["AsyncBulkSharedCtaGlobalCpMaskRelaxed"]
        self.assertEqual([dict(item) for item in dict(masked_relaxed.availability)["any_of"]],
                         [{"ptx": "9.3", "sm": 100, "family": "sm_100f"},
                          {"ptx": "9.3", "sm": 110, "family": "sm_110f"}])

    def test_bulk_reduction_scope_and_store_layouts(self) -> None:
        cp = next(item for item in self.database.instructions if item.opcode == "cp")
        cp_variants = {item.cpp_name: item for item in from_instruction_spec(cp).variants}
        shared = cp_variants["ReduceAsyncBulkSharedAddRelaxed"]
        global_policy = cp_variants["ReduceAsyncBulkGlobalAddNoftzCacheHintRelaxed"]
        self.assertEqual(dict(shared.availability), {"ptx": "9.3", "sm": 90})
        self.assertEqual(shared.completion_kind, AsyncCompletionKind.MBARRIER_COMPLETE_TX_BYTES)
        self.assertEqual(global_policy.completion_kind, AsyncCompletionKind.BULK_GROUP)
        self.assertEqual(len(global_policy.operand_layouts), 2)
        self.assertEqual(global_policy.operand_layouts[1].bindings[-1].target_field_id, "cache_policy")
        st = next(item for item in self.database.instructions if item.opcode == "st")
        st_variants = {item.cpp_name: item for item in from_instruction_spec(st).variants}
        self.assertEqual(st_variants["AsyncSharedClusterScalar"].completion_kind,
                         AsyncCompletionKind.MBARRIER_COMPLETE_TX_BYTES)
        self.assertEqual(st_variants["AsyncGlobalRelease"].completion_kind,
                         AsyncCompletionKind.NONE)
        self.assertEqual([binding.target_field_id for binding in st_variants["BulkZero"].operand_layouts[0].bindings],
                         ["dst", "size", "initval"])

    def test_cp_async_mbarrier_arrive_model(self) -> None:
        database = self.database
        cp = next(instruction for instruction in database.instructions if instruction.opcode == "cp")
        variants = from_instruction_spec(cp).variants[4:8]

        self.assertEqual(
            [variant.cpp_name for variant in variants],
            ["AsyncMbarrierArriveGenericOrShared", "AsyncMbarrierArriveSharedCta",
             "AsyncMbarrierArriveNoincGenericOrShared",
             "AsyncMbarrierArriveNoincSharedCta"],
        )
        self.assertEqual(
            [dict(variant.availability) for variant in variants],
            [{"ptx": "7.0", "sm": 80}, {"ptx": "7.8", "sm": 80},
             {"ptx": "7.0", "sm": 80}, {"ptx": "7.8", "sm": 80}],
        )
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in variants[0].fields],
            [("async", "bool"), ("mbarrier", "bool"), ("arrive", "bool"),
             ("state_space", "WithLocs<MemoryStateSpace>"),
             ("type", "ScalarType"), ("address", "WithLocs<ResolvedAddress>")],
        )
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in variants[2].fields[:4]],
            [("async", "bool"), ("mbarrier", "bool"), ("arrive", "bool"),
             ("noinc", "bool")],
        )
        for variant in variants:
            address = variant.operand_layouts[0].bindings[0]
            (alignment,) = variant.address_alignments
            self.assertEqual(address.allowed_shapes, (ResolvedOperandShape.ADDRESS,))
            self.assertEqual(
                [value.value for value in address.allowed_address_state_spaces], ["shared"]
            )
            self.assertEqual(alignment.alignment, 8)

    def test_clusterlaunchcontrol_try_cancel_model(self) -> None:
        database = self.database
        instruction = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "clusterlaunchcontrol"
        )
        variants = from_instruction_spec(instruction).variants
        self.assertEqual(
            [variant.cpp_name for variant in variants],
            [
                "TryCancelAsyncGeneric",
                "TryCancelAsyncSharedCta",
                "TryCancelAsyncMulticastGeneric",
                "TryCancelAsyncMulticastSharedCta",
                "QueryCancelIsCanceledPred",
                "QueryCancelGetFirstCtaidV4",
                "QueryCancelGetFirstCtaidX",
                "QueryCancelGetFirstCtaidY",
                "QueryCancelGetFirstCtaidZ",
            ],
        )
        self.assertEqual(
            dict(variants[0].availability),
            {"any_of": [{"ptx": "8.6", "sm": 100, "capabilities": ["cluster"]}]},
        )
        self.assertEqual(
            dict(variants[2].availability),
            {"any_of": [
                {"ptx": "8.6", "sm": 100, "target": "sm_100a", "capabilities": ["cluster"]},
                {"ptx": "8.6", "sm": 120, "target": "sm_120a", "capabilities": ["cluster"]},
                {"ptx": "8.8", "sm": 100, "family": "sm_100f", "capabilities": ["cluster"]},
                {"ptx": "8.8", "sm": 120, "family": "sm_120f", "capabilities": ["cluster"]},
                {"ptx": "9.0", "sm": 110, "family": "sm_110f", "capabilities": ["cluster"]},
            ]},
        )
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in variants[0].fields],
            [
                ("try_cancel", "bool"),
                ("async", "bool"),
                ("mbarrier_complete_tx_bytes", "bool"),
                ("type", "ScalarType"),
                ("response", "WithLocs<ResolvedAddress>"),
                ("mbarrier", "WithLocs<ResolvedAddress>"),
            ],
        )
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in variants[1].fields[:3]],
            [
                ("try_cancel", "bool"),
                ("async_shared_cta", "bool"),
                ("mbarrier_complete_tx_bytes", "bool"),
            ],
        )
        self.assertEqual(
            [constraint.alignment for constraint in variants[0].address_alignments],
            [16, 8],
        )
        self.assertEqual(
            [constraint.address_field_ids for constraint in variants[0].address_alignments],
            [("response",), ("mbarrier",)],
        )
        self.assertFalse(hasattr(variants[0], "address_alignment"))
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in variants[4].fields],
            [
                ("query_cancel", "bool"),
                ("is_canceled", "bool"),
                ("result_type", "ScalarType"),
                ("response_type", "ScalarType"),
                ("status", "WithLocs<ResolvedPredicate>"),
                ("response", "WithLocs<ResolvedRegisterRef>"),
            ],
        )
        self.assertEqual(
            variants[5].operand_layouts[0].bindings[0].allowed_vector_arities, (4,)
        )
        self.assertTrue(variants[5].operand_layouts[0].bindings[0].allow_vector_sink)
        self.assertEqual(
            variants[5].operand_layouts[0].bindings[0].vector_sink_payload_bits, 0
        )
        self.assertEqual(
            dict(variants[8].availability),
            {"any_of": [{"ptx": "8.6", "sm": 100, "capabilities": ["cluster"]}]},
        )

    def test_cp_async_commit_group_model(self) -> None:
        database = self.database
        cp = next(instruction for instruction in database.instructions if instruction.opcode == "cp")
        variant = from_instruction_spec(cp).variants[1]

        self.assertEqual(variant.cpp_name, "AsyncCommitGroup")
        self.assertEqual(dict(variant.availability), {"ptx": "7.0", "sm": 80})
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in variant.fields],
            [("async", "bool"), ("commit_group", "bool")],
        )
        self.assertEqual(variant.operand_layouts[0].bindings, ())

    def test_cp_async_wait_group_model(self) -> None:
        database = self.database
        cp = next(instruction for instruction in database.instructions if instruction.opcode == "cp")
        variant = from_instruction_spec(cp).variants[2]

        self.assertEqual(variant.cpp_name, "AsyncWaitGroup")
        self.assertEqual(dict(variant.availability), {"ptx": "7.0", "sm": 80})
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in variant.fields],
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
        database = self.database
        cp = next(instruction for instruction in database.instructions if instruction.opcode == "cp")
        variant = from_instruction_spec(cp).variants[3]

        self.assertEqual(variant.cpp_name, "AsyncWaitAll")
        self.assertEqual(dict(variant.availability), {"ptx": "7.0", "sm": 80})
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in variant.fields],
            [("async", "bool"), ("wait_all", "bool")],
        )
        self.assertEqual(variant.operand_layouts[0].bindings, ())

    def test_ldmatrix_sync_aligned_m8n8_x2_shared_b16_model(self) -> None:
        database = self.database
        ldmatrix = next(
            instruction for instruction in database.instructions if instruction.opcode == "ldmatrix"
        )
        variant = from_instruction_spec(ldmatrix).variants[0]

        self.assertEqual(variant.cpp_name, "SyncAlignedM8n8X2SharedB16")
        self.assertEqual(dict(variant.availability), {"ptx": "6.5", "sm": 75})
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in variant.fields],
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
        (alignment,) = variant.address_alignments
        self.assertEqual(alignment.address_field_ids, ("address",))
        self.assertEqual(alignment.alignment, 16)

    def test_mma_sync_aligned_m16n8k8_row_col_f32_f16_f16_f32_model(self) -> None:
        database = self.database
        mma = next(
            instruction for instruction in database.instructions if instruction.opcode == "mma"
        )
        variant = from_instruction_spec(mma).variants[0]

        self.assertEqual(variant.cpp_name, "SyncAlignedM16n8k8RowColF32F16F16F32")
        self.assertEqual(dict(variant.availability), {"ptx": "6.5", "sm": 75})
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in variant.fields],
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
            generate_resolved_opcode_source(build_test_generation_context(database), category="matrix", opcode="mma", output_path=output_path
            )
            source = output_path.read_text(encoding="utf-8")
        self.assertIn("SyncAlignedM16n8k8RowColF32F16F16F32", source)
        self.assertIn("std::expected<std::unique_ptr<Instruction>, ResolveDiagnostic> resolveMma(", source)

    def test_cp_generator_emits_immediate_value_checker(self) -> None:
        database = self.database
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir_data_movement.gen.cpp"
            methods_path = Path(directory) / "resolved_ir_cp_methods_000.gen.cpp"
            descriptor_path = Path(directory) / "resolved_ir_checker_descriptor.gen.cpp"
            context = build_test_generation_context(database)
            generate_resolved_opcode_source(context, category="data_movement", opcode="cp", output_path=output_path
            )
            generate_resolved_form_shard_source(
                context, category="data_movement", opcode="cp",
                shard_index=0, output_path=methods_path,
            )
            generate_resolved_checker_descriptor_source(context, category="data_movement", output_path=descriptor_path
            )
            source = (methods_path.read_text(encoding="utf-8") + "\n"
                      + output_path.read_text(encoding="utf-8"))
            descriptor = descriptor_path.read_text(encoding="utf-8")

        self.assertIn("check_immediate_value(", source)
        self.assertIn("check_immediate_range(", source)
        self.assertIn("check_address_alignment(", source)
        start = source.index("CpAsyncCaSharedGlobal::check(")
        cp_check = source[start:source.index("::visit_references(", start)]
        self.assertEqual(cp_check.count("check_address_alignment("), 1)
        self.assertEqual(cp_check.count("check_immediate_value("), 1)
        self.assertLess(
            cp_check.index("check_address_alignment("),
            cp_check.index("check_immediate_value("),
        )
        start = source.index("CpAsyncWaitGroup::check(")
        wait_group_check = source[start:source.index("::visit_references(", start)]
        self.assertEqual(wait_group_check.count("check_immediate_range("), 1)
        self.assertIn("selected.cp_size.value.bits", source)
        self.assertIn("std::expected<std::unique_ptr<Instruction>, ResolveDiagnostic> resolveCp(", source)
        self.assertIn("AsyncCommitGroup", source)
        self.assertIn("AsyncWaitGroup", source)
        self.assertIn("AsyncWaitAll", source)
        self.assertIn(
            "AsyncCaSharedGlobal_immediate_value_values = "
            "{{uint64_t{4ULL}, uint64_t{8ULL}, uint64_t{16ULL}}};",
            descriptor,
        )
        self.assertIn('.operand_field_id = "cp_size",', descriptor)
        self.assertIn('AsyncCaSharedGlobal_address_alignment_0_address_fields = {{"dst", "src"}};', descriptor)
        self.assertIn('.immediate_operand_field_id = "cp_size",', descriptor)
        self.assertIn('.operand_field_id = "n",', descriptor)
        self.assertIn('.minimum = uint64_t{0ULL},', descriptor)
        self.assertIn('.has_maximum = false,', descriptor)
        self.assertIn('.maximum = ~uint64_t{0},', descriptor)

    def test_membar_levels_model(self) -> None:
        database = self.database
        membar = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "membar"
        )
        resolved = from_instruction_spec(membar)

        self.assertEqual(resolved.cpp_name, "Membar")
        self.assertEqual(
            [variant.cpp_name for variant in resolved.variants],
            ["Cta", "Gl", "Sys", "ProxyAlias", "ProxyAsync",
             "ProxyAsyncSharedCluster"],
        )
        for variant, floor, scope in zip(
            resolved.variants[:3],
            (
                {"ptx": "1.4", "sm": 0},
                {"ptx": "1.4", "sm": 0},
                {"ptx": "2.0", "sm": 20},
            ),
            ("MemoryScope::Cta", "MemoryScope::Gpu", "MemoryScope::Sys"),
            strict=True,
        ):
            self.assertEqual(dict(variant.availability), floor)
            self.assertEqual(
                [(field.name, field_cpp_type(field)) for field in variant.fields],
                [("scope", "MemoryScope")],
            )
            self.assertEqual(field_cpp_constant_expr(variant.fields[0]), scope)
            self.assertEqual(variant.operand_layouts[0].bindings, ())

        proxy_alias = resolved.variants[3]
        self.assertEqual(dict(proxy_alias.availability),
                         {"ptx": "7.5", "sm": 60})
        self.assertEqual(
            [(field.name, field_cpp_type(field), field_cpp_constant_expr(field))
             for field in proxy_alias.fields],
            [("proxy", "bool", "true"), ("alias", "bool", "true")],
        )
        self.assertEqual(proxy_alias.operand_layouts[0].bindings, ())

        proxy_async, async_cluster = resolved.variants[4:]
        self.assertEqual(dict(proxy_async.availability),
                         {"ptx": "8.0", "sm": 90})
        self.assertEqual(
            [(field.name, field_cpp_type(field))
             for field in proxy_async.fields],
            [("proxy", "bool"),
             ("proxy_kind", "WithLocs<AsyncProxyKind>")],
        )
        self.assertEqual(
            dict(async_cluster.availability),
            {"any_of": [{"ptx": "8.0", "sm": 90,
                         "capabilities": ["cluster"]}]},
        )
        self.assertEqual(proxy_async.operand_layouts[0].bindings, ())
        self.assertEqual(async_cluster.operand_layouts[0].bindings, ())

    def test_fence_acq_rel_cta_model(self) -> None:
        database = self.database
        fence = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "fence"
        )
        resolved = from_instruction_spec(fence)

        self.assertEqual(resolved.cpp_name, "Fence")
        self.assertEqual(
            [variant.cpp_name for variant in resolved.variants],
            [
                "AcqRelCta",
                "OrdinaryCta",
                "OrdinaryGpuSys",
                "OrdinaryCluster",
                "MbarrierInitReleaseCluster",
                "AcquireSyncRestrictSharedCluster",
                "ReleaseSyncRestrictSharedCta",
                "ProxyAlias",
                "ProxyAsync",
                "ProxyAsyncSharedCluster",
                "ProxyTensormapGenericRelease",
                "ProxyTensormapGenericReleaseCluster",
                "ProxyTensormapGenericAcquire",
                "ProxyTensormapGenericAcquireCluster",
                "ProxyAsyncGenericAcquireSyncRestrictSharedCluster",
                "ProxyAsyncGenericReleaseSyncRestrictSharedCta",
                "ProxyGenericToFabricAcquire",
                "ProxyGenericToFabricRelease",
                "ProxyFabricToGenericAcquire",
                "ProxyFabricToGenericRelease",
                "ProxyFabricToFabricAcquire",
                "ProxyFabricToFabricRelease",
            ],
        )
        (variant, ordinary_cta, ordinary_gpu_sys, ordinary_cluster,
         mbarrier_init, acquire_restrict, release_restrict, proxy_alias, async_proxy,
         async_cluster, release, _, acquire, _, acquire_sync,
         release_sync) = resolved.variants[:16]
        self.assertEqual(dict(variant.availability), {"ptx": "6.0", "sm": 70})
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in variant.fields],
            [("semantics", "MemoryConsistency"), ("scope", "MemoryScope")],
        )
        self.assertEqual(variant.operand_layouts[0].bindings, ())
        for ordinary in (ordinary_cta, ordinary_gpu_sys, ordinary_cluster):
            self.assertEqual(ordinary.operand_layouts[0].bindings, ())
        self.assertEqual(
            [(field.name, field_cpp_type(field))
             for field in ordinary_cta.fields],
            [("semantics", "WithLocs<MemoryConsistency>"),
             ("scope", "MemoryScope")],
        )
        self.assertEqual(
            [(field.name, field_cpp_type(field))
             for field in ordinary_gpu_sys.fields],
            [("semantics", "WithLocs<MemoryConsistency>"),
             ("scope", "WithLocs<MemoryScope>")],
        )
        self.assertEqual(dict(ordinary_cluster.availability),
                         {"any_of": [{"ptx": "7.8", "sm": 90,
                                      "capabilities": ["cluster"]}]})
        self.assertEqual(
            dict(mbarrier_init.availability),
            {"any_of": [{"ptx": "8.0", "sm": 90,
                         "capabilities": ["cluster"]}]},
        )
        self.assertEqual(
            [(field.name, field_cpp_type(field), field_cpp_constant_expr(field))
             for field in mbarrier_init.fields],
            [("op_restrict", "bool", "true"),
             ("semantics", "MemoryConsistency", "MemoryConsistency::Release"),
             ("scope", "MemoryScope", "MemoryScope::Cluster")],
        )
        self.assertEqual(mbarrier_init.operand_layouts[0].bindings, ())
        self.assertEqual(dict(proxy_alias.availability),
                         {"ptx": "7.5", "sm": 70})
        self.assertEqual(
            [(field.name, field_cpp_type(field), field_cpp_constant_expr(field))
             for field in proxy_alias.fields],
            [("proxy", "bool", "true"), ("alias", "bool", "true")],
        )
        self.assertEqual(proxy_alias.operand_layouts[0].bindings, ())
        for restricted, semantics, flag in (
            (acquire_restrict, "Acquire", "sync_restrict_shared_cluster"),
            (release_restrict, "Release", "sync_restrict_shared_cta"),
        ):
            self.assertEqual(
                dict(restricted.availability),
                {"any_of": [{"ptx": "8.6", "sm": 90,
                             "capabilities": ["cluster"]}]},
            )
            self.assertEqual(
                [(field.name, field_cpp_type(field), field_cpp_constant_expr(field))
                 for field in restricted.fields],
                [("semantics", "MemoryConsistency",
                  f"MemoryConsistency::{semantics}"),
                 (flag, "bool", "true"),
                 ("scope", "MemoryScope", "MemoryScope::Cluster")],
            )
            self.assertEqual(restricted.operand_layouts[0].bindings, ())
        self.assertEqual(
            BACKEND.domains["memory_consistencies"].values["sc"],
            "MemoryConsistency::Sc",
        )
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in async_proxy.fields],
            [("proxy", "bool"), ("proxy_kind", "WithLocs<AsyncProxyKind>")],
        )
        self.assertEqual(
            dict(async_cluster.availability),
            {"any_of": [{"ptx": "8.0", "sm": 90, "capabilities": ["cluster"]}]},
        )
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in release.fields],
            [
                ("proxy", "bool"),
                ("proxy_pair", "WithLocs<ProxyKindPair>"),
                ("semantics", "MemoryConsistency"),
                ("scope", "WithLocs<MemoryScope>"),
            ],
        )
        self.assertEqual(len(acquire.operand_layouts[0].bindings), 2)
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in acquire_sync.fields],
            [
                ("proxy", "bool"),
                ("proxy_pair", "WithLocs<ProxyKindPair>"),
                ("semantics", "MemoryConsistency"),
                ("sync_restrict_shared_cluster", "bool"),
                ("scope", "MemoryScope"),
            ],
        )
        self.assertEqual(release_sync.operand_layouts[0].bindings, ())

    def test_atomic_reduction_scalar_model(self) -> None:
        """One operation/type variant owns independent typed qualifier slots."""
        for opcode, count, base_ptx in (("atom", 45, "1.1"), ("red", 38, "1.2")):
            instruction = next(
                item for item in self.database.instructions if item.opcode == opcode
            )
            resolved = from_instruction_spec(instruction)
            variants = [variant for variant in resolved.variants
                        if not variant.cpp_name.startswith("Async")]
            self.assertEqual(len(variants), count)
            self.assertEqual(len({variant.cpp_name for variant in variants}), count)
            for variant in variants:
                self.assertEqual(
                    tuple(field.name for field in variant.modifier_fields[:3]),
                    ("semantics", "scope", "state_space"),
                )
            add = next(v for v in resolved.variants if v.cpp_name == "GlobalAddU32")
            self.assertEqual(dict(add.availability), {"ptx": base_ptx, "sm": 11})
            self.assertEqual(
                [(field.name, field_cpp_type(field)) for field in add.modifier_fields[:3]],
                [
                    ("semantics", "WithLocs<MemoryConsistency>"),
                    ("scope", "WithLocs<MemoryScope>"),
                    ("state_space", "WithLocs<MemoryStateSpace>"),
                ],
            )
            self.assertEqual(
                tuple(binding.target_field_id for binding in add.operand_layouts[0].bindings),
                ("dst", "address", "src") if opcode == "atom" else ("address", "src"),
            )
            address = next(binding for binding in add.operand_layouts[0].bindings
                           if binding.target_field_id == "address")
            self.assertIsNone(address.state_space_modifier_field_id)
            self.assertEqual({entry.value for entry in address.allowed_address_state_spaces},
                             {"global", "shared"})
            source = add.operand_layouts[0].bindings[-1]
            self.assertEqual(source.allowed_shapes,
                             (ResolvedOperandShape.REGISTER, ResolvedOperandShape.IMMEDIATE))
            self.assertEqual(source.immediate_conversion_policy,
                             ResolvedImmediateConversionPolicy.NARROW)
            self.assertEqual(add.address_alignments[0].type_field_id, "type")

        atom = next(item for item in self.database.instructions if item.opcode == "atom")
        cas = next(v for v in from_instruction_spec(atom).variants
                   if v.cpp_name == "GlobalCasB64")
        self.assertEqual(dict(cas.availability), {"ptx": "1.2", "sm": 12})
        self.assertEqual(tuple(binding.target_field_id for binding in cas.operand_layouts[0].bindings),
                         ("dst", "address", "compare", "swap"))
        for binding in cas.operand_layouts[0].bindings[-2:]:
            self.assertEqual(binding.allowed_shapes,
                             (ResolvedOperandShape.REGISTER, ResolvedOperandShape.IMMEDIATE))

    def test_float_atomic_reduction_descriptor_contract(self) -> None:
        """Float variants retain equal-width operand and base target floors."""
        for opcode in ("atom", "red"):
            instruction = next(item for item in self.database.instructions if item.opcode == opcode)
            variants = {variant.cpp_name: variant for variant in from_instruction_spec(instruction).variants}
            expected_fields = ("dst", "address", "src") if opcode == "atom" else ("address", "src")
            for scalar, ptx, sm in (("F32", "2.0", 20), ("F64", "5.0", 60)):
                variant = variants[f"GlobalAdd{scalar}"]
                self.assertEqual(dict(variant.availability), {"ptx": ptx, "sm": sm})
                bindings = variant.operand_layouts[0].bindings
                self.assertEqual(tuple(binding.target_field_id for binding in bindings), expected_fields)
                self.assertEqual(bindings[-1].register_width_policy,
                                 ResolvedRegisterWidthPolicy.SAME_WIDTH)
                self.assertEqual(bindings[-1].allowed_shapes,
                                 (ResolvedOperandShape.REGISTER, ResolvedOperandShape.IMMEDIATE))

    def test_half_bfloat_and_wide_atomic_descriptor_contract(self) -> None:
        """Pin scalar register compatibility and target floors by cohort."""
        expected = {
            "atom": {
                "GlobalCasB16": ("6.3", 70, "b16", ResolvedRegisterWidthPolicy.SAME_WIDTH),
                "GlobalCasB128": ("8.3", 90, "b128", ResolvedRegisterWidthPolicy.EXACT),
                "GlobalExchB128": ("8.3", 90, "b128", ResolvedRegisterWidthPolicy.EXACT),
                "GlobalAddNoftzF16": ("6.3", 70, "f16", ResolvedRegisterWidthPolicy.SAME_WIDTH),
                "GlobalAddNoftzF16x2": ("6.2", 60, "f16x2", ResolvedRegisterWidthPolicy.SAME_WIDTH),
                "GlobalAddNoftzBf16": ("7.8", 90, "b16", ResolvedRegisterWidthPolicy.EXACT),
                "GlobalAddNoftzBf16x2": ("7.8", 90, "b32", ResolvedRegisterWidthPolicy.EXACT),
            },
            "red": {
                "GlobalAddNoftzF16": ("6.3", 70, "f16", ResolvedRegisterWidthPolicy.SAME_WIDTH),
                "GlobalAddNoftzF16x2": ("6.2", 60, "f16x2", ResolvedRegisterWidthPolicy.SAME_WIDTH),
                "GlobalAddNoftzBf16": ("7.8", 90, "b16", ResolvedRegisterWidthPolicy.EXACT),
                "GlobalAddNoftzBf16x2": ("7.8", 90, "b32", ResolvedRegisterWidthPolicy.EXACT),
            },
        }
        for opcode, tuples in expected.items():
            instruction = next(item for item in self.database.instructions
                               if item.opcode == opcode)
            variants = {variant.cpp_name: variant
                        for variant in from_instruction_spec(instruction).variants}
            for name, (ptx, sm, container, width_policy) in tuples.items():
                variant = variants[name]
                self.assertEqual(dict(variant.availability), {"ptx": ptx, "sm": sm})
                bindings = variant.operand_layouts[0].bindings
                source_bindings = tuple(binding for binding in bindings
                                        if binding.target_field_id not in ("dst", "address"))
                self.assertTrue(source_bindings)
                for binding in source_bindings:
                    self.assertEqual(binding.type_expression.scalar_type, container)
                    self.assertEqual(binding.register_width_policy,
                                     width_policy)
                if opcode == "atom":
                    dst = next(field for field in variant.operand_layouts[0].fields
                               if field.name == "dst")
                    self.assertEqual(field_cpp_type(dst),
                                     "WithLocs<ResolvedRegisterOrSink>")
                if "Noftz" in name:
                    self.assertIn("noftz", (field.name for field in variant.modifier_fields))
                if name in ("GlobalCasB128", "GlobalExchB128"):
                    sys = next(value for value in variant.modifier_value_availabilities
                               if value.source_kind_id == "scope" and value.value == "sys")
                    self.assertEqual(dict(sys.availability), {"ptx": "8.4", "sm": 90})

    def test_scalar_atomic_cache_hint_layout_contract(self) -> None:
        """Use one typed policy layout per eligible scalar tuple, without a suffix matrix."""
        for opcode, count, hinted in (("atom", 32, 28), ("red", 25, 25)):
            instruction = next(item for item in self.database.instructions
                               if item.opcode == opcode)
            variants = [variant for variant in from_instruction_spec(instruction).variants
                        if not variant.cpp_name.startswith(("Vector", "Async"))]
            self.assertEqual(len(variants), count)
            self.assertEqual(sum(len(v.operand_layouts) == 2 for v in variants), hinted)
            for variant in variants:
                if "Cas" in variant.cpp_name:
                    self.assertEqual(len(variant.operand_layouts), 1)
                    self.assertNotIn("cache_hint", (field.name for field in variant.modifier_fields))
                    continue
                self.assertEqual(tuple(layout.layout_id for layout in variant.operand_layouts),
                                 ("no_hint", "with_policy"))
                self.assertIn("cache_hint", (field.name for field in variant.modifier_fields))
                policy_layout = variant.operand_layouts[1]
                self.assertEqual(dict(policy_layout.availability), {"ptx": "7.4", "sm": 80})
                policy = policy_layout.bindings[-1]
                self.assertEqual(policy.target_field_id, "cache_policy")
                self.assertEqual(policy.type_expression.scalar_type, "b64")
                self.assertEqual(policy.allowed_shapes, (ResolvedOperandShape.REGISTER,))
                address = next(binding for binding in policy_layout.bindings
                               if binding.target_field_id == "address")
                self.assertEqual(tuple(space.value for space in address.allowed_address_state_spaces),
                                 ("global",))
                hint = next(value for value in variant.modifier_value_availabilities
                            if value.source_kind_id == "cache_hint" and value.value is True)
                self.assertEqual(dict(hint.availability), {"ptx": "7.4", "sm": 80})

    def test_red_async_mode_contract(self) -> None:
        """Pin disjoint asynchronous layouts, closed tuples, and address bases."""
        instruction = next(item for item in self.database.instructions
                           if item.opcode == "red")
        variants = {variant.cpp_name: variant
                    for variant in from_instruction_spec(instruction).variants
                    if variant.cpp_name.startswith("Async")}
        shared = {name: variant for name, variant in variants.items()
                  if name.startswith("AsyncShared")}
        release = {name: variant for name, variant in variants.items()
                   if name.startswith("AsyncRelease")}
        self.assertEqual(set(shared), {
            "AsyncSharedIncU32", "AsyncSharedDecU32", "AsyncSharedMinU32",
            "AsyncSharedMinS32", "AsyncSharedMaxU32", "AsyncSharedMaxS32",
            "AsyncSharedAndB32", "AsyncSharedOrB32", "AsyncSharedXorB32",
            "AsyncSharedAddU32", "AsyncSharedAddS32", "AsyncSharedAddU64",
        })
        self.assertEqual(set(release), {
            "AsyncReleaseAddU32", "AsyncReleaseAddS32",
            "AsyncReleaseAddU64", "AsyncReleaseAddS64",
        })
        for cohort, floor, fields, spaces in (
            (shared, {"ptx": "8.1", "sm": 90},
             ("address", "src", "mbarrier"), ("shared",)),
            (release, {"ptx": "8.7", "sm": 100},
             ("address", "src"), ("global",)),
        ):
            for variant in cohort.values():
                self.assertEqual(dict(variant.availability), floor)
                self.assertEqual(len(variant.operand_layouts), 1)
                bindings = variant.operand_layouts[0].bindings
                self.assertEqual(tuple(binding.target_field_id for binding in bindings),
                                 fields)
                address = bindings[0]
                self.assertEqual(address.address_base_policy,
                                 OperandAddressBasePolicy.REGISTER)
                self.assertEqual(address.address_offset_domain,
                                 OperandAddressOffsetDomain.SIGNED32)
                self.assertEqual(tuple(space.value for space in
                                       address.allowed_address_state_spaces), spaces)
                self.assertEqual(variant.address_alignments[0].type_field_id, "type")
                if cohort is shared:
                    self.assertEqual(bindings[2].address_base_policy,
                                     OperandAddressBasePolicy.REGISTER)
                    self.assertEqual(bindings[2].address_offset_domain,
                                     OperandAddressOffsetDomain.SIGNED32)
                    self.assertEqual(variant.address_alignments[1].alignment, 8)
        self.assertTrue(all("completion" in
                            (field.name for field in variant.modifier_fields)
                            for variant in shared.values()))
        self.assertTrue(all("mmio" in
                            (field.name for field in variant.modifier_fields)
                            for variant in release.values()))
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "red_async.gen.cpp"
            generate_resolved_opcode_source(
                build_test_generation_context(self.database),
                category="parallel_synchronization_and_communication",
                opcode="red",
                output_path=output,
            )
            generated = output.read_text()
        self.assertIn("checker::AddressBaseKind::Register", generated)
        self.assertGreaterEqual(generated.count(".address_base_kind ="), 16)

    def test_atomic_address_offset_domain_is_scoped(self) -> None:
        """Every atom/red address uses signed32; unrelated addresses keep defaults."""
        for opcode in ("atom", "red"):
            instruction = next(item for item in self.database.instructions
                               if item.opcode == opcode)
            for variant in from_instruction_spec(instruction).variants:
                for layout in variant.operand_layouts:
                    for binding in layout.bindings:
                        if binding.target_field_id in ("address", "mbarrier"):
                            self.assertEqual(binding.address_offset_domain,
                                             OperandAddressOffsetDomain.SIGNED32)
        mov = next(item for item in self.database.instructions if item.opcode == "mov")
        for variant in from_instruction_spec(mov).variants:
            for layout in variant.operand_layouts:
                for binding in layout.bindings:
                    self.assertEqual(binding.address_offset_domain,
                                     OperandAddressOffsetDomain.UNRESTRICTED)

    def test_vector_atomic_reduction_model(self) -> None:
        """Typed arity domains cover every legal tuple without suffix expansion."""
        for opcode in ("atom", "red"):
            instruction = next(item for item in self.database.instructions
                               if item.opcode == opcode)
            raw = [variant for variant in instruction.variants
                   if variant.name.startswith(f"{opcode}_vector_")]
            resolved = [variant for variant in from_instruction_spec(instruction).variants
                        if variant.cpp_name.startswith("Vector")]
            self.assertEqual(len(raw), 13)
            self.assertEqual(len(resolved), 13)
            self.assertEqual(sum(len(next(modifier for modifier in variant.modifiers
                                          if modifier.name == "vector").values)
                                 for variant in raw), 32)
            for variant in resolved:
                self.assertEqual(dict(variant.availability), {"ptx": "8.1", "sm": 90})
                self.assertEqual(variant.address_alignments[0].vector_field_id, "vector")
                self.assertEqual(tuple(layout.layout_id for layout in variant.operand_layouts),
                                 ("no_hint", "with_policy"))
                for layout in variant.operand_layouts:
                    names = tuple(binding.target_field_id for binding in layout.bindings)
                    self.assertEqual(names[:3] if opcode == "atom" else names[:2],
                                     ("dst", "address", "src") if opcode == "atom"
                                     else ("address", "src"))
                    address = next(binding for binding in layout.bindings
                                   if binding.target_field_id == "address")
                    self.assertEqual(tuple(space.value for space in
                                           address.allowed_address_state_spaces), ("global",))
                    source = next(binding for binding in layout.bindings
                                  if binding.target_field_id == "src")
                    self.assertEqual(source.allowed_shapes, (ResolvedOperandShape.VECTOR,))
                    self.assertEqual(source.register_width_policy,
                                     ResolvedRegisterWidthPolicy.SAME_WIDTH)
                    self.assertFalse(source.allow_vector_sink)
                    self.assertTrue(source.require_uniform_vector_register_family)
                    lane_types = set(source.allowed_vector_register_types)
                    if variant.cpp_name.endswith("Bf16"):
                        self.assertEqual(lane_types, {"b16"})
                    elif variant.cpp_name.endswith("F16"):
                        self.assertEqual(lane_types, {"b16", "f16", "u16", "s16"})
                    elif variant.cpp_name.endswith("F32"):
                        self.assertEqual(lane_types, {"b32", "f32", "u32", "s32"})
                    elif variant.cpp_name.endswith("F16x2"):
                        self.assertEqual(lane_types, {"b32", "f16x2"})
                    else:
                        self.assertEqual(lane_types, {"b32"})
                if opcode == "atom":
                    destination = variant.operand_layouts[0].bindings[0]
                    self.assertEqual(destination.register_width_policy,
                                     ResolvedRegisterWidthPolicy.SAME_WIDTH)
                    self.assertTrue(destination.allow_vector_sink)
                    self.assertEqual(destination.allowed_vector_register_types,
                                     source.allowed_vector_register_types)
                self.assertEqual(variant.operand_layouts[1].bindings[-1].target_field_id,
                                 "cache_policy")

    def test_vector_atomic_modifier_orders(self) -> None:
        """Keep ISA operation-first syntax and supported vector-first examples."""
        raw_spec = yaml.safe_load((REPO_ROOT / "instructions/ptx_spec" /
                                   "parallel_synchronization_and_communication.yaml").read_text())
        for opcode in ("atom", "red"):
            raw_instruction = next(item for item in raw_spec["instructions"]
                                   if item["opcode"] == opcode)
            instruction = next(item for item in self.database.instructions
                               if item.opcode == opcode)
            self.assertIn(
                f"{opcode}{{.sem}}{{.scope}}{{.space}}.{{op}}{{.noftz}}"
                "{.L2::cache_hint}.v{2|4|8}.{type}",
                raw_instruction["syntax"],
            )
            variants = [variant for variant in instruction.variants
                        if variant.name.startswith(f"{opcode}_vector_")]
            self.assertEqual(len(variants), 13)
            for variant in variants:
                operation = next(modifier.name for modifier in variant.modifiers
                                 if modifier.name in {"add", "min", "max"})
                noftz = ("noftz",) if any(modifier.name == "noftz"
                                           for modifier in variant.modifiers) else ()
                suffix = (operation, *noftz, "cache_hint")
                self.assertIn(
                    ("state_space", "semantics", "scope", "vector", "type", *suffix),
                    variant.modifier_order_aliases,
                )
                self.assertIn(
                    ("semantics", "scope", "state_space", *suffix, "vector", "type"),
                    variant.modifier_order_aliases,
                )
                self.assertIn(
                    ("state_space", "semantics", "scope", *suffix, "vector", "type"),
                    variant.modifier_order_aliases,
                )

    def test_atomic_address_qualifier_policy_drives_generated_contract(self) -> None:
        """Derive each written qualifier domain from the declared state space."""
        from ptx_frontend.ir.resolved_ir import AtomicAddressQualifierValue
        from ptx_frontend.spec.model import AtomicAddressQualifierPolicy

        instructions = {item.opcode: item for item in self.database.instructions}
        expected_scalar = {
            AtomicAddressQualifierValue.GENERIC,
            AtomicAddressQualifierValue.GLOBAL,
            AtomicAddressQualifierValue.SHARED,
            AtomicAddressQualifierValue.SHARED_CTA,
            AtomicAddressQualifierValue.SHARED_CLUSTER,
        }
        for opcode in ("atom", "red"):
            source = instructions[opcode]
            self.assertEqual(
                source.atomic_address_qualifier,
                AtomicAddressQualifierPolicy("state_space", "address"),
            )
            resolved = from_instruction_spec(source)
            self.assertIsNotNone(resolved.atomic_address_qualifier)
            for variant in resolved.variants:
                domain = set(variant.atomic_address_qualifier_domain)
                if variant.cpp_name.startswith("Vector"):
                    self.assertEqual(domain, {
                        AtomicAddressQualifierValue.GENERIC,
                        AtomicAddressQualifierValue.GLOBAL,
                    })
                elif variant.cpp_name.startswith("AsyncShared"):
                    self.assertEqual(domain, {
                        AtomicAddressQualifierValue.GENERIC,
                        AtomicAddressQualifierValue.SHARED_CLUSTER,
                    })
                elif variant.cpp_name.startswith("AsyncRelease"):
                    self.assertEqual(domain, {
                        AtomicAddressQualifierValue.GENERIC,
                        AtomicAddressQualifierValue.GLOBAL,
                    })
                else:
                    self.assertEqual(domain, expected_scalar)

        self.assertIsNone(instructions["bar"].atomic_address_qualifier)
        self.assertIsNone(from_instruction_spec(instructions["bar"])
                          .atomic_address_qualifier)

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            context = build_test_generation_context(self.database)
            for opcode in ("atom", "red"):
                generate_resolved_opcode_header(
                    context, category="parallel_synchronization_and_communication",
                    opcode=opcode, output_path=path / f"{opcode}.hpp",
                )
                generate_resolved_opcode_source(
                    context, category="parallel_synchronization_and_communication",
                    opcode=opcode, output_path=path / f"{opcode}.cpp",
                )
            generate_resolved_checker_descriptor_source(
                context, category="parallel_synchronization_and_communication",
                output_path=path / "descriptors.cpp",
            )
            model = "\n".join((path / f"{opcode}.hpp").read_text()
                              for opcode in ("atom", "red"))
            logic = "\n".join((path / f"{opcode}.cpp").read_text()
                              for opcode in ("atom", "red"))
            descriptors = (path / "descriptors.cpp").read_text()
        self.assertEqual(
            model.count("WithLocs<AtomicAddressQualifier> address_qualifier;"),
            sum(len(instructions[opcode].variants) for opcode in ("atom", "red")),
        )
        self.assertIn("value->address_qualifier = atomic_address_qualifier_from_ast(ast)", logic)
        self.assertIn(".atomic_address_qualifier,", logic)
        self.assertIn(".state_space_field_id = \"state_space\"", descriptors)
        self.assertIn(".address_operand_id = \"address\"", descriptors)
        self.assertIn("AtomicAddressQualifier::SharedCluster", descriptors)

        for bad_policy in (
            AtomicAddressQualifierPolicy("missing", "address"),
            AtomicAddressQualifierPolicy("state_space", "missing"),
        ):
            with self.assertRaisesRegex(ValueError, "atomic qualifier requires"):
                from_instruction_spec(replace(instructions["atom"],
                                              atomic_address_qualifier=bad_policy))
        atom_variant = instructions["atom"].variants[0]
        bad_modifiers = tuple(
            replace(modifier, values=(ModifierValueSpec("local"),))
            if modifier.name == "state_space" else modifier
            for modifier in atom_variant.modifiers
        )
        with self.assertRaisesRegex(ValueError, "unsupported atomic address qualifier"):
            from_instruction_spec(replace(
                instructions["atom"],
                variants=(replace(atom_variant, modifiers=bad_modifiers),),
            ))

    def test_vector_atomic_register_domain_codegen(self) -> None:
        """Emit lane-type domains and family policy only for opted-in vectors."""
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "atomic_descriptor.cpp"
            generate_resolved_descriptor_source(
                build_test_generation_context(self.database),
                category="parallel_synchronization_and_communication",
                output_path=output,
            )
            generated = output.read_text()
        self.assertIn("atom_vector_add_noftz_f16_operand_layout_0_binding_0_register_types",
                      generated)
        self.assertIn(".allowed_register_types =", generated)
        self.assertIn(".require_uniform_register_family = true", generated)
        self.assertIn("red_async_shared_add_u32_operand_layout_0_binding_0_address_state_spaces",
                      generated)
        self.assertEqual(generated.count(
            ".address_base_policy = checker::AddressBasePolicy::Register"), 28)
        self.assertIn(
            ".address_offset_domain = checker::AddressOffsetDomain::Signed32",
            generated,
        )
        self.assertIn("ScalarType::F16", generated)
        self.assertIn("ScalarType::S16", generated)
        load = next(item for item in self.database.instructions if item.opcode == "ld")
        ordinary_vectors = [binding for variant in from_instruction_spec(load).variants
                            for layout in variant.operand_layouts
                            for binding in layout.bindings
                            if binding.allowed_shapes == (ResolvedOperandShape.VECTOR,)]
        self.assertTrue(ordinary_vectors)
        self.assertTrue(all(not binding.allowed_vector_register_types and
                            not binding.require_uniform_vector_register_family
                            for binding in ordinary_vectors))

    def test_activemask_b32_model(self) -> None:
        database = self.database
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
            [(field.name, field_cpp_type(field)) for field in variant.fields],
            [("type", "ScalarType"), ("dst", "WithLocs<ResolvedRegisterRef>")],
        )
        self.assertEqual(
            variant.operand_layouts[0].bindings[0].register_width_policy,
            ResolvedRegisterWidthPolicy.SAME_WIDTH,
        )

    def test_vote_sync_ballot_b32_model(self) -> None:
        database = self.database
        vote = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "vote"
        )
        resolved = from_instruction_spec(vote)

        self.assertEqual(resolved.cpp_name, "Vote")
        self.assertEqual(
            [variant.cpp_name for variant in resolved.variants],
            ["SyncBallotB32", "SyncAllPred", "SyncAnyPred", "SyncUniPred"],
        )
        variant = resolved.variants[0]
        self.assertEqual(dict(variant.availability), {"ptx": "6.0", "sm": 30})
        self.assertEqual(
            [(field.name, field_cpp_type(field)) for field in variant.fields],
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
            ResolvedRegisterWidthPolicy.SAME_WIDTH,
        )
        for predicate_vote in resolved.variants[1:]:
            self.assertEqual(
                dict(predicate_vote.availability), {"ptx": "6.0", "sm": 30}
            )
            self.assertEqual(
                [
                    (field.name, field_cpp_type(field))
                    for field in predicate_vote.operand_layouts[0].fields
                ],
                [
                    ("dst", "WithLocs<ResolvedPredicate>"),
                    ("predicate", "WithLocs<ResolvedPredicate>"),
                    ("membermask", "WithLocs<RegOrImm>"),
                ],
            )

    def test_shfl_sync_idx_b32_model(self) -> None:
        database = self.database
        shfl = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "shfl"
        )
        resolved = from_instruction_spec(shfl)

        self.assertEqual(resolved.cpp_name, "Shfl")
        self.assertEqual(
            [variant.cpp_name for variant in resolved.variants],
            ["SyncIdxB32", "SyncUpB32", "SyncDownB32", "SyncBflyB32"],
        )
        variant = resolved.variants[0]
        self.assertEqual(variant.cpp_name, "SyncIdxB32")
        self.assertEqual(dict(variant.availability), {"ptx": "6.0", "sm": 30})
        self.assertEqual(
            [
                (field.name, field_cpp_type(field))
                for field in variant.operand_layouts[1].fields
            ],
            [
                ("dst", "WithLocs<ResolvedShflSyncDestination>"),
                ("src", "WithLocs<ResolvedRegisterRef>"),
                ("lane", "WithLocs<RegOrImm>"),
                ("clamp", "WithLocs<RegOrImm>"),
                ("membermask", "WithLocs<RegOrImm>"),
            ],
        )
        self.assertEqual(
            variant.operand_layouts[1].bindings[0].allowed_shapes,
            (ResolvedOperandShape.SHFL_DESTINATION,),
        )
        self.assertEqual(
            variant.operand_layouts[1].bindings[0].register_width_policy,
            ResolvedRegisterWidthPolicy.SAME_WIDTH,
        )
        self.assertEqual(
            field_cpp_type(variant.operand_layouts[0].fields[0]),
            "WithLocs<ResolvedRegisterRef>",
        )
        for mode in resolved.variants:
            self.assertEqual(dict(mode.availability), {"ptx": "6.0", "sm": 30})
            self.assertEqual(
                [layout.cpp_name for layout in mode.operand_layouts],
                ["WithoutPredicate", "WithPredicate"],
            )

    def test_shfl_generator_emits_pair_operand_view(self) -> None:
        database = self.database
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir_data_movement.gen.cpp"
            generate_resolved_opcode_source(build_test_generation_context(database),
                category="data_movement",
                opcode="shfl",
                output_path=output_path,
            )
            source = output_path.read_text(encoding="utf-8")

        self.assertIn("ResolvedShflSyncDestination", source)
        self.assertIn(
            ".actual_shape = check_end::OperandShape::ShflDestination,", source
        )
        self.assertGreaterEqual(
            source.count(".actual_shape = check_end::OperandShape::ShflDestination,"),
            4,
        )

    def test_setp_generator_emits_predicate_pair_operand_view(self) -> None:
        database = self.database
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir_comparison_and_selection.gen.cpp"
            generate_resolved_opcode_source(build_test_generation_context(database),
                category="comparison_and_selection",
                opcode="setp",
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
        database = self.database

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
                    cache_binding.default_value.value_kind.value,
                    "CacheOperator",
                )
                self.assertEqual(cache_binding.default_value.value, "unspecified")

    def test_comparison_models_have_independent_category_header(self) -> None:
        """Keep each exact comparison form in its narrow installed opcode leaf."""
        context = build_test_generation_context(self.database)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            comparison = root / "set.gen.hpp"
            arithmetic = root / "add.gen.hpp"
            umbrella = root / "ptx_resolved_ir.gen.hpp"
            generate_resolved_opcode_header(
                context, category="comparison_and_selection",
                opcode="set", output_path=comparison,
            )
            generate_resolved_opcode_header(
                context, category="arithmetic", opcode="add",
                output_path=arithmetic,
            )
            generate_resolved_umbrella_header(context, output_path=umbrella)
            comparison_source = comparison.read_text(encoding="utf-8")
            arithmetic_source = arithmetic.read_text(encoding="utf-8")
            umbrella_source = umbrella.read_text(encoding="utf-8")

        self.assertIn("class SetBit final : public Instruction", comparison_source)
        self.assertNotIn("class AddIntegerNoSat", comparison_source)
        self.assertIn("class AddIntegerNoSat final : public Instruction", arithmetic_source)
        self.assertNotIn("class SetBit", arithmetic_source)
        for opcode in ("set", "setp", "selp", "slct"):
            self.assertIn(
                f"model/comparison_and_selection/{opcode}.gen.hpp", umbrella_source
            )
            self.assertNotIn(
                f"model/arithmetic/{opcode}.gen.hpp", umbrella_source
            )
        self.assertIn("model/arithmetic/add.gen.hpp", umbrella_source)
        self.assertNotIn("InstructionUnion", umbrella_source)

    def test_generate_resolved_ir_header(self) -> None:
        """Emit all exact classes and the aggregate without opcode owners."""
        context = build_test_generation_context(self.database)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root / "ptx_instruction_base.gen.hpp"
            catalogue = root / "ptx_instruction_catalogue.gen.hpp"
            umbrella = root / "ptx_resolved_ir.gen.hpp"
            generate_resolved_base_header(context, output_path=base)
            generate_resolved_identity_catalogue_header(
                context, output_path=catalogue)
            generate_resolved_umbrella_header(context, output_path=umbrella)
            leaves = []
            for entry in context.entries:
                leaf = root / f"{entry.specification.opcode}.gen.hpp"
                generate_resolved_opcode_header(
                    context, category=entry.specification.codegen_category,
                    opcode=entry.specification.opcode, output_path=leaf,
                )
                leaves.append(leaf.read_text(encoding="utf-8"))
            base_source = base.read_text(encoding="utf-8")
            catalogue_source = catalogue.read_text(encoding="utf-8")
            umbrella_source = umbrella.read_text(encoding="utf-8")
            source = "\n".join(leaves)

        self.assertTrue(base_source.startswith(
            "// Generated by ptx_frontend resolved IR code generation. Do not edit."
        ))
        self.assertIn("#pragma once", umbrella_source)
        self.assertIn("class Instruction", base_source)
        self.assertIn("enum class InstructionKind : std::uint32_t;", base_source)
        self.assertNotIn("AddIntegerNoSat =", base_source)
        self.assertIn("AddIntegerNoSat =", catalogue_source)
        self.assertIn("ptx_instruction_catalogue.gen.hpp", umbrella_source)
        self.assertIn("std::unique_ptr<Instruction> clone() const", source)
        self.assertIn("void visit_references(detail::IReferenceObserver&)", source)
        unsharded_forms = sum(
            len(entry.resolved.variants)
            for entry in context.entries if not form_shards(entry)
        )
        self.assertEqual(source.count(" final : public Instruction"), unsharded_forms)
        self.assertEqual(
            unsharded_forms + sum(
                len(indices) for entry in context.entries
                for indices in form_shards(entry)
            ),
            5275,
        )
        self.assertEqual(umbrella_source.count("/model/"), len(context.entries))
        for name in ("AddIntegerNoSat", "AtomGlobalAddU32", "BraDirect",
                     "MovScalar", "SetBit", "SetpUnsigned", "CallDirect"):
            self.assertTrue(
                f"class {name} final : public Instruction" in source, name
            )
        self.assertIn("WithLocs<ResolvedBranchTarget> target;", source)
        self.assertIn("std::optional<WithLocs<ResolvedMovSource>> src_mov_source;", source)
        self.assertIn("ResolvedOperandLayoutTag operand_layout;", source)
        self.assertIn("inline static constexpr bool saturate = true;", source)
        self.assertIn("std::optional<WithLocs<ResolvedFunctionRef>> target_direct_call_target;", source)
        self.assertNotIn("InstructionUnion", source)
        self.assertNotIn("using Variant = std::variant<", source)
        self.assertNotIn("struct OwnedInstruction", source)
        self.assertNotIn("struct ResolvedModule", source)
        self.assertNotIn("resolveInstruction(", source)
        self.assertNotIn("resolveModule(", source)
        self.assertIn("}  // namespace ptx_frontend::resolved_ir", source)

    def test_rejects_unclassified_reference_payload_type(self) -> None:
        """Future operand payloads must declare their reference policy."""

        variant = self.instruction.variants[0]
        layout = variant.operand_layouts[0]

        unknown_field = replace(
            layout.fields[0],
            value_kind=cast(ResolvedValueKind, object()),
        )
        unknown_layout = replace(
            layout,
            fields=(unknown_field, *layout.fields[1:]),
        )
        unknown_variant = replace(
            variant,
            operand_layouts=(
                unknown_layout,
                *variant.operand_layouts[1:],
            ),
        )
        unknown_instruction = replace(
            self.instruction,
            variants=(
                unknown_variant,
                *self.instruction.variants[1:],
            ),
        )

        with self.assertRaisesRegex(
            ValueError,
            "explicit module-reference policy",
        ):
            validate_reference_field_types((unknown_instruction,))

    def test_generate_resolved_instruction_dispatch_source(self) -> None:
        """Dispatch every opcode to its exact final-class resolver."""
        context = build_test_generation_context(self.database)
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir_dispatch.gen.cpp"
            generate_resolved_dispatch_source(context, output_path=output_path)
            source = output_path.read_text(encoding="utf-8")

        self.assertIn(
            "#include <ptx_frontend/resolved_ir/ptx_instruction_catalogue.gen.hpp>",
            source,
        )
        self.assertNotIn("ptx_resolved_ir.hpp", source)
        self.assertIn(
            "std::expected<std::unique_ptr<Instruction>, ResolveDiagnostic>",
            source,
        )
        self.assertIn("resolveInstruction(", source)
        self.assertIn("const syntax_ast::AstInstruction& ast", source)
        self.assertIn("& 0xffff0000u", source)
        self.assertEqual(source.count("case Opcode::"), len(context.entries))
        self.assertEqual(source.count('if (ast.opcode.syntax.text == "'), len(context.entries))
        for entry in context.entries:
            opcode = entry.specification.opcode
            self.assertIn(f'ast.opcode.syntax.text == "{opcode}"', source)
            self.assertIn(f"return resolve{entry.cpp_name}(ast, context);", source)
        self.assertIn("Unknown PTX opcode", source)
        self.assertNotIn("OwnedInstruction", source)
        self.assertNotIn("resolve_owned_", source)

    def test_generate_control_flow_resolved_ir_source(self) -> None:
        """Each control-flow opcode emits an exact checker and resolver."""
        context = build_test_generation_context(self.database)
        with tempfile.TemporaryDirectory() as directory:
            sources = {}
            for opcode in ("bra", "ret", "exit", "trap"):
                output = Path(directory) / f"{opcode}.cpp"
                generate_resolved_opcode_source(
                    context, category="control_flow", opcode=opcode,
                    output_path=output,
                )
                sources[opcode] = output.read_text(encoding="utf-8")

        bra = sources["bra"]
        self.assertIn("BraDirect::check(", bra)
        self.assertIn("resolveBra(", bra)
        self.assertIn("resolved_operand<ResolvedBranchTarget>", bra)
        self.assertIn("check_operand_layout_tag(", bra)
        for opcode, class_name in (
            ("ret", "RetBare"), ("exit", "ExitBare"), ("trap", "TrapBare")
        ):
            source = sources[opcode]
            self.assertIn(f"{class_name}::check(", source)
            self.assertIn(f"resolve{opcode.capitalize()}(", source)
            self.assertEqual(source.count("check_execution_predicate("), 1)
            self.assertIn("check_operand_layout_tag(", source)
            self.assertNotIn("OwnedInstruction", source)

    def test_generate_data_movement_resolved_ir_source(self) -> None:
        """Direct MOV, load, prefetch, and CVT emission retains shared checks."""
        context = build_test_generation_context(self.database)
        with tempfile.TemporaryDirectory() as directory:
            sources = {}
            for opcode in ("mov", "ld", "ldu", "prefetch", "cvt"):
                output = Path(directory) / f"{opcode}.cpp"
                generate_resolved_opcode_source(
                    context, category="data_movement", opcode=opcode,
                    output_path=output,
                )
                sources[opcode] = output.read_text(encoding="utf-8")

        mov = sources["mov"]
        self.assertIn("MovScalar::check(", mov)
        self.assertIn("MovPred::check(", mov)
        self.assertIn("resolveMov(", mov)
        self.assertIn("resolved_operand<ResolvedMovSource>", mov)
        self.assertIn("ResolvedVectorSpecialRegisterRef", mov)
        self.assertIn("ResolvedVectorRegisterRef", mov)
        self.assertIn("ResolvedPredicateSource", mov)
        self.assertIn("ResolvedPredicateSpecialRegister", mov)
        self.assertIn("ResolvedPredicateConstant", mov)
        self.assertIn(".immediate_type = ScalarType::Pred", mov)
        self.assertIn("special_register_availability(info)", mov)
        self.assertIn("state_space_from_symbol(symbol)", mov)
        self.assertIn("observer.mov_source(", mov)
        for opcode in ("ld", "ldu", "prefetch"):
            source = sources[opcode]
            self.assertIn(f"resolve{opcode.capitalize()}(", source)
            self.assertIn("resolved_operand<ResolvedAddress>", source)
            self.assertIn("check_operand_layout_tag(", source)
        self.assertIn("check_address_alignment(", sources["ld"])
        self.assertIn("check_address_alignment(", sources["ldu"])
        self.assertIn("check_memory_vector(", sources["ld"])
        self.assertIn("check_cvt_rule(", sources["cvt"])
        self.assertNotIn("OwnedInstruction", "\n".join(sources.values()))

    def test_generate_category_resolved_ir_source(self) -> None:
        """Arithmetic forms emit direct per-opcode checks without wrappers."""
        context = build_test_generation_context(self.database)
        opcodes = ("add", "and", "or", "xor", "not", "shl", "shr")
        with tempfile.TemporaryDirectory() as directory:
            sources = {}
            for opcode in opcodes:
                output = Path(directory) / f"{opcode}.cpp"
                generate_resolved_opcode_source(
                    context, category="arithmetic", opcode=opcode,
                    output_path=output,
                )
                sources[opcode] = output.read_text(encoding="utf-8")

        add = sources["add"]
        self.assertNotIn("#pragma once", add)
        self.assertIn(
            "#include <ptx_frontend/resolved_ir/model/arithmetic/add.gen.hpp>",
            add,
        )
        self.assertIn("std::expected<std::unique_ptr<Instruction>, ResolveDiagnostic>", add)
        self.assertIn("resolveAdd(", add)
        self.assertIn("resolve_fields(", add)
        self.assertIn("fields->execution_predicate", add)
        self.assertIn("selected.dst.value.declared_type", add)
        for class_name in ("AddIntegerNoSat", "AddSat", "AddPackedOptionalSat"):
            self.assertIn(f"{class_name}::check(", add)
        self.assertIn("check_operands(", add)
        self.assertIn("check_operand_layout_tag(", add)
        self.assertIn("check_modifier_value_availability(", add)
        self.assertNotIn("check_memory_consistency(", add)
        self.assertNotIn("std::visit(detail::Overloaded{", add)
        for opcode in opcodes[1:]:
            source = sources[opcode]
            self.assertIn(f"resolve{opcode.capitalize()}(", source)
            self.assertIn("check_operand_layout_tag(", source)
            self.assertNotIn("OwnedInstruction", source)

    def test_common_scalar_checker_contract_uses_shared_descriptor_pipeline(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            arithmetic = root / "arithmetic.gen.cpp"
            descriptor = root / "resolved_descriptor.gen.cpp"
            generate_resolved_opcode_source(build_test_generation_context(self.database), category="arithmetic", opcode="add", output_path=arithmetic
            )
            generate_resolved_descriptor_source(build_test_generation_context(self.database), category="arithmetic", output_path=descriptor
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
        database = self.database

        with tempfile.TemporaryDirectory() as directory:
            context = build_test_generation_context(database)
            sources = []
            for category in sorted({entry.specification.codegen_category for entry in context.entries}):
                output_path = Path(directory) / f"resolved_descriptor_{category}.gen.cpp"
                generate_resolved_descriptor_source(context, category=category, output_path=output_path)
                sources.append(output_path.read_text(encoding="utf-8"))
            source = "\n".join(sources)

        self.assertTrue(
            source.startswith("// Generated by ptx_frontend.code_gen. Do not edit.")
        )
        self.assertNotIn("#pragma once", source)
        self.assertIn('#include <ptx_frontend/resolved_ir/ptx_resolved_ir_descriptors.hpp>', source)
        self.assertIn('#include <ptx_frontend/resolved_ir/model/arithmetic.gen.hpp>', source)
        self.assertIn("namespace ptx_frontend::resolved_ir {", source)
        self.assertTrue(
            all(category_source.count("namespace generated_detail {") == 1
                for category_source in sources)
        )
        self.assertIn("struct AddResolvedDescriptorStorage {", source)
        self.assertIn("struct BarResolvedDescriptorStorage {", source)
        self.assertIn("check_end::ResolvedFieldDescriptor", source)
        self.assertNotIn("ResolvedConstantDescriptor", source)
        self.assertIn("check_end::ResolvedModifierBindingDescriptor", source)
        self.assertIn("check_end::ResolvedOperandBindingDescriptor", source)
        self.assertIn("checker::AddressStateSpaceDescriptor", source)
        self.assertIn(".vector_sink_payload_bits = 256,", source)
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
        self.assertIn(
            ".immediate_conversion_policy = "
            "check_end::ImmediateConversionPolicy::Narrow,",
            source,
        )
        self.assertIn(
            ".immediate_conversion_policy = "
            "check_end::ImmediateConversionPolicy::RequireTargetRange,",
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
        self.assertIn(
            ".kind = check_end::ResolvedModifierDefaultKind::EvictionPriority,",
            source,
        )
        self.assertIn(
            ".eviction_priority = EvictionPriority::Invalid,", source
        )
        self.assertIn(
            ".kind = check_end::ResolvedModifierDefaultKind::PrefetchSize,",
            source,
        )
        self.assertIn(".prefetch_size = PrefetchSize::None,", source)
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
        state_source = _emit_address_state_spaces((state_space,), BACKEND)
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
                parameter_binding.parameter_constraint, # pyright: ignore[reportArgumentType]
                function_availability=dnf,
            ),
        )
        parameter_source = _emit_operand_binding_descriptor(
            parameter_binding,
            "vector_arities",
            "address_state_spaces",
            BACKEND,
        )
        self.assertIn(".function_availability = {", parameter_source)
        self.assertIn(".any_of_count = 1", parameter_source)
        self.assertIn('.capabilities = {{"tensor"}}', parameter_source)

    def test_generate_private_checker_descriptor_source(self) -> None:
        database = self.database

        with tempfile.TemporaryDirectory() as directory:
            context = build_test_generation_context(database)
            sources = []
            for category in sorted({entry.specification.codegen_category for entry in context.entries}):
                output_path = Path(directory) / f"resolved_ir_checker_descriptor_{category}.gen.cpp"
                generate_resolved_checker_descriptor_source(context, category=category, output_path=output_path)
                sources.append(output_path.read_text(encoding="utf-8"))
            source = "\n".join(sources)

        self.assertIn('#include <ptx_frontend/resolved_ir/ptx_resolved_ir_foundation.hpp>', source)
        self.assertIn('#include <ptx_frontend/resolved_ir/model/arithmetic.gen.hpp>', source)
        self.assertTrue(
            all(category_source.count("namespace generated_detail {") == 1
                for category_source in sources)
        )
        self.assertIn("struct AddCheckerDescriptorStorage {", source)
        self.assertIn("struct BarCheckerDescriptorStorage {", source)
        self.assertIn("checker::VariantDescriptor", source)
        self.assertIn("checker::OperandLayoutDescriptor", source)
        self.assertIn("checker::OperandTypeCompatibilityDescriptor", source)
        self.assertIn(".memory_consistency = {", source)
        self.assertIn(".address_alignments =", source)
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
            generate_resolved_checker_descriptor_source(build_test_generation_context(database), category="test",
                output_path=output_path,
            )
            source = output_path.read_text(encoding="utf-8")

        self.assertIn("checker::ModifierValueAvailabilityDescriptor", source)
        self.assertIn('.kind_id = "type",', source)
        self.assertIn(".scalar_type = ScalarType::U32,", source)
        self.assertIn(".scalar_type = ScalarType::U64,", source)
        self.assertIn(".minimum_ptx_version = {2, 0},", source)

    def test_modifier_value_domain_includes_legal_values_and_optional_default(
        self,
    ) -> None:
        specs = normalize_instruction_spec(
            {
                "category": "test",
                "codegen_category": "test",
                "instructions": [
                    {
                        "opcode": "sample",
                        "variants": [
                            {
                                "name": "sample_rounding",
                                "availability": {"ptx": "1.0", "sm": 0},
                                "modifiers": [
                                    {
                                        "name": "rounding",
                                        "kind": "rounding",
                                        "presence": "optional",
                                        "default": "rn",
                                        "values": [
                                            "rn",
                                            {"value": "rz", "availability": {"sm": 20}},
                                        ],
                                    },
                                    {
                                        "name": "saturate",
                                        "kind": "flag",
                                        "presence": "optional",
                                        "default": False,
                                        "token": ".sat",
                                    },
                                    {
                                        "name": "cache",
                                        "kind": "cache",
                                        "presence": "optional",
                                        "default": "unspecified",
                                        "values": ["ca"],
                                    },
                                    {
                                        "name": "eviction_priority",
                                        "kind": "eviction_priority",
                                        "presence": "optional",
                                        "default": "invalid",
                                        "values": ["evict_first"],
                                    },
                                    {
                                        "name": "prefetch_size",
                                        "kind": "prefetch_size",
                                        "presence": "optional",
                                        "default": "none",
                                        "values": ["L2::64B"],
                                    },
                                    {
                                        "name": "required_static_flag",
                                        "kind": "flag",
                                        "presence": "fixed",
                                        "value": True,
                                        "token": ".fixed",
                                    },
                                ],
                                "operands": [],
                            }
                        ],
                    }
                ]
            }
        )
        resolved = from_instruction_spec(specs[0])
        variant = resolved.variants[0]
        self.assertEqual(
            [(entry.source_kind_id, entry.value) for entry in variant.modifier_value_domains],
            [
                ("rounding", "rn"),
                ("rounding", "rz"),
                ("saturate", True),
                ("saturate", False),
                ("cache", "ca"),
                ("cache", "unspecified"),
                ("eviction_priority", "evict_first"),
                ("eviction_priority", "invalid"),
                ("prefetch_size", "L2::64B"),
                ("prefetch_size", "none"),
                ("required_static_flag", True),
            ],
        )
        self.assertEqual(
            [entry.value for entry in variant.modifier_value_availabilities], ["rz"]
        )

        database = CodegenDatabase(spec_schema="ptx-instr/v1", instructions=specs)
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir_checker_descriptor.gen.cpp"
            checker_path = Path(directory) / "resolved_ir_test.gen.cpp"
            generate_resolved_checker_descriptor_source(build_test_generation_context(database), category="test",
                output_path=output_path,
            )
            generate_resolved_opcode_source(build_test_generation_context(database),
                category="test",
                opcode=specs[0].opcode,
                output_path=checker_path,
            )
            source = output_path.read_text(encoding="utf-8")
            checker_source = checker_path.read_text(encoding="utf-8")

        self.assertIn("checker::ModifierValueDomainDescriptor", source)
        self.assertIn("Rounding_modifier_value_domains", source)
        self.assertIn(".rounding_mode = RoundingMode::Rn,", source)
        self.assertIn(".rounding_mode = RoundingMode::Rz,", source)
        self.assertIn('.kind_id = "saturate",', source)
        self.assertIn(".bool_value = true,", source)
        self.assertIn(".bool_value = false,", source)
        self.assertIn('.kind_id = "cache",', source)
        self.assertIn(".cache_operator = CacheOperator::Unspecified,", source)
        self.assertIn(".eviction_priority = EvictionPriority::Invalid,", source)
        self.assertIn(".prefetch_size = PrefetchSize::None,", source)
        self.assertIn('.kind_id = "required_static_flag",', source)
        self.assertNotIn("RoundingMode::Rzi", source)
        self.assertIn(
            "!selected.prefetch_size.locs.empty() || "
            "selected.prefetch_size.value != PrefetchSize::None",
            checker_source,
        )
        self.assertIn(
            "!selected.eviction_priority.locs.empty() || "
            "selected.eviction_priority.value != EvictionPriority::Invalid",
            checker_source,
        )

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
        self.assertEqual(field_cpp_type(field), "WithLocs<ComparisonOperator>")
        self.assertEqual(
            resolved.variants[0].modifier_value_availabilities[0].value_kind.value,
            "ComparisonOperator",
        )

        database = CodegenDatabase(spec_schema="ptx-instr/v1", instructions=specs)
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir_checker_descriptor.gen.cpp"
            generate_resolved_checker_descriptor_source(build_test_generation_context(database), category="test",
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
            resolved.variants[0].modifier_value_availabilities[0].value_kind.value,
            "EvictionPriority",
        )

        database = CodegenDatabase(spec_schema="ptx-instr/v1", instructions=specs)
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir_checker_descriptor.gen.cpp"
            generate_resolved_checker_descriptor_source(build_test_generation_context(database), category="test",
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
        value["value"] = 0
        with self.assertRaisesRegex(ValueError, "eviction_priority"):
            from_instruction_spec(normalize_instruction_spec(spec)[0])

        # Unknown values fail at the frontend semantic boundary, before C++
        # generation can inspect a backend mapping.
        value["value"] = "not_a_priority"
        with self.assertRaisesRegex(ValueError, "eviction_priority"):
            normalize_instruction_spec(spec)

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
        self.assertEqual(field_cpp_type(field), "WithLocs<BooleanOperator>")
        self.assertEqual(
            resolved.variants[0].modifier_value_availabilities[0].value_kind.value,
            "BooleanOperator",
        )

        database = CodegenDatabase(spec_schema="ptx-instr/v1", instructions=specs)
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "resolved_ir_checker_descriptor.gen.cpp"
            generate_resolved_checker_descriptor_source(build_test_generation_context(database), category="test",
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
            generate_resolved_opcode_source(build_test_generation_context(self.database),
                category="arithmetic",
                opcode="add",
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
                            kind=ModifierKind.TYPE,
                            presence=ModifierPresence.REQUIRED,
                            values=(ModifierValueSpec(value="u32"),),
                        ),
                    ),
                    operand_layouts=(
                        OperandLayoutSpec(
                            name="binary",
                            operands=(
                                OperandSpec(
                                    name="dst",
                                    kind=OperandKind.REGISTER,
                                    role=OperandRole.DESTINATION,
                                    access=OperandAccess.WRITE,
                                    type_expression=OperandTypeExpression(
                                        kind=OperandTypeExpressionKind.MODIFIER,
                                        modifier_name="type",
                                    ),
                                ),
                                OperandSpec(
                                    name="src",
                                    kind=OperandKind.REGISTER_OR_IMMEDIATE,
                                    role=OperandRole.SOURCE,
                                    access=OperandAccess.READ,
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
                                    kind=OperandKind.REGISTER,
                                    role=OperandRole.DESTINATION,
                                    access=OperandAccess.WRITE,
                                    type_expression=OperandTypeExpression(
                                        kind=OperandTypeExpressionKind.MODIFIER,
                                        modifier_name="type",
                                    ),
                                ),
                                OperandSpec(
                                    name="src",
                                    kind=OperandKind.REGISTER_OR_IMMEDIATE,
                                    role=OperandRole.SOURCE_1,
                                    access=OperandAccess.READ,
                                    type_expression=OperandTypeExpression(
                                        kind=OperandTypeExpressionKind.MODIFIER,
                                        modifier_name="type",
                                    ),
                                ),
                                OperandSpec(
                                    name="src2",
                                    kind=OperandKind.REGISTER_OR_IMMEDIATE,
                                    role=OperandRole.SOURCE_2,
                                    access=OperandAccess.READ,
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
                    rule=SemanticRule.CONTROL_FLOW_BRA,
                ),
            ),
        )
        database = CodegenDatabase(spec_schema="ptx-instr/v1", instructions=(instruction,))

        with tempfile.TemporaryDirectory() as directory:
            header_path = Path(directory) / "uncategorized.gen.hpp"
            source_path = Path(directory) / "resolved_ir_uncategorized.gen.cpp"
            generate_resolved_opcode_header(
                build_test_generation_context(database),
                category="uncategorized", opcode="sample", output_path=header_path,
            )
            generate_resolved_opcode_source(build_test_generation_context(database),
                category="uncategorized",
                opcode="sample",
                output_path=source_path,
            )
            header = header_path.read_text(encoding="utf-8")
            source = source_path.read_text(encoding="utf-8")

        # The historical test name tracks the two-layout fixture; the active
        # representation uses one final class with typed optional members.
        self.assertIn("class SampleTyped final : public Instruction", header)
        self.assertIn("WithLocs<ResolvedRegisterRef> dst;", header)
        self.assertIn("WithLocs<RegOrImm> src;", header)
        self.assertIn("std::optional<WithLocs<RegOrImm>> src2;", header)
        self.assertIn("ResolvedOperandLayoutTag operand_layout;", header)
        self.assertNotIn("struct BinaryOperands", header)
        self.assertNotIn("using Operands = std::variant<", header)
        self.assertIn("SampleTyped::check(", source)
        self.assertIn("resolveSample(", source)
        self.assertIn("selected.src2.has_value()", source)
        self.assertLess(
            source.index("check_operand_layout_tag("),
            source.index("selected.src2.has_value()"),
        )
        calls = (
            "check_immediate_value(",
            "check_immediate_range(",
            "check_immediate_multiple_of(",
        )
        self.assertEqual([source.count(call) for call in calls], [2, 2, 2])
        first_check = source.index(calls[0])
        self.assertEqual(
            [source.index(call, first_check) for call in calls],
            sorted(source.index(call, first_check) for call in calls),
        )

    def test_modifier_value_descriptor_uses_traits_mapping(self) -> None:
        from ptx_frontend.ir.resolved_ir import ResolvedModifierValueDomain
        from ptx_frontend.code_gen.emit.checker_descriptors import _emit_modifier_value_domain_descriptor
        entry = ResolvedModifierValueDomain(
            source_kind_id="type",
            value_kind=ResolvedValueKind.SCALAR_TYPE,
            value="f32",
        )

        emitted = _emit_modifier_value_domain_descriptor(entry, backend=BACKEND)

        self.assertIn(
            ".value_kind = checker::ModifierValueKind::ScalarType",
            emitted,
        )
        self.assertIn(
            ".scalar_type = ScalarType::F32",
            emitted,
        )
        self.assertIn(
            ".bool_value = false",
            emitted,
        )
        self.assertIn(
            ".rounding_mode = RoundingMode::Invalid",
            emitted,
        )

    def test_rounding_modifier_value_rejects_unknown_semantic_value(self) -> None:
        modifier = ModifierSpec(
            name="rounding",
            kind=ModifierKind.ROUNDING,
            presence=ModifierPresence.REQUIRED,
        )

        value = ModifierValueSpec(
            value="not_a_rounding_mode",
        )

        with self.assertRaisesRegex(ValueError, "unsupported semantic rounding_mode"):
            _build_modifier_value_availability(modifier, value)

    def test_resolved_ir_builds_without_a_cpp_backend(self) -> None:
        """Semantic normalization does not require C++ domain mappings."""

        resolved = from_instruction_spec(self.database.instructions[0])

        self.assertEqual(resolved.opcode, self.database.instructions[0].opcode)

    def test_operand_view_dispatch_ignores_backend_value_cpp_type_spelling(self) -> None:
        """Checker-view selection follows the semantic value kind and origin."""

        from ptx_frontend.code_gen.emit.operand_views import (
            emit_check_operand_view,
        )
        from ptx_frontend.ir.resolved_ir import ResolvedField

        raw = yaml.safe_load(
            (REPO_ROOT / "instructions/ptx_cpp_backend_spec/ptx_frontend.yaml").read_text(
                encoding="utf-8"
            )
        )
        raw["domains"]["resolved_value_cpp_types"]["values"]["RegisterVector"] = (
            "BackendRenamedVector"
        )
        with tempfile.TemporaryDirectory() as directory:
            backend_path = Path(directory) / "backend.yaml"
            backend_path.write_text(yaml.safe_dump(raw, sort_keys=False), encoding="utf-8")
            backend = load_cpp_backend(backend_path)
            emitted = emit_check_operand_view(
                ResolvedField(
                    name="vector",
                    value_kind=ResolvedValueKind.REGISTER_VECTOR,
                    origin=ResolvedFieldOrigin.OPERAND,
                    source_name="vector",
                ),
                "instruction",
                backend,
            )

        self.assertIn(".vector_arity = instruction.vector.value.elements.size()", emitted)

    def test_rounding_modifier_value_accepts_known_backend_value(self) -> None:
        modifier = ModifierSpec(
            name="rounding",
            kind=ModifierKind.ROUNDING,
            presence=ModifierPresence.REQUIRED,
        )

        value = ModifierValueSpec(
            value="rn",
        )

        resolved = _build_modifier_value_availability(
            modifier,
            value,
        )

        self.assertIs(
            resolved.value_kind,
            ResolvedValueKind.ROUNDING_MODE,
        )
        self.assertEqual(
            resolved.value,
            "rn",
        )

    def test_direct_ir_construction_checks_frontend_scalar_and_state_domains(self) -> None:
        """Direct typed-model callers cannot bypass frontend domain legality."""

        from ptx_frontend.ir.resolved_ir import (
            _resolve_operand_state_spaces,
            _resolve_operand_type_expression,
        )
        from ptx_frontend.spec.model import OperandStateSpaceValue

        with self.assertRaisesRegex(ValueError, "operand scalar type"):
            _resolve_operand_type_expression(
                OperandTypeExpression(
                    kind=OperandTypeExpressionKind.FIXED_SCALAR,
                    scalar_type="not_a_ptx_type",
                ),
                {},
            )
        with self.assertRaisesRegex(ValueError, "operand state space"):
            _resolve_operand_state_spaces(
                (OperandStateSpaceValue(value="not_a_state_space"),)
            )
        invalid_fixed = InstructionSpec(
            opcode="invalid_fixed",
            variants=(
                VariantSpec(
                    name="invalid_fixed_default",
                    availability={"ptx": "1.0"},
                    modifiers=(
                        ModifierSpec(
                            name="type",
                            kind=ModifierKind.TYPE,
                            presence=ModifierPresence.FIXED,
                            values=(ModifierValueSpec(value="u32"),),
                            value="not_a_ptx_type",
                        ),
                    ),
                    operand_layouts=(OperandLayoutSpec(name="default", operands=()),),
                ),
            ),
        )
        with self.assertRaisesRegex(ValueError, "scalar_type"):
            from_instruction_spec(invalid_fixed)
        with self.assertRaisesRegex(ValueError, "outside its allowed values"):
            from ptx_frontend.ir.resolved_ir import _build_modifier_default

            _build_modifier_default(
                ModifierSpec(
                    name="rounding",
                    kind=ModifierKind.ROUNDING,
                    presence=ModifierPresence.OPTIONAL,
                    values=(ModifierValueSpec(value="rn"),),
                    default="rz",
                )
            )

    def test_direct_ir_construction_rejects_untyped_semantic_rules(self) -> None:
        """Prevent manually constructed specs from bypassing rule normalization."""

        cvt = next(
            instruction
            for instruction in self.database.instructions
            if instruction.opcode == "cvt"
        )
        malformed = replace(
            cvt,
            variants=(
                replace(cvt.variants[0], rule="data_movement.cvt"),
                *cvt.variants[1:],
            ),
        )
        with self.assertRaisesRegex(ValueError, "non-normalized semantic rule"):
            from_instruction_spec(malformed)

    def test_ir_import_and_construction_do_not_load_codegen(self) -> None:
        """A clean process can normalize and lower IR while codegen is blocked."""

        source = '''
import importlib.abc
import sys

class BlockCodegen(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        if fullname == "ptx_frontend.code_gen" or fullname.startswith("ptx_frontend.code_gen."):
            raise ImportError("code generation is blocked")
        return None

sys.meta_path.insert(0, BlockCodegen())
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.ir.resolved_ir import from_instruction_spec

instruction = normalize_instruction_spec({
    "category": "test", "codegen_category": "test",
    "instructions": [{"opcode": "sample", "variants": [{
        "name": "sample_default", "availability": {"ptx": "1.0"},
        "modifiers": [{"name": "type", "kind": "type", "presence": "fixed", "value": "u32"}],
        "operands": [],
    }]}],
})[0]
assert from_instruction_spec(instruction).opcode == "sample"
'''
        environment = {**os.environ, "PYTHONPATH": str(REPO_ROOT / "python" / "src")}
        result = subprocess.run(
            [sys.executable, "-c", source],
            capture_output=True,
            text=True,
            env=environment,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_resolved_field_keeps_no_cpp_representation_properties(self) -> None:
        """C++ storage spelling is a code-generation projection, never IR state."""

        from ptx_frontend.ir.resolved_ir import ResolvedField

        for name in ("value_cpp_type", "cpp_type", "cpp_constant_expr"):
            with self.subTest(name=name):
                self.assertFalse(hasattr(ResolvedField, name))

if __name__ == "__main__":
    unittest.main()
