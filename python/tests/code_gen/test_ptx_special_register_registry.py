from collections import Counter
from copy import deepcopy
from pathlib import Path
import re
import unittest

from jsonschema import Draft202012Validator

from code_gen.load_yaml import load_yaml


REPO_ROOT = Path(__file__).resolve().parents[3]
REGISTRY = REPO_ROOT / "instructions/ptx_special_register_registry.yaml"
SCHEMA = REPO_ROOT / "instructions/schemas/ptx-special-register-registry-v1.schema.yaml"
RUNTIME_CATALOG = REPO_ROOT / "submod/base/src/ptx_special_register.cpp"

VECTOR_SECTIONS = frozenset({"10.1", "10.2", "10.6", "10.7", "10.12", "10.13", "10.14", "10.15"})
INDEXED_SECTIONS = frozenset({"10.25", "10.26", "10.27", "10.29"})
EXPECTED_SECTIONS = frozenset(f"10.{number}" for number in range(1, 34))
VECTOR_BASES = (
    "%tid", "%ntid", "%ctaid", "%nctaid", "%clusterid", "%nclusterid",
    "%cluster_ctaid", "%cluster_nctaid",
)
EXPECTED_SPELLINGS = frozenset(
    VECTOR_BASES
    + tuple(f"{base}.{component}" for base in VECTOR_BASES for component in "xyz")
    + (
        "%laneid", "%warpid", "%nwarpid", "%smid", "%nsmid", "%gridid",
        "%is_explicit_cluster", "%cluster_ctarank", "%cluster_nctarank",
        "%lanemask_eq", "%lanemask_le", "%lanemask_lt", "%lanemask_ge", "%lanemask_gt",
        "%clock", "%clock_hi", "%clock64", "%globaltimer", "%globaltimer_lo",
        "%globaltimer_hi", "%reserved_smem_offset_begin", "%reserved_smem_offset_end",
        "%reserved_smem_offset_cap", "%total_smem_size", "%aggr_smem_size",
        "%dynamic_smem_size", "%current_graph_exec",
    )
    + tuple(f"%pm{number}" for number in range(8))
    + tuple(f"%pm{number}_64" for number in range(8))
    + tuple(f"%envreg{number}" for number in range(32))
    + tuple(f"%reserved_smem_offset_{number}" for number in range(2))
)
EXPECTED_RUNTIME_GAPS = frozenset()
EXPECTED_RUNTIME_EXTRAS = frozenset()
EXPECTED_FORM_SIGNATURES = {
    "10.1": Counter({("vector4", "u32", "2.0", 0, 1): 1, ("scalar", "u32", "2.0", 0, 3): 1}),
    "10.2": Counter({("vector4", "u32", "2.0", 0, 1): 1, ("scalar", "u32", "2.0", 0, 3): 1}),
    "10.3": Counter({("scalar", "u32", "1.3", 0, 1): 1}),
    "10.4": Counter({("scalar", "u32", "1.3", 0, 1): 1}),
    "10.5": Counter({("scalar", "u32", "2.0", 20, 1): 1}),
    "10.6": Counter({("vector4", "u32", "2.0", 0, 1): 1, ("scalar", "u32", "2.0", 0, 3): 1}),
    "10.7": Counter({("vector4", "u32", "2.0", 0, 1): 1, ("scalar", "u32", "2.0", 0, 3): 1}),
    "10.8": Counter({("scalar", "u32", "1.3", 0, 1): 1}),
    "10.9": Counter({("scalar", "u32", "2.0", 20, 1): 1}),
    "10.10": Counter({("scalar", "u64", "3.0", 0, 1): 1}),
    "10.11": Counter({("scalar", "pred", "7.8", 90, 1): 1}),
    "10.12": Counter({("vector4", "u32", "7.8", 90, 1): 1, ("scalar", "u32", "7.8", 90, 3): 1}),
    "10.13": Counter({("vector4", "u32", "7.8", 90, 1): 1, ("scalar", "u32", "7.8", 90, 3): 1}),
    "10.14": Counter({("vector4", "u32", "7.8", 90, 1): 1, ("scalar", "u32", "7.8", 90, 3): 1}),
    "10.15": Counter({("vector4", "u32", "7.8", 90, 1): 1, ("scalar", "u32", "7.8", 90, 3): 1}),
    "10.16": Counter({("scalar", "u32", "7.8", 90, 1): 1}),
    "10.17": Counter({("scalar", "u32", "7.8", 90, 1): 1}),
    **{f"10.{number}": Counter({("scalar", "u32", "2.0", 20, 1): 1}) for number in range(18, 23)},
    "10.23": Counter({("scalar", "u32", "1.0", 0, 1): 1, ("scalar", "u32", "5.0", 20, 1): 1}),
    "10.24": Counter({("scalar", "u64", "2.0", 20, 1): 1}),
    "10.25": Counter({("scalar", "u32", "1.3", 0, 4): 1, ("scalar", "u32", "3.0", 20, 4): 1}),
    "10.26": Counter({("scalar", "u64", "4.0", 50, 8): 1}),
    "10.27": Counter({("scalar", "b32", "2.1", 0, 32): 1}),
    "10.28": Counter({("scalar", "u64", "3.1", 30, 1): 1, ("scalar", "u32", "3.1", 30, 2): 1}),
    "10.29": Counter({("scalar", "b32", "7.6", 80, 5): 1}),
    "10.30": Counter({("scalar", "u32", "4.1", 20, 1): 1}),
    "10.31": Counter({("scalar", "u32", "8.1", 90, 1): 1}),
    "10.32": Counter({("scalar", "u32", "4.1", 20, 1): 1}),
    "10.33": Counter({("scalar", "u64", "8.0", 50, 1): 1}),
}


