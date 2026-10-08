"""Fixed no-offset tensor mode lowering and descriptor-binding contracts."""

from dataclasses import replace
import unittest

from ptx_frontend.ir.resolved_ir import TensorAccessMode, from_instruction_spec
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import ModifierPresence


class TensorNoOffsetsTests(unittest.TestCase):
    """The canonical mode is typed once and bound only to tensor operands."""

    @classmethod
    def setUpClass(cls) -> None:
        database = get_packaged_spec_database()
        cls.cp = next(spec for spec in database.instructions if spec.opcode == "cp")

    def test_complete_identity_and_binding_inventory(self) -> None:
        resolved = from_instruction_spec(self.cp)
        modes = {variant.variant_id: variant.tensor_access_mode
                 for variant in resolved.variants
                 if not variant.variant_id.endswith("_cta_group")}
        no_offsets = {name for name, mode in modes.items()
                      if mode is TensorAccessMode.IM2COL_NO_OFFS}
        expected = {
            f"cp_async_bulk_tensor_{rank}d_global_shared_cta_im2col_no_offs"
            for rank in range(3, 6)
        } | {
            f"cp_reduce_async_bulk_tensor_{rank}d_{operation}_im2col_no_offs"
            for rank in range(3, 6)
            for operation in ("add", "min", "max", "inc", "dec", "and", "or", "xor")
        }
        self.assertEqual(no_offsets, expected)
        self.assertEqual(sum(mode is TensorAccessMode.TILED
                             for mode in modes.values()), 65)
        self.assertEqual(sum(mode is None for mode in modes.values()),
                         len(modes) - 133)
        for variant in resolved.variants:
            for layout in variant.operand_layouts:
                for field, binding in zip(layout.fields, layout.bindings, strict=True):
                    with self.subTest(variant=variant.variant_id, field=field.name):
                        self.assertIs(
                            binding.tensor_access_mode,
                            variant.tensor_access_mode if field.name == "tensor" else None,
                        )

    def test_malformed_fixed_mode_rank_and_topology(self) -> None:
        selected = next(v for v in self.cp.variants
                        if v.name == "cp_async_bulk_tensor_3d_global_shared_cta_im2col_no_offs")
        tiled = next(v for v in self.cp.variants
                     if v.name == "cp_async_bulk_tensor_3d_global_shared_cta")
        positions = {modifier.name: i for i, modifier in enumerate(selected.modifiers)}

        def changed_modifier(name, **changes):
            modifiers = list(selected.modifiers)
            modifiers[positions[name]] = replace(modifiers[positions[name]], **changes)
            return replace(selected, modifiers=tuple(modifiers))

        bad = [
            changed_modifier("im2col_no_offs", value=False),
            changed_modifier("im2col_no_offs", presence=ModifierPresence.OPTIONAL),
            changed_modifier("im2col_no_offs", token=".im2col"),
            changed_modifier("rank", token=".2d"),
            changed_modifier("dst_space", token=".shared::cluster"),
            changed_modifier("completion", token=".mbarrier::complete_tx::bytes"),
            replace(selected, modifiers=tuple(
                modifier for modifier in selected.modifiers
                if modifier.name != "im2col_no_offs")),
            replace(selected, modifiers=selected.modifiers +
                    (selected.modifiers[positions["im2col_no_offs"]],)),
            replace(selected, modifiers=selected.modifiers +
                    (next(modifier for modifier in tiled.modifiers
                          if modifier.name == "tile"),)),
            replace(selected, operand_layouts=()),
        ]
        layout = selected.operand_layouts[0]
        bad.extend([
            replace(selected, operand_layouts=(replace(
                layout, operands=(replace(layout.operands[0], minimum_elements=2),
                                  layout.operands[1])),)),
            replace(selected, operand_layouts=(replace(
                layout, operands=(layout.operands[0],)),)),
        ])
        for variant in bad:
            with self.subTest(variant=variant):
                with self.assertRaises(ValueError):
                    from_instruction_spec(replace(self.cp, variants=(variant,)))


if __name__ == "__main__":
    unittest.main()
