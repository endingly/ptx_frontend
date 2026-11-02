"""Exact multimem value-source carrier and public lowering invariants."""
from pathlib import Path
import tempfile
import unittest

from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import OperandKind
from ptx_frontend.ir.resolved_ir import ResolvedValueKind, from_instruction_spec
from ptx_frontend.code_gen.context import build_generation_context
from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.code_gen.emit.resolved_model import generate_resolved_opcode_header
from ptx_frontend.spec.resources import packaged_backend_spec


# These independent expected domains preserve every selected type, including
# packed16 carriers which have no adopted written literal producer.
EXPECTED_TYPES = {'multimem_st_float_none_v2_default_weak': ('f16',
                                            'f16x2',
                                            'bf16',
                                            'bf16x2',
                                            'f32',
                                            'e5m2x2',
                                            'e5m2x4',
                                            'e4m3x2',
                                            'e4m3x4'),
 'multimem_st_float_none_v2_default_ordered': ('f16',
                                               'f16x2',
                                               'bf16',
                                               'bf16x2',
                                               'f32',
                                               'e5m2x2',
                                               'e5m2x4',
                                               'e4m3x2',
                                               'e4m3x4'),
 'multimem_red_float_add_v2_default_red': ('f16', 'f16x2', 'bf16', 'bf16x2', 'f32'),
 'multimem_st_float_none_v4_default_weak': ('f16',
                                            'f16x2',
                                            'bf16',
                                            'bf16x2',
                                            'f32',
                                            'e5m2',
                                            'e5m2x2',
                                            'e5m2x4',
                                            'e4m3',
                                            'e4m3x2',
                                            'e4m3x4'),
 'multimem_st_float_none_v4_default_ordered': ('f16',
                                               'f16x2',
                                               'bf16',
                                               'bf16x2',
                                               'f32',
                                               'e5m2',
                                               'e5m2x2',
                                               'e5m2x4',
                                               'e4m3',
                                               'e4m3x2',
                                               'e4m3x4'),
 'multimem_red_float_add_v4_default_red': ('f16', 'f16x2', 'bf16', 'bf16x2', 'f32'),
 'multimem_st_float_none_v8_default_weak': ('f16',
                                            'bf16',
                                            'e5m2',
                                            'e5m2x2',
                                            'e4m3',
                                            'e4m3x2'),
 'multimem_st_float_none_v8_default_ordered': ('f16',
                                               'bf16',
                                               'e5m2',
                                               'e5m2x2',
                                               'e4m3',
                                               'e4m3x2'),
 'multimem_red_float_add_v8_default_red': ('f16', 'bf16')}


class MultimemValueVectorTests(unittest.TestCase):
    """Load the complete packaged selector inventory, with no reduced fake database."""

    @classmethod
    def setUpClass(cls) -> None:
        """Retain one canonical inventory and its normal resolved lowering."""
        cls.database = get_packaged_spec_database()
        cls.spec = next(i for i in cls.database.instructions if i.opcode == "multimem")
        cls.resolved = from_instruction_spec(cls.spec)
        cls.backend = load_cpp_backend(packaged_backend_spec())

    def test_exact_nine_sources_keep_complete_domains_and_policies(self) -> None:
        """Distinguish typed carrier admission from the narrower source producer set."""
        actual = {}
        partitions = {"numeric": 0, "exact32": 0, "source_na": 0}
        named = {True: 0, False: 0}
        for variant in self.spec.variants:
            for layout in variant.operand_layouts:
                for operand in layout.operands:
                    if operand.kind is not OperandKind.VALUE_VECTOR:
                        continue
                    self.assertIn(variant.name, EXPECTED_TYPES)
                    self.assertEqual((layout.name, operand.name), ("default", "src"))
                    slot = next(m for m in variant.modifiers if m.name == "type")
                    domain = tuple(item.value for item in slot.values)
                    self.assertEqual(set(domain), set(EXPECTED_TYPES[variant.name]))
                    self.assertEqual(operand.register_width_policy.value, "same_width")
                    self.assertEqual(operand.vector_type_policy.value, "element")
                    self.assertFalse(operand.vector_allow_sink)
                    self.assertEqual(operand.vector_allow_named, "_v8_" not in variant.name)
                    actual[variant.name] = domain
                    for dtype in domain:
                        group = ("source_na" if dtype in {"e4m3x2", "e5m2x2"}
                                 else "exact32" if dtype.endswith(("x2", "x4"))
                                 else "numeric")
                        partitions[group] += 1
                        named[operand.vector_allow_named] += 1
        self.assertEqual(set(actual), set(EXPECTED_TYPES))
        self.assertEqual(partitions, {"numeric": 32, "exact32": 20, "source_na": 12})
        self.assertEqual(named, {True: 50, False: 14})

    def test_derived_value_fields_keep_source_shape_and_existing_flags(self) -> None:
        """Each canonical source lowers through the existing value-vector path."""
        selected = [v for v in self.resolved.variants if v.variant_id in EXPECTED_TYPES]
        self.assertEqual(len(selected), 9)
        for variant in selected:
            with self.subTest(variant=variant.variant_id):
                layout = next(l for l in variant.operand_layouts if l.layout_id == "default")
                field = next(f for f in layout.fields if f.name == "src")
                binding = next(b for b in layout.bindings if b.target_field_id == "src")
                self.assertIs(field.value_kind, ResolvedValueKind.VALUE_VECTOR)
                self.assertTrue(binding.source_value_vector)
                self.assertFalse(binding.allow_vector_sink)
                self.assertEqual(binding.allow_named_vector, "_v8_" not in variant.variant_id)
                self.assertEqual(binding.register_width_policy.value, "same_width")

    def test_normal_header_exposes_nine_owned_value_sources(self) -> None:
        """Render via the existing complete generation context and normal public emitter."""
        context = build_generation_context(self.database, self.backend)
        indices = tuple(i for i, v in enumerate(self.spec.variants)
                        if v.name in EXPECTED_TYPES)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "multimem.gen.hpp"
            generate_resolved_opcode_header(context, category=self.spec.codegen_category,
                opcode="multimem", output_path=path, form_indices=indices)
            header = path.read_text(encoding="utf-8")
        self.assertEqual(header.count("WithLocs<ResolvedValueVector> src;"), 9)
        self.assertNotIn("WithLocs<ResolvedRegisterVector> src;", header)


if __name__ == "__main__":
    unittest.main()
