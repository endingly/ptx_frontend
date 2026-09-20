"""Immutable inputs shared by one deterministic C++ generation run."""

from dataclasses import dataclass

from ptx_frontend.code_gen.resolved_field_names import with_cpp_backend_field_names
from ptx_frontend.ir.resolved_ir import ResolvedInstruction, from_instruction_spec
from ptx_frontend.spec.database import CodegenDatabase
from ptx_frontend.spec.model import CodegenUnit


@dataclass(frozen=True)
class GenerationContext:
    """Immutable backend, database, and once-lowered alias-projected instructions."""

    backend: CodegenUnit
    database: CodegenDatabase
    instructions: tuple[ResolvedInstruction, ...]


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
        database=database,
        instructions=tuple(
            with_cpp_backend_field_names(from_instruction_spec(instruction), backend)
            for instruction in database.instructions
        ),
    )
