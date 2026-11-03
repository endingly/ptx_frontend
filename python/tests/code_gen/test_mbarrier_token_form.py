"""Token-only optional form policy and complete generated descriptor emission."""
from collections import Counter
import unittest

from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.code_gen.render_models.resolved_descriptors import _build_operand_binding
from ptx_frontend.ir.resolved_ir import from_instruction_spec
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import OperandKind
from ptx_frontend.spec.resources import packaged_backend_spec


class MbarrierTokenFormTests(unittest.TestCase):
    """Keep every canonical token explicit while all other policies remain absent."""

    def test_exact_token_roles_and_emission(self) -> None:
        """Check all bindings, including ordinary report and unrelated sink roles."""
        backend = load_cpp_backend(packaged_backend_spec())
        counts = Counter()
        ordinary = 0
        reports = 0
        for spec in get_packaged_spec_database().instructions:
            source = {(variant.name, layout.name, operand.name): operand
                      for variant in spec.variants for layout in variant.operand_layouts
                      for operand in layout.operands}
            for variant in from_instruction_spec(spec).variants:
                for layout in variant.operand_layouts:
                    for binding in layout.bindings:
                        key = (variant.variant_id, layout.layout_id, binding.target_field_id)
                        operand = source[key]
                        emitted = _build_operand_binding(binding, "policy", backend)
                        forms = [member.expression for member in emitted.sink_members
                                 if member.name == "mbarrier_state_token_form"]
                        if operand.kind is OperandKind.MBARRIER_STATE_TOKEN:
                            self.assertEqual(binding.mbarrier_state_token_form,
                                             operand.mbarrier_state_token_form, key)
                            form = operand.mbarrier_state_token_form.value
                            counts[form] += 1
                            self.assertEqual(forms, ["checker::MbarrierStateTokenForm::" +
                                ''.join(part.capitalize() for part in form.split('_'))], key)
                            self.assertEqual(binding.type_expression.scalar_type,
                                             operand.type_expression.scalar_type, key)
                            self.assertEqual(binding.register_width_policy.value,
                                             operand.register_width_policy.value, key)
                        else:
                            ordinary += 1
                            self.assertIsNone(binding.mbarrier_state_token_form, key)
                            self.assertEqual(forms, [], key)
                            if spec.opcode == "mbarrier" and operand.name == "report_value":
                                reports += 1
        self.assertEqual(counts, {"register": 49, "register_or_sink": 32, "sink": 12})
        self.assertEqual(reports, 24)
        self.assertGreater(ordinary, 0)
