# python/code_gen/cpp/gen_ir.py

from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from jinja2 import Environment, FileSystemLoader, StrictUndefined

from code_gen.model import (
    CodegenUnit,
    DomainBackend,
    EmitBackend,
    InstructionBackend,
    InstructionSpec,
    ModifierSpec,
    OperandBackend,
    OperandSpec,
)

# -----------------------------------------------------------------------------
# IR-template-facing view model
# -----------------------------------------------------------------------------
#
# These classes are NOT the global normalized model.
#
# They are a small view specifically prepared for:
#
#     templates/ptx_ir_instr.gen.hpp.j2
#
# The point is:
#
#     CodegenUnit
#         semantic/backend model used by all generators
#
#     IrHeaderView
#         rendering model used only by gen_ir.py
#
# The Jinja2 template should consume this simple view, not the full CodegenUnit.
# -----------------------------------------------------------------------------


@dataclass(frozen=True)
class FieldView:
    name: str
    cpp_type: str
    default: str | None = None


@dataclass(frozen=True)
class DetailTypeView:
    name: str
    fields: tuple[FieldView, ...]


@dataclass(frozen=True)
class EmitView:
    kind: str
    instance: str | None = None
    type: str | None = None
    alternatives: tuple[str, ...] = ()


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
    includes: tuple[str, ...]
    detail_types: tuple[DetailTypeView, ...]
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
    """
    Generate the C++ IR instruction header.

    This function is the main entry point used by command-line scripts or CMake
    codegen commands.

    It intentionally does not load YAML and does not validate schemas.
    It assumes the caller already has a fully normalized CodegenUnit.

    Pipeline:

        CodegenUnit
            -> build_ir_header_view()
            -> render_ir_header()
            -> write output_path
    """

    view = build_ir_header_view(unit)
    content = render_ir_header(
        view=view,
        template_dir=template_dir,
        template_name=template_name,
    )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(content, encoding="utf-8")


def build_ir_header_view(unit: CodegenUnit) -> IrHeaderView:
    """
    Convert the common CodegenUnit into an IR-header-specific view.

    This is where IR-specific decisions are made:

    - which C++ detail structs should be emitted
    - which fields belong to each detail struct
    - how instruction operands are named in C++
    - whether instruction data is direct/sub_struct/sub_variant
    - which includes are required by the generated IR header
    """

    detail_types: dict[str, DetailTypeView] = {}
    instructions: list[InstructionView] = []

    for instr in unit.instructions:
        backend_instr = require_instruction_backend(unit, instr.opcode)

        emit_view = build_emit_view(backend_instr)

        if backend_instr.emit.kind in {"sub_struct", "sub_variant"}:
            detail_name = require_emit_type(backend_instr.emit, instr.opcode)

            if detail_name not in detail_types:
                detail_types[detail_name] = DetailTypeView(
                    name=detail_name,
                    fields=build_detail_fields(
                        instr=instr,
                        backend_instr=backend_instr,
                        domains=unit.domains,
                    ),
                )

        instructions.append(
            InstructionView(
                opcode=instr.opcode,
                cpp_type=backend_instr.cpp,
                emit=emit_view,
                operands=build_operand_views(
                    instr=instr,
                    backend_instr=backend_instr,
                ),
            )
        )

    return IrHeaderView(
        spec_schema="ptx-instr/v1",
        backend_schema="ptx-cpp-backend/v1",
        category=infer_category(unit),
        namespace=unit.namespace,
        includes=resolve_ir_includes(unit),
        detail_types=tuple(detail_types.values()),
        instructions=tuple(instructions),
    )


def render_ir_header(
    *,
    view: IrHeaderView,
    template_dir: Path,
    template_name: str = "ptx_ir_instr.gen.hpp.j2",
) -> str:
    """
    Render IrHeaderView with the Jinja2 template.

    The template should remain mostly C++-looking.
    Most semantic decisions should already have been made in build_ir_header_view().
    """

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
        detail_types=view.detail_types,
        instructions=view.instructions,
    )


# -----------------------------------------------------------------------------
# Instruction-level view construction
# -----------------------------------------------------------------------------


