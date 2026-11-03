"""Named aggregate/scalar layout disjointness matches declaration-aware selection."""

from copy import deepcopy
import unittest

from jsonschema import Draft202012Validator
import yaml

from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.code_gen.emit.resolved_descriptors import _emit_resolved_descriptor_storage
from ptx_frontend.code_gen.emit.syntax_descriptors import _emit_instruction_descriptor_storage
from ptx_frontend.ir.resolved_ir import from_instruction_spec
from ptx_frontend.ir.syntax_ast import from_InstructionSpec
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import OperandKind, OperandVectorTypePolicy
from ptx_frontend.spec.normalize.layout import normalize_operand_layouts
from ptx_frontend.spec.resources import packaged_backend_spec, packaged_spec_schema
from ptx_frontend.spec.synatax_shapes import OperandSyntaxShape, operand_syntax_shapes


def operand(name: str, kind: str) -> dict:
    """Build a fixed-type slot without instruction-name-based selection policy."""
    return {"name": name, "kind": kind, "type": "b64"}


def aggregate(name: str) -> dict:
    """Authorize ordinary named registers in an exact-width aggregate slot."""
    return {**operand(name, "reg_vector"),
            "vector": {"arity": [2, 4], "type_policy": "aggregate", "allow_named": True}}


class NamedAggregateLayoutTests(unittest.TestCase):
    """Exercise canonical schema/model/render output and a true ambiguity control."""

    def test_generic_scalar_pack_unpack_shapes_are_declaration_disjoint(self) -> None:
        """No opcode name or special ordering is needed for the declaration proof."""
        raw = {"name": "unrelated_name", "operand_layouts": [
            {"name": "scalar", "operands": [operand("dst", "reg"), operand("src", "mov_scalar_src")]},
            {"name": "pack", "operands": [operand("dst", "reg"), aggregate("src")]},
            {"name": "unpack", "operands": [aggregate("dst"), operand("src", "reg")]},
        ]}
        layouts = normalize_operand_layouts(raw, None, {})
        self.assertEqual([x.name for x in layouts], ["scalar", "pack", "unpack"])
        duplicate = deepcopy(raw)
        duplicate["operand_layouts"].append({"name": "duplicate_pack", "operands": deepcopy(raw["operand_layouts"][1]["operands"])})
        with self.assertRaisesRegex(ValueError, "overlapping syntax"):
            normalize_operand_layouts(duplicate, None, {})

    def test_schema_and_canonical_models_opt_in_only_mov_aggregate_slots(self) -> None:
        """All prior flags stay local and only the four existing MOV bindings opt in."""
        schema = yaml.safe_load(packaged_spec_schema().read_text())
        validator = Draft202012Validator({"$schema": schema["$schema"], "$defs": schema["$defs"], "$ref": "#/$defs/operand"})
        validator.validate(aggregate("src"))
        rejected = aggregate("src")
        rejected["vector"]["allow_named"] = "true"
        self.assertTrue(list(validator.iter_errors(rejected)))
        named = []
        for instruction in get_packaged_spec_database().instructions:
            for variant in instruction.variants:
                for layout in variant.operand_layouts:
                    for slot in layout.operands:
                        if (slot.kind is OperandKind.REGISTER_VECTOR
                                and slot.vector_type_policy is OperandVectorTypePolicy.AGGREGATE
                                and slot.vector_allow_named):
                            named.append((instruction.opcode, variant.name, layout.name, slot.name))
                            self.assertTrue(operand_syntax_shapes(slot) & OperandSyntaxShape.IDENTIFIER_REF)
        self.assertEqual(set(named), {
            ("mov", variant, layout, slot)
            for variant in ("mov_scalar", "mov_b128_pack_unpack")
            for layout, slot in (("pack", "src"), ("unpack", "dst"))})

    def test_existing_descriptor_rendering_retains_typed_named_aggregate_policy(self) -> None:
        """Generated bindings and syntax agree without adding public descriptor fields."""
        backend = load_cpp_backend(packaged_backend_spec())
        spec = next(x for x in get_packaged_spec_database().instructions if x.opcode == "mov")
        resolved = _emit_resolved_descriptor_storage(from_instruction_spec(spec), backend)
        syntax = _emit_instruction_descriptor_storage(from_InstructionSpec(spec), backend, cpp_name="mov")
        self.assertIn(".allow_named_vector = true", resolved)
        self.assertIn("VectorTypePolicy::Aggregate", resolved)
        self.assertIn("OperandSyntaxShape::Identifier", syntax)
        self.assertIn("OperandSyntaxShape::VectorPack", syntax)