def flattened_forms(records):
    return {
        spelling: (form["shape"], form["type"], form["min_ptx"], form["min_sm"])
        for record in records
        for form in record["forms"]
        for spelling in form["spellings"]
    }


class PtxSpecialRegisterRegistryTests(unittest.TestCase):
    def test_pattern_fields_reject_non_strings(self) -> None:
        registry = load_yaml(REGISTRY)
        validator = Draft202012Validator(load_yaml(SCHEMA))
        for path, field in (
            (("records", 0), "id"),
            (("records", 0), "section"),
            (("records", 0), "anchor"),
            (("records", 0, "forms", 0), "min_ptx"),
            (("records", 0, "forms", 0, "spellings"), 0),
        ):
            for value in (0, None, {}, []):
                with self.subTest(path=path, field=field, value=value):
                    document = deepcopy(registry)
                    target = document
                    for key in path:
                        target = target[key]
                    target[field] = value
                    self.assertTrue(list(validator.iter_errors(document)))

    def test_frozen_chapter_10_registry_and_runtime_catalog(self) -> None:
        registry = load_yaml(REGISTRY)
        errors = sorted(
            Draft202012Validator(load_yaml(SCHEMA)).iter_errors(registry),
            key=lambda error: list(error.path),
        )
        self.assertEqual(errors, [])

        records = registry["records"]
        forms = flattened_forms(records)
        self.assertEqual(len(records), 33)
        self.assertEqual({record["section"] for record in records}, EXPECTED_SECTIONS)
        self.assertEqual(len({record["id"] for record in records}), len(records))
        self.assertEqual(len(forms), 109)
        self.assertEqual(
            sum(len(form["spellings"]) for record in records for form in record["forms"]),
            len(forms),
        )
        self.assertEqual(set(forms), EXPECTED_SPELLINGS)
        self.assertEqual(Counter(shape for shape, *_ in forms.values()), {"scalar": 101, "vector4": 8})
        self.assertEqual(Counter(type_ for _, type_, *_ in forms.values()), {"u32": 59, "u64": 12, "b32": 37, "pred": 1})
        self.assertTrue(all(record["ptx_isa"] == "9.3" for record in records))
        self.assertTrue(
            all(record["id"] == f'ptx-9-3-{record["anchor"]}' for record in records)
        )
        self.assertEqual(
            {
                record["section"]: Counter(
                    (form["shape"], form["type"], form["min_ptx"], form["min_sm"], len(form["spellings"]))
                    for form in record["forms"]
                )
                for record in records
            },
            EXPECTED_FORM_SIGNATURES,
        )

        runtime = re.sub(r"\s+", " ", RUNTIME_CATALOG.read_text(encoding="utf-8"))
        entries = {
            spelling: (type_.lower(), f"{major}.{minor}", int(sm or 0))
            for spelling, type_, major, minor, sm in re.findall(
                r'Entry\{"(%[a-z0-9_]+)", scalar\(SpecialRegisterKind::\w+, ScalarType::(U32|U64|B32|Pred), (\d+), (\d+)(?:, (\d+))?',
                runtime,
            )
        }
        runtime_forms = {
            spelling: ("scalar", *entries[spelling]) for spelling in entries
        }
        for section in VECTOR_SECTIONS:
            record = next(record for record in records if record["section"] == section)
            base, components = record["forms"]
            base_spelling = base["spellings"][0]
            self.assertIn(base_spelling, entries)
            metadata = entries[base_spelling]
            runtime_forms[base_spelling] = ("vector4", *metadata)
            runtime_forms.update({spelling: ("scalar", *metadata) for spelling in components["spellings"]})

        indexed_contracts = (
            (
                'parseIndexed(spelling, "%pm", 8, "_64")',
                "PerformanceMonitor64, ScalarType::U64, 4, 0, 50",
                "10.26",
            ),
            (
                'parseIndexed(spelling, "%pm", 8)',
                "PerformanceMonitor, ScalarType::U32, 1, 3, 0",
                "10.25",
            ),
            (
                'parseIndexed(spelling, "%envreg", 32)',
                "Environment, ScalarType::B32, 2, 1, 0",
                "10.27",
            ),
            (
                'parseIndexed(spelling, "%reserved_smem_offset_", 2)',
                "ReservedSmemOffset, ScalarType::B32, 7, 6, 80",
                "10.29",
            ),
        )
        for source_contract, metadata_contract, section in indexed_contracts:
            self.assertIn(source_contract, runtime)
            self.assertIn(metadata_contract, runtime)
            record = next(record for record in records if record["section"] == section)
            runtime_forms.update(
                {
                    spelling: (form["shape"], form["type"], form["min_ptx"], form["min_sm"])
                    for form in record["forms"]
                    for spelling in form["spellings"]
                }
            )
        self.assertIn("PerformanceMonitor, ScalarType::U32, 3, 0, 20", runtime)

        official = set(forms)
        runtime_supported = set(runtime_forms)
        self.assertEqual(official - runtime_supported, EXPECTED_RUNTIME_GAPS, "runtime gaps are registry-visible")
        self.assertEqual(runtime_supported - official, EXPECTED_RUNTIME_EXTRAS, "runtime extras are registry-visible")
        self.assertEqual(runtime_forms, forms)


if __name__ == "__main__":
    unittest.main()
