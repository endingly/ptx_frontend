from pathlib import Path
from code_gen.load_yaml import load_yaml, expand_value_refs
from code_gen.model import *


def normalize_operand(raw: dict[str, Any]) -> OperandSpec:
    """Normalize a raw operand specification."""
    type_expr = None
    raw_type = raw.get("type")
    if isinstance(raw_type, dict):
        type_expr = raw_type.get("expr")
    elif isinstance(raw_type, str):
        type_expr = raw_type

    return OperandSpec(
        name=raw["name"],
        kind=raw["kind"],
        role=raw.get("role"),
        access=raw.get("access"),
        type_expr=type_expr,
    )


def normalize_modifier(
    raw: dict[str, Any], type_sets: dict[str, list[str]]
) -> ModifierSpec:
    """Normalize a raw modifier specification."""
    return ModifierSpec(
        name=raw["name"],
        kind=raw["kind"],
        presence=raw["presence"],
        domain=raw.get("domain"),
        values=expand_value_refs(raw.get("values", []), type_sets),
        value=raw.get("value"),
        token=raw.get("token"),
        default=raw.get("default"),
    )


def normalize_instruction_spec(spec: dict[str, Any]) -> tuple[InstructionSpec, ...]:
    type_sets = spec.get("type_sets", {})
    operand_patterns = spec.get("operand_patterns", {})

    instructions: list[InstructionSpec] = []

    for raw_instr in spec["instructions"]:
        default_operands_raw = raw_instr.get("operands")
        variants: list[VariantSpec] = []

        for raw_var in raw_instr["variants"]:
            operands_raw = raw_var.get("operands", default_operands_raw)

            if isinstance(operands_raw, str):
                operands_raw = operand_patterns[operands_raw]

            operands = tuple(normalize_operand(x) for x in operands_raw)

            modifiers = tuple(
                normalize_modifier(x, type_sets)
                for x in raw_var.get("modifiers", [])
                if x.get("presence") != "absent"
            )

            variants.append(
                VariantSpec(
                    name=raw_var["name"],
                    availability=raw_var["availability"],
                    modifiers=modifiers,
                    operands=operands,
                    rule=raw_var.get("rule"),
                )
            )

        instructions.append(
            InstructionSpec(
                opcode=raw_instr["opcode"],
                syntax=raw_instr.get("syntax"),
                variants=tuple(variants),
            )
        )

    return tuple(instructions)


def normalize_backend(
    backend: dict[str, Any],
) -> tuple[str, dict[str, DomainBackend], dict[str, InstructionBackend]]:
    namespace = backend.get("namespace", "ptx_frontend")

    domains: dict[str, DomainBackend] = {}
    for name, raw_domain in backend.get("domains", {}).items():
        values: dict[str, str] = {}

        for k, v in raw_domain["values"].items():
            if isinstance(v, str):
                values[k] = v
            else:
                values[k] = v["cpp"]

        domains[name] = DomainBackend(
            cpp_type=raw_domain["cpp_type"],
            values=values,
            default=raw_domain.get("default"),
        )

    common = backend.get("common", {})
    keyword_rewrites = common.get("keyword_field_rewrites", {})

    instr_backends: dict[str, InstructionBackend] = {}

    for opcode, raw in backend["instructions"].items():
        emit_raw = raw["emit"]

        modifiers: dict[str, ModifierBackend] = {}
        for name, raw_mod in raw.get("modifiers", {}).items():
            modifiers[name] = ModifierBackend(
                field=raw_mod.get("field", keyword_rewrites.get(name, name)),
                cpp_type=raw_mod.get("cpp_type"),
                domain=raw_mod.get("domain"),
                default=raw_mod.get("default"),
                optional_policy=raw_mod.get("optional_policy"),
            )

        operands: dict[str, OperandBackend] = {}
        for name, raw_op in raw.get("operands", {}).items():
            operands[name] = OperandBackend(
                field=raw_op.get("field", name),
                cpp_type=raw_op.get("cpp_type", "Operand"),
            )

        type_checker_rule = None
        if "type_checker" in raw:
            type_checker_rule = raw["type_checker"].get("rule")

        visitor_name = None
        if "visitor" in raw:
            visitor_name = raw["visitor"].get("visit_name")

        printer = raw.get("printer", {})

        instr_backends[opcode] = InstructionBackend(
            opcode=opcode,
            cpp=raw["cpp"],
            emit=EmitBackend(
                kind=emit_raw["kind"],
                instance=emit_raw.get("instance"),
                type=emit_raw.get("type"),
            ),
            modifiers=modifiers,
            operands=operands,
            type_checker_rule=type_checker_rule,
            visitor_name=visitor_name,
            modifier_order=tuple(printer.get("modifier_order", ())),
            operand_order=tuple(printer.get("operand_order", ())),
        )

    return namespace, domains, instr_backends


def build_codegen_unit(spec_path: Path, backend_path: Path) -> CodegenUnit:
    spec = load_yaml(spec_path)
    backend = load_yaml(backend_path)

    instructions = normalize_instruction_spec(spec)
    namespace, domains, backends = normalize_backend(backend)

    return CodegenUnit(
        namespace=namespace,
        instructions=instructions,
        backends=backends,
        domains=domains,
    )
