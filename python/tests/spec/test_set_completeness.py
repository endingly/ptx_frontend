"""Regression coverage for the ordinary PTX 9.3 SET specification."""

import unittest

from ptx_frontend.spec.database import load_codegen_database
from ptx_frontend.spec.model import ModifierPresence, OperandKind
from ptx_frontend.spec.resources import packaged_spec_dir


class SetCompletenessTests(unittest.TestCase):
    """Keep comparison, destination, and target contracts tied to source type."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the canonical ordinary SET variants."""

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
            set(self.variants),
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


if __name__ == "__main__":
    unittest.main()
