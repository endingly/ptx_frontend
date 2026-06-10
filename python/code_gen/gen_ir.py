# python/code_gen/cpp/gen_ir.py

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from jinja2 import Environment, FileSystemLoader, StrictUndefined

from code_gen.model import (
    CodegenUnit,
    DomainBackend,
    EmitAlternativeBackend,
    EmitBackend,
    InstructionBackend,
    InstructionSpec,
    ModifierSpec,
    OperandSpec,
)

# -----------------------------------------------------------------------------
# Template-facing view model
# -----------------------------------------------------------------------------


@dataclass(frozen=True)
class FieldView:
    name: str
    cpp_type: str
    default: str | None = None


@dataclass(frozen=True)
class DataStructView:
    """
    A nested data struct inside one instruction.

    Example generated C++:

        struct InstrIntegerAdd {
            struct Data {
                bool sat = false;
                ScalarType type_ = ScalarType::U32;
            };

            Data data;
        };
    """

    name: str
    fields: tuple[FieldView, ...]


@dataclass(frozen=True)
class VariantAlternativeView:
    """
    A nested alternative type for sub_variant.

    Example generated C++:

        struct InstrFoo {
            struct IntData { ... };
            struct FloatData { ... };

            using Data = std::variant<IntData, FloatData>;
            Data data;
        };
    """

    name: str
    fields: tuple[FieldView, ...]


@dataclass(frozen=True)
class EmitView:
    kind: str
    instance: str | None = None

    # Used by direct and sub_struct.
    #
    # For direct:
    #     fields are emitted directly inside instruction struct.
    #
    # For sub_struct:
    #     data_struct is emitted as a nested struct.
    data_struct: DataStructView | None = None

    # Used by sub_variant.
    variant_type: str | None = None
    alternatives: tuple[VariantAlternativeView, ...] = ()


@dataclass(frozen=True)
class OperandView:
    name: str
    cpp_type: str


@dataclass(frozen=True)
class InstructionView:
    opcode: str
    cpp_type: str
    emit: EmitView
    operands: tuple[OperandView, ...]


@dataclass(frozen=True)
class IrHeaderView:
    spec_schema: str
    backend_schema: str
    category: str
    namespace: str
    includes: tuple[str, ...] | None
    instructions: tuple[InstructionView, ...]


# -----------------------------------------------------------------------------
# Public API
# -----------------------------------------------------------------------------


def generate_ir_header(
    unit: CodegenUnit,
    *,
    template_dir: Path,
    output_path: Path,
    template_name: str = "ptx_ir_instr.gen.hpp.j2",
) -> None:
    view = build_ir_header_view(unit)

    content = render_ir_header(
        view=view,
        template_dir=template_dir,
        template_name=template_name,
    )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(content, encoding="utf-8")


def add_external_includes(origin_includes: tuple[str, ...] | None) -> tuple[str, ...]:
    """Add includes required by the generated code that are not specified in the backend YAML."""
    external_includes = (
        "<numeric>",
        "<optional>",
        '"ptx_ir/base.hpp"',
        '"ptx_ir/details.hpp"',
        '"ptx_ir/source_loc.hpp"',
    )

    if origin_includes is None:
        return external_includes

    # Avoid duplicates while preserving order.
    seen = set()
    result = []

    for include in origin_includes + external_includes:
        if include not in seen:
            seen.add(include)
            result.append(include)

    return tuple(result)


def build_ir_header_view(unit: CodegenUnit) -> IrHeaderView:
    instructions: list[InstructionView] = []

    for instr in unit.instructions:
        backend_instr = require_instruction_backend(unit, instr.opcode)

        instructions.append(
            InstructionView(
                opcode=instr.opcode,
                cpp_type=backend_instr.cpp,
                emit=build_emit_view(
                    instr=instr,
                    backend_instr=backend_instr,
                    domains=unit.domains,
                ),
                operands=build_operand_views(
                    instr=instr,
                    backend_instr=backend_instr,
                ),
            )
        )

    return IrHeaderView(
        spec_schema=unit.spec_schema,
        backend_schema=unit.backend_schema,
        category=unit.category,
        namespace=unit.namespace,
        includes=add_external_includes(unit.includes),
        instructions=tuple(instructions),
    )


def render_ir_header(
    *,
    view: IrHeaderView,
    template_dir: Path,
    template_name: str,
) -> str:
    env = Environment(
        loader=FileSystemLoader(str(template_dir)),
        undefined=StrictUndefined,
        trim_blocks=True,
        lstrip_blocks=True,
    )

    template = env.get_template(template_name)

    return template.render(
        spec_schema=view.spec_schema,
        backend_schema=view.backend_schema,
        category=view.category,
        namespace=view.namespace,
        includes=view.includes,
        instructions=view.instructions,
    )


