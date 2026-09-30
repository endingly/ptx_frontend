"""Map canonical matrix forms onto identical generated C++ storage layouts."""

from __future__ import annotations

from dataclasses import dataclass

from ptx_frontend.code_gen.resolved_field_names import (
    field_cpp_constant_expr,
    field_cpp_type,
)
from ptx_frontend.ir.resolved_ir import (
    ResolvedField,
    ResolvedFieldStorage,
    ResolvedInstruction,
    ResolvedVariant,
)
from ptx_frontend.spec.model import CodegenUnit


@dataclass(frozen=True)
class MatrixStoragePlan:
    """Canonical logical-to-physical mapping in first-occurrence order."""

    representatives: tuple[int, ...]
    storage_indexes: tuple[int, ...]

    def storage_name(self, instruction: ResolvedInstruction, logical_index: int) -> str:
        """Return the representative C++ struct for one logical matrix form."""

        return instruction.variants[self.representatives[
            self.storage_indexes[logical_index]
        ]].cpp_name


def _fields_key(fields: tuple[ResolvedField, ...], backend: CodegenUnit) -> tuple:
    """Include every data member and immutable static value affecting C++ layout."""

    return tuple(
        (
            field.name,
            field_cpp_type(field, backend=backend),
            field.storage.value,
            field_cpp_constant_expr(field, backend=backend)
            if field.storage is ResolvedFieldStorage.STATIC_CONSTANT else None,
        )
        for field in fields
    )


def _storage_key(variant: ResolvedVariant, backend: CodegenUnit) -> tuple:
    """Exclude source and topology facts, which the owned logical tag selects."""

    return (
        variant.condition_code_effect,
        variant.completion_kind,
        _fields_key(variant.modifier_fields, backend),
        tuple(
            (layout.cpp_name, _fields_key(layout.fields, backend))
            for layout in variant.operand_layouts
        ),
    )


def matrix_storage_plan(
    instruction: ResolvedInstruction, backend: CodegenUnit
) -> MatrixStoragePlan | None:
    """Deduplicate only opcodes whose every form has canonical matrix metadata."""

    if not instruction.variants or not any(v.matrix for v in instruction.variants):
        return None
    if not all(v.matrix for v in instruction.variants):
        raise ValueError(f"matrix opcode {instruction.opcode!r} has mixed forms")

    indexes: dict[tuple, int] = {}
    representatives: list[int] = []
    storage_indexes: list[int] = []
    for logical_index, variant in enumerate(instruction.variants):
        key = _storage_key(variant, backend)
        if key not in indexes:
            indexes[key] = len(representatives)
            representatives.append(logical_index)
        storage_indexes.append(indexes[key])
    return MatrixStoragePlan(tuple(representatives), tuple(storage_indexes))
