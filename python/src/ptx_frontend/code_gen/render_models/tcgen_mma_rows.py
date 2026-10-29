"""Typed output rows shared by the fixed MMA storage templates."""

from __future__ import annotations

from dataclasses import dataclass

from ptx_frontend.spec import tcgen_mma_operations as operations


@dataclass(frozen=True)
class StaticMmaModel:
    """Select a fixed declaration or checker fragment with no variable facts."""


@dataclass(frozen=True)
class DenseShapeModel:
    """One dense shape initializer, including the optional exceptional N grid."""

    group: str
    d_type: str
    m_values: tuple[int, ...]
    n_small: tuple[int, ...]
    n_first: int
    n_step: int
    n_last: int
    k: int


@dataclass(frozen=True)
class SparseShapeModel:
    """One sparse shape initializer with padded fixed arrays and valid counts."""

    kind: str
    group: str
    d_type: str
    m_values: tuple[int, ...]
    n_small: tuple[int, ...]
    n_small_count: int
    n_first: int
    n_step: int
    n_last: int
    k: int
    a_types: tuple[str, ...]
    a_type_count: int
    b_types: tuple[str, ...]
    b_type_count: int


@dataclass(frozen=True)
class PathModel:
    """One path initializer; padded entries are excluded by allowed_lane_count."""

    group: str
    m: int
    layout: str
    half_path: bool
    allowed_lane_halves: tuple[int, ...]
    allowed_lane_count: int


@dataclass(frozen=True)
class TargetModel:
    """A selected target gate, retaining catalogue order and version units."""

    kind: str
    feature: str
    exact: bool
    ptx_major: int
    ptx_minor: int
    scaled_d: bool


@dataclass(frozen=True)
class MetadataModel:
    """A metadata nibble domain with its generated enum and valid entry count."""

    kind: str
    granularity: str
    valid_nibbles: tuple[int, ...]
    valid_count: int


def build_path(row: operations.F16PathRow | operations.SparsePathRow, *, pad: bool = False) -> PathModel:
    """Select typed CTA names and preserve the original lane-array padding."""

    lanes = row.allowed_lane_halves
    return PathModel("One" if row.group == 1 else "Two", row.m, row.layout,
                     row.half_path, lanes + (0,) * (2 - len(lanes)) if pad else lanes,
                     len(lanes))


def build_target(kind: str, gate: operations.F16TargetGate) -> TargetModel:
    """Copy the selected gate's immutable output facts without rendering C++."""

    return TargetModel(kind, gate.feature, gate.exact, gate.ptx_major,
                       gate.ptx_minor, gate.scaled_d)


def build_sparse_shape(row: operations.SparseShapeRow, *, mx: bool = False) -> SparseShapeModel:
    """Choose the fixed array padding used by sparse and sparse MX storage."""

    a_types = row.a_types + (row.a_types[0],) * (5 - len(row.a_types))
    b_types = a_types if mx else row.b_types + (row.b_types[0],) * (5 - len(row.b_types))
    small = () if mx else row.n_small
    return SparseShapeModel(row.kind, "One" if row.group == 1 else "Two",
                            "F32" if mx else row.d_type, row.m_values,
                            small + (0,) * (4 - len(small)), len(small),
                            row.n_first, row.n_step, row.n_last, row.k,
                            a_types, len(row.a_types), b_types, len(row.b_types))