# -----------------------------------------------------------------------------
# Emit view construction
# -----------------------------------------------------------------------------


def build_emit_view(
    *,
    instr: InstructionSpec,
    backend_instr: InstructionBackend,
    domains: dict[str, DomainBackend],
) -> EmitView:
    emit = backend_instr.emit

    if emit.kind == "direct":
        return EmitView(
            kind="direct",
            data_struct=DataStructView(
                name="",
                fields=build_modifier_fields(
                    instr=instr,
                    backend_instr=backend_instr,
                    domains=domains,
                ),
            ),
        )

    if emit.kind == "sub_struct":
        instance = require_emit_instance(emit, backend_instr.opcode)
        data_type = require_emit_type(emit, backend_instr.opcode)

        return EmitView(
            kind="sub_struct",
            instance=instance,
            data_struct=DataStructView(
                name=data_type,
                fields=build_modifier_fields(
                    instr=instr,
                    backend_instr=backend_instr,
                    domains=domains,
                ),
            ),
        )

    if emit.kind == "sub_variant":
        instance = require_emit_instance(emit, backend_instr.opcode)
        variant_type = require_emit_type(emit, backend_instr.opcode)

        alternatives = tuple(
            build_variant_alternative_view(
                instr=instr,
                backend_instr=backend_instr,
                domains=domains,
                alternative=alternative,
            )
            for alternative in emit.alternatives
        )

        return EmitView(
            kind="sub_variant",
            instance=instance,
            variant_type=variant_type,
            alternatives=alternatives,
        )

    raise ValueError(f"{backend_instr.opcode}: unsupported emit kind {emit.kind!r}")


def build_variant_alternative_view(
    *,
    instr: InstructionSpec,
    backend_instr: InstructionBackend,
    domains: dict[str, DomainBackend],
    alternative: EmitAlternativeBackend,
) -> VariantAlternativeView:
    if alternative.variants:
        fields = build_modifier_fields_for_variants(
            instr=instr,
            variant_names=set(alternative.variants),
            backend_instr=backend_instr,
            domains=domains,
        )
    else:
        fields = build_modifier_fields(
            instr=instr,
            backend_instr=backend_instr,
            domains=domains,
        )

    return VariantAlternativeView(
        name=alternative.name,
        fields=fields,
    )


def require_emit_instance(emit: EmitBackend, opcode: str) -> str:
    if emit.instance is None:
        raise ValueError(f"{opcode}: emit kind {emit.kind!r} requires emit.instance")

    return emit.instance


def require_emit_type(emit: EmitBackend, opcode: str) -> str:
    """
    direct:
        unused

    sub_struct:
        emit.type names the nested data struct.

    sub_variant:
        emit.type names the std::variant alias.
    """

    if emit.type is None:
        raise ValueError(f"{opcode}: emit kind {emit.kind!r} requires emit.type")

    return emit.type


# -----------------------------------------------------------------------------
# Field construction
# -----------------------------------------------------------------------------


def build_modifier_fields(
    *,
    instr: InstructionSpec,
    backend_instr: InstructionBackend,
    domains: dict[str, DomainBackend],
) -> tuple[FieldView, ...]:
    fields_by_name: dict[str, FieldView] = {}

    for modifier in unique_non_absent_modifiers(instr):
        field_name = resolve_modifier_field_name(
            modifier=modifier,
            backend_instr=backend_instr,
        )

        if field_name in fields_by_name:
            continue

        fields_by_name[field_name] = FieldView(
            name=field_name,
            cpp_type=resolve_modifier_cpp_type(
                modifier=modifier,
                backend_instr=backend_instr,
                domains=domains,
            ),
            default=resolve_modifier_default(
                modifier=modifier,
                backend_instr=backend_instr,
                domains=domains,
            ),
        )

    return order_fields_by_backend_modifier_order(
        fields_by_name=fields_by_name,
        backend_instr=backend_instr,
    )


def build_modifier_fields_for_variants(
    *,
    instr: InstructionSpec,
    variant_names: set[str],
    backend_instr: InstructionBackend,
    domains: dict[str, DomainBackend],
) -> tuple[FieldView, ...]:
    fields_by_name: dict[str, FieldView] = {}

    for variant in instr.variants:
        if variant.name not in variant_names:
            continue

        for modifier in variant.modifiers:
            if modifier.presence == "absent":
                continue

            field_name = resolve_modifier_field_name(
                modifier=modifier,
                backend_instr=backend_instr,
            )

            if field_name in fields_by_name:
                continue

            fields_by_name[field_name] = FieldView(
                name=field_name,
                cpp_type=resolve_modifier_cpp_type(
                    modifier=modifier,
                    backend_instr=backend_instr,
                    domains=domains,
                ),
                default=resolve_modifier_default(
                    modifier=modifier,
                    backend_instr=backend_instr,
                    domains=domains,
                ),
            )

    return order_fields_by_backend_modifier_order(
        fields_by_name=fields_by_name,
        backend_instr=backend_instr,
    )


