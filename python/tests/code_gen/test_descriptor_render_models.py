"""Protect typed descriptor composition independently of template rendering."""

from dataclasses import replace
from types import SimpleNamespace
from unittest.mock import patch
import unittest

from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.code_gen.artifact_layout import form_shards
from ptx_frontend.code_gen.context import GenerationInstruction
from ptx_frontend.code_gen.render_models.resolved_source import build_descriptor_shard_source
from ptx_frontend.code_gen.resolved_value_traits import modifier_descriptor_default_members
from ptx_frontend.code_gen.render_models.availability import AvailabilityModel
from ptx_frontend.code_gen.render_models.checker_descriptors import build_checker_descriptor_storage
from ptx_frontend.code_gen.render_models.matrix import MatrixDescriptorModel
from ptx_frontend.code_gen.render_models.resolved_descriptors import (
    ResolvedTypeExpressionModel,
    build_resolved_descriptor_storage,
)
from ptx_frontend.code_gen.render_models.syntax_descriptors import build_syntax_descriptor_storage
from ptx_frontend.ir.resolved_ir import from_instruction_spec
from ptx_frontend.ir.syntax_ast import from_InstructionSpec
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.resources import packaged_backend_spec


class DescriptorRenderModelTests(unittest.TestCase):
    """Builders retain typed child facts and instruction-local references."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load one canonical backend and the packaged source snapshot."""

        cls.backend = load_cpp_backend(packaged_backend_spec())
        cls.specifications = get_packaged_spec_database().instructions
        cls.mov = next(spec for spec in cls.specifications if spec.opcode == "mov")

    def test_storage_builders_do_not_render_nested_fragments(self) -> None:
        """All storage builders work while the shared renderer is unavailable."""

        instruction = from_instruction_spec(self.mov)
        with patch("ptx_frontend.code_gen.rendering.render_template", side_effect=AssertionError("unexpected render")):
            resolved = build_resolved_descriptor_storage(instruction, self.backend)
            checker = build_checker_descriptor_storage(instruction, self.backend)
            syntax = build_syntax_descriptor_storage(from_InstructionSpec(self.mov), self.backend, cpp_name=instruction.cpp_name)
        self.assertIsInstance(resolved.binding_groups[0].bindings[0].type_expression, ResolvedTypeExpressionModel)
        self.assertIsInstance(checker.variants[0].availability, AvailabilityModel)
        self.assertEqual(syntax.storage_name, "MovDescriptorStorage")

    def test_matrix_descriptor_remains_a_typed_child(self) -> None:
        """Resolved storage owns matrix facts until the parent template renders."""

        specification = next(spec for spec in self.specifications if spec.opcode == "mma")
        instruction = from_instruction_spec(specification)
        variant = next(variant for variant in instruction.variants if variant.matrix is not None)
        model = build_resolved_descriptor_storage(replace(instruction, variants=(variant,)), self.backend)
        self.assertIsInstance(model.variants[0].matrix, MatrixDescriptorModel)
        self.assertEqual(model.variants[0].matrix.m, variant.matrix.shape.m)

    def test_descriptor_shard_translates_only_its_canonical_syntax_slice(self) -> None:
        """A later shard preserves rows without translating discarded variants."""

        specification = next(spec for spec in self.specifications if spec.opcode == "mma")
        entry = GenerationInstruction(specification, from_instruction_spec(specification))
        indices = form_shards(entry)[1]
        complete = from_InstructionSpec(specification)
        expected = build_syntax_descriptor_storage(
            replace(complete, variants=tuple(complete.variants[index] for index in indices)),
            self.backend, cpp_name=entry.cpp_name,
        )
        context = SimpleNamespace(entries=(entry,), backend=self.backend)
        with patch("ptx_frontend.code_gen.render_models.resolved_source.from_InstructionSpec",
                   wraps=from_InstructionSpec) as translate:
            model = build_descriptor_shard_source(context,
                category=specification.codegen_category, opcode=specification.opcode, shard_index=1)
        self.assertEqual(model.syntax_storage, expected)
        self.assertEqual(translate.call_args.args[0].variants,
                         tuple(specification.variants[index] for index in indices))
        self.assertEqual(len(specification.variants), len(complete.variants))

    def test_checker_neutral_members_are_prepared_once_per_storage(self) -> None:
        """Independent builds reuse local defaults while retaining equal facts."""

        instruction = from_instruction_spec(self.mov)
        with patch("ptx_frontend.code_gen.render_models.checker_descriptors.modifier_descriptor_default_members",
                   wraps=modifier_descriptor_default_members) as defaults:
            first = build_checker_descriptor_storage(instruction, self.backend)
            second = build_checker_descriptor_storage(instruction, self.backend)
        self.assertEqual(defaults.call_count, 2)
        self.assertEqual(first, second)
        self.assertIsNot(first, second)

    def test_historical_modifier_orders_borrow_canonical_value_arrays(self) -> None:
        """Reordering modifier rows keeps their canonical allowed-value owners."""

        descriptor = from_InstructionSpec(self.mov)
        variant = next(variant for variant in descriptor.variants if variant.modifiers)
        variant = replace(variant, modifier_order_aliases=(tuple(reversed(variant.modifiers)),))
        descriptor = replace(descriptor, variants=(variant,))
        model = build_syntax_descriptor_storage(descriptor, self.backend, cpp_name="Mov")
        canonical = {modifier.kind_id: modifier.allowed_values for modifier in model.variants[0].modifiers.modifiers}
        alias = model.variants[0].aliases[0]
        self.assertEqual(tuple(modifier.allowed_values for modifier in alias.modifiers),
                         tuple(canonical[modifier.kind_id] for modifier in alias.modifiers))
        self.assertEqual(len(model.variants[0].allowed_values), sum(bool(modifier.allowed_spellings) for modifier in variant.modifiers))


if __name__ == "__main__":
    unittest.main()
