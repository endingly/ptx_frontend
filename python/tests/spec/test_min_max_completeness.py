"""Regression coverage for floating MIN/MAX cohorts and their layout topology."""

import unittest

from ptx_frontend.spec.database import load_codegen_database
from ptx_frontend.spec.model import ModifierPresence, OperandKind, OperandRegisterWidthPolicy
from ptx_frontend.spec.resources import packaged_spec_dir


class MinMaxCompletenessTests(unittest.TestCase):
    """Preserve the binary/ternary topology and the half/bfloat cohorts."""

    #: Canonical PTX and target minima per floating variant.
    EXPECTED_MINIMA = {
        "min_f32": {"ptx": "1.0", "sm": 0},
        "min_f64": {"ptx": "1.0", "sm": 13},
        "min_f16": {"ptx": "7.0", "sm": 80},
        "min_f16x2": {"ptx": "7.0", "sm": 80},
        "min_bf16": {"ptx": "7.0", "sm": 80},
        "min_bf16x2": {"ptx": "7.0", "sm": 80},
        "max_f32": {"ptx": "1.0", "sm": 0},
        "max_f64": {"ptx": "1.0", "sm": 13},
        "max_f16": {"ptx": "7.0", "sm": 80},
        "max_f16x2": {"ptx": "7.0", "sm": 80},
        "max_bf16": {"ptx": "7.0", "sm": 80},
        "max_bf16x2": {"ptx": "7.0", "sm": 80},
    }

    @classmethod
    def setUpClass(cls) -> None:
        """Load the two floating MIN/MAX instruction specifications by opcode."""

        cls.instructions = {
            instruction.opcode: {variant.name: variant for variant in instruction.variants}
            for instruction in load_codegen_database(spec_dir=packaged_spec_dir()).instructions
            if instruction.opcode in {"min", "max"}
        }

    def modifiers(self, opcode: str, name: str) -> dict:
        """Return one variant's modifiers keyed by slot name."""

        return {modifier.name: modifier for modifier in self.instructions[opcode][name].modifiers}

    def test_models_both_floating_cohorts_for_min_and_max(self) -> None:
        """Require the FP32/FP64 and half/bfloat variants to exist in pairs."""

        floating = {
            name
            for name in self.EXPECTED_MINIMA
        }
        for opcode in ("min", "max"):
            modeled = {
                name for name in self.instructions[opcode] if name.startswith(f"{opcode}_f")
                or name.startswith(f"{opcode}_bf")
            }
            self.assertEqual(modeled, {name for name in floating if name.startswith(opcode)})

        for name, expected in self.EXPECTED_MINIMA.items():
            opcode = name.split("_", 1)[0]
            self.assertEqual(dict(self.instructions[opcode][name].availability), expected, name)

    def test_keeps_binary_and_ternary_layouts_apart(self) -> None:
        """Require three-source topology to be a layout, not a shared spelling."""

        for opcode in ("min", "max"):
            variant = self.instructions[opcode][f"{opcode}_f32"]
            self.assertEqual(
                [layout.name for layout in variant.operand_layouts],
                ["binary", "ternary"],
            )
            self.assertEqual(
                [len(layout.operands) for layout in variant.operand_layouts], [3, 4]
            )
            # The paired sign/magnitude spelling is binary-only; the three-source
            # cohort takes `.abs` alone.
            self.assertEqual(
                [layout.forbidden_modifiers for layout in variant.operand_layouts],
                [("abs",), ("xorsign_abs",)],
            )
            binary, ternary = variant.operand_layouts
            self.assertEqual(dict(ternary.availability), {"ptx": "8.8", "sm": 100})
            self.assertEqual(dict(binary.availability), {})

    def test_keeps_sign_magnitude_coupling_and_ftz_cohorts_typed(self) -> None:
        """Keep `.xorsign.abs` a single coupled token and FTZ cohort-specific."""

        for opcode in ("min", "max"):
            # FP64 carries no FP32-only flag, so it is checked separately below.
            flagged = [
                f"{opcode}_{suffix}"
                for suffix in ("f32", "f16", "f16x2", "bf16", "bf16x2")
            ]
            for name in flagged:
                modifiers = self.modifiers(opcode, name)
                # No slot spells `.xorsign` without `.abs`, and the paired
                # spelling is one token rather than two independent flags.
                self.assertEqual(modifiers["xorsign_abs"].token, ".xorsign.abs", name)
                self.assertIs(modifiers["xorsign_abs"].presence, ModifierPresence.OPTIONAL, name)
                self.assertEqual(modifiers["nan"].token, ".NaN", name)

            for name in (f"{opcode}_f32", f"{opcode}_f16", f"{opcode}_f16x2"):
                self.assertIs(self.modifiers(opcode, name)["ftz"].presence, ModifierPresence.OPTIONAL, name)
            for name in (f"{opcode}_bf16", f"{opcode}_bf16x2"):
                self.assertIs(self.modifiers(opcode, name)["ftz"].presence, ModifierPresence.ABSENT, name)
            self.assertNotIn("ftz", self.modifiers(opcode, f"{opcode}_f64"))

            # Only the FP32 cohort carries the three-source `.abs` slot.
            for name in flagged:
                if name == f"{opcode}_f32":
                    self.assertIs(self.modifiers(opcode, name)["abs"].presence, ModifierPresence.OPTIONAL)
                else:
                    self.assertNotIn("abs", self.modifiers(opcode, name), name)

            # FP64 admits no FP32-only spelling at all.
            for slot in ("ftz", "nan", "xorsign_abs", "abs"):
                self.assertNotIn(slot, self.modifiers(opcode, f"{opcode}_f64"))

    def test_declares_per_cohort_register_containers(self) -> None:
        """Pin each cohort's container and immediate policy, including BF16 bit containers."""

        # The FP32/FP64 cohorts take their type from the modifier slot, so only
        # the half and bfloat cohorts bind a concrete container here. Only the
        # scalar cohorts admit floating literals; half and bfloat stay
        # register-only.
        containers = {}
        for opcode in ("min", "max"):
            containers[f"{opcode}_f32"] = (None, OperandRegisterWidthPolicy.SAME_WIDTH, True)
            containers[f"{opcode}_f64"] = (None, OperandRegisterWidthPolicy.SAME_WIDTH, True)
            containers[f"{opcode}_f16"] = (None, OperandRegisterWidthPolicy.SAME_WIDTH, False)
            containers[f"{opcode}_f16x2"] = (None, OperandRegisterWidthPolicy.SAME_WIDTH, False)
            containers[f"{opcode}_bf16"] = ("b16", OperandRegisterWidthPolicy.EXACT, False)
            containers[f"{opcode}_bf16x2"] = ("b32", OperandRegisterWidthPolicy.EXACT, False)

        for name, (container, policy, admits_literals) in containers.items():
            opcode = name.split("_", 1)[0]
            operands = self.instructions[opcode][name].operand_layouts[0].operands
            expected_kinds = (
                (OperandKind.REGISTER, OperandKind.REGISTER_OR_IMMEDIATE, OperandKind.REGISTER_OR_IMMEDIATE)
                if admits_literals
                else (OperandKind.REGISTER,) * 3
            )
            self.assertEqual(tuple(operand.kind for operand in operands), expected_kinds, name)
            self.assertEqual(
                tuple(operand.register_width_policy for operand in operands),
                (policy,) * 3,
                name,
            )
            if container is not None:
                self.assertEqual(
                    tuple(operand.type_expression.scalar_type for operand in operands),
                    (container,) * 3,
                    name,
                )

    def test_three_source_layout_admits_literals_in_every_source(self) -> None:
        """Keep the three-source sources immediate-capable like the binary ones."""

        for opcode in ("min", "max"):
            ternary = self.instructions[opcode][f"{opcode}_f32"].operand_layouts[1]
            self.assertEqual(
                tuple(operand.kind for operand in ternary.operands),
                (OperandKind.REGISTER,)
                + (OperandKind.REGISTER_OR_IMMEDIATE,) * 3,
            )


if __name__ == "__main__":
    unittest.main()
