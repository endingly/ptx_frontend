"""Candidate regressions for explicit raw Bit16 scalar SETP source policy."""
from dataclasses import asdict, replace
import unittest

import yaml
from jsonschema import Draft202012Validator
from ptx_frontend.spec.resources import packaged_spec_schema

from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.normalize.operands import normalize_operand
from ptx_frontend.spec.model import OperandKind, OperandAccess, OperandRole
from ptx_frontend.ir.resolved_ir import from_instruction_spec


class SetpExplicitBitSourceTests(unittest.TestCase):
    """Keep raw policy, ordinary logical conversion and exact caller closure separate."""

    def test_omission_is_none_and_explicit_invalid_values_are_not_omission(self):
        """Retain raw serialization facts as well as normalized semantic defaults."""
        raw = dict(name="src1", kind="reg_or_imm", role="src1", access="read", type="f16")
        omitted = normalize_operand(raw)
        self.assertIsNone(omitted.integer_immediate_type)
        self.assertIn("integer_immediate_type", asdict(omitted))
        self.assertIsNone(asdict(omitted)["integer_immediate_type"])
        self.assertEqual(normalize_operand(dict(raw, integer_immediate_type="b16")).integer_immediate_type, "b16")
        for invalid in (None, False, True, 16, "unknown", "s16", "u16", "f16", "b8", "b32"):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                normalize_operand(dict(raw, integer_immediate_type=invalid))

    def test_raw_schema_accepts_only_the_engaged_bit_strings(self):
        """Raw token validation remains separate from normalized semantic context."""
        schema = yaml.safe_load(packaged_spec_schema().read_text(encoding="utf-8"))
        validator = Draft202012Validator({"$ref": "#/$defs/operand", "$defs": schema["$defs"]})
        raw = dict(name="src1", kind="reg_or_imm", role="src1", access="read", type="f16")
        self.assertTrue(validator.is_valid(raw))
        self.assertTrue(validator.is_valid(dict(raw, integer_immediate_type="b16")))
        self.assertTrue(validator.is_valid(dict(raw, type="f16x2", integer_immediate_type="b32")))
        for value in (None, False, True, 16, "unknown", "s16", "u16", "f16", "b8"):
            with self.subTest(value=value):
                self.assertFalse(validator.is_valid(dict(raw, integer_immediate_type=value)))

    def test_context_and_original_diagnostic_precedence(self):
        """Engaged policy never replaces a pre-existing earlier operand failure."""
        raw = dict(name="src1", kind="reg_or_imm", role="src1", access="read", type="bf16")
        for delta in ({"kind":"reg"}, {"kind":"imm"}, {"kind":"value_vector"},
                      {"access":"write"}, {"access":"readwrite"}, {"role":"dst"},
                      {"type":"f32"}, {"type":"b16"}, {"type":"f16x2"},
                      {"type":{"expr":"modifier(type)"}}):
            with self.subTest(delta=delta), self.assertRaises((ValueError, TypeError)):
                normalize_operand(dict(raw, **delta, integer_immediate_type="b16"))
        original = dict(raw, kind="not_a_kind")
        with self.assertRaises(ValueError) as old:
            normalize_operand(original)
        with self.assertRaises(ValueError) as extended:
            normalize_operand(dict(original, integer_immediate_type=None))
        self.assertEqual(str(old.exception), str(extended.exception))

    def test_full137_raw_defaults_and_exact_sixteen_engagements(self):
        """Audit every serialized default separately from the sixteen semantic rows."""
        database = get_packaged_spec_database()
        self.assertEqual(len(database.instructions), 137)
        engaged = set()
        for instruction in database.instructions:
            for variant in instruction.variants:
                for layout in variant.operand_layouts:
                    for operand in layout.operands:
                        self.assertIn("integer_immediate_type", asdict(operand))
                        if operand.integer_immediate_type is None:
                            self.assertIsNone(asdict(operand)["integer_immediate_type"])
                            continue
                        engaged.add((instruction.opcode, variant.name, layout.name, operand.name))
                        self.assertEqual(operand.integer_immediate_type, "b32" if "x2" in variant.name else "b16")
                        self.assertIs(operand.kind, OperandKind.REGISTER_OR_IMMEDIATE)
                        self.assertEqual(operand.register_width_policy.value, "same_width")
                        self.assertEqual(operand.type_expression.scalar_type,
                                         ("bf16" if "bf16" in variant.name else "f16") + ("x2" if "x2" in variant.name else ""))
        self.assertEqual(engaged, {("setp", variant, "default", source)
            for variant in ("setp_f16", "setp_f16_boolean", "setp_bf16", "setp_bf16_boolean",
                            "setp_f16x2", "setp_f16x2_boolean", "setp_bf16x2", "setp_bf16x2_boolean")
            for source in ("src1", "src2")})

    def test_derived_boundary_rejects_manually_forged_normalized_context(self):
        """Manual normalized objects do not bypass the typed IR boundary."""
        instruction = next(i for i in get_packaged_spec_database().instructions if i.opcode == "setp")
        index = next(n for n,v in enumerate(instruction.variants) if v.name == "setp_f16")
        variant = instruction.variants[index]
        layout = variant.operand_layouts[0]
        source = layout.operands[1]
        for delta in ({"integer_immediate_type":"b32"}, {"kind":OperandKind.REGISTER},
                      {"access":OperandAccess.WRITE}, {"role":OperandRole.DESTINATION}):
            operands = list(layout.operands)
            operands[1] = replace(source, **delta)
            layouts = (replace(layout, operands=tuple(operands)),)
            variants = list(instruction.variants)
            variants[index] = replace(variant, operand_layouts=layouts)
            with self.subTest(delta=delta), self.assertRaises(ValueError):
                from_instruction_spec(replace(instruction, variants=tuple(variants)))
