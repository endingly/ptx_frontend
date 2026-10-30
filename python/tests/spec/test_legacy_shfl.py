"""Exact legacy shuffle layouts and their preserved typed destination path."""

import tempfile
import unittest
from pathlib import Path

from ptx_frontend.code_gen.emit.availability import emit_availability
from ptx_frontend.code_gen.context import build_generation_context
from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.code_gen.emit.resolved_source import generate_resolved_opcode_source
from ptx_frontend.ir.resolved_ir import from_instruction_spec
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import ModifierPresence, OperandKind


class LegacyShuffleTests(unittest.TestCase):
    """Keep all four mask-free source forms separate from synchronized forms."""

    @classmethod
    def setUpClass(cls) -> None:
        """Normalize the complete database so selector overlaps remain errors."""
        cls.database = get_packaged_spec_database()
        cls.instruction = next(item for item in cls.database.instructions if item.opcode == "shfl")
        cls.variants = {item.name: item for item in cls.instruction.variants}
        cls.resolved = {item.variant_id: item for item in from_instruction_spec(cls.instruction).variants}

    def test_exact_inventory_targets_and_emission(self) -> None:
        """Every mode carries the complete exact generic target set and PTX3.0."""
        names = {f"shfl_{mode}_b32" for mode in ("up", "down", "bfly", "idx")}
        self.assertEqual(set(self.variants) - {f"shfl_sync_{mode}_b32" for mode in
                                             ("up", "down", "bfly", "idx")}, names)
        expected = {"any_of": [{"ptx": "3.0", "targets": [f"sm_{sm}" for sm in (30, 32, 35, 37, 50, 52, 53, 60, 61, 62)]}]}
        for name in names:
            with self.subTest(variant=name):
                self.assertEqual(self.variants[name].availability, expected)
                self.assertEqual(dict(self.resolved[name].availability), expected)
                emitted = emit_availability(expected)
                self.assertIn(".any_of_count = 1", emitted)
                self.assertEqual(emitted.count(".minimum_ptx_version = {3, 0}"), 1)
                self.assertIn(".exact_targets = checker::kAvailabilityTargets<", emitted)
                for sm in (30, 32, 35, 37, 50, 52, 53, 60, 61, 62):
                    self.assertIn(f"AvailabilityTarget{{ {{{sm}}}, base::TargetFlavor::Generic }}", emitted)

    def test_complete_destination_layout_and_control_contract_matrix(self) -> None:
        """Reuse synchronized data/predicate and b/c contracts, omitting only the mask."""
        for mode in ("up", "down", "bfly", "idx"):
            name = f"shfl_{mode}_b32"
            variant = self.variants[name]
            sync = self.variants[f"shfl_sync_{mode}_b32"]
            self.assertEqual([layout.name for layout in variant.operand_layouts],
                             ["without_predicate", "with_predicate"])
            for layout, synchronized in zip(variant.operand_layouts, sync.operand_layouts, strict=True):
                self.assertEqual(layout.operands, synchronized.operands[:4])
                self.assertEqual(len(layout.operands), 4)
                self.assertEqual([operand.name for operand in layout.operands], ["dst", "src", "lane", "clamp"])
            bare, paired = variant.operand_layouts
            self.assertIs(bare.operands[0].kind, OperandKind.REGISTER)
            self.assertIs(paired.operands[0].kind, OperandKind.SHFL_DESTINATION)
            self.assertFalse(paired.operands[0].allow_destination_sink)
            self.assertFalse(paired.operands[0].allow_predicate_sink)
            modifiers = {item.name: item for item in variant.modifiers}
            self.assertIs(modifiers["sync"].presence, ModifierPresence.ABSENT)
            self.assertIs(modifiers[mode].presence, ModifierPresence.FIXED)
            self.assertIs(modifiers[mode].value, True)
            self.assertEqual(modifiers["type"].value, "b32")
            resolved = self.resolved[name]
            self.assertEqual([field.source_name for field in resolved.modifier_fields], [mode, "type"])
            for layout in resolved.operand_layouts:
                self.assertEqual([field.name for field in layout.fields], ["dst", "src", "lane", "clamp"])

    def test_emitted_source_retains_all_eight_paired_destination_paths(self) -> None:
        """Generation keeps synchronized and legacy destination checks in separate forms."""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "shfl.cpp"
            backend = load_cpp_backend(Path(__file__).resolve().parents[3] /
                                       "instructions/ptx_cpp_backend_spec/ptx_frontend.yaml")
            generate_resolved_opcode_source(build_generation_context(self.database, backend),
                                           category="data_movement", opcode="shfl", output_path=path)
            source = path.read_text(encoding="utf-8")
        self.assertEqual(source.count(".actual_shape = check_end::OperandShape::ShflDestination,"), 8)
        for mode in ("Up", "Down", "Bfly", "Idx"):
            self.assertIn(f"Shfl{mode}B32", source)


if __name__ == "__main__":
    unittest.main()
