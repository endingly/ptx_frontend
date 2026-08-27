import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
CORPUS = ROOT / "corpus" / "m9"
MANIFEST = CORPUS / "manifest.json"
ISSUES = {f"M9-I{number:02d}" for number in range(3, 25)}


class M9ManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))

    def test_manifest_schema_and_case_files(self) -> None:
        manifest = self.manifest
        self.assertEqual(
            set(manifest),
            {"schema", "status", "target", "cases", "required_slices"},
        )
        self.assertEqual(manifest["schema"], "ptx_frontend.m9_mvp/v1")
        self.assertEqual(
            manifest["target"],
            {"ptx_version": "9.3", "sm": "sm_80", "address_size": 64},
        )
        self.assertIn("not executed", manifest["status"]["simulator"])

        for case in manifest["cases"]:
            self.assertTrue((CORPUS / case["file"]).is_file())
            self.assertIn(case["entry"]["kind"], {"entry", "function"})
            self.assertTrue(case["entry"]["name"])
            self.assertTrue(case["required_opcodes"])
            self.assertEqual(case["simulator_status"], "not-run")
            self.assertIn(case["expected"]["terminal"], {"returned", "exited", "trapped"})

    def test_manifest_opcodes_match_each_ptx_file(self) -> None:
        for case in self.manifest["cases"]:
            source_opcodes = set()
            for line in (CORPUS / case["file"]).read_text(encoding="utf-8").splitlines():
                line = line.strip()
                if not line or line.startswith(".") or line == "}":
                    continue
                source_opcodes.add(line.split(maxsplit=1)[0].rstrip(";"))

            for opcode in case["required_opcodes"]:
                self.assertTrue(
                    any(
                        source == opcode or source.startswith(f"{opcode}.")
                        for source in source_opcodes
                    ),
                    f"{case['file']}: {opcode}",
                )
            for opcode in source_opcodes:
                self.assertTrue(
                    any(
                        opcode == required or opcode.startswith(f"{required}.")
                        for required in case["required_opcodes"]
                    ),
                    f"{case['file']}: undeclared {opcode}",
                )

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
            forms = requirement.get("corpus_opcodes", requirement["forms"])
            self.assertTrue(
                any(form in corpus_opcodes for form in forms),
                issue,
            )

    def test_execution_expectations_cover_every_executable_case(self) -> None:
        for case in self.manifest["cases"]:
            expected = case["expected"]
            self.assertIsInstance(expected["registers"], dict)
            self.assertIsInstance(expected["memory"], dict)


if __name__ == "__main__":
    unittest.main()
