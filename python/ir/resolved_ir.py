"""Python model for generated PTX Resolved IR instruction definitions.

The model is derived from the normalized PTX-facing ``InstructionSpec``.  It
describes the semantic fields that must appear in the generated C++ resolved
instruction structs; it does not describe C++ storage or emitter layout.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import Any

from base.utils import file_stem_to_pascal_case
from code_gen.cpp_backend import (
    CppDomain,
    cpp_domain,
    cpp_optional_value,
    cpp_value,
)
from code_gen.model import (
    InstructionSpec,
    ModifierSpec,
    ModifierValueSpec,
    OperandSpec,
    OperandTypeExpression,
    OperandTypeExpressionKind,
    VariantSpec,
)


class ResolvedFieldOrigin(Enum):
    """The PTX specification element that supplies a resolved field."""

    MODIFIER = "modifier"
    OPERAND = "operand"


class ResolvedValueKind(Enum):
    """Runtime C++ value category produced for one resolved field."""

    BOOL = "Bool"
    SCALAR_TYPE = "ScalarType"
    ROUNDING_MODE = "RoundingMode"
    REGISTER = "Register"
    PREDICATE = "Predicate"
    IMMEDIATE = "Immediate"
    REG_OR_IMM = "RegOrImm"
    BRANCH_TARGET = "BranchTarget"
    SPECIAL_REGISTER = "SpecialRegister"
    SYMBOL = "Symbol"
    ADDRESS = "Address"


class ResolvedFieldStorage(Enum):
    """Whether a field is stored per instruction or fixed by its variant."""

    INSTANCE = "Instance"
    STATIC_CONSTANT = "StaticConstant"


class ResolvedOperandRole(Enum):
    """Semantic role of one resolved operand."""

    DESTINATION = "Destination"
    SOURCE = "Source"
    ADDRESS = "Address"
    PREDICATE = "Predicate"
    BRANCH_TARGET = "BranchTarget"
    BARRIER = "Barrier"
    THREAD_COUNT = "ThreadCount"


class ResolvedOperandAccess(Enum):
    """Access mode of one resolved operand."""

    READ = "Read"
    WRITE = "Write"
    READ_WRITE = "ReadWrite"
    CONTROL = "Control"


class ResolvedOperandShape(Enum):
    """Allowed resolved value shapes for one operand position."""

    REGISTER = "Register"
    PREDICATE = "Predicate"
    IMMEDIATE = "Immediate"
    ADDRESS = "Address"
    SYMBOL = "Symbol"
    VECTOR = "Vector"
    BRANCH_TARGET = "BranchTarget"
    SPECIAL_REGISTER = "SpecialRegister"


class ResolvedOperandTypeExpressionKind(Enum):
    """C++ descriptor representation of an operand scalar-type source."""

    NONE = "None"
    FIXED_SCALAR = "FixedScalar"
    MODIFIER_FIELD = "ModifierField"


@dataclass(frozen=True)
class ResolvedOperandTypeExpression:
    """A resolved, descriptor-ready operand scalar-type expression."""

    kind: ResolvedOperandTypeExpressionKind
    scalar_type: str | None = None
    modifier_field_id: str | None = None


@dataclass(frozen=True)
class ResolvedField:
    """One provenance-carrying field in a resolved variant struct."""

    name: str
    value_cpp_type: str
    origin: ResolvedFieldOrigin
    source_name: str
    operand_role: ResolvedOperandRole | None = None
    operand_access: ResolvedOperandAccess | None = None
    allowed_operand_shapes: tuple[ResolvedOperandShape, ...] = ()
    storage: ResolvedFieldStorage = ResolvedFieldStorage.INSTANCE
    constant_value: str | bool | int | None = None

    @property
    def value_kind(self) -> ResolvedValueKind:
        """Return the generic resolver category for this output field."""

        for kind in ResolvedValueKind:
            if (
                cpp_value(CppDomain.RESOLVED_VALUE_CPP_TYPES, kind.value)
                == self.value_cpp_type
            ):
                return kind
        raise ValueError(
            f"C++ backend does not assign resolved value kind to "
            f"{self.value_cpp_type!r}"
        )

    @property
    def cpp_type(self) -> str:
        """The C++ member type used by generated resolved instruction structs."""

        if self.storage is ResolvedFieldStorage.STATIC_CONSTANT:
            return self.value_cpp_type
        return f"WithLocs<{self.value_cpp_type}>"

    @property
    def cpp_constant_expr(self) -> str:
        """Return the generated C++ expression for a fixed modifier value."""

        if self.storage is not ResolvedFieldStorage.STATIC_CONSTANT:
            raise ValueError("only static resolved fields have constant expressions")
        if self.value_cpp_type == "bool" and isinstance(self.constant_value, bool):
            return "true" if self.constant_value else "false"
        if self.value_cpp_type == "ScalarType" and isinstance(self.constant_value, str):
            return cpp_value(CppDomain.SCALAR_TYPES, self.constant_value)
        if self.value_cpp_type == "RoundingMode" and isinstance(
            self.constant_value, str
        ):
            return cpp_value(CppDomain.ROUNDING_MODES, self.constant_value)
        raise ValueError(
            f"field {self.name!r}: unsupported fixed value "
            f"{self.constant_value!r} for {self.value_cpp_type}"
        )


@dataclass(frozen=True)
class ResolvedVariant:
    """One alternative of an opcode's generated C++ ``Variant`` type."""

    variant_id: str
    cpp_name: str
    modifier_fields: tuple[ResolvedField, ...]
    modifier_bindings: tuple["ResolvedModifierBinding", ...]
    operand_layouts: tuple["ResolvedOperandLayout", ...]
    modifier_value_availabilities: tuple["ResolvedModifierValueAvailability", ...]
    availability: tuple[tuple[str, Any], ...]
    rule: str | None

    @property
    def fields(self) -> tuple[ResolvedField, ...]:
        """All fields declared by the variant, in deterministic layout order.

        Operand payloads are layout-local, so layouts may intentionally reuse a
        semantic field name with different resolved representations.
        """

        fields: list[ResolvedField] = list(self.modifier_fields)
        for layout in self.operand_layouts:
            for field in layout.fields:
                fields.append(field)
        return tuple(fields)


