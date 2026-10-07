"""Descriptor inventory for PTX 9.3 ordinary and tensor-map prefetch."""

import unittest

from ptx_frontend.spec.database import load_codegen_database
from ptx_frontend.spec.resources import packaged_spec_dir


class PrefetchCompletenessTest(unittest.TestCase):
    """Guard the supported prefetch topology and target minima."""

    @classmethod
    def setUpClass(cls) -> None:
        """Read the canonical packaged specification once."""
        database = load_codegen_database(spec_dir=packaged_spec_dir())
        cls.instructions = {
            instruction.opcode: instruction
            for instruction in database.instructions
            if instruction.opcode in {"prefetch", "prefetchu"}
        }

    def test_prefetch_variants_and_availability(self) -> None:
        """Separate ordinary, eviction, and tensor-map forms by target."""
        variants = {
            variant.name: variant
            for variant in self.instructions["prefetch"].variants
        }
        self.assertEqual(
            set(variants),
            {
                "prefetch_generic_l1", "prefetch_generic_l2",
                "prefetch_global_l1", "prefetch_global_l2",
                "prefetch_local_l1", "prefetch_local_l2",
                "prefetch_global_l2_evict",
                "prefetch_const_tensormap", "prefetch_param_tensormap",
                "prefetch_generic_tensormap",
            },
        )
        for name, variant in variants.items():
            expected = (
                {"ptx": "8.0", "sm": 90} if "tensormap" in name else
                {"ptx": "7.4", "sm": 80} if "evict" in name else
                {"ptx": "2.0", "sm": 20}
            )
            self.assertEqual(variant.availability, expected)
        evict = next(
            modifier
            for modifier in variants["prefetch_global_l2_evict"].modifiers
            if modifier.name == "eviction_priority"
        )
        self.assertEqual(
            {value.value for value in evict.values},
            {"evict_last", "evict_normal"},
        )

    def test_prefetchu_stays_generic_l1_only(self) -> None:
        """Keep uniform prefetch outside global and L2 families."""
        self.assertEqual(
            [variant.name for variant in self.instructions["prefetchu"].variants],
            ["prefetchu_l1"],
        )


if __name__ == "__main__":
    unittest.main()
