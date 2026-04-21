from code_gen.models import Instruction, Modifier


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