@dataclass(frozen=True)
class ResolvedModifierBinding:
    """Bind one syntax modifier kind to a resolved field."""

    source_kind_id: str
    target_field_id: str
    default_value: "ResolvedModifierDefault | None" = None


@dataclass(frozen=True)
class ResolvedModifierDefault:
    """Typed semantic value used when an optional modifier is omitted."""

    value_cpp_type: str
    value: str | bool | int


@dataclass(frozen=True)
class ResolvedModifierValueAvailability:
    """Target requirement attached to one dynamic semantic modifier value."""

    source_kind_id: str
    value_cpp_type: str
    value: str | bool | int
    availability: tuple[tuple[str, Any], ...]


@dataclass(frozen=True)
class ResolvedOperandBinding:
    """Bind one positional syntax operand to a resolved field."""

    target_field_id: str
    type_expression: ResolvedOperandTypeExpression
    role: ResolvedOperandRole
    access: ResolvedOperandAccess
    allowed_shapes: tuple[ResolvedOperandShape, ...]


@dataclass(frozen=True)
class ResolvedOperandLayout:
    """One resolved-field binding layout paired by index with syntax layouts."""

    layout_id: str
    cpp_name: str
    fields: tuple[ResolvedField, ...]
    bindings: tuple[ResolvedOperandBinding, ...]
    availability: tuple[tuple[str, Any], ...]


@dataclass(frozen=True)
class ResolvedInstruction:
    """Resolved IR definition for one PTX opcode, such as ``Add``."""

    opcode: str
    cpp_name: str
    variants: tuple[ResolvedVariant, ...]


