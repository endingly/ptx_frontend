from dataclasses import dataclass
from pathlib import Path

from jinja2 import Environment, FileSystemLoader, StrictUndefined

from code_gen.database import (
    CodegenDatabase,
    LoadedCodegenUnit,
)
from code_gen.model import (
    InstructionBackend,
    InstructionSpec,
    ModifierSpec,
)
from code_gen.naming import (
    category_ir_header_name,
    category_parser_header_name,
    category_parser_source_name,
    domain_parse_function_name,
    to_cpp_identifier,
    to_pascal_case,
)

# =============================================================================
# Template-facing view model
# =============================================================================


@dataclass(frozen=True)
class ModifierFieldView:
    spec_name: str
    field: str
    cpp_type: str

    # "flag" or "domain"
    kind: str

    token_name: str | None
    domain_name: str | None
    parse_func: str | None


@dataclass(frozen=True)
class VariantConstraintView:
    field: str
    kind: str
    presence: str

    allowed_cpp_values: tuple[str, ...]
    fixed_cpp_value: str | None


@dataclass(frozen=True)
class ParserVariantView:
    name: str
    enum_name: str
    predicate_name: str

    constraints: tuple[VariantConstraintView, ...]

    min_ptx_major: int
    min_ptx_minor: int
    min_sm: int
    family: str
    deprecated: bool
    removed: bool

    # Used only by emit.kind == sub_variant.
    storage_type: str | None
    storage_fields: tuple[str, ...]


@dataclass(frozen=True)
class ParserOperandView:
    spec_name: str
    field: str
    kind_cpp: str


@dataclass(frozen=True)
class ParserInstructionView:
    opcode: str
    cpp_type: str

    form_type: str
    match_type: str
    match_func: str
    parse_func: str

    emit_kind: str
    data_instance: str | None
    assignment_prefix: str | None

    modifiers: tuple[ModifierFieldView, ...]
    variants: tuple[ParserVariantView, ...]
    operands: tuple[ParserOperandView, ...]


@dataclass(frozen=True)
class ParserCategoryView:
    namespace: str
    category: str

    header_name: str
    source_name: str
    ir_header_name: str

    instructions: tuple[ParserInstructionView, ...]


# =============================================================================
# Public API
# =============================================================================


def generate_parser_category(
    database: CodegenDatabase,
    loaded: LoadedCodegenUnit,
    *,
    template_dir: Path,
    output_dir: Path,
    header_template_name: str = "ptx_parser_category.gen.hpp.j2",
    source_template_name: str = "ptx_parser_category.gen.cpp.j2",
) -> tuple[Path, Path]:
    view = build_parser_category_view(
        database=database,
        loaded=loaded,
    )

    env = Environment(
        loader=FileSystemLoader(str(template_dir)),
        undefined=StrictUndefined,
        trim_blocks=True,
        lstrip_blocks=True,
        keep_trailing_newline=True,
    )

    header_template = env.get_template(header_template_name)

    source_template = env.get_template(source_template_name)

    output_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    header_path = output_dir / view.header_name
    source_path = output_dir / view.source_name

    header_path.write_text(
        header_template.render(
            namespace=view.namespace,
            category=view.category,
            ir_header_name=view.ir_header_name,
            instructions=view.instructions,
        ),
        encoding="utf-8",
    )

    source_path.write_text(
        source_template.render(
            namespace=view.namespace,
            category=view.category,
            header_name=view.header_name,
            instructions=view.instructions,
        ),
        encoding="utf-8",
    )

    return header_path, source_path


# =============================================================================
# Category view construction
# =============================================================================


def build_parser_category_view(
    *,
    database: CodegenDatabase,
    loaded: LoadedCodegenUnit,
) -> ParserCategoryView:
    unit = loaded.unit

    instructions: list[ParserInstructionView] = []

    for spec in unit.instructions:
        try:
            backend = unit.backends[spec.opcode]
        except KeyError as exc:
            raise ValueError(
                f"{loaded.source.backend_path}: "
                f"no backend mapping for opcode "
                f"{spec.opcode!r}"
            ) from exc

        instructions.append(
            build_parser_instruction_view(
                database=database,
                spec=spec,
                backend=backend,
            )
        )

    return ParserCategoryView(
        namespace=database.namespace,
        category=unit.category,
        header_name=category_parser_header_name(unit.category),
        source_name=category_parser_source_name(unit.category),
        ir_header_name=category_ir_header_name(unit.category),
        instructions=tuple(instructions),
    )


