"""Finite scalar-half source admission, with exact alternate scalar caller closure."""
from collections import Counter
import unittest

from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.ir.resolved_ir import from_instruction_spec, ResolvedValueKind

# The original comparator separates scalar promotions from existing vector carriers.
ROLE_ROWS = ((('add', 'add_half', 'default', 'src1'), 'reg', 'register_only_preserved', False),
 (('add', 'add_half', 'default', 'src2'), 'reg', 'register_only_preserved', False),
 (('add', 'add_mixed_f32', 'default', 'src'),
  'reg_or_imm',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  True),
 (('sub', 'sub_half', 'default', 'src1'), 'reg', 'register_only_preserved', False),
 (('sub', 'sub_half', 'default', 'src2'), 'reg', 'register_only_preserved', False),
 (('sub', 'sub_mixed_f32', 'default', 'src'),
  'reg_or_imm',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  True),
 (('mul', 'mul_half', 'default', 'src1'), 'reg', 'register_only_preserved', False),
 (('mul', 'mul_half', 'default', 'src2'), 'reg', 'register_only_preserved', False),
 (('fma', 'fma_rn_f16', 'default', 'src1'), 'reg', 'register_only_preserved', False),
 (('fma', 'fma_rn_f16', 'default', 'src2'), 'reg', 'register_only_preserved', False),
 (('fma', 'fma_rn_f16', 'default', 'src3'), 'reg', 'register_only_preserved', False),
 (('fma', 'fma_half_relu', 'default', 'src1'), 'reg', 'register_only_preserved', False),
 (('fma', 'fma_half_relu', 'default', 'src2'), 'reg', 'register_only_preserved', False),
 (('fma', 'fma_half_relu', 'default', 'src3'), 'reg', 'register_only_preserved', False),
 (('fma', 'fma_half_oob', 'default', 'src1'), 'reg', 'register_only_preserved', False),
 (('fma', 'fma_half_oob', 'default', 'src2'), 'reg', 'register_only_preserved', False),
 (('fma', 'fma_half_oob', 'default', 'src3'), 'reg', 'register_only_preserved', False),
 (('fma', 'fma_half_oob_relu', 'default', 'src1'), 'reg', 'register_only_preserved', False),
 (('fma', 'fma_half_oob_relu', 'default', 'src2'), 'reg', 'register_only_preserved', False),
 (('fma', 'fma_half_oob_relu', 'default', 'src3'), 'reg', 'register_only_preserved', False),
 (('fma', 'fma_mixed_f32_f16', 'default', 'src1'),
  'reg_or_imm',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  True),
 (('fma', 'fma_mixed_f32_f16', 'default', 'src2'),
  'reg_or_imm',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  True),
 (('ex2', 'ex2_approx_f16', 'default', 'src'), 'reg', 'register_only_preserved', False),
 (('tanh', 'tanh_approx_f16', 'default', 'src'), 'reg', 'register_only_preserved', False),
 (('min', 'min_f16', 'default', 'src1'), 'reg', 'register_only_preserved', False),
 (('min', 'min_f16', 'default', 'src2'), 'reg', 'register_only_preserved', False),
 (('max', 'max_f16', 'default', 'src1'), 'reg', 'register_only_preserved', False),
 (('max', 'max_f16', 'default', 'src2'), 'reg', 'register_only_preserved', False),
 (('abs', 'abs_f16', 'default', 'src'), 'reg', 'register_only_preserved', False),
 (('neg', 'neg_f16', 'default', 'src'), 'reg', 'register_only_preserved', False),
 (('set', 'set_half_f16_f16', 'default', 'src1'),
  'reg_or_imm',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  True),
 (('set', 'set_half_f16_f16', 'default', 'src2'),
  'reg_or_imm',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  True),
 (('set', 'set_half_f16_f16_boolean', 'default', 'src1'),
  'reg_or_imm',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  True),
 (('set', 'set_half_f16_f16_boolean', 'default', 'src2'),
  'reg_or_imm',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  True),
 (('set', 'set_half_bf16_f16', 'default', 'src1'),
  'reg_or_imm',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  True),
 (('set', 'set_half_bf16_f16', 'default', 'src2'),
  'reg_or_imm',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  True),
 (('set', 'set_half_bf16_f16_boolean', 'default', 'src1'),
  'reg_or_imm',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  True),
 (('set', 'set_half_bf16_f16_boolean', 'default', 'src2'),
  'reg_or_imm',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  True),
 (('set', 'set_half_integer_f16', 'default', 'src1'),
  'reg_or_imm',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  True),
 (('set', 'set_half_integer_f16', 'default', 'src2'),
  'reg_or_imm',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  True),
 (('set', 'set_half_integer_f16_boolean', 'default', 'src1'),
  'reg_or_imm',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  True),
 (('set', 'set_half_integer_f16_boolean', 'default', 'src2'),
  'reg_or_imm',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  True),
 (('setp', 'setp_f16', 'default', 'src1'),
  'reg_or_imm',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  True),
 (('setp', 'setp_f16', 'default', 'src2'),
  'reg_or_imm',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  True),
 (('setp', 'setp_f16_boolean', 'default', 'src1'),
  'reg_or_imm',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  True),
 (('setp', 'setp_f16_boolean', 'default', 'src2'),
  'reg_or_imm',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  True),
 (('cvt', 'cvt_unrounded_s32', 'default', 'src'),
  'cvt_scalar_src',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  False),
 (('cvt', 'cvt_unrounded_base', 'default', 'src'),
  'cvt_scalar_src',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  False),
 (('cvt', 'cvt_required_non_rn_rzi', 'default', 'src'),
  'cvt_scalar_src',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  False),
 (('cvt', 'cvt_required_non_rn_rzi_f16', 'default', 'src'),
  'cvt_scalar_src',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  False),
 (('cvt', 'cvt_required_rzi_non_u32', 'default', 'src'),
  'cvt_scalar_src',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  False),
 (('cvt', 'cvt_required_rzi_u32_non_f32', 'default', 'src'),
  'cvt_scalar_src',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  False),
 (('cvt', 'cvt_required_rn_non_f32', 'default', 'src'),
  'cvt_scalar_src',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  False),
 (('cvt', 'cvt_required_rn_f16_non_f32', 'default', 'src'),
  'cvt_scalar_src',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  False),
 (('cvt', 'cvt_required_rn_f32', 'default', 'src'),
  'cvt_scalar_src',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  False),
 (('cvt', 'cvt_bf16_from_non_f32_base', 'default', 'src'),
  'cvt_scalar_src',
  'eligible_f16_source_subject_to_independent_valid_instruction_tuple',
  False),
 (('multimem', 'multimem_st_float_none_v2_default_weak', 'default', 'src'),
  'value_vector',
  'separate_value_vector_inheritance_accepted_not_a_scalar_role_promotion',
  False),
 (('multimem', 'multimem_st_float_none_v2_default_ordered', 'default', 'src'),
  'value_vector',
  'separate_value_vector_inheritance_accepted_not_a_scalar_role_promotion',
  False),
 (('multimem', 'multimem_red_float_add_v2_default_red', 'default', 'src'),
  'value_vector',
  'separate_value_vector_inheritance_accepted_not_a_scalar_role_promotion',
  False),
 (('multimem', 'multimem_st_float_none_v4_default_weak', 'default', 'src'),
  'value_vector',
  'separate_value_vector_inheritance_accepted_not_a_scalar_role_promotion',
  False),
 (('multimem', 'multimem_st_float_none_v4_default_ordered', 'default', 'src'),
  'value_vector',
  'separate_value_vector_inheritance_accepted_not_a_scalar_role_promotion',
  False),
 (('multimem', 'multimem_red_float_add_v4_default_red', 'default', 'src'),
  'value_vector',
  'separate_value_vector_inheritance_accepted_not_a_scalar_role_promotion',
  False),
 (('multimem', 'multimem_st_float_none_v8_default_weak', 'default', 'src'),
  'value_vector',
  'separate_value_vector_inheritance_accepted_not_a_scalar_role_promotion',
  False),
 (('multimem', 'multimem_st_float_none_v8_default_ordered', 'default', 'src'),
  'value_vector',
  'separate_value_vector_inheritance_accepted_not_a_scalar_role_promotion',
  False),
 (('multimem', 'multimem_red_float_add_v8_default_red', 'default', 'src'),
  'value_vector',
  'separate_value_vector_inheritance_accepted_not_a_scalar_role_promotion',
  False),
 (('atom', 'atom_global_add_noftz_f16', 'no_hint', 'src'),
  'reg_or_imm',
  'scalar_atomic_F16_source_promoted',
  True),
 (('atom', 'atom_global_add_noftz_f16', 'with_policy', 'src'),
  'reg_or_imm',
  'scalar_atomic_F16_source_promoted',
  True),
 (('red', 'red_global_add_noftz_f16', 'no_hint', 'src'),
  'reg_or_imm',
  'scalar_atomic_F16_source_promoted',
  True),
 (('red', 'red_global_add_noftz_f16', 'with_policy', 'src'),
  'reg_or_imm',
  'scalar_atomic_F16_source_promoted',
  True))

