# python/code_gen/gen_parser.py

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re

from jinja2 import Environment, FileSystemLoader, StrictUndefined

from code_gen.model import (
    CodegenUnit,
    DomainBackend,
    InstructionBackend,
    InstructionSpec,
    ModifierBackend,
    ModifierSpec,
    OperandSpec,
    VariantSpec,
)

# -----------------------------------------------------------------------------
# Template-facing view model
# -----------------------------------------------------------------------------


@dataclass(frozen=True)
class ParserFieldView:
    name: str
    cpp_type: str


@dataclass(frozen=True)
class ParserOperandView:
    name: str
    cpp_type: str


@dataclass(frozen=True)
class ParserDomainValueView:
    spelling: str
    cpp_expr: str


@dataclass(frozen=True)
class ParserDomainView:
    name: str
    func_name: str
    cpp_type: str
    values: tuple[ParserDomainValueView, ...]


@dataclass(frozen=True)
class ParserVariantView:
    name: str
    func_suffix: str
    match_body: str


@dataclass(frozen=True)
class ParserInstructionView:
    opcode: str
    cpp_type: str
    parse_func: str
    result_type: str
    data_instance: str | None
    field_assign_prefix: str
    fields: tuple[ParserFieldView, ...]
    operands: tuple[ParserOperandView, ...]
    variants: tuple[ParserVariantView, ...]


@dataclass(frozen=True)
class OpcodeGroupView:
    opcode: str
    candidates: tuple[ParserInstructionView, ...]


@dataclass(frozen=True)
class ParserHeaderView:
    namespace: str
    std_includes: tuple[str, ...]
    project_includes: tuple[str, ...]
    domains: tuple[ParserDomainView, ...]
    instructions: tuple[ParserInstructionView, ...]
    opcode_groups: tuple[OpcodeGroupView, ...]


PARSER_STD_INCLUDES = (
    "optional",
    "string",
    "string_view",
    "utility",
    "variant",
)

PARSER_PROJECT_INCLUDES = (
    "ptx_parser_core.hpp",
    "ptx_ir_instr.gen.hpp",
)


# -----------------------------------------------------------------------------
# Public API
# -----------------------------------------------------------------------------


def generate_parser_header(
    unit: CodegenUnit,
    *,
    template_dir: Path,
    output_path: Path,
    template_name: str = "ptx_instr_parser.gen.hpp.j2",
) -> None:
    view = build_parser_header_view(unit)

    content = render_parser_header(
        view=view,
        template_dir=template_dir,
        template_name=template_name,
    )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(content, encoding="utf-8")


def render_parser_header(
    *,
    view: ParserHeaderView,
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
        namespace=view.namespace,
        std_includes=view.std_includes,
        project_includes=view.project_includes,
        domains=view.domains,
        instructions=view.instructions,
        opcode_groups=view.opcode_groups,
    )


def build_parser_header_view(unit: CodegenUnit) -> ParserHeaderView:
    instructions: list[ParserInstructionView] = []

    for instr in unit.instructions:
        backend_instr = require_instruction_backend(unit, instr.opcode)
        instructions.append(
            build_parser_instruction_view(
                instr=instr,
                backend_instr=backend_instr,
                domains=unit.domains,
            )
        )

    opcode_groups = build_opcode_groups(instructions)
    domains = build_parser_domain_views(unit)

    return ParserHeaderView(
        namespace="ptx_frontend::generated",
        std_includes=PARSER_STD_INCLUDES,
        project_includes=PARSER_PROJECT_INCLUDES,
        domains=domains,
        instructions=tuple(instructions),
        opcode_groups=opcode_groups,
    )


# -----------------------------------------------------------------------------
# View construction
# -----------------------------------------------------------------------------


def build_parser_instruction_view(
    *,
    instr: InstructionSpec,
    backend_instr: InstructionBackend,
    domains: dict[str, DomainBackend],
) -> ParserInstructionView:
    data_instance = resolve_data_instance(backend_instr)

    fields = build_parser_field_views(
        instr=instr,
        backend_instr=backend_instr,
        domains=domains,
    )

    operands = build_parser_operand_views(
        instr=instr,
        backend_instr=backend_instr,
    )

    variants = tuple(
        build_parser_variant_view(
            variant=variant,
            backend_instr=backend_instr,
            domains=domains,
        )
        for variant in instr.variants
    )

    field_assign_prefix = f"instr.{data_instance}." if data_instance else "instr."

    return ParserInstructionView(
        opcode=instr.opcode,
        cpp_type=backend_instr.cpp,
        parse_func=f"tryParse{backend_instr.cpp}",
        result_type=f"{backend_instr.cpp}ParseResult",
        data_instance=data_instance,
        field_assign_prefix=field_assign_prefix,
        fields=fields,
        operands=operands,
        variants=variants,
    )


