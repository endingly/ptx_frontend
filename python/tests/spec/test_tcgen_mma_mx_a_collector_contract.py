"""Typed MX activation collector source and independent history contracts."""

from dataclasses import replace
import unittest

from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.normalize.tcgen_mma import validate_tcgen_mma_variant
from ptx_frontend.spec.resources import packaged_spec_dir
from ptx_frontend.spec import tcgen_mma_operations as rules


class TcgenMmaMxACollectorContractTests(unittest.TestCase):
    """Keep the six existing MX identities and their scale contracts intact."""

    def test_source_forms_and_order(self) -> None:
        """Collector follows the selector without adding layouts or ashift."""

        raw = load_yaml(packaged_spec_dir() / "tensor_memory_data_movement.yaml")
        forms = {v.name: v for v in normalize_instruction_spec(raw)[0].variants}
        for sparse in (False, True):
            for kind in ("mxf8f6f4", "mxf4", "mxf4nvf4"):
                name = f"tcgen05_mma_{'sp_' if sparse else ''}{kind}"
                form = forms[name]
                with self.subTest(name=name):
                    self.assertEqual(len(form.operand_layouts), 2)
                    self.assertEqual(tuple(m.name for m in form.modifiers[-2:]),
                                     ("scale_vector_size", "collector"))
                    self.assertFalse(any(m.name == "ashift" for m in form.modifiers))
                    collector = form.modifiers[-1]
                    self.assertEqual(collector.default, "absent")
                    self.assertEqual(tuple(v.value for v in collector.values),
                                     tuple(f"collector::a::{op}" for op in
                                           ("fill", "use", "lastuse", "discard")))
                    self.assertEqual(form.modifiers[-2].default,
                                     None if kind == "mxf4nvf4" else "absent")
                    with self.assertRaises(ValueError):
                        validate_tcgen_mma_variant(replace(
                            form, modifiers=form.modifiers[:-2] +
                            tuple(reversed(form.modifiers[-2:]))))
                    with self.assertRaises(ValueError):
                        validate_tcgen_mma_variant(replace(
                            form, modifiers=form.modifiers[:-1]))
                    with self.assertRaises(ValueError):
                        validate_tcgen_mma_variant(replace(
                            form, modifiers=form.modifiers +
                            (forms["tcgen05_mma_f16"].modifiers[-2],)))

    def test_mx_history_reuses_a_rule_without_ashift(self) -> None:
        """Unknown or asserted history never proves collector sequencing."""

        absent = rules.MxACollectorKnownFacts()
        report = rules.check_mx_a_collector_known_facts(absent)
        self.assertTrue(report.supplied_facts_ok)
        self.assertEqual(absent.collector, rules.CollectorControl())
        self.assertEqual(report.effective, rules.CollectorControl(
            rules.CollectorBuffer.A, rules.CollectorOp.DISCARD))
        self.assertNotIn("ashift_m", report.obligations)
        control = rules.CollectorControl(rules.CollectorBuffer.A,
                                         rules.CollectorOp.USE)
        facts = rules.MxACollectorKnownFacts(control)
        report = rules.check_mx_a_collector_known_facts(facts)
        self.assertIn("collector_a_valid", report.obligations)
        self.assertIn("collector_sequence", report.obligations)
        self.assertIn("source_stability_until_completion", report.obligations)
        self.assertIn("collector_a_valid",
                      rules.check_mx_a_collector_known_facts(replace(
                          facts, collector_a_valid=False)).violations)
        self.assertIn("collector_sequence",
                      rules.check_mx_a_collector_known_facts(replace(
                          facts, collector_a_valid=True)).obligations)
        self.assertIn("collector_domain",
                      rules.check_mx_a_collector_known_facts(replace(
                          facts, collector=replace(
                              control, buffer=rules.CollectorBuffer.B0))).violations)
        self.assertIn("invalid_context",
                      rules.check_mx_a_collector_known_facts(replace(
                          facts, collector_a_valid=0)).violations)
        self.assertFalse(hasattr(facts, "ashift"))
        self.assertFalse(hasattr(facts, "m"))


if __name__ == "__main__":
    unittest.main()
