from typing import Any
from ptx_frontend.spec.model import (
    AsyncCompletionKind,
    AtomicAddressQualifierPolicy,
    ConditionCodeEffect,
    FabricEndpointKind,
    FabricInstructionSpec,
    FabricOperation,
    FabricSharedAccess,
    InstructionSpec,
    OperandKind,
    SemanticRule,
    VariantSpec,
    WgmmaProtocolAction,
    modifier_spellings,
)
from .constraints import (
    _normalize_operand_type_compatibilities,
    _normalize_memory_consistency_constraint,
    _normalize_unified_address_access,
    _normalize_address_alignment_constraints,
    _normalize_memory_vector_constraint,
    _normalize_immediate_value_constraint,
    _normalize_immediate_range_constraints,
    _normalize_immediate_multiple_of_constraint,
)
from .availability import normalize_availability
from .layout import normalize_operand_layouts
from .matrix import normalize_matrix
from .tcgen_allocation import validate_tcgen_allocation_variant
from .tcgen_load_store import validate_tcgen_transfer_variant
from .tcgen_sync import validate_tcgen_sync_variant
from .tcgen_copy_shift import normalize_tcgen_copy_contract, validate_tcgen_copy_shift_variant
from .tcgen_mma import validate_tcgen_mma_variant
from .modifiers import (
    normalize_modifier,
    normalize_modifier_order_aliases,
)
from .validation import (
    _validate_modifier_state_space_expressions,
    _validate_modifier_type_expressions,
)


def _normalize_fabric_contract(raw: Any, opcode: str) -> FabricInstructionSpec | None:
    """Require an exact typed CFT contract on each declared fabric form."""

    if raw is None:
        if opcode == "fabric":
            raise ValueError("fabric variant requires a fabric contract")
        return None
    if opcode != "fabric" or not isinstance(raw, dict):
        raise ValueError("fabric contract belongs to the canonical fabric opcode")
    expected = {"operation", "endpoint", "shared_access", "counted",
                "reports_fabric", "requires_mbarrier_layout_v1"}
    if set(raw) != expected:
        raise ValueError("fabric contract requires all six typed fields")
    for field_name in ("counted", "reports_fabric", "requires_mbarrier_layout_v1"):
        if type(raw[field_name]) is not bool:
            raise TypeError(f"fabric {field_name} must be boolean")
    return FabricInstructionSpec(
        operation=FabricOperation(raw["operation"]),
        endpoint=FabricEndpointKind(raw["endpoint"]),
        shared_access=FabricSharedAccess(raw["shared_access"]),
        counted=raw["counted"],
        reports_fabric=raw["reports_fabric"],
        requires_mbarrier_layout_v1=raw["requires_mbarrier_layout_v1"],
    )