def build_parser_field_views(
    *,
    instr: InstructionSpec,
    backend_instr: InstructionBackend,
    domains: dict[str, DomainBackend],
) -> tuple[ParserFieldView, ...]:
    fields_by_name: dict[str, ParserFieldView] = {}

    for modifier in unique_non_absent_modifiers(instr):
        field_name = resolve_modifier_field_name(
            modifier=modifier,
            backend_instr=backend_instr,
        )

        if field_name in fields_by_name:
            continue

        cpp_type = resolve_modifier_cpp_type(
            modifier=modifier,
            backend_instr=backend_instr,
            domains=domains,
        )

        fields_by_name[field_name] = ParserFieldView(
            name=field_name,
            cpp_type=cpp_type,
        )

    return order_fields_by_backend_modifier_order(
        fields_by_name=fields_by_name,
        backend_instr=backend_instr,
    )


def build_parser_operand_views(
    *,
    instr: InstructionSpec,
    backend_instr: InstructionBackend,
) -> tuple[ParserOperandView, ...]:
    result: list[ParserOperandView] = []

    for operand in unique_operands(instr):
        backend_operand = backend_instr.operands.get(operand.name)

        if backend_operand is None:
            result.append(
                ParserOperandView(
                    name=operand.name,
                    cpp_type="Operand",
                )
            )
            continue

        result.append(
            ParserOperandView(
                name=backend_operand.field,
                cpp_type=backend_operand.cpp_type,
            )
        )

    return tuple(result)


def build_parser_variant_view(
    *,
    variant: VariantSpec,
    backend_instr: InstructionBackend,
    domains: dict[str, DomainBackend],
) -> ParserVariantView:
    return ParserVariantView(
        name=variant.name,
        func_suffix=to_cpp_identifier(variant.name),
        match_body=build_variant_match_body(
            variant=variant,
            backend_instr=backend_instr,
            domains=domains,
        ),
    )


def build_parser_domain_views(unit: CodegenUnit) -> tuple[ParserDomainView, ...]:
    used_domains = collect_used_domains(unit)
    result: list[ParserDomainView] = []

    for domain_name in sorted(used_domains):
        domain = require_domain(unit.domains, domain_name)

        values = tuple(
            ParserDomainValueView(
                spelling=spelling,
                cpp_expr=cpp_expr,
            )
            for spelling, cpp_expr in domain.values.items()
        )

        result.append(
            ParserDomainView(
                name=domain_name,
                func_name=f"parse{to_pascal_case(domain_name)}Modifier",
                cpp_type=domain.cpp_type,
                values=values,
            )
        )

    return tuple(result)


def build_opcode_groups(
    instructions: list[ParserInstructionView],
) -> tuple[OpcodeGroupView, ...]:
    groups: dict[str, list[ParserInstructionView]] = {}

    for instr in instructions:
        groups.setdefault(instr.opcode, []).append(instr)

    return tuple(
        OpcodeGroupView(
            opcode=opcode,
            candidates=tuple(candidates),
        )
        for opcode, candidates in groups.items()
    )


# -----------------------------------------------------------------------------
# Matcher body generation
# -----------------------------------------------------------------------------


def build_variant_match_body(
    *,
    variant: VariantSpec,
    backend_instr: InstructionBackend,
    domains: dict[str, DomainBackend],
) -> str:
    lines: list[str] = []
    indent = "  "

    non_absent_modifiers = [
        modifier for modifier in variant.modifiers if modifier.presence != "absent"
    ]

    all_modifiers = list(variant.modifiers)

    lines.append(f"{indent}{backend_instr.cpp}ParseResult result;")
    lines.append("")

    for modifier in non_absent_modifiers:
        field = resolve_modifier_field_name(
            modifier=modifier,
            backend_instr=backend_instr,
        )
        lines.append(f"{indent}bool seen_{field} = false;")

    if non_absent_modifiers:
        lines.append("")

    lines.append(f"{indent}for (const auto& mod : opcode.modifiers) {{")

    for modifier in all_modifiers:
        append_modifier_match_case(
            lines=lines,
            modifier=modifier,
            backend_instr=backend_instr,
            domains=domains,
            indent=indent + "  ",
        )

    lines.append(f"{indent}  return std::nullopt;")
    lines.append(f"{indent}}}")
    lines.append("")

    for modifier in non_absent_modifiers:
        if modifier.presence in ("required", "fixed"):
            field = resolve_modifier_field_name(
                modifier=modifier,
                backend_instr=backend_instr,
            )
            lines.append(f"{indent}if (!seen_{field}) {{")
            lines.append(f"{indent}  return std::nullopt;")
            lines.append(f"{indent}}}")
            lines.append("")

    lines.append(f"{indent}return result;")

    return "\n".join(lines)


