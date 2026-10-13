"""Regression coverage for distinct implicit and explicit floating MAD forms."""

import unittest

from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import ModifierPresence, OperandKind, OperandRegisterWidthPolicy
from ptx_frontend.ir.resolved_ir import from_instruction_spec
from ptx_frontend.code_gen.emit.availability import emit_availability


class MadCompletenessTests(unittest.TestCase):
    """Keep floating source rounding distinct from integer and carry forms."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the canonical MAD variants from the packaged specification."""

        instruction = next(
            item
            for item in get_packaged_spec_database().instructions
            if item.opcode == "mad"
        )
        cls.variants = {variant.name: variant for variant in instruction.variants}
        cls.resolved = from_instruction_spec(instruction)

    def test_implicit_rounding_has_exact_legacy_targets_and_no_rounding_field(self) -> None:
        """Preserve omission as absence through canonical and resolved identities."""
        variant = self.variants["mad_implicit_f32"]
        expected = {"any_of": [{"ptx": "1.0", "target": f"sm_{sm}"}
                               for sm in (10, 11, 12, 13)]}
        self.assertEqual(variant.availability, expected)
        modifiers = {modifier.name: modifier for modifier in variant.modifiers}
        self.assertIs(modifiers["rounding"].presence, ModifierPresence.ABSENT)
        self.assertIsNone(modifiers["rounding"].value)
        self.assertEqual(modifiers["type"].value, "f32")
        for name in ("ftz", "sat"):
            self.assertIs(modifiers[name].presence, ModifierPresence.OPTIONAL)
            self.assertIs(modifiers[name].default, False)
        resolved = next(item for item in self.resolved.variants
                        if item.variant_id == "mad_implicit_f32")
        self.assertEqual(resolved.cpp_name, "ImplicitF32")
        self.assertEqual(dict(resolved.availability), expected)
        self.assertEqual([field.source_name for field in resolved.modifier_fields],
                         ["ftz", "sat", "type"])
        emitted = emit_availability(dict(resolved.availability))
        self.assertIn(".any_of_count = 4", emitted)
        self.assertEqual(emitted.count(".minimum_ptx_version = {1, 0}"), 4)
        self.assertEqual(emitted.count(".has_exact_target = true"), 4)
        for sm in (10, 11, 12, 13):
            self.assertIn(f".exact_target_architecture = {{{sm}}}", emitted)

    def test_implicit_form_reuses_explicit_fp32_ternary_operand_contract(self) -> None:
        """Keep all four type/width/shape policies identical to explicit FP32 MAD."""
        implicit = self.variants["mad_implicit_f32"]
        explicit = self.variants["mad_rn_f32"]
        self.assertEqual(implicit.operand_layouts, explicit.operand_layouts)
        self.assertEqual(len(implicit.operand_layouts), 1)
        self.assertEqual(len(implicit.operand_layouts[0].operands), 4)

    def test_models_all_explicit_rounding_and_preserves_integer_variants(self) -> None:
        """Require two FP cohorts while retaining the existing integer and carry forms."""

        self.assertTrue(
            {
                "mad_rn_f32",
                "mad_directed_f32",
                "mad_rn_f64",
                "mad_directed_f64",
                "mad_lo_u32",
                "mad_hi_cc_32",
            }.issubset(self.variants)
        )
        for name in ("mad_rn_f32", "mad_directed_f32"):
            self.assertEqual(dict(self.variants[name].availability), {"ptx": "2.0", "sm": 20})
        for name in ("mad_rn_f64", "mad_directed_f64"):
            self.assertEqual(dict(self.variants[name].availability), {"ptx": "1.0", "sm": 13})

    def test_limits_modifiers_and_float_operand_shapes(self) -> None:
        """Enforce required rounding, FP32-only FTZ/saturation, and float immediates."""

        for name, rounding_values in (
            ("mad_rn_f32", ()),
            ("mad_directed_f32", ("rz", "rm", "rp")),
            ("mad_rn_f64", ()),
            ("mad_directed_f64", ("rz", "rm", "rp")),
        ):
            variant = self.variants[name]
            modifiers = {modifier.name: modifier for modifier in variant.modifiers}
            rounding = modifiers["rounding"]
            if rounding_values:
                self.assertIs(rounding.presence, ModifierPresence.REQUIRED)
                self.assertEqual(tuple(value.value for value in rounding.values), rounding_values)
            else:
                self.assertIs(rounding.presence, ModifierPresence.FIXED)
                self.assertEqual(rounding.value, "rn")
            bindings = variant.operand_layouts[0].operands
            self.assertEqual(bindings[0].kind, OperandKind.REGISTER)
            self.assertEqual(
                tuple(binding.kind for binding in bindings[1:]),
                (OperandKind.REGISTER_OR_IMMEDIATE,) * 3,
            )
            self.assertEqual(
                tuple(binding.register_width_policy for binding in bindings),
                (OperandRegisterWidthPolicy.SAME_WIDTH,) * 4,
            )

        for name in ("mad_rn_f32", "mad_directed_f32"):
            modifiers = {modifier.name: modifier for modifier in self.variants[name].modifiers}
            self.assertIs(modifiers["ftz"].presence, ModifierPresence.OPTIONAL)
            self.assertIs(modifiers["sat"].presence, ModifierPresence.OPTIONAL)
        for name in ("mad_rn_f64", "mad_directed_f64"):
            self.assertEqual(
                {modifier.name for modifier in self.variants[name].modifiers},
                {"rounding", "type"},
            )


if __name__ == "__main__":
    unittest.main()
