from pathlib import Path
from models import *
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


def _parse_variant(raw: dict) -> VariantModel:
    variant = VariantModel(description=raw.get("description", ""))

    constraints = raw.get("constraints", {})
    variant.min_ptx_version = float(constraints.get("min_ptx_version", 0))
    variant.min_sm_version = int(constraints.get("min_sm", 0))

    # emit_note: null in YAML becomes None in Python; treat as empty string
    emit_note_raw = raw.get("emit_note")
    variant.emit_note = emit_note_raw if emit_note_raw is not None else ""

    # data_kind: how to access i.data when generating modifier checks
    data_kind_raw = raw.get("data_kind", "variant")
    variant.data_kind = DataKind(data_kind_raw)

    for mod_name, mod_def in raw.get("modifiers", {}).items():
        tmp_modifier = _parse_modifier(mod_name, mod_def)
        tmp_modifier.emit_note = variant.emit_note
        tmp_modifier.data_kind = variant.data_kind
        variant.modifiers.append(tmp_modifier)

    for arg in raw.get("args", []):
        variant.arguments.append(
            Argument(name=arg["name"], kind=ArgumentKind(arg["kind"]))
        )

    variant.cpp_struct_name = raw.get("cpp_struct", "")

    return variant


def _parse_instruction(raw: dict) -> Instruction:
    instr = Instruction(opcode=raw["opcode"])
    instr.doc = raw.get("doc", "")
    for v in raw.get("variants", []):
        tmp_variant = _parse_variant(v)
        tmp_variant.variant_idx = len(instr.variants)
        instr.variants.append(tmp_variant)
    return instr


def load_instructions(yaml_path: Path) -> list[Instruction]:
    docs = yaml.safe_load(yaml_path.read_text(encoding="utf-8"))
    return [_parse_instruction(item) for item in docs]
