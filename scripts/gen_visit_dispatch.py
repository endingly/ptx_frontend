#!/usr/bin/env python3
"""Generate include/ptx_visit_dispatch.hpp from instruction definitions.

Usage:
    python3 gen_visit_dispatch.py
    python3 gen_visit_dispatch.py --output include/ptx_visit_dispatch.hpp
"""

import argparse
import io
import sys

# ---------------------------------------------------------------------------
# Instruction table
# Each entry: (struct_name, template_kind, has_data, fields)
#
#   template_kind:
#     True    -> InstrFoo<Op>
#     False   -> InstrFoo  (no template parameter)
#     "id"    -> InstrBra<typename Op::id_type>  (Bra is special)
#     "call"  -> InstrCall<Op> with special CallArgs handling
#
#   has_data: whether the struct contains a `data` field to copy as-is
#
#   fields: list of (field_name, kind) where kind is one of:
#     'dst'       -> Op field,          is_dst = true
#     'src'       -> Op field,          is_dst = false
#     'opt-dst'   -> Opt<Op> field,     is_dst = true
#     'opt-src'   -> Opt<Op> field,     is_dst = false
#     'ident'     -> id_type field,     via visit_ident, is_dst = false
#     'ident-dst' -> id_type field,     via visit_ident, is_dst = true
# ---------------------------------------------------------------------------

