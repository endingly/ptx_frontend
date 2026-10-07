"""Regression coverage for the ordinary PTX 9.3 SELP specification."""

import unittest

from ptx_frontend.spec.database import load_codegen_database
from ptx_frontend.spec.model import ModifierPresence, OperandKind
from ptx_frontend.spec.resources import packaged_spec_dir


class SelpCompletenessTests(unittest.TestCase):
    """Keep SELP's source types, public seed, and target gate disjoint."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the canonical ordinary SELP variants."""

        database = load_codegen_database(spec_dir=packaged_spec_dir())
        instruction = next(item for item in database.instructions if item.opcode == "selp")
        cls.variants = {variant.name: variant for variant in instruction.variants}

    def test_preserves_u32_and_covers_the_other_ptx93_types(self) -> None:
        """The added type language must not overlap the existing U32 ABI."""

        self.assertEqual(set(self.variants), {"selp_u32", "selp_scalar"})
        u32_type = self.variants["selp_u32"].modifiers[0]
        scalar_type = self.variants["selp_scalar"].modifiers[0]
        self.assertEqual(u32_type.value, "u32")
        self.assertIs(scalar_type.presence, ModifierPresence.REQUIRED)
        self.assertEqual(
            {value.value for value in scalar_type.values},
            {"b16", "b32", "b64", "u16", "u64", "s16", "s32", "s64", "f32", "f64"},
        )
        f64 = next(value for value in scalar_type.values if value.value == "f64")
        self.assertEqual(f64.availability, {"sm": 13})
        self.assertTrue(all(not value.availability for value in scalar_type.values if value.value != "f64"))

    def test_keeps_data_types_bound_to_type_slot_and_predicate_source(self) -> None:
        """Operand descriptors must carry the type and predicate truth source."""

        operands = self.variants["selp_scalar"].operand_layouts[0].operands
        self.assertEqual(
            tuple(operand.kind for operand in operands),
            (OperandKind.REGISTER, OperandKind.REGISTER_OR_IMMEDIATE,
             OperandKind.REGISTER_OR_IMMEDIATE, OperandKind.PREDICATE_SOURCE),
        )
        self.assertEqual(
            tuple(operand.type_expression.modifier_name for operand in operands[:3]),
            ("type", "type", "type"),
        )
        self.assertIs(
            self.variants["selp_u32"].operand_layouts[0].operands[-1].kind,
            OperandKind.PREDICATE_SOURCE,
        )


if __name__ == "__main__":
    unittest.main()
