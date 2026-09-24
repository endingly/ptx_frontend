"""Contracts for the context-owned deterministic generator pipeline."""

from __future__ import annotations

from dataclasses import replace
import argparse
import ast
from contextlib import redirect_stdout
from io import StringIO
import json
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
from ptx_frontend.code_gen.emit.syntax_descriptors import (
    generate_syntax_descriptor_source,
)
from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.code_gen.plan import (
    GeneratedArtifact,
    GenerationPlan,
    GROUPED_SOURCE_CATEGORIES,
    _source_groups,
    _stable_opcode_bucket,
    build_generation_plan,
)
from ptx_frontend.ir.resolved_ir import ResolvedValueKind
from ptx_frontend.spec.database import (
    discover_codegen_category_inputs,
    load_codegen_database,
    load_codegen_database_from_files,
)

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
                wraps=__import__(
                    "ptx_frontend.ir.resolved_ir", fromlist=["from_instruction_spec"]
                ).from_instruction_spec,
            ) as lower,
            patch(
                "ptx_frontend.code_gen.context.with_cpp_backend_field_names",
                wraps=__import__(
                    "ptx_frontend.code_gen.resolved_field_names",
                    fromlist=["with_cpp_backend_field_names"],
                ).with_cpp_backend_field_names,
            ) as project,
        ):
            context = build_generation_context(self.database, self.backend)
            with tempfile.TemporaryDirectory() as directory:
                for artifact in build_generation_plan(
                    context, Path(directory)
                ).artifacts:
                    artifact.emit(context, output_path=artifact.path)
        self.assertEqual(len(context.instructions), len(self.database.instructions))
        self.assertEqual(lower.call_count, len(self.database.instructions))
        self.assertEqual(project.call_count, len(self.database.instructions))

    def test_entries_bind_source_category_and_resolved_model(self) -> None:
        context = build_generation_context(self.database, self.backend)
        arithmetic = next(
            entry
            for entry in context.entries
            if entry.specification.codegen_category == "arithmetic"
        )
        control_flow = next(
            entry
            for entry in context.entries
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
            self.assertIn(f"resolve<{arithmetic.resolved.cpp_name}>", category)
            self.assertNotIn(f"resolve<{control_flow.resolved.cpp_name}>", category)

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

    def test_entry_replace_rejects_same_opcode_with_different_cpp_type_identity(
        self,
    ) -> None:
        entry = build_generation_context(self.database, self.backend).entries[0]

        with self.assertRaisesRegex(ValueError, "mismatched C\\+\\+ type identities"):
            replace(entry, resolved=replace(entry.resolved, cpp_name="Mismatched"))

    def test_emitter_dependencies_follow_the_model_category_dispatch_boundary(
        self,
    ) -> None:
        emitter_dir = ROOT / "python/src/ptx_frontend/code_gen/emit"

        def imported_modules(name: str) -> set[str]:
            module = ast.parse((emitter_dir / name).read_text(encoding="utf-8"))
            return {
                node.module or ""
                for node in ast.walk(module)
                if isinstance(node, ast.ImportFrom)
            }

        self.assertNotIn("resolved_checker", imported_modules("resolved_resolver.py"))
        self.assertNotIn("resolved_dispatch", imported_modules("resolved_model.py"))
        self.assertNotIn("references", imported_modules("resolved_dispatch.py"))
        category_imports = imported_modules("category_source.py")
        self.assertIn("resolved_resolver", category_imports)
        self.assertIn("resolved_checker", category_imports)

    def test_plan_is_the_only_artifact_inventory(self) -> None:
        context = build_generation_context(self.database, self.backend)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "generated"
            plan = build_generation_plan(context, output)
            self.assertEqual(len(plan.paths), len(set(plan.paths)))
            self.assertTrue(
                all(
                    path.relative_to(output).parts[:3]
                    == ("public", "ptx_frontend", "resolved_ir")
                    for path in plan.paths
                    if path.relative_to(output).parts[0] == "public"
                )
            )
            self.assertEqual(
                plan.paths,
                build_generation_plan(context, output).paths,
            )
            for artifact in plan.artifacts:
                artifact.emit(context, output_path=artifact.path)
            self.assertEqual(
                {path.relative_to(output) for path in plan.paths},
                {
                    path.relative_to(output)
                    for path in output.rglob("*")
                    if path.is_file()
                },
            )

    def test_large_category_sources_use_fixed_groups_and_narrow_headers(self) -> None:
        context = build_generation_context(self.database, self.backend)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            plan = build_generation_plan(context, output)
            expected_names = {
                "arithmetic": ("bucket_0", "bucket_1", "bucket_2"),
                "data_movement": ("cvt", "ld", "bucket_0", "bucket_1"),
                "parallel_synchronization_and_communication": (
                    "mbarrier", "atom", "red", "residual"
                ),
            }
            self.assertEqual(set(expected_names), GROUPED_SOURCE_CATEGORIES)
            cpp_names = {
                entry.specification.opcode: entry.cpp_name for entry in context.entries
            }
            for category, names in expected_names.items():
                opcodes = tuple(
                    entry.specification.opcode
                    for entry in context.entries
                    if entry.specification.codegen_category == category
                )
                groups = _source_groups(category, opcodes)
                self.assertEqual(tuple(name for name, _ in groups), names)
                self.assertEqual(
                    sorted(opcode for _, members in groups for opcode in members),
                    sorted(opcodes),
                )
                source_artifacts = {
                    artifact.path.name: artifact
                    for artifact in plan.artifacts_for_category(category)
                    if artifact.path.suffix == ".cpp"
                    and artifact.path.name.startswith(f"resolved_ir_{category}_")
                }
                self.assertEqual(
                    set(source_artifacts),
                    {
                        f"resolved_ir_{category}_{name}.gen.cpp"
                        for name in names
                    },
                )
                self.assertNotIn(
                    output / f"private/resolved_ir_{category}.gen.cpp", plan.paths
                )
                for name, members in groups:
                    artifact = source_artifacts[
                        f"resolved_ir_{category}_{name}.gen.cpp"
                    ]
                    artifact.emit(context, output_path=artifact.path)
                    source = artifact.path.read_text(encoding="utf-8")
                    includes = [line for line in source.splitlines()
                                if line.startswith("#include <ptx_frontend/resolved_ir/model/")]
                    self.assertEqual(includes, [
                        f"#include <ptx_frontend/resolved_ir/model/{category}/{opcode}/{kind}.gen.hpp>"
                        for opcode in members for kind in ("checker", "resolution")
                    ])
                    for opcode in members:
                        self.assertIn(f"resolve<{cpp_names[opcode]}>", source)
                        self.assertIn(
                            f"CheckResult check<{cpp_names[opcode]}>", source
                        )
                    self.assertNotIn(
                        f"checker/{category}.gen.hpp", source
                    )
            self.assertEqual(_stable_opcode_bucket("add", 3), 2)
            self.assertEqual(_stable_opcode_bucket("sub", 3), 0)
            self.assertEqual(_stable_opcode_bucket("mul", 3), 1)
            self.assertEqual(_stable_opcode_bucket("mov", 2), 1)
            self.assertEqual(_stable_opcode_bucket("st", 2), 0)

    def test_opcode_headers_and_category_wrappers_have_stable_ownership(self) -> None:
        context = build_generation_context(self.database, self.backend)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            plan = build_generation_plan(context, output)
            public = output / "public/ptx_frontend/resolved_ir"
            categories = {
                entry.specification.codegen_category for entry in context.entries
            }
            self.assertEqual(len(context.entries), 92)
            for kind in ("model", "resolution", "checker"):
                leaves = [
                    path for path in plan.paths
                    if path.is_relative_to(public)
                    if path.relative_to(public).parts[-1] == f"{kind}.gen.hpp"
                    and len(path.relative_to(public).parts) == 4
                ]
                self.assertEqual(len(leaves), len(context.entries))

            for category in sorted(categories):
                opcodes = tuple(
                    entry.specification.opcode for entry in context.entries
                    if entry.specification.codegen_category == category
                )
                for kind in ("model", "resolution", "checker"):
                    wrapper_path = public / kind / f"{category}.gen.hpp"
                    wrapper = next(
                        artifact for artifact in plan.artifacts
                        if artifact.path == wrapper_path
                    )
                    wrapper.emit(context, output_path=wrapper.path)
                    includes = [line for line in wrapper.path.read_text().splitlines()
                                if line.startswith("#include")]
                    self.assertEqual(includes, [
                        f"#include <ptx_frontend/resolved_ir/model/{category}/{opcode}/{kind}.gen.hpp>"
                        for opcode in opcodes
                    ])

            for category, opcode, cpp_name in (
                ("arithmetic", "add", "Add"),
                ("data_movement", "cvt", "Cvt"),
                ("parallel_synchronization_and_communication", "mbarrier", "Mbarrier"),
                ("comparison_and_selection", "setp", "Setp"),
            ):
                for kind in ("model", "resolution", "checker"):
                    path = public / f"model/{category}/{opcode}/{kind}.gen.hpp"
                    artifact = next(item for item in plan.artifacts if item.path == path)
                    artifact.emit(context, output_path=path)
                    source = path.read_text()
                    self.assertIn("#pragma once", source)
                    if kind == "model":
                        self.assertIn(f"struct {cpp_name} {{", source)
                        self.assertIn(
                            f"visit_instruction_references(const {cpp_name}&", source
                        )
                    else:
                        self.assertIn(
                            f"model/{category}/{opcode}/model.gen.hpp", source
                        )
                        self.assertIn(f"<{cpp_name}>", source)

    def test_list_outputs_is_read_only_and_uses_the_plan(self) -> None:
        from ptx_frontend.code_gen import cli

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "not-created"
            previous = sys.argv
            try:
                sys.argv = [
                    "codegen",
                    "--spec-dir",
                    str(SPEC_DIR),
                    "--backend-spec",
                    str(BACKEND_SPEC),
                    "--output",
                    str(output),
                    "--list-outputs",
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
                [
                    str(path.resolve())
                    for path in build_generation_plan(context, output.resolve()).paths
                ],
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
                    "codegen",
                    "--spec-dir",
                    str(SPEC_DIR),
                    "--backend-spec",
                    str(BACKEND_SPEC),
                    "--output",
                    str(output),
                    "--list-outputs",
                ]
                with patch(
                    "ptx_frontend.code_gen.cli.format_file_inplace"
                ) as format_file:
                    with redirect_stdout(StringIO()):
                        cli.main()
            finally:
                sys.argv = previous
            self.assertEqual(legacy.read_text(encoding="utf-8"), "retain")
            format_file.assert_not_called()

    def test_formatted_artifact_preserves_mtime_after_formatting_equal_content(
        self,
    ) -> None:
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

    def test_obsolete_cleanup_preserves_active_outputs_and_manifest_cleanup_is_scoped(
        self,
    ) -> None:
        from ptx_frontend.code_gen import cli

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "generated"
            active = output / "private/resolved_ir_arithmetic_bucket_2.gen.cpp"
            retired_opcode = output / "private/resolved_ir_arithmetic_add.gen.cpp"
            active_leaf = output / "public/ptx_frontend/resolved_ir/model/arithmetic/add/model.gen.hpp"
            retired_category = output / "private/resolved_ir_arithmetic.gen.cpp"
            stale = output / "private/resolved_ir_legacy.gen.cpp"
            stale_nested = output / "public/ptx_frontend/resolved_ir/model/retired.gen.hpp"
            retired_leaf = output / "public/ptx_frontend/resolved_ir/model/arithmetic/retired/model.gen.hpp"
            retired_resolution = retired_leaf.with_name("resolution.gen.hpp")
            retired_checker = retired_leaf.with_name("checker.gen.hpp")
            unrelated = retired_leaf.with_name("notes.txt")
            old_public = output / "public/resolved_ir.gen.hpp"
            old_category = output / "public/resolved_ir/model/arithmetic.gen.hpp"
            active.parent.mkdir(parents=True)
            active.write_text("active", encoding="utf-8")
            retired_opcode.write_text("retired", encoding="utf-8")
            active_leaf.parent.mkdir(parents=True)
            active_leaf.write_text("active leaf", encoding="utf-8")
            retired_category.write_text("retired", encoding="utf-8")
            stale.write_text("stale", encoding="utf-8")
            stale_nested.parent.mkdir(parents=True, exist_ok=True)
            stale_nested.write_text("stale", encoding="utf-8")
            retired_leaf.parent.mkdir(parents=True)
            retired_leaf.write_text("retired", encoding="utf-8")
            retired_resolution.write_text("retired", encoding="utf-8")
            retired_checker.write_text("retired", encoding="utf-8")
            unrelated.write_text("preserve", encoding="utf-8")
            old_public.write_text("old layout", encoding="utf-8")
            old_category.parent.mkdir(parents=True)
            old_category.write_text("old layout", encoding="utf-8")
            (output / ".ptx_resolved_ir_outputs.txt").write_text(
                "private/resolved_ir_arithmetic.gen.cpp\n"
                "private/resolved_ir_arithmetic_bucket_2.gen.cpp\n"
                "public/ptx_frontend/resolved_ir/model/arithmetic/add/model.gen.hpp\n",
                encoding="utf-8",
            )
            cli.remove_obsolete_generated_files(output, (active, active_leaf))
            self.assertTrue(active.exists())
            self.assertTrue(active_leaf.exists())
            self.assertFalse(retired_category.exists())
            self.assertFalse(retired_opcode.exists())
            self.assertFalse(stale.exists())
            self.assertFalse(stale_nested.exists())
            self.assertFalse(retired_leaf.exists())
            self.assertFalse(retired_resolution.exists())
            self.assertFalse(retired_checker.exists())
            self.assertEqual(unrelated.read_text(encoding="utf-8"), "preserve")
            self.assertFalse(old_public.exists())
            self.assertFalse(old_category.exists())
            (output / ".ptx_resolved_ir_outputs.txt").write_text(
                "../outside.gen.hpp\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(ValueError, "outside its output root"):
                cli.read_output_manifest(output)

    def test_main_repairs_missing_active_output_without_touching_manifest_on_noop(
        self,
    ) -> None:
        from ptx_frontend.code_gen import cli

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            spec_dir = root / "spec"
            backend_spec = root / "backend.yaml"
            output = root / "generated"
            spec_dir.mkdir()
            backend_spec.write_text("backend\n", encoding="utf-8")
            active = output / "public/ptx_frontend/resolved_ir/model/arithmetic/add/model.gen.hpp"

            def emit(_context, *, output_path: Path) -> None:
                output_path.write_text("model\n", encoding="utf-8")

            plan = GenerationPlan(
                (
                    GeneratedArtifact(
                        active, emit  # pyright: ignore[reportArgumentType]
                    ),
                )
            )
            arguments = argparse.Namespace(
                spec_dir=spec_dir,
                output=output,
                backend_spec=backend_spec,
                spec_file=[],
                category=None,
                global_artifacts=False,
                list_outputs=False,
                describe_build=False,
            )
            with (
                patch(
                    "ptx_frontend.code_gen.cli.parse_arguments", return_value=arguments
                ),
                patch("ptx_frontend.code_gen.cli.load_codegen_database"),
                patch("ptx_frontend.code_gen.cli.load_cpp_backend"),
                patch(
                    "ptx_frontend.code_gen.cli.build_generation_context",
                    return_value=object(),
                ),
                patch(
                    "ptx_frontend.code_gen.cli.build_generation_plan", return_value=plan
                ),
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
            replace(
                self.database,
                instructions=(conflicting_instruction, *self.database.instructions[1:]),
            ),
            self.backend,
        )
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "not-created"
            with self.assertRaisesRegex(ValueError, "duplicate artifact paths"):
                build_generation_plan(context, output)
            self.assertFalse(output.exists())

    def test_context_rejects_binding_cpp_name_collision_before_emission(self) -> None:
        first, second = self.entries_with_colliding_source_projection()

        with self.assertRaisesRegex(
            ValueError, "multiple generation instruction bindings"
        ):
            GenerationContext(backend=self.backend, entries=(first, second))

    def test_context_rejects_unclassified_reference_payload_before_emission(
        self,
    ) -> None:
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
                    "codegen",
                    "--spec-dir",
                    str(SPEC_DIR),
                    "--backend-spec",
                    str(BACKEND_SPEC),
                    "--output",
                    str(output),
                ]
                with (
                    patch(
                        "ptx_frontend.code_gen.cli.load_codegen_database",
                        return_value=invalid_database,
                    ),
                    patch(
                        "ptx_frontend.code_gen.cli.format_file_inplace"
                    ) as format_file,
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
            leaf = Path("public/ptx_frontend/resolved_ir/model/arithmetic/add/model.gen.hpp")
            first_artifact = next(
                artifact for artifact in first_plan.artifacts
                if artifact.path.relative_to(root / "one") == leaf
            )
            second_artifact = next(
                artifact for artifact in second_plan.artifacts
                if artifact.path.relative_to(root / "two") == leaf
            )
            first_artifact.emit(first, output_path=first_path)
            second_artifact.emit(second, output_path=second_path)
            first_artifact.emit(first, output_path=third_path)
            self.assertNotIn("saturate_for_second_backend", first_path.read_text())
            self.assertIn("saturate_for_second_backend", second_path.read_text())
            self.assertNotIn("AlternateBackendBool", first_path.read_text())
            self.assertIn("AlternateBackendBool", second_path.read_text())
            self.assertEqual(first_path.read_bytes(), third_path.read_bytes())

    def test_generation_plan_partitions_global_and_category_artifacts(self) -> None:
        context = build_generation_context(self.database, self.backend)

        with tempfile.TemporaryDirectory() as directory:
            plan = build_generation_plan(context, Path(directory))

            grouped_paths = {artifact.path for artifact in plan.global_artifacts}

            categories = {
                entry.specification.codegen_category for entry in context.entries
            }

            for category in categories:
                category_artifacts = plan.artifacts_for_category(category)

                self.assertTrue(category_artifacts)
                self.assertTrue(
                    all(
                        artifact.category == category for artifact in category_artifacts
                    )
                )

                grouped_paths.update(artifact.path for artifact in category_artifacts)

            self.assertEqual(
                grouped_paths,
                set(plan.paths),
            )

    def test_category_only_context_emits_same_artifacts_as_full_context(self) -> None:
        full_context = build_generation_context(
            self.database,
            self.backend,
        )

        category_inputs = {
            group.category: group
            for group in discover_codegen_category_inputs(spec_dir=SPEC_DIR)
        }

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            full_plan = build_generation_plan(
                full_context,
                root / "full",
            )
            for category in sorted(category_inputs):
                group = category_inputs[category]
                category_database = load_codegen_database_from_files(
                    spec_files=group.spec_files,
                    category=category,
                )
                category_context = build_generation_context(
                    category_database,
                    self.backend,
                )
                category_plan = build_generation_plan(
                    category_context,
                    root / "category",
                )
                full_by_name = {
                    artifact.path.relative_to(root / "full"): artifact
                    for artifact in full_plan.artifacts_for_category(category)
                }
                category_by_name = {
                    artifact.path.relative_to(root / "category"): artifact
                    for artifact in category_plan.artifacts_for_category(category)
                }
                self.assertEqual(full_by_name.keys(), category_by_name.keys())

                for relative_path, full_artifact in full_by_name.items():
                    category_artifact = category_by_name[relative_path]
                    full_artifact.emit(
                        full_context,
                        output_path=full_artifact.path,
                    )
                    category_artifact.emit(
                        category_context,
                        output_path=category_artifact.path,
                    )
                    self.assertEqual(
                        full_artifact.path.read_bytes(),
                        category_artifact.path.read_bytes(),
                    )

    def test_describe_build_is_read_only(self) -> None:

        from ptx_frontend.code_gen import cli

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "not-created"

            previous = sys.argv
            try:
                sys.argv = [
                    "codegen",
                    "--spec-dir",
                    str(SPEC_DIR),
                    "--backend-spec",
                    str(BACKEND_SPEC),
                    "--output",
                    str(output),
                    "--describe-build",
                ]

                stdout = StringIO()

                with redirect_stdout(stdout):
                    cli.main()
            finally:
                sys.argv = previous

            description = json.loads(stdout.getvalue())

            self.assertFalse(output.exists())
            self.assertTrue(description["categories"])
            self.assertTrue(description["global_outputs"])
            self.assertTrue(description["all_outputs"])


if __name__ == "__main__":
    unittest.main()
