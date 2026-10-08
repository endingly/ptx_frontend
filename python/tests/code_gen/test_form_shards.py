"""Regression contracts for bounded direct-class form generation."""

from __future__ import annotations

from dataclasses import replace
from pathlib import Path
import tempfile
import unittest

from ptx_frontend.code_gen.cli import (
    read_output_manifest,
    remove_obsolete_generated_files,
    write_output_manifest,
)
from ptx_frontend.code_gen.context import GenerationContext, build_generation_context
from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.code_gen.emit.resolved_model import FORM_SHARD_SIZE, form_shards
from ptx_frontend.code_gen.plan import build_generation_plan
from ptx_frontend.spec.database import get_packaged_spec_database


ROOT = Path(__file__).resolve().parents[3]
BACKEND_SPEC = ROOT / "instructions/ptx_cpp_backend_spec/ptx_frontend.yaml"


class FormShardTests(unittest.TestCase):
    """Keep the shard plan, emitted files, and cleanup in one canonical order."""

    @classmethod
    def setUpClass(cls) -> None:
        """Lower the real corpus once so tests use its exact form identities."""

        database = get_packaged_spec_database()
        backend = load_cpp_backend(BACKEND_SPEC)
        context = build_generation_context(database, backend)
        cls.cp = next(
            entry for entry in context.entries
            if entry.specification.opcode == "cp"
        )
        cls.context = GenerationContext(backend=backend, entries=(cls.cp,))

    def context_with_form_count(self, count: int) -> GenerationContext:
        """Restrict the real cp variants while retaining valid backend bindings."""

        entry = replace(
            self.cp,
            resolved=replace(self.cp.resolved,
                             variants=self.cp.resolved.variants[:count]),
        )
        return GenerationContext(backend=self.context.backend, entries=(entry,))

    def test_threshold_and_canonical_partition(self) -> None:
        """Shard only beyond 64 forms and cover every original index once."""

        self.assertGreater(len(self.cp.resolved.variants), 129)
        for count, expected_sizes in (
            (63, ()), (64, ()), (65, (64, 1)),
            (128, (64, 64)), (129, (64, 64, 1)),
        ):
            with self.subTest(count=count):
                shards = form_shards(self.context_with_form_count(count).entries[0])
                self.assertEqual(tuple(map(len, shards)), expected_sizes)
                self.assertEqual(
                    tuple(index for shard in shards for index in shard),
                    tuple(range(count)) if count > FORM_SHARD_SIZE else (),
                )

    def test_plan_has_one_ordered_triple_per_shard(self) -> None:
        """Discovery names each descriptor, header, and method file once."""

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            plan = build_generation_plan(self.context, output)
            self.assertEqual(plan.paths,
                             build_generation_plan(self.context, output).paths)
            self.assertEqual(len(plan.paths), len(set(plan.paths)))
            category = self.cp.specification.codegen_category
            opcode = self.cp.specification.opcode
            for index in range(len(form_shards(self.cp))):
                triple = (
                    output / (
                        f"private/resolved_ir_{category}_{opcode}_"
                        f"descriptors_{index:03d}.gen.cpp"
                    ),
                    output / (
                        f"public/ptx_frontend/resolved_ir/model/{category}/"
                        f"{opcode}_forms_{index:03d}.gen.hpp"
                    ),
                    output / f"private/resolved_ir_{category}_{opcode}_methods_{index:03d}.gen.cpp",
                )
                with self.subTest(index=index):
                    start = plan.paths.index(triple[0])
                    self.assertEqual(plan.paths[start:start + 3], triple)
                    self.assertTrue(all(
                        item.category == category
                        for item in plan.artifacts[start:start + 3]
                    ))
            self.assertEqual(len(plan.paths), 10 + 2 + 3 * len(form_shards(self.cp)))

    def test_emitted_shards_own_only_their_canonical_forms(self) -> None:
        """Bound emitters preserve class identity and global method indexes."""

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = build_generation_plan(self.context, root / "first")
            second = build_generation_plan(self.context, root / "second")
            selected = (0, 1)
            for index in selected:
                for offset in range(3):
                    artifact_index = 10 + 3 * index + offset
                    left = first.artifacts[artifact_index]
                    right = second.artifacts[artifact_index]
                    left.emit(self.context, output_path=left.path)
                    right.emit(self.context, output_path=right.path)
                    self.assertEqual(left.path.read_bytes(), right.path.read_bytes())

                descriptor, header, methods = (
                    first.artifacts[10 + 3 * index + offset].path.read_text(
                        encoding="utf-8")
                    for offset in range(3)
                )
                own_indices = form_shards(self.cp)[index]
                other_index = form_shards(self.cp)[1 - index][0]
                for form_index in (own_indices[0], own_indices[-1]):
                    name = self.cp.cpp_name + self.cp.resolved.variants[form_index].cpp_name
                    self.assertIn(f"class {name}", header)
                    self.assertIn(f"{name}::check(", methods)
                other_name = self.cp.cpp_name + self.cp.resolved.variants[other_index].cpp_name
                self.assertNotIn(f"class {other_name}", header)
                self.assertNotIn(f"{other_name}::check(", methods)
                self.assertIn(f"_syntax_variants_{index:03d}()", descriptor)
                self.assertIn(f"_resolved_variants_{index:03d}()", descriptor)
                self.assertIn(f"_checker_variants_{index:03d}()", descriptor)

    def test_shrink_removes_stale_shards_and_preserves_unowned_files(self) -> None:
        """A smaller plan removes old shards without touching consumer output."""

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            large = build_generation_plan(self.context, output)
            small = build_generation_plan(self.context_with_form_count(64), output)
            stale = set(large.paths) - set(small.paths)
            self.assertEqual(len(stale), 3 * len(form_shards(self.cp)))
            for path in stale:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("old shard\n", encoding="utf-8")
            unowned = output / "private/consumer-notes.txt"
            unowned.write_text("keep\n", encoding="utf-8")
            write_output_manifest(output, large.paths)
            remove_obsolete_generated_files(output, small.paths)
            self.assertTrue(all(not path.exists() for path in stale))
            self.assertEqual(unowned.read_text(encoding="utf-8"), "keep\n")
            write_output_manifest(output, small.paths)
            self.assertEqual(
                read_output_manifest(output),
                {path.relative_to(output).as_posix() for path in small.paths},
            )


if __name__ == "__main__":
    unittest.main()