def require_instruction_backend(
    unit: CodegenUnit,
    opcode: str,
) -> InstructionBackend:
    try:
        return unit.backends[opcode]
    except KeyError as exc:
        raise ValueError(f"missing C++ backend mapping for opcode {opcode!r}") from exc


def build_emit_view(backend_instr: InstructionBackend) -> EmitView:
    emit = backend_instr.emit

    if emit.kind == "sub_variant":
        emit_type = require_emit_type(emit, backend_instr.opcode)
        return EmitView(
            kind="sub_variant",
            instance=require_emit_instance(emit, backend_instr.opcode),
            type=emit_type,
            alternatives=(emit_type,),
        )

    if emit.kind == "sub_struct":
        emit_type = require_emit_type(emit, backend_instr.opcode)
        return EmitView(
            kind="sub_struct",
            instance=require_emit_instance(emit, backend_instr.opcode),
            type=emit_type,
        )

    if emit.kind == "direct":
        return EmitView(kind="direct")

    if emit.kind == "custom":
        return EmitView(kind="custom")

    raise ValueError(f"{backend_instr.opcode}: unsupported emit kind {emit.kind!r}")


def require_emit_type(emit: EmitBackend, opcode: str) -> str:
    if emit.type is None:
        raise ValueError(f"{opcode}: emit kind {emit.kind!r} requires emit.type")
    return emit.type


def require_emit_instance(emit: EmitBackend, opcode: str) -> str:
    if emit.instance is None:
        raise ValueError(f"{opcode}: emit kind {emit.kind!r} requires emit.instance")
    return emit.instance


# -----------------------------------------------------------------------------
# Detail struct field construction
# -----------------------------------------------------------------------------


