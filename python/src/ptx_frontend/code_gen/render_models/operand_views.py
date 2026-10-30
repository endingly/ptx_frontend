"""Typed models and builders for checker operand and modifier projections.

Expression fields are final C++ names, member accesses, or backend constants.
Diagnostic identities remain independent of payload aliases; callers guard
optional payload expressions before the resulting view is evaluated. Structure
tags select fixed template shapes and carry no further ISA decisions.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Literal

from ptx_frontend.code_gen.cpp_backend import CppDomain, cpp_default, cpp_value
from ptx_frontend.code_gen.resolved_value_traits import modifier_default_cpp_expr, modifier_descriptor_members
from ptx_frontend.ir.resolved_ir import (
    ResolvedField, ResolvedFieldOrigin, ResolvedFieldStorage,
    ResolvedModifierDefault, ResolvedVariant, ResolvedValueKind,
)
from ptx_frontend.spec.model import CodegenUnit



@dataclass(frozen=True)
class ModifierViewModel:
    """Final expressions for the modifier field view."""

    field_id: str
    bool_value: str
    cache_operator: str
    eviction_priority: str
    prefetch_size: str
    scalar_type: str
    comparison_operator: str
    test_property: str
    boolean_operator: str
    vector_arity: str
    memory_state_space: str
    memory_consistency: str
    memory_scope: str
    mbarrier_phase_type: str
    mbarrier_layout: str
    cta_group: str
    tcgen_scale_vector_size: str
    tcgen_collector: str
    tcgen_shape: str
    tcgen_num: str
    tcgen_red_op: str
    tcgen_wait: str
    video_scale: str
    video_shift_mode: str
    video_secondary_op: str
    video_type: str
    async_proxy_kind: str
    proxy_kind_pair: str
    locations: str
    structure: Literal["modifier_view"] = "modifier_view"



@dataclass(frozen=True)
class ModifierValueViewModel:
    """Final expressions for the modifier value view."""

    kind_id: str
    value_kind: str
    bool_value: str
    scalar_type: str
    rounding_mode: str
    comparison_operator: str
    test_property: str
    boolean_operator: str
    cache_operator: str
    eviction_priority: str
    prefetch_size: str
    vector_arity: str
    memory_state_space: str
    memory_consistency: str
    memory_scope: str
    mbarrier_phase_type: str
    mbarrier_layout: str
    cta_group: str
    tcgen_scale_vector_size: str
    tcgen_collector: str
    tcgen_shape: str
    tcgen_num: str
    tcgen_red_op: str
    tcgen_wait: str
    video_scale: str
    video_shift_mode: str
    video_secondary_op: str
    video_type: str
    async_proxy_kind: str
    proxy_kind_pair: str
    is_present: str
    locations: str
    slot_index: int
    structure: Literal["modifier_value_view"] = "modifier_value_view"



@dataclass(frozen=True)
class OperandDirectCallTargetViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    directcalltarget_cpp: str
    member_expr: str
    structure: Literal["operand_view_direct_call_target"] = "operand_view_direct_call_target"


@dataclass(frozen=True)
class OperandIndirectCalleeViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    indirectcallee_cpp: str
    member_expr: str
    structure: Literal["operand_view_indirect_callee"] = "operand_view_indirect_callee"


@dataclass(frozen=True)
class OperandCallReturnParameterViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    callreturnparameter_cpp: str
    member_expr: str
    structure: Literal["operand_view_call_return_parameter"] = "operand_view_call_return_parameter"


@dataclass(frozen=True)
class OperandCallArgumentsViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    callarguments_cpp: str
    member_expr: str
    structure: Literal["operand_view_call_arguments"] = "operand_view_call_arguments"


@dataclass(frozen=True)
class OperandRegisterVectorViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    vector_cpp: str
    member_expr: str
    register_cpp: str
    scalar_types_default: str
    structure: Literal["operand_view_register_vector"] = "operand_view_register_vector"


@dataclass(frozen=True)
class OperandMovVectorSourceViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    member_expr: str
    structure: Literal["operand_view_mov_vector_source"] = "operand_view_mov_vector_source"


@dataclass(frozen=True)
class OperandValueVectorViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    vector_cpp: str
    member_expr: str
    register_cpp: str
    scalar_types_default: str
    immediate_cpp: str
    structure: Literal["operand_view_value_vector"] = "operand_view_value_vector"


@dataclass(frozen=True)
class OperandTensorCoordinateViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    vector_cpp: str
    member_expr: str
    register_cpp: str
    scalar_types_default: str
    immediate_cpp: str
    structure: Literal["operand_view_tensor_coordinate"] = "operand_view_tensor_coordinate"


@dataclass(frozen=True)
class OperandTensorIm2ColInfoViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    member_expr: str
    structure: Literal["operand_view_tensor_im2col_info"] = "operand_view_tensor_im2col_info"


@dataclass(frozen=True)
class OperandTensorOperandViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    member_expr: str
    structure: Literal["operand_view_tensor_operand"] = "operand_view_tensor_operand"


@dataclass(frozen=True)
class OperandFabricHandleViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    fabrichandle_cpp: str
    member_expr: str
    structure: Literal["operand_view_fabric_handle"] = "operand_view_fabric_handle"


@dataclass(frozen=True)
class OperandVideoOperandViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    videooperand_cpp: str
    member_expr: str
    structure: Literal["operand_view_video_operand"] = "operand_view_video_operand"


@dataclass(frozen=True)
class OperandResourceViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    shape_cpp: str
    member_expr: str
    structure: Literal["operand_view_resource"] = "operand_view_resource"


@dataclass(frozen=True)
class OperandTextureResultViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    textureresult_cpp: str
    member_expr: str
    structure: Literal["operand_view_texture_result"] = "operand_view_texture_result"


@dataclass(frozen=True)
class OperandMatrixScaleSelectorViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    vector_cpp: str
    member_expr: str
    register_cpp: str
    scalar_types_default: str
    immediate_cpp: str
    structure: Literal["operand_view_matrix_scale_selector"] = "operand_view_matrix_scale_selector"


@dataclass(frozen=True)
class OperandVectorRegisterViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    member_expr: str
    field_id: str
    vector_cpp: str
    scalar_types_default: str
    structure: Literal["operand_view_vector_register"] = "operand_view_vector_register"


@dataclass(frozen=True)
class OperandVectorSpecialRegisterViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    member_expr: str
    field_id: str
    vector_cpp: str
    structure: Literal["operand_view_vector_special_register"] = "operand_view_vector_special_register"


@dataclass(frozen=True)
class OperandStackTokenViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    shape: str
    member_expr: str
    structure: Literal["operand_view_stack_token"] = "operand_view_stack_token"


@dataclass(frozen=True)
class OperandSharedMatrixDescriptorViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    register_cpp: str
    member_expr: str
    structure: Literal["operand_view_shared_matrix_descriptor"] = "operand_view_shared_matrix_descriptor"


@dataclass(frozen=True)
class OperandRegisterViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    register_cpp: str
    member_expr: str
    structure: Literal["operand_view_register"] = "operand_view_register"


@dataclass(frozen=True)
class OperandMbarrierStateTokenViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    register_cpp: str
    member_expr: str
    structure: Literal["operand_view_mbarrier_state_token"] = "operand_view_mbarrier_state_token"


@dataclass(frozen=True)
class OperandRegisterOrSinkViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    register_cpp: str
    member_expr: str
    structure: Literal["operand_view_register_or_sink"] = "operand_view_register_or_sink"


@dataclass(frozen=True)
class OperandShflDestinationViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    shfldestination_cpp: str
    member_expr: str
    structure: Literal["operand_view_shfl_destination"] = "operand_view_shfl_destination"


@dataclass(frozen=True)
class OperandPredicatePairViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    predicatepair_cpp: str
    member_expr: str
    scalar_types_default: str
    structure: Literal["operand_view_predicate_pair"] = "operand_view_predicate_pair"


@dataclass(frozen=True)
class OperandPredicatePairOrSinkViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    predicatepair_cpp: str
    member_expr: str
    scalar_types_default: str
    structure: Literal["operand_view_predicate_pair_or_sink"] = "operand_view_predicate_pair_or_sink"


@dataclass(frozen=True)
class OperandImmediateViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    immediate_cpp: str
    member_expr: str
    structure: Literal["operand_view_immediate"] = "operand_view_immediate"


@dataclass(frozen=True)
class OperandPredicateViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    predicate_cpp: str
    member_expr: str
    structure: Literal["operand_view_predicate"] = "operand_view_predicate"


@dataclass(frozen=True)
class OperandPredicateOrSinkViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    predicate_cpp: str
    member_expr: str
    structure: Literal["operand_view_predicate_or_sink"] = "operand_view_predicate_or_sink"


@dataclass(frozen=True)
class OperandPredicateSourceViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    member_expr: str
    field_id: str
    specialregister_cpp: str
    immediate_cpp: str
    pred_cpp: str
    predicate_cpp: str
    structure: Literal["operand_view_predicate_source"] = "operand_view_predicate_source"


@dataclass(frozen=True)
class OperandBranchTargetViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    branchtarget_cpp: str
    member_expr: str
    structure: Literal["operand_view_branch_target"] = "operand_view_branch_target"


@dataclass(frozen=True)
class OperandBranchTargetSetViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    branchtargetset_cpp: str
    member_expr: str
    structure: Literal["operand_view_branch_target_set"] = "operand_view_branch_target_set"


@dataclass(frozen=True)
class OperandSpecialRegisterViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    member_expr: str
    field_id: str
    specialregister_cpp: str
    structure: Literal["operand_view_special_register"] = "operand_view_special_register"


@dataclass(frozen=True)
class OperandSymbolViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    symbol_cpp: str
    member_expr: str
    structure: Literal["operand_view_symbol"] = "operand_view_symbol"


@dataclass(frozen=True)
class OperandAddressViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    member_expr: str
    parameter_directions_default: str
    global_cpp: str
    shared_cpp: str
    local_cpp: str
    param_cpp: str
    const_cpp: str
    input_cpp: str
    return_cpp: str
    field_id: str
    address_cpp: str
    structure: Literal["operand_view_address"] = "operand_view_address"


@dataclass(frozen=True)
class OperandRegisterOrImmediateViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    source: str
    field_id: str
    immediate_cpp: str
    member_expr: str
    register_cpp: str
    structure: Literal["operand_view_reg_or_imm"] = "operand_view_reg_or_imm"


@dataclass(frozen=True)
class OperandTcgenHalfSplitOffsetViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    field_id: str
    immediate_cpp: str
    member_expr: str
    structure: Literal["operand_view_tcgen_half_split_offset"] = "operand_view_tcgen_half_split_offset"


@dataclass(frozen=True)
class OperandCpAsyncSourceControlViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    member_expr: str
    field_id: str
    immediate_cpp: str
    predicate_cpp: str
    register_cpp: str
    structure: Literal["operand_view_cp_async_source_control"] = "operand_view_cp_async_source_control"


@dataclass(frozen=True)
class OperandCvtSourceViewModel:
    """Conversion carrier projected by the typed checker helper."""

    member_expr: str
    field_id: str
    structure: Literal["operand_view_cvt_source"] = "operand_view_cvt_source"


@dataclass(frozen=True)
class OperandMovSourceViewModel:
    """Final field identity, payload expression, and backend enum constants."""

    global_cpp: str
    shared_cpp: str
    local_cpp: str
    param_cpp: str
    const_cpp: str
    input_cpp: str
    return_cpp: str
    member_expr: str
    field_id: str
    immediate_cpp: str
    register_cpp: str
    specialregister_cpp: str
    symbol_cpp: str
    address_cpp: str
    structure: Literal["operand_view_mov_source"] = "operand_view_mov_source"

OperandViewModel = (
    OperandDirectCallTargetViewModel
    | OperandIndirectCalleeViewModel
    | OperandCallReturnParameterViewModel
    | OperandCallArgumentsViewModel
    | OperandRegisterVectorViewModel
    | OperandMovVectorSourceViewModel
    | OperandValueVectorViewModel
    | OperandTensorCoordinateViewModel
    | OperandTensorIm2ColInfoViewModel
    | OperandTensorOperandViewModel
    | OperandFabricHandleViewModel
    | OperandVideoOperandViewModel
    | OperandResourceViewModel
    | OperandTextureResultViewModel
    | OperandMatrixScaleSelectorViewModel
    | OperandVectorRegisterViewModel
    | OperandVectorSpecialRegisterViewModel
    | OperandStackTokenViewModel
    | OperandSharedMatrixDescriptorViewModel
    | OperandRegisterViewModel
    | OperandMbarrierStateTokenViewModel
    | OperandRegisterOrSinkViewModel
    | OperandShflDestinationViewModel
    | OperandPredicatePairViewModel
    | OperandPredicatePairOrSinkViewModel
    | OperandImmediateViewModel
    | OperandPredicateViewModel
    | OperandPredicateOrSinkViewModel
    | OperandPredicateSourceViewModel
    | OperandBranchTargetViewModel
    | OperandBranchTargetSetViewModel
    | OperandSpecialRegisterViewModel
    | OperandSymbolViewModel
    | OperandAddressViewModel
    | OperandRegisterOrImmediateViewModel
    | OperandTcgenHalfSplitOffsetViewModel
    | OperandCpAsyncSourceControlViewModel
    | OperandCvtSourceViewModel
    | OperandMovSourceViewModel
)


def _cpp(backend: CodegenUnit, domain: CppDomain, value: str) -> str:
    """Map an emitted semantic operand value through this run's backend."""

    return cpp_value(domain, value, backend=backend)


