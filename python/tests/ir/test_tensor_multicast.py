"""Cluster multicast keeps mask identity and source order in owned metadata."""

from dataclasses import replace
import unittest

from ptx_frontend.ir.resolved_ir import from_instruction_spec
from ptx_frontend.spec.database import load_packaged_spec_database
from ptx_frontend.spec.model import ModifierPresence, OperandKind


class TensorMulticastTests(unittest.TestCase):
    """Check all canonical cluster modes and reject malformed coupling."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.cp = next(spec for spec in load_packaged_spec_database().instructions
                      if spec.opcode == "cp")

    def test_fifteen_identities_and_twenty_four_layouts(self) -> None:
        resolved = from_instruction_spec(self.cp)
        selected = {variant.variant_id: variant for variant in resolved.variants
                    if variant.tensor_multicast
                    and not variant.variant_id.endswith("_cta_group")}
        expected = {
            f"cp_async_bulk_tensor_{rank}d_shared_cluster_multicast"
            for rank in range(1, 6)
        } | {
            "cp_async_bulk_tensor_2d_shared_cluster_tile_gather4_multicast"
        } | {
            f"cp_async_bulk_tensor_{rank}d_shared_cluster_{mode}_multicast"
            for rank in (3, 4, 5)
            for mode in ("im2col", "im2col_w", "im2col_w128")
        }
        self.assertEqual(set(selected), expected)
        self.assertEqual(sum(len(v.operand_layouts) for v in selected.values()), 24)
        old = [v for v in resolved.variants if v.tensor_access_mode is not None
               and not v.tensor_multicast
               and not v.variant_id.endswith("_cta_group")]
        self.assertEqual(len(old), 118)
        self.assertEqual(sum(len(v.operand_layouts) for v in old), 145)
        self.assertEqual(sum(not v.variant_id.endswith("_cta_group")
                             for v in resolved.variants), 250)
        for variant in selected.values():
            for layout in variant.operand_layouts:
                fields = [field.name for field in layout.fields]
                self.assertEqual(fields[-1], "cta_mask")
                self.assertEqual(fields.count("cta_mask"), 1)
                self.assertEqual(fields.index("cta_mask"), len(fields) - 1)
                self.assertEqual(fields.index("mbar"), 2)
                if "im2col_info" in fields:
                    self.assertEqual(fields.index("im2col_info"), 3)
                masks = [binding for binding in layout.bindings
                         if binding.tensor_cta_mask]
                self.assertEqual(len(masks), 1)
                self.assertEqual(masks[0].target_field_id, "cta_mask")
                self.assertIsNone(masks[0].tensor_access_mode)

    def test_coupling_and_topology_are_closed(self) -> None:
        source = next(v for v in self.cp.variants if v.name ==
                      "cp_async_bulk_tensor_3d_shared_cluster_im2col_multicast")
        modifiers = list(source.modifiers)
        multicast_index = next(i for i, m in enumerate(modifiers)
                               if m.name == "multicast")
        without_mask = replace(source, operand_layouts=tuple(
            replace(layout, operands=layout.operands[:-1])
            for layout in source.operand_layouts))
        bad_flag = replace(source, modifiers=tuple(
            replace(m, token=".multicast::other") if i == multicast_index else m
            for i, m in enumerate(modifiers)))
        no_flag = replace(source, modifiers=tuple(
            m for i, m in enumerate(modifiers) if i != multicast_index))
        wrong_direction = replace(source, modifiers=tuple(
            replace(m, token=".shared::cta") if m.name == "dst_space" else m
            for m in modifiers))
        info_layout = source.operand_layouts[1]
        reversed_mask = replace(source, operand_layouts=(
            source.operand_layouts[0],
            replace(info_layout, operands=info_layout.operands[:-2] +
                    info_layout.operands[-1:] + info_layout.operands[-2:-1]),
        ))
        wrong_source_kind = replace(source, operand_layouts=(
            source.operand_layouts[0],
            replace(info_layout, operands=info_layout.operands[:-1] +
                    (replace(info_layout.operands[-1],
                             kind=OperandKind.REGISTER),)),
        ))
        for variant in (without_mask, bad_flag, no_flag, wrong_direction,
                        reversed_mask, wrong_source_kind):
            with self.subTest(variant=variant.name):
                with self.assertRaises(ValueError):
                    from_instruction_spec(replace(self.cp, variants=(variant,)))

        ordinary = next(v for v in self.cp.variants if v.name ==
                        "cp_async_bulk_tensor_1d_shared_cluster")
        extra_mask = replace(ordinary, operand_layouts=(
            replace(ordinary.operand_layouts[0], operands=(
                *ordinary.operand_layouts[0].operands,
                info_layout.operands[-1])),))
        with self.assertRaises(ValueError):
            from_instruction_spec(replace(self.cp, variants=(extra_mask,)))

    def test_mask_has_narrow_u16_use(self) -> None:
        for variant in self.cp.variants:
            if not variant.name.endswith("_multicast") or "tensor" not in variant.name:
                continue
            for layout in variant.operand_layouts:
                mask = layout.operands[-1]
                self.assertEqual(mask.kind, OperandKind.REGISTER_OR_IMMEDIATE)
                self.assertEqual(mask.type_expression.scalar_type, "u16")
                self.assertEqual(mask.immediate_conversion_policy.value, "narrow")
                self.assertEqual(mask.access.value, "read")
        selected = next(v for v in self.cp.variants if v.name ==
                        "cp_async_bulk_tensor_1d_shared_cluster_multicast")
        multicast = next(m for m in selected.modifiers if m.name == "multicast")
        self.assertIs(multicast.presence, ModifierPresence.FIXED)


if __name__ == "__main__":
    unittest.main()
