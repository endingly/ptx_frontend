from dataclasses import dataclass
from pathlib import Path

from code_gen.model import (
    CodegenUnit,
    DomainBackend,
    InstructionBackend,
    InstructionSpec,
)
from code_gen.normalize import build_codegen_unit


@dataclass(frozen=True)
class CodegenInput:
    """Represents a pair of spec and backend YAML files for a codegen unit."""

    spec_path: Path
    backend_path: Path


@dataclass(frozen=True)
class LoadedCodegenUnit:
    """Represents a codegen unit that has been loaded and normalized from its spec and backend YAML files."""

    source: CodegenInput
    unit: CodegenUnit  # after normalization, the unit is a CodegenUnit object that contains the normalized spec and backend data.


@dataclass(frozen=True)
class BoundInstruction:
    """Represents a single instruction that has been bound to its spec and backend definitions."""

    category: str
    spec: InstructionSpec
    backend: InstructionBackend
    source: CodegenInput


@dataclass(frozen=True)
class MergedDomain:
    """Represents a domain that has been merged from multiple codegen units, containing the combined values and sources."""

    name: str
    cpp_type: str
    values: dict[str, str]
    default: str | None
    sources: tuple[CodegenInput, ...]


@dataclass(frozen=True)
class OpcodeGroup:
    """Represents a group of instructions that share the same opcode, containing the opcode and the candidate instructions."""

    opcode: str
    candidates: tuple[BoundInstruction, ...]


@dataclass(frozen=True)
class CodegenDatabase:
    """Represents the entire codegen database, containing all loaded codegen units, bound instructions, merged domains, and opcode groups."""

    spec_schema: str
    backend_schema: str
    namespace: str

    units: tuple[LoadedCodegenUnit, ...]
    instructions: tuple[BoundInstruction, ...]
    domains: dict[str, MergedDomain]
    opcode_groups: dict[str, OpcodeGroup]


def discover_codegen_inputs(
    spec_dir: Path,
    backend_dir: Path,
) -> tuple[CodegenInput, ...]:
    """Discover codegen inputs by matching YAML files in the spec and backend directories."""
    spec_files = {
        path.relative_to(spec_dir): path
        for path in spec_dir.rglob("*.yaml")
        if not path.name.endswith(".schema.yaml")
    }

    backend_files = {
        path.relative_to(backend_dir): path
        for path in backend_dir.rglob("*.yaml")
        if not path.name.endswith(".schema.yaml")
    }

    missing_backends = sorted(spec_files.keys() - backend_files.keys())
    missing_specs = sorted(backend_files.keys() - spec_files.keys())

    if missing_backends:
        raise ValueError(
            "missing backend YAML files for: "
            + ", ".join(str(path) for path in missing_backends)
        )

    if missing_specs:
        raise ValueError(
            "backend YAML files without matching spec: "
            + ", ".join(str(path) for path in missing_specs)
        )

    return tuple(
        CodegenInput(
            spec_path=spec_files[relative],
            backend_path=backend_files[relative],
        )
        for relative in sorted(spec_files)
    )


def bind_all_instructions(
    units: tuple[LoadedCodegenUnit, ...],
) -> tuple[BoundInstruction, ...]:
    """Bind instructions by matching spec and backend definitions, and validate the consistency of opcodes."""
    result: list[BoundInstruction] = []

    for loaded in units:
        unit = loaded.unit

        spec_opcodes = {instr.opcode for instr in unit.instructions}
        backend_opcodes = set(unit.backends)

        missing_backends = spec_opcodes - backend_opcodes
        extra_backends = backend_opcodes - spec_opcodes

        if missing_backends:
            raise ValueError(
                f"{loaded.source.backend_path}: missing mappings for "
                f"{sorted(missing_backends)}"
            )

        if extra_backends:
            raise ValueError(
                f"{loaded.source.backend_path}: mappings without spec for "
                f"{sorted(extra_backends)}"
            )

        for instr in unit.instructions:
            result.append(
                BoundInstruction(
                    category=unit.category,
                    spec=instr,
                    backend=unit.backends[instr.opcode],
                    source=loaded.source,
                )
            )

    return tuple(result)


