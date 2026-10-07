"""Keep Tensor Memory copy and shift source constraints closed and typed."""

from copy import deepcopy
import unittest

from ptx_frontend.spec.database import load_codegen_database
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.model import AsyncCompletionKind, SemanticRule
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.resources import packaged_spec_dir


SPEC = packaged_spec_dir() / "tensor_memory_data_movement.yaml"


class TcgenCopyShiftContractTests(unittest.TestCase):
    """Test the 36 copy tuples and two shift identities before code generation."""

    @classmethod
    def setUpClass(cls) -> None:
        """Read the packaged canonical source used by the generator."""
        database = load_codegen_database(spec_dir=packaged_spec_dir())
        instruction = next(item for item in database.instructions
                           if item.opcode == "tcgen05")
        cls.variants = {item.name: item for item in instruction.variants}

    def test_exact_tuples_and_written_alias(self) -> None:
        """Compact physical variants preserve every legal semantic combination."""
        copy = self.variants["tcgen05_cp"]
        shift = self.variants["tcgen05_shift"]
        self.assertIs(copy.rule, SemanticRule.TENSOR_MEMORY_COPY)
        self.assertIs(shift.rule, SemanticRule.TENSOR_MEMORY_SHIFT)
        self.assertEqual(len(copy.tcgen_copy_pairs), 6)
        self.assertEqual(len(copy.tcgen_copy_formats), 3)
        self.assertEqual(len(copy.tcgen_copy_pairs) *
                         len(copy.tcgen_copy_formats) * 2, 36)
        self.assertEqual(len(copy.operand_layouts[0].operands), 2)
        self.assertEqual(len(shift.operand_layouts[0].operands), 1)
        self.assertEqual(shift.modifier_order_aliases,
                         (("shift", "down", "cta_group"),))
        for variant in (copy, shift):
            self.assertIs(variant.completion_kind,
                          AsyncCompletionKind.TCGEN_MBARRIER_ARRIVE_ONE)
        self.assertEqual(sum(item.rule in {SemanticRule.TENSOR_MEMORY_COPY,
                                           SemanticRule.TENSOR_MEMORY_SHIFT}
                             for item in self.variants.values()), 2)

    def test_bad_canonical_pairs_and_operands_fail(self) -> None:
        """A drifted shape, format pair, or descriptor carrier cannot lower."""
        source = load_yaml(SPEC)
        bad = (
            ("tcgen05_cp", lambda item: item["tcgen_copy"]["shape_multicast_pairs"][0].__setitem__(1, "warpx4")),
            ("tcgen05_cp", lambda item: item["tcgen_copy"]["format_pairs"][1].__setitem__(1, False)),
            ("tcgen05_cp", lambda item: item["operands"][1].update(kind="reg_or_imm")),
            ("tcgen05_cp", lambda item: item["modifiers"][4].update(token=".warpx4")),
            ("tcgen05_shift", lambda item: item["operands"].append(
                {"name": "s_desc", "kind": "reg", "role": "src", "access": "read"})),
            ("tcgen05_shift", lambda item: item["modifier_order_aliases"].append(
                ["down", "shift", "cta_group"])),
            ("tcgen05_shift", lambda item: item.update(rule="tensor_memory.wait")),
        )
        for name, mutation in bad:
            with self.subTest(name=name, mutation=mutation):
                raw = deepcopy(source)
                variant = next(item for item in raw["instructions"][0]["variants"]
                               if item["name"] == name)
                mutation(variant)
                with self.assertRaises((ValueError, TypeError)):
                    normalize_instruction_spec(raw)


if __name__ == "__main__":
    unittest.main()
