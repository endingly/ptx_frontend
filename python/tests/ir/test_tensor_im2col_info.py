"""Closed im2col read modes, owned information bindings, and target gates."""

from dataclasses import replace
import unittest

from ptx_frontend.ir.resolved_ir import (
    TensorAccessMode, from_instruction_spec, tensor_im2col_info_contract,
)
from ptx_frontend.spec.database import load_packaged_spec_database
from ptx_frontend.spec.model import ModifierPresence, OperandKind


class TensorIm2colInfoTests(unittest.TestCase):
    """Check the independent normative mode/topology/rank inventory."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.cp = next(
            entry for entry in load_packaged_spec_database().instructions
            if entry.opcode == "cp"
        )

    def test_all_27_identities_and_54_info_layouts(self) -> None:
        resolved = from_instruction_spec(self.cp)
        expected = {
            f"cp_async_bulk_tensor_{rank}d_shared_{space}_{mode}"
            for rank in (3, 4, 5)
            for space in ("cta", "cluster")
            for mode in ("im2col", "im2col_w", "im2col_w128")
        } | {
            f"cp_async_bulk_prefetch_tensor_{rank}d_{mode}"
            for rank in (3, 4, 5)
            for mode in ("im2col", "im2col_w", "im2col_w128")
        }
        selected = {variant.variant_id: variant for variant in resolved.variants
                    if variant.tensor_im2col_info_elements
                    and not variant.tensor_multicast}
        self.assertEqual(set(selected), expected)
        self.assertEqual(sum(len(v.operand_layouts) for v in selected.values()), 54)
        for name, variant in selected.items():
            with self.subTest(name=name):
                self.assertIn(variant.tensor_access_mode, {
                    TensorAccessMode.IM2COL, TensorAccessMode.IM2COL_W,
                    TensorAccessMode.IM2COL_W128,
                })
                self.assertEqual(
                    {layout.layout_id for layout in variant.operand_layouts},
                    {"without_info", "with_info"},
                )
                for layout in variant.operand_layouts:
                    fields = {field.name: field for field in layout.fields}
                    self.assertEqual("im2col_info" in fields,
                                     layout.layout_id == "with_info")
                    self.assertEqual(
                        sum(binding.tensor_access_mode is variant.tensor_access_mode
                            for binding in layout.bindings), 1,
                        "the tensor binding is the only mode authority",
                    )
        old = [v for v in resolved.variants
               if v.tensor_access_mode in {
                   TensorAccessMode.TILED, TensorAccessMode.IM2COL_NO_OFFS}
               and not v.tensor_multicast]
        self.assertEqual(len(old), 87)
        self.assertTrue(all(not any("im2col_info" == field.name
                                    for layout in variant.operand_layouts
                                    for field in layout.fields)
                            for variant in old))

    def test_independent_expected_roles_and_unsigned_bounds(self) -> None:
        expected = {
            (TensorAccessMode.IM2COL, 3): (("OffsetW", 65535),),
            (TensorAccessMode.IM2COL, 4): (("OffsetW", 255), ("OffsetH", 255)),
            (TensorAccessMode.IM2COL, 5): (
                ("OffsetW", 31), ("OffsetH", 31), ("OffsetD", 31)),
            (TensorAccessMode.IM2COL_W, 3): (("Halo", 511), ("Offset", 31)),
            (TensorAccessMode.IM2COL_W128, 5): (("Halo", 31), ("Offset", 31)),
        }
        for key, roles in expected.items():
            self.assertEqual(tensor_im2col_info_contract(*key), roles)
        for mode in (TensorAccessMode.IM2COL_W, TensorAccessMode.IM2COL_W128):
            self.assertEqual(tensor_im2col_info_contract(mode, 3),
                             tensor_im2col_info_contract(mode, 4))
        for rank in (1, 2, 6):
            with self.assertRaises(ValueError):
                tensor_im2col_info_contract(TensorAccessMode.IM2COL, rank)
        with self.assertRaises(ValueError):
            tensor_im2col_info_contract(TensorAccessMode.TILED, 3)

    def test_exact_target_metadata(self) -> None:
        variants = {v.name: v for v in self.cp.variants}
        def availability(name):
            return variants[name].availability
        self.assertEqual(availability("cp_async_bulk_tensor_3d_shared_cluster_im2col"),
                         {"ptx": "8.0", "sm": 90})
        self.assertEqual(availability("cp_async_bulk_tensor_3d_shared_cta_im2col"),
                         {"ptx": "8.6", "sm": 90})
        self.assertEqual(availability("cp_async_bulk_tensor_3d_shared_cta_im2col_w"),
                         {"ptx": "8.6", "sm": 100})
        wider = {"any_of": [
            {"ptx": "8.6", "sm": 100, "target": "sm_100a"},
            {"ptx": "8.8", "sm": 100, "family": "sm_100f"},
            {"ptx": "9.0", "sm": 110, "family": "sm_110f"},
        ]}
        for rank in (3, 4, 5):
            with self.subTest(rank=rank):
                self.assertEqual(
                    availability(f"cp_async_bulk_tensor_{rank}d_shared_cluster_im2col_w"),
                    wider,
                )
        for name in (
            "cp_async_bulk_tensor_3d_shared_cta_im2col_w128",
            "cp_async_bulk_tensor_3d_shared_cluster_im2col_w128",
            "cp_async_bulk_prefetch_tensor_3d_im2col_w",
            "cp_async_bulk_prefetch_tensor_3d_im2col_w128",
        ):
            self.assertEqual(availability(name), wider)

    def test_invalid_fixed_flags_layouts_and_info_binding(self) -> None:
        selected = next(v for v in self.cp.variants
                        if v.name == "cp_async_bulk_tensor_4d_shared_cluster_im2col")
        modifiers = {m.name: m for m in selected.modifiers}
        no_info, with_info = selected.operand_layouts
        info = with_info.operands[-1]
        mutations = [
            replace(selected, modifiers=tuple(
                replace(m, value=False) if m.name == "im2col" else m
                for m in selected.modifiers)),
            replace(selected, modifiers=tuple(
                replace(m, token=".tile") if m.name == "im2col" else m
                for m in selected.modifiers)),
            replace(selected, modifiers=selected.modifiers + (modifiers["im2col"],)),
            replace(selected, modifiers=tuple(
                replace(m, token=".2d") if m.name == "rank" else m
                for m in selected.modifiers)),
            replace(selected, modifiers=tuple(
                replace(m, presence=ModifierPresence.OPTIONAL)
                if m.name == "im2col" else m for m in selected.modifiers)),
            replace(selected, operand_layouts=(with_info,)),
            replace(selected, operand_layouts=(no_info, no_info)),
            replace(selected, operand_layouts=(no_info, replace(
                with_info, operands=with_info.operands[:-1]))),
            replace(selected, operand_layouts=(no_info, replace(
                with_info, operands=with_info.operands[:-1] +
                (replace(info, kind=OperandKind.TENSOR_COORDINATE),)))),
            replace(selected, operand_layouts=(no_info, replace(
                with_info, operands=with_info.operands[:-1] +
                (replace(info, minimum_elements=3),)))),
        ]
        for candidate in mutations:
            with self.subTest(candidate=candidate):
                with self.assertRaises(ValueError):
                    from_instruction_spec(replace(self.cp, variants=(candidate,)))


if __name__ == "__main__":
    unittest.main()
