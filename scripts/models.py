from enum import Enum


# helper function
@staticmethod
def to_string(value: bool) -> str:
    return "true" if value else "false"


# ── enum ───────────────────────────────────────────────────────────────────────


class ModifierKind(Enum):
    Required = "required"
    Optional = "optional"
    Fixed = "fixed"


class ArgumentKind(Enum):
    Reg = "reg"
    Reg_Or_Imm = "reg_or_imm"


# ── data struct ─────────────────────────────────────────────────────────────────────


class ModifierValue:
    def __init__(self, token: str, cpp_code: str):
        self.token = token
        self.cpp_code = cpp_code

    def __repr__(self):
        return f"ModifierValue({self.token!r} -> {self.cpp_code!r})"


class Modifier:
    def __init__(
        self,
        name: str,
        kind: ModifierKind,
        type_name: str | None,  # enum type name. e.g. "ScalarType"
        values: list[ModifierValue],
        fixed_value: ModifierValue | None = None,
        # addtional arg from instruction yaml, used for code emitting
        emit_note: str = "",
    ):
        self.name = name
        self.kind = kind
        self.type_name = type_name
        self.values = values
        self.fixed_value = fixed_value  #  only have value with kind=Fixed
        self.emit_note = emit_note

    def __repr__(self):
        return f"Modifier({self.name!r}, kind={self.kind.value}, values={self.values}, emit_note={self.emit_note!r})"

    def generate_code_for_type_check(self):
        is_req = self.kind == ModifierKind.Required
        is_option = self.kind == ModifierKind.Optional
        is_fixed = self.kind == ModifierKind.Fixed

        # arg gen
        def gen_args_list():
            args_list = ""
            if self.emit_note == "":
                args_list = ""
            else:
                args_list = f"const {self.emit_note}& d"
            return args_list

        def gen_check_fixed_value():
            if self.fixed_value is None:
                return 'error("Fixed modifier must have a fixed value"); return false;'
            else:
                return f"""
                if (d.{self.name} == {self.fixed_value.cpp_code}) {{
                    return true;
                }} else {{
                    error("Modifier {self.name} must be {self.fixed_value.cpp_code}, but got " + to_string(d.{self.name}));
                    return false;
                }}
                """  # type: ignore

        def gen_content_func():
            if is_req:
                return f"""
                static constexpr std::array container_cpp_code = {{ { ",".join([value.cpp_code for value in self.values])} }};
                bool check_r = is_one_of(d.{self.name}, container_cpp_code); // d from instruction
                if (!check_r) {{
                    error("Modifier {self.name} has invalid value: " + to_string(d.{self.name}));
                }}
                return check_r;
                """
            elif is_option:
                return "return true;"
            elif is_fixed:
                return gen_check_fixed_value()

        content = f"""
        auto check_{self.name} = [&]({gen_args_list()}) {{
            {gen_content_func()}
        }};
        """
        return content

    @property
    def function_signature_for_type_check(self):
        def gen_args_list():
            args_list = ""
            if self.emit_note == "":
                args_list = ""
            else:
                args_list = f"std::get<{self.emit_note}>(i.data)"
            return args_list

        return f"check_{self.name}({gen_args_list()});"

    def generate_code_for_match(self):
        is_req = self.kind == ModifierKind.Required
        is_option = self.kind == ModifierKind.Optional
        is_fixed = self.kind == ModifierKind.Fixed

        # arg gen
        def gen_args_list():
            args_list = ""
            if self.emit_note == "":
                args_list = ""
            else:
                args_list = f"const {self.emit_note}& d"
            return args_list

        def gen_check_fixed_value():
            if self.fixed_value is None:
                return "return false;"
            else:
                return f"""
                if (d.{self.name} == {self.fixed_value.cpp_code}) {{
                    return true;
                }} else {{
                    return false;
                }}
                """  # type: ignore

        def gen_content_func():
            if is_req:
                return f"""
                static constexpr std::array container_cpp_code = {{ { ",".join([value.cpp_code for value in self.values])} }};
                bool check_r = is_one_of(d.{self.name}, container_cpp_code); // d from instruction
                return check_r;
                """
            elif is_option:
                return "return true;"
            elif is_fixed:
                return gen_check_fixed_value()

        content = f"""
        auto match_{self.name} = [&]({gen_args_list()}) {{
            {gen_content_func()}
        }};
        """
        return content

    @property
    def function_signature_for_match(self):
        def gen_args_list():
            args_list = ""
            if self.emit_note == "":
                args_list = ""
            else:
                args_list = f"std::get<{self.emit_note}>(i.data)"
            return args_list

        return f"match_{self.name}({gen_args_list()});"


