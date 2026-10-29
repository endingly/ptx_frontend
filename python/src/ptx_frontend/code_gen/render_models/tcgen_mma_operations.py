"""Select dense MMA output rows and compose typed family fragments."""
from dataclasses import dataclass

from ptx_frontend.spec import tcgen_mma_operations as operations
from .tcgen_mma_rows import DenseShapeModel, PathModel, StaticMmaModel, TargetModel, build_path, build_target
from .tcgen_mma_sparse import SparseModel, build_sparse
from .tcgen_mma_sparse_mx import SparseMxModel, build_sparse_mx
from .tcgen_mma_ws import WsModel, build_ws


@dataclass(frozen=True)
class ScaleModel:
    """Selected scale-layout names, alignment units and fixed ID-domain entries."""

    role: str
    kind: str
    sparse: bool
    k: int
    selector: str
    factor_count: int
    subcolumn_alignment_bytes: int
    valid_ids: tuple[int, ...]
    valid_id_count: int
    layout: str
    policy: str


@dataclass(frozen=True)
class DenseModel:
    """One dense output structure and its selected immutable storage arrays.

    ``has_n_prefix`` selects the exceptional-N row structure, while
    ``scaled_d_gate`` selects the target initializer's additional member.
    ``dimension_indent`` preserves the generated signature continuation column.
    The parent binds each instance to its fixed algorithm macro explicitly.
    """

    cpp_prefix: str
    snake: str
    kind: str
    description_kind: str
    shape_array: str
    path_array: str
    target_array: str
    target_checker: str
    shared_checker: str
    scaled_d_gate: bool
    has_n_prefix: bool
    dimension_indent: str
    shapes: tuple[DenseShapeModel, ...]
    paths: tuple[PathModel, ...]
    targets: tuple[TargetModel, ...]
    scales: tuple[ScaleModel, ...]


@dataclass(frozen=True)
class DenseHeaderModel:
    """Names and selected declaration structure; no catalogue rows are needed."""

    cpp_prefix: str
    snake: str
    description_kind: str


@dataclass(frozen=True)
class DenseGroup:
    """Explicit algorithm children bound to fixed source macros by the parent."""

    f16: DenseModel
    tf32: DenseModel
    i8: DenseModel
    f8f6f4: DenseModel
    mx8: DenseModel
    mx4: DenseModel
    mxnv: DenseModel


@dataclass(frozen=True)
class DenseHeaderGroup:
    """Declaration children bound to fixed algorithm macros without dispatch."""

    f16: DenseHeaderModel
    tf32: DenseHeaderModel
    i8: DenseHeaderModel
    f8f6f4: DenseHeaderModel
    mx8: DenseHeaderModel
    mx4: DenseHeaderModel
    mxnv: DenseHeaderModel


@dataclass(frozen=True)
class MmaHeaderModel:
    """Declaration-only fragments independent of catalogue validation."""

    dense: DenseHeaderGroup
    sparse: StaticMmaModel
    sparse_mx: StaticMmaModel
    collector: StaticMmaModel
    ws: StaticMmaModel


@dataclass(frozen=True)
class MmaModel:
    """Typed fragment composition in the established public artifact order."""

    dense: DenseGroup
    sparse: SparseModel
    sparse_mx: SparseMxModel
    collector: StaticMmaModel
    ws: WsModel


def build_scale(row: operations.MxScaleLayoutRow) -> ScaleModel:
    """Select C++ enum identities while retaining the original scale-row facts."""

    selector = {'scale_vec::1X': 'Vec1X', 'scale_vec::2X': 'Vec2X', 'scale_vec::4X': 'Vec4X',
                'block32': 'Block32', 'block16': 'Block16'}[row.selector]
    layout = {'MX1': 'Mx1', 'ONE_X_N': 'OneXN', 'MX2': 'Mx2', 'TWO_X_N': 'TwoXN',
              'MX3': 'Mx3', 'THREE_X_N': 'ThreeXN', 'MX4': 'Mx4', 'FOUR_X_N': 'FourXN',
              'MX6': 'Mx6', 'SIX_X_N': 'SixXN'}[row.layout_id.name]
    policy = {'BYTE_SLOT_OFFSET': 'ByteSlotOffset', 'LAYOUT_DEFINED_PLACEMENT': 'LayoutDefinedPlacement'}[row.id_alignment_policy.name]
    return ScaleModel('A' if row.role == 'a' else 'B', row.kind, row.sparse, row.k, selector,
                      row.factor_count, row.subcolumn_alignment_bytes, row.valid_ids,
                      len(row.valid_ids), layout, policy)


