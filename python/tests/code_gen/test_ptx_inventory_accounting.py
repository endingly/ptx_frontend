from collections import Counter
from copy import deepcopy
import json
from pathlib import Path
import unittest

from jsonschema import Draft202012Validator

from ptx_frontend.code_gen.load_yaml import load_yaml


REPO_ROOT = Path(__file__).resolve().parents[3]
ACCOUNTING = REPO_ROOT / "instructions/ptx_inventory_accounting.yaml"
SCHEMA = REPO_ROOT / "instructions/schemas/ptx-inventory-accounting-v2.schema.yaml"
COVERAGE = REPO_ROOT / "instructions/opcode_coverage.yaml"
SOURCES = {
    "instruction": "ptx_instruction_registry.yaml",
    "directive": "ptx_directive_registry.yaml",
    "special_register": "ptx_special_register_registry.yaml",
}
KIND_ORDER = tuple(SOURCES)
PARTIAL_OPCODE_STATUS = {
    "syntax": "partial",
    "resolved": "partial",
    "checker": "partial",
    "simulator": "unsupported",
}
COVERAGE_SECTION_ALIASES = {
    ("mov", "9.7.9"): frozenset({"9.7.9.3", "9.7.9.4"}),
    ("mbarrier", "9.7.14.16.12"): frozenset({"9.7.14.16"}),
}


def key(item):
    return item["kind"], item["source_record_id"], item["spelling"]


def coverage_matches(item, section_by_record, opcode, section):
    record_section = section_by_record[item["source_record_id"]]
    return (
        item["spelling"].split(".", 1)[0] == opcode
        and record_section in ({section} | COVERAGE_SECTION_ALIASES.get((opcode, section), set()))
    )


def entries(items, kind):
    return [
        {"source_record_id": source_record_id, "spelling": spelling}
        for item_kind, source_record_id, spelling in sorted(items)
        if item_kind == kind
    ]


def source_items():
    instruction = load_yaml(REPO_ROOT / "instructions" / SOURCES["instruction"])
    directive = load_yaml(REPO_ROOT / "instructions" / SOURCES["directive"])
    special_register = load_yaml(
        REPO_ROOT / "instructions" / SOURCES["special_register"]
    )
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


def accounting_diff(expected, dispositions):
    disposition_keys = [key(item) for item in dispositions]
    actual = set(disposition_keys)
    duplicate = {
        item_key for item_key, count in Counter(disposition_keys).items() if count > 1
    }
    conflict = {
        key(item)
        for item in dispositions
        if item["status"] == "complete" and item["disposition"] != "implemented"
    }
    return {
        "missing": {kind: entries(expected - actual, kind) for kind in KIND_ORDER},
        "extra": {kind: entries(actual - expected, kind) for kind in KIND_ORDER},
        "duplicate": {kind: entries(duplicate, kind) for kind in KIND_ORDER},
        "conflict": {kind: entries(conflict, kind) for kind in KIND_ORDER},
    }


