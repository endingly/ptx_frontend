"""PTX instruction-spec database consumed by IR generators."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from code_gen.load_yaml import load_yaml
from code_gen.model import InstructionSpec
from code_gen.normalize import normalize_instruction_spec


@dataclass(frozen=True)
class CodegenDatabase:
    """Normalized PTX ISA input shared by syntax and resolved IR generators."""

    spec_schema: str
    instructions: tuple[InstructionSpec, ...]


def discover_spec_files(spec_dir: Path) -> tuple[Path, ...]:
    """Return all PTX instruction specification files in stable order."""

    return tuple(
        sorted(
            path
            for path in spec_dir.rglob("*.yaml")
            if not path.name.endswith(".schema.yaml")
        )
    )


def load_codegen_database(*, spec_dir: Path) -> CodegenDatabase:
    """Load and normalize all PTX ISA specifications below ``spec_dir``."""

    spec_files = discover_spec_files(spec_dir)
    if not spec_files:
        raise ValueError(f"no PTX instruction specs found in {spec_dir}")

    specs = tuple(load_yaml(path) for path in spec_files)
    schema_versions = {str(spec["schema"]) for spec in specs}
    if len(schema_versions) != 1:
        raise ValueError(f"mixed PTX spec schema versions: {sorted(schema_versions)}")

    instructions = tuple(
        instruction
        for spec in specs
        for instruction in normalize_instruction_spec(spec)
    )
    _validate_unique_opcodes(instructions)
    return CodegenDatabase(
        spec_schema=next(iter(schema_versions)),
        instructions=instructions,
    )


def _validate_unique_opcodes(instructions: tuple[InstructionSpec, ...]) -> None:
    seen: set[str] = set()
    for instruction in instructions:
        if instruction.opcode in seen:
            raise ValueError(
                f"multiple PTX specifications define opcode {instruction.opcode!r}"
            )
        seen.add(instruction.opcode)