def append_modifier_match_case(
    *,
    lines: list[str],
    modifier: ModifierSpec,
    backend_instr: InstructionBackend,
    domains: dict[str, DomainBackend],
    indent: str,
) -> None:
    if modifier.kind == "flag":
        append_flag_modifier_case(
            lines=lines,
            modifier=modifier,
            backend_instr=backend_instr,
            indent=indent,
        )
        return

    domain_name = resolve_modifier_domain(
        modifier=modifier,
        backend_instr=backend_instr,
    )

    if domain_name is not None:
        append_domain_modifier_case(
            lines=lines,
            modifier=modifier,
            backend_instr=backend_instr,
            domains=domains,
            domain_name=domain_name,
            indent=indent,
        )
        return

    raise ValueError(
        f"{backend_instr.opcode}.{modifier.name}: unsupported modifier kind "
        f"{modifier.kind!r}; expected flag or domain-backed modifier"
    )


def append_flag_modifier_case(
    *,
    lines: list[str],
    modifier: ModifierSpec,
    backend_instr: InstructionBackend,
    indent: str,
) -> None:
    token_name = modifier_token_name(modifier)
    field = resolve_modifier_field_name(
        modifier=modifier,
        backend_instr=backend_instr,
    )

    lines.append(f'{indent}if (mod.name == "{token_name}") {{')

    if modifier.presence == "absent":
        lines.append(f"{indent}  return std::nullopt;")
        lines.append(f"{indent}}}")
        lines.append("")
        return

    lines.append(f"{indent}  if (seen_{field}) {{")
    lines.append(f"{indent}    return std::nullopt;")
    lines.append(f"{indent}  }}")
    lines.append(f"{indent}  result.{field} = WithLoc<bool>{{true, mod.range}};")
    lines.append(f"{indent}  seen_{field} = true;")
    lines.append(f"{indent}  continue;")
    lines.append(f"{indent}}}")
    lines.append("")


def append_domain_modifier_case(
    *,
    lines: list[str],
    modifier: ModifierSpec,
    backend_instr: InstructionBackend,
    domains: dict[str, DomainBackend],
    domain_name: str,
    indent: str,
) -> None:
    field = resolve_modifier_field_name(
        modifier=modifier,
        backend_instr=backend_instr,
    )
    cpp_type = resolve_modifier_cpp_type(
        modifier=modifier,
        backend_instr=backend_instr,
        domains=domains,
    )
    parse_func = f"parse{to_pascal_case(domain_name)}Modifier"

    lines.append(f"{{")
    lines.append(f"{indent}auto parsed = {parse_func}(mod.name);")
    lines.append(f"{indent}if (parsed) {{")

    if modifier.presence == "absent":
        lines.append(f"{indent}  return std::nullopt;")
        lines.append(f"{indent}}}")
        lines.append(f"}}")
        lines.append("")
        return

    allowed_values = modifier.values

    if modifier.presence == "fixed":
        if modifier.value is None:
            raise ValueError(
                f"{backend_instr.opcode}.{modifier.name}: fixed modifier has no value"
            )
        allowed_values = (str(modifier.value),)

    if allowed_values:
        lines.append(f"{indent}  switch (*parsed) {{")
        for spelling in allowed_values:
            cpp_expr = resolve_domain_value_expr(
                domains=domains,
                domain_name=domain_name,
                spelling=spelling,
                opcode=backend_instr.opcode,
                modifier_name=modifier.name,
            )
            lines.append(f"{indent}    case {cpp_expr}:")
        lines.append(f"{indent}      break;")
        lines.append(f"{indent}    default:")
        lines.append(f"{indent}      return std::nullopt;")
        lines.append(f"{indent}  }}")

    lines.append(f"{indent}  if (seen_{field}) {{")
    lines.append(f"{indent}    return std::nullopt;")
    lines.append(f"{indent}  }}")
    lines.append(
        f"{indent}  result.{field} = WithLoc<{cpp_type}>{{*parsed, mod.range}};"
    )
    lines.append(f"{indent}  seen_{field} = true;")
    lines.append(f"{indent}  continue;")
    lines.append(f"{indent}}}")
    lines.append(f"}}")
    lines.append("")


# -----------------------------------------------------------------------------
# Model helpers
# -----------------------------------------------------------------------------


def resolve_data_instance(backend_instr: InstructionBackend) -> str | None:
    emit = backend_instr.emit

    if emit.kind == "direct":
        return None

    if emit.kind == "sub_struct":
        if emit.instance is None:
            raise ValueError(
                f"{backend_instr.opcode}: emit kind 'sub_struct' requires instance"
            )
        return emit.instance

    raise ValueError(
        f"{backend_instr.opcode}: parser generator currently supports "
        f"emit.kind direct/sub_struct only, got {emit.kind!r}"
    )


