"""Normalize canonical warp-matrix topology and fragment facts."""

from __future__ import annotations

from collections.abc import Mapping
import re
from typing import Any

from ptx_frontend.spec.model import (
    MatrixAddressQualifier,
    MatrixBitOperation,
    MatrixElementType,
    MatrixFamily,
    MatrixFragmentRole,
    MatrixFragmentShape,
    MatrixKind,
    MatrixLayout,
    MatrixShape,
    MatrixSparseOrder,
    MatrixScaleType,
    MatrixScaleSelectorSpec,
    MatrixSpec,
    ModifierPresence,
    ModifierSpec,
    OperandKind,
    OperandLayoutSpec,
    OperandTypeExpressionKind,
    modifier_spellings,
)


def normalize_matrix(
    raw: object,
    modifiers: tuple[ModifierSpec, ...],
    layouts: tuple[OperandLayoutSpec, ...],
) -> MatrixSpec | None:
    """Validate matrix metadata against the operands that own fragment counts.

    A matrix variant has one topology. Each register count and register scalar
    type is read from its already normalized operand; the YAML matrix block
    names semantic roles and never supplies a second copy of cardinality.
    """

    if raw is None:
        return None
    if not isinstance(raw, Mapping):
        raise TypeError("matrix must be an object")
    permitted = {
        "family", "m", "n", "k", "a_layout", "b_layout", "elements",
        "fragments", "kind", "transpose", "matrix_count",
        "scale_vector_size", "scale_type", "source_packing", "sparse_order",
        "destination_packing", "address_qualifier",
        "c_layout", "d_layout", "scale_selectors", "bit_operation",
    }
    if set(raw) - permitted:
        raise ValueError(f"matrix has unknown keys {sorted(set(raw) - permitted)}")
    try:
        family = MatrixFamily(raw["family"])
        a_layout = MatrixLayout(raw.get("a_layout", "none"))
        b_layout = MatrixLayout(raw.get("b_layout", "none"))
        c_layout = MatrixLayout(raw.get("c_layout", "none"))
        d_layout = MatrixLayout(raw.get("d_layout", "none"))
        kind = MatrixKind(raw.get("kind", "classic"))
        bit_operation = MatrixBitOperation(raw.get("bit_operation", "none"))
        sparse_order = MatrixSparseOrder(raw.get("sparse_order", "none"))
        scale_type = MatrixScaleType(raw.get("scale_type", "none"))
        address_qualifier = MatrixAddressQualifier(raw.get("address_qualifier", "none"))
        source_packing = (
            MatrixElementType(raw["source_packing"])
            if "source_packing" in raw else None
        )
        destination_packing = (
            MatrixElementType(raw["destination_packing"])
            if "destination_packing" in raw else None
        )
    except (KeyError, ValueError) as error:
        raise ValueError(f"invalid matrix family, layout, kind, or sparse order: {error}") from error
    m, n, k = (raw.get(axis) for axis in ("m", "n", "k"))
    if any(type(axis) is not int for axis in (m, n, k)) or m <= 0 or n <= 0 or k < 0:
        raise ValueError("matrix M/N must be positive integers and K nonnegative")
    raw_movement = family in {
        MatrixFamily.LDMATRIX, MatrixFamily.STMATRIX, MatrixFamily.MOVMATRIX,
    }
    if (k == 0) != raw_movement:
        raise ValueError("raw matrix movement must have K=0; MMA and WMMA must have K>0")
    transpose = raw.get("transpose", False)
    matrix_count = raw.get("matrix_count", 0)
    scale_vector_size = raw.get("scale_vector_size", 0)
    if type(transpose) is not bool:
        raise TypeError("matrix.transpose must be boolean")
    if type(matrix_count) is not int or not 0 <= matrix_count <= 4:
        raise ValueError("matrix.matrix_count must be 0..4")
    if type(scale_vector_size) is not int or scale_vector_size not in (0, 1, 2, 4):
        raise ValueError("matrix.scale_vector_size must be 0, 1, 2, or 4")
    if family is MatrixFamily.MMA_SPARSE and sparse_order is MatrixSparseOrder.NONE:
        raise ValueError("sparse MMA requires a metadata ordering contract")
    if family is not MatrixFamily.MMA_SPARSE and sparse_order is not MatrixSparseOrder.NONE:
        raise ValueError("only sparse MMA accepts a metadata ordering contract")
    if family not in (MatrixFamily.LDMATRIX, MatrixFamily.STMATRIX) and matrix_count:
        raise ValueError("matrix count only applies to ldmatrix/stmatrix")
    if bit_operation is not MatrixBitOperation.NONE and family not in {
        MatrixFamily.MMA, MatrixFamily.WMMA_MMA,
    }:
        raise ValueError("single-bit operation only applies to dense MMA")
    if (scale_type is MatrixScaleType.NONE) != (scale_vector_size == 0):
        raise ValueError("scale type and vector size must both be present or absent")
    raw_selectors = raw.get("scale_selectors", {})
    if not isinstance(raw_selectors, Mapping):
        raise TypeError("matrix.scale_selectors must map operands to A/B roles")
    if scale_vector_size and sorted(map(str, raw_selectors.values())) != ["a", "b"]:
        raise ValueError("block-scaled matrix requires A and B scale selectors")
    if not scale_vector_size and raw_selectors:
        raise ValueError("unscaled matrix cannot have scale selectors")
    if (source_packing is not None or destination_packing is not None) and family is not MatrixFamily.LDMATRIX:
        raise ValueError("only ldmatrix has a decompressed source encoding")
    if (source_packing is None) != (destination_packing is None):
        raise ValueError("matrix decompression needs both source and destination packing")

    raw_elements = raw.get("elements")
    raw_fragments = raw.get("fragments")
    if not isinstance(raw_elements, Mapping) or not raw_elements:
        raise ValueError("matrix.elements must map fragment roles to element types")
    if not isinstance(raw_fragments, Mapping) or not raw_fragments:
        raise ValueError("matrix.fragments must map operands to fragment roles")
    try:
        elements = {
            MatrixFragmentRole(role): MatrixElementType(element)
            for role, element in raw_elements.items()
        }
        fragment_roles = {
            str(operand): MatrixFragmentRole(role)
            for operand, role in raw_fragments.items()
        }
    except ValueError as error:
        raise ValueError(f"invalid matrix fragment role or element type: {error}") from error
    if len(elements) != len(raw_elements) or len(fragment_roles) != len(raw_fragments):
        raise ValueError("matrix element and fragment roles must be unique")
    if len(set(fragment_roles.values())) != len(fragment_roles):
        raise ValueError("matrix fragment roles must be unique")
    if set(fragment_roles.values()) != set(elements):
        raise ValueError("matrix elements must exactly match fragment roles")

    fragments = _normalize_fragments(fragment_roles, elements, layouts, modifiers)
    selectors = _normalize_scale_selectors(raw_selectors, layouts, scale_vector_size)
    if len(fragments) > 4:
        raise ValueError("matrix supports at most four register fragments")
    _validate_fixed_modifiers(
        family, MatrixShape(m=m, n=n, k=k), elements, modifiers,
        a_layout=a_layout, b_layout=b_layout, c_layout=c_layout,
        d_layout=d_layout, kind=kind, bit_operation=bit_operation,
        sparse_order=sparse_order, scale_type=scale_type,
        source_packing=source_packing, scale_vector_size=scale_vector_size,
        destination_packing=destination_packing,
        address_qualifier=address_qualifier,
        transpose=transpose, matrix_count=matrix_count,
    )
    return MatrixSpec(
        family=family,
        shape=MatrixShape(m=m, n=n, k=k),
        a_layout=a_layout,
        b_layout=b_layout,
        c_layout=c_layout,
        d_layout=d_layout,
        elements=tuple(elements.items()),
        fragments=tuple(fragments),
        kind=kind,
        bit_operation=bit_operation,
        scale_type=scale_type,
        source_packing=source_packing,
        destination_packing=destination_packing,
        address_qualifier=address_qualifier,
        transpose=transpose,
        matrix_count=matrix_count,
        scale_vector_size=scale_vector_size,
        sparse_order=sparse_order,
        scale_selectors=tuple(selectors),
    )


