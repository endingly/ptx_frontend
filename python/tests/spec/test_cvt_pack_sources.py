"""Ordinary S32 pack sources reuse narrowing without changing carry or quad roles."""
from pathlib import Path
import unittest
import yaml

from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import OperandKind
from ptx_frontend.ir.resolved_ir import from_instruction_spec, ResolvedValueKind, ResolvedOperandShape

PACK_FORMATS = {
    "cvt_pack_sat_u8_s32_b32": ("u8",),
    "cvt_pack_sat_16_s32": ("u16", "s16"),
    "cvt_pack_sat_small_s32_b32": ("s8", "u4", "s4", "u2", "s2"),
}

class CvtPackSourceTests(unittest.TestCase):
    """Pin six public field migrations and sixteen destination/source-role cells."""

    def test_exact_scalar_source_fields_and_domains(self) -> None:
        """Keep nominal S32/Narrow, same-width registers and independent B32 carry."""
        cvt = next(i for i in get_packaged_spec_database().instructions if i.opcode == "cvt")
        resolved = from_instruction_spec(cvt)
        changed = branches = 0
        for name, formats in PACK_FORMATS.items():
            variant = next(v for v in cvt.variants if v.name == name)
            lowered = next(v for v in resolved.variants if v.variant_id == name)
            layout = variant.operand_layouts[0]
            emitted = lowered.operand_layouts[0]
            self.assertEqual(layout.name, "default")
            self.assertEqual([o.name for o in layout.operands],
                             ["dst", "src1", "src2"] + ([] if name == "cvt_pack_sat_16_s32" else ["carry"]))
            self.assertIs(layout.operands[0].kind, OperandKind.REGISTER)
            dtype = next(m for m in variant.modifiers if m.name == "dst_type")
            self.assertEqual(tuple(value.value for value in dtype.values) if dtype.values else (dtype.value,), formats)
            for index in (1, 2):
                operand = layout.operands[index]
                self.assertIs(operand.kind, OperandKind.REGISTER_OR_IMMEDIATE)
                self.assertEqual(operand.type_expression.scalar_type, "s32")
                self.assertEqual(operand.register_width_policy.value, "same_width")
                self.assertEqual(operand.immediate_conversion_policy.value, "narrow")
                self.assertEqual(operand.role.value, f"src{index}")
                self.assertEqual(operand.access.value, "read")
                self.assertIs(emitted.fields[index].value_kind, ResolvedValueKind.REG_OR_IMM)
                self.assertEqual(emitted.bindings[index].allowed_shapes,
                                 (ResolvedOperandShape.REGISTER, ResolvedOperandShape.IMMEDIATE))
                changed += 1
                branches += len(formats)
            if len(layout.operands) == 4:
                carry = layout.operands[3]
                self.assertIs(carry.kind, OperandKind.REGISTER_OR_IMMEDIATE)
                self.assertEqual(carry.type_expression.scalar_type, "b32")
                self.assertEqual(carry.register_width_policy.value, "same_width")
        self.assertEqual((changed, branches), (6, 16))

    def test_quad_and_random_bits_remain_outside_scalar_admission(self) -> None:
        """Source constants in scalar pack do not grant stochastic random-bit literals."""
        cvt = next(i for i in get_packaged_spec_database().instructions if i.opcode == "cvt")
        for name in ("cvt_rs_satfinite_f8x4_f32", "cvt_rs_satfinite_f4x4_f32", "cvt_rs_satfinite_f6x4_f32"):
            variant = next(v for v in cvt.variants if v.name == name)
            source, random = variant.operand_layouts[0].operands[1:]
            self.assertIs(source.kind, OperandKind.REGISTER_VECTOR)
            self.assertIs(random.kind, OperandKind.CVT_REGISTER_SOURCE)
            self.assertEqual(random.register_width_policy.value, "equal_or_wider")
            self.assertEqual(random.type_expression.scalar_type, "b32")

    def test_pack_description_does_not_claim_register_only_sources(self) -> None:
        """Description is source documentation, absent from the normalized semantic model."""
        path = Path(__file__).resolve().parents[3] / "python/src/ptx_frontend/spec/resources/ptx_spec/data_movement_and_conversion.yaml"
        data = yaml.safe_load(path.read_text())
        variant = next(v for i in data["instructions"] for v in i["variants"]
                       if v["name"] == "cvt_pack_sat_u8_s32_b32")
        self.assertEqual(variant["description"], "Saturating packed u8 conversion from s32 sources.")

if __name__ == "__main__":
    unittest.main()
