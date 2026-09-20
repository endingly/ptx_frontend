"""Structural module-reference policy for resolved operand payloads."""

from ptx_frontend.ir.resolved_ir import (
    ResolvedInstruction,
    ResolvedValueKind,
)


REFERENCE_VALUE_KINDS = frozenset({
    ResolvedValueKind.REGISTER,
    ResolvedValueKind.MBARRIER_STATE_TOKEN,
    ResolvedValueKind.REGISTER_OR_SINK,
    ResolvedValueKind.REG_OR_IMM,
    ResolvedValueKind.SHFL_DESTINATION,
    ResolvedValueKind.PREDICATE_PAIR,
    ResolvedValueKind.PREDICATE_PAIR_OR_SINK,
    ResolvedValueKind.PREDICATE_OR_SINK,
    ResolvedValueKind.MOV_SOURCE,
    ResolvedValueKind.PREDICATE,
    ResolvedValueKind.PREDICATE_SOURCE,
    ResolvedValueKind.BRANCH_TARGET,
    ResolvedValueKind.BRANCH_TARGET_SET,
    ResolvedValueKind.VECTOR_REGISTER,
    ResolvedValueKind.SYMBOL,
    ResolvedValueKind.ADDRESS,
    ResolvedValueKind.REGISTER_VECTOR,
    ResolvedValueKind.TENSOR_COORDINATE,
    ResolvedValueKind.DIRECT_CALL_TARGET,
    ResolvedValueKind.INDIRECT_CALLEE,
    ResolvedValueKind.CALL_RETURN_PARAMETER,
    ResolvedValueKind.CALL_ARGUMENTS,
})

REFERENCE_FREE_VALUE_KINDS = frozenset({
    ResolvedValueKind.BOOL,
    ResolvedValueKind.SCALAR_TYPE,
    ResolvedValueKind.ROUNDING_MODE,
    ResolvedValueKind.COMPARISON_OPERATOR,
    ResolvedValueKind.BOOLEAN_OPERATOR,
    ResolvedValueKind.CACHE_OPERATOR,
    ResolvedValueKind.EVICTION_PRIORITY,
    ResolvedValueKind.PREFETCH_SIZE,
    ResolvedValueKind.MEMORY_CONSISTENCY,
    ResolvedValueKind.MEMORY_SCOPE,
    ResolvedValueKind.VECTOR_ARITY,
    ResolvedValueKind.MEMORY_STATE_SPACE,
    ResolvedValueKind.MBARRIER_PHASE_TYPE,
    ResolvedValueKind.MBARRIER_LAYOUT,
    ResolvedValueKind.ASYNC_PROXY_KIND,
    ResolvedValueKind.PROXY_KIND_PAIR,
    ResolvedValueKind.IMMEDIATE,
    ResolvedValueKind.SPECIAL_REGISTER,
    ResolvedValueKind.VECTOR_SPECIAL_REGISTER,
})


def validate_reference_field_types(
    instructions: tuple[ResolvedInstruction, ...],
) -> None:
    """Require an explicit module-reference policy for every operand payload."""

    known = REFERENCE_VALUE_KINDS | REFERENCE_FREE_VALUE_KINDS
    for instruction in instructions:
        for variant in instruction.variants:
            for layout in variant.operand_layouts:
                for field in layout.fields:
                    if field.value_kind not in known:
                        raise ValueError(
                            "Resolved operand value kind needs an explicit "
                            f"module-reference policy: {field.value_kind!r}"
                        )
