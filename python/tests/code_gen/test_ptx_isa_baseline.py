from pathlib import Path
import unittest

from jsonschema import Draft202012Validator

from code_gen.load_yaml import load_yaml


REPO_ROOT = Path(__file__).resolve().parents[3]
BASELINE = REPO_ROOT / "instructions/ptx_isa_baseline.yaml"
SCHEMA = REPO_ROOT / "instructions/schemas/ptx-isa-baseline-v1.schema.yaml"


class PtxIsaBaselineTests(unittest.TestCase):
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
            baseline["content_digest"],
            {
                "algorithm": "sha256",
                "value": "14f6af4c0aefa9aadd257f85c5bcb577c27b8fcd16c4954af4e281409cd1ebcc",
            },
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


if __name__ == "__main__":
    unittest.main()
