"""Compatibility checks for safe parsing of packaged YAML resources."""

from importlib.resources import files
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import yaml

from ptx_frontend.spec.load_yaml import load_yaml


class LoadYamlTests(unittest.TestCase):
    """Keep accelerated and pure-Python loading on the same data contract."""

    def test_packaged_resources_match_safe_loader(self) -> None:
        """Parse every packaged schema and source with equivalent safe values."""

        resources = files("ptx_frontend.spec.resources")
        paths = (
            *(path for path in resources.iterdir() if path.name.endswith(".yaml")),
            *resources.joinpath("ptx_cpp_backend_spec").iterdir(),
            *resources.joinpath("ptx_spec").iterdir(),
        )
        for path in paths:
            with self.subTest(path=path.name):
                expected = yaml.safe_load(path.read_text(encoding="utf-8"))
                self.assertEqual(load_yaml(path), expected)

    def test_uses_c_safe_loader_when_available(self) -> None:
        """The installed extension handles normal inputs on the fast path."""

        if not getattr(yaml, "CSafeLoader", None):
            self.skipTest("PyYAML C extension is unavailable")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "spec.yaml"
            path.write_text("name: café\n", encoding="utf-8")
            with patch.object(yaml, "load", wraps=yaml.load) as parser:
                self.assertEqual(load_yaml(path), {"name": "café"})
            self.assertIs(parser.call_args.kwargs["Loader"], yaml.CSafeLoader)

    def test_falls_back_without_c_safe_loader(self) -> None:
        """Pure-Python wheels retain UTF-8 and safe scalar parsing."""

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "spec.yaml"
            path.write_text("name: café\nflag: true\n", encoding="utf-8")
            with (
                patch.object(yaml, "CSafeLoader", None, create=True),
                patch.object(yaml, "load", wraps=yaml.load) as parser,
            ):
                self.assertEqual(load_yaml(path), {"name": "café", "flag": True})
            self.assertIs(parser.call_args.kwargs["Loader"], yaml.SafeLoader)

    def test_rejects_unsafe_malformed_and_nonmapping_documents(self) -> None:
        """Both parser paths reject unsafe tags and preserve mapping errors."""

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "spec.yaml"
            for loader in (yaml.SafeLoader, getattr(yaml, "CSafeLoader", None)):
                if loader is None:
                    continue
                with self.subTest(loader=loader.__name__):
                    with patch.object(yaml, "CSafeLoader", loader, create=True):
                        path.write_text("value: !!python/object/apply:os.system ['true']\n")
                        with self.assertRaises(yaml.YAMLError):
                            load_yaml(path)
                        path.write_text("value: [1, 2\n")
                        with self.assertRaises(yaml.YAMLError) as error:
                            load_yaml(path)
                        self.assertIsNotNone(error.exception.problem_mark)
                        for document in ("[one, two]\n", "42\n", ""):
                            with self.subTest(document=document):
                                path.write_text(document)
                                with self.assertRaisesRegex(
                                    TypeError, "expected YAML mapping"
                                ) as error:
                                    load_yaml(path)
                                self.assertIn(str(path), str(error.exception))


if __name__ == "__main__":
    unittest.main()
