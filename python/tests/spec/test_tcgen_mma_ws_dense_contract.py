"""Fixed dense WS source topology and independent known-value rules."""

from dataclasses import replace
import unittest

from ptx_frontend.spec import tcgen_mma_operations as rules
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.normalize.tcgen_mma import validate_tcgen_mma_variant
from ptx_frontend.spec.resources import packaged_spec_dir


class TcgenMmaWsDenseContractTests(unittest.TestCase):
    """Keep four exact WS kinds independent of non-WS half-lane paths."""

    def test_four_source_identities_and_optional_zero_tail(self) -> None:
        """Only CTA one and B0–B3 actions keep four exact A/zero layouts."""

        raw = load_yaml(packaged_spec_dir() / "tensor_memory_data_movement.yaml")
        forms = {v.name: v for v in normalize_instruction_spec(raw)[0].variants}
        for kind in ("f16", "tf32", "f8f6f4", "i8"):
            form = forms[f"tcgen05_mma_ws_{kind}"]
            with self.subTest(kind=kind):
                self.assertEqual(tuple(m.name for m in form.modifiers),
                                 ("mma", "ws", "cta_group", "kind", "collector"))
                self.assertEqual(tuple(v.value for v in form.modifiers[2].values),
                                 ("cta_group::1",))
                self.assertEqual(form.modifiers[-1].default, "absent")
                self.assertEqual(len(form.modifiers[-1].values), 16)
                self.assertEqual({layout.name for layout in form.operand_layouts},
                                 {"shared", "shared_zero", "tensor", "tensor_zero"})
                for layout in form.operand_layouts:
                    names = tuple(op.name for op in layout.operands)
                    self.assertEqual(names[:5],
                                     ("d", "a", "b", "idesc", "enable_input_d"))
                    self.assertEqual(names[5:],
                                     ("zero_column_desc",) if
                                     layout.name.endswith("_zero") else ())
                with self.assertRaises(ValueError):
                    validate_tcgen_mma_variant(replace(
                        form, modifiers=form.modifiers[:2] +
                        (replace(form.modifiers[2], values=(
                            *form.modifiers[2].values,
                            replace(form.modifiers[2].values[0],
                                    value="cta_group::2"))),) +
                        form.modifiers[3:]))
                with self.assertRaises(ValueError):
                    validate_tcgen_mma_variant(replace(
                        form, modifiers=form.modifiers[:-1]))
                with self.assertRaises(ValueError):
                    validate_tcgen_mma_variant(replace(
                        form, operand_layouts=(replace(
                            form.operand_layouts[1], operands=
                            form.operand_layouts[1].operands[:-1]),) +
                            form.operand_layouts[1:]))

    def test_shapes_lanes_zero_word_and_b_history(self) -> None:
        """M32/G, M64/E and M128/D all require lane zero."""

        rules.validate_catalogue()
        self.assertEqual(len(rules.WS_SHAPES), 6)
        self.assertEqual({(row.m, row.layout, row.allowed_lane_halves)
                          for row in rules.WS_PATHS},
                         {(32, "G", (0,)), (64, "E", (0,)), (128, "D", (0,))})
        for kind, dtype, atype, btype, k in (
                ("F16", "F32", "F16", "F16", 16),
                ("Tf32", "F32", "TF32", "TF32", 8),
                ("F8F6F4", "F16", "E4M3", "E5M2", 32),
                ("I8", "S32", "U8", "S8", 32)):
            for m, layout in ((32, "G"), (64, "E"), (128, "D")):
                facts = rules.WsKnownFacts(
                    kind=kind, word_kind=kind, group=1, m=m, n=64, k=k,
                    d_type=dtype, a_type=atype, b_type=btype, sparse=False,
                    a_shared=False, a_lane_half=0, d_lane_half=0)
                with self.subTest(kind=kind, m=m):
                    report = rules.check_ws_known_facts(facts)
                    self.assertTrue(report.known_facts_ok, report.violations)
                    self.assertEqual(report.path.layout, layout)
                    self.assertEqual(report.shape.k, k)
                    self.assertNotIn("zero_column_word", report.obligations)
                    self.assertIn("d_lane_half_alignment",
                                  rules.check_ws_known_facts(replace(
                                      facts, d_lane_half=16)).violations)
                    self.assertIn("a_lane_half_alignment",
                                  rules.check_ws_known_facts(replace(
                                      facts, a_lane_half=16)).violations)
                    self.assertIn("ws_shape", rules.check_ws_known_facts(
                        replace(facts, n=32)).violations)
        facts = rules.WsKnownFacts(
            kind="F16", word_kind="F16", group=1, m=32, n=64, k=16,
            d_type="F32", a_type="F16", b_type="F16", sparse=False,
            a_shared=False, a_lane_half=0, d_lane_half=0,
            zero_column_operand_present=True)
        self.assertIn("zero_column_word", rules.check_ws_known_facts(
            facts).obligations)
        self.assertTrue(rules.check_ws_known_facts(replace(
            facts, zero_column_word=0)).known_facts_ok)
        self.assertIn("zero_shift", rules.check_ws_known_facts(replace(
            facts, zero_column_word=17 << 56)).violations)
        self.assertIn("zero_defined_fields", rules.check_ws_known_facts(replace(
            facts, zero_column_word=1 << 36)).violations)
        self.assertEqual(rules.check_ws_known_facts(replace(
            facts, zero_column_word=1 << 62)).zero_unclassified_bits, 1 << 62)
        self.assertIn("zero_word_without_operand", rules.check_ws_known_facts(
            replace(facts, zero_column_operand_present=False,
                    zero_column_word=0)).violations)
        use_b1 = rules.CollectorControl(rules.CollectorBuffer.B1,
                                        rules.CollectorOp.USE)
        report = rules.check_ws_known_facts(replace(
            facts, collector=use_b1, collector_b_valid=(True, None, None, None)))
        self.assertIn("collector_b_valid", report.obligations)
        self.assertIn("collector_sequence", report.obligations)
        self.assertIn("collector_b_valid", rules.check_ws_known_facts(replace(
            facts, collector=use_b1,
            collector_b_valid=(True, False, None, None))).violations)
        self.assertNotIn("collector_b_valid", rules.check_ws_known_facts(replace(
            facts, collector=use_b1,
            collector_b_valid=(False, True, None, None))).violations)
        self.assertIn("collector_domain", rules.check_ws_known_facts(replace(
            facts, collector=rules.CollectorControl(
                rules.CollectorBuffer.A, rules.CollectorOp.FILL))).violations)
        absent = rules.check_ws_known_facts(facts)
        self.assertEqual(absent.effective_collector, rules.CollectorControl(
            rules.CollectorBuffer.B0, rules.CollectorOp.DISCARD))
        self.assertEqual(facts.collector, rules.CollectorControl())
        self.assertIn("b_low_bit_packing_rule", rules.check_ws_known_facts(
            replace(facts, kind="F8F6F4", word_kind="F8F6F4", k=32,
                    d_type="F16", a_type="E4M3", b_type="E2M1")).obligations)


if __name__ == "__main__":
    unittest.main()
