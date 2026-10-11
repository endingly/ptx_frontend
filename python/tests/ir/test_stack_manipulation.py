"""Canonical stack-family metadata and lowering coverage."""
import copy
from pathlib import Path
import unittest
import yaml
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.ir.resolved_ir import from_instruction_spec, ResolvedValueKind
from ptx_frontend.spec.model import StackOperation


CATALOGUE = Path(__file__).resolve().parents[2] / "src/ptx_frontend/spec/resources/ptx_spec/stack_manipulation.yaml"


class StackManipulationTests(unittest.TestCase):
    """Require every source form and truthful typed result roles."""

    def test_complete_stack_forms(self):
        """Keep three operations, two widths and two allocation source layouts."""
        rows = {i.opcode: from_instruction_spec(i)
                for i in normalize_instruction_spec(yaml.safe_load(CATALOGUE.read_text()))
                if i.opcode in {"stacksave", "stackrestore", "alloca"}}
        self.assertEqual(set(rows), {"stacksave", "stackrestore", "alloca"})
        self.assertEqual(sum(len(v.operand_layouts) for i in rows.values()
                             for v in i.variants), 8)
        for opcode, instruction in rows.items():
            self.assertEqual({v.stack.width for v in instruction.variants}, {32, 64})
            for variant in instruction.variants:
                self.assertEqual(variant.stack.default_alignment, 8)
                for layout in variant.operand_layouts:
                    self.assertEqual(layout.fields[0].value_kind,
                        ResolvedValueKind.LOCAL_ALLOCATION_RESULT if opcode == "alloca"
                        else ResolvedValueKind.STACK_TOKEN)
                if opcode == "alloca":
                    self.assertIs(variant.stack.operation, StackOperation.ALLOCATE)

    def test_rejects_replaced_contract_metadata(self):
        """Catch operation, width, default, operand-role and layout drift."""
        original = yaml.safe_load(CATALOGUE.read_text())
        for mutation in range(8):
            raw = copy.deepcopy(original)
            variant = raw["instructions"][2]["variants"][0]
            if mutation == 0:
                variant["stack"]["operation"] = "save"
            elif mutation == 1:
                variant["stack"]["width"] = 64
            elif mutation == 2:
                variant["stack"]["default_alignment"] = 16
            elif mutation == 3:
                variant["operand_layouts"][0]["operands"][0]["kind"] = "stack_token"
            elif mutation == 4:
                variant["operand_layouts"][0]["operands"][1]["type"] = "u64"
            elif mutation == 5:
                variant["operand_layouts"][1]["operands"][2]["type"] = "u64"
            elif mutation == 6:
                variant["operand_layouts"].pop()
            else:
                del variant["stack"]
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                normalize_instruction_spec(raw)
