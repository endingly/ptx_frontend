from collections import Counter
from copy import deepcopy
from pathlib import Path
import unittest

from jsonschema import Draft202012Validator

from code_gen.load_yaml import load_yaml


REPO_ROOT = Path(__file__).resolve().parents[3]
REGISTRY = REPO_ROOT / "instructions/ptx_instruction_registry.yaml"
SCHEMA = REPO_ROOT / "instructions/schemas/ptx-instruction-registry-v1.schema.yaml"

EXPECTED_FAMILY_SECTIONS = {
    "integer-arithmetic": "9.7.1",
    "extended-precision-integer-arithmetic": "9.7.2",
    "floating-point": "9.7.3",
    "half-precision-floating-point": "9.7.4",
    "mixed-precision-floating-point": "9.7.5",
    "comparison-and-selection": "9.7.6",
    "half-precision-comparison": "9.7.7",
    "logic-and-shift": "9.7.8",
    "data-movement-and-conversion": "9.7.9",
    "fabric": "9.7.10",
    "texture": "9.7.11",
    "surface": "9.7.12",
    "control-flow": "9.7.13",
    "parallel-synchronization-and-communication": "9.7.14",
    "warp-level-matrix": "9.7.15",
    "asynchronous-warpgroup-level-matrix": "9.7.16",
    "tensorcore-5th-generation": "9.7.17",
    "stack-manipulation": "9.7.18",
    "video": "9.7.19",
    "miscellaneous": "9.7.20",
}
EXPECTED_RECORD_COUNTS = {
    "integer-arithmetic": 25,
    "extended-precision-integer-arithmetic": 6,
    "floating-point": 22,
    "half-precision-floating-point": 10,
    "mixed-precision-floating-point": 3,
    "comparison-and-selection": 4,
    "half-precision-comparison": 2,
    "logic-and-shift": 9,
    "data-movement-and-conversion": 37,
    "fabric": 6,
    "texture": 4,
    "surface": 4,
    "control-flow": 7,
    "parallel-synchronization-and-communication": 29,
    "warp-level-matrix": 8,
    "asynchronous-warpgroup-level-matrix": 5,
    "tensorcore-5th-generation": 12,
    "stack-manipulation": 3,
    "video": 8,
    "miscellaneous": 5,
}
NON_OPCODE_RECORD_KINDS = {
    "9.7.13.1": "syntax_construct",
    "9.7.13.2": "syntax_construct",
    "9.7.14.16": "instruction_family",
}
EXPECTED_RECORD_SECTIONS = frozenset(
    [f"9.7.1.{number}" for number in range(1, 26)]
    + [f"9.7.2.{number}" for number in range(1, 7)]
    + [f"9.7.3.{number}" for number in range(1, 23)]
    + [f"9.7.4.{number}" for number in range(1, 11)]
    + [f"9.7.5.{number}" for number in range(1, 4)]
    + [f"9.7.6.{number}" for number in range(1, 5)]
    + [f"9.7.7.{number}" for number in range(1, 3)]
    + [f"9.7.8.{number}" for number in range(1, 10)]
    + [f"9.7.9.{number}" for number in range(3, 26)]
    + [
        "9.7.9.26.3.1", "9.7.9.26.3.2", "9.7.9.26.3.3",
        "9.7.9.26.4.1", "9.7.9.26.4.2", "9.7.9.26.4.3",
        "9.7.9.26.4.4", "9.7.9.26.4.5", "9.7.9.26.5.2",
        "9.7.9.26.5.3", "9.7.9.26.5.4", "9.7.9.26.6.1",
        "9.7.9.26.6.2", "9.7.9.27",
    ]
    + [f"9.7.10.5.{number}" for number in range(1, 7)]
    + [f"9.7.11.{number}" for number in range(3, 7)]
    + [f"9.7.12.{number}" for number in range(1, 5)]
    + [f"9.7.13.{number}" for number in range(1, 8)]
    + [f"9.7.14.{number}" for number in range(1, 17)]
    + [f"9.7.14.16.{number}" for number in range(12, 22)]
    + [f"9.7.14.{number}" for number in range(17, 20)]
    + ["9.7.15.4.3", "9.7.15.4.4", "9.7.15.4.5"]
    + [f"9.7.15.5.{number}" for number in range(14, 18)]
    + ["9.7.15.6.3", "9.7.16.5.2", "9.7.16.6.3"]
    + [f"9.7.16.7.{number}" for number in range(1, 4)]
    + ["9.7.17.7.1"]
    + [f"9.7.17.8.{number}" for number in range(3, 6)]
    + [f"9.7.17.9.{number}" for number in range(2, 4)]
    + [f"9.7.17.10.9.{number}" for number in range(1, 5)]
    + ["9.7.17.11.1", "9.7.17.12.1"]
    + [f"9.7.18.{number}" for number in range(1, 4)]
    + [f"9.7.19.1.{number}" for number in range(1, 5)]
    + [f"9.7.19.2.{number}" for number in range(1, 5)]
    + [f"9.7.20.{number}" for number in range(1, 6)]
)


