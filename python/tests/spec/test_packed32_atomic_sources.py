"""Pin the eight packed atomic source keys without changing adjacent role policies."""
from pathlib import Path
import re
import tempfile
import unittest

from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.ir.resolved_ir import (
    from_instruction_spec, ResolvedOperandShape, ResolvedRegisterWidthPolicy,
    ResolvedValueKind,
)
from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.code_gen.resolved_field_names import field_cpp_type


def packed_atomic_backend():
    """Use this test source tree's explicit backend rather than an installed fallback."""
    path = Path(__file__).resolve().parents[3] / "python/src/ptx_frontend/spec/resources/ptx_cpp_backend_spec/ptx_frontend.yaml"
    return load_cpp_backend(path)


class Packed32AtomicSourceTests(unittest.TestCase):
    """Exercise canonical, derived, and generated projections for the same closed set."""

    def test_exact_eight_source_keys_and_four_members(self) -> None:
        """Only these source slots acquire the existing register/immediate union."""
        database = get_packaged_spec_database()
        backend = packed_atomic_backend()
        raw = {instruction.opcode: instruction for instruction in database.instructions}
        keys = set()
        members = set()
        for opcode in ("atom", "red"):
            variants = {v.name: v for v in raw[opcode].variants}
            derived = {v.variant_id: v for v in from_instruction_spec(raw[opcode]).variants}
            for scalar, ptx, sm in (("f16x2", "6.2", 60), ("bf16x2", "7.8", 90)):
                name = f"{opcode}_global_add_noftz_{scalar}"
                variant = variants[name]
                generated = derived[name]
                self.assertEqual(dict(generated.availability), {"ptx": ptx, "sm": sm})
                self.assertEqual(tuple(l.name for l in variant.operand_layouts),
                                 ("no_hint", "with_policy"))
                for layout in variant.operand_layouts:
                    with self.subTest(key=(opcode, name, layout.name, "src")):
                        operand = next(o for o in layout.operands if o.name == "src")
                        self.assertEqual(operand.kind.value, "reg_or_imm")
                        resolved = next(l for l in generated.operand_layouts
                                        if l.layout_id == layout.name)
                        source = next(b for b in resolved.bindings
                                      if b.target_field_id == "src")
                        self.assertEqual(source.type_expression.scalar_type, scalar)
                        self.assertEqual(source.register_width_policy,
                                         ResolvedRegisterWidthPolicy.SAME_WIDTH)
                        self.assertEqual(source.allowed_shapes,
                                         (ResolvedOperandShape.REGISTER,
                                          ResolvedOperandShape.IMMEDIATE))
                        field = next(f for f in resolved.fields if f.name == "src")
                        self.assertIs(field.value_kind, ResolvedValueKind.REG_OR_IMM)
                        self.assertEqual(field_cpp_type(field, backend=backend),
                                         "WithLocs<RegOrImm>")
                        if layout.name == "with_policy":
                            self.assertEqual(dict(resolved.availability), {"ptx": "7.4", "sm": 80})
                            policy = next(b for b in resolved.bindings
                                          if b.target_field_id == "cache_policy")
                            self.assertEqual(policy.type_expression.scalar_type, "b64")
                            self.assertEqual(policy.allowed_shapes, (ResolvedOperandShape.REGISTER,))
                        if opcode == "atom":
                            dst = next(b for b in resolved.bindings if b.target_field_id == "dst")
                            self.assertEqual(dst.type_expression.scalar_type,
                                             "b32" if scalar == "bf16x2" else "f16x2")
                            self.assertEqual(dst.register_width_policy,
                                             ResolvedRegisterWidthPolicy.EXACT if scalar == "bf16x2"
                                             else ResolvedRegisterWidthPolicy.SAME_WIDTH)
                        keys.add((opcode, name, layout.name, "src"))
                        members.add((opcode, generated.cpp_name, "src"))
        self.assertEqual(len(keys), 8)
        self.assertEqual(len(members), 4)

    def test_wide_roles_preserved(self) -> None:
        """Keep the unrelated wide CAS and EXCH register-only slots exact."""
        raw = {i.opcode: i for i in get_packaged_spec_database().instructions}
        for variant_name, fields in (("atom_global_cas_b128", ("compare", "swap")),
                                     ("atom_global_exch_b128", ("src",))):
            variant = next(v for v in raw["atom"].variants if v.name == variant_name)
            for layout in variant.operand_layouts:
                for field in fields:
                    self.assertEqual(next(o for o in layout.operands if o.name == field).kind.value, "reg")

    def test_generated_public_members_and_contexts(self) -> None:
        """Render the existing four classes; no new class or payload alternative is needed."""
        from ptx_frontend.code_gen.database import get_packaged_spec_database as codegen_database
        from ptx_frontend.code_gen.context import build_generation_context
        from ptx_frontend.code_gen.emit.resolved_model import generate_resolved_opcode_header

        context = build_generation_context(codegen_database(), packed_atomic_backend())
        with tempfile.TemporaryDirectory() as directory:
            for opcode, prefix in (("atom", "Atom"), ("red", "Red")):
                output = Path(directory) / f"{opcode}.hpp"
                generate_resolved_opcode_header(
                    context, category="parallel_synchronization_and_communication",
                    opcode=opcode, output_path=output,
                )
                text = output.read_text(encoding="utf-8")
                for suffix in ("F16x2", "Bf16x2"):
                    match = re.search(r"class " + prefix + "GlobalAddNoftz" + suffix
                                      + r"\b.*?\n};", text, re.DOTALL)
                    self.assertIsNotNone(match)
                    assert match is not None
                    self.assertIn("WithLocs<RegOrImm> src;", match.group())
                    self.assertNotIn("WithLocs<ResolvedRegisterRef> src;", match.group())


if __name__ == "__main__":
    unittest.main()