INSTRS = [
    ("InstrAbs",                    True,    True,  [("dst","dst"),("src","src")]),
    ("InstrActivemask",             True,    False, [("dst","dst")]),
    ("InstrAdd",                    True,    True,  [("dst","dst"),("src1","src"),("src2","src")]),
    ("InstrAddExtended",            True,    True,  [("dst","dst"),("src1","src"),("src2","src")]),
    ("InstrSubExtended",            True,    True,  [("dst","dst"),("src1","src"),("src2","src")]),
    ("InstrMadExtended",            True,    True,  [("dst","dst"),("src1","src"),("src2","src"),("src3","src")]),
    ("InstrAnd",                    True,    True,  [("dst","dst"),("src1","src"),("src2","src")]),
    ("InstrAtom",                   True,    True,  [("dst","dst"),("src1","src"),("src2","src")]),
    ("InstrAtomCas",                True,    True,  [("dst","dst"),("src1","src"),("src2","src"),("src3","src")]),
    ("InstrBarWarp",                True,    False, [("src","src")]),
    ("InstrBar",                    True,    True,  [("src1","src"),("src2","opt-src")]),
    ("InstrBarRed",                 True,    True,  [("dst1","dst"),("src_barrier","src"),("src_predicate","src"),("src_negate_predicate","src"),("src_threadcount","opt-src")]),
    ("InstrBfe",                    True,    True,  [("dst","dst"),("src1","src"),("src2","src"),("src3","src")]),
    ("InstrBfi",                    True,    True,  [("dst","dst"),("src1","src"),("src2","src"),("src3","src"),("src4","src")]),
    ("InstrBmsk",                   True,    True,  [("dst","dst"),("src_a","src"),("src_b","src")]),
    ("InstrBra",                    "id",    False, [("src","ident")]),
    ("InstrBrev",                   True,    True,  [("dst","dst"),("src","src")]),
    ("InstrCall",                   "call",  True,  None),
    ("InstrClz",                    True,    True,  [("dst","dst"),("src","src")]),
    ("InstrCos",                    True,    True,  [("dst","dst"),("src","src")]),
    ("InstrCpAsync",                True,    True,  [("src_to","dst"),("src_from","src")]),
    ("InstrCpAsyncCommitGroup",     False,   False, []),
    ("InstrCpAsyncWaitGroup",       True,    False, [("src_group","src")]),
    ("InstrCpAsyncWaitAll",         False,   False, []),
    ("InstrCreatePolicyFractional", True,    True,  [("dst_policy","dst")]),
    ("InstrCvt",                    True,    True,  [("dst","dst"),("src","src"),("src2","opt-src")]),
    ("InstrCvtPack",                True,    True,  [("dst","dst"),("src1","src"),("src2","src"),("src3","src")]),
    ("InstrCvta",                   True,    True,  [("dst","dst"),("src","src")]),
    ("InstrDiv",                    True,    True,  [("dst","dst"),("src1","src"),("src2","src")]),
    ("InstrDp4a",                   True,    True,  [("dst","dst"),("src1","src"),("src2","src"),("src3","src")]),
    ("InstrEx2",                    True,    True,  [("dst","dst"),("src","src")]),
    ("InstrFma",                    True,    True,  [("dst","dst"),("src1","src"),("src2","src"),("src3","src")]),
    ("InstrLd",                     True,    True,  [("dst","dst"),("src","src")]),
    ("InstrLg2",                    True,    True,  [("dst","dst"),("src","src")]),
    ("InstrMad",                    True,    True,  [("dst","dst"),("src1","src"),("src2","src"),("src3","src")]),
    ("InstrMax",                    True,    True,  [("dst","dst"),("src1","src"),("src2","src")]),
    ("InstrMembar",                 False,   True,  []),
    ("InstrMin",                    True,    True,  [("dst","dst"),("src1","src"),("src2","src")]),
    ("InstrMov",                    True,    True,  [("dst","dst"),("src","src")]),
    ("InstrMul",                    True,    True,  [("dst","dst"),("src1","src"),("src2","src")]),
    ("InstrMul24",                  True,    True,  [("dst","dst"),("src1","src"),("src2","src")]),
    ("InstrNanosleep",              True,    False, [("src","src")]),
    ("InstrNeg",                    True,    True,  [("dst","dst"),("src","src")]),
    ("InstrNot",                    True,    True,  [("dst","dst"),("src","src")]),
    ("InstrOr",                     True,    True,  [("dst","dst"),("src1","src"),("src2","src")]),
    ("InstrPopc",                   True,    True,  [("dst","dst"),("src","src")]),
    ("InstrPrmt",                   True,    False, [("dst","dst"),("src1","src"),("src2","src"),("src3","src")]),
    ("InstrRcp",                    True,    True,  [("dst","dst"),("src","src")]),
    ("InstrRem",                    True,    True,  [("dst","dst"),("src1","src"),("src2","src")]),
    ("InstrRet",                    False,   True,  []),
    ("InstrRsqrt",                  True,    True,  [("dst","dst"),("src","src")]),
    ("InstrSelp",                   True,    True,  [("dst","dst"),("src1","src"),("src2","src"),("src3","src")]),
    ("InstrSet",                    True,    True,  [("dst","dst"),("src1","src"),("src2","src")]),
    ("InstrSetBool",                True,    True,  [("dst","dst"),("src1","src"),("src2","src"),("src3","src")]),
    ("InstrSetp",                   True,    True,  [("dst1","dst"),("dst2","opt-dst"),("src1","src"),("src2","src")]),
    ("InstrSetpBool",               True,    True,  [("dst1","dst"),("dst2","opt-dst"),("src1","src"),("src2","src"),("src3","src")]),
    ("InstrShflSync",               True,    True,  [("dst","dst"),("dst_pred","opt-dst"),("src","src"),("src_lane","src"),("src_opts","src"),("src_membermask","src")]),
    ("InstrShf",                    True,    True,  [("dst","dst"),("src_a","src"),("src_b","src"),("src_c","src")]),
    ("InstrShl",                    True,    True,  [("dst","dst"),("src1","src"),("src2","src")]),
    ("InstrShr",                    True,    True,  [("dst","dst"),("src1","src"),("src2","src")]),
    ("InstrSin",                    True,    True,  [("dst","dst"),("src","src")]),
    ("InstrSqrt",                   True,    True,  [("dst","dst"),("src","src")]),
    ("InstrSt",                     True,    True,  [("src1","dst"),("src2","src")]),
    ("InstrSub",                    True,    True,  [("dst","dst"),("src1","src"),("src2","src")]),
    ("InstrTrap",                   False,   False, []),
    ("InstrXor",                    True,    True,  [("dst","dst"),("src1","src"),("src2","src")]),
    ("InstrTanh",                   True,    True,  [("dst","dst"),("src","src")]),
    ("InstrVote",                   True,    True,  [("dst","dst"),("src1","src"),("src2","src")]),
    ("InstrReduxSync",              True,    True,  [("dst","dst"),("src","src"),("src_membermask","src")]),
    ("InstrLdMatrix",               True,    True,  [("dst","dst"),("src","src")]),
    ("InstrGridDepControl",         False,   True,  []),
    ("InstrMma",                    True,    True,  [("dst","dst"),("src1","src"),("src2","src"),("src3","src")]),
    ("InstrCopysign",               True,    True,  [("dst","dst"),("src1","src"),("src2","src")]),
    ("InstrPrefetch",               True,    True,  [("src","src")]),
    ("InstrSad",                    True,    True,  [("dst","dst"),("src1","src"),("src2","src"),("src3","src")]),
    ("InstrDp2a",                   True,    True,  [("dst","dst"),("src1","src"),("src2","src"),("src3","src")]),
]


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def cpp_type_name(struct_name, template_kind):
    """Return the C++ type expression used inside std::is_same_v checks."""
    if template_kind is True:
        return f"{struct_name}<Op>"
    elif template_kind is False:
        return struct_name
    elif template_kind == "id":
        return f"{struct_name}<typename Op::id_type>"
    elif template_kind == "call":
        return f"{struct_name}<Op>"
    else:
        raise ValueError(f"Unknown template_kind: {template_kind!r}")


