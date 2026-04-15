from pathlib import Path
from models import *
import yaml

# ── custom YAML loader with !include support ──────────────────────────────────

class _IncludeLoader(yaml.SafeLoader):
    """yaml.SafeLoader extended with a !include constructor.

    !include paths are resolved relative to the directory that contains the
    YAML file being loaded.  The included file is expected to contain a
    single top-level list (or a mapping whose first value is a list).
    """

    _base_dir: Path = Path(".")

    @classmethod
    def _construct_include(cls, loader: "_IncludeLoader", node: yaml.Node):
        rel = loader.construct_scalar(node)  # type: ignore[arg-type]
        # Use the loader's own class _base_dir (set by the _Loader subclass)
        base = getattr(loader.__class__, "_base_dir", cls._base_dir)
        target = base / rel
        content = yaml.safe_load(target.read_text(encoding="utf-8"))
        # Shared files may be a mapping like {_key: &anchor [...]};
        # extract the first list value.
        if isinstance(content, dict):
            content = next(iter(content.values()))
        return content

_IncludeLoader.add_constructor("!include", _IncludeLoader._construct_include)


# ── parse functional ───────────────────────────────────────────────────────────────────


def _parse_modifier(name: str, mod_def: dict) -> Modifier:
    kind = ModifierKind(mod_def["kind"])
    values: list[ModifierValue] = []
    type_name: str | None = mod_def.get("type")  # optional field
    fixed_value: ModifierValue | None = None

    if kind == ModifierKind.Fixed:
        fv = mod_def["fixed_value"]
        if isinstance(fv, dict):
            fixed_value = ModifierValue(token=fv["token"], cpp_code=fv["cpp"])
        else:
            # Legacy plain-string format: e.g. ".approx"
            fixed_value = ModifierValue(token=str(fv), cpp_code=str(fv))
    else:
        for v in mod_def.get("values", []):
            mv = ModifierValue(token=v["token"], cpp_code=v["cpp"])
            # Per-value min_sm / min_ptx_version constraints (optional)
            mv.min_sm = int(v.get("min_sm", 0))
            mv.min_ptx_version = float(v.get("min_ptx_version", 0.0))
            values.append(mv)

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
    variant.emit_note = raw.get("emit_note", "")

    for mod_name, mod_def in raw.get("modifiers", {}).items():
        tmp_modifier = _parse_modifier(mod_name, mod_def)
        tmp_modifier.emit_note = variant.emit_note
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
        instr.variants.append(_parse_variant(v))
    return instr


def load_instructions(yaml_path: Path) -> list[Instruction]:
    text = yaml_path.read_text(encoding="utf-8")
    base_dir = yaml_path.parent

    class _Loader(_IncludeLoader):
        pass
    _Loader._base_dir = base_dir
    _Loader.add_constructor("!include", _IncludeLoader._construct_include)

    docs = yaml.load(text, Loader=_Loader)  # noqa: S506 – we control the Loader
    if docs is None:
        return []
    return [_parse_instruction(item) for item in docs]


def load_all_instructions(instructions_dir: Path) -> list[Instruction]:
    """Load every non-shared YAML in *instructions_dir*."""
    all_instrs: list[Instruction] = []
    for p in sorted(instructions_dir.glob("*.yaml")):
        if p.stem.startswith("_"):
            continue
        all_instrs.extend(load_instructions(p))
    return all_instrs

