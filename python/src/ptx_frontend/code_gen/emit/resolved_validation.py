"""Shared structural validation for the resolved generation snapshot."""

from ptx_frontend.ir.resolved_ir import ResolvedInstruction


def validate_unique_cpp_names(instructions: tuple[ResolvedInstruction, ...]) -> None:
    """Reject semantic instructions that collide after C++ name projection."""

    seen: set[str] = set()
    for instruction in instructions:
        if instruction.cpp_name in seen:
            raise ValueError(
                f"multiple resolved instructions map to C++ type {instruction.cpp_name!r}"
            )
        seen.add(instruction.cpp_name)