def _cpp_default(backend: CodegenUnit, domain: CppDomain) -> str:
    """Return an emitted C++ default from this run's semantic domain."""

    return cpp_default(domain, backend=backend)

def build_check_modifier_view(
    field: ResolvedField,
    backend: CodegenUnit,
    *, form_cpp_name: str, member_expr: str,
) -> ModifierViewModel:
    """Build a field view using the exact class and guarded instance member."""

    if field.storage is ResolvedFieldStorage.STATIC_CONSTANT:
        value_expr = f"{form_cpp_name}::{field.name}"
        locations = "std::span<const SourceRange>{}"
    else:
        value_expr = f"{member_expr}.value"
        locations = f"{member_expr}.locs"

    members = modifier_descriptor_members(
        field.value_kind,
        value_expr,
        unselected_value_expr="std::nullopt",
        backend=backend,
    )
    return ModifierViewModel(
        field_id=field.name,
        bool_value=members[ResolvedValueKind.BOOL],
        cache_operator=members[ResolvedValueKind.CACHE_OPERATOR],
        eviction_priority=members[ResolvedValueKind.EVICTION_PRIORITY],
        prefetch_size=members[ResolvedValueKind.PREFETCH_SIZE],
        scalar_type=members[ResolvedValueKind.SCALAR_TYPE],
        comparison_operator=members[ResolvedValueKind.COMPARISON_OPERATOR],
        test_property=members[ResolvedValueKind.TEST_PROPERTY],
        boolean_operator=members[ResolvedValueKind.BOOLEAN_OPERATOR],
        vector_arity=members[ResolvedValueKind.VECTOR_ARITY],
        memory_state_space=members[ResolvedValueKind.MEMORY_STATE_SPACE],
        memory_consistency=members[ResolvedValueKind.MEMORY_CONSISTENCY],
        memory_scope=members[ResolvedValueKind.MEMORY_SCOPE],
        mbarrier_phase_type=members[ResolvedValueKind.MBARRIER_PHASE_TYPE],
        mbarrier_layout=members[ResolvedValueKind.MBARRIER_LAYOUT],
        cta_group=members[ResolvedValueKind.CTA_GROUP],
        tcgen_scale_vector_size=members[ResolvedValueKind.TCGEN_SCALE_VECTOR_SIZE],
        tcgen_collector=members[ResolvedValueKind.TCGEN_COLLECTOR],
        tcgen_shape=members[ResolvedValueKind.TCGEN_SHAPE],
        tcgen_num=members[ResolvedValueKind.TCGEN_NUM],
        tcgen_red_op=members[ResolvedValueKind.TCGEN_RED_OP],
        tcgen_wait=members[ResolvedValueKind.TCGEN_WAIT],
        video_scale=members[ResolvedValueKind.VIDEO_SCALE],
        video_shift_mode=members[ResolvedValueKind.VIDEO_SHIFT_MODE],
        video_secondary_op=members[ResolvedValueKind.VIDEO_SECONDARY_OP],
        video_type=members[ResolvedValueKind.VIDEO_TYPE],
        async_proxy_kind=members[ResolvedValueKind.ASYNC_PROXY_KIND],
        proxy_kind_pair=members[ResolvedValueKind.PROXY_KIND_PAIR],
        locations=locations,
    )