def gen_visit_body(instr, const_ref):
    """Lines for one if-constexpr branch in visit_instruction / visit_instruction_mut."""
    name, template_kind, has_data, fields = instr
    lines = []

    if template_kind == "call":
        loop_q = "const auto&" if const_ref else "auto&"
        lines += [
            f"      if (auto _r = visitor.visit_ident(i.arguments.func, type_space, false, relaxed_type_check); !_r) return _r;",
            # return_arguments are destination registers that will receive return values
            f"      for ({loop_q} _ra : i.arguments.return_arguments) {{",
            f"        if (auto _r = visitor.visit_ident(_ra, type_space, true, relaxed_type_check); !_r) return _r;",
            f"      }}",
            f"      for ({loop_q} _ia : i.arguments.input_arguments) {{",
            f"        if (auto _r = visitor.visit(_ia, type_space, false, relaxed_type_check); !_r) return _r;",
            f"      }}",
            f"      return {{}};",
        ]
        return lines

    if not fields:
        lines.append(f"      return {{}};")
        return lines

    for field_name, kind in fields:
        if kind == "dst":
            lines.append(f"      if (auto _r = visitor.visit(i.{field_name}, type_space, true, relaxed_type_check); !_r) return _r;")
        elif kind == "src":
            lines.append(f"      if (auto _r = visitor.visit(i.{field_name}, type_space, false, relaxed_type_check); !_r) return _r;")
        elif kind == "opt-dst":
            lines += [
                f"      if (i.{field_name}.has_value()) {{",
                f"        if (auto _r = visitor.visit(*i.{field_name}, type_space, true, relaxed_type_check); !_r) return _r;",
                f"      }}",
            ]
        elif kind == "opt-src":
            lines += [
                f"      if (i.{field_name}.has_value()) {{",
                f"        if (auto _r = visitor.visit(*i.{field_name}, type_space, false, relaxed_type_check); !_r) return _r;",
                f"      }}",
            ]
        elif kind == "ident":
            lines.append(f"      if (auto _r = visitor.visit_ident(i.{field_name}, type_space, false, relaxed_type_check); !_r) return _r;")
        elif kind == "ident-dst":
            lines.append(f"      if (auto _r = visitor.visit_ident(i.{field_name}, type_space, true, relaxed_type_check); !_r) return _r;")

    lines.append(f"      return {{}};")
    return lines


