"""Independent source expectations for canonical tensor cache controls."""

from dataclasses import replace
import unittest

from ptx_frontend.ir.resolved_ir import (
    ResolvedFieldStorage,
    from_instruction_spec,
)
from ptx_frontend.spec.database import load_packaged_spec_database
from ptx_frontend.spec.model import (
    ModifierKind,
    ModifierPresence,
    OperandAccess,
    OperandImmediateConversionPolicy,
    OperandKind,
    OperandRole,
    OperandTypeExpressionKind,
    VariantSpec,
)


def _is_tensor(variant: VariantSpec) -> bool:
    """Identify tensor forms from canonical operand kinds, not variant names."""

    return any(operand.kind is OperandKind.TENSOR_OPERAND
               for layout in variant.operand_layouts
               for operand in layout.operands)


def _has_cache_hint(variant: VariantSpec) -> bool:
    """Separate old no-hint forms by their canonical modifier inventory."""

    return any(modifier.name == "cache_hint" for modifier in variant.modifiers)


def _parent_matches(parent: VariantSpec, sibling: VariantSpec) -> bool:
    """Compare semantic source controls after removing only the new hint."""

    inherited_layouts = tuple(
        (layout.operands, layout.kind, layout.availability,
         layout.forbidden_modifiers)
        for layout in parent.operand_layouts
    )
    sibling_unextended_layouts = tuple(
        (layout.operands, layout.kind, layout.availability,
         layout.forbidden_modifiers)
        for layout in sibling.operand_layouts
        if not layout.operands or
        layout.operands[-1].name != "cache_policy"
    )
    return (
        parent.availability == sibling.availability
        and parent.completion_kind is sibling.completion_kind
        and parent.rule is sibling.rule
        and parent.condition_code_effect is sibling.condition_code_effect
        and parent.memory_consistency == sibling.memory_consistency
        and parent.address_alignments == sibling.address_alignments
        and parent.memory_vector == sibling.memory_vector
        and parent.immediate_value == sibling.immediate_value
        and parent.immediate_ranges == sibling.immediate_ranges
        and parent.immediate_multiple_of == sibling.immediate_multiple_of
        and inherited_layouts == sibling_unextended_layouts
        and parent.operand_type_compatibilities ==
        sibling.operand_type_compatibilities
        and tuple(modifier for modifier in sibling.modifiers
                  if modifier.name != "cache_hint") == parent.modifiers
    )


