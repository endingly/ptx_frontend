"""Boundary regressions for typed normalized PTX specification values."""

import os
from pathlib import Path
import subprocess
import sys
import unittest

from ptx_frontend.spec.model import (
    ModifierKind,
    ModifierPresence,
    OperandAccess,
    OperandKind,
    OperandRole,
    OperandTypeCompatibilityValueKind,
)
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.normalize.modifiers import normalize_modifier
from ptx_frontend.spec.normalize.operands import normalize_operand
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.resources import packaged_spec_schema
from ptx_frontend.spec.semantic_domains import (
    SEMANTIC_DOMAIN_VALUES,
    SemanticDomain,
)
from ptx_frontend.spec.synatax_shapes import OPERAND_SYNTAX_SHAPES
from ptx_frontend.ir.resolved_ir import _OPERAND_ALLOWED_SHAPES


class TypedSemanticNormalizationTests(unittest.TestCase):
    """Require raw YAML discriminators to become semantic enum members once."""

    def test_normalized_modifier_and_operand_fields_are_semantic_enums(self) -> None:
        """Parse modifier and operand discriminator spellings at the boundary."""

        modifier = normalize_modifier(
            {
                "name": "type",
                "kind": "type",
                "presence": "required",
                "values": ["u32"],
            },
            {},
        )
        operand = normalize_operand(
            {"name": "dst", "kind": "reg", "role": "dst", "access": "write"}
        )
        coordinate = normalize_operand(
            {
                "name": "coordinate",
                "kind": "tensor_coordinate",
                "role": "src",
                "access": "read",
                "cardinality": {"min": 1, "max": 2},
                "element_kinds": ["reg", "imm"],
            }
        )

        self.assertIs(modifier.kind, ModifierKind.TYPE)
        self.assertIs(modifier.presence, ModifierPresence.REQUIRED)
        self.assertIs(operand.kind, OperandKind.REGISTER)
        self.assertIs(operand.role, OperandRole.DESTINATION)
        self.assertIs(operand.access, OperandAccess.WRITE)
        self.assertEqual(len(coordinate.element_kinds), 2)
        self.assertIs(coordinate.element_kinds[0], OperandKind.REGISTER)
        self.assertIs(coordinate.element_kinds[1], OperandKind.IMMEDIATE)
        self.assertNotEqual(ModifierKind.TYPE, "type")
        self.assertNotEqual(ModifierPresence.REQUIRED, "required")
        self.assertNotEqual(OperandKind.REGISTER, "reg")
        self.assertNotEqual(OperandRole.DESTINATION, "dst")
        self.assertNotEqual(OperandAccess.WRITE, "write")

    def test_operand_type_compatibility_value_kind_is_a_semantic_enum(self) -> None:
        """Normalize checker-rule discriminators before resolved-IR lowering."""

        instruction = normalize_instruction_spec(
            {
                "category": "test",
                "codegen_category": "test",
                "instructions": [
                    {
                        "opcode": "sample",
                        "variants": [
                            {
                                "name": "sample_default",
                                "availability": {"ptx": "1.0"},
                                "operands": [
                                    {
                                        "name": "src",
                                        "kind": "reg",
                                        "role": "src",
                                        "access": "read",
                                    }
                                ],
                                "operand_type_compatibilities": [
                                    {
                                        "operand": "src",
                                        "value_kind": "special_register",
                                        "values": ["tid"],
                                        "instruction_width": 32,
                                        "effective_type": "u32",
                                        "availability": {"ptx": "1.0"},
                                    }
                                ],
                            }
                        ],
                    }
                ],
            }
        )[0]

        compatibility = instruction.variants[0].operand_type_compatibilities[0]
        self.assertIs(
            compatibility.value_kind,
            OperandTypeCompatibilityValueKind.SPECIAL_REGISTER,
        )

    def test_invalid_discriminator_is_rejected_at_normalization(self) -> None:
        """Reject unsupported YAML category spellings before downstream use."""

        with self.assertRaisesRegex(ValueError, "unsupported modifier kind"):
            normalize_modifier(
                {"name": "type", "kind": "not_a_kind", "presence": "required"},
                {},
            )
        with self.assertRaisesRegex(ValueError, "unsupported operand role"):
            normalize_operand(
                {"name": "dst", "kind": "reg", "role": "not_a_role"}
            )

    def test_closed_semantic_domains_reject_every_modifier_value_ingress(self) -> None:
        """Validate direct, expanded, fixed, and default semantic payloads."""

        cases = (
            (
                {"name": "rounding", "kind": "rounding", "presence": "required", "values": ["bad"]},
                {},
            ),
            (
                {"name": "rounding", "kind": "rounding", "presence": "required", "values": ["$rounding"]},
                {"rounding": ["bad"]},
            ),
            (
                {"name": "rounding", "kind": "rounding", "presence": "fixed", "value": "bad"},
                {},
            ),
            (
                {"name": "rounding", "kind": "rounding", "presence": "optional", "values": ["rn"], "default": "bad"},
                {},
            ),
            (
                {"name": "cache", "kind": "cache", "presence": "required", "values": ["unspecified"]},
                {},
            ),
        )
        for raw, value_sets in cases:
            with self.subTest(raw=raw):
                with self.assertRaisesRegex(ValueError, "unsupported semantic"):
                    normalize_modifier(raw, value_sets)

    def test_optional_flag_defaults_keep_boolean_contract(self) -> None:
        """Flags may default to either exact Boolean value outside value lists."""

        for default in (True, False):
            with self.subTest(default=default):
                normalized = normalize_modifier(
                    {"name": "flag", "kind": "flag", "presence": "optional", "default": default},
                    {},
                )
                self.assertIs(normalized.default, default)
        for default in (0, 1):
            with self.subTest(default=default):
                with self.assertRaisesRegex(ValueError, "unsupported semantic"):
                    normalize_modifier(
                        {"name": "flag", "kind": "flag", "presence": "optional", "default": default},
                        {},
                    )

    def test_frontend_catalogue_and_operand_tables_use_strict_enum_keys(self) -> None:
        """Keep schema vocabulary and internal lookup identity independent of C++."""

        scalar_types = SEMANTIC_DOMAIN_VALUES[SemanticDomain.SCALAR_TYPE]
        state_spaces = SEMANTIC_DOMAIN_VALUES[SemanticDomain.MEMORY_STATE_SPACE]
        semantics = SEMANTIC_DOMAIN_VALUES[SemanticDomain.MEMORY_CONSISTENCY]
        schema_domains = load_yaml(packaged_spec_schema())["$defs"]
        self.assertEqual(scalar_types, frozenset(schema_domains["ptx_type_name"]["enum"]))
        self.assertEqual(state_spaces, frozenset(schema_domains["state_space"]["enum"]))
        self.assertTrue(
            {
                "u2", "s2", "u4", "s4", "e2m1x2", "e2m3x2", "e3m2x2",
                "e4m3x4", "e5m2x4", "e2m1x4", "e2m3x4", "e3m2x4",
                "ue8m0x2", "s2f6x2",
            }.issubset(scalar_types)
        )
        self.assertTrue({"rni", "rmi", "rpi", "rna", "rs"}.issubset(
            SEMANTIC_DOMAIN_VALUES[SemanticDomain.ROUNDING_MODE]
        ))
        self.assertIn("sc", semantics)
        for table in (OPERAND_SYNTAX_SHAPES, _OPERAND_ALLOWED_SHAPES):
            self.assertTrue(all(type(kind) is OperandKind for kind in table))
            self.assertNotIn("reg", table)

    def test_wrapped_availability_value_is_checked_before_lowering(self) -> None:
        """Per-value availability objects cannot hide an unknown semantic value."""

        with self.assertRaisesRegex(ValueError, "unsupported semantic rounding_mode"):
            normalize_modifier(
                {
                    "name": "rounding",
                    "kind": "rounding",
                    "presence": "required",
                    "values": [{"value": "bad", "availability": {"ptx": "1.0"}}],
                },
                {},
            )

    def test_normalizer_imports_without_ir(self) -> None:
        """The spec layer owns normalized semantics without importing IR."""

        source = '''
import importlib.abc
import sys

class BlockIr(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        if fullname == "ptx_frontend.ir" or fullname.startswith("ptx_frontend.ir."):
            raise ImportError("IR is blocked")
        return None

sys.meta_path.insert(0, BlockIr())
from ptx_frontend.spec.normalize import normalize_modifier
assert normalize_modifier({"name": "type", "kind": "type", "presence": "fixed", "value": "u4"}, {}).value == "u4"
'''
        root = Path(__file__).resolve().parents[4]
        result = subprocess.run(
            [sys.executable, "-c", source],
            capture_output=True,
            text=True,
            env={**os.environ, "PYTHONPATH": str(root / "python" / "src")},
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
