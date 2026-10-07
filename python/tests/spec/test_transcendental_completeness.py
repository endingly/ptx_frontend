"""Regression coverage for explicit transcendental instruction cohorts."""

import unittest

from ptx_frontend.spec.database import load_codegen_database
from ptx_frontend.spec.model import ModifierPresence, OperandKind, OperandRegisterWidthPolicy
from ptx_frontend.spec.resources import packaged_spec_dir


class TranscendentalCompletenessTests(unittest.TestCase):
    """Preserve typed approximate transcendental forms across float and half types."""

    #: Canonical PTX and target minima per variant, kept independent because the
    #: FP32 cohorts predate the half and bfloat architecture floors.
    EXPECTED_MINIMA = {
        "sin_approx_f32": {"ptx": "1.4", "sm": 0},
        "cos_approx_f32": {"ptx": "1.4", "sm": 0},
        "lg2_approx_f32": {"ptx": "1.4", "sm": 0},
        "ex2_approx_f32": {"ptx": "1.4", "sm": 0},
        "ex2_approx_f16": {"ptx": "7.0", "sm": 75},
        "ex2_approx_f16x2": {"ptx": "7.0", "sm": 75},
        "ex2_approx_ftz_bf16": {"ptx": "7.8", "sm": 90},
        "ex2_approx_ftz_bf16x2": {"ptx": "7.8", "sm": 90},
        "tanh_approx_f32": {"ptx": "7.0", "sm": 75},
        "tanh_approx_f16": {"ptx": "7.0", "sm": 75},
        "tanh_approx_f16x2": {"ptx": "7.0", "sm": 75},
        "tanh_approx_bf16": {"ptx": "7.8", "sm": 90},
        "tanh_approx_bf16x2": {"ptx": "7.8", "sm": 90},
    }

    @classmethod
    def setUpClass(cls) -> None:
        """Load the five transcendental instruction specifications by opcode."""

        cls.instructions = {
            instruction.opcode: {variant.name: variant for variant in instruction.variants}
            for instruction in load_codegen_database(spec_dir=packaged_spec_dir()).instructions
            if instruction.opcode in {"sin", "cos", "lg2", "ex2", "tanh"}
        }

    def modifiers(self, opcode: str, name: str) -> dict:
        """Return one variant's modifiers keyed by slot name."""

        return {modifier.name: modifier for modifier in self.instructions[opcode][name].modifiers}

    def test_models_complete_scalar_and_low_precision_variant_sets(self) -> None:
        """Require the FP32 cohorts plus both half and bfloat cohorts for ex2 and tanh."""

        for opcode in ("sin", "cos", "lg2"):
            self.assertEqual(set(self.instructions[opcode]), {f"{opcode}_approx_f32"})
        self.assertEqual(
            set(self.instructions["ex2"]),
            {
                "ex2_approx_f32",
                "ex2_approx_f16",
                "ex2_approx_f16x2",
                "ex2_approx_ftz_bf16",
                "ex2_approx_ftz_bf16x2",
            },
        )
        self.assertEqual(
            set(self.instructions["tanh"]),
            {
                "tanh_approx_f32",
                "tanh_approx_f16",
                "tanh_approx_f16x2",
                "tanh_approx_bf16",
                "tanh_approx_bf16x2",
            },
        )

    def test_assigns_independent_ptx_and_target_minima(self) -> None:
        """Keep every cohort's PTX and architecture floor separately enforced."""

        modeled = {
            name for variants in self.instructions.values() for name in variants
        }
        self.assertEqual(modeled, set(self.EXPECTED_MINIMA))

        for name, expected in self.EXPECTED_MINIMA.items():
            opcode = name.split("_", 1)[0]
            self.assertEqual(dict(self.instructions[opcode][name].availability), expected, name)

    def test_keeps_approx_typed_and_ftz_cohort_specific(self) -> None:
        """Keep the required bfloat FTZ cohort distinct from the FTZ-free half cohorts."""

        for opcode, variants in self.instructions.items():
            for name in variants:
                approx = self.modifiers(opcode, name)["approx"]
                self.assertIs(approx.presence, ModifierPresence.FIXED, name)
                self.assertIs(approx.value, True, name)

        for opcode in ("sin", "cos", "lg2", "ex2"):
            self.assertIs(
                self.modifiers(opcode, f"{opcode}_approx_f32")["ftz"].presence,
                ModifierPresence.OPTIONAL,
            )

        for name in ("ex2_approx_f16", "ex2_approx_f16x2"):
            self.assertIs(self.modifiers("ex2", name)["ftz"].presence, ModifierPresence.ABSENT, name)

        for name in ("ex2_approx_ftz_bf16", "ex2_approx_ftz_bf16x2"):
            modifiers = self.modifiers("ex2", name)
            self.assertIs(modifiers["ftz"].presence, ModifierPresence.FIXED)
            self.assertIs(modifiers["ftz"].value, True)

        # No TANH cohort accepts FTZ, so the slot is absent everywhere rather than
        # an optional spelling that could be relaxed into the model.
        for name in self.instructions["tanh"]:
            self.assertNotIn("ftz", self.modifiers("tanh", name), name)

    def test_declares_type_specific_register_containers(self) -> None:
        """Pin each cohort's container contract, including register-only half forms."""

        for opcode, name in (
            ("sin", "sin_approx_f32"),
            ("cos", "cos_approx_f32"),
            ("lg2", "lg2_approx_f32"),
            ("ex2", "ex2_approx_f32"),
            ("tanh", "tanh_approx_f32"),
        ):
            operands = self.instructions[opcode][name].operand_layouts[0].operands
            self.assertEqual(operands[0].kind, OperandKind.REGISTER, name)
            self.assertEqual(operands[1].kind, OperandKind.REGISTER_OR_IMMEDIATE, name)
            self.assertEqual(
                tuple(operand.register_width_policy for operand in operands),
                (OperandRegisterWidthPolicy.SAME_WIDTH,) * 2,
                name,
            )

        expected = {
            ("ex2", "ex2_approx_f16"): ("f16", OperandRegisterWidthPolicy.SAME_WIDTH),
            ("ex2", "ex2_approx_f16x2"): ("f16x2", OperandRegisterWidthPolicy.SAME_WIDTH),
            ("ex2", "ex2_approx_ftz_bf16"): ("b16", OperandRegisterWidthPolicy.EXACT),
            ("ex2", "ex2_approx_ftz_bf16x2"): ("b32", OperandRegisterWidthPolicy.EXACT),
            ("tanh", "tanh_approx_f16"): ("f16", OperandRegisterWidthPolicy.SAME_WIDTH),
            ("tanh", "tanh_approx_f16x2"): ("f16x2", OperandRegisterWidthPolicy.SAME_WIDTH),
            ("tanh", "tanh_approx_bf16"): ("b16", OperandRegisterWidthPolicy.EXACT),
            ("tanh", "tanh_approx_bf16x2"): ("b32", OperandRegisterWidthPolicy.EXACT),
        }
        for (opcode, name), (container, policy) in expected.items():
            operands = self.instructions[opcode][name].operand_layouts[0].operands
            self.assertEqual(tuple(operand.kind for operand in operands), (OperandKind.REGISTER,) * 2, name)
            self.assertEqual(
                tuple(operand.type_expression.scalar_type for operand in operands),
                (container,) * 2,
                name,
            )
            self.assertEqual(
                tuple(operand.register_width_policy for operand in operands),
                (policy,) * 2,
                name,
            )


if __name__ == "__main__":
    unittest.main()
