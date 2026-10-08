"""One deterministic artifact plan for output discovery and generation."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Protocol

from ptx_frontend.code_gen.context import GenerationContext
from ptx_frontend.code_gen.emit.resolved_dispatch import (
    generate_resolved_dispatch_source,
)
from ptx_frontend.code_gen.emit.resolved_model import (
    form_shards,
    generate_resolved_base_header,
    generate_resolved_form_shard_header,
    generate_resolved_opcode_header,
    generate_resolved_umbrella_header,
)
from ptx_frontend.code_gen.emit.resolved_source import (
    generate_resolved_descriptor_shard_source,
    generate_resolved_form_shard_source,
    generate_resolved_opcode_source,
)
from ptx_frontend.code_gen.emit.tcgen_descriptor_domains import (
    generate_tcgen_descriptor_header,
    generate_tcgen_descriptor_source,
)
from ptx_frontend.code_gen.emit.tcgen_mma_operations import (
    generate_tcgen_mma_header,
    generate_tcgen_mma_source,
)
from ptx_frontend.code_gen.emit.tensor_map_known_facts import (
    generate_tensor_map_known_fact_query,
    generate_tensor_map_known_fact_rules,
)
from ptx_frontend.code_gen.emit.tensor_cache_controls import (
    generate_tensor_cache_controls_query,
)
from ptx_frontend.code_gen.emit.value_domains import generate_resolved_value_domain_header


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


class OpcodeArtifactEmitter(Protocol):
    """Emit one artifact for a canonical opcode within a codegen category."""

    def __call__(
        self,
        context: GenerationContext,
        *,
        category: str,
        opcode: str,
        output_path: Path,
    ) -> None:
        """Write one opcode-local artifact."""


class FormShardArtifactEmitter(Protocol):
    """Emit one canonical subset of final classes or their methods."""

    def __call__(
        self, context: GenerationContext, *, category: str, opcode: str,
        shard_index: int, output_path: Path,
    ) -> None:
        """Write one bounded form shard."""


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
    """Plan the complete direct-class resolved IR artifact set."""

    artifacts: list[GeneratedArtifact] = [
        GeneratedArtifact(
            path=output_dir / "private/resolved_value_domains.gen.hpp",
            emit=generate_resolved_value_domain_header,
        ),
        GeneratedArtifact(
            path=output_dir / "public/ptx_frontend/resolved_ir/ptx_instruction_base.gen.hpp",
            emit=generate_resolved_base_header,
        ),
        GeneratedArtifact(
            path=output_dir / "public/ptx_frontend/resolved_ir/ptx_resolved_ir.gen.hpp",
            emit=generate_resolved_umbrella_header,
        ),
        GeneratedArtifact(
            path=output_dir / "private/resolved_ir_dispatch.gen.cpp",
            emit=generate_resolved_dispatch_source,
        ),
        GeneratedArtifact(
            path=output_dir / "public/ptx_frontend/resolved_ir/tcgen_descriptor_domains.gen.hpp",
            emit=generate_tcgen_descriptor_header,
        ),
        GeneratedArtifact(
            path=output_dir / "private/resolved_ir_tcgen_descriptor_domains.gen.cpp",
            emit=generate_tcgen_descriptor_source,
        ),
        GeneratedArtifact(
            path=output_dir / "public/ptx_frontend/resolved_ir/tcgen_mma_operations.gen.hpp",
            emit=generate_tcgen_mma_header,
        ),
        GeneratedArtifact(
            path=output_dir / "private/resolved_ir_tcgen_mma_operations.gen.cpp",
            emit=generate_tcgen_mma_source,
        ),
        GeneratedArtifact(
            path=output_dir / "public/ptx_frontend/resolved_ir/tensor_map_known_facts.gen.hpp",
            emit=generate_tensor_map_known_fact_rules,
        ),
        GeneratedArtifact(
            path=output_dir / "private/resolved_ir_tensor_map_known_facts.gen.cpp",
            emit=generate_tensor_map_known_fact_query,
        ),
        GeneratedArtifact(
            path=output_dir / "private/resolved_ir_tensor_cache_controls.gen.cpp",
            emit=generate_tensor_cache_controls_query,
        ),
    ]
    for category in instruction_categories(context):
        for opcode in _category_opcodes(context, category):
            entry = next(
                item for item in context.entries
                if item.specification.codegen_category == category
                and item.specification.opcode == opcode
            )
            for index, _ in enumerate(form_shards(entry)):
                artifacts.append(
                    _form_shard_artifact(
                        path=output_dir / (
                            f"private/resolved_ir_{category}_{opcode}_"
                            f"descriptors_{index:03d}.gen.cpp"
                        ),
                        category=category, opcode=opcode, shard_index=index,
                        emitter=generate_resolved_descriptor_shard_source,
                    )
                )
                artifacts.append(
                    _form_shard_artifact(
                        path=output_dir / (
                            f"public/ptx_frontend/resolved_ir/model/{category}/"
                            f"{opcode}_forms_{index:03d}.gen.hpp"
                        ),
                        category=category, opcode=opcode, shard_index=index,
                        emitter=generate_resolved_form_shard_header,
                    )
                )
                artifacts.append(
                    _form_shard_artifact(
                        path=output_dir / (
                            f"private/resolved_ir_{category}_{opcode}_"
                            f"methods_{index:03d}.gen.cpp"
                        ),
                        category=category, opcode=opcode, shard_index=index,
                        emitter=generate_resolved_form_shard_source,
                    )
                )
            artifacts.append(
                _opcode_artifact(
                    path=output_dir / (
                        f"public/ptx_frontend/resolved_ir/model/"
                        f"{category}/{opcode}.gen.hpp"
                    ),
                    category=category,
                    opcode=opcode,
                    emitter=generate_resolved_opcode_header,
                )
            )
            artifacts.append(
                _opcode_artifact(
                    path=output_dir / f"private/resolved_ir_{category}_{opcode}.gen.cpp",
                    category=category,
                    opcode=opcode,
                    emitter=generate_resolved_opcode_source,
                )
            )
    paths = tuple(artifact.path for artifact in artifacts)
    if len(paths) != len(set(paths)):
        raise ValueError("generation plan contains duplicate artifact paths")
    return GenerationPlan(artifacts=tuple(artifacts))
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


def _category_opcodes(context: GenerationContext, category: str) -> tuple[str, ...]:
    """Return canonical opcodes in their existing category declaration order."""

    return tuple(
        entry.specification.opcode
        for entry in context.entries
        if entry.specification.codegen_category == category
    )


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


def _opcode_artifact(
    *,
    path: Path,
    category: str,
    opcode: str,
    emitter: OpcodeArtifactEmitter,
) -> GeneratedArtifact:
    """Create one category-owned artifact bound to a canonical opcode."""

    def bound(context: GenerationContext, *, output_path: Path) -> None:
        """Emit the source selected by this plan entry."""

        emitter(
            context, category=category, opcode=opcode, output_path=output_path
        )

    return GeneratedArtifact(path=path, emit=bound, category=category)


def _form_shard_artifact(
    *, path: Path, category: str, opcode: str, shard_index: int,
    emitter: FormShardArtifactEmitter,
) -> GeneratedArtifact:
    """Bind one stable category/opcode/form-slice output to its emitter."""

    def bound(context: GenerationContext, *, output_path: Path) -> None:
        """Emit the form shard selected by this immutable plan entry."""

        emitter(
            context, category=category, opcode=opcode,
            shard_index=shard_index, output_path=output_path,
        )

    return GeneratedArtifact(path=path, emit=bound, category=category)
