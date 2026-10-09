"""Fixed sparse WS source topology and conditional metadata rules."""

from dataclasses import replace
import unittest

from ptx_frontend.spec import tcgen_mma_operations as rules
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.normalize.tcgen_mma import validate_tcgen_mma_variant
from ptx_frontend.spec.resources import packaged_spec_dir


class TcgenMmaWsSparseContractTests(unittest.TestCase):
    """Keep sparse metadata independent of the existing dense WS source."""

    def test_four_identities_and_metadata_between_b_and_idesc(self) -> None:
        """All four source classes retain both A and zero-column choices."""

        raw = load_yaml(packaged_spec_dir() / "tensor_memory_data_movement.yaml")
        forms = {v.name: v for v in normalize_instruction_spec(raw)[0].variants}
        for kind in ("f16", "tf32", "f8f6f4", "i8"):
            form = forms[f"tcgen05_mma_ws_sp_{kind}"]
            self.assertEqual(tuple(m.name for m in form.modifiers),
                             ("mma", "ws", "sp", "cta_group", "kind",
                              "collector"))
            self.assertEqual(len(form.modifiers[-1].values), 16)
            self.assertEqual({layout.name for layout in form.operand_layouts},
                             {"shared", "shared_zero", "tensor",
                              "tensor_zero"})
            for layout in form.operand_layouts:
                names = tuple(item.name for item in layout.operands)
                self.assertEqual(names[2:5], ("b", "sp_meta", "idesc"))
                self.assertEqual(names[-1], "zero_column_desc" if
                                 layout.name.endswith("zero") else
                                 "enable_input_d")
            broken = replace(form.operand_layouts[0], operands=tuple(
                item for item in form.operand_layouts[0].operands
                if item.name != "sp_meta"))
            with self.assertRaises(ValueError):
                validate_tcgen_mma_variant(replace(
                    form, operand_layouts=(broken,) + form.operand_layouts[1:]))
            with self.assertRaises(ValueError):
                validate_tcgen_mma_variant(replace(
                    form, modifiers=(form.modifiers[0], form.modifiers[2],
                                     form.modifiers[1]) + form.modifiers[3:]))

    def test_sparse_ws_rows_metadata_and_m32_obligation(self) -> None:
        """Compressed K, lane zero and absent M32 mapping remain separate."""

        rules.validate_catalogue()
        self.assertEqual(sum(row.sparse for row in rules.WS_SHAPES), 6)
        for kind, k, d_type, a_type, b_type, nibble in (
                ("F16", 32, "F16", "F16", "F16", 14),
                ("Tf32", 16, "F32", "TF32", "TF32", 4),
                ("F8F6F4", 64, "F16", "E4M3", "E5M2", 14),
                ("I8", 64, "S32", "U8", "S8", 14)):
            facts = rules.WsKnownFacts(
                kind=kind, source_sparse=True, word_kind=kind,
                group=1, m=32, n=64, k=k, d_type=d_type,
                a_type=a_type, b_type=b_type, sparse=True,
                a_shared=False, sparse_selector=0,
                metadata_nibbles=(nibble,), a_lane_half=0,
                d_lane_half=0, metadata_lane_half=0)
            with self.subTest(kind=kind):
                report = rules.check_ws_known_facts(facts)
                self.assertTrue(report.known_facts_ok, report.violations)
                self.assertEqual((report.compressed_a_k, report.logical_b_k),
                                 (k // 2, k))
                self.assertEqual(report.path.layout, "G")
                self.assertIn("metadata_layout_rule", report.obligations)
                self.assertIn("live_metadata_contents", report.obligations)
                self.assertIn("ws_shape", rules.check_ws_known_facts(
                    replace(facts, n=256)).violations)
                self.assertIn("sparsity_bit", rules.check_ws_known_facts(
                    replace(facts, sparse=False)).violations)
                self.assertIn("metadata_indices", rules.check_ws_known_facts(
                    replace(facts, metadata_nibbles=(0,))).violations)
                for role in ("a_lane_half", "d_lane_half",
                             "metadata_lane_half"):
                    self.assertIn(f"{role}_alignment",
                                  rules.check_ws_known_facts(replace(
                                      facts, **{role: 16})).violations)
                    self.assertIn(role, rules.check_ws_known_facts(replace(
                        facts, **{role: None})).obligations)
                if kind in ("F8F6F4", "I8"):
                    self.assertIn("sparsity_selector",
                                  rules.check_ws_known_facts(replace(
                                      facts, sparse_selector=1)).violations)
            self.assertNotIn("metadata_layout_rule", rules.check_ws_known_facts(
                replace(facts, m=64)).obligations)

    def test_a_placement_facts(self) -> None:
        """Sparse WS preserves shared A validity and rejects TMEM conflicts."""

        facts = rules.WsKnownFacts(
            kind="F16", source_sparse=True, word_kind="F16", group=1,
            m=64, n=64, k=32, d_type="F16", a_type="F16", b_type="F16",
            sparse=True, sparse_selector=0, metadata_nibbles=(14,),
            a_shared=False, transpose_a=False, transpose_b=False,
            a_lane_half=0, d_lane_half=0, metadata_lane_half=0)
        self.assertTrue(rules.check_ws_known_facts(facts).known_facts_ok)
        known_a = rules.SharedOperandFacts("K", "B32")
        self.assertIn("a_placement_facts", rules.check_ws_known_facts(
            replace(facts, a_shared_facts=known_a)).violations)
        shared = replace(facts, a_shared=True, a_shared_facts=known_a)
        self.assertTrue(rules.check_ws_known_facts(shared).known_facts_ok)
        self.assertIn("a_major_transpose", rules.check_ws_known_facts(
            replace(shared, a_shared_facts=rules.SharedOperandFacts(
                "MN", "B32"))).violations)
        self.assertIn("a_placement_facts", rules.check_ws_known_facts(
            replace(facts, a_shared_facts=rules.SharedOperandFacts(
                "MN", "bogus"))).violations)


if __name__ == "__main__":
    unittest.main()
