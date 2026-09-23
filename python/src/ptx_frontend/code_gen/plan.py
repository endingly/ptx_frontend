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
    generate_resolved_ir_checker_category_declarations_header,
    generate_resolved_ir_checker_declarations_header,
)
from ptx_frontend.code_gen.emit.resolved_descriptors import (
    generate_resolved_descriptor_source,
)
from ptx_frontend.code_gen.emit.resolved_dispatch import (
    generate_resolved_dispatch_source,
)
from ptx_frontend.code_gen.emit.resolved_model import (
    generate_resolved_instruction_union_header,
    generate_resolved_ir_category_header,
    generate_resolved_ir_header,
)
from ptx_frontend.code_gen.emit.resolved_resolver import (
    generate_resolved_ir_resolution_category_declarations_header,
    generate_resolved_ir_resolution_declarations_header,
)
from ptx_frontend.code_gen.emit.syntax_descriptors import (
    generate_syntax_descriptor_source,
)
from ptx_frontend.code_gen.emit.value_domains import (
    generate_resolved_value_domain_header,
)


class ArtifactEmitter(Protocol):
    """Emit one planned artifact from the frozen generation snapshot."""

    def __call__(
        self,
        context: GenerationContext,
        *,
        output_path: Path,
    ) -> None:
        """Write the artifact selected by this plan entry."""


class CategoryArtifactEmitter(Protocol):
    """Emit one artifact owned by a codegen category."""

    def __call__(
        self,
        context: GenerationContext,
        *,
        category: str,
        output_path: Path,
    ) -> None:
        """Write one category-local artifact."""


@dataclass(frozen=True)
class GeneratedArtifact:
    """One output path, emitter, and optional category ownership."""

    path: Path
    emit: ArtifactEmitter

    # None means that the artifact depends on the complete generated model.
    category: str | None = None


@dataclass(frozen=True)
class GenerationPlan:
    """Ordered output set shared by discovery and generation."""

    artifacts: tuple[GeneratedArtifact, ...]

    @property
    def paths(self) -> tuple[Path, ...]:
        """Return planned artifact paths in stable generation order."""

        return tuple(artifact.path for artifact in self.artifacts)

    @property
    def global_artifacts(self) -> tuple[GeneratedArtifact, ...]:
        """Return artifacts depending on the complete generation context."""

        return tuple(
            artifact for artifact in self.artifacts if artifact.category is None
        )

    @property
    def categories(self) -> tuple[str, ...]:
        """Return category owners represented in this plan."""

        return tuple(
            sorted(
                {
                    artifact.category
                    for artifact in self.artifacts
                    if artifact.category is not None
                }
            )
        )

    def artifacts_for_category(
        self,
        category: str,
    ) -> tuple[GeneratedArtifact, ...]:
        """Return artifacts owned exclusively by one codegen category."""

        artifacts = tuple(
            artifact for artifact in self.artifacts if artifact.category == category
        )

        if not artifacts:
            raise ValueError(
                f"generation plan contains no artifacts " f"for category {category!r}"
            )

        return artifacts


def instruction_categories(context: GenerationContext) -> tuple[str, ...]:
    """Return the stable C++ source categories for this snapshot."""

    return tuple(
        sorted({entry.specification.codegen_category for entry in context.entries})
    )


