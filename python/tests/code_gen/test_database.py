from pathlib import Path
import tempfile
from typing import Any, cast
import unittest

import yaml
from jsonschema import Draft202012Validator

from code_gen.database import load_codegen_database
from code_gen.load_yaml import load_yaml
from code_gen.gen_resolved_checker_descriptor import _emit_availability
from code_gen.normalize import normalize_availability


REPO_ROOT = Path(__file__).resolve().parents[3]
SCHEMA = REPO_ROOT / "instructions/schemas/ptx-instr-v1.schema.yaml"


def _variant(name: str, type_value: str) -> dict[str, object]:
    return {
        "name": name,
        "availability": {"ptx": "1.0", "sm": 0},
        "modifiers": [
            {
                "name": "type",
                "kind": "type",
                "presence": "fixed",
                "domain": "scalar_types",
                "value": type_value,
            }
        ],
        "operands": [],
    }


def _spec(
    *,
    category: str,
    codegen_category: str,
    variant_name: str,
    type_value: str,
) -> dict[str, object]:
    return {
        "schema": "ptx-instr/v1",
        "ptx_isa": "9.2",
        "category": category,
        "codegen_category": codegen_category,
        "instructions": [
            {
                "opcode": "add",
                "syntax": f"add.{type_value} d, a, b",
                "variants": [_variant(variant_name, type_value)],
            }
        ],
    }


