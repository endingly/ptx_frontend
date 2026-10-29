"""Independent conservative index and generator identity contracts."""
from dataclasses import replace
from pathlib import Path
import tempfile
import re
import unittest

from ptx_frontend.code_gen.context import GenerationContext, build_generation_context
from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.code_gen.emit.resolved_source import generate_resolved_opcode_source
from ptx_frontend.code_gen.selection_index import modifier_candidate_rows
from ptx_frontend.code_gen.emit.syntax_descriptors import _cpp_string
from ptx_frontend.ir.syntax_ast import (
    ModifierPresence as Presence, SyntaxInstructionDescriptor as Instruction,
    SyntaxVariantDescriptor as Variant, SyntaxModifierDescriptor as Modifier,
    from_InstructionSpec,
)
from ptx_frontend.spec.database import get_packaged_spec_database


class SelectionIndexTests(unittest.TestCase):
    """Exercise Absent, aliases, repeated spellings, and real opcode emission."""

    def test_absent_and_alias_known_are_independent(self):
        """Canonical Absent spellings remain known, alias-only spellings do not."""
        required = Modifier("a", Presence.REQUIRED, (".same",))
        absent = Modifier("no", Presence.ABSENT, (".absent",))
        alias = Modifier("a", Presence.REQUIRED, (".alias",))
        descriptor = Instruction("custom", (
            Variant("custom_one", (required, absent), (), ((alias,),)),
            Variant("custom_two", (required, replace(required, kind_id="b")), ()),
        ))
        self.assertEqual(modifier_candidate_rows(descriptor), (
            (".absent", True, 0), (".alias", False, 1), (".same", True, 3)))

    def test_generated_catalogs_use_context_identity_and_exact_words(self):
        """Actual normalized catalogs emit one index in their existing opcode source."""
        root = Path(__file__).resolve().parents[3]
        context = build_generation_context(get_packaged_spec_database(), load_cpp_backend(
            root / "instructions/ptx_cpp_backend_spec/ptx_frontend.yaml"))
        # Existing normalized spellings render byte-identically with shared escaping.
        for entry in context.entries:
            for spelling, _, _ in modifier_candidate_rows(from_InstructionSpec(entry.specification)):
                self.assertEqual(_cpp_string(spelling), f'"{spelling}"')
        for opcode in ("mov", "wgmma", "add"):
            entry = next(e for e in context.entries if e.specification.opcode == opcode)
            syntax = from_InstructionSpec(entry.specification)
            rows = modifier_candidate_rows(syntax)
            for index, variant in enumerate(syntax.variants):
                for order in (variant.modifiers, *variant.modifier_order_aliases):
                    for modifier in order:
                        if modifier.presence is Presence.ABSENT:
                            continue
                        for spelling in modifier.allowed_spellings:
                            self.assertTrue(dict((s, m) for s, _, m in rows)[spelling] & (1 << index))
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / f"{opcode}.gen.cpp"
                generate_resolved_opcode_source(context, category=entry.specification.codegen_category,
                                               opcode=opcode, output_path=path)
                text = path.read_text()
            self.assertIn("detail::select_indexed_variant", text)
            self.assertNotIn("*selected ==", text)
            self.assertNotIn("auto fields = resolve_fields(", text)
            for variant in entry.resolved.variants:
                self.assertIn(f"== {context.identities.form_value(entry, variant)}u", text)
            emitted_rows = re.search(r"candidate_rows = \{\{(.*?)\}\};", text, re.S).group(1)
            self.assertEqual(re.findall(r'ModifierCandidateRow\{"([^"]+)", (true|false)\}', emitted_rows),
                             [(spelling, str(known).lower()) for spelling, known, _ in rows])
            emitted_words = re.search(r"candidate_words = \{\{(.*?)\}\};", text, re.S).group(1)
            word_count = (len(syntax.variants) + 63) // 64
            self.assertEqual(re.findall(r"0x[0-9a-f]+ULL", emitted_words), [
                f"0x{(mask >> (word * 64)) & ((1 << 64) - 1):016x}ULL"
                for _, _, mask in rows for word in range(word_count)])
            broken_entry = replace(entry, resolved=replace(
                entry.resolved, variants=tuple(reversed(entry.resolved.variants))))
            broken = GenerationContext(backend=context.backend, entries=(broken_entry,))
            with tempfile.TemporaryDirectory() as directory:
                with self.assertRaisesRegex(ValueError, "catalog order mismatch"):
                    generate_resolved_opcode_source(broken,
                        category=entry.specification.codegen_category, opcode=opcode,
                        output_path=Path(directory) / "rejected.gen.cpp")
