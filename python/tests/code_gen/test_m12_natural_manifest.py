import json
from pathlib import Path
import unittest

from jsonschema import Draft202012Validator

from code_gen.database import load_codegen_database
from code_gen.m12_natural_corpus import build_natural_manifest


ROOT = Path(__file__).resolve().parents[3]
MANIFEST = ROOT / "corpus/m12/natural_manifest.json"
SCHEMA = ROOT / "corpus/m12/natural_manifest.schema.json"
FIXTURES = {
    "sm_80": Path("corpus/m12/natural_kernel_sm80.ptx"),
    "sm_90a": Path("corpus/m12/natural_kernel_sm90a.ptx"),
    "sm_100": Path("corpus/m12/natural_kernel_sm100.ptx"),
}


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

    def test_manifest_keeps_only_the_setp_ge_s32_residual(self) -> None:
        self.assertEqual(
            {
                (fixture["profile"], instruction["spelling"], instruction["first_blocker"])
                for fixture in self.manifest["fixtures"]
                for instruction in fixture["instructions"]
                if instruction["outcome"] == "unsupported"
            },
            {(profile, "setp.ge.s32", "unsupported_variant") for profile in FIXTURES},
        )


if __name__ == "__main__":
    unittest.main()
