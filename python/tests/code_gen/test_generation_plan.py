"""Contracts for the context-owned deterministic generator pipeline."""

from __future__ import annotations

from dataclasses import replace
import argparse
import ast
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
import json
import re
import shutil
import sys
import threading
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
from ptx_frontend.code_gen.emit.resolved_model import (
    REFERENCE_TYPES,
    form_shards,
    generate_resolved_opcode_header,
    method_name,
)
from ptx_frontend.code_gen.resolved_layout import (
    operand_slot_for_field,
    operand_slots,
)
from ptx_frontend.code_gen.emit.resolved_source import (
    _emit_resolve,
    generate_resolved_opcode_source,
)
from ptx_frontend.code_gen.emit.syntax_descriptors import (
    generate_syntax_descriptor_source,
)
from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.code_gen.model import ConditionCodeEffect
from ptx_frontend.code_gen.plan import (
    GeneratedArtifact,
    GenerationPlan,
    build_generation_plan,
)
from ptx_frontend.code_gen.reference_policy import REFERENCE_VALUE_KINDS
from ptx_frontend.code_gen.resolved_field_names import field_value_cpp_type
from ptx_frontend.ir.resolved_ir import ResolvedValueKind
from ptx_frontend.spec.database import (
    load_codegen_database,
    load_codegen_database_from_files,
    load_codegen_database_with_category_inputs,
)

ROOT = Path(__file__).resolve().parents[3]
SPEC_DIR = ROOT / "instructions/ptx_spec"
BACKEND_SPEC = ROOT / "instructions/ptx_cpp_backend_spec/ptx_frontend.yaml"
CONTROL_FLOW_SPEC = SPEC_DIR / "control_flow.yaml"


