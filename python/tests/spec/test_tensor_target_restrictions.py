"""Pin cluster gather/W target intersections without broadening adjacent grants."""
import unittest
from dataclasses import replace
from copy import deepcopy
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.ir.resolved_ir import from_instruction_spec

RESTRICTED = {"any_of": [{"ptx": "8.6", "sm": 100, "target": "sm_100a"},
                          {"ptx": "8.8", "sm": 100, "family": "sm_100f"}]}
QUALIFIED = {"any_of": RESTRICTED["any_of"] +
             [{"ptx": "9.0", "sm": 110, "family": "sm_110f"}]}

class TensorTargetRestrictionTests(unittest.TestCase):
    """Typed mode and destination intersect the existing CTA-group target grant."""

    def test_exact_cluster_feature_layout_and_group_closure(self) -> None:
        """All thirty-two variants preserve eighty-four layouts and126 group keys."""
        cp = next(i for i in get_packaged_spec_database().instructions if i.opcode == "cp")
        derived = from_instruction_spec(cp)
        selected = []
        layouts = keys = 0
        for variant in cp.variants:
            tokens = {m.token for m in variant.modifiers}
            if ".shared::cluster" not in tokens or not tokens.intersection({".tile::gather4", ".im2col::w"}):
                continue
            selected.append(variant)
            self.assertEqual(variant.availability, RESTRICTED)
            layouts += len(variant.operand_layouts)
            group = next((m for m in variant.modifiers if m.name == "cta_group"), None)
            keys += len(variant.operand_layouts) * (len(group.values) if group else 1)
            emitted = next(v for v in derived.variants if v.variant_id == variant.name)
            self.assertEqual(dict(emitted.availability), RESTRICTED)
            self.assertEqual(len(emitted.operand_layouts), len(variant.operand_layouts))
        self.assertEqual((len(selected), layouts, keys), (32, 84, 126))
        for name in ("cp_async_bulk_tensor_3d_shared_cluster_im2col_w128",
                     "cp_async_bulk_tensor_1d_shared_cluster_cta_group"):
            self.assertEqual(next(v for v in cp.variants if v.name == name).availability, QUALIFIED)

    def test_grouped_target_gate_keeps_the_exact_typed_intersection(self) -> None:
        """Wrong target lists remain invalid for both restricted and adjacent modes."""
        cp = next(i for i in get_packaged_spec_database().instructions if i.opcode == "cp")
        for name, wrong in (
            ("cp_async_bulk_tensor_2d_shared_cluster_tile_gather4_cta_group", QUALIFIED),
            ("cp_async_bulk_tensor_3d_shared_cluster_im2col_w_cta_group", QUALIFIED),
            ("cp_async_bulk_tensor_1d_shared_cluster_cta_group", RESTRICTED),
            ("cp_async_bulk_tensor_3d_shared_cluster_im2col_w128_cta_group", RESTRICTED),
            ("cp_async_bulk_tensor_2d_shared_cta_tile_gather4_cta_group", RESTRICTED),
        ):
            variant = next(v for v in cp.variants if v.name == name)
            self.assertEqual(len(from_instruction_spec(replace(cp, variants=(variant,))).variants), 1)
            with self.assertRaisesRegex(ValueError, "invalid CTA-group target gate"):
                from_instruction_spec(replace(cp, variants=(replace(variant, availability=wrong),)))

    def test_restricted_gate_rejects_structurally_wrong_alternatives(self) -> None:
        """Exact mode/topology validation rejects malformed lists and ignores names."""
        cp = next(i for i in get_packaged_spec_database().instructions if i.opcode == "cp")
        for name in (
            "cp_async_bulk_tensor_2d_shared_cluster_tile_gather4_cta_group",
            "cp_async_bulk_tensor_3d_shared_cluster_im2col_w_multicast_cta_group_cache_hint",
        ):
            variant = next(v for v in cp.variants if v.name == name)
            missing = {"any_of": RESTRICTED["any_of"][:1]}
            reordered = {"any_of": list(reversed(RESTRICTED["any_of"]))}
            unknown = deepcopy(RESTRICTED)
            unknown["any_of"].append({"ptx": "9.0", "sm": 120, "family": "sm_120f"})
            threshold = deepcopy(RESTRICTED)
            threshold["any_of"][1]["ptx"] = "8.7"
            for wrong in (QUALIFIED, missing, reordered, unknown, threshold):
                with self.assertRaisesRegex(ValueError, "invalid CTA-group target gate"):
                    from_instruction_spec(replace(cp, variants=(replace(variant, availability=wrong),)))
        ordinary = next(v for v in cp.variants if v.name == "cp_async_bulk_tensor_2d_shared_cluster_tile_gather4_cta_group")
        renamed = replace(ordinary, name="cp_independent_typed_tensor_case")
        result = from_instruction_spec(replace(cp, variants=(renamed,)))
        self.assertEqual(dict(result.variants[0].availability), RESTRICTED)

if __name__ == "__main__":
    unittest.main()
