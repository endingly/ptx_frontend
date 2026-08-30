import re
from pathlib import Path
import unittest

import yaml
from jsonschema import Draft202012Validator

from code_gen.database import load_codegen_database
from code_gen.m12_natural_corpus import classify_instruction_spelling


ROOT = Path(__file__).resolve().parents[3]
MANIFEST_PATH = ROOT / "instructions/common_kernel_gaps.yaml"
SCHEMA_PATH = ROOT / "instructions/schemas/common-kernel-gaps-v1.schema.yaml"
SPEC_ROOT = ROOT / "instructions/ptx_spec"
EXPECTED_OWNERS = {f"M12-I{issue:02d}" for issue in range(2, 34)}
TARGET = re.compile(r"^\.target\s+(sm_[A-Za-z0-9]+)\s*$", re.MULTILINE)


def entry_body(ptx: str, marker: str) -> str | None:
    match = re.search(
        rf"\.visible\s+\.entry\s+{re.escape(marker)}\(\)\s*\n\{{",
        ptx,
    )
    if match is None:
        return None
    depth = 1
    for index, char in enumerate(ptx[match.end() :], start=match.end()):
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return ptx[match.end() : index]
    raise AssertionError(f"unterminated entry marker: {marker}")


class CommonKernelGapManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.manifest = yaml.safe_load(MANIFEST_PATH.read_text(encoding="utf-8"))
        cls.schema = yaml.safe_load(SCHEMA_PATH.read_text(encoding="utf-8"))
        cls.ptx_by_profile = {
            fixture["profile"]: (ROOT / fixture["path"]).read_text(encoding="utf-8")
            for fixture in cls.manifest["fixtures"]
        }
        cls.database = load_codegen_database(spec_dir=SPEC_ROOT)

    def test_schema_fixture_profiles_and_exact_owner_set(self) -> None:
        errors = sorted(Draft202012Validator(self.schema).iter_errors(self.manifest), key=str)
        self.assertFalse(errors, "\n".join(error.message for error in errors))
        fixture_map = {
            fixture["profile"]: fixture["path"] for fixture in self.manifest["fixtures"]
        }
        self.assertEqual(
            fixture_map,
            {
                "sm_80": "corpus/m12/common_kernel_sm80.ptx",
                "sm_90a": "corpus/m12/common_kernel_sm90a.ptx",
                "sm_100": "corpus/m12/common_kernel_sm100.ptx",
            },
        )
        self.assertEqual({gap["owner"] for gap in self.manifest["gaps"]}, EXPECTED_OWNERS)
        self.assertEqual(len(self.manifest["gaps"]), len(EXPECTED_OWNERS))
        for profile, ptx in self.ptx_by_profile.items():
            self.assertEqual(TARGET.findall(ptx), [profile])

    def test_declared_frequency_recounts_fixture_marker_and_form(self) -> None:
        seen = set()
        for gap in self.manifest["gaps"]:
            for form in gap["forms"]:
                with self.subTest(owner=gap["owner"], form=form["canonical_form"]):
                    key = (gap["marker"], form["canonical_form"])
                    self.assertNotIn(key, seen, f"duplicate canonical form for {key}")
                    seen.add(key)
                    self.assertEqual(form["frequency"], len(form["profiles"]))
                    count = 0
                    for profile, ptx in self.ptx_by_profile.items():
                        body = entry_body(ptx, gap["marker"])
                        matches = 0
                        if body is not None:
                            matches = len(
                                re.findall(
                                    rf"(?<![A-Za-z0-9_.:]){re.escape(form['canonical_form'])}(?![A-Za-z0-9_.:])",
                                    body,
                                )
                            )
                        if profile not in form["profiles"]:
                            self.assertEqual(matches, 0, f"undeclared profile {profile}")
                            continue
                        self.assertIsNotNone(body, f"missing entry in {profile}")
                        count += matches
                    self.assertEqual(count, form["frequency"])

    def test_blockers_follow_current_opcode_database(self) -> None:
        for gap in self.manifest["gaps"]:
            for form in gap["forms"]:
                with self.subTest(owner=gap["owner"], form=form["canonical_form"]):
                    self.assertEqual(
                        form["first_blocker"],
                        classify_instruction_spelling(
                            form["canonical_form"], self.database
                        ),
                    )

    def test_all_frozen_forms_are_unblocked(self) -> None:
        forms = [
            (gap["owner"], form)
            for gap in self.manifest["gaps"]
            for form in gap["forms"]
        ]
        self.assertEqual(len(forms), 60)
        self.assertTrue(all(form["first_blocker"] == "none" for _, form in forms))

    def test_setmaxnreg_appears_only_in_its_corpus_profile(self) -> None:
        setmaxnreg = next(
            gap for gap in self.manifest["gaps"] if gap["owner"] == "M12-I33"
        )
        self.assertEqual(setmaxnreg["forms"][0]["profiles"], ["sm_90a"])
        self.assertIn("setmaxnreg.", self.ptx_by_profile["sm_90a"])
        self.assertNotIn("setmaxnreg.", self.ptx_by_profile["sm_80"])
        self.assertNotIn("setmaxnreg.", self.ptx_by_profile["sm_100"])


if __name__ == "__main__":
    unittest.main()
