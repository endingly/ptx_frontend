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
                args_list = f"{self.emit_note}& d"
            return args_list

        def gen_check_fixed_value():
            return f"d.{self.name} == {self.fixed_value.cpp_code};"  # type: ignore

        def gen_content_func():
            if is_req:
                return f"""
    static constexpr std::array container_cpp_code = {{ { ",".join([value.cpp_code for value in self.values])} }};
    return is_one_of(t, container_cpp_code); // t from instruction
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
                args_list = f"d"
            return args_list

        return f"check_{self.name}({gen_args_list()});"


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

    def __repr__(self):
        return (
            f"VariantModel({self.description!r}, "
            f"ptx>={self.min_ptx_version}, sm>={self.min_sm_version}, "
            f"struct={self.cpp_struct_name!r})"
        )

    def generate_code_for_type_check(self, variant_idx: int = 0):
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
auto check_variant{variant_idx} = [&]() {{
    {check_target_version_function}
    {check_modifiers_function}

    bool flag = check_target_version();
    flag &= {";".join([modifier.function_signature_for_type_check for modifier in self.modifiers])}

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
