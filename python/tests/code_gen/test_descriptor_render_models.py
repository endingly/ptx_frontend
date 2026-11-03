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
    _build_operand_binding,
    build_resolved_descriptor_storage,
)
from ptx_frontend.code_gen.render_models.syntax_descriptors import build_syntax_descriptor_storage
from ptx_frontend.ir.resolved_ir import (
    from_instruction_spec, ResolvedOperandRole, ResolvedOperandAccess,
    ResolvedOperandShape, ResolvedOperandTypeExpressionKind,
)
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

    def test_integer_alternative_is_a_trailing_typed_optional(self) -> None:
        """Default bindings stay absent; eight scalar and eight packed sources render B16/B32."""
        specification = next(s for s in self.specifications if s.opcode == "setp")
        storage = build_resolved_descriptor_storage(from_instruction_spec(specification), self.backend)
        policies = [binding.integer_immediate_type for group in storage.binding_groups
                    for binding in group.bindings if binding.integer_immediate_type is not None]
        self.assertTrue(policies)
        self.assertEqual(set(policies), {"ScalarType::B16", "ScalarType::B32"})
        variant = next(v for v in from_instruction_spec(specification).variants
                       if v.variant_id == "setp_f16")
        layout = variant.operand_layouts[0]
        without = replace(layout, layout_id="without_policy", bindings=tuple(
            replace(b, integer_immediate_type=None) if b.target_field_id == "src1" else b
            for b in layout.bindings))
        manual = replace(from_instruction_spec(specification), variants=(
            replace(variant, operand_layouts=(layout, without)),))
        interned = build_resolved_descriptor_storage(manual, self.backend)
        self.assertEqual(len(interned.binding_groups), 2)
        policies = {b.integer_immediate_type for group in interned.binding_groups
                    for b in group.bindings if b.target_field_id == "src1"}
        self.assertEqual(policies, {None, "ScalarType::B16"})

    def test_manual_integer_alternative_rendering_validates_existing_scalar_facts(self) -> None:
        """Forged derived bindings cannot bypass the normalizer's engaged context."""
        specification = next(s for s in self.specifications if s.opcode == "setp")
        instruction = from_instruction_spec(specification)
        binding = next(b for v in instruction.variants if v.variant_id == "setp_f16"
                       for layout in v.operand_layouts for b in layout.bindings
                       if b.target_field_id == "src1")
        for logical in ("f16", "bf16"):
            valid = replace(binding, type_expression=replace(
                binding.type_expression, scalar_type=logical))
            rendered = _build_operand_binding(valid, "manual", self.backend)
            self.assertEqual(rendered.integer_immediate_type, "ScalarType::B16")
            self.assertIsNone(_build_operand_binding(replace(valid,
                integer_immediate_type=None), "manual", self.backend).integer_immediate_type)
        invalid = [replace(binding, integer_immediate_type=value)
                   for value in ("b32", "u16", "f16", "b8", "invalid")]
        invalid += [replace(binding, type_expression=replace(
            binding.type_expression, scalar_type=value)) for value in ("f32", "b16", "f16x2")]
        invalid += [replace(binding, role=ResolvedOperandRole.DESTINATION),
                    replace(binding, access=ResolvedOperandAccess.WRITE),
                    replace(binding, access=ResolvedOperandAccess.READ_WRITE),
                    replace(binding, source_value_vector=True),
                    replace(binding, minimum_elements=2, maximum_elements=2),
                    replace(binding, type_expression=replace(binding.type_expression,
                        kind=ResolvedOperandTypeExpressionKind.MODIFIER_FIELD,
                        scalar_type=None, modifier_field_id="type"))]
        invalid += [replace(binding, allowed_shapes=shape) for shape in (
            (ResolvedOperandShape.REGISTER,), (ResolvedOperandShape.IMMEDIATE,),
            (ResolvedOperandShape.VECTOR, ResolvedOperandShape.IMMEDIATE))]
        for candidate in invalid:
            with self.subTest(binding=candidate):
                with self.assertRaisesRegex(ValueError, "read scalar half"):
                    _build_operand_binding(candidate, "manual", self.backend)

    def test_named_element_policy_renders_with_only_existing_min_max_count(self) -> None:
        """Named modern counts must not lose Element policy in the C++ projection."""
        specification = next(s for s in self.specifications if s.opcode == "mma")
        instruction = from_instruction_spec(specification)
        binding = next(b for v in instruction.variants for layout in v.operand_layouts
                       for b in layout.bindings if b.allow_named_vector and b.minimum_elements == 2)
        self.assertEqual(binding.allowed_vector_arities, ())
        self.assertIsNone(binding.vector_arity_modifier_field_id)
        rendered = _build_operand_binding(binding, "modern", self.backend)
        members = {member.name: member.expression for member in rendered.vector_members}
        self.assertEqual(members["vector_type_policy"], "checker::VectorTypePolicy::Element")
        self.assertEqual(members["allow_named_vector"], "true")
        self.assertNotIn("allowed_vector_arities", members)
        omitted = _build_operand_binding(replace(binding, allow_named_vector=False),
                                         "brace_only", self.backend)
        self.assertNotIn("allow_named_vector", {member.name for member in omitted.vector_members})

    def test_packed_integer_policy_manual_render_pairs_and_shapes(self) -> None:
        """Derived bindings must satisfy the same finite pairs before emission."""
        instruction = from_instruction_spec(next(s for s in self.specifications if s.opcode == "setp"))
        for logical, variant_id in (("f16x2", "setp_f16x2"), ("bf16x2", "setp_bf16x2")):
            binding = next(b for v in instruction.variants if v.variant_id == variant_id
                           for layout in v.operand_layouts for b in layout.bindings
                           if b.target_field_id == "src1")
            self.assertEqual(_build_operand_binding(binding, "packed", self.backend).integer_immediate_type,
                             "ScalarType::B32")
            self.assertIsNone(_build_operand_binding(replace(binding, integer_immediate_type=None),
                                                   "packed", self.backend).integer_immediate_type)
            invalid = [replace(binding, integer_immediate_type=word) for word in ("b16", "b64", "u32")]
            invalid += [replace(binding, type_expression=replace(binding.type_expression,
                            scalar_type=wrong)) for wrong in ("f16", "bf16", "b32", "e4m3x4")]
            invalid += [replace(binding, role=ResolvedOperandRole.DESTINATION),
                        replace(binding, access=ResolvedOperandAccess.WRITE),
                        replace(binding, source_value_vector=True),
                        replace(binding, minimum_elements=2, maximum_elements=2),
                        replace(binding, allowed_shapes=(ResolvedOperandShape.REGISTER,)),
                        replace(binding, allowed_shapes=(ResolvedOperandShape.VECTOR,
                                                        ResolvedOperandShape.IMMEDIATE))]
            for candidate in invalid:
                with self.subTest(logical=logical, binding=candidate), self.assertRaises(ValueError):
                    _build_operand_binding(candidate, "packed", self.backend)

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

    def test_packed_integer_set_destination_retains_dtype_expression(self) -> None:
        """Keep all four typed dtype bindings through descriptor render models."""

        specification = next(spec for spec in self.specifications if spec.opcode == "set")
        names = {"set_half_integer_f16x2", "set_half_integer_f16x2_boolean",
                 "set_half_integer_bf16x2", "set_half_integer_bf16x2_boolean"}
        selected = replace(specification, variants=tuple(
            variant for variant in specification.variants if variant.name in names))
        self.assertEqual(len(selected.variants), 4)
        model = build_resolved_descriptor_storage(from_instruction_spec(selected), self.backend)
        destinations = [binding for group in model.binding_groups
                        for binding in group.bindings if binding.target_field_id == "dst"]
        self.assertEqual(len(destinations), 4)
        for binding in destinations:
            self.assertEqual(binding.type_expression.kind.rsplit("::", 1)[-1], "ModifierField")
            self.assertEqual(binding.type_expression.modifier_field_id, '"dtype"')
            self.assertEqual(binding.type_expression.fixed_scalar_type.rsplit("::", 1)[-1], "Invalid")

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