def _validate_fabric_variant(variant: VariantSpec) -> None:
    """Reject contradictory CFT metadata before it reaches generated classes."""

    contract = variant.fabric
    if contract is None:
        return
    if len(variant.operand_layouts) != 1:
        raise ValueError("fabric form requires one exact operand layout")
    operands = variant.operand_layouts[0].operands
    spellings = {spelling for field in variant.modifiers
                 for spelling in modifier_spellings(field)}
    operation = contract.operation
    if f".{operation.value}" not in spellings:
        raise ValueError("fabric operation metadata must match its suffix")
    try_form = operation in {FabricOperation.TRY_GET, FabricOperation.TRY_PUT,
                             FabricOperation.TRY_RED, FabricOperation.TRY_PULLRED}
    if contract.reports_fabric != try_form or contract.requires_mbarrier_layout_v1 != try_form:
        raise ValueError("fabric report and layout contract must match try forms")
    expected_completion = {
        FabricOperation.TRY_GET: AsyncCompletionKind.MBARRIER_COMPLETE_TX_BYTES,
        FabricOperation.TRY_PUT: AsyncCompletionKind.MBARRIER_COMPLETE_TX16B,
        FabricOperation.TRY_RED: AsyncCompletionKind.MBARRIER_COMPLETE_TX16B,
        FabricOperation.TRY_PULLRED: AsyncCompletionKind.MBARRIER_COMPLETE_TX_BYTES,
        FabricOperation.SUBMIT: AsyncCompletionKind.NONE,
        FabricOperation.WAIT: AsyncCompletionKind.FABRIC_READ_WAIT,
    }[operation]
    if variant.completion_kind is not expected_completion:
        raise ValueError("fabric completion identity contradicts the operation")
    if contract.counted != (".counted::bytes" in spellings):
        raise ValueError("fabric counted metadata contradicts the written modifier")
    if operation in {FabricOperation.SUBMIT, FabricOperation.WAIT}:
        if (operands or contract.endpoint is not FabricEndpointKind.NONE or
                contract.shared_access is not FabricSharedAccess.NONE or contract.counted):
            raise ValueError("fabric submit/wait have no endpoint or explicit operands")
        return
    has_async_shared = (
        {".async", ".shared::cta"} <= spellings or
        ".async.shared::cta" in spellings
    )
    if (not has_async_shared or
            not {".relaxed", ".sys", ".mbarrier::report::fabric"} <= spellings):
        raise ValueError("fabric try form lacks fixed async/shared/ordering/report modifiers")
    completion_token = (
        ".mbarrier::complete_tx::bytes"
        if expected_completion is AsyncCompletionKind.MBARRIER_COMPLETE_TX_BYTES
        else ".mbarrier::complete_tx::16B"
    )
    if completion_token not in spellings:
        raise ValueError("fabric completion modifier contradicts the contract")
    expected_shared = (FabricSharedAccess.WRITE if operation in {
        FabricOperation.TRY_GET, FabricOperation.TRY_PULLRED}
        else FabricSharedAccess.READ)
    if contract.shared_access is not expected_shared:
        raise ValueError("fabric shared access direction contradicts the operation")
    if operation is FabricOperation.TRY_GET and contract.endpoint is not FabricEndpointKind.UNICAST:
        raise ValueError("fabric try_get requires a unicast endpoint")
    if operation is FabricOperation.TRY_PULLRED and contract.endpoint is not FabricEndpointKind.MULTICAST:
        raise ValueError("fabric try_pullred requires a multicast endpoint")
    if (operation in {FabricOperation.TRY_PUT, FabricOperation.TRY_RED} and
            contract.endpoint not in {FabricEndpointKind.UNICAST,
                                      FabricEndpointKind.MULTICAST}):
        raise ValueError("fabric put/red require a unicast or multicast endpoint")
    if (".multimem" in spellings) != (contract.endpoint is FabricEndpointKind.MULTICAST):
        raise ValueError("fabric endpoint topology contradicts multimem presence")
    expected_handle = "src" if expected_shared is FabricSharedAccess.WRITE else "dst"
    handles = [item for item in operands if item.kind is OperandKind.FABRIC_HANDLE]
    if len(handles) != 1 or handles[0].name != expected_handle:
        raise ValueError("fabric form requires one direction-specific CFT handle")
    names = [item.name for item in operands]
    if names[:4] != ["dst", "src", "size", "mbar"]:
        raise ValueError("fabric transfer operands must retain ISA order")
    if (operands[2].kind is not OperandKind.REGISTER_OR_IMMEDIATE or
            operands[3].kind is not OperandKind.ADDRESS):
        raise ValueError("fabric size and mbar operands have wrong kinds")
    if operation is FabricOperation.TRY_PULLRED:
        if (".sync" not in spellings or len(operands) != 5 or
                operands[4].name != "membermask" or
                operands[4].kind is not OperandKind.IMMEDIATE or contract.counted):
            raise ValueError("fabric pullred requires one immediate member mask")
    elif operation is FabricOperation.TRY_PUT and ".cp_mask" in spellings:
        if (contract.counted or len(operands) != 5 or
                operands[4].name != "bytemask" or
                operands[4].kind is not OperandKind.REGISTER_OR_IMMEDIATE):
            raise ValueError("fabric cp_mask requires a final u16 mask, not counted")
    elif len(operands) != 4:
        raise ValueError("fabric form has an unexpected operand")


