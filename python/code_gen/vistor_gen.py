from code_gen.models import *


def are_parameters_consistent(instruction: Instruction) -> bool:
    """Check if the parameters of the instructions are consistent."""
    # Check if the number of parameters matches the expected count
    target_arg = instruction.variants[0].arguments

    flag: bool = True
    for variant in instruction.variants:
        flag &= variant.arguments == target_arg

    return flag


class VisitorCodeGen:
    INSTR_NAME_CPP = "instr"

    def __init__(self, instruction: Instruction):
        self.instruction = instruction

    def get_actual_type_instance_name(self) -> str:

        def _get_type_instance_name() -> str | None:
            target_type = "ScalarType"
            for modifier in self.instruction.variants[0].modifiers:
                if modifier.type_name == target_type:
                    return modifier.name
            return None

        target_variant = self.instruction.variants[0]
        lowest_instance = _get_type_instance_name()
        if lowest_instance is None:
            return "ScalarType::U32"  # default type

        if target_variant.emit_note.kind == EmitKind.Direct:
            return lowest_instance
        elif target_variant.emit_note.kind == EmitKind.SubVariant:
            return f"std::get<{target_variant.emit_note.emit_type}>({self.INSTR_NAME_CPP}.{target_variant.emit_note.instance}).{lowest_instance}"
        else:
            return f"{self.INSTR_NAME_CPP}.{target_variant.emit_note.instance}.{lowest_instance}"

    def gen_arg_check_code(self, arg_instance: str) -> str:
        template = f"""
        if (auto r = v.visit(instr.{arg_instance}, ts, /*is_dst*/ true, false); !r)
            return r;
        """

    def gen_visitor_code(self) -> str:

        tempalte = f"""
        template <OperandLike Op, typename Err>
        expected<void, Err> visit_instr(const {self.instruction.cpp}<Op>& instr,
                                        Visitor<Op, Err>& v) {{
          ScalarType st = {self.get_actual_type_instance_name()};
 
          Type t = make_scalar(st);
          VisitTypeSpace ts{{&t, StateSpace::Reg}};
          if (auto r = v.visit(instr.dst, ts, /*is_dst*/ true, false); !r)
            return r;
          if (auto r = v.visit(instr.src, ts, /*is_dst*/ false, false); !r)
            return r;
          return {{}};
        }}
        """

        return tempalte
        pass
