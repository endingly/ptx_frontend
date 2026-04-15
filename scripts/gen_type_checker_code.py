#!/usr/bin/env python3
"""
gen_type_checker_code.py
────────────────────────
Reads every instruction YAML in ../instructions/ and generates two C++ files:

  include/type_checker_gen.hpp   - declarations of check_<opcode>() methods
  src/type_checker_gen.cpp       - implementations

Run from the repository root:

    python3 scripts/gen_type_checker_code.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

_SCRIPT_DIR = Path(__file__).parent
_REPO_ROOT = _SCRIPT_DIR.parent
sys.path.insert(0, str(_SCRIPT_DIR))

from load_instuctions import load_all_instructions
from models import Instruction, VariantModel, ModifierKind

INSTRUCTIONS_DIR = _REPO_ROOT / "instructions"
OUT_HPP = _REPO_ROOT / "include" / "type_checker_gen.hpp"
OUT_CPP = _REPO_ROOT / "src" / "type_checker_gen.cpp"

_AUTOGEN_NOTICE = """\
// ============================================================
// AUTO-GENERATED -- do NOT edit by hand.
// Re-generate with:  python3 scripts/gen_type_checker_code.py
// ============================================================
"""

# Structs that are NOT function templates (they don't take <Op>).
_NON_TEMPLATE_STRUCTS: set[str] = {
    "InstrTrap",
    "InstrBrkpt",
    "InstrRet",
    "InstrGridDepControl",
    "InstrMembar",
    "InstrCpAsyncCommitGroup",
    "InstrCpAsyncWaitAll",
    "InstrCpAsyncBulkCommitGroup",
}

# Canonical mapping from YAML cpp_struct → actual C++ struct name.
# None means "skip - no matching C++ struct or the struct needs special handling".
_STRUCT_NAME_MAP: dict[str, str | None] = {
    # No matching C++ struct
    "InstrBrxIdx":            None,
    "InstrSlct":              None,
    "InstrExit":              None,
    "InstrMad24":             None,
    "InstrCnot":              None,
    "InstrLop3":              None,
    "InstrBarSync":           None,
    "InstrBarArrive":         None,
    "InstrBarWarpSync":       None,
    "InstrMbarrierPendingCount": None,
    "InstrPmevent":           None,
    "InstrStacksave":         None,
    "InstrStackrestore":      None,
    "InstrMatchSync":         None,
    "InstrIsSpacep":          None,
    "InstrTcgen05MmaWs":      None,
    "InstrTcgen05Mma":        None,
    "InstrCpReduceAsyncBulk": None,
    "InstrCpReduceAsyncBulkTensor": None,
    "InstrTex":               None,
    "InstrTld4":              None,
    # data is ScalarType directly -- handled specially
    "InstrSelp":              None,
    # InstrPrmt has no data field at all
    "InstrPrmt":              None,
    # Name mismatches
    "InstrLdmatrix":          "InstrLdMatrix",
    "InstrStmatrix":          "InstrStMatrix",
    "InstrBarrierSync":       "InstrClusterBarrier",
    "InstrBarrierArrive":     "InstrClusterBarrier",
    "InstrBarrierClusterArrive": "InstrClusterBarrier",
    "InstrBarrierClusterWait":   "InstrClusterBarrier",
    "InstrSetMaxNreg":        "InstrSetMaxNReg",
    "InstrMbarrierArriveExpectTx": "InstrMbarrierExpectTx",
    "InstrMbarrierComplTx":   "InstrMbarrierCompleteTx",
    "InstrVoteSync":          "InstrVote",
    "InstrGridDepControlLaunchDependents": "InstrGridDepControl",
    "InstrGridDepControlWait":             "InstrGridDepControl",
    "InstrMmaSync":           "InstrMma",
    "InstrWgmmaMmaAsync":     "InstrWgmmaMmaAsync",
    "InstrTcgen05CommitGroup":    None,  # InstrTcgen05CommitArrival has no data field
    # atom/red: typ field is Type (not ScalarType) -- complex type mismatch, skip
    "InstrAtom":              None,
    "InstrRed":               None,
    # cvta: 'to' modifier is a fixed syntactic token, not a CvtaDetails field
    "InstrCvta":              None,
    "InstrTcgen05WaitGroup":      "InstrTcgen05Wait",
    "InstrTcgen05RelinquishAllocPermit": "InstrTcgen05Relinquish",
}

# emit_note values that are actually std::variant alternatives in data.
# For these, the generator emits holds_alternative<T>/get<T>(instr.data).
# All other emit_notes mean data IS that type directly -- no holds_alternative needed.
_VARIANT_EMIT_NOTES: set[str] = {
    "ArithInteger",   # variant alternative of ArithDetails (InstrAdd/Sub)
    "ArithFloat",     # variant alternative of ArithDetails (InstrAdd/Sub) or MulDetails/MadDetails
    "MulInt",         # variant alternative of MulDetails
    "MadInt",         # variant alternative of MadDetails
    "MinMaxInt",      # variant alternative of MinMaxDetails
    "MinMaxFloat",    # variant alternative of MinMaxDetails
    "DivInt",         # variant alternative of DivDetails
    "DivFloat",       # variant alternative of DivDetails
}

# For these cpp_struct names, data IS the emit_note type DIRECTLY (not a variant).
# Do NOT use holds_alternative/get for these.
_DIRECT_DATA_STRUCTS: set[str] = {
    "InstrFma",     # data: ArithFloat  (direct, not variant<..., ArithFloat>)
    "InstrRcp",     # data: RcpData     (direct, not variant)
    "InstrSqrt",    # data: RcpData     (direct)
}

# Structs where instr.data IS a scalar enum or primitive type directly.
# Key = cpp_struct name (resolved), Value = (C++ type name, YAML modifier name that maps to data)
# For these structs: is_one_of(instr.data, ...) instead of is_one_of(instr.data.modifier_name, ...)
_SCALAR_DATA_STRUCTS: dict[str, tuple[str, str]] = {
    # ScalarType-direct structs (logic/shift/bit operations)
    "InstrAnd":       ("ScalarType", "type_"),
    "InstrBrev":      ("ScalarType", "type_"),
    "InstrBfe":       ("ScalarType", "type_"),
    "InstrBfi":       ("ScalarType", "type_"),
    "InstrClz":       ("ScalarType", "type_"),
    "InstrCopysign":  ("ScalarType", "type_"),
    "InstrNot":       ("ScalarType", "type_"),
    "InstrOr":        ("ScalarType", "type_"),
    "InstrPopc":      ("ScalarType", "type_"),
    "InstrRem":       ("ScalarType", "type_"),
    "InstrShl":       ("ScalarType", "type_"),
    "InstrTanh":      ("ScalarType", "type_"),
    "InstrXor":       ("ScalarType", "type_"),
    # BmskMode-direct struct
    "InstrBmsk":      ("BmskMode",   "mode"),
    # GridDepControlAction-direct struct
    "InstrGridDepControl": ("GridDepControlAction", "action"),
    # Instructions with incorrect emit_note but ScalarType data
    "InstrSad":       ("ScalarType", "type_"),
}

# Modifier types that are std::variant, complex structs, or require special handling.
# We skip field-level checks for these.
_SKIP_FIELD_CHECK_TYPES: set[str] = {
    # setp/set comparison types (std::variant<SetpCompareInt, SetpCompareFloat>)
    "SetpCompareOp",
    "SetpBoolPostOp",
    # Synchronization / memory
    "BarRedOp",
    "MemScope",
    "MembarLevel",
    "FenceProxyKind",
    "LdStQualifier",
    "RawLdStQualifier",
    # Atomic ops
    "AtomicOp",
    "AtomSemantics",
    # Cache
    "EvictionPriority",
    "PrefetchSize",
    "CpAsyncCacheOperator",
    "LdCacheOperator",
    "StCacheOperator",
    # Address spaces
    "StateSpace",
    # Shift/permute
    "PrmtMode",
    # Warp ops
    "ShuffleMode",
    "VoteMode",
    "ReduxOp",
    # Data movement
    "VectorPrefix",
    "Type",
    # Matrix
    "Tcgen05Shape",
    "Tcgen05Num",
    "Tcgen05PackMode",
    "Tcgen05UnpackMode",
    "MmaLayout",
    "WgmmaLayout",
    "MmaType",
    "MatrixNumber",   # LdMatrixDetails.number -- would need MatrixNumber enum values
    "MmaShape",       # complex nested enum values in mma
    # Rcp kind is std::variant<std::monostate, RcpCompliant> -- skip
    "RcpKind",
    # DivFloat::Kind is a nested enum -- skip
    "DivFloat::Kind",
    # Conversion
    "CvtMode",
    "RoundingMode",   # nested/complex usage
    # Float qualifiers that aren't struct fields
    "TrigKind",
    "Ex2Kind",
    "TanhKind",
    "RsqrtKind",
    "DivFloatKind",
    # LdMatrix: MatrixNumber enum values need code update -- skip for now
    "MatrixNumber",
    # Warp shuffle uses ShflMode in YAML but actual type is ShuffleMode
    "ShflMode",
    # atom/red: qualifier and op have incompatible type names
    "AtomQualifier",
    "AtomOp",
    # fence proxy: FenceProxyKind is complex
    "FenceProxyKind",
}

# C++ reserved keywords that cannot be used as local variable names.
_CPP_RESERVED_KEYWORDS: set[str] = {
    "volatile", "const", "register", "static", "inline", "extern",
    "auto", "class", "struct", "union", "enum", "namespace",
    "template", "typename", "this", "new", "delete", "operator",
    "if", "else", "for", "while", "do", "switch", "case", "break",
    "continue", "return", "goto", "default", "try", "catch", "throw",
    "virtual", "override", "final", "explicit", "mutable", "friend",
    "signed", "unsigned", "long", "short", "int", "bool", "char",
    "float", "double", "void", "nullptr", "true", "false",
    "sizeof", "alignof", "typeid", "decltype", "alignas", "constexpr",
    "noexcept", "static_assert", "using", "typedef",
}

# Modifier field names in YAML that don't directly map to a field on the data struct.
# These are fixed syntactic tokens (like .sync, .aligned, .approx) that the parser
# validates but the type-checker struct doesn't store as a field.
_SKIP_MODIFIER_FIELD_NAMES: set[str] = {
    # data_movement.yaml legacy names
    "type",
    # ld/st qualifier and complex ld/st fields
    "qualifier", "nc", "global", "scope", "weak", "cop",
    "level", "level_cache_hint", "level_eviction_priority", "level_prefetch_size",
    "primary_priority", "vec", "space", "ss", "ifrnd",
    # cvt complex fields
    "frnd2", "x2_to_type", "x2_from_type", "dtype", "stype", "atype",
    "rn", "satfinite", "f8x2type", "from_f32", "convert_type",
    "s32", "b32", "b64", "sat", "size", "relu",
    # misc.yaml complex fields
    "u32", "comp", "completion_mechanism", "dim", "geom",
    "dst_space", "src_space", "a", "v4", "op",
    # ldmatrix/stmatrix: fixed syntactic tokens (not struct fields)
    "sync", "aligned",
    # float_arith: approx kind tokens -- not struct fields for FlushToZero/TypeFtz
    "kind",
    # ldmatrix/stmatrix b16 is a fixed type token (not a LdMatrixDetails field)
    "b16",
    # mma: btype contains ScalarType values (S4/U4/B1) that don't exist in the enum
    "btype",
    # mma: xor_mode is not a MmaDetails field
    "xor_mode",
    # mma: row_col (combined layout) is complex
    "row_col",
    # bar.red, vote: pred is a fixed syntactic token, not a struct field
    "pred",
    # membar, fence: proxy is a fixed syntactic token, not a struct field
    "proxy",
    # tcgen05.commit, tcgen05.wait: group is a fixed token, not a struct field
    "group",
    # fence: these are fields of fence variant alternatives, not FenceDetails itself
    "sem", "sync_restrict", "op_restrict", "to_from", "async_generic",
}


def _resolve_struct(cpp_struct: str) -> str | None:
    if cpp_struct in _STRUCT_NAME_MAP:
        return _STRUCT_NAME_MAP[cpp_struct]
    return cpp_struct


def _escape_cpp_string(s: str) -> str:
    s = s.replace("\\", "\\\\")
    s = s.replace('"', '\\"')
    s = re.sub(r'[^\x20-\x7e]', lambda m: f"\\x{ord(m.group(0)):02x}", s)
    s = " ".join(s.split())
    return s


def _should_skip_modifier(mod_name: str, type_name: str | None) -> bool:
    """Return True if this modifier's field check should be omitted."""
    if mod_name in _SKIP_MODIFIER_FIELD_NAMES:
        return True
    if mod_name in _CPP_RESERVED_KEYWORDS:
        return True
    if type_name in _SKIP_FIELD_CHECK_TYPES:
        return True
    return False