# =============================================================================
# Instruction view construction
# =============================================================================


def build_parser_instruction_view(
    *,
    database: CodegenDatabase,
    spec: InstructionSpec,
    backend: InstructionBackend,
) -> ParserInstructionView:
    modifiers = build_modifier_views(
        database=database,
        spec=spec,
        backend=backend,
    )

    validate_modifier_recognizers(
        database=database,
        opcode=spec.opcode,
        modifiers=modifiers,
    )

    storage_map = build_variant_storage_map(
        spec=spec,
        backend=backend,
    )

    storage_fields = build_storage_field_map(
        spec=spec,
        backend=backend,
        modifiers=modifiers,
        storage_map=storage_map,
    )

    variants = build_variant_views(
        database=database,
        spec=spec,
        modifiers=modifiers,
        storage_map=storage_map,
        storage_fields=storage_fields,
    )

    operands = build_operand_views(
        spec=spec,
        backend=backend,
    )

    assignment_prefix = resolve_assignment_prefix(backend)

    cpp_type = backend.cpp

    return ParserInstructionView(
        opcode=spec.opcode,
        cpp_type=cpp_type,
        form_type=f"{cpp_type}Form",
        match_type=f"{cpp_type}Match",
        match_func=f"match{cpp_type}",
        parse_func=f"parse{cpp_type}",
        emit_kind=backend.emit.kind,
        data_instance=backend.emit.instance,
        assignment_prefix=assignment_prefix,
        modifiers=modifiers,
        variants=variants,
        operands=operands,
    )


def resolve_assignment_prefix(
    backend: InstructionBackend,
) -> str | None:
    emit = backend.emit

    if emit.kind == "direct":
        return "instr."

    if emit.kind == "sub_struct":
        if emit.instance is None:
            raise ValueError(f"{backend.opcode}: " "sub_struct requires emit.instance")

        return f"instr.{emit.instance}."

    if emit.kind == "sub_variant":
        if emit.instance is None:
            raise ValueError(f"{backend.opcode}: " "sub_variant requires emit.instance")

        return None

    raise ValueError(f"{backend.opcode}: " f"unsupported emit kind {emit.kind!r}")


# =============================================================================
# Modifier view construction
# =============================================================================


def build_modifier_views(
    *,
    database: CodegenDatabase,
    spec: InstructionSpec,
    backend: InstructionBackend,
) -> tuple[ModifierFieldView, ...]:
    occurrences: dict[str, list[ModifierSpec]] = {}

    for variant in spec.variants:
        for modifier in variant.modifiers:
            occurrences.setdefault(
                modifier.name,
                [],
            ).append(modifier)

    ordered_names = canonical_modifier_order(spec)

    for name in backend.modifiers:
        if name not in ordered_names:
            ordered_names.append(name)

    for name in occurrences:
        if name not in ordered_names:
            ordered_names.append(name)

    result: list[ModifierFieldView] = []

    for name in ordered_names:
        modifier_occurrences = occurrences.get(
            name,
            [],
        )

        if not modifier_occurrences:
            continue

        # If a modifier is absent from every variant, it does not
        # need runtime storage.
        if all(modifier.presence == "absent" for modifier in modifier_occurrences):
            continue

        backend_modifier = backend.modifiers.get(name)

        if backend_modifier is None:
            raise ValueError(
                f"{spec.opcode}.{name}: modifier appears "
                "in spec but has no backend mapping"
            )

        kinds = {modifier.kind for modifier in modifier_occurrences}

        if len(kinds) != 1:
            raise ValueError(
                f"{spec.opcode}.{name}: " f"inconsistent kinds {sorted(kinds)}"
            )

        kind = next(iter(kinds))

        if kind == "flag":
            token_names = {
                strip_dot(modifier.token)
                for modifier in modifier_occurrences
                if modifier.token is not None
            }

            if len(token_names) > 1:
                raise ValueError(
                    f"{spec.opcode}.{name}: "
                    f"conflicting tokens {sorted(token_names)}"  # type: ignore
                )

            token_name = next(iter(token_names)) if token_names else name

            cpp_type = backend_modifier.cpp_type or "bool"

            result.append(
                ModifierFieldView(
                    spec_name=name,
                    field=backend_modifier.field,
                    cpp_type=cpp_type,
                    kind="flag",
                    token_name=token_name,
                    domain_name=None,
                    parse_func=None,
                )
            )

            continue

        domain_names = {
            modifier.domain
            for modifier in modifier_occurrences
            if modifier.domain is not None
        }

        if backend_modifier.domain is not None:
            domain_names.add(backend_modifier.domain)

        if len(domain_names) != 1:
            raise ValueError(
                f"{spec.opcode}.{name}: "
                "expected exactly one domain, got "
                f"{sorted(domain_names)}"
            )

        domain_name = next(iter(domain_names))

        try:
            domain = database.domains[domain_name]
        except KeyError as exc:
            raise ValueError(
                f"{spec.opcode}.{name}: " f"unknown domain {domain_name!r}"
            ) from exc

        cpp_type = backend_modifier.cpp_type or domain.cpp_type

        if cpp_type != domain.cpp_type:
            raise ValueError(
                f"{spec.opcode}.{name}: "
                f"cpp_type {cpp_type!r} conflicts with "
                f"domain cpp_type {domain.cpp_type!r}"
            )

        result.append(
            ModifierFieldView(
                spec_name=name,
                field=backend_modifier.field,
                cpp_type=cpp_type,
                kind="domain",
                token_name=None,
                domain_name=domain_name,
                parse_func=domain_parse_function_name(domain_name),
            )
        )

    return tuple(result)


