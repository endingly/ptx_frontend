"""Typed video contracts and source layouts derived from canonical YAML."""

from copy import deepcopy
import unittest
import tempfile
from pathlib import Path

from jsonschema import Draft202012Validator
from ptx_frontend.code_gen.emit.resolved_source import generate_resolved_opcode_source

from ptx_frontend.code_gen.context import build_generation_context
from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.ir.syntax_ast import from_InstructionSpec
from ptx_frontend.spec.database import load_codegen_database_from_files
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.model import VideoLanes, VideoSelectorPolicy
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.resources import packaged_backend_spec, packaged_spec_dir, packaged_spec_schema
from ptx_frontend.spec.synatax_shapes import OperandSyntaxShape


class VideoNormalizationTests(unittest.TestCase):
    """Topology, source shape specificity, and field references stay typed."""

    def setUp(self) -> None:
        """Read the packaged canonical video snapshot for each mutation test."""
        self.path = packaged_spec_dir().joinpath("video.yaml")
        self.raw = load_yaml(self.path)

    def test_scalar_vadd_shapes_and_field_projection(self) -> None:
        """Merge and secondary layouts remain disjoint after typed normalization."""
        instruction = normalize_instruction_spec(self.raw)[0]
        variant = instruction.variants[0]
        self.assertIs(variant.video.lanes, VideoLanes.SCALAR)
        syntax = from_InstructionSpec(instruction).variants[0]
        plain, secondary, merge = syntax.operand_layouts
        self.assertEqual(secondary.slots[0].allowed_syntax_shapes,
                         OperandSyntaxShape.IDENTIFIER_REF)
        self.assertEqual(merge.slots[0].allowed_syntax_shapes,
                         OperandSyntaxShape.VECTOR_MEMBER)
        self.assertTrue(plain.slots[1].allows(OperandSyntaxShape.IMMEDIATE))
        self.assertFalse(plain.slots[1].allows(
            OperandSyntaxShape.ARITHMETIC_NEGATED_REGISTER))
        database = load_codegen_database_from_files(spec_files=(self.path,), category="video")
        context = build_generation_context(database, load_cpp_backend(packaged_backend_spec()))
        slot = context.entries[0].resolved.variants[0].operand_layouts[0].bindings[0]
        self.assertEqual(slot.video_instruction.sat_modifier, "saturate")
        self.assertIs(slot.video.selector, VideoSelectorPolicy.NONE)

    def test_rejects_contradictory_slot_contracts(self) -> None:
        """Malformed canonical metadata cannot silently widen source acceptance."""
        mutations = [
            ("allow_negate", True), ("type_modifier", "missing"),
            ("selector", "half_swizzle"), ("position", "b"),
        ]
        for key, value in mutations:
            with self.subTest(key=key):
                raw = deepcopy(self.raw)
                raw["instructions"][0]["variants"][0]["operand_layouts"][0]["operands"][1]["video"][key] = value
                with self.assertRaises(ValueError):
                    normalize_instruction_spec(raw)

    def test_video_schema_is_closed(self) -> None:
        """Schema validation rejects malformed selector and control declarations."""
        validator = Draft202012Validator(load_yaml(packaged_spec_schema()))
        self.assertFalse(list(validator.iter_errors(self.raw)))
        for key, value in [("selector", "byte_alias"), ("allow_immediate", "yes"),
                           ("type_use", "integer_alias"), ("unknown", True)]:
            with self.subTest(key=key):
                raw = deepcopy(self.raw)
                raw["instructions"][0]["variants"][0]["operand_layouts"][0]["operands"][1]["video"][key] = value
                self.assertTrue(list(validator.iter_errors(raw)))

    def test_video_generated_source_uses_typed_contracts(self) -> None:
        """Generation preserves selectors and backend field identities without aliases."""
        database = load_codegen_database_from_files(spec_files=(self.path,), category="video")
        context = build_generation_context(database, load_cpp_backend(packaged_backend_spec()))
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "vadd.cpp"
            generate_resolved_opcode_source(context, category="video", opcode="vadd", output_path=output)
            source = output.read_text()
        self.assertIn('".sat"', source)
        self.assertNotIn('".True"', source)
        self.assertIn("VideoSelectorPolicy::RequiredScalar", source)
        self.assertIn("VideoOperandTypeUse::ModifierField", source)
        self.assertIn('.sat_field_id = "saturate"', source)
        self.assertIn(".video_operand = &selected.a.value", source)
        self.assertIn("observer.video_operand", source)

    def test_rejects_immediate_destination(self) -> None:
        """A write carrier never becomes an immediate through slot metadata."""
        raw = deepcopy(self.raw)
        raw["instructions"][0]["variants"][0]["operand_layouts"][0]["operands"][0]["video"]["allow_immediate"] = True
        with self.assertRaisesRegex(ValueError, "destination"):
            normalize_instruction_spec(raw)
