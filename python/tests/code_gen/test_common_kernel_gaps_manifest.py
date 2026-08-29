import re
from pathlib import Path
import unittest

import yaml
from jsonschema import Draft202012Validator


ROOT = Path(__file__).resolve().parents[3]
MANIFEST_PATH = ROOT / "instructions/common_kernel_gaps.yaml"
SCHEMA_PATH = ROOT / "instructions/schemas/common-kernel-gaps-v1.schema.yaml"
SPEC_ROOT = ROOT / "instructions/ptx_spec"
EXPECTED_OWNERS = {f"M12-I{issue:02d}" for issue in range(2, 34)}
TARGET = re.compile(r"^\.target\s+(sm_[A-Za-z0-9]+)\s*$", re.MULTILINE)
KNOWN_NONE = {
    ("M12-I02", "set.eq.u32.u32"),
    ("M12-I02", "set.lt.and.f32.s32"),
    ("M12-I04", "slct.u32.s32"),
    ("M12-I04", "slct.ftz.u64.f32"),
} | {
    ("M12-I03", form)
    for form in ("setp.eq.u32", "setp.lt.and.s32")
} | {
    ("M12-I05", form) for form in ("add.u32", "add.s32", "add.u64", "add.f32")
} | {
    ("M12-I06", form) for form in ("sub.u32", "sub.s32", "sub.u64", "sub.f32")
} | {
    ("M12-I07", form) for form in ("mul.hi.u32", "mul.wide.u32", "mul.rn.f32")
} | {
    ("M12-I08", form)
    for form in ("mad.lo.s32", "mad.wide.u32", "mad.rn.f32")
} | {
    ("M12-I09", form) for form in ("fma.rn.f16", "fma.rn.f32", "fma.rn.f64")
} | {
    ("M12-I10", form) for form in ("div.s32", "div.rn.f32", "div.rn.f64")
} | {
    ("M12-I11", form) for form in ("rem.s32", "rem.u32")
} | {
    ("M12-I12", form) for form in ("min.s32", "min.NaN.f32")
} | {
    ("M12-I13", form) for form in ("max.s32", "max.NaN.f32")
} | {
    ("M12-I14", form) for form in ("abs.s32", "abs.f32")
} | {
    ("M12-I15", form) for form in ("neg.s32", "neg.f32", "neg.f16x2")
} | {
    ("M12-I16", "lop3.b32"),
} | {
    ("M12-I17", form) for form in ("shf.l.clamp.b32", "shf.r.wrap.b32")
} | {
    ("M12-I18", form) for form in ("prmt.b32", "prmt.b32.f4e")
} | {
    ("M12-I19", "popc.b32"),
} | {
    ("M12-I20", form) for form in ("clz.b32", "clz.b64")
} | {
    ("M12-I21", "bfind.shiftamt.u32"),
} | {
    ("M12-I25", "cvt.rzi.u32.f32"),
}


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


def declared_opcodes() -> set[str]:
    result = set()

    def visit(value: object) -> None:
        if isinstance(value, dict):
            opcode = value.get("opcode")
            if isinstance(opcode, str):
                result.add(opcode)
            for child in value.values():
                visit(child)
        elif isinstance(value, list):
            for child in value:
                visit(child)

    for path in SPEC_ROOT.glob("*.yaml"):
        visit(yaml.safe_load(path.read_text(encoding="utf-8")))
    return result


class CommonKernelGapManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.manifest = yaml.safe_load(MANIFEST_PATH.read_text(encoding="utf-8"))
        cls.schema = yaml.safe_load(SCHEMA_PATH.read_text(encoding="utf-8"))
        cls.ptx_by_profile = {
            fixture["profile"]: (ROOT / fixture["path"]).read_text(encoding="utf-8")
            for fixture in cls.manifest["fixtures"]
        }

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
        opcodes = declared_opcodes()
        for gap in self.manifest["gaps"]:
            for form in gap["forms"]:
                with self.subTest(owner=gap["owner"], form=form["canonical_form"]):
                    opcode = form["canonical_form"].split(".", 1)[0]
                    key = (gap["owner"], form["canonical_form"])
                    if form["first_blocker"] == "none":
                        self.assertIn(key, KNOWN_NONE)
                        self.assertIn(opcode, opcodes)
                    elif opcode in opcodes:
                        self.assertEqual(form["first_blocker"], "unsupported_variant")
                    else:
                        self.assertEqual(form["first_blocker"], "unsupported_opcode")

    def test_setmaxnreg_is_only_in_its_family_specific_profile(self) -> None:
        setmaxnreg = next(
            gap for gap in self.manifest["gaps"] if gap["owner"] == "M12-I33"
        )
        self.assertEqual(setmaxnreg["forms"][0]["profiles"], ["sm_90a"])
        self.assertIn("setmaxnreg.", self.ptx_by_profile["sm_90a"])
        self.assertNotIn("setmaxnreg.", self.ptx_by_profile["sm_80"])
        self.assertNotIn("setmaxnreg.", self.ptx_by_profile["sm_100"])


if __name__ == "__main__":
    unittest.main()
