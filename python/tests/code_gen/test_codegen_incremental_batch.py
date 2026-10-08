"""Focused contracts for the build graph's single-process batch CLI."""

from __future__ import annotations

import argparse
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from ptx_frontend.code_gen import cli
from ptx_frontend.code_gen.plan import GeneratedArtifact, GenerationPlan


class IncrementalBatchTests(unittest.TestCase):
    """Keep category selection and finalization ordered within one load."""

    def test_selected_category_then_global_uses_one_model_load(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            spec_dir = root / "spec"
            spec_dir.mkdir()
            backend = root / "backend.yaml"
            backend.touch()
            output = root / "generated"
            output.mkdir()
            old_output = output / "private/resolved_ir_old.gen.cpp"
            old_output.parent.mkdir()
            old_output.write_text("old\n", encoding="utf-8")
            kept_output = output / "private/a.gen.cpp"
            kept_output.write_text("kept\n", encoding="utf-8")
            manifest = output / ".ptx_resolved_ir_outputs.txt"
            manifest.write_text("private/resolved_ir_old.gen.cpp\n", encoding="utf-8")

            planned = GenerationPlan(
                (
                    GeneratedArtifact(
                        output / "private/a.gen.cpp", lambda *args, **kwargs: None,
                        category="a",
                    ),
                    GeneratedArtifact(
                        output / "private/b.gen.cpp", lambda *args, **kwargs: None,
                        category="b",
                    ),
                    GeneratedArtifact(
                        output / "private/global.gen.cpp",
                        lambda *args, **kwargs: None,
                    ),
                )
            )
            arguments = argparse.Namespace(
                spec_dir=spec_dir, backend_spec=backend, output=output,
                spec_file=[], category=None, global_artifacts=False,
                list_outputs=False, describe_build=False,
                incremental_batch=True, batch_category=["b"],
                batch_global=True, defer_finalization=False, jobs=6,
            )
            written: list[tuple[Path, ...]] = []

            def write(_context: object, artifacts: tuple, _jobs: int) -> None:
                """Record each emission phase and create its selected outputs."""

                paths = tuple(artifact.path for artifact in artifacts)
                written.append(paths)
                for path in paths:
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text("new\n", encoding="utf-8")

            with (
                patch.object(cli, "parse_arguments", return_value=arguments),
                patch.object(cli, "load_cpp_backend") as load_backend,
                patch.object(cli, "load_codegen_database") as load_database,
                patch.object(cli, "build_generation_context") as make_context,
                patch.object(cli, "build_generation_plan", return_value=planned),
                patch.object(cli, "write_artifacts", side_effect=write),
            ):
                cli.main()

            load_backend.assert_called_once_with(backend)
            load_database.assert_called_once_with(spec_dir=spec_dir)
            make_context.assert_called_once()
            self.assertEqual(
                written,
                [
                    (output / "private/b.gen.cpp",),
                    (output / "private/global.gen.cpp",),
                ],
            )
            self.assertEqual(kept_output.read_text(encoding="utf-8"), "kept\n")
            self.assertFalse(old_output.exists())
            self.assertEqual(
                set(manifest.read_text(encoding="utf-8").splitlines()),
                {
                    "private/a.gen.cpp",
                    "private/b.gen.cpp",
                    "private/global.gen.cpp",
                },
            )

    def test_global_failure_keeps_the_previous_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            spec_dir = root / "spec"
            spec_dir.mkdir()
            backend = root / "backend.yaml"
            backend.touch()
            output = root / "generated"
            output.mkdir()
            manifest = output / ".ptx_resolved_ir_outputs.txt"
            manifest.write_text("private/previous.gen.cpp\n", encoding="utf-8")
            planned = GenerationPlan(
                (
                    GeneratedArtifact(
                        output / "private/a.gen.cpp", lambda *args, **kwargs: None,
                        category="a",
                    ),
                    GeneratedArtifact(
                        output / "private/global.gen.cpp",
                        lambda *args, **kwargs: None,
                    ),
                )
            )
            arguments = argparse.Namespace(
                spec_dir=spec_dir, backend_spec=backend, output=output,
                spec_file=[], category=None, global_artifacts=False,
                list_outputs=False, describe_build=False,
                incremental_batch=True, batch_category=["a"],
                batch_global=True, defer_finalization=False, jobs=6,
            )
            calls = 0

            def fail_on_global(
                _context: object, artifacts: tuple, _jobs: int
            ) -> None:
                """Fail after the category phase has written its output."""

                nonlocal calls
                calls += 1
                if calls == 2:
                    raise RuntimeError("shared emitter failed")
                artifacts[0].path.parent.mkdir(parents=True, exist_ok=True)
                artifacts[0].path.write_text("new\n", encoding="utf-8")

            with (
                patch.object(cli, "parse_arguments", return_value=arguments),
                patch.object(cli, "load_cpp_backend"),
                patch.object(cli, "load_codegen_database"),
                patch.object(cli, "build_generation_context"),
                patch.object(cli, "build_generation_plan", return_value=planned),
                patch.object(cli, "write_artifacts", side_effect=fail_on_global),
                self.assertRaisesRegex(RuntimeError, "shared emitter failed"),
            ):
                cli.main()

            self.assertEqual(calls, 2)
            self.assertTrue((output / "private/a.gen.cpp").is_file())
            self.assertEqual(
                manifest.read_text(encoding="utf-8"),
                "private/previous.gen.cpp\n",
            )


if __name__ == "__main__":
    unittest.main()
