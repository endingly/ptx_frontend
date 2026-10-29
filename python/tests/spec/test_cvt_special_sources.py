"""Bound the conversion-only special-register source contract."""

import unittest

from ptx_frontend.ir.resolved_ir import from_instruction_spec, ResolvedValueKind, ResolvedOperandShape
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import OperandKind


class CvtSpecialSourceTests(unittest.TestCase):
    """Preserve source shape, contextual metadata and unrelated opcode boundaries."""

    def test_conversion_sources_use_dedicated_carriers(self) -> None:
        """Every scalar read uses the narrow carrier and keeps immediate admission."""
        database = get_packaged_spec_database()
        cvt = next(item for item in database.instructions if item.opcode == "cvt")
        resolved = from_instruction_spec(cvt)
        for variant, lowered in zip(cvt.variants, resolved.variants, strict=True):
            if variant.name.startswith("cvt_pack_"):
                continue
            for layout, lowered_layout in zip(variant.operand_layouts, lowered.operand_layouts, strict=True):
                for operand, field in zip(layout.operands, lowered_layout.fields, strict=True):
                    if operand.access.value != "read" or operand.kind is OperandKind.REGISTER_VECTOR:
                        continue
                    self.assertIn(operand.kind, {OperandKind.CVT_SCALAR_SOURCE, OperandKind.CVT_REGISTER_SOURCE})
                    self.assertIs(field.value_kind, ResolvedValueKind.CVT_SOURCE)
                    binding = next(item for item in lowered_layout.bindings if item.target_field_id == field.name)
                    expected = (ResolvedOperandShape.REGISTER, ResolvedOperandShape.SPECIAL_REGISTER)
                    if operand.kind is OperandKind.CVT_SCALAR_SOURCE:
                        expected = (ResolvedOperandShape.REGISTER, ResolvedOperandShape.IMMEDIATE, ResolvedOperandShape.SPECIAL_REGISTER)
                    self.assertEqual(binding.allowed_shapes, expected)
            self.assertTrue(variant.operand_type_compatibilities)

    def test_other_opcodes_and_cvt_pack_keep_existing_source_domains(self) -> None:
        """The new source kinds never widen ordinary register/immediate contracts."""
        for instruction in get_packaged_spec_database().instructions:
            for variant in instruction.variants:
                if instruction.opcode == "cvt" and not variant.name.startswith("cvt_pack_"):
                    continue
                for layout in variant.operand_layouts:
                    self.assertTrue(all(operand.kind not in {OperandKind.CVT_SCALAR_SOURCE, OperandKind.CVT_REGISTER_SOURCE} for operand in layout.operands))


if __name__ == "__main__":
    unittest.main()
