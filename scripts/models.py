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


# Types for which a to_string() overload exists in the runtime
_TYPES_WITH_TO_STRING = {"ScalarType", "bool"}


class ArgumentKind(Enum):
    Reg = "reg"
    Reg_Or_Imm = "reg_or_imm"
    Imm = "imm"


# How the instruction's .data field is accessed when generating modifier checks.
#   variant      – std::get<emit_note>(i.data)   (data is a std::variant)
#   direct_struct – i.data directly, field access via d.{modifier_name}
#   direct_scalar – i.data directly, checked as a scalar value (is_one_of(d, ...))
#   none         – skip modifier type checks entirely
class DataKind(Enum):
    Variant = "variant"
    DirectStruct = "direct_struct"
    DirectScalar = "direct_scalar"
    None_ = "none"


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
        # how to access instruction data field (set by VariantModel)
        data_kind: "DataKind" = None,  # type: ignore[assignment]
    ):
        self.name = name
        self.kind = kind
        self.type_name = type_name
        self.values = values
        self.fixed_value = fixed_value  #  only have value with kind=Fixed
        self.emit_note = emit_note
        self.data_kind: DataKind = data_kind if data_kind is not None else DataKind.Variant

    def __repr__(self):
        return f"Modifier({self.name!r}, kind={self.kind.value}, values={self.values}, emit_note={self.emit_note!r}, data_kind={self.data_kind})"

    # ── helpers ──────────────────────────────────────────────────────────────

    def _has_data_access(self) -> bool:
        """Return True when this modifier can inspect i.data at runtime."""
        return self.emit_note != "" and self.data_kind != DataKind.None_

    def _gen_lambda_param(self) -> str:
        """C++ lambda parameter (empty string when no data access)."""
        if not self._has_data_access():
            return ""
        return f"const {self.emit_note}& d"

    def _gen_call_arg(self) -> str:
        """C++ argument passed when calling the lambda."""
        if not self._has_data_access():
            return ""
        if self.data_kind == DataKind.Variant:
            return f"std::get<{self.emit_note}>(i.data)"
        # direct_struct / direct_scalar
        return "i.data"

    def _gen_field_expr(self) -> str:
        """C++ expression that yields the modifier's value from 'd'."""
        if self.data_kind == DataKind.DirectScalar:
            # d IS the scalar value; no field access needed
            return "d"
        # variant or direct_struct: access via field name
        return f"d.{self.name}"

    def _gen_to_string(self, expr: str) -> str:
        """Produce a to_string() call only for types that have one defined."""
        if self.type_name in _TYPES_WITH_TO_STRING:
            return f"to_string({expr})"
        # Fallback: cast the enum/struct to int for a rudimentary representation
        return f"std::to_string(static_cast<int>({expr}))"

    # ── type-check code generation ────────────────────────────────────────────

    def generate_code_for_type_check(self) -> str:
        if not self._has_data_access():
            # Cannot inspect i.data – emit a no-op lambda
            return f"""
        auto check_{self.name} = [&]() -> bool {{
            // modifier '{self.name}' check skipped: no data accessor configured
            return true;
        }};
        """

        is_req = self.kind == ModifierKind.Required
        is_option = self.kind == ModifierKind.Optional
        is_fixed = self.kind == ModifierKind.Fixed

        field = self._gen_field_expr()

        def gen_check_fixed_value():
            if self.fixed_value is None:
                return 'error("Fixed modifier must have a fixed value"); return false;'
            return f"""
                if ({field} == {self.fixed_value.cpp_code}) {{
                    return true;
                }} else {{
                    error("Modifier {self.name} must be {self.fixed_value.cpp_code}, but got " + {self._gen_to_string(field)});
                    return false;
                }}
                """

        def gen_content_func():
            if is_req:
                values_str = ", ".join(v.cpp_code for v in self.values)
                return f"""
                static constexpr std::array container_cpp_code = {{ {values_str} }};
                bool check_r = is_one_of({field}, container_cpp_code);
                if (!check_r) {{
                    error("Modifier {self.name} has invalid value: " + {self._gen_to_string(field)});
                }}
                return check_r;
                """
            elif is_option:
                return "return true;"
            elif is_fixed:
                return gen_check_fixed_value()

        return f"""
        auto check_{self.name} = [&]({self._gen_lambda_param()}) -> bool {{
            {gen_content_func()}
        }};
        """

    @property
    def function_signature_for_type_check(self) -> str:
        if not self._has_data_access():
            return f"check_{self.name}();"
        return f"check_{self.name}({self._gen_call_arg()});"

    # ── match code generation ─────────────────────────────────────────────────

    def generate_code_for_match(self) -> str:
        if not self._has_data_access():
            return f"""
        auto match_{self.name} = [&]() -> bool {{
            return true;
        }};
        """

        is_req = self.kind == ModifierKind.Required
        is_option = self.kind == ModifierKind.Optional
        is_fixed = self.kind == ModifierKind.Fixed

        field = self._gen_field_expr()

        def gen_check_fixed_value():
            if self.fixed_value is None:
                return "return false;"
            return f"""
                if ({field} == {self.fixed_value.cpp_code}) {{
                    return true;
                }} else {{
                    return false;
                }}
                """

        def gen_content_func():
            if is_req:
                values_str = ", ".join(v.cpp_code for v in self.values)
                return f"""
                static constexpr std::array container_cpp_code = {{ {values_str} }};
                bool check_r = is_one_of({field}, container_cpp_code);
                return check_r;
                """
            elif is_option:
                return "return true;"
            elif is_fixed:
                return gen_check_fixed_value()

        return f"""
        auto match_{self.name} = [&]({self._gen_lambda_param()}) -> bool {{
            {gen_content_func()}
        }};
        """

    @property
    def function_signature_for_match(self) -> str:
        if not self._has_data_access():
            return f"match_{self.name}();"
        return f"match_{self.name}({self._gen_call_arg()});"