class PtxInstructionRegistryTests(unittest.TestCase):
    def test_pattern_fields_reject_non_strings(self) -> None:
        registry = load_yaml(REGISTRY)
        validator = Draft202012Validator(load_yaml(SCHEMA))
        for collection, fields in (
            ("families", ("id", "section", "anchor")),
            ("records", ("id", "family", "section", "anchor")),
        ):
            for field in fields:
                for value in (0, None, {}, []):
                    with self.subTest(collection=collection, field=field, value=value):
                        document = deepcopy(registry)
                        document[collection][0][field] = value
                        self.assertTrue(list(validator.iter_errors(document)))

    def test_frozen_ptx_93_instruction_headings(self) -> None:
        registry = load_yaml(REGISTRY)
        errors = sorted(
            Draft202012Validator(load_yaml(SCHEMA)).iter_errors(registry),
            key=lambda error: list(error.path),
        )
        self.assertEqual(errors, [])

        families = registry["families"]
        records = registry["records"]
        self.assertEqual(
            {family["id"]: family["section"] for family in families},
            EXPECTED_FAMILY_SECTIONS,
        )
        self.assertEqual(len({family["id"] for family in families}), len(families))
        self.assertEqual(len({family["section"] for family in families}), len(families))
        self.assertEqual(len({family["anchor"] for family in families}), len(families))

        self.assertEqual(Counter(record["family"] for record in records), EXPECTED_RECORD_COUNTS)
        self.assertEqual({record["section"] for record in records}, EXPECTED_RECORD_SECTIONS)
        self.assertEqual(len(records), 209)
        self.assertEqual(sum(len(record["opcodes"]) for record in records), 234)
        self.assertEqual(sum("record_kind" in record for record in records), 3)
        self.assertEqual(
            {
                record["section"]: record["record_kind"]
                for record in records
                if record.get("record_kind", "opcode") != "opcode"
            },
            NON_OPCODE_RECORD_KINDS,
        )
        opcode_records = [
            record for record in records if record.get("record_kind", "opcode") == "opcode"
        ]
        self.assertEqual(len(opcode_records), 206)
        self.assertEqual(sum(len(record["opcodes"]) for record in opcode_records), 231)
        self.assertEqual(len({record["id"] for record in records}), len(records))
        self.assertEqual(len({record["section"] for record in records}), len(records))
        self.assertEqual(len({record["anchor"] for record in records}), len(records))
        self.assertTrue(
            all(record["id"] == f'ptx-9-3-{record["anchor"]}' for record in records)
        )
        self.assertEqual({record["family"] for record in records}, set(EXPECTED_FAMILY_SECTIONS))
        self.assertTrue(all(record["ptx_isa"] == "9.3" for record in records))
        self.assertEqual(
            len({entry["anchor"] for entry in [*families, *records]}),
            len(families) + len(records),
        )


if __name__ == "__main__":
    unittest.main()
