"""Finite raw-word alternatives for the eight packed SETP source bindings."""
from dataclasses import replace
import unittest

from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.normalize.operands import normalize_operand
from ptx_frontend.spec.model import OperandKind
from ptx_frontend.ir.resolved_ir import (
    from_instruction_spec, ResolvedValueKind, ResolvedOperandShape,
)


class PackedSetpSourceTests(unittest.TestCase):
    """Keep logical packed words, integer words and physical carriers distinct."""

    def test_exact_eight_fields_and_existing_scalar_pairs(self):
        """Audit the eight member migrations without widening neighboring SETP rows."""
        database = get_packaged_spec_database()
        instruction = next(i for i in database.instructions if i.opcode == "setp")
        ir = from_instruction_spec(instruction)
        seen = set()
        for variant in instruction.variants:
            packed = variant.name in {
                "setp_f16x2", "setp_f16x2_boolean", "setp_bf16x2", "setp_bf16x2_boolean"}
            if not packed:
                continue
            logical = "bf16x2" if "bf16" in variant.name else "f16x2"
            derived = next(v for v in ir.variants if v.variant_id == variant.name)
            for layout in variant.operand_layouts:
                self.assertEqual(layout.name, "default")
                lowered = next(l for l in derived.operand_layouts if l.layout_id == layout.name)
                for operand in layout.operands[1:3]:
                    self.assertIs(operand.kind, OperandKind.REGISTER_OR_IMMEDIATE)
                    self.assertEqual(operand.integer_immediate_type, "b32")
                    self.assertEqual(operand.type_expression.scalar_type, logical)
                    self.assertEqual(operand.register_width_policy.value, "same_width")
                    field = next(f for f in lowered.fields if f.name == operand.name)
                    binding = next(b for b in lowered.bindings if b.target_field_id == operand.name)
                    self.assertIs(field.value_kind, ResolvedValueKind.REG_OR_IMM)
                    self.assertEqual(set(binding.allowed_shapes), {
                        ResolvedOperandShape.REGISTER, ResolvedOperandShape.IMMEDIATE})
                    self.assertEqual(binding.integer_immediate_type, "b32")
                    self.assertFalse(binding.source_value_vector)
                    seen.add((variant.name, operand.name))
        self.assertEqual(len(seen), 8)
        self.assertEqual(sum(o.integer_immediate_type == "b16"
            for i in database.instructions for v in i.variants
            for l in v.operand_layouts for o in l.operands), 8)

    def test_only_four_logical_raw_pairs_normalize(self):
        """Width and logical-family context constrain the existing optional field."""
        pairs = {("f16", "b16"), ("bf16", "b16"),
                 ("f16x2", "b32"), ("bf16x2", "b32")}
        for logical in ("f16", "bf16", "f16x2", "bf16x2", "f32", "b32", "e4m3x4"):
            for word in ("b16", "b32", "b64", "u32", "f32"):
                raw = dict(name="src1", kind="reg_or_imm", role="src1", access="read",
                           type=logical, integer_immediate_type=word)
                with self.subTest(logical=logical, word=word):
                    if (logical, word) in pairs:
                        self.assertEqual(normalize_operand(raw).integer_immediate_type, word)
                    else:
                        with self.assertRaises(ValueError):
                            normalize_operand(raw)
        valid = dict(name="src1", kind="reg_or_imm", role="src1", access="read",
                     type="f16x2", integer_immediate_type="b32")
        for delta in ({"kind":"reg"}, {"kind":"imm"}, {"kind":"value_vector"},
                      {"access":"write"}, {"access":"readwrite"}, {"role":"dst"}):
            with self.subTest(delta=delta), self.assertRaises((ValueError, TypeError)):
                normalize_operand(dict(valid, **delta))

    def test_manual_normalized_objects_cannot_cross_pair_or_field_boundaries(self):
        """The derived IR validates policy even when normalization was bypassed."""
        instruction = next(i for i in get_packaged_spec_database().instructions if i.opcode == "setp")
        for index, variant in enumerate(instruction.variants):
            if variant.name not in {"setp_f16x2", "setp_f16x2_boolean", "setp_bf16x2", "setp_bf16x2_boolean"}:
                continue
            layout = variant.operand_layouts[0]
            for delta in ({"integer_immediate_type":"b16"},
                          {"integer_immediate_type":"u32"}, {"kind":OperandKind.REGISTER}):
                operands = list(layout.operands)
                operands[1] = replace(operands[1], **delta)
                variants = list(instruction.variants)
                variants[index] = replace(variant, operand_layouts=(replace(layout, operands=tuple(operands)),))
                with self.subTest(variant=variant.name, delta=delta), self.assertRaises(ValueError):
                    from_instruction_spec(replace(instruction, variants=tuple(variants)))