class Argument:
    # Maps ArgumentKind → (C++ helper name, human-readable label)
    _KIND_INFO: dict[ArgumentKind, tuple[str, str]] = {
        ArgumentKind.Reg: ("operand_is_reg", "a register"),
        ArgumentKind.Reg_Or_Imm: ("operand_is_reg_or_imm", "a register or immediate"),
        ArgumentKind.Imm: ("operand_is_imm", "an immediate value"),
    }

    def __init__(self, name: str, kind: ArgumentKind):
        self.name = name
        self.kind = kind

    def __repr__(self):
        return f"Argument({self.name!r}, {self.kind.value})"

    def generate_code_for_type_check(self) -> str:
        helper, label = self._KIND_INFO[self.kind]
        return f"""
        auto check_arg_{self.name} = [&]() -> bool {{
            if (!{helper}(i.{self.name})) {{
                error("Argument '{self.name}' must be {label}");
                return false;
            }}
            return true;
        }};
        """

    @property
    def function_signature_for_type_check(self) -> str:
        return f"check_arg_{self.name}();"


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
        self.data_kind: DataKind = DataKind.Variant
        # for code gen
        self.variant_idx: int = 0

    def __repr__(self):
        return (
            f"VariantModel({self.description!r}, "
            f"ptx>={self.min_ptx_version}, sm>={self.min_sm_version}, "
            f"struct={self.cpp_struct_name!r}, data_kind={self.data_kind})"
        )

    def generate_code_for_type_check(self):
        # generate code for type check, including check ptx/sm version, modifiers, and args
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

        check_args_function = "\n".join(
            [arg.generate_code_for_type_check() for arg in self.arguments]
        )

        modifier_calls = "\n".join(
            [f"flag &= {modifier.function_signature_for_type_check}" for modifier in self.modifiers]
        )
        arg_calls = "\n".join(
            [f"flag &= {arg.function_signature_for_type_check}" for arg in self.arguments]
        )

        # with function idx and function content
        content = f"""
        auto check_variant{self.variant_idx} = [&]() -> bool {{
            {check_target_version_function}
            {check_modifiers_function}
            {check_args_function}

            bool flag = check_target_version();
            {modifier_calls}
            {arg_calls}

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
        # generate code for matching, checking only modifier values (not args / version)
        match_modifiers_function = "\n".join(
            [modifier.generate_code_for_match() for modifier in self.modifiers]
        )

        modifier_calls = "\n".join(
            [f"flag &= {modifier.function_signature_for_match}" for modifier in self.modifiers]
        )

        # with function idx and function content
        content = f"""
        auto match_variant{self.variant_idx} = [&]() -> bool {{
            {match_modifiers_function}

            bool flag = true;
            {modifier_calls}

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
