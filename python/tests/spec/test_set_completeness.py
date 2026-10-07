"""Regression coverage for the PTX 9.3 SET specification."""

import unittest

from ptx_frontend.spec.database import load_codegen_database
from ptx_frontend.spec.model import (
    ModifierPresence,
    OperandKind,
    OperandRegisterWidthPolicy,
)
from ptx_frontend.spec.resources import packaged_spec_dir


class SetCompletenessTests(unittest.TestCase):
    """Keep comparison, destination, and target contracts tied to source type."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load ordinary and half/bfloat variants from the canonical spec."""

        database = load_codegen_database(spec_dir=packaged_spec_dir())
        instruction = next(item for item in database.instructions if item.opcode == "set")
        cls.variants = {variant.name: variant for variant in instruction.variants}

    def test_models_disjoint_source_and_boolean_families(self) -> None:
        """Every ordinary source type selects one comparison domain per shape."""

        expected = {
            "set_bit": ({"b16", "b32", "b64"}, {"eq", "ne"}),
            "set_signed": ({"s16", "s32", "s64"}, {"eq", "ne", "lt", "le", "gt", "ge"}),
            "set_unsigned": (
                {"u16", "u32", "u64"},
                {"eq", "ne", "lt", "le", "gt", "ge", "lo", "ls", "hi", "hs"},
            ),
            "set_float": (
                {"f32"},
                {"eq", "ne", "lt", "le", "gt", "ge", "equ", "neu", "ltu", "leu", "gtu", "geu", "num", "nan"},
            ),
            "set_float_f64": (
                {"f64"},
                {"eq", "ne", "lt", "le", "gt", "ge", "equ", "neu", "ltu", "leu", "gtu", "geu", "num", "nan"},
            ),
        }
        self.assertEqual(
            {name for name in self.variants if not name.startswith("set_half_")},
            {name for family in expected for name in (family, family + "_boolean")},
        )
        for family, (source_types, comparisons) in expected.items():
            for name in (family, family + "_boolean"):
                variant = self.variants[name]
                modifiers = {modifier.name: modifier for modifier in variant.modifiers}
                self.assertEqual(
                    {value.value for value in modifiers["comparison"].values}, comparisons
                )
                self.assertEqual(
                    {value.value for value in modifiers["stype"].values}
                    if modifiers["stype"].values else {modifiers["stype"].value},
                    source_types,
                )
                self.assertEqual(
                    {value.value for value in modifiers["dtype"].values},
                    {"u32", "s32", "f32"},
                )
                self.assertIs(
                    modifiers["boolean"].presence,
                    ModifierPresence.REQUIRED if name.endswith("_boolean") else ModifierPresence.ABSENT,
                )
                self.assertIs(
                    modifiers["ftz"].presence,
                    ModifierPresence.OPTIONAL if family == "set_float" else ModifierPresence.ABSENT,
                )
                operands = variant.operand_layouts[0].operands
                self.assertEqual(
                    tuple(operand.kind for operand in operands[:3]),
                    (OperandKind.REGISTER, OperandKind.REGISTER_OR_IMMEDIATE,
                     OperandKind.REGISTER_OR_IMMEDIATE),
                )
                self.assertEqual(
                    tuple(operand.type_expression.modifier_name for operand in operands[:3]),
                    ("dtype", "stype", "stype"),
                )
                if name.endswith("_boolean"):
                    self.assertIs(operands[-1].kind, OperandKind.PREDICATE_SOURCE)
                else:
                    self.assertEqual(len(operands), 3)

        for name in ("set_float_f64", "set_float_f64_boolean"):
            self.assertEqual(self.variants[name].availability, {"ptx": "1.0", "sm": 13})

    def test_half_and_bfloat_cohorts_are_disjoint_and_typed(self) -> None:
        """Each result/source cohort owns its comparison, FTZ, and operand policy."""

        floating = {
            "eq", "ne", "lt", "le", "gt", "ge", "equ", "neu",
            "ltu", "leu", "gtu", "geu", "num", "nan",
        }
        cohorts = {
            "set_half_f16_bit": ({"f16"}, {"b16", "b32", "b64"}, {"eq", "ne"}, False, "4.2", 53),
            "set_half_f16_integer": ({"f16"}, {"u16", "u32", "u64", "s16", "s32", "s64"}, {"eq", "ne", "lt", "le", "gt", "ge"}, False, "4.2", 53),
            "set_half_f16_f16": ({"f16"}, {"f16"}, floating, True, "4.2", 53),
            "set_half_f16_f32": ({"f16"}, {"f32"}, floating, True, "4.2", 53),
            "set_half_f16_f64": ({"f16"}, {"f64"}, floating, False, "4.2", 53),
            "set_half_bf16_bit": ({"bf16"}, {"b16", "b32", "b64"}, {"eq", "ne"}, False, "7.8", 90),
            "set_half_bf16_integer": ({"bf16"}, {"u16", "u32", "u64", "s16", "s32", "s64"}, {"eq", "ne", "lt", "le", "gt", "ge"}, False, "7.8", 90),
            "set_half_bf16_f16": ({"bf16"}, {"f16"}, floating, False, "7.8", 90),
            "set_half_bf16_wide_float": ({"bf16"}, {"f32", "f64"}, floating, False, "7.8", 90),
            "set_half_integer_f16": ({"u16", "s16", "u32", "s32"}, {"f16"}, floating, True, "6.5", 53),
            "set_half_integer_bf16": ({"u16", "s16", "u32", "s32"}, {"bf16"}, floating, False, "7.8", 90),
            "set_half_native_f16x2": ({"f16x2"}, {"f16x2"}, floating, True, "4.2", 53),
            "set_half_integer_f16x2": ({"u32", "s32"}, {"f16x2"}, floating, True, "6.5", 53),
            "set_half_native_bf16x2": ({"bf16x2"}, {"bf16x2"}, floating, False, "7.8", 90),
            "set_half_integer_bf16x2": ({"u32", "s32"}, {"bf16x2"}, floating, False, "7.8", 90),
        }
        self.assertEqual(
            {name for name in self.variants if name.startswith("set_half_")},
            {name for family in cohorts for name in (family, family + "_boolean")},
        )
        for family, (dtypes, stypes, comparisons, ftz, ptx, sm) in cohorts.items():
            for name in (family, family + "_boolean"):
                with self.subTest(name=name):
                    variant = self.variants[name]
                    controls = {modifier.name: modifier for modifier in variant.modifiers}
                    values = lambda control: (
                        {value.value for value in control.values}
                        if control.values else {control.value}
                    )
                    self.assertEqual(values(controls["dtype"]), dtypes)
                    self.assertEqual(values(controls["stype"]), stypes)
                    self.assertEqual(values(controls["comparison"]), comparisons)
                    self.assertIs(
                        controls["ftz"].presence,
                        ModifierPresence.OPTIONAL if ftz else ModifierPresence.ABSENT,
                    )
                    self.assertIs(
                        controls["boolean"].presence,
                        ModifierPresence.REQUIRED if name.endswith("_boolean") else ModifierPresence.ABSENT,
                    )
                    self.assertEqual(variant.availability, {"ptx": ptx, "sm": sm})
                    operands = variant.operand_layouts[0].operands
                    self.assertIs(operands[0].kind, OperandKind.REGISTER)
                    self.assertEqual(len(operands), 4 if name.endswith("_boolean") else 3)
                    if name.endswith("_boolean"):
                        self.assertIs(operands[3].kind, OperandKind.PREDICATE_SOURCE)
                    if stypes & {"f16", "bf16", "f16x2", "bf16x2"}:
                        self.assertIs(operands[1].kind, OperandKind.REGISTER)
                        self.assertIs(operands[2].kind, OperandKind.REGISTER)
                    else:
                        self.assertIs(operands[1].kind, OperandKind.REGISTER_OR_IMMEDIATE)
                        self.assertIs(operands[2].kind, OperandKind.REGISTER_OR_IMMEDIATE)

    def test_half_register_container_policies(self) -> None:
        """Preserve native versus integer-result packed physical containers."""

        same = OperandRegisterWidthPolicy.SAME_WIDTH
        exact = OperandRegisterWidthPolicy.EXACT
        for family, destination, destination_width, source, source_width in (
            ("set_half_f16_f16", "f16", same, "f16", same),
            ("set_half_bf16_f16", "b16", exact, "f16", same),
            ("set_half_integer_f16", None, same, "f16", same),
            ("set_half_integer_bf16", None, same, "b16", exact),
            ("set_half_native_f16x2", "f16x2", same, "f16x2", same),
            ("set_half_integer_f16x2", "b32", same, "f16x2", same),
            ("set_half_native_bf16x2", "b32", exact, "b32", exact),
            ("set_half_integer_bf16x2", "b32", same, "b32", exact),
        ):
            for name in (family, family + "_boolean"):
                with self.subTest(name=name):
                    destination_operand, *sources = self.variants[name].operand_layouts[0].operands
                    self.assertEqual(
                        destination_operand.type_expression.scalar_type,
                        destination,
                    )
                    self.assertIs(destination_operand.register_width_policy, destination_width)
                    for operand in sources[:2]:
                        self.assertEqual(operand.type_expression.scalar_type, source)
                        self.assertIs(operand.register_width_policy, source_width)


if __name__ == "__main__":
    unittest.main()
