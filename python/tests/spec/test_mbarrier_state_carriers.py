"""Exact mbarrier role matrix; report B8 is a retained, qualified model contract."""
from collections import Counter
import unittest

from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import OperandKind

# Independent adopted role keys, including the unchanged mandatory-sink partition.
ROLE_KEYS = {
    ('mbarrier_arrive_generic_or_shared', 'no_count', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_generic_or_shared', 'with_count', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_shared_cta', 'no_count', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_shared_cta', 'with_count', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_shared_cluster', 'no_count', 'state'): 'arrival_sink_only',
    ('mbarrier_arrive_shared_cluster', 'with_count', 'state'): 'arrival_sink_only',
    ('mbarrier_arrive_semantics_generic_or_shared', 'no_count', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_semantics_generic_or_shared', 'with_count', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_semantics_shared_cta', 'no_count', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_semantics_shared_cta', 'with_count', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_semantics_shared_cluster', 'no_count', 'state'): 'arrival_sink_only',
    ('mbarrier_arrive_semantics_shared_cluster', 'with_count', 'state'): 'arrival_sink_only',
    ('mbarrier_arrive_expect_tx_generic_or_shared', 'default', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_expect_tx_shared_cta', 'default', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_expect_tx_shared_cluster', 'default', 'state'): 'arrival_sink_only',
    ('mbarrier_arrive_expect_tx_semantics_generic_or_shared', 'default', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_expect_tx_semantics_shared_cta', 'default', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_expect_tx_semantics_shared_cluster', 'default', 'state'): 'arrival_sink_only',
    ('mbarrier_arrive_no_complete_generic_or_shared', 'default', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_no_complete_shared_cta', 'default', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_no_complete_release_cta_generic_or_shared', 'default', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_no_complete_release_cta_shared_cta', 'default', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_drop_generic_or_shared', 'no_count', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_drop_generic_or_shared', 'with_count', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_drop_shared_cta', 'no_count', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_drop_shared_cta', 'with_count', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_drop_shared_cluster', 'no_count', 'state'): 'arrival_sink_only',
    ('mbarrier_arrive_drop_shared_cluster', 'with_count', 'state'): 'arrival_sink_only',
    ('mbarrier_arrive_drop_semantics_generic_or_shared', 'no_count', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_drop_semantics_generic_or_shared', 'with_count', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_drop_semantics_shared_cta', 'no_count', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_drop_semantics_shared_cta', 'with_count', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_drop_semantics_shared_cluster', 'no_count', 'state'): 'arrival_sink_only',
    ('mbarrier_arrive_drop_semantics_shared_cluster', 'with_count', 'state'): 'arrival_sink_only',
    ('mbarrier_arrive_drop_expect_tx_generic_or_shared', 'default', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_drop_expect_tx_shared_cta', 'default', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_drop_expect_tx_shared_cluster', 'default', 'state'): 'arrival_sink_only',
    ('mbarrier_arrive_drop_expect_tx_semantics_generic_or_shared', 'default', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_drop_expect_tx_semantics_shared_cta', 'default', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_drop_expect_tx_semantics_shared_cluster', 'default', 'state'): 'arrival_sink_only',
    ('mbarrier_arrive_drop_no_complete_generic_or_shared', 'default', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_drop_no_complete_shared_cta', 'default', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_drop_no_complete_release_cta_generic_or_shared', 'default', 'state'): 'arrival_register_or_sink',
    ('mbarrier_arrive_drop_no_complete_release_cta_shared_cta', 'default', 'state'): 'arrival_register_or_sink',
    ('mbarrier_test_wait_token_generic_or_shared', 'default', 'state'): 'unsigned_wait_input',
    ('mbarrier_test_wait_token_shared_cta', 'default', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_generic_or_shared', 'no_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_generic_or_shared', 'with_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_shared_cta', 'no_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_shared_cta', 'with_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_test_wait_token_primary_generic_or_shared', 'default', 'state'): 'unsigned_wait_input',
    ('mbarrier_test_wait_token_primary_generic_or_shared', 'report_predicate', 'state'): 'unsigned_wait_input',
    ('mbarrier_test_wait_token_primary_generic_or_shared', 'report_predicate_value', 'report_value'): 'report_value_output',
    ('mbarrier_test_wait_token_primary_generic_or_shared', 'report_predicate_value', 'state'): 'unsigned_wait_input',
    ('mbarrier_test_wait_token_primary_shared_cta', 'default', 'state'): 'unsigned_wait_input',
    ('mbarrier_test_wait_token_primary_shared_cta', 'report_predicate', 'state'): 'unsigned_wait_input',
    ('mbarrier_test_wait_token_primary_shared_cta', 'report_predicate_value', 'report_value'): 'report_value_output',
    ('mbarrier_test_wait_token_primary_shared_cta', 'report_predicate_value', 'state'): 'unsigned_wait_input',
    ('mbarrier_test_wait_parity_primary_generic_or_shared', 'report_predicate_value', 'report_value'): 'report_value_output',
    ('mbarrier_test_wait_parity_primary_shared_cta', 'report_predicate_value', 'report_value'): 'report_value_output',
    ('mbarrier_try_wait_token_primary_generic_or_shared', 'no_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_primary_generic_or_shared', 'with_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_primary_generic_or_shared', 'report_predicate_no_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_primary_generic_or_shared', 'report_predicate_with_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_primary_generic_or_shared', 'report_predicate_value_no_hint', 'report_value'): 'report_value_output',
    ('mbarrier_try_wait_token_primary_generic_or_shared', 'report_predicate_value_no_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_primary_generic_or_shared', 'report_predicate_value_with_hint', 'report_value'): 'report_value_output',
    ('mbarrier_try_wait_token_primary_generic_or_shared', 'report_predicate_value_with_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_primary_shared_cta', 'no_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_primary_shared_cta', 'with_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_primary_shared_cta', 'report_predicate_no_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_primary_shared_cta', 'report_predicate_with_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_primary_shared_cta', 'report_predicate_value_no_hint', 'report_value'): 'report_value_output',
    ('mbarrier_try_wait_token_primary_shared_cta', 'report_predicate_value_no_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_primary_shared_cta', 'report_predicate_value_with_hint', 'report_value'): 'report_value_output',
    ('mbarrier_try_wait_token_primary_shared_cta', 'report_predicate_value_with_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_parity_primary_generic_or_shared', 'report_predicate_value_no_hint', 'report_value'): 'report_value_output',
    ('mbarrier_try_wait_parity_primary_generic_or_shared', 'report_predicate_value_with_hint', 'report_value'): 'report_value_output',
    ('mbarrier_try_wait_parity_primary_shared_cta', 'report_predicate_value_no_hint', 'report_value'): 'report_value_output',
    ('mbarrier_try_wait_parity_primary_shared_cta', 'report_predicate_value_with_hint', 'report_value'): 'report_value_output',
    ('mbarrier_test_wait_token_semantics_generic_or_shared', 'default', 'state'): 'unsigned_wait_input',
    ('mbarrier_test_wait_token_semantics_shared_cta', 'default', 'state'): 'unsigned_wait_input',
    ('mbarrier_test_wait_token_primary_semantics_generic_or_shared', 'default', 'state'): 'unsigned_wait_input',
    ('mbarrier_test_wait_token_primary_semantics_generic_or_shared', 'report_predicate', 'state'): 'unsigned_wait_input',
    ('mbarrier_test_wait_token_primary_semantics_generic_or_shared', 'report_predicate_value', 'report_value'): 'report_value_output',
    ('mbarrier_test_wait_token_primary_semantics_generic_or_shared', 'report_predicate_value', 'state'): 'unsigned_wait_input',
    ('mbarrier_test_wait_token_primary_semantics_shared_cta', 'default', 'state'): 'unsigned_wait_input',
    ('mbarrier_test_wait_token_primary_semantics_shared_cta', 'report_predicate', 'state'): 'unsigned_wait_input',
    ('mbarrier_test_wait_token_primary_semantics_shared_cta', 'report_predicate_value', 'report_value'): 'report_value_output',
    ('mbarrier_test_wait_token_primary_semantics_shared_cta', 'report_predicate_value', 'state'): 'unsigned_wait_input',
    ('mbarrier_test_wait_parity_primary_semantics_generic_or_shared', 'report_predicate_value', 'report_value'): 'report_value_output',
    ('mbarrier_test_wait_parity_primary_semantics_shared_cta', 'report_predicate_value', 'report_value'): 'report_value_output',
    ('mbarrier_try_wait_token_semantics_generic_or_shared', 'no_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_semantics_generic_or_shared', 'with_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_semantics_shared_cta', 'no_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_semantics_shared_cta', 'with_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_primary_semantics_generic_or_shared', 'no_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_primary_semantics_generic_or_shared', 'with_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_primary_semantics_generic_or_shared', 'report_predicate_no_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_primary_semantics_generic_or_shared', 'report_predicate_with_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_primary_semantics_generic_or_shared', 'report_predicate_value_no_hint', 'report_value'): 'report_value_output',
    ('mbarrier_try_wait_token_primary_semantics_generic_or_shared', 'report_predicate_value_no_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_primary_semantics_generic_or_shared', 'report_predicate_value_with_hint', 'report_value'): 'report_value_output',
    ('mbarrier_try_wait_token_primary_semantics_generic_or_shared', 'report_predicate_value_with_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_primary_semantics_shared_cta', 'no_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_primary_semantics_shared_cta', 'with_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_primary_semantics_shared_cta', 'report_predicate_no_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_primary_semantics_shared_cta', 'report_predicate_with_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_primary_semantics_shared_cta', 'report_predicate_value_no_hint', 'report_value'): 'report_value_output',
    ('mbarrier_try_wait_token_primary_semantics_shared_cta', 'report_predicate_value_no_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_token_primary_semantics_shared_cta', 'report_predicate_value_with_hint', 'report_value'): 'report_value_output',
    ('mbarrier_try_wait_token_primary_semantics_shared_cta', 'report_predicate_value_with_hint', 'state'): 'unsigned_wait_input',
    ('mbarrier_try_wait_parity_primary_semantics_generic_or_shared', 'report_predicate_value_no_hint', 'report_value'): 'report_value_output',
    ('mbarrier_try_wait_parity_primary_semantics_generic_or_shared', 'report_predicate_value_with_hint', 'report_value'): 'report_value_output',
    ('mbarrier_try_wait_parity_primary_semantics_shared_cta', 'report_predicate_value_no_hint', 'report_value'): 'report_value_output',
    ('mbarrier_try_wait_parity_primary_semantics_shared_cta', 'report_predicate_value_with_hint', 'report_value'): 'report_value_output',
    ('mbarrier_pending_count', 'default', 'state'): 'pending_count_opaque_input',
}

ROLE_TYPES = {
    "arrival_register_or_sink": ("b64", "same_width", "register_or_sink"),
    "arrival_sink_only": ("b64", "exact", "sink"),
    "unsigned_wait_input": ("u64", "same_width", "register"),
    "pending_count_opaque_input": ("b64", "same_width", "register"),
    "report_value_output": ("b8", "same_width", None),
}


class MbarrierStateCarrierTests(unittest.TestCase):
    """Preserve written B64 separately from each role's semantic operand type."""

    def test_complete_role_key_matrix(self) -> None:
        """Check all117 bindings without interpreting report B8 as normative proof."""
        instruction = next(i for i in get_packaged_spec_database().instructions
                           if i.opcode == "mbarrier")
        actual = {(v.name, l.name, o.name): (v, o)
                  for v in instruction.variants for l in v.operand_layouts
                  for o in l.operands
                  if o.kind is OperandKind.MBARRIER_STATE_TOKEN or o.name == "report_value"}
        self.assertEqual(set(actual), set(ROLE_KEYS))
        counts = Counter()
        for key, role in ROLE_KEYS.items():
            with self.subTest(key=key, role=role):
                variant, operand = actual[key]
                logical_type, width, form = ROLE_TYPES[role]
                self.assertEqual(operand.type_expression.scalar_type, logical_type)
                self.assertEqual(operand.register_width_policy.value, width)
                self.assertEqual(operand.access.value,
                                 "read" if role in {"unsigned_wait_input", "pending_count_opaque_input"} else "write")
                if form is not None:
                    self.assertIs(operand.kind, OperandKind.MBARRIER_STATE_TOKEN)
                    self.assertEqual(operand.mbarrier_state_token_form.value, form)
                else:
                    self.assertIs(operand.kind, OperandKind.REGISTER)
                written = next(m for m in variant.modifiers if m.name == "type")
                self.assertEqual(written.value, "b64")
                counts[role] += 1
        self.assertEqual(counts, {"arrival_register_or_sink":32, "arrival_sink_only":12,
                                  "unsigned_wait_input":48, "report_value_output":24,
                                  "pending_count_opaque_input":1})