def canonical_modifier_order(spec: InstructionSpec) -> list[str]:
    if not spec.variants:
        raise ValueError(f"{spec.opcode}: no variants")

    canonical = [modifier.name for modifier in spec.variants[0].modifiers]

    if len(canonical) != len(set(canonical)):
        raise ValueError(f"{spec.opcode}: duplicate modifier slots")

    for variant in spec.variants[1:]:
        order = [modifier.name for modifier in variant.modifiers]
        if order != canonical:
            raise ValueError(
                f"{spec.opcode}.{variant.name}: modifier slots {order!r} "
                f"do not match canonical order {canonical!r}"
            )

    return canonical


def validate_modifier_recognizers(
    *,
    database: CodegenDatabase,
    opcode: str,
    modifiers: tuple[ModifierFieldView, ...],
) -> None:
    """
    The first implementation requires disjoint modifier recognizers.

    For example, cvt.<dst-type>.<src-type> cannot use this matcher
    because both modifier slots recognize the same scalar type domain.
    """

    recognized: list[tuple[ModifierFieldView, set[str]]] = []

    for modifier in modifiers:
        if modifier.kind == "flag":
            assert modifier.token_name is not None
            spellings = {modifier.token_name}
        else:
            assert modifier.domain_name is not None

            spellings = set(database.domains[modifier.domain_name].values.keys())

        recognized.append((modifier, spellings))

    for index, (lhs, lhs_values) in enumerate(recognized):
        for rhs, rhs_values in recognized[index + 1 :]:
            overlap = lhs_values & rhs_values

            if overlap:
                raise ValueError(
                    f"{opcode}: modifier fields "
                    f"{lhs.spec_name!r} and "
                    f"{rhs.spec_name!r} recognize "
                    f"overlapping spellings "
                    f"{sorted(overlap)}"
                )


# =============================================================================
# sub_variant storage mapping
# =============================================================================


def build_variant_storage_map(
    *,
    spec: InstructionSpec,
    backend: InstructionBackend,
) -> dict[str, str | None]:
    variant_names = {variant.name for variant in spec.variants}

    if backend.emit.kind != "sub_variant":
        return {name: None for name in variant_names}

    alternatives = backend.emit.alternatives

    if not alternatives:
        raise ValueError(f"{spec.opcode}: sub_variant requires " "emit.alternatives")

    if len(alternatives) == 1 and not alternatives[0].variants:
        return {name: alternatives[0].name for name in variant_names}

    result: dict[str, str] = {}

    for alternative in alternatives:
        if not alternative.variants:
            raise ValueError(
                f"{spec.opcode}: alternative "
                f"{alternative.name!r} must specify "
                "its variants"
            )

        for variant_name in alternative.variants:
            if variant_name not in variant_names:
                raise ValueError(
                    f"{spec.opcode}: alternative "
                    f"{alternative.name!r} references "
                    f"unknown variant {variant_name!r}"
                )

            previous = result.get(variant_name)

            if previous is not None:
                raise ValueError(
                    f"{spec.opcode}: variant "
                    f"{variant_name!r} maps to both "
                    f"{previous!r} and "
                    f"{alternative.name!r}"
                )

            result[variant_name] = alternative.name

    missing = variant_names - result.keys()

    if missing:
        raise ValueError(
            f"{spec.opcode}: alternatives do not " f"cover variants {sorted(missing)}"
        )

    return result  # type: ignore