def _normalize_scale_selectors(
    raw_selectors: Mapping[str, object],
    layouts: tuple[OperandLayoutSpec, ...],
    vector_size: int,
) -> list[MatrixScaleSelectorSpec]:
    """Bind each block-scale tuple to its semantic matrix and legal ID set."""

    result: list[MatrixScaleSelectorSpec] = []
    byte_mask = {0: 0, 1: 0b1111, 2: 0b0101, 4: 0b0001}[vector_size]
    declared = {
        operand.name
        for layout in layouts
        for operand in layout.operands
        if operand.kind is OperandKind.MATRIX_SCALE_SELECTOR
    }
    if declared != set(raw_selectors):
        raise ValueError("matrix scale selector metadata must name every selector operand")
    for name, raw_role in raw_selectors.items():
        if not isinstance(name, str) or raw_role not in ("a", "b"):
            raise ValueError("scale selector requires a named A/B operand")
        for layout in layouts:
            operand = next((item for item in layout.operands if item.name == name), None)
            if (operand is None or operand.kind is not OperandKind.MATRIX_SCALE_SELECTOR
                    or operand.type_expression is None
                    or operand.type_expression.kind is not OperandTypeExpressionKind.FIXED_SCALAR
                    or operand.type_expression.scalar_type != "u16"):
                raise ValueError("scale selector operand must be a u16 matrix selector")
        role = MatrixFragmentRole(raw_role)
        result.append(MatrixScaleSelectorSpec(
            operand=name, role=role, byte_mask=byte_mask,
            thread_max=1 if role is MatrixFragmentRole.A else 3,
        ))
    return result