def gen_map_body(instr):
    """Lines for one if-constexpr branch in visit_instruction_map."""
    name, template_kind, has_data, fields = instr

    # The return type inside the lambda
    if template_kind is True or template_kind == "call":
        ret_type = f"{name}<NewOp>"
    elif template_kind is False:
        ret_type = name
    elif template_kind == "id":
        ret_type = f"{name}<typename NewOp::id_type>"
    else:
        raise ValueError(f"Unknown template_kind: {template_kind!r}")

    lines = []

    # ---- InstrCall special path ----
    if template_kind == "call":
        lines += [
            f"      CallArgs<typename NewOp::id_type> _new_args;",
            f"      _new_args.is_external = i.arguments.is_external;",
            f"      {{",
            f"        auto _r = visitor.visit_ident(std::move(i.arguments.func), type_space, false, relaxed_type_check);",
            f"        if (!_r) return tl::unexpected(_r.error());",
            f"        _new_args.func = std::move(*_r);",
            f"      }}",
            # return_arguments are destination registers receiving return values
            f"      for (auto&& _ra : i.arguments.return_arguments) {{",
            f"        auto _r = visitor.visit_ident(std::move(_ra), type_space, true, relaxed_type_check);",
            f"        if (!_r) return tl::unexpected(_r.error());",
            f"        _new_args.return_arguments.push_back(std::move(*_r));",
            f"      }}",
            f"      for (auto&& _ia : i.arguments.input_arguments) {{",
            f"        auto _r = visitor.visit(std::move(_ia), type_space, false, relaxed_type_check);",
            f"        if (!_r) return tl::unexpected(_r.error());",
            f"        _new_args.input_arguments.push_back(std::move(*_r));",
            f"      }}",
            f"      return {ret_type}{{.data = i.data, .arguments = std::move(_new_args)}};",
        ]
        return lines

    # ---- No operands ----
    if not fields:
        if has_data:
            lines.append(f"      return {ret_type}{{.data = i.data}};")
        else:
            lines.append(f"      return {ret_type}{{}};")
        return lines

    # ---- General path: map each operand field ----
    # Collect (field_name, kind, var_name, is_optional) tuples
    mapped = []
    for field_name, kind in fields:
        var = f"_r_{field_name}"
        if kind == "dst":
            lines += [
                f"      auto {var} = visitor.visit(std::move(i.{field_name}), type_space, true, relaxed_type_check);",
                f"      if (!{var}) return tl::unexpected({var}.error());",
            ]
            mapped.append((field_name, var, False))
        elif kind == "src":
            lines += [
                f"      auto {var} = visitor.visit(std::move(i.{field_name}), type_space, false, relaxed_type_check);",
                f"      if (!{var}) return tl::unexpected({var}.error());",
            ]
            mapped.append((field_name, var, False))
        elif kind == "ident":
            lines += [
                f"      auto {var} = visitor.visit_ident(std::move(i.{field_name}), type_space, false, relaxed_type_check);",
                f"      if (!{var}) return tl::unexpected({var}.error());",
            ]
            mapped.append((field_name, var, False))
        elif kind == "ident-dst":
            lines += [
                f"      auto {var} = visitor.visit_ident(std::move(i.{field_name}), type_space, true, relaxed_type_check);",
                f"      if (!{var}) return tl::unexpected({var}.error());",
            ]
            mapped.append((field_name, var, False))
        elif kind in ("opt-dst", "opt-src"):
            is_dst_str = "true" if kind == "opt-dst" else "false"
            # Determine the mapped optional element type
            if template_kind == "id":
                opt_elem = "typename NewOp::id_type"
            else:
                opt_elem = "NewOp"
            lines += [
                f"      std::optional<{opt_elem}> {var};",
                f"      if (i.{field_name}.has_value()) {{",
                f"        auto _mapped = visitor.visit(std::move(*i.{field_name}), type_space, {is_dst_str}, relaxed_type_check);",
                f"        if (!_mapped) return tl::unexpected(_mapped.error());",
                f"        {var} = std::move(*_mapped);",
                f"      }}",
            ]
            mapped.append((field_name, var, True))

    # Build designated-initializer list
    init_parts = []
    if has_data:
        init_parts.append(".data = i.data")
    for field_name, var, is_opt in mapped:
        if is_opt:
            init_parts.append(f".{field_name} = std::move({var})")
        else:
            init_parts.append(f".{field_name} = std::move(*{var})")

    init_str = ", ".join(init_parts)
    lines.append(f"      return {ret_type}{{{init_str}}};")
    return lines


# ---------------------------------------------------------------------------
# Code generation
# ---------------------------------------------------------------------------

