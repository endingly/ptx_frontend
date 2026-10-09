"""Fixed PTX 9.3 sparse block-scale MMA forms and conditional data rules."""

from dataclasses import replace
import unittest

from ptx_frontend.spec import tcgen_mma_operations as rules
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.normalize.tcgen_mma import validate_tcgen_mma_variant
from ptx_frontend.spec.resources import packaged_spec_dir


class TcgenMmaSparseMxContractTests(unittest.TestCase):
    """Preserve three exact sparse MX identities and independent scale rows."""

    def test_source_metadata_and_selector_topology(self) -> None:
        """All placements use metadata after B and scales after idesc."""

        raw = load_yaml(packaged_spec_dir() / "tensor_memory_data_movement.yaml")
        variants = {v.name: v for v in normalize_instruction_spec(raw)[0].variants}
        for name, optional in (("mxf8f6f4", True), ("mxf4", True),
                               ("mxf4nvf4", False)):
            form = variants[f"tcgen05_mma_sp_{name}"]
            self.assertEqual(tuple(m.name for m in form.modifiers),
                             ("mma", "sp", "cta_group", "kind",
                              "block_scale", "scale_vector_size", "collector"))
            self.assertEqual(len(form.operand_layouts), 2)
            scale = form.modifiers[-2]
            self.assertEqual(scale.default, "absent" if optional else None)
            for layout in form.operand_layouts:
                self.assertEqual(tuple(x.name for x in layout.operands),
                                 ("d", "a", "b", "sp_meta", "idesc",
                                  "scale_a", "scale_b", "enable_input_d"))
            layout = form.operand_layouts[0]
            with self.assertRaises(ValueError):
                validate_tcgen_mma_variant(replace(
                    form, operand_layouts=(replace(
                        layout, operands=tuple(x for x in layout.operands
                                               if x.name != "sp_meta")),) +
                    form.operand_layouts[1:]))

    def test_sparse_mx_rows_and_known_facts(self) -> None:
        """Sparse K, pairwise metadata and scale factors use fixed rows."""

        rules.validate_catalogue()
        self.assertEqual(len(rules.SPARSE_MX_SHAPES), 6)
        self.assertEqual(len(rules.SPARSE_MX_SCALE_LAYOUTS), 16)
        for kind, k, selector, scale_type, factors, ids, granularity in (
                ("MxF8F6F4", 64, "absent", "UE8M0", 1, (0, 1, 2, 3),
                 rules.SparseMxMetadataGranularity.TWO_OF_FOUR),
                ("MxF4", 128, "absent", "UE8M0", 2, (0, 2),
                 rules.SparseMxMetadataGranularity.PAIRWISE_FOUR_OF_EIGHT),
                ("MxF4NvF4", 128, "block16", "UE4M3", 4, (0,),
                 rules.SparseMxMetadataGranularity.PAIRWISE_FOUR_OF_EIGHT)):
            low = kind != "MxF8F6F4"
            facts = rules.SparseMxKnownFacts(
                kind=kind, word_kind=kind, group=2, m=256, n=32, k=k,
                d_type="F32", a_type="E2M1" if low else "E4M3",
                b_type="E2M1" if low else "E4M3", sparse=True,
                a_shared=False, scale_selector=selector,
                scale_type=scale_type, scale_a_id=ids[0], scale_b_id=ids[0],
                metadata_nibbles=(14,), k_choice=0,
                a_lane_half=0, d_lane_half=0, metadata_lane_half=0)
            report = rules.check_sparse_mx_known_facts(facts)
            with self.subTest(kind=kind):
                self.assertTrue(report.known_facts_ok, report.violations)
                self.assertEqual(report.shape.k, k)
                self.assertEqual(report.scale_a_layout.factor_count, factors)
                self.assertEqual(report.scale_a_layout.valid_ids, ids)
                self.assertEqual(report.metadata_rule.granularity,
                                 granularity)
                self.assertIn("live_metadata_contents", report.obligations)
                unknown_types = rules.check_sparse_mx_known_facts(replace(
                    facts, a_type=None, b_type=None))
                self.assertIn("a_type", unknown_types.obligations)
                self.assertIn("b_type", unknown_types.obligations)
                self.assertNotIn("a_packing_fact", unknown_types.violations)
                invalid_partial = rules.check_sparse_mx_known_facts(replace(
                    facts, group=3, m=None, n=None, k=None, d_type=None,
                    a_type=None, b_type=None))
                self.assertIn("cta_group", invalid_partial.violations)
                self.assertIn("a_type", invalid_partial.obligations)
                unknown_group = rules.check_sparse_mx_known_facts(replace(
                    facts, group=None))
                self.assertIn("group", unknown_group.obligations)
                if low:
                    self.assertIn("a_live_packing_contents",
                                  report.obligations)
                for mutation, violation in (
                        ({"m": 128}, "sparse_mx_shape"),
                        ({"k": 96}, "sparse_mx_shape"),
                        ({"sparse": False}, "sparse_bit"),
                        ({"metadata_nibbles": (0,)}, "metadata_indices"),
                        ({"d_lane_half": 16}, "d_lane_half_alignment"),
                        ({"word_kind": "F16"}, "word_kind")):
                    changed = rules.check_sparse_mx_known_facts(
                        replace(facts, **mutation))
                    self.assertIn(violation, changed.violations)
                if low:
                    self.assertIn("sparse_k_choice",
                                  rules.check_sparse_mx_known_facts(replace(
                                      facts, k_choice=1)).violations)
                    self.assertIn("scale_a_id",
                                  rules.check_sparse_mx_known_facts(replace(
                                      facts, scale_a_id=1)).violations)
            if kind == "MxF4NvF4":
                self.assertIn("scale_selector",
                              rules.check_sparse_mx_known_facts(replace(
                                  facts, scale_selector="absent")).violations)


if __name__ == "__main__":
    unittest.main()