def unique_non_absent_modifiers(
    instr: InstructionSpec,
) -> tuple[ModifierSpec, ...]:
    result: dict[str, ModifierSpec] = {}

    for variant in instr.variants:
        for modifier in variant.modifiers:
            if modifier.presence == "absent":
                continue

            if modifier.name not in result:
                result[modifier.name] = modifier

    return tuple(result.values())


def order_fields_by_backend_modifier_order(
    *,
    fields_by_name: dict[str, FieldView],
    backend_instr: InstructionBackend,
) -> tuple[FieldView, ...]:
    ordered: list[FieldView] = []
    emitted: set[str] = set()

    # Prefer backend YAML modifier order.
    for modifier_backend in backend_instr.modifiers.values():
        field = fields_by_name.get(modifier_backend.field)
        if field is None:
            continue

        ordered.append(field)
        emitted.add(field.name)

    # Add any remaining fields in discovery order.
    for field in fields_by_name.values():
        if field.name not in emitted:
            ordered.append(field)

    return tuple(ordered)


def resolve_modifier_field_name(
    *,
    modifier: ModifierSpec,
    backend_instr: InstructionBackend,
) -> str:
    backend_modifier = backend_instr.modifiers.get(modifier.name)

    if backend_modifier is not None:
        return backend_modifier.field

    return default_cpp_field_name(modifier.name)


def default_cpp_field_name(name: str) -> str:
    if name in CPP_KEYWORDS:
        return f"{name}_"

    return name


CPP_KEYWORDS = {
    "alignas",
    "alignof",
    "and",
    "and_eq",
    "asm",
    "auto",
    "bitand",
    "bitor",
    "bool",
    "break",
    "case",
    "catch",
    "char",
    "char8_t",
    "char16_t",
    "char32_t",
    "class",
    "compl",
    "concept",
    "const",
    "consteval",
    "constexpr",
    "constinit",
    "const_cast",
    "continue",
    "co_await",
    "co_return",
    "co_yield",
    "decltype",
    "default",
    "delete",
    "do",
    "double",
    "dynamic_cast",
    "else",
    "enum",
    "explicit",
    "export",
    "extern",
    "false",
    "float",
    "for",
    "friend",
    "goto",
    "if",
    "inline",
    "int",
    "long",
    "mutable",
    "namespace",
    "new",
    "noexcept",
    "not",
    "not_eq",
    "nullptr",
    "operator",
    "or",
    "or_eq",
    "private",
    "protected",
    "public",
    "register",
    "reinterpret_cast",
    "requires",
    "return",
    "short",
    "signed",
    "sizeof",
    "static",
    "static_assert",
    "static_cast",
    "struct",
    "switch",
    "template",
    "this",
    "thread_local",
    "throw",
    "true",
    "try",
    "typedef",
    "typeid",
    "typename",
    "union",
    "unsigned",
    "using",
    "virtual",
    "void",
    "volatile",
    "wchar_t",
    "while",
    "xor",
    "xor_eq",
    # Not a C++ keyword, but commonly rewritten in PTX backends.
    "type",
}


def resolve_modifier_cpp_type(
    *,
    modifier: ModifierSpec,
    backend_instr: InstructionBackend,
    domains: dict[str, DomainBackend],
) -> str:
    backend_modifier = backend_instr.modifiers.get(modifier.name)

    if backend_modifier is not None and backend_modifier.cpp_type is not None:
        return backend_modifier.cpp_type

    if modifier.kind == "flag":
        return "bool"

    domain_name = resolve_modifier_domain(
        modifier=modifier,
        backend_instr=backend_instr,
    )

    if domain_name is not None:
        return require_domain(domains, domain_name).cpp_type

    raise ValueError(
        f"{backend_instr.opcode}.{modifier.name}: cannot infer C++ type; "
        "please specify cpp_type or domain in backend mapping"
    )


