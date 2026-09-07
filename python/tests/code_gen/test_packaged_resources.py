from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[3]
RESOURCES = ROOT / "python/code_gen/resources"


class PackagedResourceTests(unittest.TestCase):
    def test_ptx_yaml_schema_comments_resolve(self) -> None:
        for spec in (RESOURCES / "ptx_spec").glob("*.yaml"):
            comment = spec.read_text(encoding="utf-8").splitlines()[1]
            match = re.fullmatch(r"# yaml-language-server: \$schema=(.+)", comment)
            self.assertIsNotNone(match, spec)
            self.assertTrue((spec.parent / match.group(1)).is_file(), spec)

    def test_packaged_schemas_match_compatibility_paths(self) -> None:
        for name in ("ptx-instr-v1.schema.yaml", "ptx-cpp-backend-v1.schema.yaml"):
            self.assertEqual(
                (RESOURCES / name).read_bytes(),
                (ROOT / "instructions/schemas" / name).read_bytes(),
            )

    def test_cpp_backend_is_packaged_with_an_instructions_compatibility_link(
        self,
    ) -> None:
        backend = RESOURCES / "ptx_cpp_backend_spec/ptx_frontend.yaml"
        comment = backend.read_text(encoding="utf-8").splitlines()[0]
        match = re.fullmatch(r"# yaml-language-server: \$schema=(.+)", comment)
        self.assertIsNotNone(match)
        self.assertTrue((backend.parent / match.group(1)).is_file())

        compatibility_path = ROOT / "instructions/ptx_cpp_backend_spec"
        self.assertTrue(compatibility_path.is_symlink())
        self.assertEqual(compatibility_path.resolve(), backend.parent.resolve())


if __name__ == "__main__":
    unittest.main()
