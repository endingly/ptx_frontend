"""Descriptor inventory for PTX 9.3 ordinary and tensor-map prefetch."""

import unittest

from jsonschema import Draft202012Validator
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.resources import packaged_spec_schema
from ptx_frontend.spec.database import get_packaged_spec_database


class PrefetchCompletenessTest(unittest.TestCase):
    """Guard the supported prefetch topology and target minima."""

    @classmethod
    def setUpClass(cls) -> None:
        """Read the canonical packaged specification once."""
        database = get_packaged_spec_database()
        cls.instructions = {
            instruction.opcode: instruction
            for instruction in database.instructions
            if instruction.opcode in {"prefetch", "prefetchu"}
        }

    def test_prefetch_variants_and_availability(self) -> None:
        """Separate ordinary, eviction, and tensor-map forms by target."""
        variants = {
            variant.name: variant
            for variant in self.instructions["prefetch"].variants
        }
        self.assertEqual(
            set(variants),
            {
                "prefetch_generic_l1", "prefetch_generic_l2",
                "prefetch_global_l1", "prefetch_global_l2",
                "prefetch_local_l1", "prefetch_local_l2",
                "prefetch_global_l2_evict",
                "prefetch_const_tensormap", "prefetch_param_tensormap",
                "prefetch_generic_tensormap",
            },
        )
        for name, variant in variants.items():
            expected = (
                {"ptx": "8.0", "sm": 90} if "tensormap" in name else
                {"ptx": "7.4", "sm": 80} if "evict" in name else
                {"ptx": "2.0", "sm": 20}
            )
            self.assertEqual(variant.availability, expected)
        evict = next(
            modifier
            for modifier in variants["prefetch_global_l2_evict"].modifiers
            if modifier.name == "eviction_priority"
        )
        self.assertEqual(
            {value.value for value in evict.values},
            {"evict_last", "evict_normal"},
        )

    def test_entry_only_schema_is_optional_boolean(self) -> None:
        """Reject non-Boolean entry restrictions at the public YAML boundary."""
        schema = load_yaml(packaged_spec_schema())
        validator = Draft202012Validator({"$ref": "#/$defs/parameter_address_constraint",
                                          "$defs": schema["$defs"]})
        parameter = {"direction": "input", "function_availability": {"sm": 90}}
        self.assertTrue(validator.is_valid(parameter))
        for value in (True, False):
            self.assertTrue(validator.is_valid(dict(parameter, entry_only=value)))
        for value in (1, 0, "true", None, [], {}):
            with self.subTest(value=value):
                self.assertFalse(validator.is_valid(dict(parameter, entry_only=value)))

    def test_prefetchu_stays_generic_l1_only(self) -> None:
        """Keep uniform prefetch suffix-free with an explicit known-space domain."""
        self.assertEqual(
            [variant.name for variant in self.instructions["prefetchu"].variants],
            ["prefetchu_l1"],
        )
        address = self.instructions["prefetchu"].variants[0].operand_layouts[0].operands[0]
        self.assertEqual(
            tuple(value.value for value in address.state_space_values),
            ("global", "const", "local", "shared"),
        )
        self.assertIsNone(address.parameter_constraint)

    def test_generic_tensormap_keeps_shared_noop_provenance(self) -> None:
        """Retain concrete spaces and require kernel input provenance for param."""
        variants = {
            variant.name: variant
            for variant in self.instructions["prefetch"].variants
        }
        generic = variants["prefetch_generic_tensormap"]
        self.assertEqual(len(generic.operand_layouts), 1)
        address = generic.operand_layouts[0].operands[0]
        self.assertEqual(
            tuple(value.value for value in address.state_space_values),
            ("global", "shared", "const", "param"),
        )
        self.assertEqual(address.parameter_constraint.direction, "input")
        self.assertTrue(address.parameter_constraint.entry_only)
        self.assertEqual(address.parameter_constraint.function_availability, {"ptx": "8.0", "sm": 90})
        for name in ("prefetch_const_tensormap", "prefetch_param_tensormap"):
            address = variants[name].operand_layouts[0].operands[0]
            self.assertNotIn(
                "shared",
                tuple(value.value for value in address.state_space_values),
            )
        self.assertFalse(variants["prefetch_param_tensormap"].operand_layouts[0]
                         .operands[0].parameter_constraint.entry_only)


if __name__ == "__main__":
    unittest.main()
