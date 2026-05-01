from code_gen.models import *
from code_gen.merge_variant import merge_variant


class InstructionStructCodeGen:
    def __init__(self, instruction: Instruction):
        """init function

        Args:
            instruction (Instruction): instruction data struct before merge
        """
        self.instruction = instruction
        self.inst_after_merge = merge_variant(instruction)

    def _generate_sub_info_struct(self) -> str:
        content_r = ""

        for variant in self.inst_after_merge.variants:
            if variant.emit_note.kind != EmitKind.Direct:
                content = f"""
                struct {variant.emit_note.emit_type} {{
                    {";\n".join(f"{m.type_name} {m.name}" for m in variant.modifiers)};
                }};
                """
                content_r += content
        return content_r

    @property
    def variant_type_def(self) -> str:
        """
        get the type definition of the variant field in the instruction struct.
        If there is no variant, return empty string.
        """
        filter_list = [
            variant
            for variant in self.inst_after_merge.variants
            if variant.emit_note.kind == EmitKind.SubVariant
        ]
        if not filter_list:
            return ""
        list_r: list[str] = [
            variant.emit_note.emit_type or "Unkown" for variant in filter_list
        ]
        content = f"std::variant<{" ,".join(list_r)}>"
        return content

    @property
    def sub_info_kind(self) -> EmitKind:
        return self.inst_after_merge.variants[0].emit_note.kind

    @property
    def sub_structs_type_def_name(self) -> str:
        if self.sub_info_kind == EmitKind.SubStruct:
            return self.inst_after_merge.variants[0].emit_note.emit_type or "Unknown"
        return "Unknown"

    @staticmethod
    def _generate_operand_def(arg: Argument) -> str:
        return f"Op {arg.name}"

    def _generate_instruction_struct(self, variant: VariantModel) -> str:
        if self.sub_info_kind == EmitKind.SubStruct:
            content = f"""
            template <OperandLike Op>
            struct {self.inst_after_merge.cpp} {{
                

                // sub info
                {self.sub_structs_type_def_name} data;
            
                {";\n".join(self._generate_operand_def(arg) for arg in variant.arguments)};
            }};
            """
        elif self.sub_info_kind == EmitKind.SubVariant:
            content = f"""
            template <OperandLike Op>
            struct {self.inst_after_merge.cpp} {{
                // sub info
                {self.variant_type_def} data;
            
                {";\n".join(self._generate_operand_def(arg) for arg in variant.arguments)};
            }};
            """
        else:
            content = f"""
            template <OperandLike Op>
            struct {self.inst_after_merge.cpp} {{
                {";\n".join(f"{m.type_name} {m.name}" for m in variant.modifiers)};
            
                {";\n".join(self._generate_operand_def(arg) for arg in variant.arguments)};
            }};
            """

        return content

    def generate_code(self) -> str:
        content = self._generate_sub_info_struct()
        for variant in self.inst_after_merge.variants:
            content += self._generate_instruction_struct(variant)
        return content


if __name__ == "__main__":
    from load_instuctions import load_instructions
    from pathlib import Path

    input_path = Path("/root/code/ptx_frontend/instructions/integer_arith.yaml")
    instructions = load_instructions(input_path)

    from base.utils import *
    from code_gen.merge_variant import merge_variant

    output_path = Path("/root/code/ptx_frontend/instruction_struct.src.gen")
    content_r = ""
    for instruction in instructions:
        r = merge_variant(instruction)
        codegen = InstructionStructCodeGen(instruction)
        content_r += codegen.generate_code()

    with open(output_path, "w") as f:
        f.write(content_r)
    format_file_inplace(output_path.__str__())