def _generate_variant_check(variant: VariantModel, variant_idx: int, cpp_struct: str) -> str:
    """Generate a C++ lambda check_v{variant_idx} = [&]() -> bool { ... }.

    Handles three data access patterns:
    1. Scalar-data structs: data IS the value (ScalarType/BmskMode enum directly)
    2. Direct-struct data: data IS a named struct, no variant wrapper
    3. Variant-data structs: data is std::variant<A, B, ...> -- use holds_alternative/get
    """
    emit_note = variant.emit_note
    desc = _escape_cpp_string(variant.description)

    lines: list[str] = []
    lines.append(f"  // variant {variant_idx}: {desc}")
    lines.append(f"  auto check_v{variant_idx} = [&]() -> bool {{")
    if variant.min_ptx_version > 0:
        lines.append(f'    if (!require_ptx({variant.min_ptx_version}f, "{desc}")) return false;')
    if variant.min_sm_version > 0:
        lines.append(f'    if (!require_sm({variant.min_sm_version}u, "{desc}")) return false;')

    # --- Determine data access pattern ---
    is_scalar_data = cpp_struct in _SCALAR_DATA_STRUCTS
    is_direct_struct = cpp_struct in _DIRECT_DATA_STRUCTS
    # Use holds_alternative/get only when emit_note IS a variant alternative
    # AND the struct is not in the "direct" or "scalar" sets
    use_variant_access = (
        emit_note in _VARIANT_EMIT_NOTES
        and not is_direct_struct
        and not is_scalar_data
    )

    if use_variant_access:
        detail_type = emit_note
        lines.append(f"    if (!std::holds_alternative<{detail_type}>(instr.data)) return false;")
        lines.append(f"    [[maybe_unused]] auto& d = std::get<{detail_type}>(instr.data);")
        data_var = "d"
    else:
        # direct struct or direct scalar -- access data directly
        data_var = "instr.data"

    if is_scalar_data:
        # data IS the scalar value -- handle the key modifier specially
        scalar_type, key_mod_name = _SCALAR_DATA_STRUCTS[cpp_struct]
        for mod in variant.modifiers:
            if mod.name != key_mod_name:
                # All other modifiers for scalar-data structs are fixed syntactic tokens
                # (like .b32, .approx) that don't have struct fields -- skip them
                continue
            if mod.kind == ModifierKind.Fixed:
                if mod.fixed_value:
                    check = f"(instr.data == {mod.fixed_value.cpp_code})"
                    mod_desc = _escape_cpp_string(f"{desc}: modifier {mod.name} check failed")
                    lines.append(f'    if (!({check})) {{ error("{mod_desc}"); return false; }}')
            elif mod.kind == ModifierKind.Required and mod.values:
                vals = ", ".join(v.cpp_code for v in mod.values)
                check = (
                    f"([&]{{ "
                    f"static constexpr auto _allowed = std::to_array<{scalar_type}>({{{vals}}}); "
                    f"return is_one_of(instr.data, _allowed); "
                    f"}}())"
                )
                mod_desc = _escape_cpp_string(f"{desc}: modifier {mod.name} check failed")
                lines.append(f'    if (!({check})) {{ error("{mod_desc}"); return false; }}')
    else:
        # Normal struct (direct or variant alternative) -- access fields by name
        for mod in variant.modifiers:
            if _should_skip_modifier(mod.name, mod.type_name):
                continue
            check = mod.generate_field_check(data_var)
            if check != "true":
                mod_desc = _escape_cpp_string(f"{desc}: modifier {mod.name} check failed")
                lines.append(
                    f'    if (!({check})) {{ error("{mod_desc}"); return false; }}'
                )

    lines.append("    return true;")
    lines.append("  };")
    return "\n".join(lines)