class TensorCacheControlsTests(unittest.TestCase):
    """Require a one-to-one cache sibling without weakening parent identities."""

    @classmethod
    def setUpClass(cls) -> None:
        """Read packaged canonical metadata exactly as an installed client does."""

        cls.cp = next(instruction for instruction
                      in load_packaged_spec_database().instructions
                      if instruction.opcode == "cp")

    def _pairs(self) -> tuple[tuple[VariantSpec, VariantSpec], ...]:
        """Pair by typed controls and availability, never raw opcode dispatch."""

        parents = [variant for variant in self.cp.variants
                   if _is_tensor(variant) and not _has_cache_hint(variant)]
        siblings = [variant for variant in self.cp.variants
                    if _is_tensor(variant) and _has_cache_hint(variant)]
        self.assertEqual(len(parents), 178)
        self.assertEqual(sum(len(v.operand_layouts) for v in parents), 241)
        self.assertEqual(len(siblings), 178)
        self.assertEqual(sum(len(v.operand_layouts) for v in siblings), 482)
        self.assertEqual(len(self.cp.variants), 473)
        pairs = []
        for parent in parents:
            matches = [candidate for candidate in siblings
                       if _parent_matches(parent, candidate)]
            self.assertEqual(len(matches), 1, parent.name)
            pairs.append((parent, matches[0]))
        self.assertEqual(len({sibling.name for _, sibling in pairs}), 178)
        return tuple(pairs)

    def test_parent_bijection_and_inherited_target(self) -> None:
        """Every eligible tensor identity retains exact prior source contracts."""

        resolved = from_instruction_spec(self.cp)
        by_name = {variant.variant_id: variant for variant in resolved.variants}
        self.assertEqual(len(by_name), len(resolved.variants))
        for parent, sibling in self._pairs():
            with self.subTest(parent=parent.name):
                old = by_name[parent.name]
                new = by_name[sibling.name]
                self.assertEqual(new.tensor_access_mode,
                                 old.tensor_access_mode)
                self.assertEqual(new.tensor_destination,
                                 old.tensor_destination)
                old_rank = next(binding.expected_tensor_rank
                                for layout in old.operand_layouts
                                for binding in layout.bindings
                                if binding.tensor_access_mode is not None)
                new_rank = next(binding.expected_tensor_rank
                                for layout in new.operand_layouts
                                for binding in layout.bindings
                                if binding.tensor_access_mode is not None)
                self.assertEqual(new_rank, old_rank)
                self.assertEqual(new.tensor_reduction_op,
                                 old.tensor_reduction_op)
                self.assertEqual(new.tensor_multicast,
                                 old.tensor_multicast)
                self.assertEqual(new.tensor_cta_group_applicable,
                                 old.tensor_cta_group_applicable)
                self.assertEqual(new.availability, old.availability)
                self.assertEqual(len(new.operand_layouts),
                                 2 * len(old.operand_layouts))
                hint = [field for field in new.modifier_fields
                        if field.source_name == "cache_hint"]
                self.assertEqual(len(hint), 1)
                self.assertIs(hint[0].storage, ResolvedFieldStorage.INSTANCE)
                self.assertFalse(any(field.source_name == "cache_hint"
                                     for field in old.modifier_fields))

    def test_written_hint_and_policy_final_layouts(self) -> None:
        """The only added source states are hint-only and hint plus policy."""

        for parent, sibling in self._pairs():
            with self.subTest(parent=parent.name):
                hint = [modifier for modifier in sibling.modifiers
                        if modifier.name == "cache_hint"]
                self.assertEqual(len(hint), 1)
                self.assertIs(hint[0].kind, ModifierKind.FLAG)
                self.assertIs(hint[0].presence, ModifierPresence.REQUIRED)
                self.assertEqual(tuple(value.value for value in hint[0].values),
                                 (True,))
                self.assertEqual(hint[0].token, ".L2::cache_hint")
                self.assertEqual(sibling.modifiers[-1].name, "cache_hint")
                self.assertFalse(any(operand.name == "cache_policy"
                                     for layout in parent.operand_layouts
                                     for operand in layout.operands))
                for old_layout in parent.operand_layouts:
                    base_name = ("without_policy" if old_layout.name == "default"
                                 else old_layout.name)
                    policy_name = ("with_policy" if base_name == "without_policy"
                                   else base_name + "_policy")
                    matching = [layout for layout in sibling.operand_layouts
                                if layout.name in
                                {base_name, policy_name}]
                    self.assertEqual(len(matching), 2)
                    self.assertEqual(sum(layout.operands ==
                                         old_layout.operands
                                         for layout in matching), 1)
                    self.assertEqual(sum(len(layout.operands) ==
                                         len(old_layout.operands) + 1
                                         for layout in matching), 1)
                    for layout in matching:
                        self.assertEqual(layout.availability,
                                         old_layout.availability)
                        if len(layout.operands) == len(old_layout.operands):
                            continue
                        policy = layout.operands[-1]
                        self.assertEqual(policy.name, "cache_policy")
                        self.assertIs(policy.kind,
                                      OperandKind.REGISTER_OR_IMMEDIATE)
                        self.assertIs(policy.role, OperandRole.SOURCE)
                        self.assertIs(policy.access, OperandAccess.READ)
                        self.assertIs(policy.immediate_conversion_policy,
                                      OperandImmediateConversionPolicy.NARROW)
                        self.assertIsNotNone(policy.type_expression)
                        self.assertIs(policy.type_expression.kind,
                                      OperandTypeExpressionKind.FIXED_SCALAR)
                        self.assertEqual(policy.type_expression.scalar_type,
                                         "b64")

    def test_info_mask_group_order_and_equal_arity_shape(self) -> None:
        """Scalar policy and vector info cannot alias at equal operand count."""

        for parent, sibling in self._pairs():
            for layout in sibling.operand_layouts:
                names = tuple(operand.name for operand in layout.operands)
                if "cache_policy" in names:
                    self.assertEqual(names[-1], "cache_policy")
                if "im2col_info" in names and "cta_mask" in names:
                    self.assertLess(names.index("im2col_info"),
                                    names.index("cta_mask"))
                if "cta_mask" in names and "cache_policy" in names:
                    self.assertLess(names.index("cta_mask"),
                                    names.index("cache_policy"))
                if "im2col_info" in names and "cache_policy" in names:
                    self.assertLess(names.index("im2col_info"),
                                    names.index("cache_policy"))
            if len(parent.operand_layouts) != 2:
                continue
            absent = next(layout for layout in sibling.operand_layouts
                          if "im2col_info" not in
                          (operand.name for operand in layout.operands)
                          and layout.operands[-1].name == "cache_policy")
            present = next(layout for layout in sibling.operand_layouts
                           if "im2col_info" in
                           (operand.name for operand in layout.operands)
                           and layout.operands[-1].name != "cache_policy")
            self.assertEqual(len(absent.operands), len(present.operands))
            self.assertIs(absent.operands[-1].kind,
                          OperandKind.REGISTER_OR_IMMEDIATE)
            info = next(operand for operand in present.operands
                        if operand.name == "im2col_info")
            self.assertIs(info.kind, OperandKind.TENSOR_IM2COL_INFO)

    def test_malformed_hint_or_policy_metadata_is_rejected(self) -> None:
        """A sibling cannot silently turn policy into a fourth source state."""

        parent, sibling = self._pairs()[0]
        hint = next(modifier for modifier in sibling.modifiers
                    if modifier.name == "cache_hint")
        policy_layout = next(layout for layout in sibling.operand_layouts
                             if layout.operands[-1].name == "cache_policy")
        policy = policy_layout.operands[-1]
        damaged = (
            replace(sibling, modifiers=tuple(
                modifier for modifier in sibling.modifiers
                if modifier.name != "cache_hint")),
            replace(sibling, modifiers=tuple(
                replace(modifier, presence=ModifierPresence.FIXED,
                        value=True, values=())
                if modifier.name == "cache_hint" else modifier
                for modifier in sibling.modifiers)),
            replace(sibling, operand_layouts=tuple(
                replace(layout, operands=layout.operands[:-1] +
                        (replace(policy, kind=OperandKind.REGISTER),))
                if layout is policy_layout else layout
                for layout in sibling.operand_layouts)),
            replace(sibling, operand_layouts=tuple(
                replace(layout, operands=(policy,) + layout.operands[:-1])
                if layout is policy_layout else layout
                for layout in sibling.operand_layouts)),
            replace(sibling, modifiers=tuple(
                replace(modifier, token=".L2::wrong")
                if modifier.name == "cache_hint" else modifier
                for modifier in sibling.modifiers)),
            replace(sibling, modifiers=sibling.modifiers + (hint,)),
        )
        self.assertIsNotNone(hint)
        self.assertFalse(_has_cache_hint(parent))
        for candidate in damaged:
            with self.subTest(candidate=candidate.name,
                              modifiers=candidate.modifiers,
                              layouts=candidate.operand_layouts):
                with self.assertRaises(ValueError):
                    from_instruction_spec(replace(self.cp,
                                                  variants=(candidate,)))


if __name__ == "__main__":
    unittest.main()
