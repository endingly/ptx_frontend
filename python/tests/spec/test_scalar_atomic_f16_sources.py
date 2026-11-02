"""Pin the four scalar F16 atomic source permissions and unchanged sibling roles."""
import unittest
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import OperandKind
from ptx_frontend.ir.resolved_ir import from_instruction_spec, ResolvedValueKind, ResolvedOperandShape

KEYS = {(opcode, opcode + "_global_add_noftz_f16", layout, "src")
        for opcode in ("atom", "red") for layout in ("no_hint", "with_policy")}

class ScalarAtomicF16SourceTests(unittest.TestCase):
    """The field kind changes; logical F16, width and every policy sibling stay fixed."""
    def test_exact_four_keys_and_existing_two_members(self) -> None:
        """Exercise real normalization and derived member/layout projections."""
        database = get_packaged_spec_database()
        actual = {(i.opcode, v.name, l.name, o.name): o
                  for i in database.instructions for v in i.variants
                  for l in v.operand_layouts for o in l.operands}
        found = {key for key, operand in actual.items()
                 if key[0] in ("atom", "red") and key[3] == "src"
                 and operand.kind is OperandKind.REGISTER_OR_IMMEDIATE
                 and operand.type_expression.scalar_type == "f16"}
        self.assertEqual(found, KEYS)
        members = set()
        for opcode, variant_name, layout_name, operand_name in sorted(KEYS):
            operand = actual[(opcode, variant_name, layout_name, operand_name)]
            self.assertEqual(operand.type_expression.scalar_type, "f16")
            self.assertEqual(operand.register_width_policy.value, "same_width")
            self.assertIsNone(operand.integer_immediate_type)
            self.assertFalse(operand.vector_allow_named)
            instruction = next(i for i in database.instructions if i.opcode == opcode)
            variant = next(v for v in from_instruction_spec(instruction).variants
                           if v.variant_id == variant_name)
            layout = next(l for l in variant.operand_layouts if l.layout_id == layout_name)
            field = next(f for f in layout.fields if f.name == operand_name)
            binding = next(b for b in layout.bindings if b.target_field_id == operand_name)
            self.assertIs(field.value_kind, ResolvedValueKind.REG_OR_IMM)
            self.assertEqual(binding.allowed_shapes,
                             (ResolvedOperandShape.REGISTER, ResolvedOperandShape.IMMEDIATE))
            self.assertIsNone(binding.integer_immediate_type)
            members.add((opcode, variant.cpp_name, field.name))
            if layout_name == "with_policy":
                policy = next(b for b in layout.bindings if b.target_field_id == "cache_policy")
                self.assertEqual(policy.allowed_shapes, (ResolvedOperandShape.REGISTER,))
                self.assertEqual(policy.type_expression.scalar_type, "b64")
        self.assertEqual(len(members), 2)

    def test_packed_bfloat_vector_and_destination_permissions_are_preserved(self) -> None:
        """No permission follows merely from a shared opcode or physical width."""
        database = get_packaged_spec_database()
        for instruction in database.instructions:
            if instruction.opcode not in ("atom", "red"):
                continue
            for variant in instruction.variants:
                if variant.name not in {instruction.opcode + "_global_add_noftz_" + suffix
                                        for suffix in ("f16x2", "bf16", "bf16x2")}:
                    continue
                for layout in variant.operand_layouts:
                    source = next(o for o in layout.operands if o.name == "src")
                    self.assertIs(source.kind, OperandKind.REGISTER)
                    if instruction.opcode == "atom":
                        destination = next(o for o in layout.operands if o.name == "dst")
                        self.assertIs(destination.kind, OperandKind.REGISTER_OR_SINK)

if __name__ == "__main__":
    unittest.main()
