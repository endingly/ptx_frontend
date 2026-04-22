import sys
from pathlib import Path

sys.path.append(str(Path(__file__).resolve().parents[1]))

from code_gen.load_instuctions import load_instructions, Instruction
from code_gen.type_check_gen import InstructionCodeGen
from argparse import ArgumentParser
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


def generate_src_code_for_type_check(instructions: list[Instruction]):
    content = "\n".join(
        [
            InstructionCodeGen(instruction).generate_code_for_type_check()
            for instruction in instructions
        ]
    )
    return content


if __name__ == "__main__":
    parser = add_parser()
    args = parser.parse_args()

    input_file = Path(args.input)
    output_dir = Path(args.output)  # "type_checker.gen"

    instructions = load_instructions(input_file)
    src_code = generate_src_code_for_type_check(instructions)

    src_file = output_dir / "type_checker.src.gen"
    with open(src_file, "w") as f:
        f.write(src_code)

    format_file_inplace(src_file.__str__())
