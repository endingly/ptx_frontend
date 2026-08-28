import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
CORPUS = ROOT / "corpus" / "m10"
MANIFEST = CORPUS / "manifest.json"
ISSUES = {f"M10-I{number:02d}" for number in range(1, 18)}


class M10ManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))

    def test_manifest_schema_and_case_files(self) -> None:
        manifest = self.manifest
        self.assertEqual(
            set(manifest),
            {"schema", "status", "target", "cases", "required_slices"},
        )
        self.assertEqual(manifest["schema"], "ptx_frontend.m10_advanced/v1")
        self.assertEqual(
            manifest["target"],
            {"ptx_version": "9.3", "sm": "sm_80", "address_size": 64},
        )
        self.assertEqual(manifest["status"]["frontend"], "resolved/checked")
        self.assertIn("unsupported", manifest["status"]["simulator"])

        self.assertEqual(
            {case["file"] for case in manifest["cases"]},
            {path.name for path in CORPUS.glob("*.ptx")},
        )
        for case in manifest["cases"]:
            self.assertTrue((CORPUS / case["file"]).is_file())
            self.assertEqual(case["entry"]["kind"], "entry")
            self.assertTrue(case["entry"]["name"])
            self.assertTrue(case["required_opcodes"])
            self.assertEqual(case["simulator_status"], "not-run")

    def test_manifest_opcodes_match_each_ptx_file(self) -> None:
        for case in self.manifest["cases"]:
            source_opcodes = set()
            for line in (CORPUS / case["file"]).read_text(encoding="utf-8").splitlines():
                line = line.strip()
                if not line or line.startswith(".") or line == "}":
                    continue
                source_opcodes.add(line.split(maxsplit=1)[0].rstrip(";"))

            self.assertEqual(source_opcodes, set(case["required_opcodes"]))

    def test_issue_slices_are_exactly_covered_by_corpus_opcodes(self) -> None:
        slices = self.manifest["required_slices"]
        self.assertEqual(set(slices), ISSUES)
        corpus_opcodes = {
            opcode
            for case in self.manifest["cases"]
            for opcode in case["required_opcodes"]
        }
        for issue, requirement in slices.items():
            self.assertTrue(requirement["forms"], issue)
            self.assertTrue(requirement["excludes"], issue)
            if requirement.get("corpus_required") is False:
                continue
            self.assertTrue(set(requirement["forms"]) <= corpus_opcodes, issue)


if __name__ == "__main__":
    unittest.main()
