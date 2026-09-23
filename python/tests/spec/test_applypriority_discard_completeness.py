"""Descriptor inventory for PTX 9.3 global cache-range operations."""

import unittest

from ptx_frontend.spec.database import load_codegen_database
from ptx_frontend.spec.resources import packaged_spec_dir


class ApplypriorityDiscardCompletenessTest(unittest.TestCase):
    """Guard the generic and explicit forms and their fixed ranges."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the canonical instruction database once."""
        database = load_codegen_database(spec_dir=packaged_spec_dir())
        cls.instructions = {
            instruction.opcode: instruction
            for instruction in database.instructions
            if instruction.opcode in {"applypriority", "discard"}
        }

    def test_forms_and_target_floors(self) -> None:
        """Preserve public explicit variants while adding generic spellings."""
        self.assertEqual(
            [variant.name for variant in self.instructions["applypriority"].variants],
            [
                "applypriority_global_l2_evict_normal",
                "applypriority_generic_l2_evict_normal",
            ],
        )
        self.assertEqual(
            [variant.name for variant in self.instructions["discard"].variants],
            ["discard_global_l2", "discard_generic_l2"],
        )
        for instruction in self.instructions.values():
            for variant in instruction.variants:
                self.assertEqual(variant.availability, {"ptx": "7.4", "sm": 80})
                self.assertEqual(
                    [
                        (item.address_operands, item.alignment)
                        for item in variant.address_alignments
                    ],
                    [(("address",), 128)],
                )
                immediate = variant.immediate_value
                assert immediate is not None
                self.assertEqual(immediate.values, (128,))


if __name__ == "__main__":
    unittest.main()
