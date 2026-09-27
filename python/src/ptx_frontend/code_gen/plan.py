"""One deterministic artifact plan for output discovery and generation."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, auto
import hashlib
from pathlib import Path
from types import MappingProxyType
from typing import Mapping, Protocol

from ptx_frontend.code_gen.context import GenerationContext
from ptx_frontend.code_gen.emit.checker_descriptors import (
    generate_resolved_checker_descriptor_source,
)
from ptx_frontend.code_gen.emit.category_source import (
    generate_resolved_ir_category_source,
    generate_resolved_ir_group_source,
)
from ptx_frontend.code_gen.emit.resolved_checker import (
    generate_resolved_ir_checker_category_declarations_header,
    generate_resolved_ir_checker_declarations_header,
    generate_resolved_ir_checker_opcode_declarations_header,
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
    generate_resolved_ir_opcode_header,
)
from ptx_frontend.code_gen.emit.resolved_resolver import (
    generate_resolved_ir_resolution_category_declarations_header,
    generate_resolved_ir_resolution_declarations_header,
    generate_resolved_ir_resolution_opcode_declarations_header,
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


class GroupArtifactEmitter(Protocol):
    """Emit one category-owned source for a stable group of opcodes."""

    def __call__(
        self,
        context: GenerationContext,
        *,
        category: str,
        opcodes: tuple[str, ...],
        output_path: Path,
    ) -> None:
        """Write one implementation group for its selected opcodes."""


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


class _SourcePartitionKind(Enum):
    """Internal topology for category-owned implementation sources."""

    CATEGORY = auto()
    HASH_BUCKETS = auto()
    RESIDUAL = auto()


class _SourceGroupId(Enum):
    """Fixed physical group identities; values are output filename suffixes."""

    BUCKET_0 = "bucket_0"
    BUCKET_1 = "bucket_1"
    BUCKET_2 = "bucket_2"
    CVT = "cvt"
    LD = "ld"
    MBARRIER = "mbarrier"
    ATOM = "atom"
    RED = "red"
    RESIDUAL = "residual"


@dataclass(frozen=True)
class _SourcePartitionPolicy:
    """Immutable physical partitioning independent of external category spellings."""

    kind: _SourcePartitionKind
    # Ordered group identities paired with canonical opcode input spellings.
    dedicated_groups: tuple[tuple[_SourceGroupId, str], ...] = ()
    # Ordered hash buckets, or the single residual group for remaining opcodes.
    remainder_groups: tuple[_SourceGroupId, ...] = ()


_CATEGORY_SOURCE_POLICY = _SourcePartitionPolicy(_SourcePartitionKind.CATEGORY)

# Category spellings are adapted here once; they do not drive partition dispatch.
_SOURCE_PARTITION_POLICIES: Mapping[str, _SourcePartitionPolicy] = MappingProxyType(
    {
        "arithmetic": _SourcePartitionPolicy(
            kind=_SourcePartitionKind.HASH_BUCKETS,
            remainder_groups=(
                _SourceGroupId.BUCKET_0,
                _SourceGroupId.BUCKET_1,
                _SourceGroupId.BUCKET_2,
            ),
        ),
        "data_movement": _SourcePartitionPolicy(
            kind=_SourcePartitionKind.HASH_BUCKETS,
            dedicated_groups=((_SourceGroupId.CVT, "cvt"), (_SourceGroupId.LD, "ld")),
            remainder_groups=(_SourceGroupId.BUCKET_0, _SourceGroupId.BUCKET_1),
        ),
        "parallel_synchronization_and_communication": _SourcePartitionPolicy(
            kind=_SourcePartitionKind.RESIDUAL,
            dedicated_groups=(
                (_SourceGroupId.MBARRIER, "mbarrier"),
                (_SourceGroupId.ATOM, "atom"),
                (_SourceGroupId.RED, "red"),
            ),
            remainder_groups=(_SourceGroupId.RESIDUAL,),
        ),
    }
)


def _source_partition_policy(category: str) -> _SourcePartitionPolicy:
    """Adapt an extensible category spelling to its internal physical policy."""

    return _SOURCE_PARTITION_POLICIES.get(category, _CATEGORY_SOURCE_POLICY)


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
        artifacts.extend(
            _opcode_artifact(
                path=(
                    output_dir
                    / f"public/ptx_frontend/resolved_ir/model/{category}/{opcode}/model.gen.hpp"
                ),
                category=category,
                opcode=opcode,
                emitter=generate_resolved_ir_opcode_header,
            )
            for opcode in _category_opcodes(context, category)
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
        artifacts.extend(
            _opcode_artifact(
                path=(
                    output_dir
                    / f"public/ptx_frontend/resolved_ir/model/{category}/{opcode}/resolution.gen.hpp"
                ),
                category=category,
                opcode=opcode,
                emitter=generate_resolved_ir_resolution_opcode_declarations_header,
            )
            for opcode in _category_opcodes(context, category)
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
        artifacts.extend(
            _opcode_artifact(
                path=(
                    output_dir
                    / f"public/ptx_frontend/resolved_ir/model/{category}/{opcode}/checker.gen.hpp"
                ),
                category=category,
                opcode=opcode,
                emitter=generate_resolved_ir_checker_opcode_declarations_header,
            )
            for opcode in _category_opcodes(context, category)
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
        policy = _source_partition_policy(category)
        if policy.kind is not _SourcePartitionKind.CATEGORY:
            artifacts.extend(
                _group_artifact(
                    path=(
                        output_dir
                        / f"private/resolved_ir_{category}_{group.value}.gen.cpp"
                    ),
                    category=category,
                    opcodes=members,
                    emitter=generate_resolved_ir_group_source,
                )
                for group, members in _source_groups(
                    policy, _category_opcodes(context, category)
                )
            )
        else:
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


def _stable_opcode_bucket(opcode: str, bucket_count: int) -> int:
    """Place one canonical opcode using a process-independent SHA-256 digest."""

    digest = hashlib.sha256(opcode.encode("utf-8")).digest()
    return int.from_bytes(digest, byteorder="big") % bucket_count


def _source_groups(
    policy: _SourcePartitionPolicy, opcodes: tuple[str, ...]
) -> tuple[tuple[_SourceGroupId, tuple[str, ...]], ...]:
    """Apply typed topology while preserving fixed groups and opcode order."""

    dedicated_opcodes = tuple(opcode for _, opcode in policy.dedicated_groups)
    remaining = tuple(opcode for opcode in opcodes if opcode not in dedicated_opcodes)
    dedicated = tuple(
        (group, (opcode,) if opcode in opcodes else ())
        for group, opcode in policy.dedicated_groups
    )
    if policy.kind is _SourcePartitionKind.HASH_BUCKETS:
        return dedicated + tuple(
            (
                group,
                tuple(
                    opcode for opcode in remaining
                    if _stable_opcode_bucket(opcode, len(policy.remainder_groups)) == index
                ),
            )
            for index, group in enumerate(policy.remainder_groups)
        )
    if policy.kind is _SourcePartitionKind.RESIDUAL:
        return dedicated + ((policy.remainder_groups[0], remaining),)
    raise ValueError("monolithic source partition has no source groups")


def _group_artifact(
    *,
    path: Path,
    category: str,
    opcodes: tuple[str, ...],
    emitter: GroupArtifactEmitter,
) -> GeneratedArtifact:
    """Bind one fixed source group to a category-owned plan artifact."""

    def bound(context: GenerationContext, *, output_path: Path) -> None:
        """Emit the definitions selected by this source group."""

        emitter(context, category=category, opcodes=opcodes, output_path=output_path)

    return GeneratedArtifact(path=path, emit=bound, category=category)
