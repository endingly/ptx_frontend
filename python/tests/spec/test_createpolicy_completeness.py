"""Descriptor inventory for documented PTX 9.3 createpolicy forms."""

import unittest

from ptx_frontend.spec.database import load_codegen_database
from ptx_frontend.spec.resources import packaged_spec_dir


class CreatepolicyCompletenessTest(unittest.TestCase):
    """Keep the distinct source topologies and modifier domains stable."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the canonical packaged database once."""
        database = load_codegen_database(spec_dir=packaged_spec_dir())
        cls.instruction = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "createpolicy"
        )

    def test_topologies_and_priorities(self) -> None:
        """Represent omitted and expressed secondary priorities separately."""
        variants = {variant.name: variant for variant in self.instruction.variants}
        self.assertEqual(
            set(variants),
            {
                "createpolicy_fractional_l2_b64",
                "createpolicy_fractional_l2_secondary_b64",
                "createpolicy_range_generic_l2_b64",
                "createpolicy_range_generic_l2_secondary_b64",
                "createpolicy_range_global_l2_b64",
                "createpolicy_range_global_l2_secondary_b64",
                "createpolicy_cvt_l2_b64",
            },
        )
        for variant in variants.values():
            self.assertEqual(variant.availability, {"ptx": "7.4", "sm": 80})
        for name, variant in variants.items():
            if "cvt" in name:
                continue
            primary = next(
                modifier
                for modifier in variant.modifiers
                if modifier.name == "primary_priority"
            )
            self.assertEqual(
                {value.value for value in primary.values},
                {"evict_last", "evict_normal", "evict_first", "evict_unchanged"},
            )
            secondary = [
                modifier
                for modifier in variant.modifiers
                if modifier.name == "secondary_priority"
            ]
            self.assertEqual(bool(secondary), "secondary" in name)
            if secondary:
                self.assertEqual(
                    {value.value for value in secondary[0].values},
                    {"evict_first", "evict_unchanged"},
                )
        for name in (
            "createpolicy_fractional_l2_b64",
            "createpolicy_fractional_l2_secondary_b64",
        ):
            self.assertEqual(
                [layout.name for layout in variants[name].operand_layouts],
                ["default", "with_fraction"],
            )


if __name__ == "__main__":
    unittest.main()
