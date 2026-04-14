from __future__ import annotations
from enum import Enum
from typing import Optional


# ── enum ───────────────────────────────────────────────────────────────────────


class ModifierKind(Enum):
    Required = "required"
    Optional = "optional"
    Fixed = "fixed"


class ArgumentKind(Enum):
    Reg = "reg"
    Reg_Or_Imm = "reg_or_imm"
    Addr = "addr"
    Imm = "imm"
    Label = "label"
    LabelOrReg = "label_or_reg"
    Pred = "pred"
    RegList = "reg_list"
    OptionalReg = "optional_reg"
    OptionalPred = "optional_pred"
    OptionalImm = "optional_imm"
    OptionalRegOrImm = "optional_reg_or_imm"
    OptionalRegList = "optional_reg_list"

    @classmethod
    def _missing_(cls, value: object):
        """Accept any unknown kind as a generic string kind (for future extensibility)."""
        obj = object.__new__(cls)
        obj._value_ = value
        obj._name_ = str(value)
        return obj


# ── data struct ─────────────────────────────────────────────────────────────────────


class ModifierValue:
    def __init__(self, token: str, cpp_code: str):
        self.token = token
        self.cpp_code = cpp_code
        self.min_sm: int = 0
        self.min_ptx_version: float = 0.0

    def __repr__(self):
        return f"ModifierValue({self.token!r} -> {self.cpp_code!r})"


class Modifier:
    def __init__(
        self,
        name: str,
        kind: ModifierKind,
        type_name: Optional[str],  # enum type name. e.g. "ScalarType"
        values: list[ModifierValue],
        fixed_value: Optional[ModifierValue] = None,
        emit_note: str = "",
    ):
        self.name = name
        self.kind = kind
        self.type_name = type_name
        self.values = values
        self.fixed_value = fixed_value  # only set when kind=Fixed
        self.emit_note = emit_note

    def __repr__(self):
        return f"Modifier({self.name!r}, kind={self.kind.value}, values={self.values}, emit_note={self.emit_note!r})"

    # ── code generation helpers ──────────────────────────────────────────────

    def _cpp_type(self) -> str:
        """Return the C++ field type name, e.g. 'ScalarType', 'bool', 'RoundingMode'."""
        return self.type_name or "bool"

    def generate_field_check(self, data_var: str = "d") -> str:
        """Return a C++ expression (bool) that validates this modifier on *data_var*.

        For Required modifiers the field is checked against the allowed set.
        For Optional modifiers the check always succeeds (any value is legal).
        For Fixed modifiers the field is compared to the fixed cpp value.
        """
        if self.kind == ModifierKind.Fixed:
            cpp = self.fixed_value.cpp_code if self.fixed_value else "true"  # type: ignore[union-attr]
            return f"({data_var}.{self.name} == {cpp})"
        elif self.kind == ModifierKind.Required and self.values:
            vals = ", ".join(v.cpp_code for v in self.values)
            return (
                f"([&]{{ "
                f"static constexpr auto _allowed = std::to_array<{self._cpp_type()}>({{{vals}}}); "
                f"return is_one_of({data_var}.{self.name}, _allowed); "
                f"}}())"
            )
        else:
            # Optional — always valid
            return "true"


class Argument:
    def __init__(self, name: str, kind: ArgumentKind):
        self.name = name
        self.kind = kind

    def __repr__(self):
        return f"Argument({self.name!r}, {self.kind.value})"


class VariantModel:
    def __init__(self, description: str):
        self.description: str = description
        self.min_ptx_version: float = 0.0
        self.min_sm_version: int = 0
        self.modifiers: list[Modifier] = []
        self.arguments: list[Argument] = []
        self.cpp_struct_name: str = ""
        self.emit_note: str = ""

    def __repr__(self):
        return (
            f"VariantModel({self.description!r}, "
            f"ptx>={self.min_ptx_version}, sm>={self.min_sm_version}, "
            f"struct={self.cpp_struct_name!r})"
        )

    def generate_check_body(self, data_expr: str = "instr.data") -> str:
        """Return the inner body of a check lambda for this variant.

        The body:
         1. Checks min PTX version and SM version.
         2. Checks each Required/Fixed modifier value against the allowed set.
         3. Returns true iff all checks pass (errors reported via error()).
        """
        lines: list[str] = []
        desc = self.description.replace('"', '\\"')

        lines.append(f'// variant: {desc}')
        if self.min_ptx_version > 0:
            lines.append(
                f'if (!require_ptx({self.min_ptx_version}f, "{desc}")) return false;'
            )
        if self.min_sm_version > 0:
            lines.append(
                f'if (!require_sm({self.min_sm_version}, "{desc}")) return false;'
            )

        for mod in self.modifiers:
            if mod.kind in (ModifierKind.Required, ModifierKind.Fixed) and mod.values or mod.kind == ModifierKind.Fixed:
                check_expr = mod.generate_field_check(data_expr)
                lines.append(f'if (!{check_expr}) return false;')

        lines.append('return true;')
        return '\n    '.join(lines)


class Instruction:
    def __init__(self, opcode: str):
        self.opcode: str = opcode
        self.doc: str = ""
        self.variants: list[VariantModel] = []

    def __repr__(self):
        return f"Instruction({self.opcode!r}, {len(self.variants)} variants)"

    def cpp_safe_name(self) -> str:
        """Convert opcode to a valid C++ identifier, e.g. 'add.cc' → 'add_cc'."""
        return self.opcode.replace(".", "_").replace(" ", "_")

