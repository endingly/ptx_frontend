from typing import Any
from ptx_frontend.spec.model import (
    AsyncCompletionKind,
    AtomicAddressQualifierPolicy,
    ConditionCodeEffect,
    FabricEndpointKind,
    FabricInstructionSpec,
    FabricOperation,
    FabricSharedAccess,
    TextureGeometry,
    TextureMipmapMode,
    TextureComponent,
    TextureQuery,
    OpaqueResourceKind,
    StackInstructionSpec,
    StackOperation,
    TextureInstructionSpec,
    InstructionSpec,
    OperandKind,
    OperandTypeExpressionKind,
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
from .surface import normalize_surface_contract, validate_surface_variant
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


def _normalize_stack_contract(raw: Any, opcode: str) -> StackInstructionSpec | None:
    """Validate the closed canonical stack operation and width metadata."""
    if raw is None:
        if opcode in {"stacksave", "stackrestore", "alloca"}:
            raise ValueError("stack instructions require a typed stack contract")
        return None
    operation = StackOperation(raw["operation"])
    if {"stacksave": StackOperation.SAVE, "stackrestore": StackOperation.RESTORE,
            "alloca": StackOperation.ALLOCATE}.get(opcode) is not operation:
        raise ValueError("stack operation disagrees with opcode")
    if raw["width"] not in (32, 64) or raw.get("default_alignment", 8) != 8:
        raise ValueError("invalid stack width or default alignment")
    return StackInstructionSpec(operation, raw["width"])


def _validate_stack_variant(variant: VariantSpec) -> None:
    """Require canonical stack metadata to agree with carrier and source layouts."""
    contract = variant.stack
    if contract is None:
        return
    scalar = f"u{contract.width}"
    if len(variant.modifiers) != 1 or variant.modifiers[0].name != "type":
        raise ValueError("stack forms require one fixed type modifier")
    modifier = variant.modifiers[0]
    if modifier.presence.value != "fixed" or modifier.value != scalar:
        raise ValueError("stack width disagrees with its fixed type")
    allocate = contract.operation is StackOperation.ALLOCATE
    expected_counts = (2, 3) if allocate else (1,)
    if tuple(len(layout.operands) for layout in variant.operand_layouts) != expected_counts:
        raise ValueError("stack source layouts disagree with operation")
    for layout in variant.operand_layouts:
        carrier = layout.operands[0]
        expected_kind = OperandKind.LOCAL_ALLOCATION_RESULT if allocate else OperandKind.STACK_TOKEN
        restore = contract.operation is StackOperation.RESTORE
        if (carrier.kind is not expected_kind or carrier.type_expression is None
                or carrier.type_expression.scalar_type != scalar
                or carrier.role is None or carrier.role.value != ("src" if restore else "dst")
                or carrier.access is None or carrier.access.value != ("read" if restore else "write")):
            raise ValueError("stack carrier role or width disagrees with contract")
        if allocate:
            size = layout.operands[1]
            if (size.name != "size" or size.kind is not OperandKind.REGISTER_OR_IMMEDIATE
                    or size.type_expression is None or size.type_expression.scalar_type != scalar):
                raise ValueError("allocation byte count must use the selected unsigned width")
            if len(layout.operands) == 3:
                alignment = layout.operands[2]
                if (alignment.name != "alignment" or alignment.kind is not OperandKind.IMMEDIATE
                        or alignment.type_expression is None
                        or alignment.type_expression.scalar_type != "u32"):
                    raise ValueError("allocation alignment must be an independent u32 constant")


def _normalize_texture_contract(raw: Any, opcode: str) -> TextureInstructionSpec | None:
    """Validate closed texture-family facts at the canonical YAML boundary."""

    families = {"tex", "tld4", "txq", "istypep"}
    if raw is None:
        if opcode in families:
            raise ValueError(f"{opcode} variant requires a texture contract")
        return None
    if opcode not in families or not isinstance(raw, dict):
        raise ValueError("texture contract belongs to a texture-family opcode")
    allowed = {"geometry", "mipmap", "component", "query", "tested_kind",
               "result_arity", "allows_offset", "allows_compare",
               "allows_residency", "query_level", "indirect_availability"}
    if set(raw) - allowed:
        raise ValueError("texture contract has unknown fields")
    geometry = raw.get("geometry")
    if geometry is not None:
        try:
            geometry = TextureGeometry(geometry)
        except ValueError as error:
            raise ValueError("texture contract has unknown geometry") from error
    try:
        mipmap = TextureMipmapMode(raw.get("mipmap", "omitted"))
    except (TypeError, ValueError) as error:
        raise ValueError("texture contract has invalid mipmap") from error
    try:
        component = (TextureComponent(raw["component"])
                     if raw.get("component") is not None else None)
    except (TypeError, ValueError) as error:
        raise ValueError("texture contract has unknown component") from error
    try:
        query = (TextureQuery(raw["query"])
                 if raw.get("query") is not None else None)
    except (TypeError, ValueError) as error:
        raise ValueError("texture contract has unknown query") from error
    try:
        tested_kind = (OpaqueResourceKind(raw["tested_kind"])
                       if raw.get("tested_kind") is not None else None)
    except (TypeError, ValueError) as error:
        raise ValueError("texture contract has unknown tested kind") from error
    result_arity = raw.get("result_arity", 1)
    if type(result_arity) is not int or result_arity not in {1, 2, 4}:
        raise ValueError("texture contract has invalid result arity")
    flags = {name: raw.get(name, False) for name in
             ("allows_offset", "allows_compare", "allows_residency", "query_level")}
    if any(type(value) is not bool for value in flags.values()):
        raise TypeError("texture contract control flags must be boolean")
    indirect_raw = raw.get("indirect_availability")
    if opcode == "istypep":
        if indirect_raw is not None:
            raise ValueError("opaque kind test has no indirect texture gate")
        indirect_availability = None
    else:
        if indirect_raw is None:
            raise ValueError("texture resource requires an indirect availability gate")
        indirect_availability = normalize_availability(indirect_raw)
        if not indirect_availability:
            raise ValueError("indirect availability gate cannot be empty")
    if opcode in {"tex", "tld4"}:
        if geometry is None or query or tested_kind or result_arity not in {2, 4}:
            raise ValueError("texture access contract has invalid topology")
        if opcode == "tld4" and (component is None or mipmap is not TextureMipmapMode.OMITTED or result_arity != 4):
            raise ValueError("tld4 requires component and four-lane result")
        if opcode == "tex" and component is not None:
            raise ValueError("tex has no component selector")
        if opcode == "tld4" and geometry not in {
            TextureGeometry.TWO_D, TextureGeometry.ARRAY_TWO_D,
            TextureGeometry.CUBE, TextureGeometry.ARRAY_CUBE,
        }:
            raise ValueError("tld4 geometry is outside its four legal forms")
        if opcode == "tld4" and flags["allows_offset"] and geometry in {
            TextureGeometry.CUBE, TextureGeometry.ARRAY_CUBE,
        }:
            raise ValueError("cube gather has no coordinate offset")
        if opcode == "tex":
            if geometry in {TextureGeometry.TWO_D_MULTISAMPLE,
                            TextureGeometry.ARRAY_TWO_D_MULTISAMPLE} and (
                    mipmap in {TextureMipmapMode.LEVEL, TextureMipmapMode.GRADIENT} or flags["allows_compare"]):
                raise ValueError("multisample texture cannot use mipmap or compare")
            if flags["allows_offset"] and geometry in {
                    TextureGeometry.CUBE, TextureGeometry.ARRAY_CUBE}:
                raise ValueError("cube texture has no coordinate offset")
            if flags["allows_compare"] and geometry == TextureGeometry.THREE_D:
                raise ValueError("3d texture has no depth compare")
        if flags["query_level"]:
            raise ValueError("texture access has no query level")
    elif opcode == "txq":
        if geometry or component or tested_kind or query is None or result_arity != 1 or mipmap is not TextureMipmapMode.OMITTED:
            raise ValueError("txq requires one query and scalar result")
        if flags["query_level"] and query not in {TextureQuery.WIDTH, TextureQuery.HEIGHT, TextureQuery.DEPTH}:
            raise ValueError("query level belongs to dimensions")
        if any(flags[name] for name in ("allows_offset", "allows_compare",
                                       "allows_residency")):
            raise ValueError("query has no access controls")
    else:
        if geometry or component or query or tested_kind is None or result_arity != 1 or mipmap is not TextureMipmapMode.OMITTED:
            raise ValueError("istypep requires one queried opaque kind")
        if any(flags.values()):
            raise ValueError("opaque kind test has no texture controls")
    return TextureInstructionSpec(geometry=geometry, mipmap=mipmap,
        component=component, query=query, tested_kind=tested_kind,
        result_arity=result_arity, indirect_availability=indirect_availability,
        **flags)


def _validate_texture_variant(opcode: str, variant: VariantSpec) -> None:
    """Keep a texture descriptor, spellings, and operand layouts coherent."""

    contract = variant.texture
    if contract is None:
        return
    tokens = {spelling for modifier in variant.modifiers
              for spelling in modifier_spellings(modifier)}
    if opcode in {"tex", "tld4"}:
        assert contract.geometry is not None
        mip_token = {TextureMipmapMode.BASE: ".base",
                     TextureMipmapMode.LEVEL: ".level",
                     TextureMipmapMode.GRADIENT: ".grad"}.get(contract.mipmap)
        if f".{contract.geometry.value}" not in tokens or (
                mip_token is not None and mip_token not in tokens) or (
                mip_token is None and tokens & {".base", ".level", ".grad"}):
            raise ValueError("texture modifier spelling contradicts geometry or mipmap")
        if f".v{contract.result_arity}" not in tokens:
            raise ValueError("texture result arity contradicts vector modifier")
        if opcode == "tld4" and f".{contract.component.value}" not in tokens:
            raise ValueError("gather component contradicts its modifier")
        for layout in variant.operand_layouts:
            operands = layout.operands
            if len(operands) < 2 or operands[0].kind not in {
                    OperandKind.TEXTURE_RESULT,
                    OperandKind.TEXTURE_RESULT_WITH_PREDICATE} or (
                    operands[0].vector_arities != (contract.result_arity,)) or (
                    operands[1].kind is not OperandKind.TEXTURE_ACCESS) or (
                    operands[1].texture_geometry != contract.geometry):
                raise ValueError("texture operand layout contradicts typed topology")
            coordinate_type = operands[1].type_expression
            if (coordinate_type is None or
                    coordinate_type.kind is not OperandTypeExpressionKind.MODIFIER or
                    coordinate_type.modifier_name != "ctype"):
                raise ValueError("texture access must select its coordinate type from ctype")
            has_predicate = operands[0].kind is OperandKind.TEXTURE_RESULT_WITH_PREDICATE
            names = {operand.name for operand in operands}
            if (has_predicate and not contract.allows_residency) or (
                    ("offset" in names) and not contract.allows_offset) or (
                    ("compare" in names) and not contract.allows_compare):
                raise ValueError("texture layout uses a forbidden optional control")
            if contract.mipmap is TextureMipmapMode.LEVEL and "lod" not in names:
                raise ValueError("mipmap level layout lacks LOD")
            if contract.mipmap is TextureMipmapMode.GRADIENT and not {"ddx", "ddy"} <= names:
                raise ValueError("gradient layout lacks both derivatives")
    elif opcode == "txq":
        if f".{contract.query.value}" not in tokens or ".b32" not in tokens:
            raise ValueError("query spelling contradicts typed query")
        if contract.query_level != (".level" in tokens):
            raise ValueError("query level modifier contradicts typed query")
        for layout in variant.operand_layouts:
            operands = layout.operands
            if len(operands) != (3 if contract.query_level else 2) or (
                    operands[1].kind is not OperandKind.TEXTURE_QUERY_RESOURCE):
                raise ValueError("query resource or LOD layout is invalid")
    elif opcode == "istypep":
        spelling = {OpaqueResourceKind.TEXTURE: ".texref",
                    OpaqueResourceKind.SAMPLER: ".samplerref",
                    OpaqueResourceKind.SURFACE: ".surfref"}[contract.tested_kind]
        if spelling not in tokens:
            raise ValueError("opaque kind test spelling contradicts its descriptor")


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
                    surface=normalize_surface_contract(raw_variant.get("surface"), raw_instruction["opcode"]),
                    stack=_normalize_stack_contract(
                        raw_variant.get("stack"), raw_instruction["opcode"]
                    ),
                    texture=_normalize_texture_contract(
                        raw_variant.get("texture"), raw_instruction["opcode"]
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
            validate_surface_variant(raw_instruction["opcode"], variant)
            _validate_stack_variant(variant)
            _validate_fabric_variant(variant)
            _validate_texture_variant(raw_instruction["opcode"], variant)
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
