"""Canonical availability and identity contracts for the legacy source option."""

import unittest

from ptx_frontend.code_gen.emit.availability import emit_availability
from ptx_frontend.ir.resolved_ir import from_instruction_spec
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import ModifierPresence, OperandRegisterWidthPolicy
from ptx_frontend.spec.normalize.availability import normalize_availability


class LegacyMapF64Tests(unittest.TestCase):
    """Limit the source option to three existing explicit-rounding f64 forms."""

    def test_exact_variant_inventory_and_descriptor_terms(self) -> None:
        """Preserve all four independent conjunctions through spec, IR and emission."""
        expected_names = {"div_rn_f64", "rcp_rn_f64", "sqrt_rn_f64"}
        found = set()
        expected = {"any_of": [
            {"ptx": "1.4", "sm": 13},
            *({"ptx": "1.4", "target": f"sm_{sm}", "map_f64_to_f32": True}
              for sm in (10, 11, 12)),
        ]}
        for instruction in get_packaged_spec_database().instructions:
            for variant in instruction.variants:
                clauses = variant.availability.get("any_of", [])
                if variant.floating_point_default is not None:
                    continue  # Historical omissions have their own bounded descriptor tests.
                if any("map_f64_to_f32" in clause for clause in clauses):
                    found.add(variant.name)
                    with self.subTest(variant=variant.name):
                        self.assertEqual(variant.availability, expected)
                        self.assertEqual(normalize_availability(variant.availability), expected)
                        resolved = from_instruction_spec(instruction)
                        selected = next(item for item in resolved.variants if item.variant_id == variant.name)
                        self.assertEqual(dict(selected.availability), expected)
                        emitted = emit_availability(dict(selected.availability))
                        self.assertIn(".any_of_count = 4", emitted)
                        self.assertEqual(emitted.count(".requires_map_f64_to_f32 = true"), 3)
                        self.assertEqual(emitted.count(".minimum_ptx_version = {1, 4}"), 4)
                        for sm in (10, 11, 12):
                            self.assertIn(f".exact_target_architecture = {{{sm}}}", emitted)
                        modifiers = {item.name: item for item in variant.modifiers}
                        self.assertIs(modifiers["rounding"].presence, ModifierPresence.FIXED)
                        self.assertEqual(modifiers["rounding"].value, "rn")
                        self.assertEqual(modifiers["type"].value, "f64")
                        for layout in variant.operand_layouts:
                            for operand in layout.operands:
                                self.assertIs(operand.register_width_policy,
                                              OperandRegisterWidthPolicy.SAME_WIDTH)
        self.assertEqual(found, expected_names)


if __name__ == "__main__":
    unittest.main()
