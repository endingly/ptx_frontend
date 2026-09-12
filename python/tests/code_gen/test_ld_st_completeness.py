"""Database regression coverage for PTX 9.3 LD/ST memory contracts."""

from pathlib import Path
import unittest

from ptx_frontend.code_gen.database import load_codegen_database


ROOT = Path(__file__).resolve().parents[3]
SPEC_DIR = ROOT / "instructions" / "ptx_spec"


class LdStCompletenessTest(unittest.TestCase):
    """Assert generated-memory metadata remains canonical and target-qualified."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the source-tree instruction database once for these checks."""
        cls.database = load_codegen_database(spec_dir=SPEC_DIR)

    def test_scalar_memory_forms_admit_b128_at_its_documented_minimum(self) -> None:
        """Keep the `.b128` source/target gate on every scalar LD/ST family."""
        variants = {
            variant.name: variant
            for instruction in self.database.instructions
            if instruction.opcode in {"ld", "st"}
            for variant in instruction.variants
        }
        for name in (
            "ld_generic_scalar", "ld_explicit_scalar",
            "st_generic_scalar", "st_explicit_scalar",
        ):
            type_modifier = next(
                modifier for modifier in variants[name].modifiers
                if modifier.name == "type"
            )
            b128 = next(value for value in type_modifier.values if value.value == "b128")
            self.assertEqual(b128.availability, {"ptx": "8.3", "sm": 70})

    def test_mmio_semantics_are_descriptor_data(self) -> None:
        """Keep opcode-neutral MMIO semantic alternatives and target gates in YAML."""
        variants = {
            variant.name: variant
            for instruction in self.database.instructions
            if instruction.opcode in {"ld", "st"}
            for variant in instruction.variants
        }
        ld = variants["ld_explicit_scalar"].memory_consistency
        st = variants["st_explicit_scalar"].memory_consistency
        self.assertIsNotNone(ld)
        self.assertIsNotNone(st)
        self.assertTrue(variants["ld_explicit_scalar"].permits_unified_address)
        self.assertEqual(
            {(value.value, tuple(value.availability.items())) for value in ld.mmio_semantics},
            {("relaxed", ()), ("acquire", (("ptx", "9.3"), ("sm", 75)))},
        )
        self.assertEqual(
            {(value.value, tuple(value.availability.items())) for value in st.mmio_semantics},
            {("relaxed", ()), ("release", (("ptx", "9.3"), ("sm", 75)))},
        )

    def test_l2_prefetch_is_a_typed_scalar_and_vector_domain(self) -> None:
        """Keep coherent and noncoherent prefetch forms target-qualified."""
        variants = {
            variant.name: variant
            for instruction in self.database.instructions
            if instruction.opcode == "ld"
            for variant in instruction.variants
        }
        for name, unified in (
            ("ld_global_l2_prefetch_scalar", True),
            ("ld_global_l2_prefetch_vector", True),
            ("ld_global_nc_l2_prefetch_scalar", False),
            ("ld_global_nc_l2_prefetch_vector", False),
        ):
            variant = variants[name]
            self.assertEqual(variant.availability, {"ptx": "7.4", "sm": 75})
            self.assertEqual(variant.permits_unified_address, unified)
            prefetch = next(
                modifier
                for modifier in variant.modifiers
                if modifier.name == "prefetch_size"
            )
            self.assertEqual(prefetch.kind, "prefetch_size")
            self.assertEqual(
                [value.value for value in prefetch.values],
                ["L2::64B", "L2::128B", "L2::256B"],
            )
            self.assertEqual(prefetch.values[-1].availability, {"sm": 80})
        for name in (
            "ld_global_l2_prefetch_vector",
            "ld_global_nc_l2_prefetch_vector",
        ):
            vector = variants[name].memory_vector
            self.assertIsNotNone(vector)
            self.assertEqual(vector.availability, {"ptx": "8.8", "sm": 100})

    def test_complete_cache_and_shared_matrix_has_typed_limits(self) -> None:
        """Keep shared, eviction, and noncoherent cross-products explicit."""
        variants = {
            variant.name: variant
            for instruction in self.database.instructions
            if instruction.opcode in {"ld", "st"}
            for variant in instruction.variants
        }
        self.assertTrue(
            {
                "ld_shared_cta_vector", "ld_shared_cluster_vector",
                "st_shared_cta_vector", "st_shared_cluster_vector",
                "ld_l2_evict_vector",
                "st_l2_evict_vector",
                "ld_global_nc_scalar", "ld_global_nc_vector",
                "ld_global_nc_l2_evict",
                "ld_global_nc_cache_hint_scalar",
                "ld_global_nc_cache_hint_vector",
            }.issubset(variants)
        )
        for name in (
            "ld_global_l2_evict_vector",
            "ld_l2_evict_vector",
            "st_global_l2_evict_vector",
            "st_l2_evict_vector",
            "ld_global_nc_l2_evict",
        ):
            vector = variants[name].memory_vector
            self.assertIsNotNone(vector)
            self.assertTrue(vector.require_modern)
        self.assertNotIn("ld_global_l2_evict_scalar", variants)
        self.assertNotIn("st_global_l2_evict_scalar", variants)
        self.assertEqual(
            variants["ld_explicit_scalar"].unified_address_access, "read"
        )
        self.assertEqual(
            variants["st_explicit_scalar"].unified_address_access, "write"
        )
        self.assertFalse(variants["ld_global_nc_scalar"].permits_unified_address)
        self.assertEqual(
            variants["ld_global_nc_scalar"].availability, {"ptx": "3.1", "sm": 32}
        )
        self.assertEqual(
            variants["ld_global_nc_vector"].availability, {"ptx": "3.1", "sm": 32}
        )

    def test_cache_partition_uses_typed_optional_defaults(self) -> None:
        """Keep cache-family subsets disjoint without losing typed omission values."""
        variants = {
            variant.name: variant
            for instruction in self.database.instructions
            if instruction.opcode in {"ld", "st"}
            for variant in instruction.variants
        }
        expected_partition = {
            "ld_global_l2_evict_vector", "ld_l2_evict_vector",
            "ld_global_l1_l2_cache_hint_prefetch_vector",
            "ld_l2_evict_cache_hint_vector",
            "ld_global_u32_l1_evict", "ld_global_l1_evict_vector",
            "ld_l1_evict_scalar", "ld_l1_evict_vector",
            "ld_global_l1_cache_hint_prefetch_scalar",
            "ld_global_l1_cache_hint_prefetch_vector",
            "ld_l1_cache_hint_scalar", "ld_l1_cache_hint_vector",
            "ld_global_u32_l2_cache_hint", "ld_global_l2_cache_hint_vector",
            "ld_l2_cache_hint_scalar", "ld_l2_cache_hint_vector",
            "ld_global_l2_prefetch_scalar", "ld_global_l2_prefetch_vector",
            "ld_l2_prefetch_scalar", "ld_l2_prefetch_vector",
            "ld_global_nc_l2_evict", "ld_global_nc_l2_evict_cache_hint_vector",
            "ld_global_nc_l1_no_allocate_u32", "ld_global_nc_l1_evict_vector",
            "ld_global_nc_l1_cache_hint_scalar",
            "ld_global_nc_l1_cache_hint_prefetch_legacy_vector",
            "ld_global_nc_cache_hint_scalar", "ld_global_nc_cache_hint_vector",
            "ld_global_nc_l2_prefetch_scalar", "ld_global_nc_l2_prefetch_vector",
            "st_global_l2_evict_vector", "st_l2_evict_vector",
            "st_global_l1_l2_cache_hint_vector", "st_l2_evict_cache_hint_vector",
            "st_global_u32_l1_evict", "st_global_l1_evict_vector",
            "st_l1_evict_scalar", "st_l1_evict_vector",
            "st_global_l1_cache_hint_scalar", "st_global_l1_cache_hint_vector",
            "st_l1_cache_hint_scalar", "st_l1_cache_hint_vector",
            "st_global_u32_l2_cache_hint", "st_global_l2_cache_hint_vector",
            "st_l2_cache_hint_scalar", "st_l2_cache_hint_vector",
        }
        self.assertTrue(expected_partition.issubset(variants))
        for name in (
            "ld_global_l2_evict_vector",
            "ld_l2_evict_vector",
            "ld_global_nc_l2_evict",
            "st_global_l2_evict_vector",
            "st_l2_evict_vector",
        ):
            l1 = next(
                modifier for modifier in variants[name].modifiers
                if modifier.name == "l1_eviction_priority"
            )
            self.assertEqual(l1.presence, "optional")
            self.assertEqual(l1.default, "invalid")
        for name in (
            "ld_global_l2_evict_vector",
            "ld_global_u32_l1_evict",
            "ld_global_u32_l2_cache_hint",
            "ld_global_nc_l2_evict",
            "ld_global_nc_cache_hint_scalar",
        ):
            prefetch = next(
                modifier for modifier in variants[name].modifiers
                if modifier.name == "prefetch_size"
            )
            self.assertEqual(prefetch.presence, "optional")
            self.assertEqual(prefetch.default, "none")
        self.assertFalse(
            any(
                modifier.name == "prefetch_size"
                for name, variant in variants.items()
                if name.startswith("st_")
                for modifier in variant.modifiers
            )
        )
