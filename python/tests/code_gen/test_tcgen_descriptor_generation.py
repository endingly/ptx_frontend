"""Keep the descriptor catalogue as one source for two planned C++ artifacts."""

from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest

from ptx_frontend.code_gen.emit.tcgen_descriptor_domains import (
    generate_tcgen_descriptor_header,
    generate_tcgen_descriptor_source,
)
from ptx_frontend.code_gen.plan import build_generation_plan


class TcgenDescriptorGenerationTests(unittest.TestCase):
    """Check output ownership and deterministic finite code generation."""

    def test_global_plan_owns_exactly_two_descriptor_outputs(self) -> None:
        """Manifest discovery and generation name the same public/private pair."""

        with TemporaryDirectory() as directory:
            root = Path(directory)
            context = SimpleNamespace(entries=())
            plan = build_generation_plan(context, root)
            descriptor_paths = tuple(path.relative_to(root).as_posix()
                                     for path in plan.paths
                                     if "tcgen_descriptor_domains" in path.name)
            self.assertEqual(descriptor_paths, (
                "public/ptx_frontend/resolved_ir/tcgen_descriptor_domains.gen.hpp",
                "private/resolved_ir_tcgen_descriptor_domains.gen.cpp",
            ))
            self.assertEqual(len(plan.paths), len(set(plan.paths)))
            self.assertTrue(all(item.category is None for item in plan.artifacts
                                if "tcgen_descriptor_domains" in item.path.name))

    def test_emitted_tables_are_deterministic_and_cover_literal_rules(self) -> None:
        """The public/private pair retains fixed fields and eight layout rows."""

        with TemporaryDirectory() as directory:
            root = Path(directory)
            header = root / "tcgen_descriptor_domains.gen.hpp"
            source = root / "resolved_ir_tcgen_descriptor_domains.gen.cpp"
            generate_tcgen_descriptor_header(None, output_path=header)
            generate_tcgen_descriptor_source(None, output_path=source)
            first_header, first_source = header.read_bytes(), source.read_bytes()
            generate_tcgen_descriptor_header(None, output_path=header)
            generate_tcgen_descriptor_source(None, output_path=source)
            self.assertEqual((header.read_bytes(), source.read_bytes()),
                             (first_header, first_source))
            self.assertIn(b"TcgenMmaKind", first_header)
            self.assertIn(b"TcgenZeroPartition", first_header)
            self.assertIn(b"bool generate_mask;", first_header)
            self.assertNotIn(b"zero_all", first_header + first_source)
            self.assertIn(b"0x3ffffULL", first_source)
            self.assertIn(b"sm_103a", first_source)
            self.assertIn(b"std::array<TcgenRelativeLayoutRow, 8>", first_source)
            self.assertNotIn(b"__FIELDS_", first_source)
            self.assertNotIn(b"__PARTITION_", first_source)


if __name__ == "__main__":
    unittest.main()
