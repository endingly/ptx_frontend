"""Finite generic-address register width across canonical and generated ISSPACEP forms."""

import unittest

from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.code_gen.emit.resolved_descriptors import _emit_resolved_descriptor_storage
from ptx_frontend.ir.resolved_ir import (
    ResolvedOperandTypeExpressionKind, ResolvedRegisterWidthPolicy,
    ResolvedValueKind, from_instruction_spec,
)
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import OperandKind, OperandRegisterWidthPolicy
from ptx_frontend.spec.resources import packaged_backend_spec


class IsspacepAddressWidthTests(unittest.TestCase):
    """All eight state-space variants share a finite source role and pred destination."""

    def test_canonical_ir_and_rendered_binding_keep_the_same_width_policy(self) -> None:
        """No opcode size suffix or global data-width rule is needed for admission."""
        spec = next(item for item in get_packaged_spec_database().instructions if item.opcode == "isspacep")
        self.assertEqual(len(spec.variants), 8)
        for variant in spec.variants:
            source = next(operand for operand in variant.operand_layouts[0].operands if operand.name == "src")
            destination = next(operand for operand in variant.operand_layouts[0].operands if operand.name == "dst")
            self.assertIs(source.kind, OperandKind.REGISTER)
            self.assertIs(source.register_width_policy, OperandRegisterWidthPolicy.WORD_OR_DOUBLEWORD)
            self.assertEqual(source.type_expression.scalar_type, "u64")
            self.assertIs(destination.kind, OperandKind.PREDICATE)
        resolved = from_instruction_spec(spec)
        self.assertEqual(len(resolved.variants), 8)
        for variant in resolved.variants:
            for layout in variant.operand_layouts:
                binding = next(item for item in layout.bindings if item.target_field_id == "src")
                self.assertIs(binding.register_width_policy, ResolvedRegisterWidthPolicy.WORD_OR_DOUBLEWORD)
                self.assertIs(binding.type_expression.kind, ResolvedOperandTypeExpressionKind.FIXED_SCALAR)
                self.assertEqual(binding.type_expression.scalar_type, "u64")
                self.assertIs(next(field for field in layout.fields if field.name == "src").value_kind,
                              ResolvedValueKind.REGISTER)
        rendered = _emit_resolved_descriptor_storage(resolved, load_cpp_backend(packaged_backend_spec()))
        self.assertIn("base::ScalarTypeSizePolicy::WordOrDoubleword", rendered)
        self.assertNotIn("base::ScalarTypeSizePolicy::EqualOrWider", rendered)
        self.assertIn("ScalarType::U64", rendered)
