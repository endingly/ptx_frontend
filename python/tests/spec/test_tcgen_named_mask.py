"""Exact mandatory-mask named admission and common physical-carrier parity."""
import unittest
from dataclasses import replace
from ptx_frontend.spec.normalize.tcgen_mma import validate_tcgen_mma_variant
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import OperandKind
from ptx_frontend.ir.resolved_ir import from_instruction_spec, ResolvedValueKind

EXPECTED_KEYS = (('tcgen05_mma_f16', 'shared_mask_no_scale'), ('tcgen05_mma_f16', 'shared_mask_scale'), ('tcgen05_mma_f16', 'tensor_mask_no_scale'), ('tcgen05_mma_f16', 'tensor_mask_scale'), ('tcgen05_mma_tf32', 'shared_mask_no_scale'), ('tcgen05_mma_tf32', 'shared_mask_scale'), ('tcgen05_mma_tf32', 'tensor_mask_no_scale'), ('tcgen05_mma_tf32', 'tensor_mask_scale'), ('tcgen05_mma_i8', 'shared_mask'), ('tcgen05_mma_i8', 'tensor_mask'), ('tcgen05_mma_f8f6f4', 'shared_mask'), ('tcgen05_mma_f8f6f4', 'tensor_mask'), ('tcgen05_mma_sp_f16', 'shared_mask_no_scale'), ('tcgen05_mma_sp_f16', 'shared_mask_scale'), ('tcgen05_mma_sp_f16', 'tensor_mask_no_scale'), ('tcgen05_mma_sp_f16', 'tensor_mask_scale'), ('tcgen05_mma_sp_tf32', 'shared_mask_no_scale'), ('tcgen05_mma_sp_tf32', 'shared_mask_scale'), ('tcgen05_mma_sp_tf32', 'tensor_mask_no_scale'), ('tcgen05_mma_sp_tf32', 'tensor_mask_scale'), ('tcgen05_mma_sp_f8f6f4', 'shared_mask'), ('tcgen05_mma_sp_f8f6f4', 'tensor_mask'), ('tcgen05_mma_sp_i8', 'shared_mask'), ('tcgen05_mma_sp_i8', 'tensor_mask'))
CARRIERS = ("b32", "u32", "s32", "f32", "f16x2")


class TcgenNamedMaskTests(unittest.TestCase):
    """Check the finite mask role rather than granting an opcode-wide vector rule."""

    def test_exact_twenty_four_mask_roles_keep_shape_and_order(self) -> None:
        """Only the authorized layouts admit named input and append one carrier."""
        spec = next(i for i in get_packaged_spec_database().instructions if i.opcode == "tcgen05")
        found = set()
        for variant in spec.variants:
            for layout in variant.operand_layouts:
                key = (variant.name, layout.name)
                if key not in EXPECTED_KEYS:
                    continue
                field = next(o for o in layout.operands if o.name == "disable_output_lane")
                self.assertIs(field.kind, OperandKind.REGISTER_VECTOR)
                self.assertTrue(field.vector_allow_named)
                self.assertFalse(field.vector_allow_sink)
                self.assertEqual(field.vector_allowed_register_types, CARRIERS)
                self.assertEqual(field.vector_arities, tuple(range(1, 9)))
                self.assertEqual(field.vector_type_policy.value, "element")
                self.assertIsNone(field.vector_arity_expression)
                self.assertEqual(field.type_expression.scalar_type, "b32")
                self.assertLess(tuple(o.name for o in layout.operands).index(field.name),
                                tuple(o.name for o in layout.operands).index("enable_input_d"))
                found.add(key)
        self.assertEqual(found, set(EXPECTED_KEYS))

    def test_derived_mask_remains_mandatory_register_vector(self) -> None:
        """The existing payload and group-dependent cardinality are unchanged."""
        spec = next(i for i in get_packaged_spec_database().instructions if i.opcode == "tcgen05")
        derived = from_instruction_spec(spec)
        actual = set()
        for variant in derived.variants:
            for layout in variant.operand_layouts:
                key = (variant.variant_id, layout.layout_id)
                if key not in EXPECTED_KEYS:
                    continue
                field = next(f for f in layout.fields if f.name == "disable_output_lane")
                binding = next(b for b in layout.bindings if b.target_field_id == field.name)
                self.assertIs(field.value_kind, ResolvedValueKind.REGISTER_VECTOR)
                self.assertTrue(binding.allow_named_vector)
                self.assertEqual(binding.allowed_vector_arities, tuple(range(1, 9)))
                self.assertEqual(binding.allowed_vector_register_types, CARRIERS)
                self.assertFalse(binding.allow_vector_sink)
                self.assertFalse(binding.source_value_vector)
                actual.add(key)
        self.assertEqual(actual, set(EXPECTED_KEYS))

    def test_normalizer_keeps_the_exact_ordered_common_domain(self) -> None:
        """Dense and sparse canonical masks accept five carriers and reject coherent domain edits."""
        spec = next(i for i in get_packaged_spec_database().instructions if i.opcode == "tcgen05")
        for name in ("tcgen05_mma_f16", "tcgen05_mma_sp_f16"):
            variant = next(v for v in spec.variants if v.name == name)
            validate_tcgen_mma_variant(variant)
            for layout_index, layout in enumerate(variant.operand_layouts):
                mask = next(o for o in layout.operands if o.name == "disable_output_lane")
                self.assertEqual(mask.vector_allowed_register_types, CARRIERS)
                for bad_domain in (CARRIERS[:-1], CARRIERS[:-1] + ("f64",), CARRIERS + ("b16",), tuple(reversed(CARRIERS))):
                    changed = replace(mask, vector_allowed_register_types=bad_domain)
                    bad_layout = replace(layout, operands=tuple(changed if o is mask else o for o in layout.operands))
                    layouts = list(variant.operand_layouts)
                    layouts[layout_index] = bad_layout
                    with self.subTest(variant=name, layout=layout.name, domain=bad_domain):
                        with self.assertRaisesRegex(ValueError, "^dense MMA mask carrier domain changed$"):
                            validate_tcgen_mma_variant(replace(variant, operand_layouts=tuple(layouts)))

    def test_removed_omission_layouts_stay_absent(self) -> None:
        """Name support never restores the removed optional-mask branches."""
        spec = next(i for i in get_packaged_spec_database().instructions if i.opcode == "tcgen05")
        names = {variant for variant, _ in EXPECTED_KEYS}
        layouts = {(v.name, l.name) for v in spec.variants if v.name in names
                   for l in v.operand_layouts}
        self.assertEqual(layouts, set(EXPECTED_KEYS))
        self.assertFalse(any("no_mask" in name for _, name in layouts))


if __name__ == "__main__":
    unittest.main()
