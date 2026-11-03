"""Exact atomic ValueVector sources retain physical lane domains and brace-only policy."""

import unittest
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import OperandKind, OperandVectorTypePolicy, OperandRegisterWidthPolicy

EXPECTED = (
    ('atom/atom_vector_add_noftz_f16/no_hint/src', 'f16', ('b16', 'f16', 'u16', 's16'), (2, 4, 8)),
    ('atom/atom_vector_add_noftz_f16/with_policy/src', 'f16', ('b16', 'f16', 'u16', 's16'), (2, 4, 8)),
    ('atom/atom_vector_min_noftz_f16/no_hint/src', 'f16', ('b16', 'f16', 'u16', 's16'), (2, 4, 8)),
    ('atom/atom_vector_min_noftz_f16/with_policy/src', 'f16', ('b16', 'f16', 'u16', 's16'), (2, 4, 8)),
    ('atom/atom_vector_max_noftz_f16/no_hint/src', 'f16', ('b16', 'f16', 'u16', 's16'), (2, 4, 8)),
    ('atom/atom_vector_max_noftz_f16/with_policy/src', 'f16', ('b16', 'f16', 'u16', 's16'), (2, 4, 8)),
    ('atom/atom_vector_add_noftz_bf16/no_hint/src', 'bf16', ('b16',), (2, 4, 8)),
    ('atom/atom_vector_add_noftz_bf16/with_policy/src', 'bf16', ('b16',), (2, 4, 8)),
    ('atom/atom_vector_min_noftz_bf16/no_hint/src', 'bf16', ('b16',), (2, 4, 8)),
    ('atom/atom_vector_min_noftz_bf16/with_policy/src', 'bf16', ('b16',), (2, 4, 8)),
    ('atom/atom_vector_max_noftz_bf16/no_hint/src', 'bf16', ('b16',), (2, 4, 8)),
    ('atom/atom_vector_max_noftz_bf16/with_policy/src', 'bf16', ('b16',), (2, 4, 8)),
    ('atom/atom_vector_add_noftz_f16x2/no_hint/src', 'f16x2', ('b32', 'f16x2'), (2, 4)),
    ('atom/atom_vector_add_noftz_f16x2/with_policy/src', 'f16x2', ('b32', 'f16x2'), (2, 4)),
    ('atom/atom_vector_min_noftz_f16x2/no_hint/src', 'f16x2', ('b32', 'f16x2'), (2, 4)),
    ('atom/atom_vector_min_noftz_f16x2/with_policy/src', 'f16x2', ('b32', 'f16x2'), (2, 4)),
    ('atom/atom_vector_max_noftz_f16x2/no_hint/src', 'f16x2', ('b32', 'f16x2'), (2, 4)),
    ('atom/atom_vector_max_noftz_f16x2/with_policy/src', 'f16x2', ('b32', 'f16x2'), (2, 4)),
    ('atom/atom_vector_add_noftz_bf16x2/no_hint/src', 'bf16x2', ('b32',), (2, 4)),
    ('atom/atom_vector_add_noftz_bf16x2/with_policy/src', 'bf16x2', ('b32',), (2, 4)),
    ('atom/atom_vector_min_noftz_bf16x2/no_hint/src', 'bf16x2', ('b32',), (2, 4)),
    ('atom/atom_vector_min_noftz_bf16x2/with_policy/src', 'bf16x2', ('b32',), (2, 4)),
    ('atom/atom_vector_max_noftz_bf16x2/no_hint/src', 'bf16x2', ('b32',), (2, 4)),
    ('atom/atom_vector_max_noftz_bf16x2/with_policy/src', 'bf16x2', ('b32',), (2, 4)),
    ('atom/atom_vector_add_f32/no_hint/src', 'f32', ('b32', 'f32', 'u32', 's32'), (2, 4)),
    ('atom/atom_vector_add_f32/with_policy/src', 'f32', ('b32', 'f32', 'u32', 's32'), (2, 4)),
    ('red/red_vector_add_noftz_f16/no_hint/src', 'f16', ('b16', 'f16', 'u16', 's16'), (2, 4, 8)),
    ('red/red_vector_add_noftz_f16/with_policy/src', 'f16', ('b16', 'f16', 'u16', 's16'), (2, 4, 8)),
    ('red/red_vector_min_noftz_f16/no_hint/src', 'f16', ('b16', 'f16', 'u16', 's16'), (2, 4, 8)),
    ('red/red_vector_min_noftz_f16/with_policy/src', 'f16', ('b16', 'f16', 'u16', 's16'), (2, 4, 8)),
    ('red/red_vector_max_noftz_f16/no_hint/src', 'f16', ('b16', 'f16', 'u16', 's16'), (2, 4, 8)),
    ('red/red_vector_max_noftz_f16/with_policy/src', 'f16', ('b16', 'f16', 'u16', 's16'), (2, 4, 8)),
    ('red/red_vector_add_noftz_bf16/no_hint/src', 'bf16', ('b16',), (2, 4, 8)),
    ('red/red_vector_add_noftz_bf16/with_policy/src', 'bf16', ('b16',), (2, 4, 8)),
    ('red/red_vector_min_noftz_bf16/no_hint/src', 'bf16', ('b16',), (2, 4, 8)),
    ('red/red_vector_min_noftz_bf16/with_policy/src', 'bf16', ('b16',), (2, 4, 8)),
    ('red/red_vector_max_noftz_bf16/no_hint/src', 'bf16', ('b16',), (2, 4, 8)),
    ('red/red_vector_max_noftz_bf16/with_policy/src', 'bf16', ('b16',), (2, 4, 8)),
    ('red/red_vector_add_noftz_f16x2/no_hint/src', 'f16x2', ('b32', 'f16x2'), (2, 4)),
    ('red/red_vector_add_noftz_f16x2/with_policy/src', 'f16x2', ('b32', 'f16x2'), (2, 4)),
    ('red/red_vector_min_noftz_f16x2/no_hint/src', 'f16x2', ('b32', 'f16x2'), (2, 4)),
    ('red/red_vector_min_noftz_f16x2/with_policy/src', 'f16x2', ('b32', 'f16x2'), (2, 4)),
    ('red/red_vector_max_noftz_f16x2/no_hint/src', 'f16x2', ('b32', 'f16x2'), (2, 4)),
    ('red/red_vector_max_noftz_f16x2/with_policy/src', 'f16x2', ('b32', 'f16x2'), (2, 4)),
    ('red/red_vector_add_noftz_bf16x2/no_hint/src', 'bf16x2', ('b32',), (2, 4)),
    ('red/red_vector_add_noftz_bf16x2/with_policy/src', 'bf16x2', ('b32',), (2, 4)),
    ('red/red_vector_min_noftz_bf16x2/no_hint/src', 'bf16x2', ('b32',), (2, 4)),
    ('red/red_vector_min_noftz_bf16x2/with_policy/src', 'bf16x2', ('b32',), (2, 4)),
    ('red/red_vector_max_noftz_bf16x2/no_hint/src', 'bf16x2', ('b32',), (2, 4)),
    ('red/red_vector_max_noftz_bf16x2/with_policy/src', 'bf16x2', ('b32',), (2, 4)),
    ('red/red_vector_add_f32/no_hint/src', 'f32', ('b32', 'f32', 'u32', 's32'), (2, 4)),
    ('red/red_vector_add_f32/with_policy/src', 'f32', ('b32', 'f32', 'u32', 's32'), (2, 4)),
)


