"""Descriptor inventory for synchronized shuffle and vote forms."""

import unittest

from ptx_frontend.spec.database import load_codegen_database
from ptx_frontend.spec.model import OperandKind
from ptx_frontend.spec.resources import packaged_spec_dir


class WarpSyncCompletenessTest(unittest.TestCase):
    """Keep programmer-expressed warp controls and operand forms distinct."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the packaged instruction source once."""
        database = load_codegen_database(spec_dir=packaged_spec_dir())
        cls.instructions = {
            instruction.opcode: instruction
            for instruction in database.instructions
            if instruction.opcode in {"shfl", "vote"}
        }

    def test_shuffle_modes_and_destination_layouts(self) -> None:
        """Expose all four modes with bare and paired destinations."""
        variants = self.instructions["shfl"].variants
        self.assertEqual(
            {variant.name for variant in variants},
            {f"shfl_sync_{mode}_b32" for mode in ("up", "down", "bfly", "idx")},
        )
        for variant in variants:
            self.assertEqual(variant.availability, {"ptx": "6.0", "sm": 30})
            self.assertEqual(
                [layout.name for layout in variant.operand_layouts],
                ["without_predicate", "with_predicate"],
            )
            bare, paired = variant.operand_layouts
            self.assertEqual(bare.operands[0].kind, OperandKind.REGISTER)
            self.assertEqual(paired.operands[0].kind, OperandKind.SHFL_DESTINATION)
            self.assertFalse(paired.operands[0].allow_destination_sink)
            self.assertFalse(paired.operands[0].allow_predicate_sink)
            self.assertEqual(
                {modifier.token for modifier in variant.modifiers},
                {".sync", f".{variant.name.split('_')[2]}", None},
            )

    def test_vote_modes_and_negatable_source(self) -> None:
        """Retain the source negation control for each result type."""
        variants = self.instructions["vote"].variants
        self.assertEqual(
            {variant.name for variant in variants},
            {"vote_sync_ballot_b32", "vote_sync_all_pred",
             "vote_sync_any_pred", "vote_sync_uni_pred"},
        )
        for variant in variants:
            self.assertEqual(variant.availability, {"ptx": "6.0", "sm": 30})
            operands = variant.operand_layouts[0].operands
            self.assertEqual(operands[1].kind, OperandKind.PREDICATE_OR_NOT)
            self.assertEqual(operands[2].kind, OperandKind.REGISTER_OR_IMMEDIATE)
            expected = (
                OperandKind.REGISTER if "ballot" in variant.name
                else OperandKind.PREDICATE
            )
            self.assertEqual(operands[0].kind, expected)


if __name__ == "__main__":
    unittest.main()