def _normalize_fragments(
    fragment_roles: Mapping[str, MatrixFragmentRole],
    elements: Mapping[MatrixFragmentRole, MatrixElementType],
    layouts: tuple[OperandLayoutSpec, ...],
    modifiers: tuple[ModifierSpec, ...],
) -> list[MatrixFragmentShape]:
    """Require every operand layout to preserve one exact fragment contract."""

    contracts: list[tuple[MatrixFragmentShape, ...]] = []
    for layout in layouts:
        operands = {operand.name: operand for operand in layout.operands}
        fragments: list[MatrixFragmentShape] = []
        for operand_name, role in fragment_roles.items():
            operand = operands.get(operand_name)
            if operand is None or operand.kind not in {
                OperandKind.REGISTER_VECTOR, OperandKind.MATRIX_FRAGMENT,
                OperandKind.REGISTER,
            }:
                raise ValueError(f"matrix fragment {operand_name!r} requires a register pack")
            if operand.kind is OperandKind.REGISTER_VECTOR:
                if len(operand.vector_arities) != 1:
                    raise ValueError(f"matrix fragment {operand_name!r} requires fixed arity")
                count = operand.vector_arities[0]
            elif operand.kind is OperandKind.MATRIX_FRAGMENT:
                if operand.minimum_elements != operand.maximum_elements:
                    raise ValueError(f"matrix fragment {operand_name!r} requires exact cardinality")
                assert operand.minimum_elements is not None
                count = operand.minimum_elements
            else:
                count = 1
            expression = operand.type_expression
            if expression is None:
                raise ValueError(f"matrix fragment {operand_name!r} requires register type")
            if expression.kind is OperandTypeExpressionKind.FIXED_SCALAR:
                register_type = expression.scalar_type
            else:
                modifier = next(
                    (item for item in modifiers if item.name == expression.modifier_name),
                    None,
                )
                if modifier is None or modifier.presence is not ModifierPresence.FIXED:
                    raise ValueError(f"matrix fragment {operand_name!r} requires fixed register type")
                register_type = modifier.value
            if not isinstance(register_type, str):
                raise ValueError(f"matrix fragment {operand_name!r} has invalid register type")
            fragments.append(MatrixFragmentShape(
                operand=operand_name,
                role=role,
                element_type=elements[role],
                register_type=register_type,
                register_count=count,
            ))
        contracts.append(tuple(fragments))
    if not contracts or any(contract != contracts[0] for contract in contracts[1:]):
        raise ValueError("matrix operand layouts have incompatible fragment contracts")
    return list(contracts[0])


