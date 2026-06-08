from code_gen.model import *
from code_gen.cpp_emit import *


def gen_ir(unit: CodegenUnit) -> str:
    out: list[str] = []
    out.append("// Auto-generated. Do not edit manually.")
    out.append("#pragma once")
    out.append("")
    out.append("#include <variant>")
    out.append('#include "ptx_frontend/ir/operand.hpp"')
    out.append('#include "ptx_frontend/ir/scalar_type.hpp"')
    out.append("")
    out.append(f"namespace {unit.namespace} {{")
    out.append("")

    emitted_detail_types: set[str] = set()

    for instr in unit.instructions:
        ib = unit.backends[instr.opcode]

        if ib.emit.kind in {"sub_struct", "sub_variant"}:
            detail_type = ib.emit.type
            assert detail_type is not None

            if detail_type not in emitted_detail_types:
                emitted_detail_types.add(detail_type)

                out.append(f"struct {detail_type} {{")
                for mod in unique_modifiers(instr):
                    field = cpp_field_for_modifier(mod, ib)
                    ctype = cpp_type_for_modifier(mod, ib, unit.domains)
                    default = cpp_default_for_modifier(mod, ib, unit.domains)
                    out.append(f"    {ctype} {field} = {default};")
                out.append("};")
                out.append("")

        out.append(f"struct {ib.cpp} {{")

        if ib.emit.kind == "sub_variant":
            detail_type = ib.emit.type
            instance = ib.emit.instance
            out.append(f"    using Data = std::variant<{detail_type}>;")
            out.append(f"    Data {instance};")
            out.append("")
        elif ib.emit.kind == "sub_struct":
            detail_type = ib.emit.type
            instance = ib.emit.instance
            out.append(f"    {detail_type} {instance};")
            out.append("")

        for op in unique_operands(instr):
            ob = ib.operands.get(op.name)
            field = ob.field if ob else op.name
            ctype = ob.cpp_type if ob else "Operand"
            out.append(f"    {ctype} {field};")

        out.append("};")
        out.append("")

    out.append(f"}} // namespace {unit.namespace}")
    out.append("")

    return "\n".join(out)


if __name__ == "__main__":
    import argparse
    from pathlib import Path
    from code_gen.load_yaml import load_yaml
    from code_gen.normalize import build_codegen_unit

    parser = argparse.ArgumentParser(
        description="Generate C++ IR from YAML specification."
    )
    parser.add_argument(
        "spec_yaml", type=Path, help="Path to the YAML specification file."
    )
    parser.add_argument(
        "backend_yaml", type=Path, help="Path to the YAML backend mapping file."
    )
    # parser.add_argument(
    #     "output_path", type=Path, help="Path to the output C++ header file."
    # )
    args = parser.parse_args()

    unit = build_codegen_unit(args.spec_yaml, args.backend_yaml)
    cpp_code = gen_ir(unit)

    print(cpp_code)