class CodegenDatabaseMergeTests(unittest.TestCase):
    def _load(self, *specs: dict[str, object]):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for index, spec in enumerate(specs):
                (root / f"{index:02}.yaml").write_text(
                    yaml.safe_dump(spec, sort_keys=False), encoding="utf-8"
                )
            return load_codegen_database(spec_dir=root)

    def test_merges_opcode_definitions_in_file_order(self) -> None:
        database = self._load(
            _spec(
                category="integer_arithmetic",
                codegen_category="arithmetic",
                variant_name="add_integer",
                type_value="u32",
            ),
            _spec(
                category="floating_point",
                codegen_category="arithmetic",
                variant_name="add_float",
                type_value="f32",
            ),
        )

        self.assertEqual(len(database.instructions), 1)
        add = database.instructions[0]
        self.assertEqual(add.opcode, "add")
        self.assertEqual(add.codegen_category, "arithmetic")
        self.assertEqual(
            add.source_categories,
            ("integer_arithmetic", "floating_point"),
        )
        self.assertEqual(
            [variant.name for variant in add.variants],
            ["add_integer", "add_float"],
        )
        self.assertEqual(
            add.syntax_forms,
            ("add.u32 d, a, b", "add.f32 d, a, b"),
        )

    def test_requires_file_level_categories(self) -> None:
        missing_category = _spec(
            category="integer_arithmetic",
            codegen_category="arithmetic",
            variant_name="add_integer",
            type_value="u32",
        )
        del missing_category["category"]
        with self.assertRaisesRegex(ValueError, "category"):
            self._load(missing_category)

        missing_codegen_category = _spec(
            category="integer_arithmetic",
            codegen_category="arithmetic",
            variant_name="add_integer",
            type_value="u32",
        )
        del missing_codegen_category["codegen_category"]
        with self.assertRaisesRegex(ValueError, "codegen_category"):
            self._load(missing_codegen_category)

    def test_rejects_schema_violation_before_normalization(self) -> None:
        invalid = _spec(
            category="integer_arithmetic",
            codegen_category="arithmetic",
            variant_name="add_integer",
            type_value="u32",
        )
        invalid["unexpected"] = True

        with self.assertRaisesRegex(ValueError, "unexpected"):
            self._load(invalid)

    def test_rejects_codegen_category_disagreement(self) -> None:
        with self.assertRaisesRegex(ValueError, "disagree on codegen_category"):
            self._load(
                _spec(
                    category="integer_arithmetic",
                    codegen_category="integer_arithmetic",
                    variant_name="add_integer",
                    type_value="u32",
                ),
                _spec(
                    category="floating_point",
                    codegen_category="floating_point",
                    variant_name="add_float",
                    type_value="f32",
                ),
            )

    def test_rejects_duplicate_variant_ids_after_merge(self) -> None:
        with self.assertRaisesRegex(ValueError, "duplicate variant ids"):
            self._load(
                _spec(
                    category="integer_arithmetic",
                    codegen_category="arithmetic",
                    variant_name="add_value",
                    type_value="u32",
                ),
                _spec(
                    category="floating_point",
                    codegen_category="arithmetic",
                    variant_name="add_value",
                    type_value="f32",
                ),
            )

    def test_rejects_generated_cpp_variant_name_collision(self) -> None:
        with self.assertRaisesRegex(ValueError, "collide in C\\+\\+"):
            self._load(
                _spec(
                category="integer_arithmetic",
                codegen_category="arithmetic",
                variant_name="add_a__b",
                type_value="u32",
                ),
                _spec(
                    category="floating_point",
                    codegen_category="arithmetic",
                    variant_name="add_a_b",
                    type_value="f32",
                ),
            )

    def test_rejects_variant_id_without_opcode_prefix(self) -> None:
        with self.assertRaisesRegex(ValueError, "without required prefix"):
            self._load(
                _spec(
                    category="integer_arithmetic",
                    codegen_category="arithmetic",
                    variant_name="integer",
                    type_value="u32",
                )
            )

    def test_rejects_overlapping_modifier_combinations(self) -> None:
        with self.assertRaisesRegex(ValueError, "overlapping modifier combination"):
            self._load(
                _spec(
                    category="integer_arithmetic",
                    codegen_category="arithmetic",
                    variant_name="add_first",
                    type_value="u32",
                ),
                _spec(
                    category="floating_point",
                    codegen_category="arithmetic",
                    variant_name="add_second",
                    type_value="u32",
                ),
            )

    def test_allows_one_spelling_to_bind_different_slots_across_variants(
        self,
    ) -> None:
        first = _spec(
            category="integer_arithmetic",
            codegen_category="arithmetic",
            variant_name="add_integer",
            type_value="u32",
        )
        second = _spec(
            category="floating_point",
            codegen_category="arithmetic",
            variant_name="add_float",
            type_value="f32",
        )
        instructions = cast(list[dict[str, Any]], second["instructions"])
        variants = cast(list[dict[str, Any]], instructions[0]["variants"])
        modifiers = cast(list[dict[str, Any]], variants[0]["modifiers"])
        modifiers[0]["name"] = "result_type"
        modifiers.append(
            {
                "name": "input_type",
                "kind": "type",
                "presence": "fixed",
                "domain": "scalar_types",
                "value": "f16",
            }
        )

        database = self._load(first, second)

        self.assertEqual(len(database.instructions), 1)
        self.assertEqual(len(database.instructions[0].variants), 2)

    def test_allows_one_spelling_to_bind_ordered_slots_in_one_variant(self) -> None:
        spec = _spec(
            category="floating_point",
            codegen_category="arithmetic",
            variant_name="add_mixed",
            type_value="f32",
        )
        instructions = cast(list[dict[str, Any]], spec["instructions"])
        variants = cast(list[dict[str, Any]], instructions[0]["variants"])
        modifiers = cast(list[dict[str, Any]], variants[0]["modifiers"])
        modifiers[0]["name"] = "result_type"
        modifiers.append(
            {
                "name": "input_type",
                "kind": "type",
                "presence": "fixed",
                "domain": "scalar_types",
                "value": "f32",
            }
        )

        database = self._load(spec)

        self.assertEqual(len(database.instructions), 1)
        self.assertEqual(len(database.instructions[0].variants), 1)

    def test_rejects_optional_slot_with_repeated_spelling(self) -> None:
        spec = _spec(
            category="floating_point",
            codegen_category="arithmetic",
            variant_name="add_mixed",
            type_value="f16",
        )
        instructions = cast(list[dict[str, Any]], spec["instructions"])
        variants = cast(list[dict[str, Any]], instructions[0]["variants"])
        variants[0]["modifiers"] = [
            {
                "name": "optional_type",
                "kind": "type",
                "presence": "optional",
                "domain": "scalar_types",
                "default": "f16",
                "values": ["f16"],
            },
            {
                "name": "required_type",
                "kind": "type",
                "presence": "fixed",
                "domain": "scalar_types",
                "value": "f16",
            },
        ]

        with self.assertRaisesRegex(ValueError, "optional slot"):
            self._load(spec)

    def test_does_not_treat_different_modifier_orders_as_overlap(self) -> None:
        spec = _spec(
            category="floating_point",
            codegen_category="arithmetic",
            variant_name="add_first",
            type_value="f32",
        )
        instructions = cast(list[dict[str, Any]], spec["instructions"])
        variants = cast(list[dict[str, Any]], instructions[0]["variants"])
        variants[0]["modifiers"] = [
            {
                "name": "first_type",
                "kind": "type",
                "presence": "fixed",
                "domain": "scalar_types",
                "value": "f32",
            },
            {
                "name": "second_type",
                "kind": "type",
                "presence": "fixed",
                "domain": "scalar_types",
                "value": "f16",
            },
        ]
        variants.append(
            {
                "name": "add_second",
                "availability": {"ptx": "1.0", "sm": 0},
                "modifiers": [
                    {
                        "name": "first_type",
                        "kind": "type",
                        "presence": "fixed",
                        "domain": "scalar_types",
                        "value": "f16",
                    },
                    {
                        "name": "second_type",
                        "kind": "type",
                        "presence": "fixed",
                        "domain": "scalar_types",
                        "value": "f32",
                    },
                ],
                "operands": [],
            }
        )

        database = self._load(spec)

        self.assertEqual(
            [variant.name for variant in database.instructions[0].variants],
            ["add_first", "add_second"],
        )