def _generate_check_function(instr: Instruction) -> tuple[str, str]:
    safe_name = instr.cpp_safe_name()
    structs: list[str] = []
    for v in instr.variants:
        if v.cpp_struct_name and v.cpp_struct_name not in structs:
            structs.append(v.cpp_struct_name)

    if not structs:
        return f"  // {instr.opcode}: no cpp_struct -- skipped\n", ""

    raw_struct = structs[0]
    cpp_struct = _resolve_struct(raw_struct)
    if cpp_struct is None:
        return f"  // {instr.opcode}: struct {raw_struct} not available -- skipped\n", ""

    is_non_template = cpp_struct in _NON_TEMPLATE_STRUCTS
    param_type = f"const {cpp_struct}&" if is_non_template else f"const {cpp_struct}<ResolvedOp>&"

    decl = f"  void check_{safe_name}({param_type} instr);\n"

    variant_blocks: list[str] = []
    for idx, variant in enumerate(instr.variants):
        variant_blocks.append(_generate_variant_check(variant, idx, cpp_struct))

    variant_calls = " || ".join(f"check_v{i}()" for i in range(len(instr.variants)))
    if not variant_calls:
        variant_calls = "true"

    doc_ref = instr.doc or f"PTX opcode: {instr.opcode}"
    definition = (
        f"// {doc_ref}\n"
        f"void TypeCheckerGen::check_{safe_name}({param_type} instr) {{\n"
        + "\n".join(variant_blocks) + "\n"
        f"  if (!({variant_calls})) {{\n"
        f'    error("{_escape_cpp_string(instr.opcode)}: no matching variant found");\n'
        f"  }}\n"
        f"}}\n\n"
    )
    return decl, definition


