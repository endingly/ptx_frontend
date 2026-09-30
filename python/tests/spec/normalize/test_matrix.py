"""Canonical matrix metadata must agree with its operand and suffix facts."""

from copy import deepcopy
import unittest

from ptx_frontend.spec.model import MatrixFamily, MatrixFragmentRole
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.normalize.availability import normalize_availability
from ptx_frontend.spec.resources import packaged_spec_dir


def _matrix_seed() -> dict:
    """Return a minimal standalone form with one generated fragment count."""

    return {
        "category": "test",
        "codegen_category": "matrix",
        "instructions": [{
            "opcode": "ldmatrix",
            "variants": [{
                "name": "ldmatrix_sync_aligned_m8n8_x2_shared_b16",
                "availability": {"ptx": "6.5", "sm": 75},
                "modifiers": [
                    {"name": "sync", "kind": "flag", "presence": "fixed", "value": True},
                    {"name": "aligned", "kind": "flag", "presence": "fixed", "value": True},
                    {"name": "m8n8", "kind": "flag", "presence": "fixed", "value": True},
                    {"name": "x2", "kind": "flag", "presence": "fixed", "value": True},
                    {"name": "shared", "kind": "flag", "presence": "fixed", "value": True},
                    {"name": "type", "kind": "type", "domain": "scalar_types", "presence": "fixed", "value": "b16"},
                ],
                "operands": [
                    {"name": "dst", "kind": "reg_vector", "role": "dst", "access": "write",
                     "type": "b32", "vector": {"kind": "matrix_fragment", "arity": [2], "type_policy": "element"}},
                    {"name": "address", "kind": "addr", "role": "addr", "access": "read",
                     "state_space": ["shared"]},
                ],
                "matrix": {
                    "family": "ldmatrix", "m": 8, "n": 8, "k": 0,
                    "matrix_count": 2, "address_qualifier": "shared", "elements": {"d": "b16"},
                    "fragments": {"dst": "d"},
                },
            }],
        }],
    }


class MatrixNormalizationTests(unittest.TestCase):
    """Validate typed topology and one-source fragment cardinality."""

    def test_fragment_count_and_register_type_come_from_operand(self) -> None:
        """A matrix fragment does not duplicate operand packing facts."""

        variant = normalize_instruction_spec(_matrix_seed())[0].variants[0]
        self.assertIsNotNone(variant.matrix)
        self.assertIs(variant.matrix.family, MatrixFamily.LDMATRIX)
        self.assertEqual(variant.matrix.shape.k, 0)
        self.assertEqual(variant.matrix.fragments[0].register_count, 2)
        self.assertEqual(variant.matrix.fragments[0].register_type, "b32")
        self.assertIs(variant.matrix.fragments[0].role, MatrixFragmentRole.D)

    def test_rejects_metadata_drift_and_untyped_fragments(self) -> None:
        """Fixed source suffixes and register packs constrain metadata."""

        for change, message in (
            ({"m": 16}, "shape disagrees"),
            ({"matrix_count": 4}, "count disagrees"),
            ({"elements": {"d": "b8"}}, "element disagrees"),
            ({"fragments": {"address": "d"}}, "requires a register pack"),
        ):
            raw = deepcopy(_matrix_seed())
            raw["instructions"][0]["variants"][0]["matrix"].update(change)
            with self.subTest(change=change), self.assertRaisesRegex(ValueError, message):
                normalize_instruction_spec(raw)

    def test_six_target_clauses_fit_exact_modern_matrix_availability(self) -> None:
        """Represent three architecture and three later family target paths."""

        clauses = [
            {"ptx": "8.6", "target": f"sm_{arch}a"}
            for arch in (100, 110, 120)
        ] + [
            {"ptx": "8.8", "sm": arch, "family": f"sm_{arch}f"}
            for arch in (100, 110, 120)
        ]
        self.assertEqual(len(normalize_availability({"any_of": clauses})["any_of"]), 6)
        with self.assertRaisesRegex(ValueError, "one to six"):
            normalize_availability({"any_of": clauses + [clauses[0]]})

    def test_compressed_load_uses_one_exact_lexical_suffix(self) -> None:
        """Decompression packing stays typed although its suffix is one token."""

        source = load_yaml(packaged_spec_dir() /
                           "warp_level_matrix_multiply_accumulate.yaml")
        load = next(item for item in source["instructions"]
                    if item["opcode"] == "ldmatrix")
        sample = next(item for item in load["variants"]
                      if item["name"].endswith("b8x16_b6x16_p32"))
        suffixes = [modifier.get("token") for modifier in sample["modifiers"]]
        self.assertIn(".b8x16.b6x16_p32", suffixes)
        self.assertNotIn(".b6x16_p32", suffixes)
        specification = {"category": "test", "codegen_category": "matrix",
                         "instructions": [{"opcode": "ldmatrix", "variants": [sample]}]}
        normalized = normalize_instruction_spec(specification)[0].variants[0]
        self.assertEqual(normalized.matrix.source_packing.value, "b6x16_p32")
        self.assertEqual(normalized.matrix.destination_packing.value, "b8x16")
        changed = deepcopy(sample)
        changed["matrix"]["source_packing"] = "b4x16_p64"
        specification["instructions"][0]["variants"] = [changed]
        with self.assertRaisesRegex(ValueError, "source packing disagrees"):
            normalize_instruction_spec(specification)
        malformed = deepcopy(sample)
        next(modifier for modifier in malformed["modifiers"]
             if modifier.get("token") == ".b8x16.b6x16_p32")["token"] = (
                 ".b8x16.unsupported")
        specification["instructions"][0]["variants"] = [malformed]
        with self.assertRaisesRegex(ValueError, "source packing disagrees"):
            normalize_instruction_spec(specification)


if __name__ == "__main__":
    unittest.main()
