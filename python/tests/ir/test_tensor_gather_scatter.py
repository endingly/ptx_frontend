"""Canonical gather/scatter modes keep tensor rank separate from arity."""

from dataclasses import replace
import unittest

from ptx_frontend.ir.resolved_ir import TensorAccessMode, from_instruction_spec
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import ModifierPresence


class TensorGatherScatterTests(unittest.TestCase):
    """Check the four fixed forms and the generated rank binding."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.cp = next(spec for spec in get_packaged_spec_database().instructions
                      if spec.opcode == "cp")

    def test_four_forms_and_old_tensor_parity(self) -> None:
        resolved = from_instruction_spec(self.cp)
        expected = {
            "cp_async_bulk_tensor_2d_shared_cta_tile_gather4",
            "cp_async_bulk_tensor_2d_shared_cluster_tile_gather4",
            "cp_async_bulk_prefetch_tensor_2d_tile_gather4",
            "cp_async_bulk_tensor_2d_global_shared_cta_tile_scatter4",
        }
        gathered = {variant.variant_id: variant for variant in resolved.variants
                    if variant.tensor_access_mode in {
                        TensorAccessMode.TILE_GATHER4,
                        TensorAccessMode.TILE_SCATTER4,
                    } and not variant.tensor_multicast
                    and not variant.variant_id.endswith("_cta_group")}
        self.assertEqual(set(gathered), expected)
        self.assertEqual(sum(not v.variant_id.endswith("_cta_group")
                             for v in resolved.variants), 250)
        old = [variant for variant in resolved.variants
               if variant.tensor_access_mode is not None
               and variant.variant_id not in gathered
               and not variant.tensor_multicast
               and not variant.variant_id.endswith("_cta_group")]
        self.assertEqual(len(old), 114)
        self.assertEqual(sum(len(variant.operand_layouts) for variant in old), 141)
        for variant in resolved.variants:
            for layout in variant.operand_layouts:
                for field, binding in zip(layout.fields, layout.bindings, strict=True):
                    if field.name == "tensor":
                        with self.subTest(variant=variant.variant_id,
                                          layout=layout.layout_id):
                            self.assertIs(binding.tensor_access_mode,
                                          variant.tensor_access_mode)
                            self.assertIn(binding.expected_tensor_rank,
                                          range(1, 6))
                            expected_count = (5 if variant.tensor_access_mode in {
                                                  TensorAccessMode.TILE_GATHER4,
                                                  TensorAccessMode.TILE_SCATTER4}
                                              else binding.expected_tensor_rank)
                            self.assertEqual(binding.minimum_elements, expected_count)
                            self.assertEqual(binding.maximum_elements, expected_count)
                    else:
                        self.assertIsNone(binding.expected_tensor_rank)
        for variant in gathered.values():
            self.assertEqual(len(variant.operand_layouts), 1)
            tensor = next(binding for binding in variant.operand_layouts[0].bindings
                          if binding.tensor_access_mode is not None)
            self.assertEqual(tensor.expected_tensor_rank, 2)

    def test_exact_target_metadata(self) -> None:
        variants = {variant.name: variant for variant in self.cp.variants}
        qualified = {"any_of": [
            {"ptx": "8.6", "sm": 100, "target": "sm_100a"},
            {"ptx": "8.8", "sm": 100, "family": "sm_100f"},
            {"ptx": "9.0", "sm": 110, "family": "sm_110f"},
        ]}
        self.assertEqual(
            variants["cp_async_bulk_tensor_2d_shared_cta_tile_gather4"].availability,
            {"ptx": "8.6", "sm": 100},
        )
        for name in (
            "cp_async_bulk_tensor_2d_shared_cluster_tile_gather4",
            "cp_async_bulk_prefetch_tensor_2d_tile_gather4",
            "cp_async_bulk_tensor_2d_global_shared_cta_tile_scatter4",
        ):
            self.assertEqual(variants[name].availability, qualified)

    def test_malformed_rank_mode_topology_and_arity(self) -> None:
        selected = next(variant for variant in self.cp.variants
                        if variant.name ==
                        "cp_async_bulk_tensor_2d_shared_cta_tile_gather4")
        by_name = {modifier.name: index
                   for index, modifier in enumerate(selected.modifiers)}

        def change_modifier(name: str, **changes):
            """Copy one canonical variant with a malformed fixed modifier."""
            modifiers = list(selected.modifiers)
            index = by_name[name]
            modifiers[index] = replace(modifiers[index], **changes)
            return replace(selected, modifiers=tuple(modifiers))

        layout = selected.operand_layouts[0]
        tensor = layout.operands[1]
        bad = (
            change_modifier("rank", token=".3d"),
            change_modifier("rank", value=False),
            change_modifier("tile_gather4", value=False),
            change_modifier("tile_gather4", token=".tile"),
            change_modifier("tile_gather4",
                            presence=ModifierPresence.OPTIONAL),
            change_modifier("dst_space", token=".global"),
            change_modifier("completion", token=".bulk_group"),
            replace(selected, operand_layouts=(replace(
                layout, operands=(layout.operands[0],
                                  replace(tensor, minimum_elements=2),
                                  layout.operands[2])),)),
            replace(selected, operand_layouts=(replace(
                layout, operands=(layout.operands[0], tensor)),)),
            replace(selected, modifiers=selected.modifiers +
                    (selected.modifiers[by_name["tile_gather4"]],)),
        )
        for variant in bad:
            with self.subTest(variant=variant):
                with self.assertRaises(ValueError):
                    from_instruction_spec(replace(self.cp, variants=(variant,)))


if __name__ == "__main__":
    unittest.main()
