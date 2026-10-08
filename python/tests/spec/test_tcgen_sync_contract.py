"""Guard the closed canonical TCGEN synchronization forms."""

from copy import deepcopy
import unittest

from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.model import AsyncCompletionKind, OperandKind, SemanticRule
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.resources import packaged_spec_dir


SPEC_PATH = packaged_spec_dir() / "tensor_memory_data_movement.yaml"


class TcgenSyncContractTests(unittest.TestCase):
    """Ensure generated synchronization metadata comes from one closed source."""

    @classmethod
    def setUpClass(cls) -> None:
        """Read the same packaged source used by production generation."""
        database = get_packaged_spec_database()
        cls.variants = {variant.name: variant for instruction in database.instructions
                        if instruction.opcode == "tcgen05"
                        for variant in instruction.variants}

    def test_eight_commit_layouts_and_two_fences(self) -> None:
        """Group, address spelling and multicast are independent closed axes."""
        commits = [item for item in self.variants.values()
                   if item.rule is SemanticRule.TENSOR_MEMORY_COMMIT]
        fences = [item for item in self.variants.values()
                  if item.rule is SemanticRule.TENSOR_MEMORY_FENCE]
        self.assertEqual((len(commits), len(fences)), (8, 2))
        seen = set()
        for item in commits:
            mods = {modifier.name: modifier for modifier in item.modifiers}
            group = mods["cta_group"].values[0].value
            shared = "shared_cluster" in mods
            multicast = "multicast_cluster" in mods
            seen.add((group, shared, multicast))
            self.assertIs(item.completion_kind,
                          AsyncCompletionKind.TCGEN_MBARRIER_ARRIVE_ONE)
            operands = item.operand_layouts[0].operands
            self.assertEqual(tuple(operand.name for operand in operands),
                             ("mbar", "cta_mask") if multicast else ("mbar",))
            if multicast:
                self.assertIs(operands[1].kind, OperandKind.REGISTER)
                self.assertEqual(operands[1].type_expression.scalar_type, "u16")
            self.assertEqual(item.address_alignments[0].alignment, 8)
        self.assertEqual(len(seen), 8)
        self.assertEqual({next(modifier for modifier in item.modifiers
                               if modifier.name == "fence_direction").values[0].token
                          for item in fences},
                         {".fence::before_thread_sync",
                          ".fence::after_thread_sync"})
        self.assertTrue(all(item.completion_kind is AsyncCompletionKind.NONE
                            and not item.operand_layouts[0].operands for item in fences))

    def test_canonical_drift_fails_before_generation(self) -> None:
        """Malformed mask, qualifier and alignment contracts cannot lower."""
        raw = load_yaml(SPEC_PATH)["instructions"][0]["variants"]
        cases = (
            ("tcgen05_commit_group1_generic_multicast",
             lambda item: item["operands"][1].update(kind="reg_or_imm")),
            ("tcgen05_commit_group1_generic_multicast",
             lambda item: item["operands"].pop()),
            ("tcgen05_commit_group1_generic_single",
             lambda item: item["operands"].append(
                 {"name": "cta_mask", "kind": "reg", "role": "mask",
                  "access": "read", "type": "u16"})),
            ("tcgen05_commit_group2_shared_cluster_single",
             lambda item: item["constraints"][0].update(alignment=4)),
            ("tcgen05_commit_group2_shared_cluster_single",
             lambda item: next(modifier for modifier in item["modifiers"]
                               if modifier["name"] == "shared_cluster")
             .update(token=".shared::cta")),
            ("tcgen05_commit_group1_generic_single",
             lambda item: next(modifier for modifier in item["modifiers"]
                               if modifier["name"] == "cta_group")
             .update(values=[{"value": "cta_group::1",
                              "token": ".cta_group::2"}])),
            ("tcgen05_fence_after_thread_sync",
             lambda item: item["operands"].append(
                 {"name": "mbar", "kind": "addr", "role": "barrier",
                  "access": "read"})),
        )
        for name, mutation in cases:
            with self.subTest(name=name, mutation=mutation):
                changed = deepcopy(next(item for item in raw
                                        if item["name"] == name))
                mutation(changed)
                with self.assertRaises(ValueError):
                    normalize_instruction_spec({
                        "category": "tensor_memory_data_movement",
                        "codegen_category": "tensor_memory",
                        "instructions": [{"opcode": "tcgen05", "variants": [changed]}],
                    })


if __name__ == "__main__":
    unittest.main()