def build_generation_plan(
    context: GenerationContext,
    output_dir: Path,
) -> GenerationPlan:
    """Build every generated artifact exactly once in deterministic order."""

    categories = instruction_categories(context)
    artifacts: list[GeneratedArtifact] = []

    # ------------------------------------------------------------------
    # Backend/global support artifacts.
    # ------------------------------------------------------------------

    artifacts.append(
        GeneratedArtifact(
            path=output_dir / "private/resolved_value_domains.gen.hpp",
            emit=generate_resolved_value_domain_header,
        )
    )

    # ------------------------------------------------------------------
    # Category-local Resolved IR model declarations.
    # ------------------------------------------------------------------

    for category in categories:
        artifacts.append(
            _category_artifact(
                path=(output_dir / f"public/ptx_frontend/resolved_ir/model/{category}.gen.hpp"),
                category=category,
                emitter=generate_resolved_ir_category_header,
            )
        )

    # ------------------------------------------------------------------
    # Aggregate model compatibility layer.
    # ------------------------------------------------------------------

    artifacts.append(
        GeneratedArtifact(
            path=(output_dir / "public/ptx_frontend/resolved_ir/resolved_instruction_union.gen.hpp"),
            emit=generate_resolved_instruction_union_header,
        )
    )

    artifacts.append(
        GeneratedArtifact(
            path=output_dir / "public/ptx_frontend/resolved_ir/resolved_ir.gen.hpp",
            emit=generate_resolved_ir_header,
        )
    )

    # ------------------------------------------------------------------
    # Category-local resolver declarations.
    # ------------------------------------------------------------------

    for category in categories:
        artifacts.append(
            _category_artifact(
                path=(
                    output_dir / "public/ptx_frontend/resolved_ir/resolution" / f"{category}.gen.hpp"
                ),
                category=category,
                emitter=(generate_resolved_ir_resolution_category_declarations_header),
            )
        )

    # Aggregate resolver compatibility header.
    artifacts.append(
        GeneratedArtifact(
            path=(output_dir / "public/ptx_frontend/resolved_ir/resolved_ir_resolution.gen.hpp"),
            emit=generate_resolved_ir_resolution_declarations_header,
        )
    )

    # ------------------------------------------------------------------
    # Category-local checker declarations.
    # ------------------------------------------------------------------

    for category in categories:
        artifacts.append(
            _category_artifact(
                path=(
                    output_dir / "public/ptx_frontend/resolved_ir/checker" / f"{category}.gen.hpp"
                ),
                category=category,
                emitter=(generate_resolved_ir_checker_category_declarations_header),
            )
        )

    # Aggregate checker compatibility header.
    artifacts.append(
        GeneratedArtifact(
            path=(output_dir / "public/ptx_frontend/resolved_ir/resolved_ir_checker.gen.hpp"),
            emit=generate_resolved_ir_checker_declarations_header,
        )
    )

    # ------------------------------------------------------------------
    # Whole-model dispatch.
    # ------------------------------------------------------------------

    artifacts.append(
        GeneratedArtifact(
            path=(output_dir / "private/resolved_ir_dispatch.gen.cpp"),
            emit=generate_resolved_dispatch_source,
        )
    )

    # ------------------------------------------------------------------
    # Category-local resolver/checker implementation.
    # ------------------------------------------------------------------

    for category in categories:
        artifacts.append(
            _category_artifact(
                path=(output_dir / f"private/resolved_ir_{category}.gen.cpp"),
                category=category,
                emitter=generate_resolved_ir_category_source,
            )
        )

    # ------------------------------------------------------------------
    # Category-local descriptor implementation.
    # ------------------------------------------------------------------

    for category in categories:
        artifacts.extend(
            (
                _category_artifact(
                    path=(output_dir / f"private/syntax_descriptor_{category}.gen.cpp"),
                    category=category,
                    emitter=generate_syntax_descriptor_source,
                ),
                _category_artifact(
                    path=(
                        output_dir / f"private/resolved_descriptor_{category}.gen.cpp"
                    ),
                    category=category,
                    emitter=generate_resolved_descriptor_source,
                ),
                _category_artifact(
                    path=(
                        output_dir
                        / (
                            "private/"
                            "resolved_ir_checker_descriptor_"
                            f"{category}.gen.cpp"
                        )
                    ),
                    category=category,
                    emitter=generate_resolved_checker_descriptor_source,
                ),
            )
        )

    # ------------------------------------------------------------------
    # Ownership sanity check.
    # ------------------------------------------------------------------

    paths = tuple(artifact.path for artifact in artifacts)

    if len(paths) != len(set(paths)):
        raise ValueError("generation plan contains duplicate artifact paths")

    return GenerationPlan(
        artifacts=tuple(artifacts),
    )


def _bind_category_emitter(
    emitter: CategoryArtifactEmitter,
    *,
    category: str,
) -> ArtifactEmitter:
    """Bind one category emitter to the standard artifact-emitter interface."""

    def bound(
        context: GenerationContext,
        *,
        output_path: Path,
    ) -> None:
        emitter(
            context,
            category=category,
            output_path=output_path,
        )

    return bound


def _category_artifact(
    *,
    path: Path,
    category: str,
    emitter: CategoryArtifactEmitter,
) -> GeneratedArtifact:
    """Create one category-owned artifact with its category bound."""

    return GeneratedArtifact(
        path=path,
        emit=_bind_category_emitter(
            emitter,
            category=category,
        ),
        category=category,
    )
