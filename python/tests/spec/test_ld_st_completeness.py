"""Database regression coverage for PTX 9.3 LD/ST memory contracts."""

from pathlib import Path
import unittest

from ptx_frontend.spec.database import load_codegen_database
from ptx_frontend.spec.resources import packaged_spec_dir
from ptx_frontend.spec.model import ModifierKind, ModifierPresence

SPEC_DIR = packaged_spec_dir()


class LdStCompletenessTest(unittest.TestCase):
    """Assert generated-memory metadata remains canonical and target-qualified."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the source-tree instruction database once for these checks."""
        cls.database = load_codegen_database(spec_dir=SPEC_DIR)

    def test_ldu_type_and_vector_matrix(self) -> None:
        """Keep the documented uniform-load forms within 128 vector bits."""
        ldu = next(
            instruction
            for instruction in self.database.instructions
            if instruction.opcode == "ldu"
        )
        variants = {variant.name: variant for variant in ldu.variants}
        self.assertEqual(
            set(variants),
            {
                "ldu_generic_scalar", "ldu_explicit_scalar",
                "ldu_generic_v2", "ldu_explicit_v2",
                "ldu_generic_v4", "ldu_explicit_v4",
            },
        )
        scalar_types = {
            f"{family}{bits}"
            for family in ("b", "u", "s")
            for bits in (8, 16, 32, 64)
        } | {"b128", "f32", "f64"}
        v2_types = scalar_types - {"b128"}
        v4_types = {
            f"{family}{bits}"
            for family in ("b", "u", "s")
            for bits in (8, 16, 32)
        } | {"f32"}
        for name, types in (
            ("ldu_generic_scalar", scalar_types),
            ("ldu_explicit_scalar", scalar_types),
            ("ldu_generic_v2", v2_types),
            ("ldu_explicit_v2", v2_types),
            ("ldu_generic_v4", v4_types),
            ("ldu_explicit_v4", v4_types),
        ):
            variant = variants[name]
            type_modifier = next(
                modifier for modifier in variant.modifiers if modifier.name == "type"
            )
            self.assertEqual({value.value for value in type_modifier.values}, types)
            self.assertEqual(
                variant.availability,
                {"ptx": "2.0", "sm": 20 if "generic" in name else 0},
            )
            if "b128" in types:
                b128 = next(
                    value for value in type_modifier.values if value.value == "b128"
                )
                self.assertEqual(b128.availability, {"ptx": "8.3", "sm": 70})
            if "f64" in types:
                f64 = next(
                    value for value in type_modifier.values if value.value == "f64"
                )
                self.assertEqual(f64.availability, {"sm": 13})

    def test_scalar_memory_forms_admit_b128_at_its_documented_minimum(self) -> None:
        """Keep the `.b128` source/target gate on every scalar LD/ST family."""
        variants = {
            variant.name: variant
            for instruction in self.database.instructions
            if instruction.opcode in {"ld", "st"}
            for variant in instruction.variants
        }
        for name in (
            "ld_generic_scalar",
            "ld_explicit_scalar",
            "st_generic_scalar",
            "st_explicit_scalar",
        ):
            type_modifier = next(
                modifier
                for modifier in variants[name].modifiers
                if modifier.name == "type"
            )
            b128 = next(
                value for value in type_modifier.values if value.value == "b128"
            )
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
            {
                (value.value, tuple(value.availability.items()))
                for value in ld.mmio_semantics  # pyright: ignore[reportOptionalMemberAccess]
            },
            {("relaxed", ()), ("acquire", (("ptx", "9.3"), ("sm", 75)))},
        )
        self.assertEqual(
            {
                (value.value, tuple(value.availability.items()))
                for value in st.mmio_semantics  # pyright: ignore[reportOptionalMemberAccess]
            },
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
            self.assertIs(prefetch.kind, ModifierKind.PREFETCH_SIZE)
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
            self.assertEqual(
                vector.availability,  # pyright: ignore[reportOptionalMemberAccess]
                {
                    "ptx": "8.8",
                    "sm": 100,
                },
            )

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
                "ld_shared_cta_vector",
                "ld_shared_cluster_vector",
                "st_shared_cta_vector",
                "st_shared_cluster_vector",
                "ld_l2_evict_vector",
                "st_l2_evict_vector",
                "ld_global_nc_scalar",
                "ld_global_nc_vector",
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
            self.assertTrue(
                vector.require_modern  # pyright: ignore[reportOptionalMemberAccess]
            )
        self.assertNotIn("ld_global_l2_evict_scalar", variants)
        self.assertNotIn("st_global_l2_evict_scalar", variants)
        self.assertEqual(variants["ld_explicit_scalar"].unified_address_access, "read")
        self.assertEqual(variants["st_explicit_scalar"].unified_address_access, "write")
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
            "ld_global_l2_evict_vector",
            "ld_l2_evict_vector",
            "ld_global_l1_l2_cache_hint_prefetch_vector",
            "ld_l2_evict_cache_hint_vector",
            "ld_global_u32_l1_evict",
            "ld_global_l1_evict_vector",
            "ld_l1_evict_scalar",
            "ld_l1_evict_vector",
            "ld_global_l1_cache_hint_prefetch_scalar",
            "ld_global_l1_cache_hint_prefetch_vector",
            "ld_l1_cache_hint_scalar",
            "ld_l1_cache_hint_vector",
            "ld_global_u32_l2_cache_hint",
            "ld_global_l2_cache_hint_vector",
            "ld_l2_cache_hint_scalar",
            "ld_l2_cache_hint_vector",
            "ld_global_l2_prefetch_scalar",
            "ld_global_l2_prefetch_vector",
            "ld_l2_prefetch_scalar",
            "ld_l2_prefetch_vector",
            "ld_global_nc_l2_evict",
            "ld_global_nc_l2_evict_cache_hint_vector",
            "ld_global_nc_l1_no_allocate_u32",
            "ld_global_nc_l1_evict_vector",
            "ld_global_nc_l1_cache_hint_scalar",
            "ld_global_nc_l1_cache_hint_prefetch_legacy_vector",
            "ld_global_nc_cache_hint_scalar",
            "ld_global_nc_cache_hint_vector",
            "ld_global_nc_l2_prefetch_scalar",
            "ld_global_nc_l2_prefetch_vector",
            "st_global_l2_evict_vector",
            "st_l2_evict_vector",
            "st_global_l1_l2_cache_hint_vector",
            "st_l2_evict_cache_hint_vector",
            "st_global_u32_l1_evict",
            "st_global_l1_evict_vector",
            "st_l1_evict_scalar",
            "st_l1_evict_vector",
            "st_global_l1_cache_hint_scalar",
            "st_global_l1_cache_hint_vector",
            "st_l1_cache_hint_scalar",
            "st_l1_cache_hint_vector",
            "st_global_u32_l2_cache_hint",
            "st_global_l2_cache_hint_vector",
            "st_l2_cache_hint_scalar",
            "st_l2_cache_hint_vector",
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
                modifier
                for modifier in variants[name].modifiers
                if modifier.name == "l1_eviction_priority"
            )
            self.assertIs(l1.presence, ModifierPresence.OPTIONAL)
            self.assertEqual(l1.default, "invalid")
        for name in (
            "ld_global_l2_evict_vector",
            "ld_global_u32_l1_evict",
            "ld_global_u32_l2_cache_hint",
            "ld_global_nc_l2_evict",
            "ld_global_nc_cache_hint_scalar",
        ):
            prefetch = next(
                modifier
                for modifier in variants[name].modifiers
                if modifier.name == "prefetch_size"
            )
            self.assertIs(prefetch.presence, ModifierPresence.OPTIONAL)
            self.assertEqual(prefetch.default, "none")
        self.assertFalse(
            any(
                modifier.name == "prefetch_size"
                for name, variant in variants.items()
                if name.startswith("st_")
                for modifier in variant.modifiers
            )
        )
