"""Build typed non-block-scaled sparse MMA output facts."""
from dataclasses import dataclass

from ptx_frontend.spec import tcgen_mma_operations as operations
from .tcgen_mma_rows import (MetadataModel, PathModel, SparseShapeModel, TargetModel,
                             build_path, build_sparse_shape, build_target)


@dataclass(frozen=True)
class SparseModel:
    """Ordered sparse arrays consumed by the fixed sparse checker template."""

    shapes: tuple[SparseShapeModel, ...]
    paths: tuple[PathModel, ...]
    metadata: tuple[MetadataModel, ...]
    targets: tuple[TargetModel, ...]


def build_sparse() -> SparseModel:
    """Validate the sole catalogue and select sparse storage rows."""

    operations.validate_catalogue()
    metadata = tuple(MetadataModel(
        row.kind, "OneOfTwo" if row.granularity is operations.SparseMetadataGranularity.ONE_OF_TWO else "TwoOfFour",
        row.valid_nibbles + (0,) * (7 - len(row.valid_nibbles)), len(row.valid_nibbles))
        for row in operations.SPARSE_METADATA_RULES)
    return SparseModel(tuple(build_sparse_shape(row) for row in operations.SPARSE_SHAPES),
                       tuple(build_path(row, pad=True) for row in operations.SPARSE_PATHS),
                       metadata, tuple(build_target(kind, gate) for kind, gate in operations.SPARSE_TARGET_GATES))
