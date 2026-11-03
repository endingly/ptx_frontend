"""Exact legacy shuffle layouts and their preserved typed destination path."""

import tempfile
import unittest
from pathlib import Path

from ptx_frontend.code_gen.emit.availability import emit_availability
from ptx_frontend.code_gen.context import build_generation_context
from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.code_gen.emit.resolved_source import generate_resolved_opcode_source
from ptx_frontend.ir.resolved_ir import from_instruction_spec
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import ModifierPresence, OperandKind


class LegacyShuffleTests(unittest.TestCase):
    """Keep all four mask-free source forms separate from synchronized forms."""

    @classmethod
    def setUpClass(cls) -> None:
        """Normalize the complete database so selector overlaps remain errors."""
        cls.database = get_packaged_spec_database()
        cls.instruction = next(item for item in cls.database.instructions if item.opcode == "shfl")
        cls.variants = {item.name: item for item in cls.instruction.variants}
        cls.resolved = {item.variant_id: item for item in from_instruction_spec(cls.instruction).variants}

    def test_exact_inventory_targets_and_emission(self) -> None:
        """Every mode retains the exact two-clause version/SM window."""
        names = {f"shfl_{mode}_b32" for mode in ("up", "down", "bfly", "idx")}
        self.assertEqual(set(self.variants) - {f"shfl_sync_{mode}_b32" for mode in
                                             ("up", "down", "bfly", "idx")}, names)
        expected = {"any_of": [{"ptx": "3.0", "ptx_before": "6.4", "sm": 30},
                               {"ptx": "3.0", "sm": 30, "sm_before": 70}]}
        for name in names:
            with self.subTest(name=name):
                self.assertEqual(self.variants[name].availability, expected)
                self.assertEqual(dict(self.resolved[name].availability), expected)
                emitted = emit_availability(expected)
                self.assertIn(".any_of_count = 2", emitted)
                self.assertEqual(emitted.count(".minimum_ptx_version = {3, 0}"), 2)
                self.assertIn(".sm_before = 70", emitted)
                self.assertIn(".ptx_before = checker::PtxVersion{ 6, 4 }", emitted)
                self.assertNotIn(".exact_targets", emitted)

    def test_complete_destination_layout_and_control_contract_matrix(self) -> None:
        """Reuse synchronized data/predicate and b/c contracts, omitting only the mask."""
        for mode in ("up", "down", "bfly", "idx"):
            name = f"shfl_{mode}_b32"
            variant = self.variants[name]
            sync = self.variants[f"shfl_sync_{mode}_b32"]
            self.assertEqual([layout.name for layout in variant.operand_layouts],
                             ["without_predicate", "with_predicate"])
            for layout, synchronized in zip(variant.operand_layouts, sync.operand_layouts, strict=True):
                self.assertEqual(layout.operands, synchronized.operands[:4])
                self.assertEqual(len(layout.operands), 4)
                self.assertEqual([operand.name for operand in layout.operands], ["dst", "src", "lane", "clamp"])
            bare, paired = variant.operand_layouts
            self.assertIs(bare.operands[0].kind, OperandKind.REGISTER)
            self.assertIs(paired.operands[0].kind, OperandKind.SHFL_DESTINATION)
            self.assertFalse(paired.operands[0].allow_destination_sink)
            self.assertFalse(paired.operands[0].allow_predicate_sink)
            modifiers = {item.name: item for item in variant.modifiers}
            self.assertIs(modifiers["sync"].presence, ModifierPresence.ABSENT)
            self.assertIs(modifiers[mode].presence, ModifierPresence.FIXED)
            self.assertIs(modifiers[mode].value, True)
            self.assertEqual(modifiers["type"].value, "b32")
            resolved = self.resolved[name]
            self.assertEqual([field.source_name for field in resolved.modifier_fields], [mode, "type"])
            for layout in resolved.operand_layouts:
                self.assertEqual([field.name for field in layout.fields], ["dst", "src", "lane", "clamp"])

    def test_emitted_source_retains_all_eight_paired_destination_paths(self) -> None:
        """Generation keeps synchronized and legacy destination checks in separate forms."""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "shfl.cpp"
            backend = load_cpp_backend(Path(__file__).resolve().parents[3] /
                                       "instructions/ptx_cpp_backend_spec/ptx_frontend.yaml")
            generate_resolved_opcode_source(build_generation_context(self.database, backend),
                                           category="data_movement", opcode="shfl", output_path=path)
            source = path.read_text(encoding="utf-8")
        self.assertEqual(source.count(".actual_shape = check_end::OperandShape::ShflDestination,"), 8)
        for mode in ("Up", "Down", "Bfly", "Idx"):
            self.assertIn(f"Shfl{mode}B32", source)

    def test_exclusive_numeric_sm_bound_pipeline_and_validation(self) -> None:
        """Preserve plain/DNF bounds through schema, normalization, IR and rendering."""
        from copy import deepcopy
        from jsonschema import Draft202012Validator
        from ptx_frontend.spec.resources import packaged_spec_schema
        from ptx_frontend.spec.load_yaml import load_yaml
        from ptx_frontend.spec.normalize.availability import normalize_availability
        from ptx_frontend.code_gen.render_models.availability import build_availability
        schema = load_yaml(packaged_spec_schema())
        validator = Draft202012Validator({"$ref": "#/$defs/availability", "$defs": schema['$defs']})
        for plain in ({'sm': 30, 'sm_before': 70}, {'sm_before': 4294967295}):
            for raw in (plain, {'any_of': [plain]}):
                self.assertTrue(validator.is_valid(raw))
                self.assertEqual(normalize_availability(raw), raw)
                from dataclasses import replace
                variant = replace(self.instruction.variants[0], availability=raw)
                instruction = replace(self.instruction, variants=(variant,))
                projected = from_instruction_spec(instruction).variants[0]
                self.assertEqual(dict(projected.availability), raw)
                model = build_availability(raw)
                clause = model.minimum or model.clauses[0]
                self.assertEqual(clause.sm_before, plain['sm_before'])
                self.assertIn(f".sm_before = {plain['sm_before']}", emit_availability(raw))
        for value in (0, -1, True, False, 70.0, '70', None, 2**32):
            for raw in ({'sm_before': value}, {'any_of': [{'sm_before': value}]}):
                # JSON Schema treats integral floats as mathematical integers; the typed
                # normalizer/render boundary additionally requires the Python int domain.
                if type(value) is not float:
                    self.assertFalse(validator.is_valid(raw))
                with self.assertRaises((TypeError, ValueError)):
                    normalize_availability(raw)
                with self.assertRaises((TypeError, ValueError)):
                    build_availability(raw)
        for plain in ({'sm': 70, 'sm_before': 70}, {'sm': 71, 'sm_before': 70}):
            for raw in (plain, {'any_of': [plain]}):
                with self.assertRaises(ValueError): normalize_availability(raw)
                with self.assertRaises(ValueError): build_availability(raw)
        mixed = {'any_of': [{'sm': 30}], 'sm_before': 70}
        self.assertFalse(validator.is_valid(mixed))
        with self.assertRaises(ValueError): normalize_availability(mixed)
        with self.assertRaises(ValueError): build_availability(mixed)
        with self.assertRaises(ValueError): normalize_availability({'any_of': [{'sm_before': 70}]*7})
        self.assertIsNone(build_availability({'sm': 30}).minimum.sm_before)
        self.assertNotIn('.sm_before', emit_availability({'sm': 30}))


if __name__ == "__main__":
    unittest.main()
