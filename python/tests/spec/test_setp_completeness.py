"""Regression coverage for the complete PTX 9.3 SETP specification."""

from pathlib import Path
import unittest

from ptx_frontend.spec.database import load_codegen_database
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.model import (
    ModifierPresence,
    OperandKind,
    OperandRegisterWidthPolicy,
)
from ptx_frontend.spec.resources import packaged_spec_dir

SPEC_DIR = packaged_spec_dir()


class SetpCompletenessTests(unittest.TestCase):
    """Keep SETP comparison, destination, type, and availability contracts closed."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the canonical database and retain SETP variants by YAML name."""

        database = load_codegen_database(spec_dir=SPEC_DIR)
        instruction = next(
            item for item in database.instructions if item.opcode == "setp"
        )
        cls.variants = {variant.name: variant for variant in instruction.variants}

    def test_records_the_ptx93_setp_section_and_all_modifier_variants(self) -> None:
        """Require the ordinary, half, and bfloat forms from SETP's normative section."""

        comparison = load_yaml(SPEC_DIR / "comparison_and_selection.yaml")
        sections = {
            instruction["section"]
            for instruction in comparison["instructions"]
            if instruction["opcode"] == "setp"
        }
        self.assertEqual(sections, {"9.7.6.2", "9.7.7.2"})
        self.assertEqual(
            set(self.variants),
            {
                "setp_bit",
                "setp_bit_boolean",
                "setp_signed",
                "setp_signed_boolean",
                "setp_unsigned",
                "setp_unsigned_boolean",
                "setp_float",
                "setp_float_boolean",
                "setp_float_f64",
                "setp_float_f64_boolean",
                "setp_f16",
                "setp_f16_boolean",
                "setp_f16x2",
                "setp_f16x2_boolean",
                "setp_bf16",
                "setp_bf16_boolean",
                "setp_bf16x2",
                "setp_bf16x2_boolean",
            },
        )

    def test_models_each_comparison_domain_and_boolean_form(self) -> None:
        """Keep comparison suffixes tied to their PTX scalar-type families."""

        expected_comparisons = {
            "setp_bit": ("eq", "ne"),
            "setp_signed": ("eq", "ne", "lt", "le", "gt", "ge"),
            "setp_unsigned": (
                "eq",
                "ne",
                "lt",
                "le",
                "gt",
                "ge",
                "lo",
                "ls",
                "hi",
                "hs",
            ),
            "setp_float": (
                "eq",
                "ne",
                "lt",
                "le",
                "gt",
                "ge",
                "equ",
                "neu",
                "ltu",
                "leu",
                "gtu",
                "geu",
                "num",
                "nan",
            ),
        }
        for family, comparisons in expected_comparisons.items():
            for name in (family, f"{family}_boolean"):
                modifiers = {
                    modifier.name: modifier
                    for modifier in self.variants[name].modifiers
                }
                self.assertEqual(
                    tuple(value.value for value in modifiers["comparison"].values),
                    comparisons,
                )
                self.assertEqual(
                    modifiers["boolean"].presence,
                    (
                        ModifierPresence.REQUIRED
                        if name.endswith("_boolean")
                        else ModifierPresence.ABSENT
                    ),
                )
                if name.endswith("_boolean"):
                    for layout in self.variants[name].operand_layouts:
                        self.assertIs(
                            layout.operands[-1].kind,
                            OperandKind.PREDICATE_SOURCE,
                        )

        for name in ("setp_float_f64", "setp_float_f64_boolean"):
            modifiers = {
                modifier.name: modifier for modifier in self.variants[name].modifiers
            }
            self.assertEqual(
                tuple(value.value for value in modifiers["comparison"].values),
                expected_comparisons["setp_float"],
            )
            self.assertEqual(
                modifiers["boolean"].presence,
                    (
                        ModifierPresence.REQUIRED
                        if name.endswith("_boolean")
                        else ModifierPresence.ABSENT
                    ),
            )
            if name.endswith("_boolean"):
                for layout in self.variants[name].operand_layouts:
                    self.assertIs(
                        layout.operands[-1].kind,
                        OperandKind.PREDICATE_SOURCE,
                    )

        half_comparisons = expected_comparisons["setp_float"]
        for family in ("setp_f16", "setp_f16x2", "setp_bf16", "setp_bf16x2"):
            for name in (family, f"{family}_boolean"):
                modifiers = {
                    modifier.name: modifier
                    for modifier in self.variants[name].modifiers
                }
                self.assertEqual(
                    tuple(value.value for value in modifiers["comparison"].values),
                    half_comparisons,
                )

    def test_models_ordinary_sinks_and_half_bfloat_destination_boundaries(self) -> None:
        """Allow ordinary single or pair sinks, but retain required half/bfloat outputs."""

        for family in (
            "setp_bit",
            "setp_signed",
            "setp_unsigned",
            "setp_float",
            "setp_float_f64",
        ):
            for name in (family, f"{family}_boolean"):
                layouts = self.variants[name].operand_layouts
                self.assertEqual(
                    tuple(layout.name for layout in layouts), ("single", "pair")
                )
                self.assertEqual(
                    tuple(layout.operands[0].kind for layout in layouts),
                    (
                        OperandKind.PREDICATE_OR_SINK,
                        OperandKind.PREDICATE_PAIR_OR_SINK,
                    ),
                )

        for family, type_name, destination, source_container in (
            ("setp_f16", "f16", "pred", "f16"),
            ("setp_f16x2", "f16x2", "pred_pair", "b32"),
            ("setp_bf16", "bf16", "pred", "b16"),
            ("setp_bf16x2", "bf16x2", "pred_pair", "b32"),
        ):
            for name in (family, f"{family}_boolean"):
                variant = self.variants[name]
                operands = variant.operand_layouts[0].operands
                self.assertIs(operands[0].kind, OperandKind(destination))
                self.assertEqual(
                    tuple(operand.kind for operand in operands[1:3]),
                    (OperandKind.REGISTER, OperandKind.REGISTER),
                )
                self.assertEqual(
                    tuple(
                        operand.type_expression.scalar_type  # pyright: ignore[reportOptionalMemberAccess]
                        for operand in operands[1:3]
                    ),
                    (source_container, source_container),
                )
                if source_container != "f16":
                    self.assertEqual(
                        tuple(
                            operand.register_width_policy for operand in operands[1:3]
                        ),
                        (OperandRegisterWidthPolicy.EXACT,) * 2,
                    )
                modifiers = {modifier.name: modifier for modifier in variant.modifiers}
                self.assertEqual(modifiers["type"].value, type_name)
                if name.endswith("_boolean"):
                    self.assertIs(operands[-1].kind, OperandKind.PREDICATE_SOURCE)

    def test_models_ftz_and_target_availability_without_overextending_them(
        self,
    ) -> None:
        """Keep F32/half FTZ and f64/half/bfloat target minima exact."""

        for name in ("setp_float", "setp_float_boolean"):
            modifiers = {
                modifier.name: modifier for modifier in self.variants[name].modifiers
            }
            self.assertIs(modifiers["ftz"].presence, ModifierPresence.OPTIONAL)
            self.assertEqual(modifiers["type"].value, "f32")
        for name in ("setp_float_f64", "setp_float_f64_boolean"):
            modifiers = {
                modifier.name: modifier for modifier in self.variants[name].modifiers
            }
            self.assertEqual(
                dict(self.variants[name].availability), {"ptx": "1.0", "sm": 13}
            )
            self.assertIs(modifiers["ftz"].presence, ModifierPresence.ABSENT)
            self.assertEqual(modifiers["type"].value, "f64")

        for family, availability, ftz_presence in (
            ("setp_f16", {"ptx": "4.2", "sm": 53}, ModifierPresence.OPTIONAL),
            ("setp_f16x2", {"ptx": "4.2", "sm": 53}, ModifierPresence.OPTIONAL),
            ("setp_bf16", {"ptx": "7.8", "sm": 90}, ModifierPresence.ABSENT),
            ("setp_bf16x2", {"ptx": "7.8", "sm": 90}, ModifierPresence.ABSENT),
        ):
            for name in (family, f"{family}_boolean"):
                variant = self.variants[name]
                modifiers = {modifier.name: modifier for modifier in variant.modifiers}
                self.assertEqual(dict(variant.availability), availability)
                self.assertIs(modifiers["ftz"].presence, ftz_presence)


if __name__ == "__main__":
    unittest.main()