def _modifier_default_cpp_value(
    default: ResolvedModifierDefault,
    backend: CodegenUnit,
) -> str:
    """Return the normalized C++ expression for an optional modifier default."""

    return modifier_default_cpp_expr(
        default.value_kind,
        default.value, backend=backend,
    )


def build_check_modifier_value_view(
    variant: ResolvedVariant,
    field: ResolvedField,
    backend: CodegenUnit,
    slot_index: int,
    *, form_cpp_name: str, member_expr: str,
) -> ModifierValueViewModel:
    """Build a modifier value with exact class and guarded member expressions."""

    # The variant-local slot index is the typed identity layout constraints
    # compare against; the source name stays for diagnostics.

    value_expr = (
        f"{form_cpp_name}::{field.name}"
        if field.storage is ResolvedFieldStorage.STATIC_CONSTANT
        else f"{member_expr}.value"
    )

    members = modifier_descriptor_members(
        field.value_kind,
        value_expr,
        backend=backend,
    )

    locations = (
        "std::span<const SourceRange>{}"
        if field.storage is ResolvedFieldStorage.STATIC_CONSTANT
        else f"{member_expr}.locs"
    )

    if field.storage is ResolvedFieldStorage.STATIC_CONSTANT:
        is_present = "true"
    else:
        is_present = f"!{member_expr}.locs.empty()"

        binding = next(
            binding
            for binding in variant.modifier_bindings
            if binding.target_field_id == field.name
        )

        if binding.default_value is not None:
            default_value = _modifier_default_cpp_value(binding.default_value, backend)
            is_present += f" || {member_expr}.value != {default_value}"

    value_kind = _cpp(backend,
        CppDomain.CHECKER_MODIFIER_VALUE_KINDS,
        field.value_kind.value,
    )

    return ModifierValueViewModel(
        kind_id=field.source_name,
        value_kind=value_kind,
        bool_value=members[ResolvedValueKind.BOOL],
        scalar_type=members[ResolvedValueKind.SCALAR_TYPE],
        rounding_mode=members[ResolvedValueKind.ROUNDING_MODE],
        comparison_operator=members[ResolvedValueKind.COMPARISON_OPERATOR],
        test_property=members[ResolvedValueKind.TEST_PROPERTY],
        boolean_operator=members[ResolvedValueKind.BOOLEAN_OPERATOR],
        cache_operator=members[ResolvedValueKind.CACHE_OPERATOR],
        eviction_priority=members[ResolvedValueKind.EVICTION_PRIORITY],
        prefetch_size=members[ResolvedValueKind.PREFETCH_SIZE],
        vector_arity=members[ResolvedValueKind.VECTOR_ARITY],
        memory_state_space=members[ResolvedValueKind.MEMORY_STATE_SPACE],
        memory_consistency=members[ResolvedValueKind.MEMORY_CONSISTENCY],
        memory_scope=members[ResolvedValueKind.MEMORY_SCOPE],
        mbarrier_phase_type=members[ResolvedValueKind.MBARRIER_PHASE_TYPE],
        mbarrier_layout=members[ResolvedValueKind.MBARRIER_LAYOUT],
        cta_group=members[ResolvedValueKind.CTA_GROUP],
        tcgen_scale_vector_size=members[ResolvedValueKind.TCGEN_SCALE_VECTOR_SIZE],
        tcgen_collector=members[ResolvedValueKind.TCGEN_COLLECTOR],
        tcgen_shape=members[ResolvedValueKind.TCGEN_SHAPE],
        tcgen_num=members[ResolvedValueKind.TCGEN_NUM],
        tcgen_red_op=members[ResolvedValueKind.TCGEN_RED_OP],
        tcgen_wait=members[ResolvedValueKind.TCGEN_WAIT],
        video_scale=members[ResolvedValueKind.VIDEO_SCALE],
        video_shift_mode=members[ResolvedValueKind.VIDEO_SHIFT_MODE],
        video_secondary_op=members[ResolvedValueKind.VIDEO_SECONDARY_OP],
        video_type=members[ResolvedValueKind.VIDEO_TYPE],
        async_proxy_kind=members[ResolvedValueKind.ASYNC_PROXY_KIND],
        proxy_kind_pair=members[ResolvedValueKind.PROXY_KIND_PAIR],
        is_present=is_present,
        locations=locations,
        slot_index=slot_index,
    )


