"""Three scalar packed multimem source fields retain six existing logical branches."""

import unittest
from pathlib import Path
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import OperandKind, OperandRegisterWidthPolicy, OperandTypeExpressionKind

VARIANTS = {
    "multimem_st_float_packed_none_scalar_default_weak",
    "multimem_st_float_packed_none_scalar_default_ordered",
    "multimem_red_float_packed_add_scalar_default_red",
}


class ScalarMultimemPacked32SourcesTests(unittest.TestCase):
    """Pin exact source keys without adding six metadata edits or vector permissions."""

    def test_exact_three_keys_keep_type_width_layout_and_target(self) -> None:
        """Only src.kind changes; logical type remains selected by modifier(type)."""
        database = get_packaged_spec_database()
        instruction = next(item for item in database.instructions if item.opcode == "multimem")
        variants = {variant.name: variant for variant in instruction.variants}
        for name in VARIANTS:
            variant = variants[name]
            self.assertEqual(variant.availability, {"ptx": "8.1", "sm": 90})
            self.assertEqual(tuple(layout.name for layout in variant.operand_layouts), ("default",))
            layout = variant.operand_layouts[0]
            self.assertFalse(layout.availability)
            operands = {operand.name: operand for operand in layout.operands}
            self.assertEqual(set(operands), {"address", "src"})
            source = operands["src"]
            self.assertIs(source.kind, OperandKind.REGISTER_OR_IMMEDIATE)
            self.assertIs(source.register_width_policy, OperandRegisterWidthPolicy.SAME_WIDTH)
            self.assertIs(source.type_expression.kind, OperandTypeExpressionKind.MODIFIER)
            self.assertEqual(source.type_expression.modifier_name, "type")
            self.assertIsNone(source.integer_immediate_type)
            selected = next(modifier for modifier in variant.modifiers if modifier.name == "type")
            self.assertEqual({value.value for value in selected.values}, {"f16x2", "bf16x2"})

    def test_derived_three_public_members_and_six_branches(self) -> None:
        """Project the same three scalar fields without touching vector field kinds."""
        from ptx_frontend.ir.resolved_ir import (from_instruction_spec, ResolvedValueKind,
            ResolvedOperandShape, ResolvedOperandTypeExpressionKind, ResolvedRegisterWidthPolicy)
        from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
        from ptx_frontend.code_gen.resolved_field_names import field_cpp_type

        backend=load_cpp_backend(Path(__file__).resolve().parents[3] /
            "python/src/ptx_frontend/spec/resources/ptx_cpp_backend_spec/ptx_frontend.yaml")
        instruction=next(i for i in get_packaged_spec_database().instructions if i.opcode=="multimem")
        variants={v.variant_id:v for v in from_instruction_spec(instruction).variants}
        members=set()
        for name in VARIANTS:
            with self.subTest(variant=name):
                variant=variants[name]
                self.assertEqual(len(variant.operand_layouts),1)
                layout=variant.operand_layouts[0]
                source=next(b for b in layout.bindings if b.target_field_id=="src")
                self.assertEqual(source.allowed_shapes,(ResolvedOperandShape.REGISTER,ResolvedOperandShape.IMMEDIATE))
                self.assertIs(source.type_expression.kind,ResolvedOperandTypeExpressionKind.MODIFIER_FIELD)
                self.assertEqual(source.type_expression.modifier_field_id,"type")
                self.assertIs(source.register_width_policy,ResolvedRegisterWidthPolicy.SAME_WIDTH)
                field=next(f for f in layout.fields if f.name=="src")
                self.assertIs(field.value_kind,ResolvedValueKind.REG_OR_IMM)
                self.assertEqual(field_cpp_type(field,backend=backend),"WithLocs<RegOrImm>")
                members.add(variant.cpp_name)
        self.assertEqual(len(members),3)
