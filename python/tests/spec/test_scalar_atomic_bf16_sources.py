"""Four exact atomic BF16 source keys retain destinations, policy and memory contracts."""

import unittest
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import OperandKind, OperandRegisterWidthPolicy


class ScalarAtomicBf16SourcesTests(unittest.TestCase):
    """Keep scalar BF16 numeric permission separate from other atomic cohorts."""

    def test_exact_sources_and_unchanged_physical_roles(self) -> None:
        """Pin both layouts independently and retain the existing cache-policy carrier."""
        database = get_packaged_spec_database()
        for opcode in ("atom", "red"):
            instruction = next(item for item in database.instructions if item.opcode == opcode)
            variant = next(item for item in instruction.variants
                           if item.name == f"{opcode}_global_add_noftz_bf16")
            self.assertEqual(variant.availability, {"ptx": "7.8", "sm": 90})
            self.assertEqual(tuple(layout.name for layout in variant.operand_layouts),
                             ("no_hint", "with_policy"))
            for layout in variant.operand_layouts:
                operands = {operand.name: operand for operand in layout.operands}
                source = operands["src"]
                self.assertIs(source.kind, OperandKind.REGISTER_OR_IMMEDIATE)
                self.assertEqual(source.type_expression.scalar_type, "bf16")
                self.assertIs(source.register_width_policy, OperandRegisterWidthPolicy.SAME_WIDTH)
                self.assertIsNone(source.integer_immediate_type)
                if opcode == "atom":
                    self.assertIs(operands["dst"].kind, OperandKind.REGISTER_OR_SINK)
                    self.assertEqual(operands["dst"].type_expression.scalar_type, "b16")
                    self.assertIs(operands["dst"].register_width_policy, OperandRegisterWidthPolicy.EXACT)
                if layout.name == "with_policy":
                    self.assertEqual(layout.availability, {"ptx": "7.4", "sm": 80})
                    self.assertIs(operands["cache_policy"].kind, OperandKind.REGISTER)
                    self.assertEqual(operands["cache_policy"].type_expression.scalar_type, "b64")
                else:
                    self.assertNotIn("cache_policy", operands)
            # Packed raw-word permission is an independent adopted source route.
            for suffix in ("bf16x2",):
                sibling = next(item for item in instruction.variants
                               if item.name == f"{opcode}_global_add_noftz_{suffix}")
                for layout in sibling.operand_layouts:
                    source = next(item for item in layout.operands if item.name == "src")
                    self.assertIs(source.kind, OperandKind.REGISTER_OR_IMMEDIATE)
                    self.assertEqual(source.type_expression.scalar_type, suffix)
                    self.assertIs(source.register_width_policy,
                                  OperandRegisterWidthPolicy.SAME_WIDTH)
                    self.assertIsNone(source.integer_immediate_type)
                    self.assertFalse(source.vector_allow_named)
