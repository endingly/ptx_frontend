from dataclasses import FrozenInstanceError
import unittest

from code_gen.model import (
    CodegenUnit,
    DomainBackend,
    EmitAlternativeBackend,
    EmitBackend,
    InstructionBackend,
    ModifierBackend,
    OperandBackend,
)


class BackendModelTests(unittest.TestCase):
    def test_constructs_detached_cpp_backend_model(self) -> None:
        scalar_types = DomainBackend(
            cpp_type="ScalarType",
            values={"u32": "ScalarType::U32"},
        )
        emit = EmitBackend(
            kind="sub_variant",
            instance="data",
            type="Data",
            alternatives=(
                EmitAlternativeBackend(
                    name="IntegerData",
                    variants=("add_integer",),
                ),
            ),
        )
        add = InstructionBackend(
            opcode="add",
            cpp="Add",
            emit=emit,
            modifiers={
                "type": ModifierBackend(
                    field="type_",
                    cpp_type="ScalarType",
                    domain="scalar_types",
                )
            },
            operands={"dst": OperandBackend(field="dst", cpp_type="Operand")},
            type_checker_rule="integer_arith::check_add",
            visitor_name="visitAdd",
            modifier_order=("type",),
            operand_order=("dst",),
        )

        unit = CodegenUnit(
            spec_schema="ptx-instr/v1",
            backend_schema="ptx-cpp-backend/v1",
            category="integer_arithmetic",
            namespace="ptx_frontend::generated",
            includes=("<variant>",),
            instructions=(),
            backends={"add": add},
            domains={"scalar_types": scalar_types},
        )

        self.assertEqual(unit.backends["add"].emit.alternatives[0].name, "IntegerData")
        self.assertEqual(
            unit.domains["scalar_types"].values["u32"],
            "ScalarType::U32",
        )

    def test_backend_model_is_frozen(self) -> None:
        emit = EmitBackend(kind="direct")

        with self.assertRaises(FrozenInstanceError):
            emit.kind = "sub_struct"  # type: ignore[misc]


if __name__ == "__main__":
    unittest.main()
