#!/usr/bin/env python3
"""
gen_type_checker_code.py
────────────────────────
Reads every instruction YAML in ../instructions/ and generates two C++ files:

  include/type_checker_gen.hpp   - declarations of check_<opcode>() methods
  src/type_checker_gen.cpp       - implementations

Run from the repository root:

    python3 scripts/gen_type_checker_code.py

The generated checker operates on the *resolved* module produced by
symbol_resolver.hpp: each operand is a ParsedOperand<SymbolId> and the
SymbolTable maps SymbolId to SymbolEntry (with type and state-space info).
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
# None means "skip - no matching C++ struct exists".
_STRUCT_NAME_MAP: dict[str, str | None] = {
    "InstrBrxIdx":            None,
    "InstrSlct":              None,
    "InstrExit":              None,
    "InstrMad24":             None,
    "InstrCnot":              None,
    "InstrLop3":              None,
    "InstrLdmatrix":          "InstrLdMatrix",
    "InstrStmatrix":          "InstrStMatrix",
    "InstrBarSync":           None,
    "InstrBarArrive":         None,
    "InstrBarWarpSync":       None,
    "InstrBarrierSync":       "InstrClusterBarrier",
    "InstrBarrierArrive":     "InstrClusterBarrier",
    "InstrBarrierClusterArrive": "InstrClusterBarrier",
    "InstrBarrierClusterWait":   "InstrClusterBarrier",
    "InstrSetMaxNreg":        "InstrSetMaxNReg",
    "InstrMbarrierArriveExpectTx": "InstrMbarrierExpectTx",
    "InstrMbarrierPendingCount": None,
    "InstrMbarrierComplTx":   "InstrMbarrierCompleteTx",
    "InstrPmevent":           None,
    "InstrStacksave":         None,
    "InstrStackrestore":      None,
    "InstrVoteSync":          "InstrVote",
    "InstrMatchSync":         None,
    "InstrGridDepControlLaunchDependents": "InstrGridDepControl",
    "InstrGridDepControlWait":             "InstrGridDepControl",
    "InstrIsSpacep":          None,
    "InstrMmaSync":           "InstrMma",
    "InstrWgmmaMmaAsync":     "InstrWgmmaMmaAsync",
    "InstrTcgen05CommitGroup":    "InstrTcgen05CommitArrival",
    "InstrTcgen05WaitGroup":      "InstrTcgen05Wait",
    "InstrTcgen05RelinquishAllocPermit": "InstrTcgen05Relinquish",
    "InstrTcgen05MmaWs":          None,
    "InstrTcgen05Mma":            None,
    "InstrCpReduceAsyncBulk":     None,
    "InstrCpReduceAsyncBulkTensor": None,
    "InstrTex":               None,
    "InstrSelp":             None,  # data is ScalarType directly -- use manual check
    "InstrTld4":              None,
}

# emit_note → C++ detail struct accessed via std::get<T>(instr.data)
# Only types listed here will use holds_alternative/get (they are variant alternatives).
# Other emit_notes are direct data field types and accessed via instr.data.field directly.
_VARIANT_DETAIL_TYPES: set[str] = {
    "ArithInteger",   # variant alternative of ArithDetails
    "ArithFloat",     # variant alternative of ArithDetails (for add/sub/mad)
    "MulInt",         # variant alternative of MulDetails
    "MadInt",         # variant alternative of MadDetails
    "MinMaxInt",      # variant alternative of MinMaxDetails
    "MinMaxFloat",    # variant alternative of MinMaxDetails
    "DivInt",         # variant alternative of DivDetails
    "DivFloat",       # variant alternative of DivDetails
    "SetpData",       # not actually a variant -- will be removed if instr.data IS SetpData
    "SetData",
    "RetData",
}

# emit_note → C++ detail struct
_DETAIL_TYPE_MAP: dict[str, str] = {
    "ArithInteger": "ArithInteger",
    "ArithFloat":   "ArithFloat",
    "MulInt":       "MulInt",
    "MadInt":       "MadInt",
    "Mul24Details": "Mul24Details",
    "MinMaxInt":    "MinMaxInt",
    "MinMaxFloat":  "MinMaxFloat",
    "DivInt":       "DivInt",
    "DivFloat":     "DivFloat",
    "TypeFtz":      "TypeFtz",
    "FlushToZero":  "FlushToZero",
    "ShrData":      "ShrData",
    "ShfDetails":   "ShfDetails",
    "RcpData":      "RcpData",
    "CarryDetails": "CarryDetails",
    "SetpData":     "SetpData",
    "SetData":      "SetData",
    "RetData":      "RetData",
}

# Modifier types that are std::variant, complex structs, or otherwise cannot
# be checked with simple is_one_of(). We skip field-level checks for these.
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
    "Type",       # non-scalar Type (vectors, etc.)
    # Matrix
    "Tcgen05Shape",
    "Tcgen05Num",
    "Tcgen05PackMode",
    "Tcgen05UnpackMode",
    "MmaLayout",
    "WgmmaLayout",
    "MmaType",
    # Conversion
    "CvtMode",
    "RoundingMode",   # nested/complex usage in ld/st contexts
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

# Modifier field names in YAML that don't directly map to a field of the
# same name on the data struct (complex / nested / wrong name). Skip them.
_SKIP_MODIFIER_FIELD_NAMES: set[str] = {
    # data_movement.yaml uses 'type' not 'type_' (YAML not in instrAdd format)
    "type",
    # ld/st qualifier is a nested struct, not a simple enum
    "qualifier", "nc", "global", "scope", "weak", "cop",
    "level", "level_cache_hint", "level_eviction_priority", "level_prefetch_size",
    "primary_priority", "vec", "space", "ss", "ifrnd",
    # cvt complex fields
    "frnd2", "x2_to_type", "x2_from_type", "dtype", "stype", "atype",
    "rn", "satfinite", "f8x2type", "from_f32", "convert_type",
    "s32", "b32", "b64", "to", "size", "relu",
    # misc.yaml complex fields
    "u32", "comp", "completion_mechanism", "dim", "geom",
    "dst_space", "src_space", "a", "v4", "op",
}

# Structs where instr.data IS directly a ScalarType (not a struct with type_ field).
_DATA_IS_SCALAR_TYPE_STRUCTS: set[str] = {
    "InstrSelp",
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


def _generate_variant_check(variant: VariantModel, variant_idx: int) -> str:
    detail_type = _DETAIL_TYPE_MAP.get(variant.emit_note, "")
    desc = _escape_cpp_string(variant.description)

    lines: list[str] = []
    lines.append(f"  // variant {variant_idx}: {desc}")
    lines.append(f"  auto check_v{variant_idx} = [&]() -> bool {{")
    if variant.min_ptx_version > 0:
        lines.append(f'    if (!require_ptx({variant.min_ptx_version}f, "{desc}")) return false;')
    if variant.min_sm_version > 0:
        lines.append(f'    if (!require_sm({variant.min_sm_version}u, "{desc}")) return false;')

    if detail_type:
        lines.append(f"    if (!std::holds_alternative<{detail_type}>(instr.data)) return false;")
        lines.append(f"    [[maybe_unused]] auto& d = std::get<{detail_type}>(instr.data);")
        data_var = "d"
    else:
        data_var = "instr.data"

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
        variant_blocks.append(_generate_variant_check(variant, idx))

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
