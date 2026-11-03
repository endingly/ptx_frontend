"""Pin the six optional FTZ permissions and their generated instance fields."""
import unittest
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import ModifierPresence
from ptx_frontend.ir.resolved_ir import from_instruction_spec, ResolvedValueKind, ResolvedFieldStorage

FTZ_VARIANTS = ("set_half_f16_bit", "set_half_f16_bit_boolean",
                "set_half_f16_integer", "set_half_f16_integer_boolean",
                "set_half_f16_f64", "set_half_f16_f64_boolean")

class HalfSetFtzTests(unittest.TestCase):
    """Optional flags retain position, defaults, source domains and public storage."""

    def test_six_optional_fields_and_exact_domains(self) -> None:
        """The existing modifier slot becomes one owned Bool field per class."""
        spec = next(i for i in get_packaged_spec_database().instructions if i.opcode == "set")
        resolved = from_instruction_spec(spec)
        products = 0
        for name in FTZ_VARIANTS:
            variant = next(v for v in spec.variants if v.name == name)
            self.assertEqual([m.name for m in variant.modifiers],
                             ["comparison", "boolean", "ftz", "dtype", "stype"])
            controls = {m.name: m for m in variant.modifiers}
            self.assertIs(controls["ftz"].presence, ModifierPresence.OPTIONAL)
            self.assertIs(controls["ftz"].default, False)
            self.assertEqual(controls["ftz"].token, ".ftz")
            self.assertEqual(variant.availability, {"ptx": "4.2", "sm": 53})
            count = lambda m: len(m.values) if m.values else 1
            products += count(controls["comparison"]) * count(controls["stype"]) * count(controls["boolean"])
            emitted = next(v for v in resolved.variants if v.variant_id == name)
            field = next(f for f in emitted.modifier_fields if f.name == "ftz")
            self.assertIs(field.value_kind, ResolvedValueKind.BOOL)
            self.assertIs(field.storage, ResolvedFieldStorage.INSTANCE)
            binding = next(b for b in emitted.modifier_bindings if b.source_kind_id == "ftz")
            self.assertIs(binding.default_value.value, False)
        self.assertEqual(products, 224)

if __name__ == "__main__":
    unittest.main()
