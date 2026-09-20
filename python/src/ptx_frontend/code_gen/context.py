"""Immutable inputs shared by one deterministic C++ generation run."""

from dataclasses import dataclass

from ptx_frontend.code_gen.resolved_field_names import with_cpp_backend_field_names
from ptx_frontend.code_gen.reference_policy import validate_reference_field_types
from ptx_frontend.base.utils import file_stem_to_pascal_case
from ptx_frontend.ir.resolved_ir import ResolvedInstruction, from_instruction_spec
from ptx_frontend.spec.database import CodegenDatabase
from ptx_frontend.spec.model import CodegenUnit, InstructionSpec


@dataclass(frozen=True)
class GenerationInstruction:
    """One source specification bound to its once-lowered resolved model.

    ``resolved`` is projected for the context's backend but preserves the
    specification opcode. Keeping the pair together prevents emitters from
    associating source syntax with the resolved model of another instruction.
    """

    specification: InstructionSpec
    resolved: ResolvedInstruction

    def __post_init__(self) -> None:
        """Reject a source/resolved pair whose opcode identities differ."""

        if self.specification.opcode != self.resolved.opcode:
            raise ValueError(
                "generation instruction binding has mismatched opcodes: "
                f"{self.specification.opcode!r} and {self.resolved.opcode!r}"
            )


@dataclass(frozen=True)
class GenerationContext:
    """Immutable backend and canonical source/resolved instruction bindings."""

    backend: CodegenUnit
    entries: tuple[GenerationInstruction, ...]

    def __post_init__(self) -> None:
        """Reject a snapshot that cannot be rendered into unique C++ entities."""

        _validate_unique_source_cpp_names(self.entries)
        _validate_unique_resolved_cpp_names(self.instructions)
        validate_reference_field_types(self.instructions)

    @property
    def instructions(self) -> tuple[ResolvedInstruction, ...]:
        """Return resolved instructions in their canonical binding order."""

        return tuple(entry.resolved for entry in self.entries)


def build_generation_context(
    database: CodegenDatabase, backend: CodegenUnit
) -> GenerationContext:
    """Lower and project each instruction once for a single generation run."""

    if backend.spec_schema != database.spec_schema:
        raise ValueError(
            f"C++ backend expects spec schema {backend.spec_schema!r}, got "
            f"{database.spec_schema!r}"
        )
    return GenerationContext(
        backend=backend,
        entries=tuple(
            GenerationInstruction(
                specification=instruction,
                resolved=with_cpp_backend_field_names(
                    from_instruction_spec(instruction), backend
                ),
            )
            for instruction in database.instructions
        ),
    )


def _validate_unique_source_cpp_names(
    entries: tuple[GenerationInstruction, ...],
) -> None:
    """Reject syntax instructions that collide after C++ type projection."""

    seen: set[str] = set()
    for entry in entries:
        cpp_name = file_stem_to_pascal_case(entry.specification.opcode)
        if cpp_name in seen:
            raise ValueError(
                "multiple syntax instructions map to C++ type " f"{cpp_name!r}"
            )
        seen.add(cpp_name)


def _validate_unique_resolved_cpp_names(
    instructions: tuple[ResolvedInstruction, ...],
) -> None:
    """Reject resolved instructions that collide after C++ name projection."""

    seen: set[str] = set()
    for instruction in instructions:
        if instruction.cpp_name in seen:
            raise ValueError(
                "multiple resolved instructions map to C++ type "
                f"{instruction.cpp_name!r}"
            )
        seen.add(instruction.cpp_name)
