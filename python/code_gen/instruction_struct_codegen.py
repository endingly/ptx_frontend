"""
# Design doc:

1.
"""

from code_gen.models import *


class SubInfoCodeGen:
    def __init__(self):
        self.type_name: str = ""  # cpp type name
        self.instance_name: str = ""  # cpp instance name
        self.modifier: list[Modifier] = []
        self.emit_kind: EmitKind = EmitKind.Direct


class InstructionSubInfoTemplate:
    def __init__(self):
        self.name: str = (
            ""  # subinfo type def in cpp, eq to instruction name if emit direct
        )
        self.modifier: list[Modifier] = []
        self.args: list[Argument] = []
        self.kind: EmitKind = EmitKind.Direct
        self.instrcution_name_cpp: str = ""

    @staticmethod
    def _generate_subInfo_cpp_def(mod: Modifier) -> str:
        content = f"{mod.type_name} {mod.name};"
        return content

    @staticmethod
    def _generate_arg_cpp_def(arg: Argument) -> str:
        content = f"Op {arg.name};"
        return content

    def generate_cpp_code(self) -> str:
        if self.kind == EmitKind.Direct:
            content = f"""
            struct {self.instrcution_name_cpp} {{
                // sub_info
                {"\n".join([self._generate_subInfo_cpp_def(mod) for mod in self.modifier])}
                // arg
                {"\n".join([self._generate_arg_cpp_def(arg) for arg in self.args])}
            }};
            """
        elif self.kind == EmitKind.SubStruct:
            content = f"""
            struct {self.instrcution_name_cpp} {{
                struct {self.name} {{
                    // sub_info
                    {";\n".join([self._generate_subInfo_cpp_def(mod) for mod in self.modifier])}
                }};

                // arg
                {";\n".join([self._generate_arg_cpp_def(arg) for arg in self.args])}
            }};
            """
        elif self.kind == EmitKind.SubVariant:
            content = f"""
            struct {self.instrcution_name_cpp} {{
                
            }};
            """

            return content


class InstructionTemplateCodeGen:
    def __init__(self, template: Instruction):
        self.template = template

    def _collect_variant_info(self) -> list[InstructionSubInfoTemplate]:

        def __containes_subInfoTemplate(
            str: str, info_list: list[InstructionSubInfoTemplate]
        ) -> bool:
            for info in info_list:
                if info.name == str:
                    return True
            return False

        def __operator(
            target_cpp_struct_name: str, target_list: list[InstructionSubInfoTemplate]
        ):
            flag = __containes_subInfoTemplate(variant.cpp_struct_name, info_r)
            if not flag:
                info = InstructionSubInfoTemplate()
                info.name = variant.cpp_struct_name
                info.modifier = variant.modifiers
                info_r.append(info)
                # add arg
                info.args = variant.arguments
                info.instrcution_name_cpp = variant.cpp_struct_name
                info.name = variant.emit_note.emit_type or variant.cpp_struct_name
            else:
                for info in info_r:
                    if info.name == variant.cpp_struct_name:
                        if modifier.name not in [item.name for item in info.modifier]:
                            info.modifier.append(modifier)
                            break

        info_r: list[InstructionSubInfoTemplate] = []

        for variant in self.template.variants:
            emit_type: str = variant.emit_note.emit_type or "Default"
            kind = variant.emit_note.kind
            for modifier in variant.modifiers:
                if kind == EmitKind.Direct:
                    __operator(variant.cpp_struct_name, info_r)
                elif kind == EmitKind.SubStruct:
                    __operator(emit_type, info_r)
                elif kind == EmitKind.SubVariant:
                    __operator(emit_type, info_r)
                else:
                    raise ValueError(f"unknown emit kind: {kind}")

        return info_r

    def generate_code(self) -> str:
        content = f"""
        template <OperandLike Op>
        struct {self.template.cpp} {{
        
        }};
        """

        return content


if __name__ == "__main__":
    from load_instuctions import load_instructions
    from pathlib import Path

    input_path = Path("/root/code/ptx_frontend/instructions/integer_arith.yaml")
    instructions = load_instructions(input_path)

    for instruction in instructions:
        if instruction.opcode == "add":
            codegen = InstructionTemplateCodeGen(instruction)
            r = codegen.__getattribute__("_collect_variant_info")()
            break
