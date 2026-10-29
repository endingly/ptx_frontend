"""Typed canonical opcode ordering for global identity and resolver dispatch."""
from dataclasses import dataclass
from ptx_frontend.code_gen.context import GenerationContext
from ptx_frontend.code_gen.artifact_layout import INCLUDE_ROOT

@dataclass(frozen=True)
class DispatchEntry:
    """One opcode's canonical mnemonic and exact C++ family name."""
    opcode: str
    cpp_name: str

@dataclass(frozen=True)
class DispatchModel:
    """Global dispatch preserving context ownership and stable entry order."""
    include_root: str
    entries: tuple[DispatchEntry, ...]


def build_dispatch(context: GenerationContext) -> DispatchModel:
    """Project canonical opcode dispatch facts without rendering source."""
    return DispatchModel(INCLUDE_ROOT, tuple(DispatchEntry(entry.specification.opcode, entry.cpp_name) for entry in context.entries))
