"""One deterministic artifact plan for output discovery and generation."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Protocol

from ptx_frontend.code_gen.context import GenerationContext
from ptx_frontend.code_gen.emit.checker_descriptors import (
    generate_resolved_checker_descriptor_source,
)
from ptx_frontend.code_gen.emit.category_source import (
    generate_resolved_ir_category_source,
)
from ptx_frontend.code_gen.emit.resolved_checker import (
    generate_resolved_ir_checker_declarations_header,
)
from ptx_frontend.code_gen.emit.resolved_descriptors import generate_resolved_descriptor_source
from ptx_frontend.code_gen.emit.resolved_dispatch import generate_resolved_dispatch_source
from ptx_frontend.code_gen.emit.resolved_model import generate_resolved_ir_header
from ptx_frontend.code_gen.emit.resolved_resolver import (
    generate_resolved_ir_resolution_declarations_header,
)
from ptx_frontend.code_gen.emit.syntax_descriptors import generate_syntax_descriptor_source
from ptx_frontend.code_gen.emit.value_domains import generate_resolved_value_domain_header

class ArtifactEmitter(Protocol):
    """Emit one planned artifact from the frozen generation snapshot."""

    def __call__(
        self, context: GenerationContext, *, output_path: Path
    ) -> None:
        """Write the artifact selected by this plan entry."""


@dataclass(frozen=True)
class GeneratedArtifact:
    """One output path and the emitter that owns its complete contents."""

    path: Path
    emit: ArtifactEmitter


@dataclass(frozen=True)
class GenerationPlan:
    """Ordered output set shared by listing, emission, and formatting."""

    artifacts: tuple[GeneratedArtifact, ...]

    @property
    def paths(self) -> tuple[Path, ...]:
        """Return planned artifact paths in stable generation order."""

        return tuple(artifact.path for artifact in self.artifacts)


def instruction_categories(context: GenerationContext) -> tuple[str, ...]:
    """Return the stable C++ source categories for this snapshot."""

    return tuple(sorted({entry.specification.codegen_category for entry in context.entries}))


def build_generation_plan(context: GenerationContext, output_dir: Path) -> GenerationPlan:
    """Build every artifact exactly once from an already lowered context."""

    artifacts: list[GeneratedArtifact] = [
        GeneratedArtifact(output_dir / "private/resolved_value_domains.gen.hpp", generate_resolved_value_domain_header),
        GeneratedArtifact(output_dir / "public/resolved_ir.gen.hpp", generate_resolved_ir_header),
        GeneratedArtifact(output_dir / "public/resolved_ir_resolution.gen.hpp", generate_resolved_ir_resolution_declarations_header),
        GeneratedArtifact(output_dir / "public/resolved_ir_checker.gen.hpp", generate_resolved_ir_checker_declarations_header),
        GeneratedArtifact(output_dir / "private/resolved_ir_dispatch.gen.cpp", generate_resolved_dispatch_source),
    ]
    artifacts.extend(
        GeneratedArtifact(
            output_dir / f"private/resolved_ir_{category}.gen.cpp",
            lambda active_context, *, output_path, category=category: generate_resolved_ir_category_source(
                active_context, category=category, output_path=output_path
            ),
        )
        for category in instruction_categories(context)
    )
    artifacts.extend((
        GeneratedArtifact(output_dir / "private/syntax_descriptor.gen.cpp", generate_syntax_descriptor_source),
        GeneratedArtifact(output_dir / "private/resolved_descriptor.gen.cpp", generate_resolved_descriptor_source),
        GeneratedArtifact(output_dir / "private/resolved_ir_checker_descriptor.gen.cpp", generate_resolved_checker_descriptor_source),
    ))
    paths = tuple(artifact.path for artifact in artifacts)
    if len(paths) != len(set(paths)):
        raise ValueError("generation plan contains duplicate artifact paths")
    return GenerationPlan(tuple(artifacts))
