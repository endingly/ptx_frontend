import sys
from pathlib import Path

sys.path.append(str(Path(__file__).resolve().parents[1]))

from code_gen.load_instuctions import load_instructions, Instruction
from python.code_gen.type_check_gen import InstructionCodeGen
from argparse import ArgumentParser
from base.utils import format_file_inplace

