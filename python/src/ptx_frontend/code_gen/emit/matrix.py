"""Emit typed C++ topology from normalized matrix facts."""

from ptx_frontend.code_gen.cpp_backend import CppDomain, cpp_value
from ptx_frontend.spec.model import CodegenUnit, MatrixSpec


def emit_matrix_descriptor(matrix: MatrixSpec, backend: CodegenUnit) -> str:
    """Emit one value-owning public descriptor with canonical fragment counts."""

    fragments = ", ".join(
        "MatrixFragmentShape{"
        f'.operand_field_id = "{fragment.operand}", '
        f".role = MatrixFragmentRole::{fragment.role.name}, "
        f".element_type = MatrixElementType::{fragment.element_type.name}, "
        f".register_type = {cpp_value(CppDomain.SCALAR_TYPES, fragment.register_type, backend=backend)}, "
        f".register_count = {fragment.register_count}"
        "}"
        for fragment in matrix.fragments
    )
    return (
        "MatrixInstructionDescriptor{"
        f".family = MatrixFamily::{matrix.family.name}, "
        f".shape = MatrixShape{{.m = {matrix.shape.m}, .n = {matrix.shape.n}, .k = {matrix.shape.k}}}, "
        f".a_layout = MatrixLayout::{matrix.a_layout.name}, "
        f".b_layout = MatrixLayout::{matrix.b_layout.name}, "
        f".kind = MatrixKind::{matrix.kind.name}, "
        f".sparse_order = MatrixSparseOrder::{matrix.sparse_order.name}, "
        f".scale_type = MatrixScaleType::{matrix.scale_type.name}, "
        f".address_qualifier = MatrixAddressQualifier::{matrix.address_qualifier.name}, "
        f".source_packing = {('MatrixElementType::' + matrix.source_packing.name) if matrix.source_packing else 'std::nullopt'}, "
        f".destination_packing = {('MatrixElementType::' + matrix.destination_packing.name) if matrix.destination_packing else 'std::nullopt'}, "
        f".transpose = {'true' if matrix.transpose else 'false'}, "
        f".matrix_count = {matrix.matrix_count}, "
        f".scale_vector_size = {matrix.scale_vector_size}, "
        f".fragments = std::array<MatrixFragmentShape, 4>{{{fragments}}}, "
        f".fragment_count = {len(matrix.fragments)}"
        "}"
    )
