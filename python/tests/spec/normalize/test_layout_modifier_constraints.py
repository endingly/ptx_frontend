"""Boundary regressions for per-layout modifier constraints."""

import unittest

import jsonschema

from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.resources import packaged_spec_schema


#: Register operand slots a probe layout may declare, in arity order.
_OPERAND_SLOTS = (
    ("dst", "dst", "write"),
    ("src1", "src1", "read"),
    ("src2", "src2", "read"),
)


def _probe_operands(count: int) -> list[dict[str, object]]:
    """Return ``count`` register operands for one flat layout."""

    return [
        {
            "name": name,
            "kind": "reg",
            "role": role,
            "access": access,
            "type": {"expr": "modifier(type)"},
        }
        for name, role, access in _OPERAND_SLOTS[:count]
    ]


def _probe_spec(operand_layouts: list[dict[str, object]]) -> dict[str, object]:
    """Return one instruction whose single variant carries the given layouts."""

    return {
        "schema": "ptx-instr/v1",
        "ptx_isa": "9.3",
        "category": "arithmetic",
        "codegen_category": "arithmetic",
        "instructions": [
            {
                "opcode": "probe",
                "section": "9.7.3.11",
                "doc": "Probe instruction for layout modifier constraints.",
                "variants": [
                    {
                        "name": "probe_f32",
                        "availability": {"ptx": "1.0", "sm": 0},
                        "modifiers": [
                            {
                                "name": "ftz",
                                "kind": "flag",
                                "presence": "optional",
                                "default": False,
                                "token": ".ftz",
                            },
                            {
                                "name": "abs",
                                "kind": "flag",
                                "presence": "optional",
                                "default": False,
                                "token": ".abs",
                            },
                            {
                                "name": "type",
                                "kind": "type",
                                "domain": "scalar_types",
                                "presence": "fixed",
                                "value": "f32",
                            },
                        ],
                        "operand_layouts": operand_layouts,
                    }
                ],
            }
        ],
    }


class LayoutModifierConstraintTests(unittest.TestCase):
    """Keep forbidden-modifier declarations declared, omittable, and reachable."""

    def test_normalizes_forbidden_modifiers_per_layout(self) -> None:
        """Retain the declared slot names in declaration order."""

        (instruction,) = normalize_instruction_spec(
            _probe_spec(
                [
                    {
                        "name": "binary",
                        "operands": _probe_operands(1),
                        "forbidden_modifiers": ["abs"],
                    },
                    {"name": "ternary", "operands": _probe_operands(2)},
                ]
            )
        )
        layouts = instruction.variants[0].operand_layouts
        self.assertEqual(
            [layout.name for layout in layouts], ["binary", "ternary"]
        )
        self.assertEqual(layouts[0].forbidden_modifiers, ("abs",))
        self.assertEqual(layouts[1].forbidden_modifiers, ())

    def test_rejects_undeclared_forbidden_slot(self) -> None:
        """Require every forbidden name to be a declared modifier slot."""

        with self.assertRaises(ValueError) as raised:
            normalize_instruction_spec(
                _probe_spec(
                    [
                        {
                            "name": "binary",
                            "operands": _probe_operands(1),
                            "forbidden_modifiers": ["xorsign"],
                        }
                    ]
                )
            )
        self.assertIn("undeclared modifier slots", str(raised.exception))

    def test_rejects_forbidding_a_fixed_or_required_slot(self) -> None:
        """Reject forbidding a slot that cannot be omitted."""

        with self.assertRaises(ValueError) as raised:
            normalize_instruction_spec(
                _probe_spec(
                    [
                        {
                            "name": "binary",
                            "operands": _probe_operands(1),
                            "forbidden_modifiers": ["type"],
                        }
                    ]
                )
            )
        self.assertIn("optional or absent slots", str(raised.exception))

    def test_rejects_a_slot_forbidden_by_every_layout(self) -> None:
        """Require every optional slot to stay spellable through some layout."""

        with self.assertRaises(ValueError) as raised:
            normalize_instruction_spec(
                _probe_spec(
                    [
                        {
                            "name": "binary",
                            "operands": _probe_operands(1),
                            "forbidden_modifiers": ["abs"],
                        },
                        {
                            "name": "ternary",
                            "operands": _probe_operands(1),
                            "forbidden_modifiers": ["abs"],
                        },
                    ]
                )
            )
        self.assertIn("forbidden by every operand layout", str(raised.exception))

    def test_schema_accepts_forbidden_modifiers(self) -> None:
        """Keep the operand-layout property declared for the packaged schema."""

        variant = _probe_spec(
            [
                {
                    "name": "binary",
                    "operands": _probe_operands(1),
                    "forbidden_modifiers": ["abs"],
                }
            ]
        )
        schema = load_yaml(packaged_spec_schema())
        jsonschema.Draft202012Validator(schema).validate(variant)
        variant["instructions"][0]["variants"][0]["operand_layouts"][0][
            "forbidden_modifier"
        ] = ["abs"]
        with self.assertRaises(jsonschema.ValidationError):
            jsonschema.Draft202012Validator(schema).validate(variant)


if __name__ == "__main__":
    unittest.main()
