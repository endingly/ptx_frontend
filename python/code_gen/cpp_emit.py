from code_gen.model import *


def cpp_type_for_modifier(
    mod: ModifierSpec, ib: InstructionBackend, domains: dict[str, DomainBackend]
) -> str:
    """Determine the C++ type for a given modifier."""
    mb = ib.modifiers.get(mod.name)
    if mb and mb.cpp_type:
        return mb.cpp_type

    if mod.domain:
        return domains[mod.domain].cpp_type

    if mod.kind == "flag":
        return "bool"

    raise ValueError(f"cannot infer C++ type for modifier {mod.name}")


def cpp_field_for_modifier(mod: ModifierSpec, ib: InstructionBackend) -> str:
    """Determine the C++ field name for a given modifier."""
    mb = ib.modifiers.get(mod.name)
    if mb:
        return mb.field
    return mod.name


def cpp_default_for_modifier(
    mod: ModifierSpec, ib: InstructionBackend, domains: dict[str, DomainBackend]
) -> str:
    """Determine the C++ default value for a given modifier, if any."""
    mb = ib.modifiers.get(mod.name)

    if mb and mb.default is not None:
        return mb.default

    if mod.kind == "flag":
        if mod.presence == "fixed":
            return "true" if mod.value is True else "false"
        if isinstance(mod.default, bool):
            return "true" if mod.default else "false"
        return "false"

    if mod.presence == "fixed":
        assert mod.domain is not None
        return domains[mod.domain].values[str(mod.value)]

    if mod.domain:
        domain = domains[mod.domain]
        if domain.default:
            return domain.default

    return "{}"


def unique_modifiers(instr: InstructionSpec) -> list[ModifierSpec]:
    """Collect unique modifiers across all variants of an instruction."""
    seen: dict[str, ModifierSpec] = {}

    for var in instr.variants:
        for mod in var.modifiers:
            if mod.name not in seen:
                seen[mod.name] = mod

    return list(seen.values())


def unique_operands(instr: InstructionSpec) -> list[OperandSpec]:
    """Collect unique operands across all variants of an instruction."""
    seen: dict[str, OperandSpec] = {}

    for var in instr.variants:
        for op in var.operands:
            if op.name not in seen:
                seen[op.name] = op

    return list(seen.values())