class PtxInventoryAccountingTests(unittest.TestCase):
    def test_frozen_ptx_93_inventory_has_one_support_outcome_per_key(self) -> None:
        accounting = load_yaml(ACCOUNTING)
        errors = sorted(
            Draft202012Validator(load_yaml(SCHEMA)).iter_errors(accounting),
            key=lambda error: list(error.path),
        )
        self.assertEqual(errors, [])
        self.assertEqual(accounting["sources"], SOURCES)

        actual = {item for group in source_items() for item in group}
        dispositions = accounting["dispositions"]
        diff = accounting_diff(actual, dispositions)
        self.assertEqual(
            diff,
            {field: {kind: [] for kind in KIND_ORDER} for field in diff},
            json.dumps(diff, sort_keys=True, separators=(",", ":")),
        )

    def test_instruction_coverage_join_records_implemented_source_and_residual(self) -> None:
        accounting = load_yaml(ACCOUNTING)
        registry = load_yaml(REPO_ROOT / "instructions" / SOURCES["instruction"])
        coverage = load_yaml(COVERAGE)
        section_by_record = {record["id"]: record["section"] for record in registry["records"]}
        implemented = [
            (entry["opcode"], slice_["section"], slice_["id"])
            for entry in coverage["opcodes"]
            if entry["status"] == PARTIAL_OPCODE_STATUS
            for slice_ in entry["slices"]
            if slice_["disposition"] == "implemented"
        ]
        instruction_dispositions = [
            item for item in accounting["dispositions"] if item["kind"] == "instruction"
        ]
        for item in instruction_dispositions:
            covered = any(
                coverage_matches(item, section_by_record, opcode, section)
                for opcode, section, _ in implemented
            )
            with self.subTest(item=key(item)):
                self.assertEqual(item["status"] == "partial", covered)
                if covered:
                    self.assertEqual(item["support_source"], "opcode_coverage")
                    self.assertNotEqual(item["disposition"], "implemented")
                    self.assertTrue(item["reason"])
                    self.assertTrue(item["milestone"])

        unmapped = [
            slice_id
            for opcode, section, slice_id in implemented
            if not any(
                item["status"] == "partial"
                and item["support_source"] == "opcode_coverage"
                and coverage_matches(item, section_by_record, opcode, section)
                for item in instruction_dispositions
            )
        ]
        self.assertEqual(unmapped, [])

    def test_m12_lifecycle_is_closed_and_deferred(self) -> None:
        accounting = load_yaml(ACCOUNTING)
        dispositions = accounting["dispositions"]
        self.assertEqual(
            [item for item in dispositions if item.get("milestone") == "M12"], []
        )
        closed = [
            item
            for item in dispositions
            if item.get("milestone") == "post-1.0"
            and item["reason"].endswith("deferred after M12.")
        ]
        self.assertEqual(
            Counter(item["status"] for item in closed),
            {"partial": 56, "unsupported": 7},
        )
        for item in closed:
            with self.subTest(item=key(item)):
                self.assertEqual(item["disposition"], "deferred")
                if item["status"] == "partial":
                    self.assertEqual(item["support_source"], "opcode_coverage")
                    self.assertIn("implemented", item["reason"])
                    self.assertIn("residual variants", item["reason"])
                else:
                    self.assertEqual(item["status"], "unsupported")
                    self.assertIn("No implemented frozen coverage slice", item["reason"])

    def test_m13_lifecycle_is_closed_and_deferred(self) -> None:
        accounting = load_yaml(ACCOUNTING)
        dispositions = accounting["dispositions"]
        self.assertEqual(
            [item for item in dispositions if item.get("milestone") == "M13"], []
        )
        closed = [
            item
            for item in dispositions
            if item.get("milestone") == "post-1.0"
            and item.get("reason", "").endswith("deferred after M13.")
        ]
        self.assertEqual(Counter(item["status"] for item in closed), {"partial": 24})
        for item in closed:
            with self.subTest(item=key(item)):
                self.assertEqual(item["disposition"], "deferred")
                self.assertEqual(item["support_source"], "opcode_coverage")
                self.assertIn("implemented", item["reason"])
                self.assertIn("residual variants", item["reason"])

        tensormap_fence = next(
            item
            for item in dispositions
            if item["spelling"] == "tensormap.cp_fenceproxy"
        )
        self.assertEqual(
            (
                tensormap_fence["status"],
                tensormap_fence["disposition"],
                tensormap_fence["milestone"],
            ),
            ("unsupported", "planned", "M14"),
        )

    def test_gate_reports_missing_extra_duplicate_and_conflict_stably(self) -> None:
        accounting = load_yaml(ACCOUNTING)
        expected = {item for group in source_items() for item in group}
        dispositions = accounting["dispositions"]
        first = dispositions[0]

        missing = accounting_diff(expected, dispositions[1:])
        self.assertEqual(missing["missing"][first["kind"]], [{
            "source_record_id": first["source_record_id"], "spelling": first["spelling"],
        }])

        registry_new_key = ("instruction", "new-official-record", "newop")
        added_registry = accounting_diff(expected | {registry_new_key}, dispositions)
        self.assertEqual(added_registry["missing"]["instruction"], [{
            "source_record_id": "new-official-record", "spelling": "newop",
        }])

        duplicate = accounting_diff(expected, dispositions + [deepcopy(first)])
        self.assertEqual(duplicate["duplicate"][first["kind"]], [{
            "source_record_id": first["source_record_id"], "spelling": first["spelling"],
        }])

        complete = next(item for item in dispositions if item["status"] == "complete")
        conflicting = deepcopy(complete)
        conflicting["disposition"] = "out_of_scope"
        conflict = accounting_diff(expected, [conflicting])
        self.assertEqual(conflict["conflict"][complete["kind"]], [{
            "source_record_id": complete["source_record_id"], "spelling": complete["spelling"],
        }])

        extra_item = {
            "kind": "directive", "source_record_id": "extra-record", "spelling": ".extra",
        }
        extra = accounting_diff(expected, dispositions + [{
            **extra_item, "status": "unsupported", "disposition": "deferred",
            "reason": "not in the frozen registry", "milestone": "post-1.0",
        }])
        self.assertEqual(extra["extra"]["directive"], [{
            "source_record_id": "extra-record", "spelling": ".extra",
        }])

    def test_schema_requires_reason_and_milestone_for_nonimplemented_outcomes(self) -> None:
        accounting = load_yaml(ACCOUNTING)
        validator = Draft202012Validator(load_yaml(SCHEMA))
        for status in ("partial", "syntax_only", "unsupported"):
            outcome_index = next(
                index for index, item in enumerate(accounting["dispositions"])
                if item["status"] == status
            )
            fields = ("reason", "milestone") + (
                ("support_source",) if status != "unsupported" else ()
            )
            for field in fields:
                with self.subTest(status=status, field=field):
                    document = deepcopy(accounting)
                    del document["dispositions"][outcome_index][field]
                    self.assertTrue(list(validator.iter_errors(document)))


if __name__ == "__main__":
    unittest.main()
