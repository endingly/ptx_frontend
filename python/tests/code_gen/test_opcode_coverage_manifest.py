from pathlib import Path
import unittest

from jsonschema import Draft202012Validator

from code_gen.database import load_codegen_database
from code_gen.load_yaml import load_yaml


REPO_ROOT = Path(__file__).resolve().parents[3]
MANIFEST = REPO_ROOT / "instructions/opcode_coverage.yaml"
SCHEMA = REPO_ROOT / "instructions/schemas/opcode-coverage-v1.schema.yaml"
SPEC_DIR = REPO_ROOT / "instructions/ptx_spec"

M9_OPCODE_ISSUES = {
    "ret": ("M9-I03",),
    "exit": ("M9-I04",),
    "trap": ("M9-I05",),
    "and": ("M9-I06",),
    "or": ("M9-I07",),
    "xor": ("M9-I08",),
    "not": ("M9-I09",),
    "shl": ("M9-I10",),
    "shr": ("M9-I11",),
    "setp": ("M9-I14",),
    "selp": ("M9-I15",),
    "cvt": ("M9-I16", "M9-I17", "M9-I18"),
    "cvta": ("M9-I19",),
    "mul": ("M9-I20", "M9-I21"),
    "mad": ("M9-I22",),
    "fma": ("M9-I23",),
    "div": ("M9-I24",),
}


class OpcodeCoverageManifestTests(unittest.TestCase):
    def test_matches_current_database_and_m9_opcode_plan(self) -> None:
        manifest = load_yaml(MANIFEST)
        errors = sorted(
            Draft202012Validator(load_yaml(SCHEMA)).iter_errors(manifest),
            key=lambda error: list(error.path),
        )
        self.assertEqual(errors, [])

        entries = manifest["opcodes"]
        opcodes = [entry["opcode"] for entry in entries]
        self.assertEqual(len(opcodes), len(set(opcodes)))

        by_opcode = {entry["opcode"]: entry for entry in entries}
        database_opcodes = {
            instruction.opcode
            for instruction in load_codegen_database(spec_dir=SPEC_DIR).instructions
        }
        self.assertEqual(set(by_opcode), database_opcodes | set(M9_OPCODE_ISSUES))

        for opcode in database_opcodes:
            self.assertEqual(
                {field: by_opcode[opcode][field]
                 for field in ("syntax", "resolved", "checker")},
                {"syntax": "partial", "resolved": "partial", "checker": "partial"},
            )
            self.assertEqual(by_opcode[opcode]["simulator"], "unsupported")
            if opcode not in M9_OPCODE_ISSUES:
                self.assertNotIn("m9_issues", by_opcode[opcode])

        for opcode, issues in M9_OPCODE_ISSUES.items():
            expected_frontend_status = (
                "partial" if opcode in database_opcodes else "unsupported"
            )
            self.assertEqual(
                {field: by_opcode[opcode][field]
                 for field in ("syntax", "resolved", "checker", "simulator")},
                {
                    "syntax": expected_frontend_status,
                    "resolved": expected_frontend_status,
                    "checker": expected_frontend_status,
                    "simulator": "unsupported",
                },
            )
            self.assertEqual(tuple(by_opcode[opcode]["m9_issues"]), issues)


if __name__ == "__main__":
    unittest.main()