class GeneratorJobsTests(unittest.TestCase):
    """Exercise worker-count parsing and independent artifact writing."""

    def test_jobs_must_be_positive_integer(self) -> None:
        from ptx_frontend.code_gen import cli

        base = [
            "codegen",
            "--spec-dir",
            str(SPEC_DIR),
            "--backend-spec",
            str(BACKEND_SPEC),
            "--output",
            str(SPEC_DIR / "generated"),
        ]
        for value in ("0", "-1", "1.5", "many"):
            with self.subTest(value=value), patch.object(
                sys, "argv", base + ["--jobs", value]
            ):
                with redirect_stderr(StringIO()), self.assertRaises(SystemExit) as error:
                    cli.parse_arguments()
                self.assertEqual(error.exception.code, 2)
        with patch.object(sys, "argv", base):
            self.assertEqual(cli.parse_arguments().jobs, 6)
        with patch.object(sys, "argv", base + ["--jobs", "1"]):
            self.assertEqual(cli.parse_arguments().jobs, 1)

    def test_describe_build_discovers_real_spec_without_writing(self) -> None:
        """Exercise CLI discovery with one unpatched canonical spec file."""

        from ptx_frontend.code_gen import cli

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            spec_dir = root / "spec"
            spec_dir.mkdir()
            spec_file = spec_dir / CONTROL_FLOW_SPEC.name
            shutil.copy2(CONTROL_FLOW_SPEC, spec_file)
            output = root / "not-created"
            arguments = [
                "codegen",
                "--spec-dir", str(spec_dir),
                "--backend-spec", str(BACKEND_SPEC),
                "--output", str(output),
                "--describe-build",
            ]
            stdout = StringIO()
            with patch.object(sys, "argv", arguments), redirect_stdout(stdout):
                cli.main()

            description = json.loads(stdout.getvalue())
            self.assertFalse(output.exists())
            self.assertEqual(
                [group["name"] for group in description["categories"]],
                ["control_flow"],
            )
            self.assertEqual(
                description["categories"][0]["spec_files"],
                [str(spec_file.resolve())],
            )
            database = load_codegen_database(spec_dir=spec_dir)
            backend = load_cpp_backend(BACKEND_SPEC)
            expected_plan = build_generation_plan(
                build_generation_context(database, backend), output.resolve()
            )
            self.assertEqual(
                description["all_outputs"],
                [str(path) for path in expected_plan.paths],
            )
            self.assertEqual(
                description["global_outputs"],
                [str(artifact.path) for artifact in expected_plan.global_artifacts],
            )
            self.assertEqual(
                description["categories"][0]["outputs"],
                [
                    str(artifact.path)
                    for artifact in expected_plan.artifacts_for_category("control_flow")
                ],
            )

    def test_parallel_artifacts_match_serial_bytes_and_stable_manifest(self) -> None:
        from ptx_frontend.code_gen import cli

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            serial = root / "serial"
            parallel = root / "parallel"
            barrier = threading.Barrier(2)
            parallel_execution = False

            def emitter(index: int):
                """Build one deterministic test emitter."""

                def emit(_context, *, output_path: Path) -> None:
                    """Write one candidate after synchronizing parallel writers."""

                    if parallel_execution and index < 2:
                        barrier.wait(timeout=5)
                    output_path.write_text(f"artifact {index}\n", encoding="utf-8")
                return emit

            def artifacts(output: Path) -> tuple[GeneratedArtifact, ...]:
                """Plan identical artifact names below one output root."""

                return tuple(
                    GeneratedArtifact(
                        output / f"private/artifact_{index}.gen.cpp", emitter(index) # pyright: ignore[reportArgumentType]
                    )
                    for index in range(3)
                )

            with patch("ptx_frontend.code_gen.cli.format_file_inplace"):
                with patch("ptx_frontend.code_gen.cli.ThreadPoolExecutor") as executor:
                    cli.write_artifacts(None, artifacts(serial), 1)
                    executor.assert_not_called()
                parallel_execution = True
                cli.write_artifacts(None, artifacts(parallel), 2)

            cli.write_output_manifest(
                serial, tuple(item.path for item in artifacts(serial))
            )
            cli.write_output_manifest(
                parallel, tuple(item.path for item in artifacts(parallel))
            )
            self.assertEqual(
                (serial / ".ptx_resolved_ir_outputs.txt").read_bytes(),
                (parallel / ".ptx_resolved_ir_outputs.txt").read_bytes(),
            )
            for index in range(3):
                relative = f"private/artifact_{index}.gen.cpp"
                self.assertEqual(
                    (serial / relative).read_bytes(), (parallel / relative).read_bytes()
                )

    def test_failure_cancels_pending_work_joins_workers_and_skips_manifest(self) -> None:
        from ptx_frontend.code_gen import cli

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            spec_dir = root / "spec"
            spec_dir.mkdir()
            backend_spec = root / "backend.yaml"
            backend_spec.write_text("backend\n", encoding="utf-8")
            output = root / "generated"
            output.mkdir()
            manifest = output / ".ptx_resolved_ir_outputs.txt"
            manifest.write_text("previous\n", encoding="utf-8")
            running = threading.Event()
            release = threading.Event()
            def fail(_context, *, output_path: Path) -> None:
                """Fail after another worker starts its candidate."""

                self.assertTrue(running.wait(timeout=5))
                raise RuntimeError("emission failed")

            def finish(_context, *, output_path: Path) -> None:
                """Finish an in-flight candidate before the CLI returns."""

                running.set()
                self.assertTrue(release.wait(timeout=5))
                output_path.write_text("finished\n", encoding="utf-8")

            def queued(_context, *, output_path: Path) -> None:
                """Provide pending work for cancellation after failure."""

                output_path.write_text("unexpected\n", encoding="utf-8")

            plan = GenerationPlan(
                (
                    GeneratedArtifact(output / "private/failed.gen.cpp", fail), # pyright: ignore[reportArgumentType]
                    GeneratedArtifact(output / "private/running.gen.cpp", finish), # pyright: ignore[reportArgumentType]
                    GeneratedArtifact(output / "private/queued.gen.cpp", queued), # pyright: ignore[reportArgumentType]
                )
            )
            arguments = argparse.Namespace(
                spec_dir=spec_dir,
                backend_spec=backend_spec,
                output=output,
                spec_file=[],
                category=None,
                global_artifacts=False,
                list_outputs=False,
                describe_build=False,
                defer_finalization=False,
                jobs=2,
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
                timer = threading.Timer(0.2, release.set)
                timer.start()
                try:
                    with self.assertRaisesRegex(RuntimeError, "emission failed"):
                        cli.main()
                finally:
                    release.set()
                    timer.join()
            self.assertEqual(manifest.read_text(encoding="utf-8"), "previous\n")
            self.assertEqual((output / "private/running.gen.cpp").read_text(), "finished\n")
            self.assertFalse((output / "private/failed.gen.cpp").exists())
            self.assertEqual(list(output.rglob(".*.gen.cpp")), [])


class GenerationPlanTests(unittest.TestCase):
    """Exercise one real corpus snapshot and independent mutation probes."""
    @classmethod
    def setUpClass(cls) -> None:
        """Lower the immutable canonical corpus once for read-only assertions."""

        cls.database, cls.category_inputs = load_codegen_database_with_category_inputs(
            spec_dir=SPEC_DIR
        )
        cls.backend = load_cpp_backend(BACKEND_SPEC)
        cls.context = build_generation_context(cls.database, cls.backend)
        cls._full_tree: tempfile.TemporaryDirectory[str] | None = None
        cls._full_plan: GenerationPlan | None = None
        cls._full_output: Path | None = None
        cls.addClassCleanup(cls._release_full_generation)

    @classmethod
    def _release_full_generation(cls) -> None:
        """Release the class-owned output tree after all consumers finish."""

        if cls._full_tree is not None:
            cls._full_tree.cleanup()
        cls._full_tree = None
        cls._full_plan = None
        cls._full_output = None

    @classmethod
    def full_generation(cls) -> tuple[GenerationPlan, Path]:
        """Emit the unmodified full corpus once and lend its bytes read-only."""

        if cls._full_plan is None:
            cls._full_tree = tempfile.TemporaryDirectory()
            output = Path(cls._full_tree.name) / "generated"
            plan = build_generation_plan(cls.context, output)
            for artifact in plan.artifacts:
                artifact.emit(cls.context, output_path=artifact.path)
            cls._full_plan = plan
            cls._full_output = output
        assert cls._full_output is not None
        return cls._full_plan, cls._full_output

    def test_full_direct_class_corpus_and_layout_slots(self) -> None:
        """Plan every form once and keep overloaded fields typed by layout."""

        context = self.context
        forms = tuple(
            variant for entry in context.entries for variant in entry.resolved.variants
        )
        self.assertEqual((len(context.entries), len(forms)), (101, 4514))
        self.assertEqual(sum(len(form.operand_layouts) for form in forms), 5599)
        self.assertEqual(sum(len(form.operand_layouts) > 1 for form in forms), 897)
        for form in forms:
            slots = operand_slots(form, self.backend)
            self.assertEqual(len({slot.member_name for slot in slots}), len(slots))
            for index, layout in enumerate(form.operand_layouts):
                for field in layout.fields:
                    slot = operand_slot_for_field(slots, field, self.backend)
                    self.assertIn(index, slot.layout_indices)
                    self.assertEqual(
                        slot.optional,
                        len(slot.layout_indices) != len(form.operand_layouts),
                    )
        with tempfile.TemporaryDirectory() as directory:
            plan = build_generation_plan(context, Path(directory))
            shard_count = sum(len(form_shards(entry)) for entry in context.entries)
            self.assertEqual(shard_count, 59)
            self.assertEqual(len(plan.paths), 11 + 2 * len(context.entries)
                             + 3 * shard_count)
            self.assertTrue(all(path.name.endswith((".gen.cpp", ".gen.hpp"))
                                for path in plan.paths))

    def test_condition_code_effects_are_typed_and_variant_local(self) -> None:
        """Check all source variants, projected variants, and CC availability."""

        expected = {
            "add": {"cc": ConditionCodeEffect.CARRY_OUT},
            "addc": {"plain": ConditionCodeEffect.CARRY_IN,
                     "cc": ConditionCodeEffect.CARRY_IN_OUT},
            "sub": {"cc": ConditionCodeEffect.BORROW_OUT},
            "subc": {"plain": ConditionCodeEffect.BORROW_IN,
                     "cc": ConditionCodeEffect.BORROW_IN_OUT},
            "mad": {"hi_cc": ConditionCodeEffect.CARRY_OUT,
                    "lo_cc": ConditionCodeEffect.CARRY_OUT},
            "madc": {"hi_plain": ConditionCodeEffect.CARRY_IN,
                     "lo_plain": ConditionCodeEffect.CARRY_IN,
                     "hi_cc": ConditionCodeEffect.CARRY_IN_OUT,
                     "lo_cc": ConditionCodeEffect.CARRY_IN_OUT},
        }
        self.assertEqual(len(self.database.instructions), len(self.context.entries))
        for source, entry in zip(self.database.instructions, self.context.entries):
            self.assertIs(source, entry.specification)
            self.assertEqual(len(source.variants), len(entry.resolved.variants))
            expected_variant_effects = {
                f"{source.opcode}_{form}_{width}": effect
                for form, effect in expected.get(source.opcode, {}).items()
                for width in (32, 64)
            }
            for variant, generated in zip(source.variants, entry.resolved.variants):
                self.assertEqual(generated.variant_id, variant.name)
                self.assertIsInstance(variant.condition_code_effect, ConditionCodeEffect)
                self.assertIs(
                    generated.condition_code_effect,
                    variant.condition_code_effect,
                )
                self.assertIs(
                    variant.condition_code_effect,
                    expected_variant_effects.get(
                        variant.name, ConditionCodeEffect.NONE
                    ),
                )
            if source.opcode not in expected:
                continue
            variants = {variant.name: variant for variant in source.variants}
            for form, effect in expected[source.opcode].items():
                for width in (32, 64):
                    variant = variants[f"{source.opcode}_{form}_{width}"]
                    self.assertIs(variant.condition_code_effect, effect)
                    if source.opcode in {"mad", "madc"}:
                        availability = (
                            {"ptx": "3.0", "sm": 20} if width == 32
                            else {"ptx": "4.3", "sm": 20}
                        )
                    else:
                        availability = (
                            {"ptx": "1.2", "sm": 0} if width == 32
                            else {"ptx": "4.3", "sm": 20}
                        )
                    self.assertEqual(variant.availability, availability)

    def test_cp_direct_dispatch_names_every_generated_form(self) -> None:
        """Descriptor selection reaches all Cp forms beyond reflection limits."""
        context = self.context
        cp = next(
            entry for entry in context.entries
            if entry.specification.opcode == "cp"
        )
        resolver = _emit_resolve(cp, self.backend)
        self.assertGreater(len(cp.resolved.variants), 128)
        for variant in cp.resolved.variants:
            self.assertIn(
                f'if (*selected == "{variant.cpp_name}") '
                f'return InstructionKind::{cp.cpp_name}{variant.cpp_name};',
                resolver,
            )

    def test_context_lowers_and_projects_each_instruction_once(self) -> None:
        """Fresh lowering stays one-to-one while every artifact kind emits."""

        selected = tuple(
            instruction for instruction in self.database.instructions
            if instruction.opcode in {"add", "cp", "tcgen05"}
        )
        self.assertEqual({item.opcode for item in selected},
                         {"add", "cp", "tcgen05"})
        database = replace(self.database, instructions=selected)
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
            context = build_generation_context(database, self.backend)
            with tempfile.TemporaryDirectory() as directory:
                plan = build_generation_plan(context, Path(directory))
                self.assertTrue(plan.global_artifacts)
                self.assertTrue(any(form_shards(entry) for entry in context.entries))
                self.assertTrue(any(not form_shards(entry) for entry in context.entries))
                self.assertTrue(any("_forms_" in path.name for path in plan.paths))
                self.assertTrue(any("_methods_" in path.name for path in plan.paths))
                self.assertTrue(any("_descriptors_" in path.name for path in plan.paths))
                for artifact in plan.artifacts:
                    artifact.emit(context, output_path=artifact.path)
        self.assertEqual(len(context.instructions), len(selected))
        self.assertEqual(lower.call_count, len(selected))
        self.assertEqual(project.call_count, len(selected))

    def test_typed_observer_covers_current_reference_payloads(self) -> None:
        """Every reference kind has a fixed typed callback and module collector."""
        context = self.context
        collector = (
            ROOT / "submod/resolved_ir/src/ptx_module_availability.cpp"
        ).read_text()
        payload_types = {
            field_value_cpp_type(field, backend=self.backend)
            for instruction in context.instructions
            for variant in instruction.variants
            for layout in variant.operand_layouts
            for field in layout.fields
            if field.value_kind in REFERENCE_VALUE_KINDS
        }
        payload_types.add("ResolvedPredicate")
        self.assertTrue(payload_types <= set(REFERENCE_TYPES))
        for payload_type in REFERENCE_TYPES:
            callback = (
                rf"\bvoid\s+{re.escape(method_name(payload_type))}\s*\("
                rf"\s*const\s+{re.escape(payload_type)}\s*&"
            )
            self.assertIsNotNone(
                re.search(callback, collector),
                f"Missing typed callback for {payload_type}",
            )
        self.assertGreater(len(payload_types), 10)
        plan, output = self.full_generation()
        for entry in context.entries:
            source_path = output / (
                f"private/resolved_ir_{entry.specification.codegen_category}_"
                f"{entry.specification.opcode}.gen.cpp"
            )
            self.assertIn(source_path, plan.paths)
            source = source_path.read_text(encoding="utf-8")
            self.assertIn(f"resolve{entry.cpp_name}(", source)
            self.assertNotIn("OwnedInstruction", source)
            self.assertNotIn("box_instruction", source)

    def test_entries_bind_source_category_and_resolved_model(self) -> None:
        context = self.context
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
            generate_resolved_opcode_source(
                reordered, category="arithmetic",
                opcode=arithmetic.specification.opcode, output_path=category_path
            )

            syntax = syntax_path.read_text(encoding="utf-8")
            category = category_path.read_text(encoding="utf-8")
            self.assertIn(f'Opcode_name = "{arithmetic.specification.opcode}"', syntax)
            self.assertNotIn(
                f'Opcode_name = "{control_flow.specification.opcode}"', syntax
            )
            self.assertIn(f"resolve{arithmetic.resolved.cpp_name}(", category)
            self.assertNotIn(f"resolve{control_flow.resolved.cpp_name}(", category)

    def test_entry_rejects_resolved_model_from_another_opcode(self) -> None:
        context = self.context
        first, second = context.entries[:2]
        with self.assertRaisesRegex(ValueError, "mismatched opcodes"):
            GenerationInstruction(
                specification=first.specification,
                resolved=second.resolved,
            )

    def test_entry_rejects_same_opcode_with_different_cpp_type_identity(self) -> None:
        entry = self.context.entries[0]
        mismatched_resolved = replace(entry.resolved, cpp_name="Mismatched")

        with self.assertRaisesRegex(ValueError, "mismatched C\\+\\+ type identities"):
            GenerationInstruction(
                specification=entry.specification,
                resolved=mismatched_resolved,
            )

    def test_entry_replace_rejects_same_opcode_with_different_cpp_type_identity(
        self,
    ) -> None:
        entry = self.context.entries[0]

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

        model_imports = imported_modules("resolved_model.py")
        source_imports = imported_modules("resolved_source.py")
        dispatch_imports = imported_modules("resolved_dispatch.py")
        self.assertFalse(any(name.endswith("resolved_source") for name in model_imports))
        self.assertTrue(any(name.endswith("resolved_model") for name in source_imports))
        self.assertFalse(any(name.endswith("resolved_source") for name in dispatch_imports))
        self.assertFalse(any(name.endswith("category_source") for name in dispatch_imports))

    def test_plan_is_the_only_artifact_inventory(self) -> None:
        plan, output = self.full_generation()
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
            build_generation_plan(self.context, output).paths,
        )
        self.assertEqual(
            {path.relative_to(output) for path in plan.paths},
            {
                path.relative_to(output)
                for path in output.rglob("*")
                if path.is_file()
            },
        )

    def test_op_sources_own_exactly_one_binding_and_all_descriptors(self) -> None:
        context = self.context
        plan, output = self.full_generation()
        sources = {
            artifact.path: artifact for artifact in plan.artifacts
            if artifact.path.suffix == ".cpp"
            and artifact.path.name.startswith("resolved_ir_")
            and artifact.path.name != "resolved_ir_dispatch.gen.cpp"
        }
        opcode_paths = {
            output / f"private/resolved_ir_{entry.specification.codegen_category}_"
            f"{entry.specification.opcode}.gen.cpp"
            for entry in context.entries
        }
        self.assertTrue(opcode_paths <= sources.keys())
        for entry in context.entries:
            category = entry.specification.codegen_category
            opcode = entry.specification.opcode
            path = output / f"private/resolved_ir_{category}_{opcode}.gen.cpp"
            artifact = sources[path]
            self.assertEqual(artifact.category, category)
            source = path.read_text(encoding="utf-8")
            self.assertIn(f"model/{category}/{opcode}.gen.hpp", source)
            self.assertIn(f"resolve{entry.cpp_name}(", source)
            self.assertIn(f"{opcode}_syntax_descriptor()", source)
            self.assertIn(f"{opcode}_resolved_descriptor()", source)
            self.assertIn(f"{opcode}_checker_descriptor()", source)
            if form_shards(entry):
                for index, shard in enumerate(form_shards(entry)):
                    method_path = output / (
                        f"private/resolved_ir_{category}_{opcode}_"
                        f"methods_{index:03d}.gen.cpp"
                    )
                    self.assertIn(method_path, sources)
                    methods = method_path.read_text(encoding="utf-8")
                    for variant_index in shard:
                        variant = entry.resolved.variants[variant_index]
                        self.assertIn(
                            f"{entry.cpp_name}{variant.cpp_name}::check(", methods
                        )
            else:
                for variant in entry.resolved.variants:
                    self.assertIn(
                        f"{entry.cpp_name}{variant.cpp_name}::check(", source
                    )
            for other in context.entries:
                if other is not entry:
                    self.assertNotIn(f"resolve{other.cpp_name}(", source)

    def test_opcode_headers_have_stable_direct_class_ownership(self) -> None:
        context = self.context
        plan, output = self.full_generation()
        public = output / "public/ptx_frontend/resolved_ir"
        leaves = tuple(
            (entry, public / (
                f"model/{entry.specification.codegen_category}/"
                f"{entry.specification.opcode}.gen.hpp"
            ))
            for entry in context.entries
        )
        shard_headers = {
            public / "model" / entry.specification.codegen_category /
            f"{entry.specification.opcode}_forms_{index:03d}.gen.hpp"
            for entry in context.entries
            for index, _ in enumerate(form_shards(entry))
        }
        self.assertEqual(
            {path for path in plan.paths if path.is_relative_to(public / "model")},
            {path for _, path in leaves} | shard_headers,
        )
        self.assertIn(public / "ptx_instruction_base.gen.hpp", plan.paths)
        aggregate = public / "ptx_resolved_ir.gen.hpp"
        self.assertIn(aggregate, plan.paths)
        self.assertFalse(any("union" in path.name or "owner" in path.name
                             for path in plan.paths))
        artifacts = {artifact.path: artifact for artifact in plan.artifacts}
        aggregate_source = aggregate.read_text(encoding="utf-8")
        for entry, path in leaves:
            self.assertEqual(artifacts[path].category,
                             entry.specification.codegen_category)
            self.assertIn(
                f"model/{entry.specification.codegen_category}/"
                f"{entry.specification.opcode}.gen.hpp",
                aggregate_source,
            )
        for category, opcode, class_name in (
            ("arithmetic", "add", "AddIntegerNoSat"),
            ("data_movement", "cvt", "Cvt"),
            ("control_flow", "call", "CallDirect"),
            ("comparison_and_selection", "setp", "Setp"),
        ):
            leaf = public / f"model/{category}/{opcode}.gen.hpp"
            source = leaf.read_text(encoding="utf-8")
            self.assertIn(" final : public Instruction", source)
            if class_name != "Cvt" and class_name != "Setp":
                self.assertIn(f"class {class_name} final", source)
            self.assertNotIn("using Variant =", source)
            self.assertNotIn("struct Operands", source)
            self.assertNotIn("box_instruction", source)

    def test_new_opcode_changes_only_owned_leaves_and_aggregates(self) -> None:
        context = self.context
        category = context.entries[0].specification.codegen_category
        selected = tuple(
            entry for entry in context.entries
            if entry.specification.codegen_category == category
        )
        self.assertGreaterEqual(len(selected), 2)
        first = GenerationContext(backend=self.backend, entries=(selected[0],))
        expanded = GenerationContext(backend=self.backend, entries=selected[:2])
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            first_plan = build_generation_plan(first, output)
            expanded_plan = build_generation_plan(expanded, output)
            opcode = selected[0].specification.opcode
            local_paths = (
                output / f"private/resolved_ir_{category}_{opcode}.gen.cpp",
                output / f"public/ptx_frontend/resolved_ir/model/{category}/{opcode}.gen.hpp",
            )
            first_category_artifacts = {
                artifact.path: artifact
                for artifact in first_plan.artifacts_for_category(category)
            }
            expanded_category_artifacts = {
                artifact.path: artifact
                for artifact in expanded_plan.artifacts_for_category(category)
            }
            self.assertTrue(set(local_paths) <= first_category_artifacts.keys())
            self.assertTrue(set(local_paths) <= expanded_category_artifacts.keys())
            first_local = {
                path: first_category_artifacts[path] for path in local_paths
            }
            expanded_local = {
                path: expanded_category_artifacts[path] for path in local_paths
            }
            self.assertEqual(set(first_local), set(expanded_local))
            for path, artifact in first_local.items():
                artifact.emit(first, output_path=path)
                previous = path.read_bytes()
                expanded_local[path].emit(expanded, output_path=path)
                self.assertEqual(path.read_bytes(), previous)
            self.assertEqual(
                len(expanded_plan.paths) - len(first_plan.paths), 2
            )

    def test_list_outputs_is_read_only_and_uses_the_plan(self) -> None:
        """Cover absent and legacy output roots through the real category loader."""

        from ptx_frontend.code_gen import cli

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            spec_dir = root / "spec"
            spec_dir.mkdir()
            shutil.copy2(CONTROL_FLOW_SPEC, spec_dir / CONTROL_FLOW_SPEC.name)
            context = build_generation_context(
                load_codegen_database(spec_dir=spec_dir), self.backend
            )
            for existing in (False, True):
                with self.subTest(existing=existing):
                    output = root / ("generated" if existing else "not-created")
                    legacy = output / "private/resolved_ir_legacy.gen.cpp"
                    if existing:
                        legacy.parent.mkdir(parents=True)
                        legacy.write_text("retain", encoding="utf-8")
                    arguments = [
                        "codegen",
                        "--spec-dir",
                        str(spec_dir),
                        "--backend-spec",
                        str(BACKEND_SPEC),
                        "--output",
                        str(output),
                        "--list-outputs",
                    ]
                    listed = StringIO()
                    with (
                        patch.object(sys, "argv", arguments),
                        patch(
                            "ptx_frontend.code_gen.cli.format_file_inplace"
                        ) as format_file,
                        patch(
                            "ptx_frontend.code_gen.cli.remove_obsolete_generated_files"
                        ) as cleanup,
                        patch(
                            "ptx_frontend.code_gen.cli.write_output_manifest"
                        ) as manifest,
                        redirect_stdout(listed),
                    ):
                        cli.main()
                    self.assertEqual(
                        listed.getvalue().splitlines(),
                        [
                            str(path)
                            for path in build_generation_plan(context, output.resolve()).paths
                        ],
                    )
                    if existing:
                        self.assertEqual(legacy.read_text(encoding="utf-8"), "retain")
                        self.assertEqual(
                            [path for path in output.rglob("*") if path.is_file()],
                            [legacy],
                        )
                    else:
                        self.assertFalse(output.exists())
                    format_file.assert_not_called()
                    cleanup.assert_not_called()
                    manifest.assert_not_called()

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
            active = output / "private/resolved_ir_arithmetic_add.gen.cpp"
            retired_opcode = output / "private/resolved_ir_arithmetic_bucket_2.gen.cpp"
            active_leaf = output / "public/ptx_frontend/resolved_ir/model/arithmetic/add.gen.hpp"
            retired_category = output / "private/resolved_ir_arithmetic.gen.cpp"
            stale = output / "private/resolved_ir_legacy.gen.cpp"
            stale_nested = output / "public/ptx_frontend/resolved_ir/model/retired.gen.hpp"
            retired_leaf = output / "public/ptx_frontend/resolved_ir/model/arithmetic/retired/model.gen.hpp"
            retired_resolution = retired_leaf.with_name("resolution.gen.hpp")
            retired_checker = retired_leaf.with_name("checker.gen.hpp")
            unrelated = retired_leaf.with_name("notes.txt")
            old_public = output / "public/resolved_ir.gen.hpp"
            old_category = output / "public/resolved_ir/model/arithmetic.gen.hpp"
            experimental_aggregate = (
                output / "public/ptx_frontend/resolved_ir_experiment/"
                "ptx_resolved_ir_experiment.gen.hpp"
            )
            experimental_leaf = experimental_aggregate.parent / "model/arithmetic/add.gen.hpp"
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
            experimental_aggregate.parent.mkdir(parents=True, exist_ok=True)
            experimental_aggregate.write_text("retired", encoding="utf-8")
            experimental_leaf.parent.mkdir(parents=True, exist_ok=True)
            experimental_leaf.write_text("retired", encoding="utf-8")
            (output / ".ptx_resolved_ir_outputs.txt").write_text(
                "private/resolved_ir_arithmetic.gen.cpp\n"
                "private/resolved_ir_arithmetic_add.gen.cpp\n"
                "public/ptx_frontend/resolved_ir/model/arithmetic/add.gen.hpp\n",
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
            self.assertFalse(experimental_aggregate.exists())
            self.assertFalse(experimental_leaf.exists())
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
                defer_finalization=False,
                jobs=6,
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

    def test_dispatch_named_category_has_distinct_opcode_path(self) -> None:
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
            plan = build_generation_plan(context, output)
            self.assertEqual(len(plan.paths), len(set(plan.paths)))
            self.assertIn(
                output / f"private/resolved_ir_dispatch_{conflicting_instruction.opcode}.gen.cpp",
                plan.paths,
            )
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
        context = self.context
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

        context = self.context
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
            leaf = Path("public/ptx_frontend/resolved_ir/model/arithmetic/add.gen.hpp")
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
        context = self.context

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
        full_plan, full_output = self.full_generation()

        category_inputs = {group.category: group for group in self.category_inputs}

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
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
                    artifact.path.relative_to(full_output): artifact
                    for artifact in full_plan.artifacts_for_category(category)
                }
                category_by_name = {
                    artifact.path.relative_to(root / "category"): artifact
                    for artifact in category_plan.artifacts_for_category(category)
                }
                self.assertEqual(full_by_name.keys(), category_by_name.keys())

                for relative_path, full_artifact in full_by_name.items():
                    category_artifact = category_by_name[relative_path]
                    category_artifact.emit(
                        category_context,
                        output_path=category_artifact.path,
                    )
                    self.assertEqual(
                        full_artifact.path.read_bytes(),
                        category_artifact.path.read_bytes(),
                    )

    def test_describe_build_is_read_only(self) -> None:
        """Match canonical category inputs and every planned output path."""

        from ptx_frontend.code_gen import cli

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "not-created"
            plan = build_generation_plan(self.context, output)
            description = cli.describe_build(
                plan, category_inputs=self.category_inputs
            )
            self.assertFalse(output.exists())
            self.assertEqual(
                description["all_outputs"],
                [str(path) for path in plan.paths],
            )
            self.assertEqual(
                description["global_outputs"],
                [str(artifact.path) for artifact in plan.global_artifacts],
            )
            groups = {group.category: group for group in self.category_inputs}
            categories = description["categories"]
            self.assertEqual([item["name"] for item in categories], sorted(groups))
            for item in categories:
                group = groups[item["name"]]
                self.assertEqual(
                    item["spec_files"],
                    [str(Path(str(path)).resolve()) for path in group.spec_files],
                )
                self.assertEqual(
                    item["outputs"],
                    [
                        str(artifact.path)
                        for artifact in plan.artifacts_for_category(group.category)
                    ],
                )


if __name__ == "__main__":
    unittest.main()
