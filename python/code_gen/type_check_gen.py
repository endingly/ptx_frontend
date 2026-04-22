from code_gen.models import *


class ArgumentCodeGen:
    def __init__(self, argument: Argument):
        self.argument = argument

    def gen_actual_argument_list(self) -> str:

        def get_actual_var_name() -> str:
            """we should get modifier value to gen actual argument"""
            target_modifier_type_name = "ScalarType"  # currently we only support type check on scalar type, so the target modifier type is fixed to "ScalarType"
            if self.argument.parent_variant is None:
                raise ValueError(
                    "Argument must have a parent variant to gen actual argument list"
                )
            for modifier in self.argument.parent_variant.modifiers:
                if modifier.type_name == target_modifier_type_name:
                    return modifier.name

            return "type_"  # default name for type modifier

        if self.argument.parent_variant is None:
            raise ValueError(
                "Argument must have a parent variant to gen actual argument list"
            )
        if self.argument.parent_variant.emit_note.kind == EmitKind.Direct:
            return f"i.{get_actual_var_name()}"
        elif self.argument.parent_variant.emit_note.kind == EmitKind.SubStruct:
            return f"i.{self.argument.parent_variant.emit_note.instance}.{get_actual_var_name()}"
        else:
            return f"std::get<{self.argument.parent_variant.emit_note.emit_type}>(i.{self.argument.parent_variant.emit_note.instance}).{get_actual_var_name()}"

    def gen_code_for_type_check(self) -> str:
        return (
            f"check_operand(i.{self.argument.name}, {self.gen_actual_argument_list()});"
        )


class ModifierCodeGen:
    def __init__(self, modifier: Modifier):
        self.modifier = modifier

    # and then, for code gen

    def gen_formal_parameter_list(self):
        """generate the argument list for the type check function"""
        if self.modifier.parent_variant is None:
            raise ValueError(
                "Modifier must have a parent variant to gen formal parameter list"
            )
        if self.modifier.parent_variant.emit_note.kind == EmitKind.Direct:
            return f"const {self.modifier.type_name}& d"
        else:
            return f"const {self.modifier.parent_variant.emit_note.emit_type}& d"

    def gen_actual_argument_list(self):
        """generate the actual argument list to pass to the type check function"""
        if self.modifier.parent_variant is None:
            raise ValueError(
                "Modifier must have a parent variant to gen actual argument list"
            )
        if self.modifier.parent_variant.emit_note.kind == EmitKind.Direct:
            return f"i.{self.modifier.name}"  # instruction struct self
        elif self.modifier.parent_variant.emit_note.kind == EmitKind.SubVariant:
            return f"std::get<{self.modifier.parent_variant.emit_note.emit_type}>(i.{self.modifier.parent_variant.emit_note.instance})"
        else:
            return f"i.{self.modifier.parent_variant.emit_note.instance}"

    def generate_code_for_type_check(self):
        is_req = self.modifier.kind == ModifierKind.Required
        is_option = self.modifier.kind == ModifierKind.Optional
        is_fixed = self.modifier.kind == ModifierKind.Fixed

        def gen_check_fixed_value():
            if self.modifier.fixed_value is None:
                return 'error("Fixed modifier must have a fixed value"); return false;'
            else:
                return f"""
                if (d.{self.modifier.name} == {self.modifier.fixed_value.cpp_code}) {{
                    return true;
                }} else {{
                    error("Modifier {self.modifier.name} must be {self.modifier.fixed_value.cpp_code}, but got " + to_string(d.{self.modifier.name}));
                    return false;
                }}
                """  # type: ignore

        def gen_content_func():
            if is_req:
                if self.modifier.parent_variant is None:
                    raise ValueError(
                        "Modifier must have a parent variant to gen content function"
                    )
                output_name = (
                    f"d.{self.modifier.name}"
                    if self.modifier.parent_variant.emit_note.kind != EmitKind.Direct
                    else "d"
                )
                return f"""
                static constexpr std::array container_cpp_code = {{ { ",".join([value.cpp_code for value in self.modifier.values])} }};
                bool check_r = is_one_of({output_name}, container_cpp_code); // d from instruction
                if (!check_r) {{
                    error("Modifier {self.modifier.name} has invalid value: " + to_string({output_name}));
                }}
                return check_r;
                """
            elif is_option:
                return "return true;"
            elif is_fixed:
                return gen_check_fixed_value()

        content = f"""
        auto check_{self.modifier.name} = [&]({self.gen_formal_parameter_list()}) {{
            {gen_content_func()}
        }};
        """
        return content

    @property
    def function_signature_for_type_check(self):
        return f"check_{self.modifier.name}({self.gen_actual_argument_list()});"

    def generate_code_for_match(self):
        is_req = self.modifier.kind == ModifierKind.Required
        is_option = self.modifier.kind == ModifierKind.Optional
        is_fixed = self.modifier.kind == ModifierKind.Fixed

        def gen_check_fixed_value():
            if self.modifier.fixed_value is None:
                return "return false;"
            else:
                return f"""
                if (d.{self.modifier.name} == {self.modifier.fixed_value.cpp_code}) {{
                    return true;
                }} else {{
                    return false;
                }}
                """  # type: ignore

        def gen_content_func():
            if is_req:
                if self.modifier.parent_variant is None:
                    raise ValueError(
                        "Modifier must have a parent variant to gen content function"
                    )
                output_name = (
                    f"d.{self.modifier.name}"
                    if self.modifier.parent_variant.emit_note.kind != EmitKind.Direct
                    else "d"
                )
                return f"""
                static constexpr std::array container_cpp_code = {{ { ",".join([value.cpp_code for value in self.modifier.values])} }};
                bool check_r = is_one_of({output_name}, container_cpp_code); // d from instruction
                return check_r;
                """
            elif is_option:
                return "return true;"
            elif is_fixed:
                return gen_check_fixed_value()

        content = f"""
        auto match_{self.modifier.name} = [&]({self.gen_formal_parameter_list()}) {{
            {gen_content_func()}
        }};
        """
        return content

    @property
    def function_signature_for_match(self):
        return f"match_{self.modifier.name}({self.gen_actual_argument_list()});"