def normalize_instruction_spec(spec: dict[str, Any]) -> tuple[InstructionSpec, ...]:
    """Normalize all instruction definitions in one PTX ISA YAML file."""

    source_category = spec.get("category")
    codegen_category = spec.get("codegen_category")
    if not isinstance(source_category, str):
        raise ValueError("PTX spec file must define top-level category")
    if not isinstance(codegen_category, str):
        raise ValueError("PTX spec file must define top-level codegen_category")

    type_sets = spec.get("type_sets", {})
    value_sets = spec.get("value_sets", {})
    duplicate_set_names = set(type_sets) & set(value_sets)
    if duplicate_set_names:
        raise ValueError(
            "type_sets and value_sets define the same names: "
            f"{sorted(duplicate_set_names)}"
        )
    reusable_value_sets = {**type_sets, **value_sets}
    operand_patterns = spec.get("operand_patterns", {})
    instructions: list[InstructionSpec] = []

    for raw_instruction in spec["instructions"]:
        default_operands = raw_instruction.get("operands")
        variants: list[VariantSpec] = []

        for raw_variant in raw_instruction["variants"]:
            modifiers = tuple(
                normalize_modifier(modifier, reusable_value_sets)
                for modifier in raw_variant.get("modifiers", ())
            )
            modifier_order_aliases = normalize_modifier_order_aliases(
                raw_variant, modifiers
            )
            operand_layouts = normalize_operand_layouts(
                raw_variant, default_operands, operand_patterns
            )
            _validate_modifier_type_expressions(modifiers, operand_layouts)
            _validate_modifier_state_space_expressions(modifiers, operand_layouts)
            copy_pairs, copy_formats = normalize_tcgen_copy_contract(raw_variant)
            variant = VariantSpec(
                    name=raw_variant["name"],
                    completion_kind=AsyncCompletionKind(
                        raw_variant.get("completion_kind", "none")
                    ),
                    fabric=_normalize_fabric_contract(
                        raw_variant.get("fabric"), raw_instruction["opcode"]
                    ),
                    wgmma_protocol_action=WgmmaProtocolAction(
                        raw_variant.get("wgmma_protocol_action", "none")
                    ),
                    condition_code_effect=ConditionCodeEffect(
                        raw_variant.get("condition_code_effect", "none")
                    ),
                    availability=normalize_availability(raw_variant["availability"]),
                    modifiers=modifiers,
                    operand_layouts=operand_layouts,
                    matrix=normalize_matrix(raw_variant.get("matrix"), modifiers, operand_layouts),
                    modifier_order_aliases=modifier_order_aliases,
                    tcgen_copy_pairs=copy_pairs,
                    tcgen_copy_formats=copy_formats,
                    rule=_normalize_semantic_rule(raw_variant.get("rule")),
                    operand_type_compatibilities=(
                        _normalize_operand_type_compatibilities(
                            raw_variant, operand_layouts
                        )
                    ),
                    memory_consistency=_normalize_memory_consistency_constraint(
                        raw_variant, modifiers, operand_layouts
                    ),
                    permits_unified_address=bool(
                        raw_variant.get("permits_unified_address", False)
                    ),
                    unified_address_access=_normalize_unified_address_access(
                        raw_variant
                    ),
                    address_alignments=_normalize_address_alignment_constraints(
                        raw_variant, modifiers, operand_layouts
                    ),
                    memory_vector=_normalize_memory_vector_constraint(
                        raw_variant, modifiers, operand_layouts
                    ),
                    immediate_value=_normalize_immediate_value_constraint(
                        raw_variant, operand_layouts
                    ),
                    immediate_ranges=_normalize_immediate_range_constraints(
                        raw_variant, operand_layouts
                    ),
                    immediate_multiple_of=_normalize_immediate_multiple_of_constraint(
                        raw_variant, operand_layouts
                    ),
                )
            validate_tcgen_allocation_variant(variant)
            validate_tcgen_transfer_variant(variant)
            validate_tcgen_sync_variant(variant)
            validate_tcgen_copy_shift_variant(variant)
            validate_tcgen_mma_variant(variant)
            _validate_fabric_variant(variant)
            variants.append(variant)

        instructions.append(
            InstructionSpec(
                opcode=raw_instruction["opcode"],
                variants=tuple(variants),
                syntax_forms=(
                    (raw_instruction["syntax"],) if "syntax" in raw_instruction else ()
                ),
                source_categories=(source_category,),
                codegen_category=codegen_category,
                atomic_address_qualifier=_normalize_atomic_address_qualifier(
                    raw_instruction.get("atomic_address_qualifier")
                ),
            )
        )

    return tuple(instructions)


def _normalize_atomic_address_qualifier(
    raw: object,
) -> AtomicAddressQualifierPolicy | None:
    """Normalize the instruction-level written atomic address policy."""

    if raw is None:
        return None
    if not isinstance(raw, dict):
        raise ValueError("atomic_address_qualifier must be an object")
    state_space = raw.get("state_space_modifier")
    address = raw.get("address_operand")
    if not isinstance(state_space, str) or not isinstance(address, str):
        raise ValueError("atomic_address_qualifier requires modifier and operand names")
    return AtomicAddressQualifierPolicy(state_space, address)


def _normalize_semantic_rule(raw_rule: object) -> SemanticRule | None:
    """Convert an optional external rule spelling into its closed identity."""

    if raw_rule is None:
        return None
    if not isinstance(raw_rule, str):
        raise ValueError("semantic rule must be a string")
    try:
        return SemanticRule(raw_rule)
    except ValueError as error:
        raise ValueError(f"unknown semantic rule {raw_rule!r}") from error