def build_storage_field_map(
    *,
    spec: InstructionSpec,
    backend: InstructionBackend,
    modifiers: tuple[ModifierFieldView, ...],
    storage_map: dict[str, str | None],
) -> dict[str, tuple[str, ...]]:
    if backend.emit.kind != "sub_variant":
        return {}

    modifier_fields = {modifier.spec_name: modifier.field for modifier in modifiers}

    fields_by_storage: dict[
        str,
        list[str],
    ] = {}

    for variant in spec.variants:
        storage_type = storage_map[variant.name]

        assert storage_type is not None

        fields = fields_by_storage.setdefault(
            storage_type,
            [],
        )

        for modifier in variant.modifiers:
            if modifier.presence == "absent":
                continue

            field = modifier_fields.get(modifier.name)

            if field is not None and field not in fields:
                fields.append(field)

    return {
        storage_type: tuple(fields)
        for storage_type, fields in fields_by_storage.items()
    }


# =============================================================================
# Variant view construction
# =============================================================================


def build_variant_views(
    *,
    database: CodegenDatabase,
    spec: InstructionSpec,
    modifiers: tuple[ModifierFieldView, ...],
    storage_map: dict[str, str | None],
    storage_fields: dict[str, tuple[str, ...]],
) -> tuple[ParserVariantView, ...]:
    result: list[ParserVariantView] = []

    modifier_by_name = {modifier.spec_name: modifier for modifier in modifiers}

    for variant in spec.variants:
        spec_modifiers = {modifier.name: modifier for modifier in variant.modifiers}

        constraints: list[VariantConstraintView] = []

        for name, field_view in modifier_by_name.items():
            modifier = spec_modifiers.get(name)

            if modifier is None:
                raise ValueError(
                    f"{spec.opcode}.{variant.name}: "
                    f"modifier {name!r} must explicitly "
                    "specify absent, optional, required "
                    "or fixed"
                )

            constraints.append(
                build_variant_constraint(
                    database=database,
                    opcode=spec.opcode,
                    variant_name=variant.name,
                    modifier=modifier,
                    field_view=field_view,
                )
            )

        storage_type = storage_map[variant.name]

        result.append(
            ParserVariantView(
                name=variant.name,
                enum_name=to_pascal_case(variant.name),
                predicate_name=("is_" + to_cpp_identifier(variant.name).lower()),
                constraints=tuple(constraints),
                **build_availability_view(
                    opcode=spec.opcode,
                    variant_name=variant.name,
                    availability=variant.availability,
                ),
                storage_type=storage_type,
                storage_fields=(
                    storage_fields.get(
                        storage_type,
                        (),
                    )
                    if storage_type is not None
                    else ()
                ),
            )
        )

    return tuple(result)


def build_availability_view(
    *,
    opcode: str,
    variant_name: str,
    availability: dict,
) -> dict[str, object]:
    raw_ptx = str(availability.get("ptx", "0.0"))
    parts = raw_ptx.split(".", maxsplit=1)

    try:
        major = int(parts[0])
        minor = int(parts[1]) if len(parts) == 2 else 0
        min_sm = int(availability.get("sm", 0))
    except ValueError as exc:
        raise ValueError(
            f"{opcode}.{variant_name}: invalid availability {availability!r}"
        ) from exc

    if (
        major < 0
        or minor < 0
        or min_sm < 0
        or major > 65535
        or minor > 65535
        or min_sm > 65535
    ):
        raise ValueError(
            f"{opcode}.{variant_name}: availability values must fit uint16"
        )

    return {
        "min_ptx_major": major,
        "min_ptx_minor": minor,
        "min_sm": min_sm,
        "family": str(availability.get("family", "")),
        "deprecated": bool(availability.get("deprecated", False)),
        "removed": bool(availability.get("removed", False)),
    }


