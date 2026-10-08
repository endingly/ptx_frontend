"""Closed PTX 9.3 tiled tensor-reduction operation and descriptor-type contract.

The tensor-map descriptor remains opaque. This table states conditional
operation/type compatibility; it does not describe any descriptor instance.
"""

from enum import Enum
from types import MappingProxyType

from ptx_frontend.spec.semantic_domains import SemanticDomain, is_semantic_value


class TensorReductionOp(Enum):
    """Operation encoded by one tiled tensor-reduction instruction form."""

    ADD = "add"
    MIN = "min"
    MAX = "max"
    INC = "inc"
    DEC = "dec"
    AND = "and"
    OR = "or"
    XOR = "xor"


# PTX 9.3, cp.reduce.async.bulk.tensor descriptor element-type restrictions.
TENSOR_REDUCTION_ELEMENT_TYPES = MappingProxyType({
    TensorReductionOp.ADD: ("u32", "s32", "u64", "f32", "f16", "bf16"),
    TensorReductionOp.MIN: ("u32", "s32", "u64", "s64", "f16", "bf16"),
    TensorReductionOp.MAX: ("u32", "s32", "u64", "s64", "f16", "bf16"),
    TensorReductionOp.INC: ("u32",),
    TensorReductionOp.DEC: ("u32",),
    TensorReductionOp.AND: ("b32", "b64"),
    TensorReductionOp.OR: ("b32", "b64"),
    TensorReductionOp.XOR: ("b32", "b64"),
})


def validate_tensor_reduction_element_types() -> None:
    """Reject incomplete, duplicate, or unmodeled conditional type metadata."""

    if set(TENSOR_REDUCTION_ELEMENT_TYPES) != set(TensorReductionOp):
        raise ValueError("tensor reduction type table must cover every operation")
    for operation, types in TENSOR_REDUCTION_ELEMENT_TYPES.items():
        if not types or len(types) != len(set(types)):
            raise ValueError(f"invalid tensor reduction types for {operation.value}")
        for scalar_type in types:
            if not is_semantic_value(SemanticDomain.SCALAR_TYPE, scalar_type):
                raise ValueError(f"unknown tensor reduction type {scalar_type!r}")


validate_tensor_reduction_element_types()