def merge_one_domain(
    *,
    current: MergedDomain,
    incoming: DomainBackend,
    source: CodegenInput,
) -> MergedDomain:
    """Merge a single domain by combining the values and defaults from the current merged domain and the incoming domain backend, while validating the consistency of cpp_type, values, and defaults.

    Args:
        current (MergedDomain): The current merged domain that has been built from previous codegen units.
        incoming (DomainBackend): The incoming domain backend to merge with the current one.
        source (CodegenInput): The codegen input source for the incoming domain backend.

    Returns:
        MergedDomain: The merged domain with combined values and defaults.

    .. note::
        - The `cpp_type` of the current and incoming domains must match, otherwise a ValueError is
            raised indicating a conflicting cpp_type.
        - `cpp_expr` values with the same spelling must be consistent between the current and incoming domains, otherwise a ValueError is raised indicating conflicting mappings for the same spelling.
        - Different spellings: Merge to form the union
        - Defaults must be consistent between the current and incoming domains, otherwise a ValueError is raised indicating conflicting defaults.
    """
    if current.cpp_type != incoming.cpp_type:
        raise ValueError(
            f"domain {current.name!r} uses conflicting cpp_type: "
            f"{current.cpp_type!r} vs {incoming.cpp_type!r}"
        )

    values = dict(current.values)

    for spelling, cpp_expr in incoming.values.items():
        existing = values.get(spelling)

        if existing is not None and existing != cpp_expr:
            raise ValueError(
                f"domain {current.name!r}: spelling {spelling!r} maps to "
                f"both {existing!r} and {cpp_expr!r}"
            )

        values[spelling] = cpp_expr

    defaults = {
        default
        for default in (current.default, incoming.default)
        if default is not None
    }

    if len(defaults) > 1:
        raise ValueError(
            f"domain {current.name!r} has conflicting defaults: " f"{sorted(defaults)}"
        )

    return MergedDomain(
        name=current.name,
        cpp_type=current.cpp_type,
        values=values,
        default=next(iter(defaults), None),
        sources=(*current.sources, source),
    )


def merge_all_domains(
    units: tuple[LoadedCodegenUnit, ...],
) -> dict[str, MergedDomain]:
    """Merge all domains from the loaded codegen units, combining values and defaults while validating consistency."""
    merged: dict[str, MergedDomain] = {}

    for loaded in units:
        for name, incoming in loaded.unit.domains.items():
            current = merged.get(name)

            if current is None:
                merged[name] = MergedDomain(
                    name=name,
                    cpp_type=incoming.cpp_type,
                    values=dict(incoming.values),
                    default=incoming.default,
                    sources=(loaded.source,),
                )
                continue

            merged[name] = merge_one_domain(
                current=current,
                incoming=incoming,
                source=loaded.source,
            )

    return merged


def build_opcode_groups(
    instructions: tuple[BoundInstruction, ...],
) -> dict[str, OpcodeGroup]:
    """Build opcode groups by grouping bound instructions based on their opcodes, and validate that instructions with the same opcode are correctly grouped together.

    Args:
        instructions (tuple[BoundInstruction, ...]): _description_

    Returns:
        dict[str, OpcodeGroup]: _description_

    .. note::
        Opcodes with the same name are allowed.
    """
    grouped: dict[str, list[BoundInstruction]] = {}

    for instr in instructions:
        grouped.setdefault(instr.spec.opcode, []).append(instr)

    return {
        opcode: OpcodeGroup(
            opcode=opcode,
            candidates=tuple(candidates),
        )
        for opcode, candidates in grouped.items()
    }


def validate_global_cpp_types(
    instructions: tuple[BoundInstruction, ...],
) -> None:
    owners: dict[str, BoundInstruction] = {}

    for instr in instructions:
        cpp_type = instr.backend.cpp
        previous = owners.get(cpp_type)

        if previous is not None:
            raise ValueError(
                f"generated C++ type {cpp_type!r} appears in both "
                f"{previous.source.backend_path} and "
                f"{instr.source.backend_path}"
            )

        owners[cpp_type] = instr


def validate_global_metadata(
    units: tuple[LoadedCodegenUnit, ...],
) -> None:
    if not units:
        raise ValueError("no codegen input units were discovered")

    spec_schemas = {loaded.unit.spec_schema for loaded in units}
    backend_schemas = {loaded.unit.backend_schema for loaded in units}
    namespaces = {loaded.unit.namespace for loaded in units}

    if len(spec_schemas) != 1:
        raise ValueError(f"mixed spec schema versions: {sorted(spec_schemas)}")

    if len(backend_schemas) != 1:
        raise ValueError(f"mixed backend schema versions: {sorted(backend_schemas)}")

    if len(namespaces) != 1:
        raise ValueError(f"mixed generated namespaces: {sorted(namespaces)}")


def build_codegen_database(
    inputs: tuple[CodegenInput, ...],
) -> CodegenDatabase:
    loaded_units = tuple(
        LoadedCodegenUnit(
            source=item,
            unit=build_codegen_unit(
                item.spec_path,
                item.backend_path,
            ),
        )
        for item in inputs
    )

    validate_global_metadata(loaded_units)

    instructions = bind_all_instructions(loaded_units)
    validate_global_cpp_types(instructions)

    domains = merge_all_domains(loaded_units)
    opcode_groups = build_opcode_groups(instructions)

    first = loaded_units[0].unit

    return CodegenDatabase(
        spec_schema=first.spec_schema,
        backend_schema=first.backend_schema,
        namespace=first.namespace,
        units=loaded_units,
        instructions=instructions,
        domains=domains,
        opcode_groups=opcode_groups,
    )


def load_codegen_database(
    *,
    spec_dir: Path,
    backend_dir: Path,
) -> CodegenDatabase:
    inputs = discover_codegen_inputs(spec_dir, backend_dir)
    return build_codegen_database(inputs)
