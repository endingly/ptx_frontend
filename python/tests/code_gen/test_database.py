from pathlib import Path
import tempfile
from typing import Any, cast
import unittest

import yaml

from code_gen.database import load_codegen_database


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


if __name__ == "__main__":
    unittest.main()
