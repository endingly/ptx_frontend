"""Pin half FMA defaults, closed modifier products and generated instance storage."""
import unittest
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import ModifierPresence
from ptx_frontend.ir.resolved_ir import from_instruction_spec, ResolvedFieldStorage

HALF_VARIANTS = ("fma_rn_f16", "fma_rn_f16x2", "fma_half_relu", "fma_half_oob",
                 "fma_half_oob_relu", "fma_bf16", "fma_bf16x2", "fma_bf16_oob",
                 "fma_bf16x2_oob")

class HalfFmaModifierTests(unittest.TestCase):
    """Only nine RN controls and the OOB SAT permission change their public shape."""

    def test_closed_products_and_instance_defaults(self) -> None:
        """Forty-eight half products coexist with the unchanged forty-four others."""
        spec = next(i for i in get_packaged_spec_database().instructions if i.opcode == "fma")
        resolved = from_instruction_spec(spec)
        totals = {True: 0, False: 0}
        for variant in spec.variants:
            half = variant.name in HALF_VARIANTS
            count = 1
            for modifier in variant.modifiers:
                count *= (1 if modifier.presence in (ModifierPresence.ABSENT, ModifierPresence.FIXED)
                          else (len(modifier.values) or 1) + (modifier.presence is ModifierPresence.OPTIONAL))
            totals[half] += count
            if not half:
                continue
            rounding = next(m for m in variant.modifiers if m.name == "rounding")
            self.assertIs(rounding.presence, ModifierPresence.OPTIONAL)
            self.assertEqual(rounding.default, "rn")
            self.assertEqual([v.value for v in rounding.values], ["rn"])
            self.assertIsNone(rounding.value)
            emitted = next(v for v in resolved.variants if v.variant_id == variant.name)
            field = next(f for f in emitted.modifier_fields if f.name == "rounding")
            self.assertIs(field.storage, ResolvedFieldStorage.INSTANCE)
            self.assertEqual(field.value_kind.value, "RoundingMode")
            binding = next(b for b in emitted.modifier_bindings if b.source_kind_id == "rounding")
            self.assertEqual(binding.default_value.value, "rn")
            self.assertTrue(all(b.allowed_shapes[0].value == "Register" for b in emitted.operand_layouts[0].bindings))
            if variant.name == "fma_half_oob":
                self.assertIs(next(m for m in variant.modifiers if m.name == "sat").presence, ModifierPresence.ABSENT)
                self.assertNotIn("sat", [f.name for f in emitted.modifier_fields])
        self.assertEqual(totals, {True: 48, False: 44})
        self.assertEqual(len(spec.variants), 16)

if __name__ == "__main__":
    unittest.main()
