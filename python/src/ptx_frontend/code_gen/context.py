"""Immutable inputs shared by one deterministic C++ generation run."""

from dataclasses import dataclass, field

from ptx_frontend.code_gen.resolved_field_names import with_cpp_backend_field_names
from ptx_frontend.code_gen.reference_policy import validate_reference_field_types
from ptx_frontend.base.utils import file_stem_to_pascal_case
from ptx_frontend.ir.resolved_ir import ResolvedInstruction, from_instruction_spec
from ptx_frontend.spec.database import CodegenDatabase
from ptx_frontend.spec.model import CodegenUnit, InstructionSpec


@dataclass(frozen=True)
class GenerationInstruction:
    """One source specification bound to its once-lowered resolved model.

    ``cpp_name`` is the canonical C++ instruction-type identity projected from
    ``specification``. ``resolved`` is projected for the context's backend but
    must preserve both source identities. Keeping the pair together prevents
    emitters from associating source syntax with another resolved instruction.
    """

    specification: InstructionSpec
    resolved: ResolvedInstruction
    cpp_name: str = field(init=False)

    def __post_init__(self) -> None:
        """Derive and validate the source/resolved C++ instruction identity."""

        if self.specification.opcode != self.resolved.opcode:
            raise ValueError(
                "generation instruction binding has mismatched opcodes: "
                f"{self.specification.opcode!r} and {self.resolved.opcode!r}"
            )
        cpp_name = file_stem_to_pascal_case(self.specification.opcode)
        object.__setattr__(self, "cpp_name", cpp_name)
        if self.resolved.cpp_name != cpp_name:
            raise ValueError(
                "generation instruction binding has mismatched C++ type identities: "
                f"{cpp_name!r} and {self.resolved.cpp_name!r}"
            )


@dataclass(frozen=True)
class GenerationContext:
    """Immutable backend and canonical source/resolved instruction bindings."""

    backend: CodegenUnit
    entries: tuple[GenerationInstruction, ...]

    def __post_init__(self) -> None:
        """Reject a snapshot that cannot be rendered into unique C++ entities."""

        _validate_unique_entry_cpp_names(self.entries)
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


def _validate_unique_entry_cpp_names(
    entries: tuple[GenerationInstruction, ...],
) -> None:
    """Reject bindings that share one canonical C++ instruction type."""

    seen: set[str] = set()
    for entry in entries:
        if entry.cpp_name in seen:
            raise ValueError(
                "multiple generation instruction bindings map to C++ type "
                f"{entry.cpp_name!r}"
            )
        seen.add(entry.cpp_name)
