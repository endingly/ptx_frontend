"""Contracts for the context-owned deterministic generator pipeline."""

from __future__ import annotations

from dataclasses import replace
import argparse
import ast
from contextlib import redirect_stdout
from io import StringIO
import sys
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import patch
from typing import cast

from ptx_frontend.code_gen.context import (
    GenerationContext,
    GenerationInstruction,
    build_generation_context,
)
from ptx_frontend.code_gen.emit.category_source import (
    generate_resolved_ir_category_source,
)
from ptx_frontend.code_gen.emit.syntax_descriptors import generate_syntax_descriptor_source
from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.code_gen.plan import (
    GeneratedArtifact,
    GenerationPlan,
    build_generation_plan,
)
from ptx_frontend.ir.resolved_ir import ResolvedValueKind
from ptx_frontend.spec.database import load_codegen_database


ROOT = Path(__file__).resolve().parents[3]
SPEC_DIR = ROOT / "instructions/ptx_spec"
BACKEND_SPEC = ROOT / "instructions/ptx_cpp_backend_spec/ptx_frontend.yaml"


class GenerationPlanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.database = load_codegen_database(spec_dir=SPEC_DIR)
        cls.backend = load_cpp_backend(BACKEND_SPEC)

    def test_context_lowers_and_projects_each_instruction_once(self) -> None:
        with (
            patch(
                "ptx_frontend.code_gen.context.from_instruction_spec",
                wraps=__import__("ptx_frontend.ir.resolved_ir", fromlist=["from_instruction_spec"]).from_instruction_spec,
            ) as lower,
            patch(
                "ptx_frontend.code_gen.context.with_cpp_backend_field_names",
                wraps=__import__("ptx_frontend.code_gen.resolved_field_names", fromlist=["with_cpp_backend_field_names"]).with_cpp_backend_field_names,
            ) as project,
        ):
            context = build_generation_context(self.database, self.backend)
            with tempfile.TemporaryDirectory() as directory:
                for artifact in build_generation_plan(context, Path(directory)).artifacts:
                    artifact.emit(context, output_path=artifact.path)
        self.assertEqual(len(context.instructions), len(self.database.instructions))
        self.assertEqual(lower.call_count, len(self.database.instructions))
        self.assertEqual(project.call_count, len(self.database.instructions))

    def test_entries_bind_source_category_and_resolved_model(self) -> None:
        context = build_generation_context(self.database, self.backend)
        arithmetic = next(
            entry for entry in context.entries
            if entry.specification.codegen_category == "arithmetic"
        )
        control_flow = next(
            entry for entry in context.entries
            if entry.specification.codegen_category == "control_flow"
        )
        reordered = GenerationContext(
            backend=self.backend,
            entries=(control_flow, arithmetic),
        )

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            syntax_path = output / "syntax.cpp"
            category_path = output / "arithmetic.cpp"
            generate_syntax_descriptor_source(
                reordered, category="arithmetic", output_path=syntax_path
            )
            generate_resolved_ir_category_source(
                reordered, category="arithmetic", output_path=category_path
            )

            syntax = syntax_path.read_text(encoding="utf-8")
            category = category_path.read_text(encoding="utf-8")
            self.assertIn(f'Opcode_name = "{arithmetic.specification.opcode}"', syntax)
            self.assertNotIn(
                f'Opcode_name = "{control_flow.specification.opcode}"', syntax
            )
            self.assertIn(
                f"resolve<{arithmetic.resolved.cpp_name}>", category
            )
            self.assertNotIn(
                f"resolve<{control_flow.resolved.cpp_name}>", category
            )

    def test_entry_rejects_resolved_model_from_another_opcode(self) -> None:
        context = build_generation_context(self.database, self.backend)
        first, second = context.entries[:2]
        with self.assertRaisesRegex(ValueError, "mismatched opcodes"):
            GenerationInstruction(
                specification=first.specification,
                resolved=second.resolved,
            )

    def test_entry_rejects_same_opcode_with_different_cpp_type_identity(self) -> None:
        entry = build_generation_context(self.database, self.backend).entries[0]
        mismatched_resolved = replace(entry.resolved, cpp_name="Mismatched")

        with self.assertRaisesRegex(ValueError, "mismatched C\\+\\+ type identities"):
            GenerationInstruction(
                specification=entry.specification,
                resolved=mismatched_resolved,
            )

    def test_entry_replace_rejects_same_opcode_with_different_cpp_type_identity(self) -> None:
        entry = build_generation_context(self.database, self.backend).entries[0]

        with self.assertRaisesRegex(ValueError, "mismatched C\\+\\+ type identities"):
            replace(entry, resolved=replace(entry.resolved, cpp_name="Mismatched"))

    def test_emitter_dependencies_follow_the_model_category_dispatch_boundary(self) -> None:
        emitter_dir = ROOT / "python/src/ptx_frontend/code_gen/emit"

        def imported_modules(name: str) -> set[str]:
            module = ast.parse((emitter_dir / name).read_text(encoding="utf-8"))
            return {
                node.module or ""
                for node in ast.walk(module)
                if isinstance(node, ast.ImportFrom)
            }

        self.assertNotIn(
            "resolved_checker", imported_modules("resolved_resolver.py")
        )
        self.assertNotIn(
            "resolved_dispatch", imported_modules("resolved_model.py")
        )
        self.assertNotIn(
            "references", imported_modules("resolved_dispatch.py")
        )
        category_imports = imported_modules("category_source.py")
        self.assertIn("resolved_resolver", category_imports)
        self.assertIn("resolved_checker", category_imports)

    def test_plan_is_the_only_artifact_inventory(self) -> None:
        context = build_generation_context(self.database, self.backend)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "generated"
            plan = build_generation_plan(context, output)
            self.assertEqual(len(plan.paths), len(set(plan.paths)))
            self.assertEqual(
                plan.paths,
                build_generation_plan(context, output).paths,
            )
            for artifact in plan.artifacts:
                artifact.emit(context, output_path=artifact.path)
            self.assertEqual(
                {path.relative_to(output) for path in plan.paths},
                {path.relative_to(output) for path in output.rglob("*") if path.is_file()},
            )

    def test_list_outputs_is_read_only_and_uses_the_plan(self) -> None:
        from ptx_frontend.code_gen import cli

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "not-created"
            previous = sys.argv
            try:
                sys.argv = [
                    "codegen", "--spec-dir", str(SPEC_DIR), "--backend-spec",
                    str(BACKEND_SPEC), "--output", str(output), "--list-outputs",
                ]
                listed = StringIO()
                with redirect_stdout(listed):
                    cli.main()
            finally:
                sys.argv = previous
            self.assertFalse(output.exists())
            context = build_generation_context(self.database, self.backend)
            self.assertEqual(
                listed.getvalue().splitlines(),
                [str(path.resolve()) for path in build_generation_plan(context, output.resolve()).paths],
            )

    def test_list_outputs_preserves_legacy_files_and_skips_formatting(self) -> None:
        from ptx_frontend.code_gen import cli

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "generated"
            legacy = output / "private/resolved_ir_legacy.gen.cpp"
            legacy.parent.mkdir(parents=True)
            legacy.write_text("retain", encoding="utf-8")
            previous = sys.argv
            try:
                sys.argv = [
                    "codegen", "--spec-dir", str(SPEC_DIR), "--backend-spec",
                    str(BACKEND_SPEC), "--output", str(output), "--list-outputs",
                ]
                with patch("ptx_frontend.code_gen.cli.format_file_inplace") as format_file:
                    with redirect_stdout(StringIO()):
                        cli.main()
            finally:
                sys.argv = previous
            self.assertEqual(legacy.read_text(encoding="utf-8"), "retain")
            format_file.assert_not_called()

    def test_formatted_artifact_preserves_mtime_after_formatting_equal_content(self) -> None:
        from ptx_frontend.code_gen import cli

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "generated/public/model.gen.hpp"
            raw_contents = iter(("first raw candidate\n", "second raw candidate\n"))

            def emit(_context, *, output_path: Path) -> None:
                output_path.write_text(next(raw_contents), encoding="utf-8")

            def format_candidate(path: str) -> None:
                Path(path).write_text("formatted candidate\n", encoding="utf-8")

            with patch(
                "ptx_frontend.code_gen.cli.format_file_inplace",
                side_effect=format_candidate,
            ):
                cli.write_formatted_artifact(None, emit, output)
                first_mtime = output.stat().st_mtime_ns
                self.assertEqual(output.stat().st_mode & 0o777, 0o644)
                time.sleep(0.01)
                cli.write_formatted_artifact(None, emit, output)
            self.assertEqual(output.stat().st_mtime_ns, first_mtime)

    def test_formatted_artifact_preserves_existing_mode_when_replacing(self) -> None:
        from ptx_frontend.code_gen import cli

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "generated/public/model.gen.hpp"
            output.parent.mkdir(parents=True)
            output.write_text("old content\n", encoding="utf-8")
            output.chmod(0o640)

            def emit(_context, *, output_path: Path) -> None:
                output_path.write_text("new raw content\n", encoding="utf-8")

            with patch("ptx_frontend.code_gen.cli.format_file_inplace"):
                cli.write_formatted_artifact(None, emit, output)
            self.assertEqual(output.read_text(encoding="utf-8"), "new raw content\n")
            self.assertEqual(output.stat().st_mode & 0o777, 0o640)

    def test_obsolete_cleanup_preserves_active_outputs_and_manifest_cleanup_is_scoped(self) -> None:
        from ptx_frontend.code_gen import cli

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "generated"
            active = output / "private/resolved_ir_arithmetic.gen.cpp"
            stale = output / "private/resolved_ir_legacy.gen.cpp"
            stale_nested = output / "public/resolved_ir/model/retired.gen.hpp"
            active.parent.mkdir(parents=True)
            active.write_text("active", encoding="utf-8")
            stale.write_text("stale", encoding="utf-8")
            stale_nested.parent.mkdir(parents=True)
            stale_nested.write_text("stale", encoding="utf-8")
            (output / ".ptx_resolved_ir_outputs.txt").write_text(
                "private/resolved_ir_arithmetic.gen.cpp\n"
                "public/resolved_ir/model/retired.gen.hpp\n",
                encoding="utf-8",
            )
            cli.remove_obsolete_generated_files(output, (active,))
            self.assertTrue(active.exists())
            self.assertFalse(stale.exists())
            self.assertFalse(stale_nested.exists())
            (output / ".ptx_resolved_ir_outputs.txt").write_text(
                "../outside.gen.hpp\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(ValueError, "outside its output root"):
                cli.read_output_manifest(output)

    def test_main_repairs_missing_active_output_without_touching_manifest_on_noop(self) -> None:
        from ptx_frontend.code_gen import cli

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            spec_dir = root / "spec"
            backend_spec = root / "backend.yaml"
            output = root / "generated"
            spec_dir.mkdir()
            backend_spec.write_text("backend\n", encoding="utf-8")
            active = output / "public/resolved_ir/model/arithmetic.gen.hpp"

            def emit(_context, *, output_path: Path) -> None:
                output_path.write_text("model\n", encoding="utf-8")

            plan = GenerationPlan((GeneratedArtifact(active, emit),))
            arguments = argparse.Namespace(
                spec_dir=spec_dir,
                output=output,
                backend_spec=backend_spec,
                list_outputs=False,
            )
            with (
                patch("ptx_frontend.code_gen.cli.parse_arguments", return_value=arguments),
                patch("ptx_frontend.code_gen.cli.load_codegen_database"),
                patch("ptx_frontend.code_gen.cli.load_cpp_backend"),
                patch("ptx_frontend.code_gen.cli.build_generation_context", return_value=object()),
                patch("ptx_frontend.code_gen.cli.build_generation_plan", return_value=plan),
                patch("ptx_frontend.code_gen.cli.format_file_inplace"),
            ):
                cli.main()
                manifest = output / ".ptx_resolved_ir_outputs.txt"
                first_manifest_mtime = manifest.stat().st_mtime_ns
                time.sleep(0.01)
                cli.main()
                self.assertEqual(manifest.stat().st_mtime_ns, first_manifest_mtime)
                active.unlink()
                cli.main()
            self.assertEqual(active.read_text(encoding="utf-8"), "model\n")

    def test_plan_rejects_category_path_collision_before_writes(self) -> None:
        conflicting_instruction = replace(
            self.database.instructions[0], codegen_category="dispatch"
        )
        context = build_generation_context(
            replace(self.database, instructions=(
                conflicting_instruction, *self.database.instructions[1:]
            )),
            self.backend,
        )
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "not-created"
            with self.assertRaisesRegex(ValueError, "duplicate artifact paths"):
                build_generation_plan(context, output)
            self.assertFalse(output.exists())

    def test_context_rejects_binding_cpp_name_collision_before_emission(self) -> None:
        first, second = self.entries_with_colliding_source_projection()

        with self.assertRaisesRegex(ValueError, "multiple generation instruction bindings"):
            GenerationContext(backend=self.backend, entries=(first, second))

    def test_context_rejects_unclassified_reference_payload_before_emission(self) -> None:
        context = build_generation_context(self.database, self.backend)
        entry = context.entries[0]
        variant = entry.resolved.variants[0]
        layout = variant.operand_layouts[0]
        unknown_field = replace(
            layout.fields[0], value_kind=cast(ResolvedValueKind, object())
        )
        unknown_instruction = replace(
            entry.resolved,
            variants=(
                replace(
                    variant,
                    operand_layouts=(
                        replace(layout, fields=(unknown_field, *layout.fields[1:])),
                        *variant.operand_layouts[1:],
                    ),
                ),
                *entry.resolved.variants[1:],
            ),
        )

        with self.assertRaisesRegex(ValueError, "explicit module-reference policy"):
            GenerationContext(
                backend=self.backend,
                entries=(
                    GenerationInstruction(
                        specification=entry.specification,
                        resolved=unknown_instruction,
                    ),
                    *context.entries[1:],
                ),
            )

    def test_invalid_context_cli_has_no_filesystem_side_effects(self) -> None:
        from ptx_frontend.code_gen import cli

        first, second = self.instructions_with_colliding_source_projection()
        invalid_database = replace(
            self.database, instructions=(first, second, *self.database.instructions[2:])
        )
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "generated"
            legacy = output / "private/resolved_ir_legacy.gen.cpp"
            legacy.parent.mkdir(parents=True)
            legacy.write_text("retain", encoding="utf-8")
            previous = sys.argv
            try:
                sys.argv = [
                    "codegen", "--spec-dir", str(SPEC_DIR), "--backend-spec",
                    str(BACKEND_SPEC), "--output", str(output),
                ]
                with (
                    patch("ptx_frontend.code_gen.cli.load_codegen_database", return_value=invalid_database),
                    patch("ptx_frontend.code_gen.cli.format_file_inplace") as format_file,
                    self.assertRaisesRegex(
                        ValueError, "multiple generation instruction bindings"
                    ),
                ):
                    cli.main()
            finally:
                sys.argv = previous
            self.assertEqual(legacy.read_text(encoding="utf-8"), "retain")
            self.assertEqual(list(output.rglob("*.gen.*")), [legacy])
            format_file.assert_not_called()

    def entries_with_colliding_source_projection(
        self,
    ) -> tuple[GenerationInstruction, GenerationInstruction]:
        """Return valid bindings whose source C++ projections collide."""

        context = build_generation_context(self.database, self.backend)
        first, second = context.entries[:2]
        return (
            GenerationInstruction(
                specification=replace(first.specification, opcode="collision.name"),
                resolved=replace(
                    first.resolved, opcode="collision.name", cpp_name="CollisionName"
                ),
            ),
            GenerationInstruction(
                specification=replace(second.specification, opcode="collision_name"),
                resolved=replace(
                    second.resolved, opcode="collision_name", cpp_name="CollisionName"
                ),
            ),
        )

    def instructions_with_colliding_source_projection(self):
        """Return source instructions that lower and collide as C++ syntax types."""

        first, second = self.database.instructions[:2]

        def rename_opcode(instruction, opcode: str):
            return replace(
                instruction,
                opcode=opcode,
                variants=tuple(
                    replace(
                        variant,
                        name=variant.name.replace(
                            f"{instruction.opcode}_", f"{opcode}_", 1
                        ),
                    )
                    for variant in instruction.variants
                ),
            )

        return (
            rename_opcode(first, "collision.name"),
            rename_opcode(second, "collision_name"),
        )

    def test_contexts_do_not_share_backend_alias_projection(self) -> None:
        aliases = dict(self.backend.domains["modifier_field_names"].values)
        aliases["sat"] = "saturate_for_second_backend"
        domains = dict(self.backend.domains)
        domains["modifier_field_names"] = replace(
            self.backend.domains["modifier_field_names"], values=aliases
        )
        types = dict(self.backend.domains["resolved_value_cpp_types"].values)
        types["Bool"] = "AlternateBackendBool"
        domains["resolved_value_cpp_types"] = replace(
            self.backend.domains["resolved_value_cpp_types"], values=types
        )
        alternate = replace(self.backend, domains=domains)
        first = build_generation_context(self.database, self.backend)
        second = build_generation_context(self.database, alternate)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first_path = root / "first.hpp"
            second_path = root / "second.hpp"
            third_path = root / "third.hpp"
            first_plan = build_generation_plan(first, root / "one")
            second_plan = build_generation_plan(second, root / "two")
            first_plan.artifacts[1].emit(first, output_path=first_path)
            second_plan.artifacts[1].emit(second, output_path=second_path)
            first_plan.artifacts[1].emit(first, output_path=third_path)
            self.assertNotIn("saturate_for_second_backend", first_path.read_text())
            self.assertIn("saturate_for_second_backend", second_path.read_text())
            self.assertNotIn("AlternateBackendBool", first_path.read_text())
            self.assertIn("AlternateBackendBool", second_path.read_text())
            self.assertEqual(first_path.read_bytes(), third_path.read_bytes())


if __name__ == "__main__":
    unittest.main()