class ScalarHalfSourceTests(unittest.TestCase):
    """Pin all original role keys and the generated public field migration."""

    def test_exact_role_and_generated_field_matrix(self) -> None:
        """Preserve69 stable roles; promote24 role/layout keys in13 existing classes."""
        spec = get_packaged_spec_database()
        actual = {(i.opcode, v.name, l.name, o.name): o
                  for i in spec.instructions for v in i.variants
                  for l in v.operand_layouts for o in l.operands}
        generated = {i.opcode: from_instruction_spec(i) for i in spec.instructions
                     if i.opcode in {"add", "sub", "fma", "set", "setp", "atom", "red"}}
        classes = set()
        counts = Counter()
        for key, kind, disposition, promoted in ROLE_ROWS:
            with self.subTest(key=key):
                self.assertEqual(actual[key].kind.value, kind)
                counts[disposition] += 1
                if promoted:
                    variant = next(v for v in generated[key[0]].variants if v.variant_id == key[1])
                    layout = next(l for l in variant.operand_layouts if l.layout_id == key[2])
                    field = next(f for f in layout.fields if f.name == key[3])
                    self.assertIs(field.value_kind, ResolvedValueKind.REG_OR_IMM)
                    classes.add((key[0], variant.cpp_name))
        self.assertEqual(len(ROLE_ROWS), 69)
        self.assertEqual(sum(row[3] for row in ROLE_ROWS), 24)
        self.assertEqual(len(classes), 13)
        self.assertEqual(counts["register_only_preserved"], 26)
        self.assertEqual(sum(key[0] == "cvt" for key, *_ in ROLE_ROWS), 10)
        for opcode in ("add", "sub"):
            variant = next(v for v in generated[opcode].variants if v.variant_id == opcode + "_mixed_f32")
            domain = {d.value for d in variant.modifier_value_domains if d.source_kind_id == "input_type"}
            self.assertEqual(domain, {"f16", "bf16"})

    def test_exact_alternate_scalar_source_closure(self) -> None:
        """Keep original five BF16 fields plus four scalar SETP sources; no scalar FP8 callers."""
        spec = get_packaged_spec_database()
        actual = set()
        for instruction in spec.instructions:
            for variant in instruction.variants:
                domains = {
                    modifier.name: ({modifier.value} if modifier.value is not None
                                    else {item.value for item in modifier.values})
                    for modifier in variant.modifiers
                }
                for layout in variant.operand_layouts:
                    for operand in layout.operands:
                        if operand.kind.value not in {"reg_or_imm", "cvt_scalar_src"}:
                            continue
                        expression = operand.type_expression
                        if expression is None:
                            continue
                        domain = ({expression.scalar_type} if expression.scalar_type
                                  else domains.get(expression.modifier_name, set()))
                        for scalar in domain & {"bf16", "e4m3", "e5m2"}:
                            actual.add((instruction.opcode, variant.name, layout.name,
                                        operand.name, scalar))
        self.assertEqual(actual, {
            ('fma', 'fma_mixed_f32_bf16', 'default', 'src1', 'bf16'),
            ('fma', 'fma_mixed_f32_bf16', 'default', 'src2', 'bf16'),
            ('set', 'set_half_integer_bf16', 'default', 'src1', 'bf16'),
            ('set', 'set_half_integer_bf16', 'default', 'src2', 'bf16'),
            ('set', 'set_half_integer_bf16_boolean', 'default', 'src1', 'bf16'),
            ('set', 'set_half_integer_bf16_boolean', 'default', 'src2', 'bf16'),
            ("setp", "setp_bf16", "default", "src1", "bf16"),
            ("setp", "setp_bf16", "default", "src2", "bf16"),
            ("setp", "setp_bf16_boolean", "default", "src1", "bf16"),
            ("setp", "setp_bf16_boolean", "default", "src2", "bf16"),
            ("add", "add_mixed_f32", "default", "src", "bf16"),
            ("sub", "sub_mixed_f32", "default", "src", "bf16"),
            ("cvt", "cvt_f32_bf16", "default", "src", "bf16"),
            ("cvt", "cvt_f32_bf16_ftz", "default", "src", "bf16"),
            ("cvt", "cvt_non_f32_base_from_bf16", "default", "src", "bf16"),
        })

if __name__ == "__main__":
    unittest.main()