def build_detail_fields(
    *,
    instr: InstructionSpec,
    backend_instr: InstructionBackend,
    domains: dict[str, DomainBackend],
) -> tuple[FieldView, ...]:
    """
    Build fields for the detail struct used by sub_struct/sub_variant.

    For add with backend:

        emit:
          kind: sub_variant
          instance: data
          type: ArithInteger

        modifiers:
          sat:
            field: sat
            cpp_type: bool
            default: "false"

          type:
            field: type_
            cpp_type: ScalarType
            domain: scalar_types

    This produces:

        struct ArithInteger {
            bool sat = false;
            ScalarType type_ = ScalarType::U32;
        };
    """

    fields: dict[str, FieldView] = {}

    for modifier in unique_non_absent_modifiers(instr):
        field_name = resolve_modifier_field_name(
            modifier=modifier,
            backend_instr=backend_instr,
        )

        if field_name in fields:
            continue

        fields[field_name] = FieldView(
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

    return tuple(fields.values())


def unique_non_absent_modifiers(
    instr: InstructionSpec,
) -> tuple[ModifierSpec, ...]:
    """
    Return one representative ModifierSpec per modifier name.

    Variants may repeat the same modifier with different availability/type sets.
    For IR layout generation, we only need one field per modifier name.

    Example for add:

        variants:
          add_integer_no_sat:
            sat absent
            type required

          add_sat_s32:
            sat fixed
            type fixed

          add_packed_optional_sat_sm120:
            sat optional
            type required

    The resulting IR fields should still just be:

        sat
        type_

    So this function collects unique non-absent modifiers by name.
    """

    result: dict[str, ModifierSpec] = {}

    for variant in instr.variants:
        for modifier in variant.modifiers:
            if modifier.presence == "absent":
                continue

            if modifier.name not in result:
                result[modifier.name] = modifier

    return tuple(result.values())


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
    """
    Minimal C++ field-name fallback.

    The backend YAML should normally specify important rewrites, for example:

        type:
          field: type_

    This fallback is only for simple names.
    """

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
    # Not a C++ keyword, but commonly rewritten in this project/backend.
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
        domain = require_domain(domains, domain_name)
        return domain.cpp_type

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
# Operand view construction
# -----------------------------------------------------------------------------


def build_operand_views(
    *,
    instr: InstructionSpec,
    backend_instr: InstructionBackend,
) -> tuple[OperandView, ...]:
    """
    Build operand fields for the instruction struct.

    The normalized model from normalize.py should already have expanded
    instruction-level operand patterns into each VariantSpec.

    If all variants share the same operand shape, this returns that shape.
    If variants differ, this currently builds a union-by-name view.

    For int arith add, variants all share:

        dst, src1, src2

    So the generated instruction is:

        struct InstrAdd {
            ...
            Operand dst;
            Operand src1;
            Operand src2;
        };
    """

    operands = unique_operands(instr)

    result: list[OperandView] = []

    for operand in operands:
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
    """
    Collect instruction operands by name while preserving first-seen order.

    For the current int arithmetic model, all variants of one instruction should
    have the same operand list. This function is tolerant of repeated lists.

    Later, if an instruction has genuinely different operand forms, you may
    want to represent those as sub-variants instead of merging operands.
    """

    result: dict[str, OperandSpec] = {}

    for variant in instr.variants:
        for operand in variant.operands:
            if operand.name not in result:
                result[operand.name] = operand

    return tuple(result.values())


# -----------------------------------------------------------------------------
# Include/category helpers
# -----------------------------------------------------------------------------


def resolve_ir_includes(unit: CodegenUnit) -> tuple[str, ...]:
    """
    Current CodegenUnit model does not carry backend includes.

    If you later add includes to CodegenUnit or to a higher-level backend model,
    this function is the only place gen_ir.py needs to change.
    """

    required = {
        "<variant>",
        '"ptx_frontend/ir/operand.hpp"',
    }

    if uses_scalar_type(unit):
        required.add('"ptx_frontend/ir/scalar_type.hpp"')

    return tuple(sorted(required, key=include_sort_key))


def include_sort_key(include: str) -> tuple[int, str]:
    """
    Keep system includes before local includes.
    """

    if include.startswith("<"):
        return (0, include)

    return (1, include)


def uses_scalar_type(unit: CodegenUnit) -> bool:
    for domain in unit.domains.values():
        if domain.cpp_type == "ScalarType":
            return True

    for backend_instr in unit.backends.values():
        for modifier in backend_instr.modifiers.values():
            if modifier.cpp_type == "ScalarType":
                return True

    return False


def infer_category(unit: CodegenUnit) -> str:
    """
    The minimal CodegenUnit from the earlier one-file prototype does not carry
    category. For now infer it from the codegen invocation context if needed.

    Since the current unit only carries instructions/backends/domains/namespace,
    this returns a stable placeholder.

    If you add category to CodegenUnit later, replace this with:

        return unit.category
    """

    # For the current use-case.
    # This is intentionally centralized so it is easy to remove later.
    return "integer_arithmetic"


# -----------------------------------------------------------------------------
# Optional utility for tests/debugging
# -----------------------------------------------------------------------------


def render_ir_header_to_string(
    unit: CodegenUnit,
    *,
    template_dir: Path,
    template_name: str = "ptx_ir_instr.gen.hpp.j2",
) -> str:
    """
    Convenience wrapper for unit tests.

    Example:

        content = render_ir_header_to_string(unit, template_dir=...)
        assert "struct InstrAdd" in content
    """

    view = build_ir_header_view(unit)
    return render_ir_header(
        view=view,
        template_dir=template_dir,
        template_name=template_name,
    )


def iter_detail_type_names(unit: CodegenUnit) -> Iterable[str]:
    """
    Small debugging helper.
    """

    for instr in unit.instructions:
        backend_instr = require_instruction_backend(unit, instr.opcode)

        if backend_instr.emit.kind in {"sub_struct", "sub_variant"}:
            yield require_emit_type(backend_instr.emit, instr.opcode)


# -----------------------------------------------------------------------------
# script entry point for manual testing
# -----------------------------------------------------------------------------

import argparse
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

    unit = build_codegen_unit(args.spec, args.backend)

    generate_ir_header(
        unit,
        template_dir=Path(args.template_dir),
        output_path=Path(args.output),
    )


if __name__ == "__main__":
    main()