_OPERAND_ALLOWED_SHAPES = {
    "reg": (ResolvedOperandShape.REGISTER,),
    "imm": (ResolvedOperandShape.IMMEDIATE,),
    "reg_or_imm": (
        ResolvedOperandShape.REGISTER,
        ResolvedOperandShape.IMMEDIATE,
    ),
    "pred": (ResolvedOperandShape.PREDICATE,),
    "pred_or_not": (ResolvedOperandShape.PREDICATE,),
    "label": (ResolvedOperandShape.BRANCH_TARGET,),
    "sreg": (ResolvedOperandShape.SPECIAL_REGISTER,),
    "symbol": (ResolvedOperandShape.SYMBOL,),
    "addr": (ResolvedOperandShape.ADDRESS,),
}

_OPERAND_ROLES = {
    "dst": ResolvedOperandRole.DESTINATION,
    "src": ResolvedOperandRole.SOURCE,
    "src1": ResolvedOperandRole.SOURCE,
    "src2": ResolvedOperandRole.SOURCE,
    "addr": ResolvedOperandRole.ADDRESS,
    "address": ResolvedOperandRole.ADDRESS,
    "predicate": ResolvedOperandRole.PREDICATE,
    "branch_target": ResolvedOperandRole.BRANCH_TARGET,
    "label": ResolvedOperandRole.BRANCH_TARGET,
    "barrier": ResolvedOperandRole.BARRIER,
    "thread_count": ResolvedOperandRole.THREAD_COUNT,
}

_OPERAND_ACCESS = {
    "read": ResolvedOperandAccess.READ,
    "write": ResolvedOperandAccess.WRITE,
    "readwrite": ResolvedOperandAccess.READ_WRITE,
    "control": ResolvedOperandAccess.CONTROL,
}

def from_instruction_spec(spec: InstructionSpec) -> ResolvedInstruction:
    """Build the resolved instruction model from one normalized PTX spec."""

    return ResolvedInstruction(
        opcode=spec.opcode,
        cpp_name=file_stem_to_pascal_case(spec.opcode),
        variants=tuple(
            _build_variant(spec.opcode, variant) for variant in spec.variants
        ),
    )


def _build_variant(opcode: str, variant: VariantSpec) -> ResolvedVariant:
    active_modifiers = tuple(
        modifier for modifier in variant.modifiers if modifier.presence != "absent"
    )
    modifier_fields = tuple(
        _build_modifier_field(modifier)
        for modifier in active_modifiers
    )
    operand_layouts = tuple(
        _build_operand_layout(
            layout.name,
            layout.operands,
            layout.availability,
            {field.source_name: field.name for field in modifier_fields},
        )
        for layout in variant.operand_layouts
    )

    return ResolvedVariant(
        variant_id=variant.name,
        cpp_name=_variant_cpp_name(opcode, variant.name),
        modifier_fields=modifier_fields,
        modifier_bindings=tuple(
            ResolvedModifierBinding(
                source_kind_id=field.source_name,
                target_field_id=field.name,
                default_value=_build_modifier_default(modifier),
            )
            for modifier, field in zip(
                active_modifiers, modifier_fields, strict=True
            )
        ),
        operand_layouts=operand_layouts,
        modifier_value_availabilities=tuple(
            _build_modifier_value_availability(modifier, value)
            for modifier in variant.modifiers
            for value in modifier.values
            if value.availability
        ),
        availability=tuple(variant.availability.items()),
        rule=variant.rule,
    )


