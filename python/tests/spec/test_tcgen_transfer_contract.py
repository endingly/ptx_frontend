"""Check the bounded canonical register-transfer contract before C++ emission."""

from copy import deepcopy
import unittest

from ptx_frontend.spec.database import load_codegen_database
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.model import AsyncCompletionKind, OperandKind, SemanticRule
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.resources import packaged_spec_dir


SPEC_PATH = packaged_spec_dir() / "tensor_memory_data_movement.yaml"
TRANSFER_NAMES = (
    "tcgen05_ld", "tcgen05_ld_split", "tcgen05_st", "tcgen05_st_split",
    "tcgen05_ld_red_float", "tcgen05_ld_red_float_split",
    "tcgen05_ld_red_integer", "tcgen05_ld_red_integer_split",
    "tcgen05_wait_ld", "tcgen05_wait_st",
)


class TcgenTransferContractTests(unittest.TestCase):
    """Keep compact forms, typed values, and exact documented tuples aligned."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the packaged canonical source used by production generation."""

        database = load_codegen_database(spec_dir=packaged_spec_dir())
        instruction = next(item for item in database.instructions
                           if item.opcode == "tcgen05")
        cls.variants = {item.name: item for item in instruction.variants}

    def test_exact_transfer_forms_and_semantic_census(self) -> None:
        """Compact modifiers enumerate 318 legal transfer tuples, not 318 structs."""

        self.assertEqual(set(TRANSFER_NAMES), set(self.variants) - {
            "tcgen05_alloc_generic", "tcgen05_alloc_shared_cta",
            "tcgen05_dealloc", "tcgen05_relinquish_alloc_permit",
            *(name for name, variant in self.variants.items()
              if variant.rule in {SemanticRule.TENSOR_MEMORY_COMMIT,
                                  SemanticRule.TENSOR_MEMORY_FENCE,
                                  SemanticRule.TENSOR_MEMORY_COPY,
                                  SemanticRule.TENSOR_MEMORY_SHIFT}),
        })
        tuples = set()
        for name in TRANSFER_NAMES:
            variant = self.variants[name]
            mods = {item.name: item for item in variant.modifiers}
            if variant.rule is SemanticRule.TENSOR_MEMORY_WAIT:
                tuples.add(("wait", mods["wait"].value))
                continue
            shape_values = ([mods["shape"].value] if mods["shape"].value
                            else [item.value for item in mods["shape"].values])
            num_values = [item.value for item in mods["num"].values]
            for shape in shape_values:
                for num in num_values:
                    repeat = int(num[1:])
                    if shape == "s16x128b" and repeat > 64:
                        continue
                    if shape == "s16x256b" and repeat > 32:
                        continue
                    if variant.rule is SemanticRule.TENSOR_MEMORY_LOAD_REDUCTION:
                        types = ([mods["type"].value] if mods["type"].value
                                 else [item.value for item in mods["type"].values])
                        for op in ("min", "max"):
                            for scalar in types:
                                controls = ((False, False), (True, False),
                                            (False, True), (True, True)) if scalar == "f32" else ((False, False),)
                                for abs_value, nan_value in controls:
                                    tuples.add(("ld.red", shape, num, op, scalar,
                                                abs_value, nan_value))
                    else:
                        action = "st" if variant.rule is SemanticRule.TENSOR_MEMORY_STORE else "ld"
                        for packed in (False, True):
                            tuples.add((action, shape, num, packed))
        self.assertEqual(len(tuples), 318)
        self.assertEqual(sum(item[0] == "ld" for item in tuples), 74)
        self.assertEqual(sum(item[0] == "st" for item in tuples), 74)
        self.assertEqual(sum(item[0] == "ld.red" for item in tuples), 168)

    def test_typed_completion_topology_and_aliases(self) -> None:
        """Transfer direction, split source and reduction source order stay typed."""

        for name in TRANSFER_NAMES:
            variant = self.variants[name]
            operands = variant.operand_layouts[0].operands
            ids = tuple(item.name for item in operands)
            if "wait" in name:
                self.assertEqual(ids, ())
                expected = (AsyncCompletionKind.TCGEN_STORE_WAIT if name.endswith("st")
                            else AsyncCompletionKind.TCGEN_LOAD_WAIT)
                self.assertIs(variant.completion_kind, expected)
                continue
            self.assertIs(variant.completion_kind,
                          AsyncCompletionKind.TCGEN_STORE_WAIT if "_st" in name
                          else AsyncCompletionKind.TCGEN_LOAD_WAIT)
            self.assertIs(next(item for item in operands if item.name == "r").kind,
                          OperandKind.MATRIX_FRAGMENT)
            self.assertIs(next(item for item in operands if item.name == "taddr").kind,
                          OperandKind.TENSOR_MEMORY_ADDRESS_BRACKET)
            if "split" in name:
                self.assertIs(next(item for item in operands if item.name == "splitoff").kind,
                              OperandKind.TCGEN_HALF_SPLIT_OFFSET)
                self.assertEqual(ids[-1] if "_st" not in name else ids[1],
                                 "splitoff")
            if "_red_" in name:
                self.assertEqual(len(variant.modifier_order_aliases), 1)
                canonical = tuple(item.name for item in variant.modifiers)
                alias = variant.modifier_order_aliases[0]
                self.assertLess(canonical.index("red_op"), canonical.index("type"))
                self.assertLess(alias.index("type"), alias.index("red_op"))

    def test_rejects_canonical_contract_drift(self) -> None:
        """A malformed source form cannot silently broaden typed transfer roles."""

        source = load_yaml(SPEC_PATH)
        raw_variants = source["instructions"][0]["variants"]
        cases = (
            ("tcgen05_ld", lambda item: next(m for m in item["modifiers"]
                                               if m["name"] == "num")
             .update(values=["x1", "x3"])),
            ("tcgen05_ld_split", lambda item: next(m for m in item["modifiers"]
                                                     if m["name"] == "shape")
             .update(value="s32x32b")),
            ("tcgen05_st_split", lambda item: item["operands"][1]
             .update(kind="imm")),
            ("tcgen05_ld_red_float", lambda item: item["operands"][0]
             ["cardinality"].update(min=1)),
            ("tcgen05_wait_ld", lambda item: item.update(
                completion_kind="tcgen_store_wait")),
        )
        for name, mutate in cases:
            with self.subTest(form=name):
                changed = deepcopy(next(item for item in raw_variants
                                        if item["name"] == name))
                mutate(changed)
                candidate = {
                    "category": "tensor_memory_data_movement",
                    "codegen_category": "tensor_memory",
                    "instructions": [{"opcode": "tcgen05", "variants": [changed]}],
                }
                with self.assertRaises(ValueError):
                    normalize_instruction_spec(candidate)


if __name__ == "__main__":
    unittest.main()