class Argument:
    def __init__(self, name: str, kind: ArgumentKind):
        self.name = name
        self.kind = kind

    def __repr__(self):
        return f"Argument({self.name!r}, {self.kind.value})"


class VariantModel:
    # for generator
    REQ_PTX_VERSION_FUNC_NAME = "require_ptx"
    REQ_SM_VERSION_FUNC_NAME = "require_sm"

    def __init__(self, description: str):
        self.description: str = description
        self.min_ptx_version: float = 0.0
        self.min_sm_version: int = 0
        self.modifiers: list[Modifier] = []
        self.arguments: list[Argument] = []
        self.cpp_struct_name: str = ""
        self.emit_note: str = ""
        # for code gen
        self.variant_idx: int = 0

    def __repr__(self):
        return (
            f"VariantModel({self.description!r}, "
            f"ptx>={self.min_ptx_version}, sm>={self.min_sm_version}, "
            f"struct={self.cpp_struct_name!r})"
        )

    def generate_code_for_type_check(self):
        # generate code for type check, including check ptx/sm version and modifiers
        check_target_version_function = f"""
        auto check_target_version = [&](){{
            bool flag = true;
            flag &= {self.REQ_PTX_VERSION_FUNC_NAME}({self.min_ptx_version},"{self.description}");
            flag &= {self.REQ_SM_VERSION_FUNC_NAME}({self.min_sm_version},"{self.description}");
            return flag;
        }};
        """

        check_modifiers_function = "\n".join(
            [modifier.generate_code_for_type_check() for modifier in self.modifiers]
        )

        # TODO: gen check arguments after resolving simbolic register and immediate value

        # with function idx and function content
        content = f"""
        auto check_variant{self.variant_idx} = [&]() -> bool {{
            {check_target_version_function}
            {check_modifiers_function}

            bool flag = check_target_version();
            {"\n".join([f"flag &= {modifier.function_signature_for_type_check}" for modifier in self.modifiers])}

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
            [modifier.generate_code_for_match() for modifier in self.modifiers]
        )

        # TODO: gen check arguments after resolving simbolic register and immediate value

        # with function idx and function content
        content = f"""
        auto match_variant{self.variant_idx} = [&]() -> bool {{
            {match_modifiers_function}

            bool flag = true;
            {"\n".join([f"flag &= {modifier.function_signature_for_match}" for modifier in self.modifiers])}

            return flag;
        }};
        """

        return content


class Instruction:
    def __init__(self, opcode: str):
        self.opcode: str = opcode
        self.doc: str = ""
        self.variants: list[VariantModel] = []

    def __repr__(self):
        return f"Instruction({self.opcode!r}, {len(self.variants)} variants)"

    @property
    def op_name(self):
        return self.opcode[:1].upper() + self.opcode[1:]

    def generate_code_for_type_check(self):
        # generate code for type check, including all variants
        variant_check_funcs = "\n".join(
            [variant.generate_code_for_type_check() for variant in self.variants]
        )
        variant_match_funcs = "\n".join(
            [variant.generate_code_for_match() for variant in self.variants]
        )

        variant_check_funcs_signatures = [
            variant.function_signature_for_type_check for variant in self.variants
        ]
        variant_match_funcs_signatures = [
            variant.function_signature_for_match for variant in self.variants
        ]

        content = f"""
bool TypeChecker::check(const Instr{self.op_name}<ResolvedOp> &i) {{
    {variant_check_funcs}
    {variant_match_funcs}

    {"".join([f'if ({variant.function_signature_for_match}) return {variant.function_signature_for_type_check};' for variant in self.variants])}
    
    // if reach here, means no variant match the instruction with given modifiers, emit error
    error("illegal modifier: no matching variant found for instruction {self.opcode} with given modifiers");
    return false;
}};

bool TypeChecker::check(const Instr{self.op_name}<ParsedOp> &i) {{
    {variant_check_funcs}
    {variant_match_funcs}

    {"".join([f'if ({variant.function_signature_for_match}) return {variant.function_signature_for_type_check};' for variant in self.variants])}

    // if reach here, means no variant match the instruction with given modifiers, emit error
    error("illegal modifier: no matching variant found for instruction {self.opcode} with given modifiers");
    return false;
}};
        """

        return content

    @property
    def function_signature_for_type_check(self):
        return f"""
bool check(const Instr{self.op_name}<ResolvedOp> &i);
bool check(const Instr{self.op_name}<ParsedOp> &i);
        """