class AtomicVectorSourcesTests(unittest.TestCase):
    """Pin every source/layout key without weakening destination or scalar contracts."""

    def test_exact_fifty_two_source_policies(self) -> None:
        """Logical immediate types remain separate from unchanged physical allowlists."""
        database = get_packaged_spec_database()
        variants = {(instruction.opcode, variant.name): variant
                    for instruction in database.instructions for variant in instruction.variants}
        actual = []
        for key, logical, allowed, arities in EXPECTED:
            opcode, name, layout_name, field = key.split("/")
            variant = variants[opcode, name]
            layout = next(item for item in variant.operand_layouts if item.name == layout_name)
            source = next(item for item in layout.operands if item.name == field)
            self.assertIs(source.kind, OperandKind.VALUE_VECTOR)
            self.assertEqual(source.type_expression.scalar_type, logical)
            self.assertIs(source.register_width_policy, OperandRegisterWidthPolicy.SAME_WIDTH)
            self.assertIs(source.vector_type_policy, OperandVectorTypePolicy.ELEMENT)
            self.assertEqual(source.vector_allowed_register_types, allowed)
            self.assertTrue(source.vector_require_uniform_register_family)
            self.assertFalse(source.vector_allow_named)
            self.assertFalse(source.vector_allow_sink)
            self.assertIsNone(source.integer_immediate_type)
            control = next(item for item in variant.modifiers if item.name == "vector")
            self.assertEqual(tuple(int(item.value[1:]) for item in control.values), arities)
            self.assertEqual(variant.availability, {"ptx": "8.1", "sm": 90})
            if opcode == "atom":
                self.assertIs(layout.operands[0].kind, OperandKind.REGISTER_VECTOR)
            if layout_name == "with_policy":
                policy = layout.operands[-1]
                self.assertEqual(policy.name, "cache_policy")
                self.assertIs(policy.kind, OperandKind.REGISTER)
                self.assertEqual(policy.type_expression.scalar_type, "b64")
            actual.append(key)
        self.assertEqual(len(actual), 52)
        self.assertEqual(len({key.split("/")[1] for key in actual}), 26)
