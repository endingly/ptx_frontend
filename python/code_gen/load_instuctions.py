import copy
from pathlib import Path
from code_gen.models import *
import yaml
from code_gen.merge_variant import merge_variants
from code_gen.spec_loader import load_instruction_specs

project_root = Path(__file__).parent.parent.parent

# ── feature !ref functional ───────────────────────────────────────────────────────────────────

# global load shared yamls
yaml_base_dir = project_root / "instructions"
shared_yamls = yaml.safe_load(
    (yaml_base_dir / "_shared/meta.yaml").read_text(encoding="utf-8")
)


def _find_in_shared(obj, key):
    if isinstance(obj, dict):
        if key in obj:
            return obj[key]
        for v in obj.values():
            if isinstance(v, dict) and v.get("token") in (key, "." + key):
                return v
    if isinstance(obj, list):
        token = key if key.startswith(".") else "." + key
        for it in obj:
            if isinstance(it, dict) and it.get("token") == token:
                return it
        try:
            return obj[int(key)]
        except:
            pass
    raise KeyError(f"shared path part '{key}' not found")


def ref_constructor(loader, node):
    path = loader.construct_scalar(node)  # e.g. "scalar_types.u16"
    obj = shared_yamls
    for part in path.split("."):
        if not part:
            continue
        obj = _find_in_shared(obj, part)
    return copy.deepcopy(obj)


yaml.SafeLoader.add_constructor("!ref", ref_constructor)

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
    variant.cpp_struct_name = raw.get("cpp_struct_name", "")  # optional

    for mod_name, mod_def in raw.get("modifiers", {}).items():
        tmp_modifier = _parse_modifier(mod_name, mod_def)
        tmp_modifier.parent_variant = variant
        variant.modifiers.append(tmp_modifier)

    for arg in raw.get("args", []):
        temp_arg = Argument(name=arg["name"], kind=ArgumentKind(arg["kind"]))
        temp_arg.parent_variant = variant
        if "type" in arg:
            types_raw = arg.get("type", [])
            args_list: list[ModifierValue] = []

            # normalize to iterable of dicts
            if isinstance(types_raw, dict):
                types_iter = [types_raw]
            else:
                types_iter = list(types_raw)

            for item in types_iter:
                if not isinstance(item, dict):
                    raise TypeError(f"unexpected arg.type item: {item!r}")
                args_list.append(
                    ModifierValue(token=item["token"], cpp_code=item["cpp"])
                )

            temp_arg.type = args_list
        else:
            temp_arg.type = None

        variant.arguments.append(temp_arg)

    return variant


def _parse_instruction(raw: dict) -> Instruction:
    instr = Instruction(opcode=raw["opcode"])
    instr.doc = raw.get("doc", "")
    instr.cpp = raw.get("cpp", "")
    for v in raw.get("variants", []):
        tmp_variant = _parse_variant(v)
        tmp_variant.parent_instruction = instr
        if tmp_variant.cpp_struct_name == "":
            tmp_variant.cpp_struct_name = instr.cpp
        instr.variants.append(tmp_variant)
    return instr


def _restore_parent_links(instructions: list[Instruction]) -> list[Instruction]:
    for instr in instructions:
        for variant in instr.variants:
            variant.parent_instruction = instr
            for modifier in variant.modifiers:
                modifier.parent_variant = variant
            for arg in variant.arguments:
                arg.parent_variant = variant
    return instructions


def load_instructions(yaml_path: Path) -> list[Instruction]:
    raw_text = yaml_path.read_text(encoding="utf-8")
    docs = yaml.safe_load(raw_text)

    if isinstance(docs, dict) and docs.get("schema") == "ptx-instr/v1":
        # New declarative schema: validate and lower to the legacy codegen model.
        temp_r = load_instruction_specs(yaml_path)
    else:
        # Legacy schema retained for other instruction categories during the migration.
        temp_r = [_parse_instruction(item) for item in docs]

    merged_instructions = merge_variants(temp_r)
    return _restore_parent_links(merged_instructions)


def get_default_value_for_type(type_name: str) -> str:
    TARGET_GROUP_NAME = "default_values_map"
    default_values_map = shared_yamls.get(TARGET_GROUP_NAME, {})
    if type_name in default_values_map:
        return default_values_map[type_name]
    else:
        raise KeyError(f"default value for type '{type_name}' not found")
