"""Canonical matrix metadata must agree with its operand and suffix facts."""

from copy import deepcopy
import unittest

from ptx_frontend.spec.model import MatrixFamily, MatrixFragmentRole
from ptx_frontend.spec.normalize import normalize_instruction_spec


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
                    "matrix_count": 2, "elements": {"d": "b16"},
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


if __name__ == "__main__":
    unittest.main()
