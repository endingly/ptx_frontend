"""Build typed warp-specialized MMA storage facts."""
from dataclasses import dataclass

from ptx_frontend.spec import tcgen_mma_operations as operations
from .tcgen_mma_rows import PathModel, TargetModel, build_target


@dataclass(frozen=True)
class WsShapeModel:
    """A selected WS shape with padded arrays and valid domain counts."""

    kind: str
    sparse: bool
    d_type: str
    m_values: tuple[int, ...]
    n_values: tuple[int, ...]
    n_value_count: int
    k: int
    a_types: tuple[str, ...]
    a_type_count: int
    b_types: tuple[str, ...]
    b_type_count: int


@dataclass(frozen=True)
class WsModel:
    """Ordered WS shapes, paths and non-scaled target gates."""

    shapes: tuple[WsShapeModel, ...]
    paths: tuple[PathModel, ...]
    targets: tuple[TargetModel, ...]


def build_ws() -> WsModel:
    """Select WS data without changing its source catalogue's ordering."""

    operations.validate_catalogue()
    shapes = tuple(WsShapeModel(
        row.kind, row.sparse, row.d_type, row.m_values,
        row.n_values + (row.n_values[-1],) * (3 - len(row.n_values)), len(row.n_values), row.k,
        row.a_types + (row.a_types[0],) * (5 - len(row.a_types)), len(row.a_types),
        row.b_types + (row.b_types[0],) * (5 - len(row.b_types)), len(row.b_types))
        for row in operations.WS_SHAPES)
    paths = tuple(PathModel("One", row.m, row.layout, False, (0, 0), 1) for row in operations.WS_PATHS)
    targets = tuple(build_target(kind, gate)
                    for kind, gates in (("F16", operations.F16_TARGET_GATES), ("Tf32", operations.TF32_TARGET_GATES),
                                        ("F8F6F4", operations.F8F6F4_TARGET_GATES), ("I8", operations.I8_TARGET_GATES))
                    for gate in gates if not gate.scaled_d)
    return WsModel(shapes, paths, targets)