def build_variant_constraint(
    *,
    database: CodegenDatabase,
    opcode: str,
    variant_name: str,
    modifier: ModifierSpec,
    field_view: ModifierFieldView,
) -> VariantConstraintView:
    presence = modifier.presence

    if presence not in {
        "absent",
        "optional",
        "required",
        "fixed",
    }:
        raise ValueError(
            f"{opcode}.{variant_name}."
            f"{modifier.name}: unsupported "
            f"presence {presence!r}"
        )

    if field_view.kind == "flag":
        fixed_cpp_value: str | None = None

        if presence == "fixed":
            if not isinstance(
                modifier.value,
                bool,
            ):
                raise ValueError(
                    f"{opcode}.{variant_name}."
                    f"{modifier.name}: fixed flag "
                    "requires a bool value"
                )

            fixed_cpp_value = "true" if modifier.value else "false"

        return VariantConstraintView(
            field=field_view.field,
            kind="flag",
            presence=presence,
            allowed_cpp_values=(),
            fixed_cpp_value=fixed_cpp_value,
        )

    assert field_view.domain_name is not None

    domain = database.domains[field_view.domain_name]

    allowed_cpp_values: tuple[str, ...] = ()

    if modifier.values:
        allowed_cpp_values = tuple(
            require_domain_cpp_value(
                values=domain.values,
                spelling=spelling,
                context=(f"{opcode}.{variant_name}." f"{modifier.name}"),
            )
            for spelling in modifier.values
        )

    fixed_cpp_value: str | None = None

    if presence == "fixed":
        if modifier.value is None:
            raise ValueError(
                f"{opcode}.{variant_name}."
                f"{modifier.name}: fixed domain "
                "modifier requires a value"
            )

        fixed_cpp_value = require_domain_cpp_value(
            values=domain.values,
            spelling=str(modifier.value),
            context=(f"{opcode}.{variant_name}." f"{modifier.name}"),
        )

    return VariantConstraintView(
        field=field_view.field,
        kind="domain",
        presence=presence,
        allowed_cpp_values=(allowed_cpp_values),
        fixed_cpp_value=fixed_cpp_value,
    )


def require_domain_cpp_value(
    *,
    values: dict[str, str],
    spelling: str,
    context: str,
) -> str:
    try:
        return values[spelling]
    except KeyError as exc:
        raise ValueError(
            f"{context}: spelling {spelling!r} " "is not present in the domain"
        ) from exc


# =============================================================================
# Operand view construction
# =============================================================================


def build_operand_views(
    *,
    spec: InstructionSpec,
    backend: InstructionBackend,
) -> tuple[ParserOperandView, ...]:
    if not spec.variants:
        raise ValueError(f"{spec.opcode}: no variants")

    canonical = spec.variants[0].operands

    canonical_signature = operand_signature(canonical)

    for variant in spec.variants[1:]:
        if operand_signature(variant.operands) != canonical_signature:
            raise ValueError(
                f"{spec.opcode}: all variants must "
                "currently use the same operand layout"
            )

    result: list[ParserOperandView] = []

    for operand in canonical:
        backend_operand = backend.operands.get(operand.name)

        if backend_operand is None:
            raise ValueError(
                f"{spec.opcode}.{operand.name}: " "missing operand backend mapping"
            )

        result.append(
            ParserOperandView(
                spec_name=operand.name,
                field=backend_operand.field,
                kind_cpp=operand_kind_cpp(
                    opcode=spec.opcode,
                    operand_name=operand.name,
                    kind=operand.kind,
                ),
            )
        )

    return tuple(result)


def operand_kind_cpp(*, opcode: str, operand_name: str, kind: str) -> str:
    mappings = {
        "reg": "OperandKind::Register",
        "pred": "OperandKind::Register",
        "sreg": "OperandKind::Register",
        "imm": "OperandKind::Immediate",
        "reg_or_imm": "OperandKind::RegisterOrImmediate",
        "addr": "OperandKind::Address",
        "addr_or_symbol": "OperandKind::AddressOrIdentifier",
        "label": "OperandKind::Identifier",
        "label_or_reg": "OperandKind::Identifier",
        "symbol": "OperandKind::Identifier",
        "func": "OperandKind::Identifier",
        "descriptor": "OperandKind::Identifier",
        "reg_list": "OperandKind::Vector",
        "pred_list": "OperandKind::Vector",
        "operand_list": "OperandKind::Vector",
        "vector": "OperandKind::Vector",
        "tuple": "OperandKind::Vector",
        "matrix_fragment": "OperandKind::Vector",
    }

    try:
        return mappings[kind]
    except KeyError as exc:
        raise ValueError(
            f"{opcode}.{operand_name}: parser does not support operand kind {kind!r}"
        ) from exc


def operand_signature(
    operands: tuple,
) -> tuple:
    return tuple(
        (
            operand.name,
            operand.kind,
            operand.role,
            operand.access,
            operand.type_expr,
        )
        for operand in operands
    )


def strip_dot(
    value: str | None,
) -> str | None:
    if value is None:
        return None

    if value.startswith("."):
        return value[1:]

    return value
