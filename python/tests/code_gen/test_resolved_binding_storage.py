"""Keep complete operand binding storage shared only within one emitted catalog."""

from copy import deepcopy
from dataclasses import fields, replace
import re
import unittest

from ptx_frontend.code_gen.cpp_backend import CppDomain, load_cpp_backend
from ptx_frontend.code_gen.emit.resolved_descriptors import _emit_resolved_descriptor_storage
from ptx_frontend.ir.resolved_ir import (
    ResolvedAddressStateSpace,
    ResolvedOperandBinding,
    ResolvedInstruction,
    ResolvedOperandShape,
    ResolvedOperandTypeExpression,
    ResolvedOperandTypeExpressionKind,
    ResolvedParameterAddressConstraint,
    ResolvedVectorTypePolicy,
    from_instruction_spec,
)
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.resources import packaged_backend_spec
from ptx_frontend.spec.model import VideoInstructionSpec, VideoOperandSpec


class ResolvedBindingStorageTests(unittest.TestCase):
    """Exercise real MOV layouts with independently varied descriptor constraints."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load a real resolved form and add nonempty borrowed side-array domains."""
        cls.backend = load_cpp_backend(packaged_backend_spec())
        spec = next(x for x in get_packaged_spec_database().instructions if x.opcode == "mov")
        cls.instruction = from_instruction_spec(spec)
        cls.variant = cls.instruction.variants[0]
        cls.layout = cls.variant.operand_layouts[0]
        availability = (("any_of", [{"ptx": "8.0", "sm": 90}]),)
        cls.binding = replace(
            cls.layout.bindings[0],
            allowed_vector_arities=(2, 4),
            allowed_vector_register_types=("u32", "b32"),
            allowed_address_state_spaces=(ResolvedAddressStateSpace("global", availability),),
            parameter_constraint=ResolvedParameterAddressConstraint("input", availability),
            sink_availability=availability,
        )

    def instruction_with_groups(
        self, *groups: tuple[ResolvedOperandBinding, ...]
    ) -> ResolvedInstruction:
        """Keep layout fields distinct while replacing only their complete bindings."""
        layouts = tuple(replace(self.layout, layout_id=f"layout_{index}", bindings=group)
                        for index, group in enumerate(groups))
        return replace(self.instruction, variants=(replace(self.variant, operand_layouts=layouts),))

    def binding_definitions(self, text: str) -> list[str]:
        """Extract actual array definitions, excluding layout references and modifier arrays."""
        return re.findall(r"array<check_end::ResolvedOperandBindingDescriptor, \d+>\s+(\w+) =", text)

    def test_equal_groups_share_complete_storage_with_direct_references(self) -> None:
        """Equal unhashable constraints share side arrays, but keep form/layout fields."""
        with self.assertRaises(TypeError):
            hash(self.binding)
        group = (self.binding, self.layout.bindings[1])
        instruction = self.instruction_with_groups(group, deepcopy(group))
        other = replace(instruction.variants[0], variant_id="mov_shared", cpp_name="Shared",
                        operand_layouts=instruction.variants[0].operand_layouts[:1])
        instruction = replace(instruction, variants=(*instruction.variants, other))
        original = deepcopy(instruction)
        text = _emit_resolved_descriptor_storage(instruction, self.backend)
        definitions = self.binding_definitions(text)
        self.assertEqual(definitions, ["mov_scalar_operand_layout_0_bindings"])
        self.assertEqual(re.findall(r"\.bindings = (\w+),", text), definitions * 3)
        side_definitions = re.findall(
            r"\s(\w+_binding_\d+_(?:vector_arities|register_types|address_state_spaces)) =", text)
        self.assertEqual(len(side_definitions), 3)
        side_references = re.findall(
            r"\.(?:allowed_vector_arities|allowed_register_types|allowed_address_state_spaces) = (\w+),", text)
        self.assertEqual(set(side_references), set(side_definitions))
        for name in side_definitions:
            self.assertEqual(side_definitions.count(name), 1)
            self.assertLess(text.index(name + " ="), text.index(definitions[0] + " ="))
        field_names = re.findall(r"\s(\w+_operand_layout_\d+_fields) =", text)
        self.assertEqual(field_names, ["mov_scalar_operand_layout_0_fields",
                                     "mov_scalar_operand_layout_1_fields",
                                     "mov_shared_operand_layout_0_fields"])
        self.assertTrue(all(f".fields = {name}," in text for name in field_names))
        self.assertLess(text.index(definitions[0] + " ="), text.index("mov_scalar_fields ="))
        self.assertIn('.variant_name = "Shared"', text)
        self.assertIn('.layout_id = "layout_1"', text)
        self.assertEqual(instruction, original)
        self.assertEqual(text, _emit_resolved_descriptor_storage(instruction, self.backend))

    def test_semantic_differences_keep_separate_complete_arrays(self) -> None:
        """Nested availability, parameter, type, vector, and sink differences never merge."""
        availability = (("any_of", [{"ptx": "8.1", "sm": 90}]),)
        changes = {
            "target": {"target_field_id": "different"},
            "type": {"type_expression": ResolvedOperandTypeExpression(
                ResolvedOperandTypeExpressionKind.FIXED_SCALAR, scalar_type="u32")},
            "shape": {"allowed_shapes": (ResolvedOperandShape.IMMEDIATE,)},
            "address_domain": {"allowed_address_state_spaces": (
                replace(self.binding.allowed_address_state_spaces[0], value="shared"),)},
            "address_availability": {"allowed_address_state_spaces": (
                replace(self.binding.allowed_address_state_spaces[0], availability=availability),)},
            "parameter_direction": {"parameter_constraint": replace(
                self.binding.parameter_constraint, direction="return")},
            "parameter_entry": {"parameter_constraint": replace(
                self.binding.parameter_constraint, entry_only=True)},
            "parameter_availability": {"parameter_constraint": replace(
                self.binding.parameter_constraint, function_availability=availability)},
            "sink_availability": {"sink_availability": availability},
            "sink_permission": {"allow_destination_sink": True},
            "vector_arities": {"allowed_vector_arities": (4,)},
            "vector_types": {"allowed_vector_register_types": ("u32",)},
            "vector_policy": {"vector_type_policy": ResolvedVectorTypePolicy.ELEMENT},
            "parameter_address": {"preserve_parameter_address_space": True},
            "type_tag": {"type_tag": "tensor"},
        }
        for label, kwargs in changes.items():
            with self.subTest(label=label):
                instruction = self.instruction_with_groups((self.binding,),
                    (replace(self.binding, **kwargs),))
                text = _emit_resolved_descriptor_storage(instruction, self.backend)
                self.assertEqual(self.binding_definitions(text), [
                    "mov_scalar_operand_layout_0_bindings", "mov_scalar_operand_layout_1_bindings"])
                for suffix in ("vector_arities", "register_types", "address_state_spaces"):
                    self.assertIn(f"mov_scalar_operand_layout_1_binding_0_{suffix} =", text)
        # Tuple order is part of the full binding contract too.
        text = _emit_resolved_descriptor_storage(self.instruction_with_groups(
            (self.binding, self.layout.bindings[1]), (self.layout.bindings[1], self.binding)), self.backend)
        self.assertEqual(len(self.binding_definitions(text)), 2)
        for model in (ResolvedOperandBinding, ResolvedOperandTypeExpression,
                      ResolvedAddressStateSpace, ResolvedParameterAddressConstraint,
                      VideoInstructionSpec, VideoOperandSpec):
            self.assertTrue(all(field.compare for field in fields(model)))

    def test_calls_shards_empty_groups_and_backends_are_isolated(self) -> None:
        """First occurrence naming restarts for each invocation and shard slice."""
        instruction = self.instruction_with_groups((self.binding,), (self.binding,))
        original = _emit_resolved_descriptor_storage(instruction, self.backend)
        for index, layout in enumerate(instruction.variants[0].operand_layouts):
            variant = replace(self.variant, variant_id=f"mov_shard_{index}",
                              operand_layouts=(layout,))
            shard = replace(instruction, variants=(variant,))
            text = _emit_resolved_descriptor_storage(shard, self.backend)
            self.assertEqual(self.binding_definitions(text), [f"mov_shard_{index}_operand_layout_0_bindings"])
            self.assertNotIn("mov_scalar_operand_layout_0_bindings", text)
        empty = _emit_resolved_descriptor_storage(self.instruction_with_groups((), ()), self.backend)
        self.assertEqual(len(self.binding_definitions(empty)), 1)
        self.assertIn("array<check_end::ResolvedOperandBindingDescriptor, 0>", empty)
        disabled = replace(self.binding, allowed_vector_arities=(),
                           allowed_vector_register_types=(), allowed_address_state_spaces=(),
                           parameter_constraint=None, sink_availability=())
        disabled_text = _emit_resolved_descriptor_storage(self.instruction_with_groups(
            (disabled,), (replace(disabled, vector_type_policy=ResolvedVectorTypePolicy.ELEMENT),)),
            self.backend)
        self.assertEqual(len(self.binding_definitions(disabled_text)), 2)
        for suffix in ("vector_arities", "register_types", "address_state_spaces"):
            self.assertNotIn(f"_binding_0_{suffix}", disabled_text)
        other_backend = deepcopy(self.backend)
        other_backend.domains[CppDomain.SCALAR_TYPES.value].values["u32"] = "OtherScalar::U32"
        changed = _emit_resolved_descriptor_storage(instruction, other_backend)
        self.assertIn("OtherScalar::U32", changed)
        self.assertNotIn("OtherScalar::U32", original)
        self.assertEqual(original, _emit_resolved_descriptor_storage(instruction, self.backend))
