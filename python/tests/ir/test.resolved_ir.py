from __future__ import annotations

from pathlib import Path
import sys
import unittest


REPO_ROOT = Path(__file__).resolve().parents[3]
PYTHON_ROOT = REPO_ROOT / "python"

if str(PYTHON_ROOT) not in sys.path:
    sys.path.insert(0, str(PYTHON_ROOT))


from code_gen.database import load_codegen_database
from ir.resolved_ir import ResolvedFieldOrigin, from_instruction_spec


class ResolvedIrBuildTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "instructions/ptx_spec",
            backend_dir=REPO_ROOT / "instructions/ptx_cpp_backend_spec",
        )
        add = next(
            bound.spec
            for bound in database.instructions
            if bound.spec.opcode == "add"
        )
        cls.instruction = from_instruction_spec(add)

    def test_add_variant_names(self) -> None:
        self.assertEqual(self.instruction.opcode, "add")
        self.assertEqual(self.instruction.cpp_name, "Add")
        self.assertEqual(
            [variant.cpp_name for variant in self.instruction.variants],
            [
                "IntegerNoSat",
                "SatS32",
                "SimdNoSatSm90",
                "PackedOptionalSatSm120",
                "SatSm120",
            ],
        )

    def test_add_resolved_variant_fields(self) -> None:
        variants = {variant.cpp_name: variant for variant in self.instruction.variants}

        self.assertEqual(
            [
                (field.name, field.cpp_type, field.origin, field.type_expr)
                for field in variants["IntegerNoSat"].fields
            ],
            [
                ("type", "WithLocs<ScalarType>", ResolvedFieldOrigin.MODIFIER, None),
                ("dst", "WithLocs<ResolvedRegisterId>", ResolvedFieldOrigin.OPERAND, "$type"),
                ("src1", "WithLocs<RegOrImm>", ResolvedFieldOrigin.OPERAND, "$type"),
                ("src2", "WithLocs<RegOrImm>", ResolvedFieldOrigin.OPERAND, "$type"),
            ],
        )
        self.assertEqual(
            [field.name for field in variants["SatS32"].fields],
            ["dst", "src1", "src2"],
        )
        self.assertEqual(
            [
                (field.name, field.cpp_type, field.origin)
                for field in variants["PackedOptionalSatSm120"].fields
            ],
            [
                ("saturate", "WithLocs<bool>", ResolvedFieldOrigin.MODIFIER),
                ("type", "WithLocs<ScalarType>", ResolvedFieldOrigin.MODIFIER),
                ("dst", "WithLocs<ResolvedRegisterId>", ResolvedFieldOrigin.OPERAND),
                ("src1", "WithLocs<RegOrImm>", ResolvedFieldOrigin.OPERAND),
                ("src2", "WithLocs<RegOrImm>", ResolvedFieldOrigin.OPERAND),
            ],
        )


if __name__ == "__main__":
    unittest.main()