def build_dense() -> DenseGroup:
    """Select all dense variants, preserving catalogue and scale-array order."""

    operations.validate_catalogue()
    scales = tuple(build_scale(row) for row in (*operations.MX8_SCALE_LAYOUTS, *operations.MX4_SCALE_LAYOUTS,
                                               *operations.MXNV_SCALE_LAYOUTS, *operations.SPARSE_MX_SCALE_LAYOUTS))
    result = []
    for variant, prefix, kind, shapes, paths, gates in (
        ('f16', 'F16', 'F16', operations.F16_SHAPES, operations.F16_PATHS, operations.F16_TARGET_GATES),
        ('tf32', 'Tf32', 'Tf32', operations.TF32_SHAPES, operations.TF32_PATHS, operations.TF32_TARGET_GATES),
        ('i8', 'I8', 'I8', operations.I8_SHAPES, operations.I8_PATHS, operations.I8_TARGET_GATES),
        ('f8f6f4', 'F8F6F4', 'F8F6F4', operations.F8F6F4_SHAPES, operations.F8F6F4_PATHS, operations.F8F6F4_TARGET_GATES),
        ('mx8', 'Mx8', 'MxF8F6F4', operations.MX8_SHAPES, operations.MX8_PATHS, operations.MX8_TARGET_GATES),
        ('mx4', 'Mx4', 'MxF4', operations.MX4_SHAPES, operations.MX4_PATHS, operations.MX8_TARGET_GATES),
        ('mxnv', 'MxNv', 'MxF4NvF4', operations.MXNV_SHAPES, operations.MXNV_PATHS, operations.MXNV_TARGET_GATES),
    ):
        shape_models = tuple(DenseShapeModel(
            'One' if row.group == 1 else 'Two', row.d_type, row.m_values,
            row.n_small + (0,) * (4 - len(row.n_small)) if isinstance(row, operations.I8ShapeRow) else (),
            row.n_first, row.n_step, row.n_last, row.k) for row in shapes)
        result.append(DenseModel('Tcgen' + prefix, 'tcgen_' + variant, kind,
                                 variant, 'kShapes' if variant == 'f16' else 'k' + prefix + 'Shapes',
                                 'kPaths' if variant == 'f16' else 'k' + prefix + 'Paths',
                                 'kTargetGates' if variant == 'f16' else 'k' + prefix + 'TargetGates',
                                 'accepts_target' if variant == 'f16' else 'accepts_' + variant + '_target',
                                 'check_shared_operand' if variant == 'f16' else 'check_' + variant + '_shared_operand',
                                 variant in ('f16', 'tf32'), variant == 'i8',
                                 ' ' * {'tf32': 29, 'i8': 27, 'f8f6f4': 31}.get(variant, 28),
                                 shape_models, tuple(build_path(row) for row in paths),
                                 tuple(build_target(kind, gate) for gate in gates), scales if variant == 'mx8' else ()))
    return DenseGroup(*result)


def build_mma() -> MmaModel:
    """Compose the complete artifact without pre-rendering child C++ fragments."""

    return MmaModel(build_dense(), build_sparse(), build_sparse_mx(), StaticMmaModel(), build_ws())


def build_mma_header() -> MmaHeaderModel:
    """Compose declarations without running source-only catalogue validation."""

    dense = tuple(DenseHeaderModel('Tcgen' + prefix, 'tcgen_' + variant, variant)
                  for variant, prefix in (('f16', 'F16'), ('tf32', 'Tf32'), ('i8', 'I8'),
                                          ('f8f6f4', 'F8F6F4'), ('mx8', 'Mx8'), ('mx4', 'Mx4'),
                                          ('mxnv', 'MxNv')))
    return MmaHeaderModel(DenseHeaderGroup(*dense), StaticMmaModel(), StaticMmaModel(), StaticMmaModel(), StaticMmaModel())
