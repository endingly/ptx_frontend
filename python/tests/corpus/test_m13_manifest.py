import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[3]
CORPUS = ROOT / "corpus" / "m13"
MANIFEST = CORPUS / "manifest.json"
ISSUES = {f"M13-I{number:02d}" for number in range(1, 25)}


class M13SynchronizationManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))

    def test_cases_and_required_forms_match_exact_ptx_files(self) -> None:
        manifest = self.manifest
        self.assertEqual(
            set(manifest), {"schema", "status", "cases", "boundaries", "required_slices"}
        )
        self.assertEqual(manifest["schema"], "ptx_frontend.m13_synchronization/v1")
        self.assertEqual(
            {case["file"] for case in manifest["cases"]},
            {path.name for path in CORPUS.glob("*.ptx")},
        )
        self.assertEqual(
            [(case["file"], case["target"], case["entry"])
             for case in manifest["cases"]],
            [("synchronization_sm90a.ptx", "sm_90a", "m13_synchronization_sm90a"),
             ("synchronization_sm100.ptx", "sm_100", "m13_synchronization_sm100")],
        )
        self.assertEqual(manifest["status"]["frontend"], "resolved/checked")
        self.assertEqual(manifest["status"]["simulator"].split(";", 1)[0], "not-run")

        forms = set()
        for case in manifest["cases"]:
            source = (CORPUS / case["file"]).read_text(encoding="utf-8")
            self.assertIn(f".target {case['target']}", source)
            self.assertIn(f".entry {case['entry']}()", source)
            self.assertEqual(case["simulator_status"], "not-run")
            self.assertTrue(case["required_forms"])
            for form in case["required_forms"]:
                self.assertIn(form, source, f"{case['file']}: {form}")
            forms.update(case["required_forms"])

        self.assertEqual(set(manifest["required_slices"]), ISSUES)
        for issue, requirement in manifest["required_slices"].items():
            self.assertTrue(requirement["forms"], issue)
            self.assertTrue(requirement["excludes"], issue)
            self.assertTrue(set(requirement["forms"]) <= forms, issue)

        self.assertEqual(
            manifest["boundaries"],
            {
                "M13-I03": ["match.all data sink with predicate", "match.all data with predicate sink"],
                "M13-I14": ["mbarrier arrival count 1048575"],
            },
        )


if __name__ == "__main__":
    unittest.main()
