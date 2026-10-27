"""Exact legacy vote domains and preserved mask-free source identities."""

import unittest

from ptx_frontend.code_gen.emit.availability import emit_availability
from ptx_frontend.ir.resolved_ir import from_instruction_spec
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import ModifierPresence, OperandKind


class LegacyVoteTests(unittest.TestCase):
    """Keep legacy vote controls separate from synchronized counterparts."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load canonical and resolved vote models from the full database."""
        cls.instruction = next(item for item in get_packaged_spec_database().instructions
                               if item.opcode == "vote")
        cls.variants = {item.name: item for item in cls.instruction.variants}
        cls.resolved = {item.variant_id: item for item in from_instruction_spec(cls.instruction).variants}

    def test_exact_four_variant_inventory_and_complete_availability_domains(self) -> None:
        """Retain all six predicate clauses and all four ballot clauses through emission."""
        expected_names = {"vote_all_pred", "vote_any_pred", "vote_uni_pred", "vote_ballot_b32"}
        self.assertEqual({name for name in self.variants if not name.startswith("vote_sync_")},
                         expected_names)
        for name in expected_names:
            with self.subTest(variant=name):
                targets = (20, 30, 50, 52) if "ballot" in name else (12, 13, 20, 30, 50, 52)
                expected = {"any_of": [{"ptx": "1.2", "target": f"sm_{sm}"} for sm in targets]}
                self.assertEqual(self.variants[name].availability, expected)
                self.assertEqual(dict(self.resolved[name].availability), expected)
                emitted = emit_availability(expected)
                self.assertIn(f".any_of_count = {len(targets)}", emitted)
                self.assertEqual(emitted.count(".minimum_ptx_version = {1, 2}"), len(targets))
                self.assertEqual(emitted.count(".has_exact_target = true"), len(targets))
                for sm in targets:
                    self.assertIn(f".exact_target_architecture = {{{sm}}}", emitted)

    def test_mask_absence_and_predicate_contract_match_synchronized_forms(self) -> None:
        """Reuse typed destinations and complemented source without synthesizing sync or mask."""
        for mode, result in (("all", "pred"), ("any", "pred"), ("uni", "pred"), ("ballot", "b32")):
            name = f"vote_{mode}_{result}"
            variant = self.variants[name]
            synchronized = self.variants[f"vote_sync_{mode}_{result}"]
            self.assertEqual(len(variant.operand_layouts), 1)
            operands = variant.operand_layouts[0].operands
            self.assertEqual(operands, synchronized.operand_layouts[0].operands[:2])
            self.assertEqual(len(operands), 2)
            self.assertIs(operands[1].kind, OperandKind.PREDICATE_OR_NOT)
            modifiers = {item.name: item for item in variant.modifiers}
            self.assertIs(modifiers["sync"].presence, ModifierPresence.ABSENT)
            self.assertIs(modifiers[mode].presence, ModifierPresence.FIXED)
            self.assertIs(modifiers[mode].value, True)
            self.assertEqual(modifiers["type"].value, result)
            self.assertEqual([item.source_name for item in self.resolved[name].modifier_fields],
                             [mode, "type"])
            self.assertEqual([item.name for item in self.resolved[name].operand_layouts[0].fields],
                             ["dst", "predicate"])


if __name__ == "__main__":
    unittest.main()
