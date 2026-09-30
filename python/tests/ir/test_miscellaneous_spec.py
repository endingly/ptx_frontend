"""Typed-model coverage for PTX 9.3 miscellaneous instruction forms."""

from pathlib import Path
import unittest

from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.code_gen.resolved_field_names import field_cpp_type
from ptx_frontend.ir.resolved_ir import (
    ResolvedImmediateConversionPolicy,
    from_instruction_spec,
)
from ptx_frontend.spec.database import load_codegen_database
from ptx_frontend.spec.resources import packaged_spec_dir


ROOT = Path(__file__).resolve().parents[3]
BACKEND = load_cpp_backend(
    ROOT / "instructions/ptx_cpp_backend_spec/ptx_frontend.yaml"
)


class MiscellaneousSpecTest(unittest.TestCase):
    """Check canonical forms survive normalization as distinct typed variants."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the repository's complete canonical specification once."""

        database = load_codegen_database(spec_dir=packaged_spec_dir())
        cls.instructions = {
            item.opcode: from_instruction_spec(item)
            for item in database.instructions
            if item.opcode in {"brkpt", "nanosleep", "pmevent", "trap", "setmaxnreg"}
        }

    def test_family_variants_and_typed_operands(self) -> None:
        """Retain distinct event selector and register-count actions."""

        expected = {
            "brkpt": ["Bare"],
            "nanosleep": ["U32"],
            "pmevent": ["Index", "Mask"],
            "trap": ["Bare"],
            "setmaxnreg": ["IncSyncAlignedU32", "DecSyncAlignedU32"],
        }
        self.assertEqual(set(self.instructions), set(expected))
        for opcode, variant_names in expected.items():
            with self.subTest(opcode=opcode):
                self.assertEqual(
                    [item.cpp_name for item in self.instructions[opcode].variants],
                    variant_names,
                )

        nanosleep = self.instructions["nanosleep"].variants[0]
        self.assertEqual(
            [(field.name, field_cpp_type(field, backend=BACKEND))
             for field in nanosleep.fields],
            [("type", "ScalarType"), ("t", "WithLocs<RegOrImm>")],
        )
        self.assertEqual(
            nanosleep.operand_layouts[0].bindings[0].immediate_conversion_policy,
            ResolvedImmediateConversionPolicy.NARROW,
        )
        for index, maximum in ((0, 15), (1, 65535)):
            event = self.instructions["pmevent"].variants[index]
            self.assertEqual(
                [(field.name, field_cpp_type(field, backend=BACKEND))
                 for field in event.fields if field.name == "a"],
                [("a", "WithLocs<ResolvedImmediate>")],
            )
            self.assertEqual(event.immediate_ranges[0].maximum, maximum)
            self.assertEqual(event.immediate_ranges[0].minimum, 0)

    def test_variant_availability(self) -> None:
        """Pin ISA and target floors independently of source examples."""

        expected = {
            "brkpt": [("1.0", 11)],
            "nanosleep": [("6.3", 70)],
            "pmevent": [("1.4", 0), ("3.0", 20)],
            "trap": [("1.0", 0)],
        }
        for opcode, floors in expected.items():
            for variant, (ptx, sm) in zip(
                self.instructions[opcode].variants, floors, strict=True
            ):
                with self.subTest(opcode=opcode, variant=variant.cpp_name):
                    self.assertEqual(dict(variant.availability), {"ptx": ptx, "sm": sm})


if __name__ == "__main__":
    unittest.main()