def _build_modifier_default(
    modifier: ModifierSpec,
) -> ResolvedModifierDefault | None:
    if modifier.presence != "optional":
        return None
    if modifier.default is None:
        raise ValueError(
            f"optional modifier {modifier.name!r} has no normalized default"
        )

    try:
        value_cpp_type = cpp_value(
            CppDomain.MODIFIER_VALUE_CPP_TYPES, modifier.kind
        )
    except ValueError as error:
        raise ValueError(
            f"optional modifier {modifier.name!r}: unsupported default for "
            f"modifier kind {modifier.kind!r}"
        ) from error

    if value_cpp_type == "bool" and type(modifier.default) is not bool:
        raise ValueError(
            f"optional flag modifier {modifier.name!r} must have a boolean "
            "default"
        )
    if value_cpp_type == "ScalarType":
        if not isinstance(modifier.default, str):
            raise ValueError(
                f"optional type modifier {modifier.name!r} must have a string "
                "default"
            )
        if modifier.default not in cpp_domain(CppDomain.SCALAR_TYPES).values:
            raise ValueError(
                f"optional type modifier {modifier.name!r} has unsupported "
                f"default {modifier.default!r}"
            )
    if value_cpp_type == "RoundingMode":
        if not isinstance(modifier.default, str):
            raise ValueError(
                f"optional rounding modifier {modifier.name!r} must have a "
                "string default"
            )
        if modifier.default not in cpp_domain(CppDomain.ROUNDING_MODES).values:
            raise ValueError(
                f"optional rounding modifier {modifier.name!r} has unsupported "
                f"default {modifier.default!r}"
            )
    return ResolvedModifierDefault(
        value_cpp_type=value_cpp_type,
        value=modifier.default,
    )


def _build_modifier_value_availability(
    modifier: ModifierSpec, value: ModifierValueSpec
) -> ResolvedModifierValueAvailability:
    try:
        value_cpp_type = cpp_value(
            CppDomain.MODIFIER_VALUE_CPP_TYPES, modifier.kind
        )
    except ValueError as error:
        raise ValueError(
            f"modifier {modifier.name!r}: availability for unsupported modifier "
            f"kind {modifier.kind!r}"
        ) from error
    if value_cpp_type == "ScalarType" and not isinstance(value.value, str):
        raise ValueError(
            f"modifier {modifier.name!r}: scalar-type value must be a string"
        )
    if value_cpp_type == "bool" and not isinstance(value.value, bool):
        raise ValueError(
            f"modifier {modifier.name!r}: flag value must be boolean"
        )
    if value_cpp_type == "RoundingMode":
        if not isinstance(value.value, str):
            raise ValueError(
                f"modifier {modifier.name!r}: rounding value must be a string"
            )
        if value.value not in cpp_domain(CppDomain.ROUNDING_MODES).values:
            raise ValueError(
                f"modifier {modifier.name!r}: unsupported rounding value "
                f"{value.value!r}"
            )
    return ResolvedModifierValueAvailability(
        source_kind_id=modifier.name,
        value_cpp_type=value_cpp_type,
        value=value.value,
        availability=tuple(value.availability.items()),
    )


def _build_operand_layout(
    layout_id: str,
    operands: tuple[OperandSpec, ...],
    availability: dict[str, Any],
    modifier_field_ids: dict[str, str],
) -> ResolvedOperandLayout:
    fields = tuple(_build_operand_field(operand) for operand in operands)
    return ResolvedOperandLayout(
        layout_id=layout_id,
        cpp_name=file_stem_to_pascal_case(layout_id),
        fields=fields,
        bindings=tuple(
            ResolvedOperandBinding(
                target_field_id=field.name,
                type_expression=_resolve_operand_type_expression(
                    operand.type_expression,
                    modifier_field_ids,
                ),
                role=_require_operand_role(field),
                access=_require_operand_access(field),
                allowed_shapes=field.allowed_operand_shapes,
            )
            for operand, field in zip(operands, fields, strict=True)
        ),
        availability=tuple(availability.items()),
    )