def build_check_operand_view(
    field: ResolvedField, member_expr: str, backend: CodegenUnit
) -> OperandViewModel:
    """Build a semantic operand view from its final payload member expression.

    The caller guards any optional dereference in ``member_expr``. Diagnostic
    field identity continues to come from metadata, independently of that name.
    """

    if field.origin is not ResolvedFieldOrigin.OPERAND:
        raise ValueError(f"field {field.name!r} is not an operand field")

    if field.value_kind is ResolvedValueKind.DIRECT_CALL_TARGET:
        return OperandDirectCallTargetViewModel(
            field_id=field.name,
            directcalltarget_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'DirectCallTarget'),
            member_expr=member_expr,
        )
    if field.value_kind is ResolvedValueKind.INDIRECT_CALLEE:
        return OperandIndirectCalleeViewModel(
            field_id=field.name,
            indirectcallee_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'IndirectCallee'),
            member_expr=member_expr,
        )
    if field.value_kind is ResolvedValueKind.CALL_RETURN_PARAMETER:
        return OperandCallReturnParameterViewModel(
            field_id=field.name,
            callreturnparameter_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'CallReturnParameter'),
            member_expr=member_expr,
        )
    if field.value_kind is ResolvedValueKind.CALL_ARGUMENTS:
        return OperandCallArgumentsViewModel(
            field_id=field.name,
            callarguments_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'CallArguments'),
            member_expr=member_expr,
        )
    if field.value_kind is ResolvedValueKind.REGISTER_VECTOR:
        return OperandRegisterVectorViewModel(
            field_id=field.name,
            vector_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Vector'),
            member_expr=member_expr,
            register_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Register'),
            scalar_types_default=_cpp_default(backend, CppDomain.SCALAR_TYPES),
        )
    if field.value_kind is ResolvedValueKind.MOV_VECTOR_SOURCE:
        return OperandMovVectorSourceViewModel(field_id=field.name, member_expr=member_expr)
    if field.value_kind is ResolvedValueKind.VALUE_VECTOR:
        return OperandValueVectorViewModel(
            field_id=field.name,
            vector_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Vector'),
            member_expr=member_expr,
            register_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Register'),
            scalar_types_default=_cpp_default(backend, CppDomain.SCALAR_TYPES),
            immediate_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Immediate'),
        )
    if field.value_kind is ResolvedValueKind.TENSOR_COORDINATE:
        return OperandTensorCoordinateViewModel(
            field_id=field.name,
            vector_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Vector'),
            member_expr=member_expr,
            register_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Register'),
            scalar_types_default=_cpp_default(backend, CppDomain.SCALAR_TYPES),
            immediate_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Immediate'),
        )
    if field.value_kind is ResolvedValueKind.TENSOR_IM2COL_INFO:
        return OperandTensorIm2ColInfoViewModel(field_id=field.name, member_expr=member_expr)
    if field.value_kind is ResolvedValueKind.TENSOR_OPERAND:
        return OperandTensorOperandViewModel(field_id=field.name, member_expr=member_expr)
    if field.value_kind is ResolvedValueKind.FABRIC_HANDLE:
        return OperandFabricHandleViewModel(
            field_id=field.name,
            fabrichandle_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'FabricHandle'),
            member_expr=member_expr,
        )
    if field.value_kind is ResolvedValueKind.VIDEO_OPERAND:
        return OperandVideoOperandViewModel(
            field_id=field.name,
            videooperand_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'VideoOperand'),
            member_expr=member_expr,
        )
    if field.value_kind in {ResolvedValueKind.SURFACE_ACCESS,
                            ResolvedValueKind.SURFACE_QUERY_RESOURCE,
                            ResolvedValueKind.TEXTURE_ACCESS,
                            ResolvedValueKind.TEXTURE_QUERY_RESOURCE}:
        shape = field.value_kind.value
        return OperandResourceViewModel(
            field_id=field.name,
            shape_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, shape),
            member_expr=member_expr,
        )
    if field.value_kind is ResolvedValueKind.TEXTURE_RESULT:
        return OperandTextureResultViewModel(
            field_id=field.name,
            textureresult_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'TextureResult'),
            member_expr=member_expr,
        )
    if field.value_kind is ResolvedValueKind.MATRIX_SCALE_SELECTOR:
        return OperandMatrixScaleSelectorViewModel(
            field_id=field.name,
            vector_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Vector'),
            member_expr=member_expr,
            register_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Register'),
            scalar_types_default=_cpp_default(backend, CppDomain.SCALAR_TYPES),
            immediate_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Immediate'),
        )
    if field.value_kind is ResolvedValueKind.VECTOR_REGISTER:
        return OperandVectorRegisterViewModel(
            member_expr=member_expr,
            field_id=field.name,
            vector_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Vector'),
            scalar_types_default=_cpp_default(backend, CppDomain.SCALAR_TYPES),
        )
    if field.value_kind is ResolvedValueKind.VECTOR_SPECIAL_REGISTER:
        return OperandVectorSpecialRegisterViewModel(
            member_expr=member_expr,
            field_id=field.name,
            vector_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Vector'),
        )
    if field.value_kind in {ResolvedValueKind.STACK_TOKEN,
                            ResolvedValueKind.LOCAL_ALLOCATION_RESULT}:
        shape = _cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, field.value_kind.value)
        return OperandStackTokenViewModel(
            field_id=field.name,
            shape=shape,
            member_expr=member_expr,
        )
    if field.value_kind is ResolvedValueKind.SHARED_MATRIX_DESCRIPTOR:
        return OperandSharedMatrixDescriptorViewModel(
            field_id=field.name,
            register_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Register'),
            member_expr=member_expr,
        )
    if field.value_kind is ResolvedValueKind.REGISTER:
        return OperandRegisterViewModel(
            field_id=field.name,
            register_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Register'),
            member_expr=member_expr,
        )
    if field.value_kind is ResolvedValueKind.MBARRIER_STATE_TOKEN:
        return OperandMbarrierStateTokenViewModel(
            field_id=field.name,
            register_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Register'),
            member_expr=member_expr,
        )
    if field.value_kind is ResolvedValueKind.REGISTER_OR_SINK:
        return OperandRegisterOrSinkViewModel(
            field_id=field.name,
            register_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Register'),
            member_expr=member_expr,
        )
    if field.value_kind is ResolvedValueKind.SHFL_DESTINATION:
        return OperandShflDestinationViewModel(
            field_id=field.name,
            shfldestination_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'ShflDestination'),
            member_expr=member_expr,
        )
    if field.value_kind is ResolvedValueKind.PREDICATE_PAIR:
        return OperandPredicatePairViewModel(
            field_id=field.name,
            predicatepair_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'PredicatePair'),
            member_expr=member_expr,
            scalar_types_default=_cpp_default(backend, CppDomain.SCALAR_TYPES),
        )
    if field.value_kind is ResolvedValueKind.PREDICATE_PAIR_OR_SINK:
        return OperandPredicatePairOrSinkViewModel(
            field_id=field.name,
            predicatepair_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'PredicatePair'),
            member_expr=member_expr,
            scalar_types_default=_cpp_default(backend, CppDomain.SCALAR_TYPES),
        )
    if field.value_kind is ResolvedValueKind.IMMEDIATE:
        return OperandImmediateViewModel(
            field_id=field.name,
            immediate_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Immediate'),
            member_expr=member_expr,
        )
    if field.value_kind is ResolvedValueKind.PREDICATE:
        return OperandPredicateViewModel(
            field_id=field.name,
            predicate_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Predicate'),
            member_expr=member_expr,
        )
    if field.value_kind is ResolvedValueKind.PREDICATE_OR_SINK:
        return OperandPredicateOrSinkViewModel(
            field_id=field.name,
            predicate_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Predicate'),
            member_expr=member_expr,
        )
    if field.value_kind in {ResolvedValueKind.PREDICATE_SOURCE,
                            ResolvedValueKind.WGMMA_SCALE_D}:
        return OperandPredicateSourceViewModel(
            member_expr=member_expr,
            field_id=field.name,
            specialregister_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'SpecialRegister'),
            immediate_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Immediate'),
            pred_cpp=_cpp(backend, CppDomain.SCALAR_TYPES, 'pred'),
            predicate_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Predicate'),
        )
    if field.value_kind is ResolvedValueKind.BRANCH_TARGET:
        return OperandBranchTargetViewModel(
            field_id=field.name,
            branchtarget_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'BranchTarget'),
            member_expr=member_expr,
        )
    if field.value_kind is ResolvedValueKind.BRANCH_TARGET_SET:
        return OperandBranchTargetSetViewModel(
            field_id=field.name,
            branchtargetset_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'BranchTargetSet'),
            member_expr=member_expr,
        )
    if field.value_kind is ResolvedValueKind.SPECIAL_REGISTER:
        return OperandSpecialRegisterViewModel(
            member_expr=member_expr,
            field_id=field.name,
            specialregister_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'SpecialRegister'),
        )
    if field.value_kind is ResolvedValueKind.SYMBOL:
        return OperandSymbolViewModel(
            field_id=field.name,
            symbol_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Symbol'),
            member_expr=member_expr,
        )
    if field.value_kind is ResolvedValueKind.ADDRESS:
        return OperandAddressViewModel(
            member_expr=member_expr,
            parameter_directions_default=_cpp_default(backend, CppDomain.PARAMETER_DIRECTIONS),
            global_cpp=_cpp(backend, CppDomain.MEMORY_STATE_SPACES, 'global'),
            shared_cpp=_cpp(backend, CppDomain.MEMORY_STATE_SPACES, 'shared'),
            local_cpp=_cpp(backend, CppDomain.MEMORY_STATE_SPACES, 'local'),
            param_cpp=_cpp(backend, CppDomain.MEMORY_STATE_SPACES, 'param'),
            const_cpp=_cpp(backend, CppDomain.MEMORY_STATE_SPACES, 'const'),
            input_cpp=_cpp(backend, CppDomain.PARAMETER_DIRECTIONS, 'input'),
            return_cpp=_cpp(backend, CppDomain.PARAMETER_DIRECTIONS, 'return'),
            field_id=field.name,
            address_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Address'),
        )
    if field.value_kind in {ResolvedValueKind.REG_OR_IMM,
                            ResolvedValueKind.TENSOR_MEMORY_ADDRESS,
                            ResolvedValueKind.TCGEN_BRACKETED_ADDRESS}:
        source = (f"{member_expr}.value.value"
                  if field.value_kind in {ResolvedValueKind.TENSOR_MEMORY_ADDRESS,
                                          ResolvedValueKind.TCGEN_BRACKETED_ADDRESS}
                  else f"{member_expr}.value")
        return OperandRegisterOrImmediateViewModel(
            source=source,
            field_id=field.name,
            immediate_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Immediate'),
            member_expr=member_expr,
            register_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Register'),
        )
    if field.value_kind is ResolvedValueKind.TCGEN_HALF_SPLIT_OFFSET:
        return OperandTcgenHalfSplitOffsetViewModel(
            field_id=field.name,
            immediate_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Immediate'),
            member_expr=member_expr,
        )
    if field.value_kind is ResolvedValueKind.CP_ASYNC_SOURCE_CONTROL:
        return OperandCpAsyncSourceControlViewModel(
            member_expr=member_expr,
            field_id=field.name,
            immediate_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Immediate'),
            predicate_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Predicate'),
            register_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Register'),
        )
    if field.value_kind is ResolvedValueKind.CVT_SOURCE:
        return OperandCvtSourceViewModel(member_expr=member_expr, field_id=field.name)
    if field.value_kind is ResolvedValueKind.MOV_SOURCE:
        return OperandMovSourceViewModel(
            global_cpp=_cpp(backend, CppDomain.MEMORY_STATE_SPACES, 'global'),
            shared_cpp=_cpp(backend, CppDomain.MEMORY_STATE_SPACES, 'shared'),
            local_cpp=_cpp(backend, CppDomain.MEMORY_STATE_SPACES, 'local'),
            param_cpp=_cpp(backend, CppDomain.MEMORY_STATE_SPACES, 'param'),
            const_cpp=_cpp(backend, CppDomain.MEMORY_STATE_SPACES, 'const'),
            input_cpp=_cpp(backend, CppDomain.PARAMETER_DIRECTIONS, 'input'),
            return_cpp=_cpp(backend, CppDomain.PARAMETER_DIRECTIONS, 'return'),
            member_expr=member_expr,
            field_id=field.name,
            immediate_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Immediate'),
            register_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Register'),
            specialregister_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'SpecialRegister'),
            symbol_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Symbol'),
            address_cpp=_cpp(backend, CppDomain.RESOLVED_OPERAND_SHAPES, 'Address'),
        )
    raise ValueError(
        f"operand field {field.name!r}: unsupported checker view value kind "
        f"{field.value_kind.value!r}"
    )
