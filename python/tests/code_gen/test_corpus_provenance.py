from copy import deepcopy
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import unittest

from jsonschema import Draft202012Validator

from code_gen.m12_natural_corpus import (
    canonical_bytes,
    fixture_targets,
    target_directive_architectures,
)


ROOT = Path(__file__).resolve().parents[3]
CORPUS = ROOT / "corpus"
MANIFEST = CORPUS / "provenance.json"
SCHEMA = CORPUS / "provenance.schema.json"
VERSION = "ptx_frontend.corpus_provenance/v2"
SHA256 = re.compile(r"^[0-9a-f]{64}$")
PLACEHOLDERS = {"", "unknown", "todo", "tbd"}


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

        for mutation in (
            lambda record: record.pop("targets"),
            lambda record: record.pop("kind"),
            lambda record: record.__setitem__("target", "sm_80"),
            lambda record: record.__setitem__("targets", []),
        ):
            document = deepcopy(self.manifest)
            mutation(document["fixtures"][0])
            self.assertTrue(list(self.validator.iter_errors(document)))

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

    def test_hash_targets_and_provenance_fields(self) -> None:
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
                self.assertEqual(record["targets"], fixture_targets(path))
                for value in (
                    record["kind"],
                    record["generator"],
                    record["toolkit"],
                    *record["targets"],
                    record["source"],
                    record["license"],
                ):
                    self.assertTrue(value.strip())
                    self.assertNotIn(value.strip().lower(), PLACEHOLDERS)
                    self.assertNotIn("todo", value.lower())
                    self.assertNotIn("unknown", value.lower())
                if record["generator"] == "project-authored":
                    self.assertEqual(record["license"], "MIT")

    def test_m12_evidence_kinds_distinguish_inline_and_natural_ptx(self) -> None:
        by_path = {record["path"]: record for record in self.manifest["fixtures"]}
        inline = {
            "corpus/m12/common_kernel_sm80.ptx",
            "corpus/m12/common_kernel_sm90a.ptx",
            "corpus/m12/common_kernel_sm100.ptx",
        }
        natural = {
            "corpus/m12/natural_kernel_sm80.ptx",
            "corpus/m12/natural_kernel_sm90a.ptx",
            "corpus/m12/natural_kernel_sm100.ptx",
        }
        self.assertTrue(inline <= by_path.keys())
        self.assertTrue(natural <= by_path.keys())
        self.assertEqual({by_path[path]["kind"] for path in inline}, {"inline_ptx_wrapper"})
        self.assertEqual(
            {by_path[path]["kind"] for path in natural},
            {"natural_compiler_emission"},
        )

    def test_multi_target_sequence_detects_any_change(self) -> None:
        record = next(
            record
            for record in self.manifest["fixtures"]
            if record["path"] == "corpus/m11/multi_target_profiles.ptx"
        )
        targets = ["sm_80", "sm_90", "sm_120f"]
        self.assertEqual(record["targets"], targets)
        for changed in (
            [targets[0], targets[2]],
            [targets[1], targets[0], targets[2]],
            [*targets, "sm_80"],
        ):
            with self.subTest(changed=changed):
                self.assertNotEqual(changed, fixture_targets(ROOT / record["path"]))

    def test_target_options_are_not_target_directives(self) -> None:
        self.assertEqual(
            target_directive_architectures(
                ".target sm_80, debug\n.target sm_90\n"
            ),
            ["sm_80", "sm_90"],
        )

    def test_lf_and_crlf_fixture_text_hash_identically(self) -> None:
        lf = hashlib.sha256(canonical_bytes(b".target sm_80\n", "lf")).hexdigest()
        crlf = hashlib.sha256(
            canonical_bytes(b".target sm_80\r\n", "crlf")
        ).hexdigest()
        self.assertEqual(lf, crlf)


if __name__ == "__main__":
    unittest.main()
