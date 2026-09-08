import json
from pathlib import Path
import re
import unittest

from jsonschema import Draft202012Validator

from ptx_frontend.code_gen.database import load_codegen_database
from ptx_frontend.code_gen.m12_natural_corpus import build_natural_manifest


ROOT = Path(__file__).resolve().parents[3]
MANIFEST = ROOT / "corpus/m12/natural_manifest.json"
SCHEMA = ROOT / "corpus/m12/natural_manifest.schema.json"
FIXTURES = {
    "sm_80": Path("corpus/m12/natural_kernel_sm80.ptx"),
    "sm_90a": Path("corpus/m12/natural_kernel_sm90a.ptx"),
    "sm_100": Path("corpus/m12/natural_kernel_sm100.ptx"),
}
COMMON_SOURCE = ROOT / "corpus/m12/common_kernel.cu"
COMMON_FIXTURES = {
    "sm_80": ROOT / "corpus/m12/common_kernel_sm80.ptx",
    "sm_90a": ROOT / "corpus/m12/common_kernel_sm90a.ptx",
    "sm_100": ROOT / "corpus/m12/common_kernel_sm100.ptx",
}
_COMMON_SOURCE_ENTRY = re.compile(
    r'extern "C" __global__ void (?P<entry>m12_i\d{2}_[a-z0-9_]+)\(\) '
    r'\{ M12_ASM\("(?P<body>[^"]*)"\); \}'
)


def inline_ptx_spellings(body: str) -> tuple[str, ...]:
    """Return the instruction spellings embedded in one inline-PTX body."""

    body = "\n".join(line.partition("//")[0] for line in body.splitlines())
    return tuple(
        statement.split(maxsplit=1)[0]
        for raw_statement in body.replace("{", "").replace("}", "").split(";")
        if (statement := raw_statement.strip()) and not statement.startswith(".")
    )


def entry_body(ptx: str, entry: str) -> str | None:
    """Return one PTX entry body while preserving nested inline-PTX braces."""

    match = re.search(
        rf"\.visible\s+\.entry\s+{re.escape(entry)}\(\)\s*\n\{{", ptx
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
    raise AssertionError(f"unterminated entry marker: {entry}")


class M12NaturalManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        cls.validator = Draft202012Validator(
            json.loads(SCHEMA.read_text(encoding="utf-8"))
        )
        cls.database = load_codegen_database(spec_dir=ROOT / "instructions/ptx_spec")

    def test_schema_and_profiles(self) -> None:
        errors = sorted(self.validator.iter_errors(self.manifest), key=str)
        self.assertFalse(errors, "\n".join(error.message for error in errors))
        self.assertEqual(
            {fixture["profile"] for fixture in self.manifest["fixtures"]},
            set(FIXTURES),
        )
        for fixture in self.manifest["fixtures"]:
            self.assertEqual(fixture["target"], fixture["profile"])
            self.assertEqual(fixture["entries"], ["natural_kernel"])

    def test_checked_ptx_recomputes_the_manifest(self) -> None:
        self.assertEqual(
            self.manifest,
            build_natural_manifest(FIXTURES, self.database),
        )

    def test_source_is_an_ordinary_cuda_kernel(self) -> None:
        source = (ROOT / self.manifest["source"]).read_text(encoding="utf-8")
        self.assertIn("__global__ void natural_kernel", source)
        self.assertNotIn("asm", source)

    def test_manifest_marks_every_compiler_spelling_supported(self) -> None:
        instructions = [
            instruction
            for fixture in self.manifest["fixtures"]
            for instruction in fixture["instructions"]
        ]
        self.assertTrue(instructions)
        self.assertTrue(
            all(instruction["first_blocker"] == "none" for instruction in instructions)
        )
        self.assertTrue(all("outcome" not in instruction for instruction in instructions))

    def test_common_inline_forms_survive_in_the_named_ptx_entries(self) -> None:
        """Keep source inline forms in their corresponding frozen PTX entries."""

        expected = {
            match["entry"]: inline_ptx_spellings(match["body"])
            for match in _COMMON_SOURCE_ENTRY.finditer(
                COMMON_SOURCE.read_text(encoding="utf-8")
            )
        }
        self.assertTrue(expected)
        self.assertTrue(all(forms for forms in expected.values()))

        for profile, fixture in COMMON_FIXTURES.items():
            with self.subTest(profile=profile):
                ptx = fixture.read_text(encoding="utf-8")
                expected_entries = set(expected)
                if profile != "sm_90a":
                    expected_entries.remove("m12_i33_setmaxnreg")
                actual_entries = set(
                    re.findall(
                        r"^\.visible\s+\.entry\s+([A-Za-z_][A-Za-z0-9_]*)\(",
                        ptx,
                        re.MULTILINE,
                    )
                )
                self.assertEqual(actual_entries, expected_entries)
                for entry in expected_entries:
                    body = entry_body(ptx, entry)
                    self.assertIsNotNone(body)
                    self.assertTrue(
                        set(expected[entry]) <= set(inline_ptx_spellings(body)),
                        entry,
                    )


if __name__ == "__main__":
    unittest.main()