class AvailabilityNormalizationTests(unittest.TestCase):
    def test_legacy_and_dnf_availability(self) -> None:
        legacy = {"ptx": "9.0", "sm": 90, "family": "sm_120f"}
        self.assertEqual(normalize_availability(legacy), legacy)
        dnf = {"any_of": [
            {"ptx": "9.0", "sm": 100, "target": "sm_100a",
             "capabilities": ["tensor", "cluster"]},
            {"ptx": "9.2", "sm": 120},
        ]}
        self.assertEqual(normalize_availability(dnf), dnf)

    def test_emits_sm103f_minimum_family_feature(self) -> None:
        availability = {"ptx": "9.3", "sm": 103, "family": "sm_103f"}
        self.assertEqual(normalize_availability(availability), availability)
        self.assertIn(
            '.required_family = "sm_103f",',
            _emit_availability(normalize_availability(availability)),
        )

    def test_rejects_non_feature_legacy_families(self) -> None:
        for family in ("sm_90a", "sm_90", "sm_0f", "sm_90ff"):
            with self.assertRaisesRegex(ValueError, "availability family"):
                normalize_availability({"family": family})

    def test_rejects_invalid_dnf_availability(self) -> None:
        for availability in (
            {"any_of": []},
            {"any_of": [{}]},
            {"any_of": [{"target": "sm_90b"}]},
            {"any_of": [{"capabilities": []}]},
            {"any_of": [{"sm": 90}] * 5},
        ):
            with self.assertRaises((TypeError, ValueError)):
                normalize_availability(availability)

    def test_rejects_malformed_exact_targets_during_normalization(self) -> None:
        for target in ("sm_0", "sm_80b", "sm_80aa", "sm_4294967296", "", 80):
            with self.assertRaisesRegex(ValueError, "availability target"):
                normalize_availability({"any_of": [{"target": target}]})

    def test_accepts_maximum_exact_target_during_normalization(self) -> None:
        availability = {"any_of": [{"target": "sm_4294967295"}]}
        self.assertEqual(normalize_availability(availability), availability)
        source = _emit_availability(normalize_availability(availability))
        self.assertIn(".exact_target_architecture = {4294967295}", source)
        self.assertIn("TargetFlavor::Generic", source)

    def test_rejects_non_uint32_sm_during_normalization(self) -> None:
        for availability in (
            {"sm": 4294967296},
            {"sm": True},
            {"any_of": [{"sm": 4294967296}]},
            {"any_of": [{"sm": True}]},
        ):
            with self.assertRaisesRegex(ValueError, "availability SM version"):
                normalize_availability(availability)

    def test_accepts_uint32_sm_boundaries_during_normalization(self) -> None:
        for availability in (
            {"sm": 0},
            {"sm": 4294967295},
            {"any_of": [{"sm": 4294967295}]},
        ):
            self.assertEqual(normalize_availability(availability), availability)

    def test_availability_schema_dnf_boundaries(self) -> None:
        schema = load_yaml(SCHEMA)
        validator = Draft202012Validator({
            "$schema": schema["$schema"],
            "$defs": schema["$defs"],
            "$ref": "#/$defs/availability",
        })
        self.assertEqual(list(validator.iter_errors({"any_of": [{"sm": 100}]})), [])
        self.assertEqual(list(validator.iter_errors({"sm": 4294967295})), [])
        for availability in ({"any_of": []}, {"any_of": [{}]},
                             {"any_of": [{"sm": 100}] * 5},
                             {"sm": 4294967296}, {"sm": True},
                             {"family": "sm_90a"},
                             {"any_of": [{"target": 80}]}):
            self.assertTrue(list(validator.iter_errors(availability)))

    def test_dnf_generator_keeps_or_clauses_and_and_terms(self) -> None:
        source = _emit_availability({"any_of": [
            {"ptx": "9.0", "sm": 100, "target": "sm_100a",
             "capabilities": ["tensor", "cluster"]},
            {"sm": 120},
        ]})
        self.assertIn(".any_of_count = 2", source)
        self.assertIn("TargetFlavor::ArchitectureSpecific", source)
        self.assertIn('.capabilities = {{"tensor", "cluster"}}', source)

    def test_dnf_emitter_handles_all_exact_target_flavors(self) -> None:
        source = _emit_availability(normalize_availability({"any_of": [
            {"target": "sm_80", "capabilities": ["tensor", "cluster"]},
            {"target": "sm_90a"},
            {"target": "sm_100f"},
        ]}))
        self.assertIn(".any_of_count = 3", source)
        self.assertIn(".has_exact_target = true", source)
        self.assertIn(".exact_target_architecture = {80}", source)
        self.assertIn("TargetFlavor::Generic", source)
        self.assertIn(".exact_target_architecture = {90}", source)
        self.assertIn("TargetFlavor::ArchitectureSpecific", source)
        self.assertIn(".exact_target_architecture = {100}", source)
        self.assertIn("TargetFlavor::FamilySpecific", source)
        self.assertIn('.capabilities = {{"tensor", "cluster"}}', source)

    def test_emitter_rejects_unvalidated_sm(self) -> None:
        for availability in ({"sm": 4294967296}, {"any_of": [{"sm": True}]}):
            with self.assertRaisesRegex(ValueError, "availability SM version"):
                _emit_availability(availability)
        with self.assertRaisesRegex(ValueError, "availability target"):
            _emit_availability({"any_of": [{"target": "sm_4294967296"}]})


if __name__ == "__main__":
    unittest.main()
