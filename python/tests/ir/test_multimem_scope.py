"""Preserve optional ordinary multimem scope through lowering and C++ emission."""

from dataclasses import replace
from pathlib import Path
import tempfile
import unittest

from ptx_frontend.code_gen.context import build_generation_context
from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.code_gen.emit.checker_descriptors import (
    _emit_variant_modifier_value_domain_descriptors,
)
from ptx_frontend.code_gen.emit.resolved_descriptors import _emit_resolved_descriptor_storage
from ptx_frontend.code_gen.emit.resolved_model import generate_resolved_opcode_header
from ptx_frontend.ir.resolved_ir import ResolvedValueKind, from_instruction_spec
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.resources import packaged_backend_spec


class MultimemScopeTests(unittest.TestCase):
    """Exercise all ordinary weak forms using the full packaged selector database."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the complete inventory so selector ambiguity remains observable."""

        cls.database = get_packaged_spec_database()
        cls.backend = load_cpp_backend(packaged_backend_spec())
        cls.spec = next(entry for entry in cls.database.instructions
                        if entry.opcode == "multimem")
        cls.resolved = from_instruction_spec(cls.spec)
        cls.indices = tuple(index for index, variant in enumerate(cls.spec.variants)
                            if variant.name.startswith(("multimem_ld_reduce_", "multimem_st_"))
                            and variant.name.endswith("_weak"))

    def test_all_weak_forms_keep_typed_omission_and_scope_domain(self) -> None:
        """Lower omitted semantics and scope without manufacturing explicit weak."""

        self.assertEqual(len(self.indices), 49)
        for index in self.indices:
            variant = self.resolved.variants[index]
            with self.subTest(variant=variant.variant_id):
                bindings = {binding.target_field_id: binding
                            for binding in variant.modifier_bindings}
                self.assertEqual(bindings["semantics"].default_value.value, "omitted")
                self.assertIs(bindings["semantics"].default_value.value_kind,
                              ResolvedValueKind.MEMORY_CONSISTENCY)
                self.assertEqual(bindings["scope"].default_value.value, "none")
                self.assertIs(bindings["scope"].default_value.value_kind,
                              ResolvedValueKind.MEMORY_SCOPE)
                fields = {field.name: field for field in variant.modifier_fields}
                self.assertIs(fields["scope"].value_kind, ResolvedValueKind.MEMORY_SCOPE)
                storage = _emit_resolved_descriptor_storage(
                    replace(self.resolved, variants=(variant,)), self.backend)
                self.assertIn("MemoryConsistency::Omitted", storage)
                self.assertIn("MemoryScope::None", storage)
                domains = {entry.value for entry in variant.modifier_value_domains
                           if entry.source_kind_id == "scope"}
                self.assertEqual(domains, {"none", "cta", "cluster", "gpu", "sys"})
                checker = _emit_variant_modifier_value_domain_descriptors(variant, self.backend)
                for scope in ("Cta", "Cluster", "Gpu", "Sys"):
                    self.assertIn("MemoryScope::" + scope, checker)

    def test_public_forms_emit_owned_scope_members(self) -> None:
        """Expose a typed source-located scope on every affected installed form."""

        context = build_generation_context(self.database, self.backend)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "multimem.gen.hpp"
            generate_resolved_opcode_header(
                context, category=self.spec.codegen_category, opcode="multimem",
                output_path=path, form_indices=self.indices)
            header = path.read_text(encoding="utf-8")
        self.assertEqual(header.count("WithLocs<MemoryScope> scope;"), 49)


if __name__ == "__main__":
    unittest.main()
