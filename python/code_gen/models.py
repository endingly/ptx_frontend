from __future__ import annotations
from enum import Enum
from typing import Type


# helper function
@staticmethod
def to_string(value: bool) -> str:
    return "true" if value else "false"


# ── enum ───────────────────────────────────────────────────────────────────────


class ModifierKind(Enum):
    Required = "required"
    Optional = "optional"
    Fixed = "fixed"


class ArgumentKind(Enum):
    Reg = "reg"
    Reg_Or_Imm = "reg_or_imm"


class EmitKind(Enum):
    SubVariant = "sub_variant"
    SubStruct = "sub_struct"
    Direct = "direct"


# ── data struct ─────────────────────────────────────────────────────────────────────


class EmitNote:

    def __init__(self, kind: EmitKind, instance: str | None, emit_type: str | None):
        self.kind = kind
        self.instance = instance
        self.emit_type = emit_type

    def __repr__(self):
        return f"EmitNote(kind={self.kind.value!r}, instance={self.instance!r}, emit_type={self.emit_type!r})"


class ModifierValue:
    def __init__(self, token: str, cpp_code: str):
        self.token = token
        self.cpp_code = cpp_code

    def __repr__(self):
        return f"ModifierValue({self.token!r} -> {self.cpp_code!r})"


class Modifier:
    def __init__(
        self,
        name: str,
        kind: ModifierKind,
        type_name: str | None,  # enum type name. e.g. "ScalarType"
        values: list[ModifierValue],
        fixed_value: ModifierValue | None = None,
        # parent variant emit note, used for code gen, set when parsing yaml
        parent_variant: VariantModel | None = None,
    ):
        self.name = name
        self.kind = kind
        self.type_name = type_name
        self.values = values
        self.fixed_value = fixed_value  #  only have value with kind=Fixed
        self.parent_variant = parent_variant

    def __repr__(self):
        return f"Modifier({self.name!r}, kind={self.kind.value}, values={self.values})"


class Argument:
    def __init__(self, name: str, kind: ArgumentKind):
        self.name = name
        self.kind = kind
        self.type: list[ModifierValue] | None = None
        # type of the argument, used for type checking
        self.parent_variant: VariantModel | None = None

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
        self.emit_note: EmitNote = EmitNote(EmitKind.Direct, None, None)
        # parent instruction, set when parsing yaml
        self.parent_instruction: Instruction | None = None

    def __repr__(self):
        return (
            f"VariantModel({self.description!r}, "
            f"ptx>={self.min_ptx_version}, sm>={self.min_sm_version}, "
            f"struct={self.cpp_struct_name!r})"
        )


class Instruction:
    def __init__(self, opcode: str):
        self.opcode: str = opcode
        self.doc: str = ""
        self.variants: list[VariantModel] = []
        self.cpp: str = ""

    def __repr__(self):
        return f"Instruction({self.opcode!r}, {len(self.variants)} variants)"