def generate():
    out = io.StringIO()

    def emit(line=""):
        out.write(line + "\n")

    emit("// AUTO-GENERATED by scripts/gen_visit_dispatch.py — DO NOT EDIT")
    emit("#pragma once")
    emit('#include "ptx_visit.hpp"')
    emit()
    emit("namespace ptx_frontend {")
    emit()

    # ------------------------------------------------------------------
    # visit_instruction  (read-only)
    # ------------------------------------------------------------------
    emit("template <OperandLike Op, typename Err>")
    emit("inline expected<void, Err> visit_instruction(")
    emit("    const Instruction<Op>& instr,")
    emit("    Visitor<Op, Err>& visitor,")
    emit("    std::optional<VisitTypeSpace> type_space,")
    emit("    bool relaxed_type_check) {")
    emit("  return std::visit([&](const auto& i) -> expected<void, Err> {")
    emit("    using T = std::decay_t<decltype(i)>;")

    first = True
    for instr in INSTRS:
        name, template_kind, has_data, fields = instr
        tname = cpp_type_name(name, template_kind)
        kw = "if" if first else "} else if"
        first = False
        emit(f"    {kw} constexpr (std::is_same_v<T, {tname}>) {{")
        for line in gen_visit_body(instr, const_ref=True):
            emit(line)
    emit("    } else {")
    emit('      static_assert(!std::is_same_v<T, T>, "unhandled instruction type in visit_instruction");')
    emit("    }")
    emit("  }, instr);")
    emit("}")
    emit()

    # ------------------------------------------------------------------
    # visit_instruction_mut  (mutable)
    # ------------------------------------------------------------------
    emit("template <OperandLike Op, typename Err>")
    emit("inline expected<void, Err> visit_instruction_mut(")
    emit("    Instruction<Op>& instr,")
    emit("    VisitorMut<Op, Err>& visitor,")
    emit("    std::optional<VisitTypeSpace> type_space,")
    emit("    bool relaxed_type_check) {")
    emit("  return std::visit([&](auto& i) -> expected<void, Err> {")
    emit("    using T = std::decay_t<decltype(i)>;")

    first = True
    for instr in INSTRS:
        name, template_kind, has_data, fields = instr
        tname = cpp_type_name(name, template_kind)
        kw = "if" if first else "} else if"
        first = False
        emit(f"    {kw} constexpr (std::is_same_v<T, {tname}>) {{")
        for line in gen_visit_body(instr, const_ref=False):
            emit(line)
    emit("    } else {")
    emit('      static_assert(!std::is_same_v<T, T>, "unhandled instruction type in visit_instruction_mut");')
    emit("    }")
    emit("  }, instr);")
    emit("}")
    emit()

    # ------------------------------------------------------------------
    # visit_instruction_map  (Op -> NewOp)
    # ------------------------------------------------------------------
    emit("template <OperandLike Op, OperandLike NewOp, typename Err>")
    emit("inline expected<Instruction<NewOp>, Err> visit_instruction_map(")
    emit("    Instruction<Op> instr,")
    emit("    VisitorMap<Op, NewOp, Err>& visitor,")
    emit("    std::optional<VisitTypeSpace> type_space,")
    emit("    bool relaxed_type_check) {")
    emit("  return std::visit([&](auto&& i) -> expected<Instruction<NewOp>, Err> {")
    emit("    using T = std::decay_t<decltype(i)>;")

    first = True
    for instr in INSTRS:
        name, template_kind, has_data, fields = instr
        tname = cpp_type_name(name, template_kind)
        kw = "if" if first else "} else if"
        first = False
        emit(f"    {kw} constexpr (std::is_same_v<T, {tname}>) {{")
        for line in gen_map_body(instr):
            emit(line)
    emit("    } else {")
    emit('      static_assert(!std::is_same_v<T, T>, "unhandled instruction type in visit_instruction_map");')
    emit("    }")
    emit("  }, std::move(instr));")
    emit("}")
    emit()

    emit("}  // namespace ptx_frontend")

    return out.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output", "-o", metavar="PATH",
        help="Write output to PATH (default: stdout)",
    )
    args = parser.parse_args()

    content = generate()

    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(content)
        print(f"Written to {args.output}", file=sys.stderr)
    else:
        sys.stdout.write(content)


if __name__ == "__main__":
    main()