def unique_non_absent_modifiers(instr: InstructionSpec) -> tuple[ModifierSpec, ...]:
    result: dict[str, ModifierSpec] = {}

    for variant in instr.variants:
        for modifier in variant.modifiers:
            if modifier.presence == "absent":
                continue
            if modifier.name not in result:
                result[modifier.name] = modifier

    return tuple(result.values())


def unique_operands(instr: InstructionSpec) -> tuple[OperandSpec, ...]:
    result: dict[str, OperandSpec] = {}

    for variant in instr.variants:
        for operand in variant.operands:
            if operand.name not in result:
                result[operand.name] = operand

    return tuple(result.values())


def order_fields_by_backend_modifier_order(
    *,
    fields_by_name: dict[str, ParserFieldView],
    backend_instr: InstructionBackend,
) -> tuple[ParserFieldView, ...]:
    ordered: list[ParserFieldView] = []
    emitted: set[str] = set()

    for modifier_backend in backend_instr.modifiers.values():
        field = fields_by_name.get(modifier_backend.field)
        if field is None:
            continue
        ordered.append(field)
        emitted.add(field.name)

    for field in fields_by_name.values():
        if field.name not in emitted:
            ordered.append(field)

    return tuple(ordered)


def collect_used_domains(unit: CodegenUnit) -> set[str]:
    result: set[str] = set()

    for instr in unit.instructions:
        backend_instr = require_instruction_backend(unit, instr.opcode)

        for variant in instr.variants:
            for modifier in variant.modifiers:
                domain_name = resolve_modifier_domain(
                    modifier=modifier,
                    backend_instr=backend_instr,
                )
                if domain_name is not None:
                    result.add(domain_name)

    return result


def resolve_modifier_field_name(
    *,
    modifier: ModifierSpec,
    backend_instr: InstructionBackend,
) -> str:
    backend_modifier = backend_instr.modifiers.get(modifier.name)

    if backend_modifier is not None:
        return backend_modifier.field

    return default_cpp_field_name(modifier.name)


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


def resolve_modifier_domain(
    *,
    modifier: ModifierSpec,
    backend_instr: InstructionBackend,
) -> str | None:
    backend_modifier = backend_instr.modifiers.get(modifier.name)

    if backend_modifier is not None and backend_modifier.domain is not None:
        return backend_modifier.domain

    return modifier.domain


def modifier_token_name(modifier: ModifierSpec) -> str:
    if modifier.token is not None:
        return modifier.token[1:] if modifier.token.startswith(".") else modifier.token

    return modifier.name


def resolve_domain_value_expr(
    *,
    domains: dict[str, DomainBackend],
    domain_name: str,
    spelling: str,
    opcode: str,
    modifier_name: str,
) -> str:
    domain = require_domain(domains, domain_name)

    try:
        return domain.values[spelling]
    except KeyError as exc:
        raise ValueError(
            f"{opcode}.{modifier_name}: value {spelling!r} not found in "
            f"backend domain {domain_name!r}"
        ) from exc


def require_instruction_backend(
    unit: CodegenUnit,
    opcode: str,
) -> InstructionBackend:
    try:
        return unit.backends[opcode]
    except KeyError as exc:
        raise ValueError(f"missing C++ backend mapping for opcode {opcode!r}") from exc


def require_domain(
    domains: dict[str, DomainBackend],
    domain_name: str,
) -> DomainBackend:
    try:
        return domains[domain_name]
    except KeyError as exc:
        raise ValueError(f"unknown backend domain {domain_name!r}") from exc


# -----------------------------------------------------------------------------
# Name helpers
# -----------------------------------------------------------------------------


def default_cpp_field_name(name: str) -> str:
    if name in CPP_KEYWORDS:
        return f"{name}_"
    return name


def to_pascal_case(name: str) -> str:
    parts = re.split(r"[^A-Za-z0-9]+", name)
    return "".join(part[:1].upper() + part[1:] for part in parts if part)


def to_cpp_identifier(name: str) -> str:
    ident = re.sub(r"[^A-Za-z0-9_]", "_", name)
    if not ident:
        return "_"
    if ident[0].isdigit():
        ident = "_" + ident
    return ident


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
    "type",
}


# main function for testing
from pathlib import Path
import argparse
from base.utils import format_file_inplace
from code_gen.normalize import build_codegen_unit
from code_gen.gen_parser import generate_parser_header


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec", required=True)
    parser.add_argument("--backend", required=True)
    parser.add_argument("--template-dir", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    unit = build_codegen_unit(Path(args.spec), Path(args.backend))

    generate_parser_header(
        unit,
        template_dir=Path(args.template_dir),
        output_path=Path(args.output),
    )

    format_file_inplace(args.output)


if __name__ == "__main__":
    main()