def resolve_modifier_default(
    *,
    modifier: ModifierSpec,
    backend_instr: InstructionBackend,
    domains: dict[str, DomainBackend],
) -> str | None:
    backend_modifier = backend_instr.modifiers.get(modifier.name)

    if backend_modifier is not None and backend_modifier.default is not None:
        return backend_modifier.default

    if modifier.kind == "flag":
        return resolve_flag_default(modifier)

    if modifier.presence == "fixed":
        return resolve_fixed_modifier_value(
            modifier=modifier,
            backend_instr=backend_instr,
            domains=domains,
        )

    domain_name = resolve_modifier_domain(
        modifier=modifier,
        backend_instr=backend_instr,
    )

    if domain_name is not None:
        domain = require_domain(domains, domain_name)
        if domain.default is not None:
            return domain.default

    return None


def resolve_flag_default(modifier: ModifierSpec) -> str:
    if modifier.presence == "fixed":
        return "true" if modifier.value is True else "false"

    if isinstance(modifier.default, bool):
        return "true" if modifier.default else "false"

    return "false"


def resolve_fixed_modifier_value(
    *,
    modifier: ModifierSpec,
    backend_instr: InstructionBackend,
    domains: dict[str, DomainBackend],
) -> str:
    if modifier.value is None:
        raise ValueError(
            f"{backend_instr.opcode}.{modifier.name}: fixed modifier has no value"
        )

    if isinstance(modifier.value, bool):
        return "true" if modifier.value else "false"

    domain_name = resolve_modifier_domain(
        modifier=modifier,
        backend_instr=backend_instr,
    )

    if domain_name is None:
        return str(modifier.value)

    domain = require_domain(domains, domain_name)
    value_key = str(modifier.value)

    try:
        return domain.values[value_key]
    except KeyError as exc:
        raise ValueError(
            f"{backend_instr.opcode}.{modifier.name}: value {value_key!r} "
            f"not found in backend domain {domain_name!r}"
        ) from exc


def resolve_modifier_domain(
    *,
    modifier: ModifierSpec,
    backend_instr: InstructionBackend,
) -> str | None:
    backend_modifier = backend_instr.modifiers.get(modifier.name)

    if backend_modifier is not None and backend_modifier.domain is not None:
        return backend_modifier.domain

    return modifier.domain


def require_domain(
    domains: dict[str, DomainBackend],
    domain_name: str,
) -> DomainBackend:
    try:
        return domains[domain_name]
    except KeyError as exc:
        raise ValueError(f"unknown backend domain {domain_name!r}") from exc


# -----------------------------------------------------------------------------
# Operand construction
# -----------------------------------------------------------------------------


def build_operand_views(
    *,
    instr: InstructionSpec,
    backend_instr: InstructionBackend,
) -> tuple[OperandView, ...]:
    result: list[OperandView] = []

    for operand in unique_operands(instr):
        backend_operand = backend_instr.operands.get(operand.name)

        if backend_operand is None:
            result.append(
                OperandView(
                    name=operand.name,
                    cpp_type="Operand",
                )
            )
            continue

        result.append(
            OperandView(
                name=backend_operand.field,
                cpp_type=backend_operand.cpp_type,
            )
        )

    return tuple(result)


def unique_operands(instr: InstructionSpec) -> tuple[OperandSpec, ...]:
    result: dict[str, OperandSpec] = {}

    for variant in instr.variants:
        for operand in variant.operands:
            if operand.name not in result:
                result[operand.name] = operand

    return tuple(result.values())


# -----------------------------------------------------------------------------
# Backend lookup
# -----------------------------------------------------------------------------


def require_instruction_backend(
    unit: CodegenUnit,
    opcode: str,
) -> InstructionBackend:
    try:
        return unit.backends[opcode]
    except KeyError as exc:
        raise ValueError(f"missing C++ backend mapping for opcode {opcode!r}") from exc


# -----------------------------------------------------------------------------
# Test/debug helper
# -----------------------------------------------------------------------------


def render_ir_header_to_string(
    unit: CodegenUnit,
    *,
    template_dir: Path,
    template_name: str = "ptx_ir_instr.gen.hpp.j2",
) -> str:
    view = build_ir_header_view(unit)

    return render_ir_header(
        view=view,
        template_dir=template_dir,
        template_name=template_name,
    )


# -----------------------------------------------------------------------------
# script entry point for manual testing
# -----------------------------------------------------------------------------

import argparse

from base.utils import format_file_inplace
from code_gen.normalize import build_codegen_unit


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec", required=True)
    parser.add_argument("--backend", required=True)
    parser.add_argument("--template-dir", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    # validate_all(raw_spec, raw_backend)
    # validate_semantics(raw_spec, raw_backend)

    unit = build_codegen_unit(Path(args.spec), Path(args.backend))

    generate_ir_header(
        unit,
        template_dir=Path(args.template_dir),
        output_path=Path(args.output),
    )

    format_file_inplace(args.output)


if __name__ == "__main__":
    main()
