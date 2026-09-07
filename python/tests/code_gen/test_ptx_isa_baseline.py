from copy import deepcopy
from pathlib import Path
import unittest

from jsonschema import Draft202012Validator

from ptx_frontend.code_gen.load_yaml import load_yaml
from scripts.verify_ptx_isa_baseline import (
    BaselineVerificationError,
    fetch_raw_response_body,
    sha256_hex,
    subject_from_baseline,
    verify_digest,
)


REPO_ROOT = Path(__file__).resolve().parents[3]
BASELINE = REPO_ROOT / "instructions/ptx_isa_baseline.yaml"
SCHEMA = REPO_ROOT / "instructions/schemas/ptx-isa-baseline-v1.schema.yaml"
REGISTRIES = (
    REPO_ROOT / "instructions/ptx_instruction_registry.yaml",
    REPO_ROOT / "instructions/ptx_directive_registry.yaml",
    REPO_ROOT / "instructions/ptx_special_register_registry.yaml",
)


class FakeResponse:
    def __init__(self, body: bytes, url: str, content_encoding: str | None = None):
        self.body = body
        self.url = url
        self.headers = {}
        if content_encoding is not None:
            self.headers["Content-Encoding"] = content_encoding

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        return None

    def geturl(self) -> str:
        return self.url

    def read(self) -> bytes:
        return self.body


class FakeOpener:
    def __init__(self, response: FakeResponse):
        self.response = response

    def open(self, request, timeout: int) -> FakeResponse:
        return self.response


class PtxIsaBaselineTests(unittest.TestCase):
    def test_registries_match_frozen_baseline_sources(self) -> None:
        baseline = load_yaml(BASELINE)
        expected_sources = {
            "source_url": baseline["sources"]["root"],
            "evidence_url": baseline["sources"]["contents"],
        }
        for registry_path in REGISTRIES:
            with self.subTest(registry=registry_path.name):
                registry = load_yaml(registry_path)
                self.assertEqual(
                    {field: registry[field] for field in expected_sources}, expected_sources
                )

    def test_freezes_ptx_93_evidence_and_update_policy(self) -> None:
        baseline = load_yaml(BASELINE)
        errors = sorted(
            Draft202012Validator(load_yaml(SCHEMA)).iter_errors(baseline),
            key=lambda error: list(error.path),
        )
        self.assertEqual(errors, [])
        self.assertEqual(baseline["ptx_isa"], "9.3")
        self.assertEqual(baseline["toolkit_release"], "13.3.0")
        self.assertEqual(baseline["document_revision"], "PTX ISA 9.3")
        self.assertEqual(baseline["published"], "2026-06-25")
        self.assertEqual(baseline["retrieved_at"], "2026-08-29")
        self.assertEqual(
            baseline["content_digest"]["subject"], baseline["sources"]["contents"]
        )
        self.assertEqual(
            baseline["content_digest"]["canonicalization"], "raw_response_body"
        )
        self.assertTrue(
            all(
                url.startswith(
                    "https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/"
                )
                for url in baseline["sources"].values()
            )
        )
        self.assertEqual(
            baseline["update_policy"],
            {
                "automatic_latest_tracking": False,
                "upgrade_requires_review": True,
                "required_artifact_diffs": ["registry", "coverage"],
            },
        )

    def test_requires_reproducible_digest_contract(self) -> None:
        baseline = load_yaml(BASELINE)
        validator = Draft202012Validator(load_yaml(SCHEMA))
        for field in ("subject", "canonicalization"):
            with self.subTest(field=field):
                document = deepcopy(baseline)
                del document["content_digest"][field]
                self.assertTrue(list(validator.iter_errors(document)))

    def test_hashes_raw_response_bytes_without_normalization(self) -> None:
        body = b"PTX ISA 9.3\n"
        digest = "afe40baf2fc05f190cf1e4f3dd2e50321807f3d7f5044464f24d9ca9d4f9e073"
        self.assertEqual(sha256_hex(body), digest)
        self.assertEqual(verify_digest(body, digest), digest)
        for changed in (body + b"!", b"PTX ISA 9.3\r\n"):
            with self.subTest(changed=changed):
                with self.assertRaisesRegex(BaselineVerificationError, "digest mismatch"):
                    verify_digest(changed, digest)

    def test_rejects_invalid_subject_and_response_contracts(self) -> None:
        baseline = load_yaml(BASELINE)
        subject = baseline["content_digest"]["subject"]
        document = deepcopy(baseline)
        document["content_digest"]["subject"] = document["sources"]["root"]
        with self.assertRaisesRegex(BaselineVerificationError, "sources.contents"):
            subject_from_baseline(document)

        for replacement in (
            subject.replace("docs.nvidia.com", "example.com"),
            subject.replace("archive/13.3.0", "archive/13.4.0"),
            f"{subject}?unexpected=1",
        ):
            with self.subTest(replacement=replacement):
                document = deepcopy(baseline)
                document["content_digest"]["subject"] = replacement
                document["sources"]["contents"] = replacement
                with self.assertRaises(BaselineVerificationError):
                    subject_from_baseline(document)

        with self.assertRaisesRegex(BaselineVerificationError, "redirect"):
            fetch_raw_response_body(
                subject,
                FakeOpener(FakeResponse(b"body", f"{subject}?redirected=1")),
            )
        with self.assertRaisesRegex(BaselineVerificationError, "Content-Encoding"):
            fetch_raw_response_body(
                subject,
                FakeOpener(FakeResponse(b"body", subject, "gzip")),
            )


if __name__ == "__main__":
    unittest.main()