def _validate_fixed_modifiers(
    family: MatrixFamily,
    shape: MatrixShape,
    elements: Mapping[MatrixFragmentRole, MatrixElementType],
    modifiers: tuple[ModifierSpec, ...],
    *,
    a_layout: MatrixLayout,
    b_layout: MatrixLayout,
    c_layout: MatrixLayout,
    d_layout: MatrixLayout,
    kind: MatrixKind,
    bit_operation: MatrixBitOperation,
    sparse_order: MatrixSparseOrder,
    scale_type: MatrixScaleType,
    source_packing: MatrixElementType | None,
    destination_packing: MatrixElementType | None,
    address_qualifier: MatrixAddressQualifier,
    scale_vector_size: int,
    transpose: bool,
    matrix_count: int,
) -> None:
    """Reject canonical metadata that disagrees with fixed source suffixes."""

    fixed_spellings = tuple(
        spelling
        for modifier in modifiers
        if modifier.presence is ModifierPresence.FIXED
        for spelling in modifier_spellings(modifier)
    )
    packed_suffix_parts = {
        ".b8x16.b6x16_p32": (".b8x16", ".b6x16_p32"),
        ".b8x16.b4x16_p64": (".b8x16", ".b4x16_p64"),
    }
    tokens = tuple(
        part
        for spelling in fixed_spellings
        for part in packed_suffix_parts.get(spelling, (spelling,))
    )
    shape_tokens = [
        match for token in tokens
        if (match := re.fullmatch(r"\.m(\d+)n(\d+)(?:k(\d+))?", token))
    ]
    if len(shape_tokens) != 1:
        raise ValueError("matrix topology requires exactly one fixed shape suffix")
    source_shape = tuple(
        int(value) if value is not None else 0
        for value in shape_tokens[0].groups()
    )
    if source_shape != (shape.m, shape.n, shape.k):
        raise ValueError("matrix shape disagrees with its fixed suffix")
    counts = [int(match.group(1)) for token in tokens
              if (match := re.fullmatch(r"\.x([124])", token))]
    if family in (MatrixFamily.LDMATRIX, MatrixFamily.STMATRIX):
        if len(counts) != 1 or counts[0] != matrix_count:
            raise ValueError("matrix count disagrees with its fixed .x suffix")
    elif counts:
        raise ValueError("only matrix movement permits a fixed .x count")
    if (".trans" in tokens) != transpose:
        raise ValueError("matrix transpose disagrees with its fixed suffix")
    layouts = tuple(token.removeprefix(".") for token in tokens
                    if token in (".row", ".col"))
    expected_layouts = tuple(layout.value for layout in (a_layout, b_layout, c_layout, d_layout)
                             if layout is not MatrixLayout.NONE)
    if layouts != expected_layouts:
        raise ValueError("matrix layouts disagree with fixed source suffixes")
    sparse_tokens = tuple(token for token in tokens
                          if token in (".sp", ".sp::ordered_metadata"))
    expected_sparse = {
        MatrixSparseOrder.NONE: (),
        MatrixSparseOrder.NATIVE: (".sp",),
        MatrixSparseOrder.ORDERED: (".sp::ordered_metadata",),
    }[sparse_order]
    if sparse_tokens != expected_sparse:
        raise ValueError("matrix sparse order disagrees with fixed suffix")
    kinds = tuple(token for token in tokens if token.startswith(".kind::"))
    expected_kind = () if kind is MatrixKind.CLASSIC else (f".kind::{kind.value}",)
    if kinds != expected_kind:
        raise ValueError("matrix numeric kind disagrees with fixed suffix")
    bit_tokens = tuple(token for token in tokens if token in (".xor", ".and"))
    expected_bit = () if bit_operation is MatrixBitOperation.NONE else (
        f".{bit_operation.value}",)
    if bit_tokens != expected_bit or ((".popc" in tokens) != bool(expected_bit)):
        raise ValueError("matrix bit operation disagrees with fixed suffix")
    source_pack_tokens = tuple(token for token in tokens
                               if token in (".b4x16_p64", ".b6x16_p32"))
    expected_pack = () if source_packing is None else (f".{source_packing.value}",)
    if source_pack_tokens != expected_pack:
        raise ValueError("matrix source packing disagrees with fixed suffix")
    destination_pack_tokens = tuple(token for token in tokens
                                    if token == ".b8x16")
    expected_destination_pack = (
        () if destination_packing is None else (f".{destination_packing.value}",)
    )
    if destination_pack_tokens != expected_destination_pack:
        raise ValueError("matrix destination packing disagrees with fixed suffix")
    scale_types = tuple(token for token in tokens
                        if token in (".ue8m0", ".ue4m3"))
    expected_scale_type = () if scale_type is MatrixScaleType.NONE else (f".{scale_type.value}",)
    if scale_types != expected_scale_type:
        raise ValueError("matrix scale type disagrees with fixed suffix")
    address_tokens = tuple(token for token in tokens
                           if token in (".global", ".shared", ".shared::cta"))
    expected_address = (
        () if address_qualifier is MatrixAddressQualifier.NONE
        else (f".{address_qualifier.value}",)
    )
    if address_tokens != expected_address:
        raise ValueError("matrix address qualifier disagrees with fixed suffix")
    scale_vectors = tuple(int(match.group(1)) for token in tokens
                          if (match := re.fullmatch(r"\.scale_vec::([124])X", token)))
    if scale_vectors and scale_vectors != (scale_vector_size,):
        raise ValueError("matrix scale vector size disagrees with fixed suffix")
    fixed_values = {
        modifier.name: modifier.value
        for modifier in modifiers
        if modifier.presence is ModifierPresence.FIXED
    }
    for role in MatrixFragmentRole:
        field = f"{role.value}_type"
        element = elements.get(role)
        if field in fixed_values and (
            element is None or fixed_values[field] != element.value
        ):
            raise ValueError(f"matrix {role.value} element disagrees with {field}")
    if "type" in fixed_values:
        if any(fixed_values["type"] != element.value for element in elements.values()):
            raise ValueError("matrix element disagrees with fixed type suffix")
