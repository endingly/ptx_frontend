# python/code_gen/model.py
from dataclasses import dataclass
from typing import Any

# -----------------------------------------------------------------------------
# PTX instruction spec normalized model
# -----------------------------------------------------------------------------


@dataclass(frozen=True)
class ModifierSpec:
    """
    A normalized PTX modifier from instruction spec YAML.

    Example PTX spec:

        - name: sat
          kind: flag
          presence: optional
          default: false
          token: ".sat"

        - name: type
          kind: type
          domain: scalar_types
          presence: required
          values:
            - "$add_integer_scalar"

    After normalization, referenced value sets are expanded, so values becomes:

        ("u16", "u32", "u64", "s16", "s32", "s64")
    """

    name: str
    kind: str
    presence: str
    domain: str | None = None
    values: tuple[str, ...] = ()
    value: str | bool | int | None = None
    token: str | None = None
    default: str | bool | int | None = None


@dataclass(frozen=True)
class OperandSpec:
    """
    A normalized PTX operand.

    Example PTX spec:

        - name: dst
          kind: reg
          role: dst
          access: write
          type:
            expr: "$type"

    Normalized as:

        OperandSpec(
            name="dst",
            kind="reg",
            role="dst",
            access="write",
            type_expr="$type",
        )
    """

    name: str
    kind: str
    role: str | None = None
    access: str | None = None
    type_expr: str | None = None


@dataclass(frozen=True)
class VariantSpec:
    """
    One PTX instruction variant.

    For add, examples include:

        add_integer_no_sat
        add_sat_s32
        add_simd_no_sat_sm90
        add_packed_optional_sat_sm120
        add_sat_sm120
    """

    name: str
    availability: dict[str, Any]
    modifiers: tuple[ModifierSpec, ...]
    operands: tuple[OperandSpec, ...]
    rule: str | None = None


@dataclass(frozen=True)
class InstructionSpec:
    """
    One normalized PTX instruction.

    Example:

        opcode = "add"
        syntax = "add{.sat}.{type} dst, src1, src2"
        variants = (...)
    """

    opcode: str
    syntax: str | None
    variants: tuple[VariantSpec, ...]


# -----------------------------------------------------------------------------
# C++ backend normalized model
# -----------------------------------------------------------------------------


@dataclass(frozen=True)
class DomainBackend:
    """
    Backend mapping for one semantic domain.

    Example backend YAML:

        scalar_types:
          cpp_type: ScalarType
          values:
            u32: ScalarType::U32
            s32: ScalarType::S32
          default: ScalarType::U32

    Normalized as:

        DomainBackend(
            cpp_type="ScalarType",
            values={
                "u32": "ScalarType::U32",
                "s32": "ScalarType::S32",
            },
            default="ScalarType::U32",
        )
    """

    cpp_type: str
    values: dict[str, str]
    default: str | None = None


@dataclass(frozen=True)
class ModifierBackend:
    """
    Backend mapping for one modifier.

    Example backend YAML:

        type:
          field: type_
          cpp_type: ScalarType
          domain: scalar_types

        sat:
          field: sat
          cpp_type: bool
          default: "false"
    """

    field: str
    cpp_type: str | None = None
    domain: str | None = None
    default: str | None = None


@dataclass(frozen=True)
class OperandBackend:
    """
    Backend mapping for one operand.

    Example backend YAML:

        dst:
          field: dst
          cpp_type: Operand
          state_space: StateSpace::Reg

    For now, this minimal model only stores field/cpp_type.
    More fields such as state_space can be added when gen_checker/gen_parser
    starts using them.
    """

    field: str
    cpp_type: str


@dataclass(frozen=True)
class EmitAlternativeBackend:
    name: str
    variants: tuple[str, ...] = ()

@dataclass(frozen=True)
class EmitBackend:
    """
    How a PTX instruction is represented inside generated C++ IR.

    Example:

        emit:
          kind: sub_variant
          instance: data
          type: Data
          alternatives:
            - name: IntegerData

    Meaning:

        struct InstrAdd {
            using Data = std::variant<IntegerData>;
            Data data;
            ...
        };
    """

    kind: str
    instance: str | None = None
    # sub_struct:
    #     nested struct name
    #
    # sub_variant:
    #     variant alias name
    type: str | None = None

    # sub_variant:
    #     nested alternative struct names
    alternatives: tuple[EmitAlternativeBackend, ...] = ()


@dataclass(frozen=True)
class InstructionBackend:
    """
    C++ backend mapping for one PTX instruction.

    Example:

        add:
          cpp: InstrAdd
          emit:
            kind: sub_variant
            instance: data
            type: ArithInteger

          modifiers:
            sat: ...
            type: ...

          operands:
            dst: ...
            src1: ...
            src2: ...

          type_checker:
            rule: integer_arith::check_add

          visitor:
            visit_name: visitAdd

          printer:
            modifier_order: [sat, type]
            operand_order: [dst, src1, src2]
    """

    opcode: str
    cpp: str
    emit: EmitBackend
    modifiers: dict[str, ModifierBackend]
    operands: dict[str, OperandBackend]
    type_checker_rule: str | None = None
    visitor_name: str | None = None
    modifier_order: tuple[str, ...] = ()
    operand_order: tuple[str, ...] = ()


# -----------------------------------------------------------------------------
# Top-level codegen model
# -----------------------------------------------------------------------------


@dataclass(frozen=True)
class CodegenUnit:
    """
    The unified normalized model consumed by all C++ generators.

    It is produced by:

        PTX spec YAML
          +
        C++ backend YAML
          ↓
        schema validation
          ↓
        semantic validation
          ↓
        normalization
          ↓
        CodegenUnit

    Then different generators consume the same CodegenUnit:

        gen_ir.py
        gen_checker.py
        gen_printer.py
        gen_visitor.py
        gen_registry.py
    """

    # Schema/version metadata.
    spec_schema: str
    backend_schema: str

    # Instruction category, for example:
    #   integer_arithmetic
    #   floating_point
    #   data_movement
    category: str

    # C++ namespace for generated code.
    namespace: str

    # C++ includes needed by generated IR/header files.
    #
    # Keep each include as a complete token:
    #   "<variant>"
    #   "\"ptx_frontend/ir/operand.hpp\""
    includes: tuple[str, ...] | None

    # Normalized PTX instruction specs.
    instructions: tuple[InstructionSpec, ...]

    # C++ backend mappings, keyed by opcode.
    backends: dict[str, InstructionBackend]

    # Backend domains, keyed by domain name.
    domains: dict[str, DomainBackend]
