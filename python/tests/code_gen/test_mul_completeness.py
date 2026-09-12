"""Regression coverage for the complete PTX 9.3 MUL specification."""

from pathlib import Path
import unittest

from ptx_frontend.code_gen.database import load_codegen_database
from ptx_frontend.code_gen.load_yaml import load_yaml
from ptx_frontend.code_gen.model import OperandRegisterWidthPolicy


ROOT = Path(__file__).resolve().parents[3]
SPEC_DIR = ROOT / "instructions/ptx_spec"


class MulCompletenessTests(unittest.TestCase):
    """Keep MUL's PTX section, variants, and generated operand contracts closed."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the canonical database and retain MUL variants by stable YAML name."""

        database = load_codegen_database(spec_dir=SPEC_DIR)
        instruction = next(item for item in database.instructions if item.opcode == "mul")
        cls.variants = {variant.name: variant for variant in instruction.variants}

    def test_records_all_three_normative_mul_sections(self) -> None:
        """Keep integer, floating, and half/bfloat MUL provenance in the YAML source."""

        arithmetic = load_yaml(SPEC_DIR / "arithmetic.yaml")
        sections = {
            instruction["section"]
            for instruction in arithmetic["instructions"]
            if instruction["opcode"] == "mul"
        }
        self.assertEqual(sections, {"9.7.1.3", "9.7.3.5", "9.7.4.3"})

    def test_models_every_legal_integer_mode_and_width(self) -> None:
        """Require all six high/low types and only the four legal wide forms."""

        high_low_types = ("u16", "u32", "u64", "s16", "s32", "s64")
        expected_names = {
            *(f"mul_lo_{type_name}" for type_name in high_low_types),
            *(f"mul_hi_{type_name}" for type_name in high_low_types),
            "mul_wide_u16",
            "mul_wide_s16",
            "mul_wide_u32",
            "mul_wide_s32",
        }
        integer_names = {
            name
            for name in self.variants
            if name.startswith(("mul_lo_", "mul_hi_", "mul_wide_"))
        }
        self.assertEqual(integer_names, expected_names)

        for type_name in high_low_types:
            for mode in ("lo", "hi"):
                variant = self.variants[f"mul_{mode}_{type_name}"]
                modifiers = {modifier.name: modifier for modifier in variant.modifiers}
                self.assertTrue(modifiers[mode].value)
                self.assertEqual(modifiers["type"].value, type_name)

        for signedness in ("u", "s"):
            for width, destination_width in (("16", "32"), ("32", "64")):
                variant = self.variants[f"mul_wide_{signedness}{width}"]
                operands = variant.operand_layouts[0].operands
                self.assertEqual(
                    tuple(operand.type_expression.scalar_type for operand in operands),
                    (f"{signedness}{destination_width}",
                     f"{signedness}{width}", f"{signedness}{width}"),
                )
                self.assertEqual(
                    tuple(operand.kind for operand in operands),
                    ("reg", "reg_or_imm", "reg_or_imm"),
                )
        self.assertNotIn("mul_wide_u64", self.variants)
        self.assertNotIn("mul_wide_s64", self.variants)

    def test_models_floating_rounding_flags_and_availability(self) -> None:
        """Keep the floating-family defaults separate from directed-rounding minima."""

        f32 = {modifier.name: modifier for modifier in self.variants["mul_rn_f32"].modifiers}
        self.assertEqual(f32["rounding"].presence, "optional")
        self.assertEqual(f32["rounding"].default, "rn")
        f32_rounding = {
            value.value: value.availability for value in f32["rounding"].values
        }
        self.assertEqual(f32_rounding["rn"], {})
        self.assertEqual(f32_rounding["rz"], {})
        self.assertEqual(f32_rounding["rm"], {"ptx": "1.0", "sm": 20})
        self.assertEqual(f32_rounding["rp"], {"ptx": "1.0", "sm": 20})
        self.assertEqual(f32["ftz"].presence, "optional")
        self.assertEqual(f32["sat"].presence, "optional")
        self.assertEqual(
            tuple(operand.kind for operand in self.variants["mul_rn_f32"].operand_layouts[0].operands),
            ("reg", "reg_or_imm", "reg_or_imm"),
        )

        for name in ("mul_f32x2", "mul_f64"):
            modifiers = {modifier.name: modifier for modifier in self.variants[name].modifiers}
            self.assertEqual(modifiers["rounding"].presence, "optional")
            self.assertEqual(modifiers["rounding"].default, "rn")
            self.assertEqual(
                tuple(value.value for value in modifiers["rounding"].values),
                ("rn", "rz", "rm", "rp"),
            )
        f32x2_operands = self.variants["mul_f32x2"].operand_layouts[0].operands
        self.assertEqual(tuple(operand.kind for operand in f32x2_operands), ("reg",) * 3)
        self.assertEqual(
            tuple(operand.type_expression.scalar_type for operand in f32x2_operands),
            ("b64",) * 3,
        )
        self.assertEqual(
            tuple(operand.register_width_policy for operand in f32x2_operands),
            (OperandRegisterWidthPolicy.EXACT,) * 3,
        )

    def test_models_half_and_bfloat_modifier_and_container_boundaries(self) -> None:
        """Require half flags only where PTX 9.3 permits them and exact packed widths."""

        for name, type_name, container in (
            ("mul_half", "f16", None),
            ("mul_half_x2", "f16x2", "b32"),
            ("mul_bfloat", "bf16", "b16"),
            ("mul_bfloat_x2", "bf16x2", "b32"),
        ):
            variant = self.variants[name]
            modifiers = {modifier.name: modifier for modifier in variant.modifiers}
            self.assertEqual(modifiers["rounding"].default, "rn")
            self.assertEqual(tuple(value.value for value in modifiers["rounding"].values), ("rn",))
            self.assertEqual(modifiers["type"].value, type_name)
            if container is not None:
                operands = variant.operand_layouts[0].operands
                self.assertEqual(
                    tuple(operand.type_expression.scalar_type for operand in operands),
                    (container,) * 3,
                )
                self.assertEqual(
                    tuple(operand.register_width_policy for operand in operands),
                    (OperandRegisterWidthPolicy.EXACT,) * 3,
                )

        for name in ("mul_half", "mul_half_x2"):
            modifiers = {modifier.name: modifier for modifier in self.variants[name].modifiers}
            self.assertEqual(modifiers["ftz"].presence, "optional")
            self.assertEqual(modifiers["sat"].presence, "optional")
        for name in ("mul_bfloat", "mul_bfloat_x2"):
            modifiers = {modifier.name: modifier for modifier in self.variants[name].modifiers}
            self.assertEqual(modifiers["ftz"].presence, "absent")
            self.assertEqual(modifiers["sat"].presence, "absent")


if __name__ == "__main__":
    unittest.main()