class VariantCodeGen:
    # for generator
    REQ_PTX_VERSION_FUNC_NAME = "require_ptx"
    REQ_SM_VERSION_FUNC_NAME = "require_sm"

    def __init__(self, variant: VariantModel, variant_idx: int = 0):
        self.variant = variant
        # for code gen
        self.variant_idx = variant_idx  # will be set later after merging variants

    def generate_code_for_type_check(self):
        # generate code for type check, including check ptx/sm version and modifiers
        check_target_version_function = f"""
        auto check_target_version = [&](){{
            bool flag = true;
            flag &= {self.REQ_PTX_VERSION_FUNC_NAME}({self.variant.min_ptx_version},"{self.variant.description}");
            flag &= {self.REQ_SM_VERSION_FUNC_NAME}({self.variant.min_sm_version},"{self.variant.description}");
            return flag;
        }};
        """

        check_modifiers_function = "\n".join(
            [
                ModifierCodeGen(modifier).generate_code_for_type_check()
                for modifier in self.variant.modifiers
            ]
        )

        # with function idx and function content
        content = f"""
        auto check_variant{self.variant_idx} = [&]() -> bool {{
            {check_target_version_function}
            {check_modifiers_function}

            bool flag = check_target_version();
            {"\n".join([f"flag &= {ModifierCodeGen(modifier).function_signature_for_type_check}" for modifier in self.variant.modifiers])}
            return flag;
        }};
        """

        return content

    @property
    def function_signature_for_type_check(self):
        return f"check_variant{self.variant_idx}()"

    @property
    def function_signature_for_match(self):
        return f"match_variant{self.variant_idx}()"

    def generate_code_for_match(self):
        # generate code for type check, including check ptx/sm version and modifiers
        match_modifiers_function = "\n".join(
            [
                ModifierCodeGen(modifier).generate_code_for_match()
                for modifier in self.variant.modifiers
            ]
        )

        # with function idx and function content
        content = f"""
        auto match_variant{self.variant_idx} = [&]() -> bool {{
            {match_modifiers_function}

            bool flag = true;
            {"\n".join([f"flag &= {ModifierCodeGen(modifier).function_signature_for_match}" for modifier in self.variant.modifiers])}

            {"\n".join([f"{ArgumentCodeGen(arg).gen_code_for_type_check()}" for arg in self.variant.arguments])}
            return flag;
        }};
        """

        return content


class InstructionCodeGen:
    def __init__(self, instruction: Instruction):
        self.instruction = instruction

    @property
    def op_name(self):
        """e.g. for opcode `add`, return `Add`"""
        return self.instruction.opcode[:1].upper() + self.instruction.opcode[1:]

    def generate_code_for_type_check(self):
        # generate code for type check, including all variants
        variant_check_funcs = "\n".join(
            [
                VariantCodeGen(variant, index).generate_code_for_type_check()
                for index, variant in enumerate(self.instruction.variants)
            ]
        )
        variant_match_funcs = "\n".join(
            [
                VariantCodeGen(variant, index).generate_code_for_match()
                for index, variant in enumerate(self.instruction.variants)
            ]
        )

        variant_check_funcs_signatures = [
            VariantCodeGen(variant, index).function_signature_for_type_check
            for index, variant in enumerate(self.instruction.variants)
        ]
        variant_match_funcs_signatures = [
            VariantCodeGen(variant, index).function_signature_for_match
            for index, variant in enumerate(self.instruction.variants)
        ]

        content = f"""
        template <IdentLike Id>
        bool check(const Instr{self.op_name}<ParsedOperand<Id>> &i) {{
            {variant_check_funcs}
            {variant_match_funcs}
        
            {"".join([f'if ({VariantCodeGen(variant, index).function_signature_for_match}) return {VariantCodeGen(variant, index).function_signature_for_type_check};' for index, variant in enumerate(self.instruction.variants)])}
            
            // if reach here, means no variant match the instruction with given modifiers, emit error
            error("illegal modifier: no matching variant found for instruction {self.instruction.opcode} with given modifiers");
            return false;
        }};
        """

        return content
