"""Stochastic quad data sources use the existing floating ValueVector contract."""
import unittest
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import OperandKind
from ptx_frontend.ir.resolved_ir import from_instruction_spec, ResolvedValueKind

QUAD_FORMATS = {
    "cvt_rs_satfinite_f8x4_f32": ("e4m3x4", "e5m2x4"),
    "cvt_rs_satfinite_f4x4_f32": ("e2m1x4",),
    "cvt_rs_satfinite_f6x4_f32": ("e2m3x4", "e3m2x4"),
}

class CvtQuadSourceTests(unittest.TestCase):
    """Pin three public ValueVector fields without granting random-bit constants."""

    def test_exact_quad_fields_and_independent_random_bits(self) -> None:
        """Preserve lane format, arity, width, and all five output-format domains."""
        cvt = next(i for i in get_packaged_spec_database().instructions if i.opcode == "cvt")
        resolved = from_instruction_spec(cvt)
        branches = 0
        for name, formats in QUAD_FORMATS.items():
            variant = next(v for v in cvt.variants if v.name == name)
            emitted = next(v for v in resolved.variants if v.variant_id == name)
            layout = variant.operand_layouts[0]
            self.assertEqual(layout.name, "default")
            self.assertEqual([o.name for o in layout.operands], ["dst", "src", "rbits"])
            dst, src, random = layout.operands
            self.assertIs(dst.kind, OperandKind.REGISTER)
            self.assertIs(src.kind, OperandKind.VALUE_VECTOR)
            self.assertTrue(src.vector_allow_named)
            self.assertFalse(src.vector_allow_sink)
            self.assertEqual(src.vector_arities, (4,))
            self.assertEqual(src.vector_type_policy.value, "element")
            self.assertEqual(src.type_expression.scalar_type, "f32")
            self.assertEqual(src.register_width_policy.value, "equal_or_wider")
            self.assertEqual(src.access.value, "read")
            self.assertIs(emitted.operand_layouts[0].fields[1].value_kind,
                          ResolvedValueKind.VALUE_VECTOR)
            dtype = next(m for m in variant.modifiers if m.name == "dst_type")
            self.assertEqual(tuple(v.value for v in dtype.values) if dtype.values else (dtype.value,), formats)
            self.assertIs(random.kind, OperandKind.CVT_REGISTER_SOURCE)
            self.assertEqual(random.type_expression.scalar_type, "b32")
            self.assertEqual(random.register_width_policy.value, "equal_or_wider")
            branches += 4 * len(formats)
        self.assertEqual(branches, 20)

if __name__ == "__main__":
    unittest.main()
