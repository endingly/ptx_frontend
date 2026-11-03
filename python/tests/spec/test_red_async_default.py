"""Pin the singleton-true async shared-completion default and generated fields."""
import unittest
from pathlib import Path
from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.code_gen.emit.operand_views import emit_check_modifier_value_view
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import ModifierPresence
from ptx_frontend.ir.resolved_ir import from_instruction_spec, ResolvedFieldStorage

class RedAsyncDefaultTests(unittest.TestCase):
    """Completion is an effective mechanism, with token locations carrying presence."""

    def test_twelve_singleton_true_defaults_and_release_separation(self) -> None:
        """Exactly twelve existing classes gain owned default-true Bool controls."""
        spec = next(i for i in get_packaged_spec_database().instructions if i.opcode == "red")
        resolved = from_instruction_spec(spec)
        backend = load_cpp_backend(Path(__file__).resolve().parents[3] / "instructions/ptx_cpp_backend_spec/ptx_frontend.yaml")
        shared = [v for v in spec.variants if v.name.startswith("red_async_shared_")]
        release = [v for v in spec.variants if v.name.startswith("red_async_release_")]
        self.assertEqual(len(shared), 12)
        self.assertEqual(len(release), 4)
        for variant in shared:
            completion = next(m for m in variant.modifiers if m.name == "completion")
            self.assertIs(completion.presence, ModifierPresence.OPTIONAL)
            self.assertIs(completion.default, True)
            self.assertEqual([v.value for v in completion.values], [True])
            self.assertIsNone(completion.value)
            self.assertEqual(completion.token, ".mbarrier::complete_tx::bytes")
            self.assertEqual(variant.availability, {"ptx": "8.1", "sm": 90})
            emitted = next(v for v in resolved.variants if v.variant_id == variant.name)
            field = next(f for f in emitted.modifier_fields if f.name == "completion")
            self.assertIs(field.storage, ResolvedFieldStorage.INSTANCE)
            self.assertEqual(field.value_kind.value, "Bool")
            view = emit_check_modifier_value_view(emitted, field, backend, 4,
                                                form_cpp_name="ExactShared", member_expr="completion")
            self.assertIn("!completion.locs.empty() || completion.value != true", view)
            self.assertIn(".bool_value = completion.value", view)
            binding = next(b for b in emitted.modifier_bindings if b.source_kind_id == "completion")
            self.assertIs(binding.default_value.value, True)
            domain = [d.value for d in emitted.modifier_value_domains if d.source_kind_id == "completion"]
            self.assertEqual(domain, [True])
            self.assertEqual([b.target_field_id for b in emitted.operand_layouts[0].bindings], ["address", "src", "mbarrier"])
        for variant in release:
            self.assertNotIn("completion", [m.name for m in variant.modifiers])
            self.assertEqual(variant.availability, {"ptx": "8.7", "sm": 100})

if __name__ == "__main__":
    unittest.main()
