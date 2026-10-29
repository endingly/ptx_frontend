"""Build typed block-scaled sparse MMA output facts."""
from dataclasses import dataclass

from ptx_frontend.spec import tcgen_mma_operations as operations
from .tcgen_mma_rows import MetadataModel, SparseShapeModel, TargetModel, build_sparse_shape, build_target


@dataclass(frozen=True)
class SparseMxModel:
    """Sparse MX arrays with independently selected base/vector/block gates."""

    shapes: tuple[SparseShapeModel, ...]
    metadata: tuple[MetadataModel, ...]
    base_targets: tuple[TargetModel, ...]
    vec_targets: tuple[TargetModel, ...]
    block_targets: tuple[TargetModel, ...]


def build_sparse_mx() -> SparseMxModel:
    """Select the existing sparse MX row and gate ordering."""

    operations.validate_catalogue()
    metadata = tuple(MetadataModel(
        row.kind, "TwoOfFour" if row.granularity is operations.SparseMxMetadataGranularity.TWO_OF_FOUR else "PairwiseFourOfEight",
        row.valid_nibbles, len(row.valid_nibbles)) for row in operations.SPARSE_MX_METADATA_RULES)
    return SparseMxModel(tuple(build_sparse_shape(row, mx=True) for row in operations.SPARSE_MX_SHAPES), metadata,
                         tuple(build_target(kind, gate) for kind, gate in operations.SPARSE_MX_BASE_GATES),
                         tuple(build_target(kind, gate) for kind, gate in operations.SPARSE_MX_VEC_GATES),
                         tuple(build_target(kind, gate) for kind, gate in operations.SPARSE_MX_BLOCK_GATES))
