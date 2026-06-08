from __future__ import annotations

import copy
from pathlib import Path
from typing import Any

import yaml

from code_gen.models import (
    Argument,
    ArgumentKind,
    EmitKind,
    EmitNote,
    Instruction,
    Modifier,
    ModifierKind,
    ModifierValue,
    VariantModel,
)

PROJECT_ROOT = Path(__file__).parent.parent.parent
SHARED_YAML_DIR = PROJECT_ROOT / "instructions" / "_shared"


def _load_shared_meta() -> dict[str, Any]:
    path = SHARED_YAML_DIR / "meta.yaml"
    if not path.exists():
        return {}
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    return data or {}


SHARED_META = _load_shared_meta()


def _syntax_error(ctx: str, message: str) -> ValueError:
    return ValueError(f"{ctx}: {message}")


def _expect_map(value: Any, ctx: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise _syntax_error(ctx, "expected mapping")
    return value


def _expect_list(value: Any, ctx: str) -> list[Any]:
    if not isinstance(value, list):
        raise _syntax_error(ctx, "expected list")
    return value


def _check_unknown_keys(obj: dict[str, Any], allowed: set[str], ctx: str) -> None:
    for key in obj:
        if key not in allowed:
            raise _syntax_error(ctx, f"unknown key {key!r}")


TOP_LEVEL_KEYS = {
    "schema",
    "ptx_isa",
    "category",
    "section",
    "type_sets",
    "operand_patterns",
    "instructions",
    "cpp_backend",
}

INSTRUCTION_KEYS = {"opcode", "doc", "section", "syntax", "operands", "variants"}
VARIANT_KEYS = {"name", "description", "availability", "modifiers", "operands", "rule"}
AVAILABILITY_KEYS = {"ptx", "sm", "family"}
MODIFIER_KEYS = {
    "name",
    "kind",
    "domain",
    "presence",
    "values",
    "value",
    "token",
    "default",
}
OPERAND_KEYS = {"name", "kind", "type"}


class PtxInstructionSpecLoader:
    """Load the declarative PTX instruction schema and lower it to the legacy codegen model.

    The instruction YAML should describe PTX facts. C++ names and enum spelling live under
    the cpp_backend section so changing the generated C++ layout does not require touching
    the instruction facts.
    """

    def __init__(self, raw: dict[str, Any], source: Path):
        self.raw = raw
        self.source = source
        self.type_sets = raw.get("type_sets", {}) or {}
        self.operand_patterns = raw.get("operand_patterns", {}) or {}
        self.cpp_backend = raw.get("cpp_backend", {}) or {}
        self.domains = self.cpp_backend.get("domains", {}) or {}
        self.backend_instructions = self.cpp_backend.get("instructions", {}) or {}

    def validate(self) -> None:
        ctx = str(self.source)
        _check_unknown_keys(self.raw, TOP_LEVEL_KEYS, ctx)
        if self.raw.get("schema") != "ptx-instr/v1":
            raise _syntax_error(ctx, "schema must be 'ptx-instr/v1'")
        if not isinstance(self.raw.get("instructions"), list):
            raise _syntax_error(ctx, "instructions must be a list")

        seen_opcodes: set[str] = set()
        for instr_idx, instr_raw in enumerate(self.raw["instructions"]):
            instr = _expect_map(instr_raw, f"{ctx}:instructions[{instr_idx}]")
            _check_unknown_keys(instr, INSTRUCTION_KEYS, f"{ctx}:instructions[{instr_idx}]")
            opcode = instr.get("opcode")
            if not isinstance(opcode, str) or not opcode:
                raise _syntax_error(f"{ctx}:instructions[{instr_idx}]", "missing opcode")
            if opcode in seen_opcodes:
                raise _syntax_error(ctx, f"duplicate opcode {opcode!r}")
            seen_opcodes.add(opcode)
            if opcode not in self.backend_instructions:
                raise _syntax_error(f"{ctx}:{opcode}", "missing cpp_backend entry")

            variants = instr.get("variants")
            if not isinstance(variants, list) or not variants:
                raise _syntax_error(f"{ctx}:{opcode}", "variants must be a non-empty list")

            seen_variant_names: set[str] = set()
            for variant_idx, variant_raw in enumerate(variants):
                variant_ctx = f"{ctx}:{opcode}.variants[{variant_idx}]"
                variant = _expect_map(variant_raw, variant_ctx)
                _check_unknown_keys(variant, VARIANT_KEYS, variant_ctx)
                name = variant.get("name")
                if not isinstance(name, str) or not name:
                    raise _syntax_error(variant_ctx, "missing variant name")
                if name in seen_variant_names:
                    raise _syntax_error(variant_ctx, f"duplicate variant name {name!r}")
                seen_variant_names.add(name)
                availability = _expect_map(variant.get("availability", {}), variant_ctx + ".availability")
                _check_unknown_keys(availability, AVAILABILITY_KEYS, variant_ctx + ".availability")
                self._validate_modifiers(variant.get("modifiers", []), variant_ctx + ".modifiers")
                self._resolve_operands(variant.get("operands", instr.get("operands")), variant_ctx + ".operands")

    def _validate_modifiers(self, raw_modifiers: Any, ctx: str) -> None:
        if raw_modifiers is None:
            return
        modifiers = _expect_list(raw_modifiers, ctx)
        seen: set[str] = set()
        for idx, raw_modifier in enumerate(modifiers):
            mod_ctx = f"{ctx}[{idx}]"
            modifier = _expect_map(raw_modifier, mod_ctx)
            _check_unknown_keys(modifier, MODIFIER_KEYS, mod_ctx)
            name = modifier.get("name")
            if not isinstance(name, str) or not name:
                raise _syntax_error(mod_ctx, "missing modifier name")
            if name in seen:
                raise _syntax_error(mod_ctx, f"duplicate modifier {name!r}")
            seen.add(name)
            kind = modifier.get("kind")
            if kind not in {"flag", "type", "enum"}:
                raise _syntax_error(mod_ctx, "kind must be flag/type/enum")
            presence = modifier.get("presence")
            if presence not in {"required", "optional", "fixed", "absent"}:
                raise _syntax_error(mod_ctx, "presence must be required/optional/fixed/absent")
            if presence == "required" and not modifier.get("values"):
                raise _syntax_error(mod_ctx, "required modifier must provide values")
            if presence == "fixed" and "value" not in modifier:
                raise _syntax_error(mod_ctx, "fixed modifier must provide value")
            if kind in {"type", "enum"} and modifier.get("domain") not in self.domains:
                raise _syntax_error(mod_ctx, f"unknown value domain {modifier.get('domain')!r}")

    def _resolve_operands(self, operand_ref: Any, ctx: str) -> list[dict[str, Any]]:
        if isinstance(operand_ref, str):
            if operand_ref not in self.operand_patterns:
                raise _syntax_error(ctx, f"unknown operand pattern {operand_ref!r}")
            operands = self.operand_patterns[operand_ref]
        else:
            operands = operand_ref
        operands = _expect_list(operands, ctx)
        for idx, operand_raw in enumerate(operands):
            operand = _expect_map(operand_raw, f"{ctx}[{idx}]")
            _check_unknown_keys(operand, OPERAND_KEYS, f"{ctx}[{idx}]")
            if not isinstance(operand.get("name"), str):
                raise _syntax_error(f"{ctx}[{idx}]", "missing operand name")
            if operand.get("kind") not in {kind.value for kind in ArgumentKind}:
                raise _syntax_error(f"{ctx}[{idx}]", f"unknown operand kind {operand.get('kind')!r}")
        return operands

    def lower(self) -> list[Instruction]:
        self.validate()
        return [self._lower_instruction(raw) for raw in self.raw["instructions"]]

    def _lower_instruction(self, raw: dict[str, Any]) -> Instruction:
        opcode = raw["opcode"]
        backend = self.backend_instructions[opcode]
        instr = Instruction(opcode=opcode)
        instr.doc = raw.get("doc", "")
        instr.cpp = backend["cpp"]

        for raw_variant in raw["variants"]:
            variant = self._lower_variant(raw_variant, raw, backend)
            variant.parent_instruction = instr
            instr.variants.append(variant)
        return instr

    def _lower_variant(
        self,
        raw_variant: dict[str, Any],
        raw_instruction: dict[str, Any],
        instr_backend: dict[str, Any],
    ) -> VariantModel:
        name = raw_variant["name"]
        variant = VariantModel(description=raw_variant.get("description", name))
        availability = raw_variant.get("availability", {}) or {}
        variant.min_ptx_version = float(availability.get("ptx", 0))
        variant.min_sm_version = int(availability.get("sm", 0))
        variant.cpp_struct_name = instr_backend.get("cpp", "")
        variant.emit_note = self._resolve_emit_note(name, instr_backend)

        for raw_modifier in raw_variant.get("modifiers", []) or []:
            modifier = self._lower_modifier(raw_modifier)
            if modifier is None:
                continue
            modifier.parent_variant = variant
            variant.modifiers.append(modifier)

        for raw_operand in self._resolve_operands(
            raw_variant.get("operands", raw_instruction.get("operands")),
            f"{self.source}:{raw_instruction['opcode']}.{name}.operands",
        ):
            arg = self._lower_operand(raw_operand)
            arg.parent_variant = variant
            variant.arguments.append(arg)

        return variant

    def _resolve_emit_note(self, variant_name: str, instr_backend: dict[str, Any]) -> EmitNote:
        emit = copy.deepcopy(instr_backend.get("emit", {}))
        emit.update(copy.deepcopy(instr_backend.get("variants", {}).get(variant_name, {}).get("emit", {})))
        kind = EmitKind(emit.get("kind", "direct"))
        instance = emit.get("instance")
        emit_type = emit.get("type")
        return EmitNote(kind=kind, instance=instance, emit_type=emit_type)

    def _lower_modifier(self, raw: dict[str, Any]) -> Modifier | None:
        presence = raw["presence"]
        if presence == "absent":
            return None
        kind = raw["kind"]
        name = raw["name"]

        if kind == "flag":
            type_name = "bool"
            flag_value = ModifierValue(raw.get("token", f".{name}"), "true")
            if presence == "fixed":
                return Modifier(name, ModifierKind.Fixed, type_name, [], flag_value)
            if presence == "required":
                return Modifier(name, ModifierKind.Fixed, type_name, [], flag_value)
            return Modifier(name, ModifierKind.Optional, type_name, [flag_value])

        domain = self.domains[raw["domain"]]
        type_name = domain["cpp_type"]
        value_map = domain["values"]
        if presence == "fixed":
            return Modifier(name, ModifierKind.Fixed, type_name, [], self._make_modifier_value(value_map, raw["value"]))
        values = [self._make_modifier_value(value_map, value) for value in self._expand_values(raw.get("values", []))]
        return Modifier(name, ModifierKind.Required, type_name, values)

    def _make_modifier_value(self, value_map: dict[str, Any], value: Any) -> ModifierValue:
        key = str(value)
        if key not in value_map:
            raise _syntax_error(str(self.source), f"unknown modifier value {key!r}")
        entry = value_map[key]
        return ModifierValue(token=entry["token"], cpp_code=entry["cpp"])

    def _expand_values(self, values: Any) -> list[Any]:
        expanded: list[Any] = []
        for value in _expect_list(values, str(self.source) + ":values"):
            if isinstance(value, str) and value.startswith("$"):
                set_name = value[1:]
                if set_name not in self.type_sets:
                    raise _syntax_error(str(self.source), f"unknown type set {set_name!r}")
                expanded.extend(self.type_sets[set_name])
            else:
                expanded.append(value)
        return expanded

    def _lower_operand(self, raw: dict[str, Any]) -> Argument:
        arg = Argument(name=raw["name"], kind=ArgumentKind(raw["kind"]))
        if "type" not in raw:
            return arg
        operand_types = raw["type"]
        if not isinstance(operand_types, list):
            operand_types = [operand_types]
        scalar_values = self.domains["scalar_types"]["values"]
        arg.type = [self._make_modifier_value(scalar_values, value) for value in operand_types]
        return arg


def load_instruction_specs(yaml_path: Path) -> list[Instruction]:
    raw = yaml.safe_load(yaml_path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise _syntax_error(str(yaml_path), "ptx-instr/v1 file must be a mapping")
    return PtxInstructionSpecLoader(raw, yaml_path).lower()
