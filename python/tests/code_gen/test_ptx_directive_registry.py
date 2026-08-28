from collections import Counter
from pathlib import Path
import re
import unittest

from jsonschema import Draft202012Validator

from code_gen.load_yaml import load_yaml


REPO_ROOT = Path(__file__).resolve().parents[3]
REGISTRY = REPO_ROOT / "instructions/ptx_directive_registry.yaml"
SCHEMA = REPO_ROOT / "instructions/schemas/ptx-directive-registry-v1.schema.yaml"
SYNTAX_COVERAGE = REPO_ROOT / "docs/us-en/syntax_coverage.md"

CHAPTER_11_SPELLINGS = frozenset({
    ".version", ".target", ".address_size", ".entry", ".func", ".alias",
    ".branchtargets", ".calltargets", ".callprototype", ".maxnreg", ".maxntid",
    ".reqntid", ".minnctapersm", ".maxnctapersm", ".noreturn", ".pragma",
    ".abi_preserve", ".abi_preserve_control", "@@dwarf", ".section", ".file",
    ".loc", ".extern", ".visible", ".weak", ".common", ".reqnctapercluster",
    ".explicitcluster", ".maxclusterrank", ".blocksareclusters", ".language",
})
CHAPTER_12_SPELLINGS = frozenset({
    "nounroll", "used_bytes_mask", "enable_smem_spilling", "frequency", "mma_throughput",
})
SUPPLEMENTAL_SPELLINGS = frozenset({
    ".align", ".attribute", ".const", ".global", ".local", ".param", ".reg",
    ".shared", ".sreg", ".tex",
})
DOT_DIRECTIVES = frozenset({
    spelling for spelling in CHAPTER_11_SPELLINGS | SUPPLEMENTAL_SPELLINGS
    if spelling.startswith(".")
})
CHAPTER_11_SECTIONS = frozenset(
    [f"11.1.{number}" for number in range(1, 4)]
    + [f"11.2.{number}" for number in range(1, 4)]
    + [f"11.3.{number}" for number in range(1, 4)]
    + [f"11.4.{number}" for number in range(1, 10)]
    + [f"11.5.{number}" for number in range(1, 5)]
    + [f"11.6.{number}" for number in range(1, 5)]
    + [f"11.7.{number}" for number in range(1, 4)]
    + [f"11.8.{number}" for number in range(1, 3)]
)


class PtxDirectiveRegistryTests(unittest.TestCase):
    def test_frozen_ptx_93_directive_and_pragma_headings(self) -> None:
        registry = load_yaml(REGISTRY)
        errors = sorted(
            Draft202012Validator(load_yaml(SCHEMA)).iter_errors(registry),
            key=lambda error: list(error.path),
        )
        self.assertEqual(errors, [])

        records = registry["records"]
        by_scope = {
            scope: [record for record in records if record["source_scope"] == scope]
            for scope in ("chapter_11", "chapter_12", "supplemental_official")
        }
        self.assertEqual(len(records), 46)
        self.assertEqual(
            Counter(record["kind"] for record in records),
            {"directive": 40, "pragma_string": 5, "legacy_construct": 1},
        )
        self.assertEqual(
            Counter(record["source_scope"] for record in records),
            {"chapter_11": 31, "chapter_12": 5, "supplemental_official": 10},
        )
        self.assertEqual({record["spelling"] for record in by_scope["chapter_11"]}, CHAPTER_11_SPELLINGS)
        self.assertEqual(
            {record["source_section"] for record in by_scope["chapter_11"]},
            CHAPTER_11_SECTIONS,
        )
        self.assertEqual({record["spelling"] for record in by_scope["chapter_12"]}, CHAPTER_12_SPELLINGS)
        self.assertEqual(
            {record["source_section"] for record in by_scope["chapter_12"]},
            {f"12.{number}" for number in range(1, 6)},
        )
        self.assertEqual({record["spelling"] for record in by_scope["supplemental_official"]}, SUPPLEMENTAL_SPELLINGS)
        self.assertEqual(
            {record["source_section"] for record in by_scope["supplemental_official"]},
            {"4.3.1", "5.4.8"},
        )
        self.assertEqual(
            {record["spelling"] for record in records if record["kind"] == "legacy_construct"},
            {"@@dwarf"},
        )
        self.assertTrue(all(record["kind"] == "pragma_string" for record in by_scope["chapter_12"]))
        self.assertEqual(len({record["id"] for record in records}), len(records))
        self.assertEqual(len({record["spelling"] for record in records}), len(records))
        self.assertTrue(all(record["ptx_isa"] == "9.3" for record in records))
        self.assertTrue(
            all(
                f'{registry["source_url"]}#{record["source_anchor"]}'
                .startswith("https://docs.nvidia.com/cuda/parallel-thread-execution/#")
                for record in records
            )
        )

        coverage = SYNTAX_COVERAGE.read_text(encoding="utf-8")
        coverage_registry = coverage.split("## PTX 9.3 directive registry", 1)[1].split(
            "## Implementation priority", 1
        )[0]
        coverage_spellings = frozenset(
            re.findall(r"^\| `(\.[a-z_]+)` \|", coverage_registry, re.MULTILINE)
        )
        self.assertEqual(coverage_spellings, DOT_DIRECTIVES)
        self.assertTrue(coverage_spellings <= {record["spelling"] for record in records})


if __name__ == "__main__":
    unittest.main()
