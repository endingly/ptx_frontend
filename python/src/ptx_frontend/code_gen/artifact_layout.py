"""Canonical output layout shared by artifact planning and C++ emission."""

from __future__ import annotations

from ptx_frontend.code_gen.context import GenerationInstruction


# Public generated includes share this prefix across every artifact family.
INCLUDE_ROOT: str = "ptx_frontend/resolved_ir"
# Only larger opcodes are partitioned; each shard preserves canonical form indices.
FORM_SHARD_SIZE: int = 64


def form_shards(entry: GenerationInstruction) -> tuple[tuple[int, ...], ...]:
    """Partition large opcode forms in canonical order for bounded generation."""

    count = len(entry.resolved.variants)
    if count <= FORM_SHARD_SIZE:
        return ()
    return tuple(
        tuple(range(start, min(start + FORM_SHARD_SIZE, count)))
        for start in range(0, count, FORM_SHARD_SIZE)
    )
