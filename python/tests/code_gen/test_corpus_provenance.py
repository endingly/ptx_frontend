import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import unittest

from jsonschema import Draft202012Validator


ROOT = Path(__file__).resolve().parents[3]
CORPUS = ROOT / "corpus"
MANIFEST = CORPUS / "provenance.json"
SCHEMA = CORPUS / "provenance.schema.json"
VERSION = "ptx_frontend.corpus_provenance/v1"
TARGET = re.compile(r"^\s*\.target\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s*,.*)?\s*$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")
PLACEHOLDERS = {"", "unknown", "todo", "tbd"}


def fixture_target(path: Path) -> str:
    """Return the first target spelling; comma-separated target options are ignored."""

    for line in path.read_text(encoding="utf-8").splitlines():
        match = TARGET.match(line)
        if match is not None:
            return match.group(1)
    raise AssertionError(f"{path.relative_to(ROOT)}: missing .target directive")


def canonical_bytes(data: bytes, label: str) -> bytes:
    """Return UTF-8 bytes with CRLF normalized to LF."""

    canonical = data.replace(b"\r\n", b"\n")
    if b"\r" in canonical:
        raise AssertionError(f"{label}: bare CR is not canonical")
    try:
        canonical.decode("utf-8")
    except UnicodeDecodeError as error:
        raise AssertionError(f"{label}: fixture is not UTF-8") from error
    return canonical


def canonical_fixture_bytes(path: Path) -> bytes:
    """Return canonical UTF-8 fixture bytes for provenance hashing."""

    return canonical_bytes(path.read_bytes(), str(path.relative_to(ROOT)))


class CorpusProvenanceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
        cls.validator = Draft202012Validator(schema)

    def test_schema_and_version(self) -> None:
        errors = sorted(self.validator.iter_errors(self.manifest), key=str)
        self.assertFalse(
            errors,
            "\n".join(f"{error.json_path}: {error.message}" for error in errors),
        )
        self.assertEqual(self.manifest["schema"], VERSION)

    def test_records_exactly_match_ptx_fixtures(self) -> None:
        records = self.manifest["fixtures"]
        paths = [record["path"] for record in records]
        self.assertEqual(len(paths), len(set(paths)), "duplicate provenance path")
        for value in paths:
            path = PurePosixPath(value)
            self.assertFalse(path.is_absolute(), value)
            self.assertNotIn("..", path.parts, value)
            self.assertEqual(value, path.as_posix(), value)

        actual = {
            path.relative_to(ROOT).as_posix() for path in CORPUS.rglob("*.ptx")
        }
        self.assertEqual(set(paths), actual)

    def test_hash_target_and_provenance_fields(self) -> None:
        for record in self.manifest["fixtures"]:
            with self.subTest(path=record["path"]):
                path = ROOT / record["path"]
                self.assertTrue(path.is_file())
                relative = path.relative_to(CORPUS)
                current = CORPUS
                for component in relative.parts:
                    current /= component
                    self.assertFalse(current.is_symlink(), current)
                self.assertTrue(path.resolve().is_relative_to(CORPUS.resolve()))
                self.assertRegex(record["sha256"], SHA256)
                self.assertEqual(
                    record["sha256"],
                    hashlib.sha256(canonical_fixture_bytes(path)).hexdigest(),
                )
                self.assertEqual(record["target"], fixture_target(path))
                for field in ("generator", "toolkit", "target", "source", "license"):
                    value = record[field]
                    self.assertTrue(value.strip(), f"{field} is empty")
                    self.assertNotIn(value.strip().lower(), PLACEHOLDERS)
                    self.assertNotIn("todo", value.lower())
                    self.assertNotIn("unknown", value.lower())
                if record["generator"] == "project-authored":
                    self.assertEqual(record["license"], "MIT")

    def test_lf_and_crlf_fixture_text_hash_identically(self) -> None:
        lf = hashlib.sha256(canonical_bytes(b".target sm_80\n", "lf")).hexdigest()
        crlf = hashlib.sha256(
            canonical_bytes(b".target sm_80\r\n", "crlf")
        ).hexdigest()
        self.assertEqual(lf, crlf)


if __name__ == "__main__":
    unittest.main()
