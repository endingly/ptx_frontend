from code_gen.models import *
import copy
from collections import OrderedDict


def get_all_opcodes(instructions: list[Instruction]) -> set[str]:
    opcodes = set()
    for instr in instructions:
        opcodes.add(instr.opcode)
    return opcodes


def select_instructions_by_opcode(
    instructions: list[Instruction], opcode: str
) -> list[Instruction]:
    selected = []
    for instr in instructions:
        if instr.opcode == opcode:
            selected.append(instr)
    return selected


def merge_variants(instructions: list[Instruction]) -> list[Instruction]:
    """In the inst list, variants belonging to the same opcode are grouped into the same inst entity."""
    all_opcodes = get_all_opcodes(instructions)
    merged_instructions: list[Instruction] = []

    for opcode in all_opcodes:
        selected_instructions = select_instructions_by_opcode(instructions, opcode)
        if len(selected_instructions) == 1:
            merged_instructions.append(selected_instructions[0])
        else:
            # merge variants of the same opcode into one instruction
            merged_instr = Instruction(opcode)
            merged_instr.doc = selected_instructions[0].doc
            merged_instr.cpp = selected_instructions[0].cpp

            for instr in selected_instructions:
                merged_instr.variants.extend(instr.variants)
            merged_instructions.append(merged_instr)

    return merged_instructions


def _merge_modifier_list(mods: list[Modifier]) -> Modifier:
    # pick type_name (first non-empty)
    type_name = next((m.type_name for m in mods if m.type_name), None)

    kinds = {m.kind for m in mods}
    if any(k == ModifierKind.Required for k in kinds):
        kind = ModifierKind.Required
    elif any(k == ModifierKind.Optional for k in kinds):
        kind = ModifierKind.Optional
    else:
        kind = ModifierKind.Fixed

    values: list[ModifierValue] = []
    fixed_value = None

    if kind == ModifierKind.Fixed:
        fixed_vals = [m.fixed_value for m in mods if m.fixed_value is not None]
        if fixed_vals:
            first = fixed_vals[0]
            same = all(
                (fv.token == first.token and fv.cpp_code == first.cpp_code)
                for fv in fixed_vals
            )
            if same:
                fixed_value = copy.deepcopy(first)
            else:
                # conflicting fixed -> escalate to Optional, collect distinct fixed values
                kind = ModifierKind.Optional
                seen = set()
                for fv in fixed_vals:
                    key = (fv.token, fv.cpp_code)
                    if key not in seen:
                        seen.add(key)
                        values.append(copy.deepcopy(fv))

    if kind in (ModifierKind.Optional, ModifierKind.Required):
        seen = set()
        for m in mods:
            if m.values:
                for v in m.values:
                    key = (v.token, v.cpp_code)
                    if key not in seen:
                        seen.add(key)
                        values.append(copy.deepcopy(v))
            if m.fixed_value:
                fv = m.fixed_value
                key = (fv.token, fv.cpp_code)
                if key not in seen:
                    seen.add(key)
                    values.append(copy.deepcopy(fv))

    return Modifier(
        name=mods[0].name,
        kind=kind,
        type_name=type_name,
        values=values,
        fixed_value=fixed_value,
    )


def merge_variant(instruction_input: Instruction):
    """
    Merge variants within one Instruction.
    Groups variants by (emit_kind, emit_type or cpp_struct_name),
    creates one merged VariantModel per group with:
      - modifiers = union( group modifiers ) (merged by _merge_modifier_list)
      - arguments = union by name (preserve first-seen order, merge type lists)
      - constraints: take conservative min (min_ptx_version/min_sm_version)
    Replaces instruction.variants in-place.
    """
    instruction = copy.deepcopy(instruction_input)
    if not instruction.variants:
        return instruction

    # group variants by their emit target
    groups: dict = {}
    for v in instruction.variants:
        key = (v.emit_note.kind, v.emit_note.emit_type or v.cpp_struct_name)
        groups.setdefault(key, []).append(v)

    new_variants: list[VariantModel] = []
    for key, variants in groups.items():
        # merge modifiers by name
        mods_by_name: dict = {}
        for v in variants:
            for m in v.modifiers:
                mods_by_name.setdefault(m.name, []).append(m)

        merged_mods = [_merge_modifier_list(mods) for mods in mods_by_name.values()]

        # merge args, preserve first-seen order
        args_map: OrderedDict = OrderedDict()
        for v in variants:
            for a in v.arguments:
                if a.name not in args_map:
                    na = Argument(name=a.name, kind=a.kind)
                    na.type = copy.deepcopy(a.type) if a.type is not None else None
                    args_map[a.name] = na
                else:
                    existing = args_map[a.name]
                    if a.type:
                        if existing.type is None:
                            existing.type = copy.deepcopy(a.type)
                        else:
                            seen = {(t.token, t.cpp_code) for t in existing.type}
                            for tv in a.type:
                                if (tv.token, tv.cpp_code) not in seen:
                                    existing.type.append(copy.deepcopy(tv))
                                    seen.add((tv.token, tv.cpp_code))

        merged_args = list(args_map.values())

        # build merged VariantModel
        mv = VariantModel(description=f"merged({len(variants)})")
        mv.min_ptx_version = min(v.min_ptx_version for v in variants)
        mv.min_sm_version = min(v.min_sm_version for v in variants)
        mv.emit_note = variants[0].emit_note
        mv.cpp_struct_name = variants[0].cpp_struct_name
        mv.modifiers = merged_mods
        mv.arguments = merged_args
        mv.parent_instruction = instruction

        new_variants.append(mv)

    instruction.variants = new_variants
    return instruction
