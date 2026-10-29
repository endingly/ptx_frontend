"""Typed matrix descriptor facts for fixed aggregate templates."""

from dataclasses import dataclass

from ptx_frontend.code_gen.cpp_backend import CppDomain, cpp_value
from ptx_frontend.spec.model import CodegenUnit, MatrixSpec


@dataclass(frozen=True)
class MatrixFragmentModel:
    """One canonical register fragment."""

    operand: str
    role: str
    element_type: str
    register_type: str
    register_count: int


@dataclass(frozen=True)
class MatrixScaleSelectorModel:
    """One scale selector with normalized masks and thread bounds."""

    operand: str
    role: str
    byte_mask: int
    thread_max: int


@dataclass(frozen=True)
class MatrixDescriptorModel:
    """Final topology names and ordered fragment/selector rows."""

    family: str
    m: int
    n: int
    k: int
    a_layout: str
    b_layout: str
    c_layout: str
    d_layout: str
    kind: str
    bit_operation: str
    sparse_order: str
    scale_type: str
    address_qualifier: str
    source_packing: str
    destination_packing: str
    transpose: str
    matrix_count: int
    scale_vector_size: int
    fragments: tuple[MatrixFragmentModel, ...]
    scale_selectors: tuple[MatrixScaleSelectorModel, ...]
    source_placement: str
    sparse_metadata_kind: str
    warpgroup: str


def build_matrix_descriptor(matrix: MatrixSpec, backend: CodegenUnit) -> MatrixDescriptorModel:
    """Map normalized matrix facts to backend expressions without rendering."""

    return MatrixDescriptorModel(
        matrix.family.name, matrix.shape.m, matrix.shape.n, matrix.shape.k,
        matrix.a_layout.name, matrix.b_layout.name, matrix.c_layout.name, matrix.d_layout.name,
        matrix.kind.name, matrix.bit_operation.name, matrix.sparse_order.name,
        matrix.scale_type.name, matrix.address_qualifier.name,
        'MatrixElementType::' + matrix.source_packing.name if matrix.source_packing else 'std::nullopt',
        'MatrixElementType::' + matrix.destination_packing.name if matrix.destination_packing else 'std::nullopt',
        str(matrix.transpose).lower(), matrix.matrix_count, matrix.scale_vector_size,
        tuple(MatrixFragmentModel(fragment.operand, fragment.role.name, fragment.element_type.name,
            cpp_value(CppDomain.SCALAR_TYPES, fragment.register_type, backend=backend), fragment.register_count)
            for fragment in matrix.fragments),
        tuple(MatrixScaleSelectorModel(selector.operand, selector.role.name, selector.byte_mask, selector.thread_max)
            for selector in matrix.scale_selectors),
        matrix.source_placement.name, matrix.sparse_metadata_kind.name,
        'WarpGroup128{}' if matrix.family.name in ('WGMMA', 'WGMMA_SPARSE') else 'std::nullopt',
    )