def generate() -> None:
    instructions = load_all_instructions(INSTRUCTIONS_DIR)
    declarations: list[str] = []
    definitions: list[str] = []
    for instr in instructions:
        decl, defn = _generate_check_function(instr)
        declarations.append(decl)
        if defn:
            definitions.append(defn)

    hpp_content = (
        _AUTOGEN_NOTICE + "\n"
        "#pragma once\n"
        "#include <array>\n"
        "#include <variant>\n"
        "#include \"type_checker.hpp\"\n"
        "#include \"symbol_resolver.hpp\"\n"
        "#include \"ptx_ir/instr.hpp\"\n"
        "\n"
        "namespace ptx_frontend {\n"
        "\n"
        "/// TypeCheckerGen uses the ResolvedModule (symbol-resolved AST) to perform\n"
        "/// per-instruction type checking.  Each check_<opcode>() method validates\n"
        "/// the data modifiers against the PTX 9.2 specification.\n"
        "///\n"
        "/// Inherits from TypeChecker to reuse require_sm(), require_ptx(), error().\n"
        "class TypeCheckerGen : public TypeChecker {\n"
        " public:\n"
        "  TypeCheckerGen(const LegacySymbolTable& sym, const CompileTarget& target)\n"
        "      : TypeChecker(sym, target) {}\n"
        "\n"
        "  /// Check all instructions in a resolved module.\n"
        "  void check_module(const ResolvedModule& mod);\n"
        "\n"
        + "".join(declarations)
        + "\n"
        "};\n"
        "\n"
        "}  // namespace ptx_frontend\n"
    )

    cpp_content = (
        _AUTOGEN_NOTICE + "\n"
        "#include \"type_checker_gen.hpp\"\n"
        "#include <array>\n"
        "#include <variant>\n"
        "\n"
        "namespace ptx_frontend {\n"
        "\n"
        "namespace {\n"
        "template <typename T, std::size_t N>\n"
        "bool is_one_of(const T& val, const std::array<T, N>& arr) {\n"
        "  for (const auto& x : arr) if (val == x) return true;\n"
        "  return false;\n"
        "}\n"
        "}  // namespace\n"
        "\n"
        "void TypeCheckerGen::check_module(const ResolvedModule& /*mod*/) {\n"
        "  // Dispatch happens via ptx_visit_dispatch.hpp visitor pattern.\n"
        "}\n"
        "\n"
        + "".join(definitions)
        + "\n"
        "}  // namespace ptx_frontend\n"
    )

    OUT_HPP.write_text(hpp_content, encoding="utf-8")
    OUT_CPP.write_text(cpp_content, encoding="utf-8")
    print(f"Generated {OUT_HPP}")
    print(f"Generated {OUT_CPP}")
    print(f"Total instructions: {len(instructions)}")
    skipped = sum(1 for d in declarations if "skipped" in d)
    print(f"Skipped (no struct): {skipped}")
    print(f"Generated checks: {len(instructions) - skipped}")


if __name__ == "__main__":
    generate()
