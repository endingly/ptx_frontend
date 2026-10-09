"""Typed non-WS activation-collector and ashift source/known-fact rules."""

from dataclasses import replace
import unittest

from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.normalize.tcgen_mma import validate_tcgen_mma_variant
from ptx_frontend.spec.resources import packaged_spec_dir
from ptx_frontend.spec import tcgen_mma_operations as rules


class TcgenMmaACollectorContractTests(unittest.TestCase):
    """Preserve all ordinary identities while adding typed optional controls."""

    def test_existing_dense_sparse_forms_share_ordered_controls(self) -> None:
        """Eight identities retain old layouts and one optional qualifier pair."""

        raw = load_yaml(packaged_spec_dir() / "tensor_memory_data_movement.yaml")
        forms = {v.name: v for v in normalize_instruction_spec(raw)[0].variants}
        for sparse in (False, True):
            for kind, layouts in (("f16", 8), ("tf32", 8),
                                  ("i8", 4), ("f8f6f4", 4)):
                name = f"tcgen05_mma_{'sp_' if sparse else ''}{kind}"
                form = forms[name]
                with self.subTest(name=name):
                    self.assertEqual(len(form.operand_layouts), layouts)
                    self.assertEqual(tuple(m.name for m in form.modifiers[-2:]),
                                     ("ashift", "collector"))
                    self.assertEqual(form.modifiers[-2].default, False)
                    self.assertEqual(form.modifiers[-1].default, "absent")
                    self.assertEqual(tuple(v.value for v in
                                           form.modifiers[-1].values),
                                     tuple(f"collector::a::{op}" for op in
                                           ("fill", "use", "lastuse", "discard")))
                    with self.assertRaises(ValueError):
                        validate_tcgen_mma_variant(replace(
                            form, modifiers=form.modifiers[:-2] +
                            tuple(reversed(form.modifiers[-2:]))))
                    with self.assertRaises(ValueError):
                        validate_tcgen_mma_variant(replace(
                            form, modifiers=form.modifiers[:-1]))

    def test_ashift_and_history_remain_conditional(self) -> None:
        """Known M and independent history assertions never prove execution."""

        absent = rules.ACollectorKnownFacts()
        report = rules.check_a_collector_known_facts(absent)
        self.assertTrue(report.supplied_facts_ok)
        self.assertEqual(report.effective,
                         rules.CollectorControl(rules.CollectorBuffer.A,
                                                rules.CollectorOp.DISCARD))
        self.assertEqual(absent.collector, rules.CollectorControl())
        control = rules.CollectorControl(rules.CollectorBuffer.A,
                                         rules.CollectorOp.LAST_USE)
        facts = rules.ACollectorKnownFacts(control, True, True, 128, None)
        report = rules.check_a_collector_known_facts(facts)
        self.assertTrue(report.supplied_facts_ok)
        self.assertIn("collector_a_valid", report.obligations)
        self.assertIn("collector_sequence", report.obligations)
        self.assertIn("collector_a_valid", rules.check_a_collector_known_facts(
            replace(facts, collector_a_valid=False)).violations)
        self.assertIn("collector_sequence", rules.check_a_collector_known_facts(
            replace(facts, collector_a_valid=True)).obligations)
        self.assertIn("ashift_m", rules.check_a_collector_known_facts(
            replace(facts, m=64)).violations)
        self.assertIn("ashift_m", rules.check_a_collector_known_facts(
            replace(facts, m=None)).obligations)
        self.assertIn("ashift_a_placement", rules.check_a_collector_known_facts(
            replace(facts, a_in_tmem=False)).violations)
        self.assertIn("a_placement", rules.check_a_collector_known_facts(
            replace(facts, a_in_tmem=None)).obligations)
        for op in (rules.CollectorOp.FILL, rules.CollectorOp.USE):
            self.assertIn("ashift_collector",
                          rules.check_a_collector_known_facts(replace(
                              facts, collector=replace(control,
                                                     operation=op))).violations)
        explicit_discard = rules.CollectorControl(
            rules.CollectorBuffer.A, rules.CollectorOp.DISCARD)
        self.assertTrue(explicit_discard.present)
        self.assertNotEqual(explicit_discard, absent.collector)
        for malformed in (
                rules.CollectorControl(rules.CollectorBuffer.A),
                rules.CollectorControl(rules.CollectorBuffer.B0,
                                       rules.CollectorOp.FILL),
                rules.CollectorControl("a", rules.CollectorOp.FILL)):
            self.assertIn("collector_domain",
                          rules.check_a_collector_known_facts(replace(
                              facts, collector=malformed)).violations)
        self.assertIn("invalid_context",
                      rules.check_a_collector_known_facts(replace(
                          facts, collector_a_valid=0)).violations)


if __name__ == "__main__":
    unittest.main()
