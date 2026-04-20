from pathlib import Path
from code_gen.models import *
import yaml

# ── parse functional ───────────────────────────────────────────────────────────────────


def _parse_modifier(name: str, mod_def: dict) -> Modifier:
    kind = ModifierKind(mod_def["kind"])
    values: list[ModifierValue] = []
    type_name: str | None = mod_def.get("type")  # optional field
    fixed_value: ModifierValue | None = None

    if kind == ModifierKind.Fixed:
        fixed_value = ModifierValue(
            mod_def["fixed_value"]["token"], mod_def["fixed_value"]["cpp"]
        )
    else:
        for v in mod_def.get("values", []):
            values.append(ModifierValue(token=v["token"], cpp_code=v["cpp"]))

    return Modifier(
        name=name,
        kind=kind,
        type_name=type_name,
        values=values,
        fixed_value=fixed_value,
    )


def _parse_emit_note(raw: dict) -> EmitNote:
    kind = EmitKind(raw["kind"])
    instance: str | None = raw.get("instance")
    emit_type: str | None = raw.get("emit_type")
    return EmitNote(kind=kind, instance=instance, emit_type=emit_type)


def _parse_variant(raw: dict) -> VariantModel:
    variant = VariantModel(description=raw.get("description", ""))

    constraints = raw.get("constraints", {})
    variant.min_ptx_version = float(constraints.get("min_ptx_version", 0))
    variant.min_sm_version = int(constraints.get("min_sm", 0))
    variant.emit_note = _parse_emit_note(raw.get("emit_note", {}))

    for mod_name, mod_def in raw.get("modifiers", {}).items():
        tmp_modifier = _parse_modifier(mod_name, mod_def)
        tmp_modifier.parent_variant = variant
        variant.modifiers.append(tmp_modifier)

    for arg in raw.get("args", []):
        temp_arg = Argument(name=arg["name"], kind=ArgumentKind(arg["kind"]))
        temp_arg.parent_variant = variant
        variant.arguments.append(temp_arg)

    return variant


def _parse_instruction(raw: dict) -> Instruction:
    instr = Instruction(opcode=raw["opcode"])
    instr.doc = raw.get("doc", "")
    instr.cpp = raw.get("cpp", "")
    for v in raw.get("variants", []):
        tmp_variant = _parse_variant(v)
        instr.variants.append(tmp_variant)
    return instr


def load_instructions(yaml_path: Path) -> list[Instruction]:
    docs = yaml.safe_load(yaml_path.read_text(encoding="utf-8"))
    return [_parse_instruction(item) for item in docs]
