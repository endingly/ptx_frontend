"""
the script to generate the code for ptx ir struct definitions, which are used in the ptx ir builder and ptx ir printer.
"""

from argparse import ArgumentParser
from code_gen.instruction_struct_codegen import InstructionStructCodeGen
from code_gen.load_instuctions import load_instructions, Instruction
from pathlib import Path
from base.utils import format_file_inplace

def add_parser():
    parser = ArgumentParser(
        description="Generate type checker code for PTX instructions."
    )
    parser.add_argument(
        "-i",
        "--input",
        type=str,
        required=True,
        help="Path to the YAML file containing instruction definitions.",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=str,
        required=True,
        help="Dir, Path to the output file where the generated code will be saved.",
    )
    return parser

if __name__ == "__main__":
    parser = add_parser()
    args = parser.parse_args()

    input_file = Path(args.input)
    output_dir = Path(args.output)  # "instruction_struct.src.gen"

    instructions = load_instructions(input_file)
    content_r = ""
    for instruction in instructions:
        codegen = InstructionStructCodeGen(instruction)
        content_r += codegen.generate_code()

    src_file = output_dir / "ptx_ir.src.gen"
    with open(src_file, "w") as f:
        f.write(content_r)

    format_file_inplace(src_file.__str__())