def _build_modifier_field(modifier: ModifierSpec) -> ResolvedField:
    try:
        value_cpp_type = cpp_value(
            CppDomain.MODIFIER_VALUE_CPP_TYPES, modifier.kind
        )
    except ValueError as error:
        raise ValueError(
            f"modifier {modifier.name!r}: unsupported resolved modifier kind "
            f"{modifier.kind!r}"
        ) from error

    return ResolvedField(
        name=cpp_optional_value(CppDomain.MODIFIER_FIELD_NAMES, modifier.name)
        or modifier.name,
        value_cpp_type=value_cpp_type,
        origin=ResolvedFieldOrigin.MODIFIER,
        source_name=modifier.name,
        storage=(
            ResolvedFieldStorage.STATIC_CONSTANT
            if modifier.presence == "fixed"
            else ResolvedFieldStorage.INSTANCE
        ),
        constant_value=modifier.value if modifier.presence == "fixed" else None,
    )


def _build_operand_field(operand: OperandSpec) -> ResolvedField:
    try:
        value_cpp_type = cpp_value(
            CppDomain.OPERAND_VALUE_CPP_TYPES, operand.kind
        )
    except ValueError as error:
        raise ValueError(
            f"operand {operand.name!r}: unsupported resolved operand kind "
            f"{operand.kind!r}"
        ) from error

    try:
        role = _OPERAND_ROLES[operand.role or ""]
    except KeyError as error:
        raise ValueError(
            f"operand {operand.name!r}: unsupported resolved operand role "
            f"{operand.role!r}"
        ) from error

    try:
        access = _OPERAND_ACCESS[operand.access or ""]
    except KeyError as error:
        raise ValueError(
            f"operand {operand.name!r}: unsupported resolved operand access "
            f"{operand.access!r}"
        ) from error

    return ResolvedField(
        name=operand.name,
        value_cpp_type=value_cpp_type,
        origin=ResolvedFieldOrigin.OPERAND,
        source_name=operand.name,
        operand_role=role,
        operand_access=access,
        allowed_operand_shapes=_OPERAND_ALLOWED_SHAPES[operand.kind],
    )


def _resolve_operand_type_expression(
    expression: OperandTypeExpression | None,
    modifier_field_ids: dict[str, str],
) -> ResolvedOperandTypeExpression:
    """Map a source-model type expression to its resolved descriptor form."""

    if expression is None:
        return ResolvedOperandTypeExpression(
            kind=ResolvedOperandTypeExpressionKind.NONE,
        )
    if expression.kind is OperandTypeExpressionKind.FIXED_SCALAR:
        assert expression.scalar_type is not None
        if expression.scalar_type not in cpp_domain(CppDomain.SCALAR_TYPES).values:
            raise ValueError(
                f"unsupported fixed operand scalar type {expression.scalar_type!r}"
            )
        return ResolvedOperandTypeExpression(
            kind=ResolvedOperandTypeExpressionKind.FIXED_SCALAR,
            scalar_type=expression.scalar_type,
        )
    if expression.kind is OperandTypeExpressionKind.MODIFIER:
        assert expression.modifier_name is not None
        try:
            field_id = modifier_field_ids[expression.modifier_name]
        except KeyError as error:
            raise ValueError(
                f"operand type expression references unresolved modifier "
                f"{expression.modifier_name!r}"
            ) from error
        return ResolvedOperandTypeExpression(
            kind=ResolvedOperandTypeExpressionKind.MODIFIER_FIELD,
            modifier_field_id=field_id,
        )
    raise AssertionError(f"unhandled operand type expression: {expression.kind}")


def _require_operand_role(field: ResolvedField) -> ResolvedOperandRole:
    if field.operand_role is None:
        raise ValueError(f"operand field {field.name!r} has no semantic role")
    return field.operand_role


def _require_operand_access(field: ResolvedField) -> ResolvedOperandAccess:
    if field.operand_access is None:
        raise ValueError(f"operand field {field.name!r} has no access mode")
    return field.operand_access


def _variant_cpp_name(opcode: str, variant_id: str) -> str:
    prefix = f"{opcode}_"
    if not variant_id.startswith(prefix):
        raise ValueError(
            f"variant {variant_id!r} does not start with opcode prefix {prefix!r}"
        )
    return file_stem_to_pascal_case(variant_id.removeprefix(prefix))
