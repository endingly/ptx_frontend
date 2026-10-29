"""Focused contracts for discovery provenance and descriptor-shard generation."""

from dataclasses import dataclass, replace
import argparse
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch

from ptx_frontend.code_gen import cli
from ptx_frontend.code_gen.emit import resolved_source
from ptx_frontend.code_gen.render_models import resolved_source as source_models
from ptx_frontend.code_gen.plan import GeneratedArtifact, GenerationPlan
from ptx_frontend.spec import database as spec_database
from ptx_frontend.spec.model import InstructionSpec


@dataclass(frozen=True)
class _Variants:
    """Minimal immutable model used to exercise canonical shard slicing."""

    variants: tuple[str, ...]


class CodegenBuildGraphTests(unittest.TestCase):
    """Check one-pass discovery and stable form-shard extraction."""

    def test_description_reuses_the_normalized_spec_snapshot(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            spec_dir = Path(directory)
            paths = (
                spec_dir / "control_a.yaml",
                spec_dir / "control_b.yaml",
                spec_dir / "other.yaml",
            )
            for path, category, opcode in zip(
                paths,
                ("control_flow", "control_flow", "other"),
                ("trap", "brkpt", "nop"),
            ):
                path.write_text(
                    f"""schema: ptx-instr/v1
ptx_isa: "9.3"
category: {category}
codegen_category: {category}
instructions:
  - opcode: {opcode}
    syntax: "{opcode}"
    variants:
      - name: {opcode}_bare
        availability: {{ptx: "1.0", sm: 0}}
        modifiers: []
        operands: []
""",
                    encoding="utf-8",
                )
            with patch.object(
                spec_database,
                "_load_normalized_spec_files",
                wraps=spec_database._load_normalized_spec_files,
            ) as normalize:
                database, category_inputs = (
                    spec_database.load_codegen_database_with_category_inputs(
                        spec_dir=spec_dir
                    )
                )

            self.assertEqual(normalize.call_count, 1)
            self.assertEqual(
                {group.category: group.spec_files for group in category_inputs},
                {
                    "control_flow": paths[:2],
                    "other": paths[2:],
                },
            )
            self.assertEqual(
                {instruction.opcode for instruction in database.instructions},
                {"trap", "brkpt", "nop"},
            )

    def test_descriptor_shard_lowers_syntax_once(self) -> None:
        specification = InstructionSpec(
            codegen_category="arithmetic", opcode="add", variants=("v0", "v1", "v2")
        )
        entry = SimpleNamespace(
            specification=specification,
            resolved=_Variants(("r0", "r1", "r2")),
            cpp_name="Add",
        )
        context = SimpleNamespace(entries=(entry,), backend=object())
        syntax = _Variants(("s0", "s2"))
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "descriptor.gen.cpp"
            with (
                patch.object(source_models, "form_shards", return_value=((0, 2),)),
                patch.object(
                    source_models, "from_InstructionSpec", return_value=syntax
                ) as lower,
                patch.object(
                    source_models, "build_syntax_descriptor_storage", return_value=syntax
                ) as emit_syntax,
                patch.object(source_models, "build_resolved_descriptor_storage", return_value=None),
                patch.object(source_models, "build_checker_descriptor_storage", return_value=None),
                patch.object(resolved_source, "render_template", return_value="") as render,
            ):
                resolved_source.generate_resolved_descriptor_shard_source(
                    context,
                    category="arithmetic",
                    opcode="add",
                    shard_index=0,
                    output_path=output,
                )

            lower.assert_called_once_with(replace(specification, variants=("v0", "v2")))
            self.assertEqual(emit_syntax.call_args.args[0].variants, ("s0", "s2"))
            render.assert_called_once()
            self.assertIs(render.call_args.args[1].syntax_storage, syntax)
            self.assertTrue(output.is_file())

    def test_failed_global_generation_preserves_previous_manifest_and_files(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            spec_dir = root / "spec"
            spec_dir.mkdir()
            backend = root / "backend.yaml"
            backend.touch()
            output = root / "generated"
            stale = output / "private/resolved_ir_old.gen.cpp"
            stale.parent.mkdir(parents=True)
            stale.write_text("previous\n", encoding="utf-8")
            manifest = output / ".ptx_resolved_ir_outputs.txt"
            manifest.write_text("private/resolved_ir_old.gen.cpp\n", encoding="utf-8")

            def fail(_context: object, *, output_path: Path) -> None:
                """Simulate one failed shared-artifact emitter."""

                raise RuntimeError("global generation failed")

            plan = GenerationPlan(
                (GeneratedArtifact(output / "private/new.gen.cpp", fail),)
            )
            arguments = argparse.Namespace(
                spec_dir=spec_dir,
                backend_spec=backend,
                output=output,
                spec_file=[],
                category=None,
                global_artifacts=True,
                list_outputs=False,
                describe_build=False,
                jobs=1,
            )
            with (
                patch.object(cli, "parse_arguments", return_value=arguments),
                patch.object(cli, "load_codegen_database"),
                patch.object(cli, "load_cpp_backend"),
                patch.object(cli, "build_generation_context", return_value=object()),
                patch.object(cli, "build_generation_plan", return_value=plan),
                self.assertRaisesRegex(RuntimeError, "global generation failed"),
            ):
                cli.main()

            self.assertEqual(stale.read_text(encoding="utf-8"), "previous\n")
            self.assertEqual(
                manifest.read_text(encoding="utf-8"),
                "private/resolved_ir_old.gen.cpp\n",
            )

    def test_deferred_full_emission_leaves_finalization_to_global_job(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            spec_dir = root / "spec"
            spec_dir.mkdir()
            backend = root / "backend.yaml"
            backend.touch()
            output = root / "generated"
            stale = output / "private/resolved_ir_old.gen.cpp"
            stale.parent.mkdir(parents=True)
            stale.write_text("previous\n", encoding="utf-8")
            manifest = output / ".ptx_resolved_ir_outputs.txt"
            manifest.write_text("private/resolved_ir_old.gen.cpp\n", encoding="utf-8")

            def emit(_context: object, *, output_path: Path) -> None:
                """Write the planned replacement artifact."""

                output_path.write_text("new\n", encoding="utf-8")

            planned = output / "private/new.gen.cpp"
            plan = GenerationPlan((GeneratedArtifact(planned, emit),))
            arguments = argparse.Namespace(
                spec_dir=spec_dir,
                backend_spec=backend,
                output=output,
                spec_file=[],
                category=None,
                global_artifacts=False,
                list_outputs=False,
                describe_build=False,
                defer_finalization=True,
                jobs=1,
            )
            with (
                patch.object(cli, "parse_arguments", return_value=arguments),
                patch.object(cli, "load_codegen_database"),
                patch.object(cli, "load_cpp_backend"),
                patch.object(cli, "build_generation_context", return_value=object()),
                patch.object(cli, "build_generation_plan", return_value=plan),
                patch.object(cli, "format_file_inplace"),
            ):
                cli.main()

            self.assertEqual(planned.read_text(encoding="utf-8"), "new\n")
            self.assertEqual(stale.read_text(encoding="utf-8"), "previous\n")
            self.assertEqual(
                manifest.read_text(encoding="utf-8"),
                "private/resolved_ir_old.gen.cpp\n",
            )


if __name__ == "__main__":
    unittest.main()
