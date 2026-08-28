import json
from pathlib import Path
import unittest

from jsonschema import Draft202012Validator

from code_gen.load_yaml import load_yaml


REPO_ROOT = Path(__file__).resolve().parents[3]
ACCOUNTING = REPO_ROOT / "instructions/ptx_inventory_accounting.yaml"
SCHEMA = REPO_ROOT / "instructions/schemas/ptx-inventory-accounting-v1.schema.yaml"
SOURCES = {
    "instruction": "ptx_instruction_registry.yaml",
    "directive": "ptx_directive_registry.yaml",
    "special_register": "ptx_special_register_registry.yaml",
}
KIND_ORDER = tuple(SOURCES)


def source_items():
    instruction = load_yaml(REPO_ROOT / "instructions" / SOURCES["instruction"])
    directive = load_yaml(REPO_ROOT / "instructions" / SOURCES["directive"])
    special_register = load_yaml(REPO_ROOT / "instructions" / SOURCES["special_register"])
    return (
        [
            ("instruction", record["id"], spelling)
            for record in instruction["records"]
            for spelling in record["opcodes"]
        ],
        [
            ("directive", record["id"], record["spelling"])
            for record in directive["records"]
        ],
        [
            ("special_register", record["id"], spelling)
            for record in special_register["records"]
            for form in record["forms"]
            for spelling in form["spellings"]
        ],
    )


def structured_diff(expected, actual):
    def entries(items, kind):
        return [
            {"source_record_id": source_record_id, "spelling": spelling}
            for item_kind, source_record_id, spelling in sorted(items)
            if item_kind == kind
        ]

    return {
        "added": {kind: entries(actual - expected, kind) for kind in KIND_ORDER},
        "removed": {kind: entries(expected - actual, kind) for kind in KIND_ORDER},
    }


class PtxInventoryAccountingTests(unittest.TestCase):
    def test_frozen_ptx_93_inventory_is_accounted_for(self) -> None:
        accounting = load_yaml(ACCOUNTING)
        errors = sorted(
            Draft202012Validator(load_yaml(SCHEMA)).iter_errors(accounting),
            key=lambda error: list(error.path),
        )
        self.assertEqual(errors, [])
        self.assertEqual(accounting["sources"], SOURCES)

        declared = [
            (item["kind"], item["source_record_id"], item["spelling"])
            for item in accounting["items"]
        ]
        source_groups = source_items()
        actual = [item for group in source_groups for item in group]

        diff = structured_diff(set(declared), set(actual))
        self.assertFalse(
            any(diff[side][kind] for side in diff for kind in KIND_ORDER),
            json.dumps(diff, sort_keys=True, separators=(",", ":")),
        )
        self.assertEqual(len(declared), len(set(declared)), "duplicate accounting key")
        self.assertEqual(len(actual), len(set(actual)), "duplicate source accounting key")

    def test_structured_diff_is_stable_and_keeps_provenance_distinct(self) -> None:
        expected = {
            ("instruction", "first-heading", "same"),
            ("instruction", "second-heading", "same"),
        }
        actual = {("instruction", "first-heading", "same")}
        self.assertEqual(
            structured_diff(expected, actual),
            {
                "added": {kind: [] for kind in KIND_ORDER},
                "removed": {
                    "instruction": [{"source_record_id": "second-heading", "spelling": "same"}],
                    "directive": [],
                    "special_register": [],
                },
            },
        )


if __name__ == "__main__":
    unittest.main()
