"""Guard the canonical Tensor Memory allocation and permit forms."""

from copy import deepcopy
import unittest

from ptx_frontend.spec.database import load_codegen_database
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.model import ModifierKind, OperandKind, SemanticRule
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.resources import packaged_spec_dir


SPEC_PATH = packaged_spec_dir() / "tensor_memory_data_movement.yaml"
EXPECTED_AVAILABILITY = {
    "any_of": [
        {"ptx": "8.6", "sm": 100, "target": "sm_100a"},
        {"ptx": "8.8", "sm": 100, "family": "sm_100f"},
        {"ptx": "9.0", "sm": 110, "target": "sm_110a"},
        {"ptx": "9.0", "sm": 110, "family": "sm_110f"},
    ],
}


class TcgenAllocationContractTests(unittest.TestCase):
    """Keep source, normalized variants, and typed allocation roles aligned."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the same packaged instruction database used by generation."""

        database = load_codegen_database(spec_dir=packaged_spec_dir())
        cls.instruction = next(
            instruction for instruction in database.instructions
            if instruction.opcode == "tcgen05"
        )
        cls.variants = {variant.name: variant
                        for variant in cls.instruction.variants}

    def test_allocation_trio_and_two_written_slot_forms(self) -> None:
        """Preserve every first-slice action and the optional shared qualifier."""

        allocation_names = {
            "tcgen05_alloc_generic",
            "tcgen05_alloc_shared_cta",
            "tcgen05_dealloc",
            "tcgen05_relinquish_alloc_permit",
        }
        self.assertTrue(allocation_names <= set(self.variants))
        allocation = {name: self.variants[name] for name in allocation_names}
        self.assertEqual(
            {name: variant.rule for name, variant in allocation.items()},
            {
                "tcgen05_alloc_generic": SemanticRule.TENSOR_MEMORY_ALLOC,
                "tcgen05_alloc_shared_cta": SemanticRule.TENSOR_MEMORY_ALLOC,
                "tcgen05_dealloc": SemanticRule.TENSOR_MEMORY_DEALLOC,
                "tcgen05_relinquish_alloc_permit":
                    SemanticRule.TENSOR_MEMORY_RELINQUISH_ALLOC_PERMIT,
            },
        )
        self.assertEqual(
            {name: len(variant.operand_layouts[0].operands)
             for name, variant in allocation.items()},
            {
                "tcgen05_alloc_generic": 2,
                "tcgen05_alloc_shared_cta": 2,
                "tcgen05_dealloc": 2,
                "tcgen05_relinquish_alloc_permit": 0,
            },
        )
        for variant in allocation.values():
            with self.subTest(variant=variant.name):
                self.assertEqual(variant.availability, EXPECTED_AVAILABILITY)
                group = next(modifier for modifier in variant.modifiers
                             if modifier.name == "cta_group")
                self.assertIs(group.kind, ModifierKind.CTA_GROUP)
                self.assertEqual(tuple(value.value for value in group.values),
                                 ("cta_group::1", "cta_group::2"))

    def test_distinct_tensor_address_and_result_slot_contract(self) -> None:
        """The deallocation value is not the allocation result-slot address."""

        dealloc = self.variants["tcgen05_dealloc"]
        taddr, ncols = dealloc.operand_layouts[0].operands
        self.assertIs(taddr.kind, OperandKind.TENSOR_MEMORY_ADDRESS)
        self.assertIs(ncols.kind, OperandKind.REGISTER_OR_IMMEDIATE)
        self.assertEqual(ncols.type_expression.scalar_type, "u32")

        for name in ("tcgen05_alloc_generic", "tcgen05_alloc_shared_cta"):
            alloc = self.variants[name]
            dst, count = alloc.operand_layouts[0].operands
            self.assertIs(dst.kind, OperandKind.ADDRESS)
            self.assertIs(count.kind, OperandKind.REGISTER_OR_IMMEDIATE)
            self.assertEqual(alloc.address_alignments[0].alignment, 4)
            self.assertEqual(alloc.address_alignments[0].address_operands,
                             ("dst",))
        self.assertFalse(self.variants["tcgen05_alloc_generic"]
                         .operand_layouts[0].operands[0].state_space_values)
        shared = self.variants["tcgen05_alloc_shared_cta"]
        self.assertEqual(
            tuple(value.value for value in
                  shared.operand_layouts[0].operands[0].state_space_values),
            ("shared",),
        )

    def test_normalizer_rejects_allocation_contract_drift(self) -> None:
        """A malformed canonical form cannot silently weaken typed checks."""

        source = load_yaml(SPEC_PATH)
        instruction = source["instructions"][0]
        mutations = (
            ("group domain", "tcgen05_dealloc",
             lambda item: next(modifier for modifier in item["modifiers"]
                               if modifier["name"] == "cta_group")
             .update(values=["cta_group::1", "cta_group::3"])),
            ("taddr role", "tcgen05_dealloc",
             lambda item: item["operands"][0].update(kind="reg_or_imm")),
            ("column use type", "tcgen05_dealloc",
             lambda item: item["operands"][1].update(type="u64")),
            ("deallocation element type", "tcgen05_dealloc",
             lambda item: next(modifier for modifier in item["modifiers"]
                               if modifier["name"] == "type")
             .update(value="u32")),
            ("deallocation result-slot qualifier", "tcgen05_dealloc",
             lambda item: item["modifiers"].append(
                 {"name": "shared_cta", "kind": "flag",
                  "presence": "fixed", "value": True,
                  "token": ".shared::cta"})),
            ("result alignment", "tcgen05_alloc_generic",
             lambda item: item["constraints"][0].update(alignment=2)),
        )
        for label, name, mutate in mutations:
            with self.subTest(contract=label):
                changed = deepcopy(next(variant for variant in
                                        instruction["variants"]
                                        if variant["name"] == name))
                mutate(changed)
                candidate = {
                    "category": "tensor_memory_data_movement",
                    "codegen_category": "tensor_memory",
                    "instructions": [{"opcode": "tcgen05",
                                      "variants": [changed]}],
                }
                with self.assertRaises(ValueError):
                    normalize_instruction_spec(candidate)


if __name__ == "__main__":
    unittest.main()
