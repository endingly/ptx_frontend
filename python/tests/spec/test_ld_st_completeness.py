"""Database regression coverage for PTX 9.3 LD/ST memory contracts."""

import unittest

from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import (ModifierKind, ModifierPresence, OperandKind,
                                     OperandRegisterWidthPolicy, OperandTypeExpressionKind,
                                     OperandAddressBasePolicy)



class LdStCompletenessTest(unittest.TestCase):
    """Assert generated-memory metadata remains canonical and target-qualified."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the source-tree instruction database once for these checks."""
        cls.database = get_packaged_spec_database()

    def test_sync_scalar_store_data_uses_register_or_immediate(self) -> None:
        """Widen data only; vector, asynchronous and policy roles stay registers."""
        instruction = next(row for row in self.database.instructions if row.opcode == "st")
        scalars = set()
        for variant in instruction.variants:
            for layout in variant.operand_layouts:
                operands = {row.name: row for row in layout.operands}
                if "src" in operands:
                    data = operands["src"]
                    if data.kind is OperandKind.REGISTER_VECTOR:
                        continue
                    self.assertIs(data.kind, OperandKind.REGISTER_OR_IMMEDIATE)
                    self.assertIs(data.register_width_policy, OperandRegisterWidthPolicy.EQUAL_OR_WIDER)
                    scalars.add(variant.name)
                    if "cache_policy" in operands:
                        self.assertIs(operands["cache_policy"].kind, OperandKind.REGISTER)
        self.assertEqual(scalars, {
            "st_generic_scalar", "st_explicit_scalar", "st_shared_cta_scalar",
            "st_shared_cluster_scalar", "st_global_u32_l1_evict", "st_l1_evict_scalar",
            "st_global_l1_cache_hint_scalar", "st_l1_cache_hint_scalar",
            "st_global_u32_l2_cache_hint", "st_l2_cache_hint_scalar",
        })

    def test_all_async_store_data_keys_inherit_wider_carriers(self) -> None:
        """Keep the exact eight selectors, including combined compatibility flags."""

        names = {"st_async_shared_scalar", "st_async_shared_v2", "st_async_shared_v4",
                 "st_async_shared_cluster_scalar", "st_async_shared_cluster_v2", "st_async_shared_cluster_v4",
                 "st_async_global_release", "st_async_global_mmio_release"}
        instruction = next(row for row in self.database.instructions if row.opcode == "st")
        variants = [row for row in instruction.variants if row.name in names]
        self.assertEqual({row.name for row in variants}, names)
        branches = 0
        combined = 0
        for variant in variants:
            self.assertEqual(len(variant.operand_layouts), 1)
            operands = {row.name: row for row in variant.operand_layouts[0].operands}
            data = operands["value"]
            self.assertIs(data.register_width_policy, OperandRegisterWidthPolicy.EQUAL_OR_WIDER)
            self.assertIs(data.type_expression.kind, OperandTypeExpressionKind.MODIFIER)
            self.assertEqual(data.type_expression.modifier_name, "type")
            self.assertIn(data.kind, {OperandKind.REGISTER, OperandKind.REGISTER_VECTOR})
            self.assertIs(operands["dst"].address_base_policy, OperandAddressBasePolicy.REGISTER)
            dtype = next(row for row in variant.modifiers if row.name == "type")
            branches += len(dtype.values)
            if "shared_cluster" in variant.name:
                combined += 1
                self.assertFalse(any(row.name == "async" for row in variant.modifiers))
            if "mbar" in operands:
                self.assertIs(operands["mbar"].kind, OperandKind.ADDRESS)
                self.assertEqual(variant.completion_kind.value, "mbarrier_complete_tx_bytes")
            else:
                self.assertEqual(variant.completion_kind.value, "none")
        self.assertEqual(combined, 3)
        self.assertEqual(branches, 70)

    def test_every_cache_hint_variant_has_equivalent_policy_layouts(self) -> None:
        """Inventory the complete hint partition without changing its other gates."""
        expected = {
            "ld_global_l1_l2_cache_hint_prefetch_vector",
            "ld_l2_evict_cache_hint_vector",
            "ld_global_l1_cache_hint_prefetch_scalar",
            "ld_global_l1_cache_hint_prefetch_vector",
            "ld_l1_cache_hint_scalar", "ld_l1_cache_hint_vector",
            "ld_global_u32_l2_cache_hint", "ld_global_l2_cache_hint_vector",
            "ld_l2_cache_hint_scalar", "ld_l2_cache_hint_vector",
            "ld_global_nc_l2_evict_cache_hint_vector",
            "ld_global_nc_l1_cache_hint_scalar",
            "ld_global_nc_l1_cache_hint_prefetch_legacy_vector",
            "ld_global_nc_cache_hint_scalar", "ld_global_nc_cache_hint_vector",
            "st_global_l1_l2_cache_hint_vector", "st_l2_evict_cache_hint_vector",
            "st_global_l1_cache_hint_scalar", "st_global_l1_cache_hint_vector",
            "st_l1_cache_hint_scalar", "st_l1_cache_hint_vector",
            "st_global_u32_l2_cache_hint", "st_global_l2_cache_hint_vector",
            "st_l2_cache_hint_scalar", "st_l2_cache_hint_vector",
        }
        variants = {
            variant.name: variant
            for instruction in self.database.instructions
            if instruction.opcode in {"ld", "st"}
            for variant in instruction.variants
            if any(modifier.name == "cache_hint" for modifier in variant.modifiers)
        }
        self.assertEqual(set(variants), expected)
        for name, variant in variants.items():
            with self.subTest(variant=name):
                hint = next(modifier for modifier in variant.modifiers
                            if modifier.name == "cache_hint")
                self.assertIs(hint.presence, ModifierPresence.FIXED)
                self.assertIs(hint.value, True)
                self.assertEqual(hint.token, ".L2::cache_hint")
                policy, hint_only = variant.operand_layouts
                self.assertEqual((policy.name, hint_only.name),
                                 ("default", "without_policy"))
                self.assertEqual(policy.operands[:-1], hint_only.operands)
                self.assertEqual(policy.kind, hint_only.kind)
                self.assertEqual(policy.availability, hint_only.availability)
                self.assertEqual(policy.forbidden_modifiers,
                                 hint_only.forbidden_modifiers)
                self.assertEqual(len(hint_only.operands), 2)
                final = policy.operands[-1]
                self.assertEqual(final.name, "cache_policy")
                self.assertIs(final.kind, OperandKind.REGISTER)
                self.assertEqual(final.type_expression.scalar_type, "b64")
                modern = variant.memory_vector is not None and variant.memory_vector.require_modern
                self.assertEqual(variant.availability,
                                 {"ptx": "8.8", "sm": 100} if modern
                                 else {"ptx": "7.4", "sm": 80})

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
