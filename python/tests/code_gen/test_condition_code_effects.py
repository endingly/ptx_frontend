"""Canonical implicit-state contracts for extended-precision arithmetic."""

import unittest
from pathlib import Path

from ptx_frontend.code_gen.database import load_codegen_database
from ptx_frontend.code_gen.model import ConditionCodeEffect
from ptx_frontend.ir.resolved_ir import from_instruction_spec


class ConditionCodeEffectsTest(unittest.TestCase):
    """Ensure state effects survive normalization and IR generation."""

    def test_effects_are_typed_and_variant_local(self):
        """Cover each modeled CC access and default no-effect ordinary variants."""

        database = load_codegen_database(
            spec_dir=Path(__file__).resolve().parents[3] / "instructions/ptx_spec"
        )
        expected = {
            "add": {"cc": ConditionCodeEffect.CARRY_OUT},
            "addc": {"plain": ConditionCodeEffect.CARRY_IN,
                     "cc": ConditionCodeEffect.CARRY_IN_OUT},
            "sub": {"cc": ConditionCodeEffect.BORROW_OUT},
            "subc": {"plain": ConditionCodeEffect.BORROW_IN,
                     "cc": ConditionCodeEffect.BORROW_IN_OUT},
            "mad": {"hi_cc": ConditionCodeEffect.CARRY_OUT,
                    "lo_cc": ConditionCodeEffect.CARRY_OUT},
            "madc": {"hi_plain": ConditionCodeEffect.CARRY_IN,
                     "lo_plain": ConditionCodeEffect.CARRY_IN,
                     "hi_cc": ConditionCodeEffect.CARRY_IN_OUT,
                     "lo_cc": ConditionCodeEffect.CARRY_IN_OUT},
        }
        for instruction in database.instructions:
            resolved = from_instruction_spec(instruction)
            expected_variant_effects = {
                f"{instruction.opcode}_{form}_{width}": effect
                for form, effect in expected.get(instruction.opcode, {}).items()
                for width in (32, 64)
            }
            for variant, generated in zip(instruction.variants, resolved.variants):
                self.assertIsInstance(variant.condition_code_effect, ConditionCodeEffect)
                self.assertEqual(generated.condition_code_effect, variant.condition_code_effect)
                self.assertIs(
                    variant.condition_code_effect,
                    expected_variant_effects.get(
                        variant.name, ConditionCodeEffect.NONE
                    ),
                )
            if instruction.opcode not in expected:
                continue
            variants = {variant.name: variant for variant in instruction.variants}
            for form, effect in expected[instruction.opcode].items():
                for width in (32, 64):
                    variant = variants[f"{instruction.opcode}_{form}_{width}"]
                    self.assertIs(variant.condition_code_effect, effect)
                    if instruction.opcode in {"mad", "madc"}:
                        expected_availability = (
                            {"ptx": "3.0", "sm": 20}
                            if width == 32 else {"ptx": "4.3", "sm": 20}
                        )
                    else:
                        expected_availability = (
                            {"ptx": "1.2", "sm": 0}
                            if width == 32 else {"ptx": "4.3", "sm": 20}
                        )
                    self.assertEqual(variant.availability, expected_availability)

    def test_unknown_semantic_value_is_rejected(self):
        """The normalized effect domain cannot retain arbitrary spelling."""

        with self.assertRaises(ValueError):
            ConditionCodeEffect("implicit_carry_maybe")
