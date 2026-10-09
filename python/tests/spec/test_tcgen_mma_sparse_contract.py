"""Fixed PTX 9.3 ordinary sparse MMA source and conditional facts."""

from dataclasses import replace
import unittest

from ptx_frontend.spec import tcgen_mma_operations as rules
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.normalize.tcgen_mma import validate_tcgen_mma_variant
from ptx_frontend.spec.resources import packaged_spec_dir


class TcgenMmaSparseContractTests(unittest.TestCase):
    """Keep sparse metadata topology separate from operational facts."""

    def test_four_canonical_forms_and_metadata_position(self) -> None:
        """Every sparse layout puts a typed metadata address before idesc."""

        raw = load_yaml(packaged_spec_dir() / "tensor_memory_data_movement.yaml")
        variants = {item.name: item for item in
                    normalize_instruction_spec(raw)[0].variants}
        expected = {"f16": 8, "tf32": 8, "f8f6f4": 4, "i8": 4}
        for kind, count in expected.items():
            variant = variants[f"tcgen05_mma_sp_{kind}"]
            self.assertEqual(len(variant.operand_layouts), count)
            self.assertEqual([item.name for item in variant.modifiers[:2]],
                             ["mma", "sp"])
            for layout in variant.operand_layouts:
                names = [item.name for item in layout.operands]
                self.assertEqual(names[names.index("b") + 1:names.index("idesc")],
                                 ["sp_meta"])
                metadata = layout.operands[names.index("sp_meta")]
                self.assertEqual(metadata.kind.value,
                                 "tensor_memory_address_bracket")
            with self.subTest(kind=kind, mutation="missing_metadata"):
                layout = variant.operand_layouts[0]
                malformed = replace(layout, operands=tuple(
                    item for item in layout.operands if item.name != "sp_meta"))
                with self.assertRaises(ValueError):
                    validate_tcgen_mma_variant(replace(
                        variant, operand_layouts=(malformed,) +
                        variant.operand_layouts[1:]))
            with self.subTest(kind=kind, mutation="misplaced_metadata"):
                layout = variant.operand_layouts[0]
                operands = list(layout.operands)
                index = next(index for index, item in enumerate(operands)
                             if item.name == "sp_meta")
                operands[index], operands[index + 1] = (
                    operands[index + 1], operands[index])
                with self.assertRaises(ValueError):
                    validate_tcgen_mma_variant(replace(
                        variant, operand_layouts=(replace(
                            layout, operands=tuple(operands)),) +
                        variant.operand_layouts[1:]))

    def test_sparse_rows_and_conditional_metadata(self) -> None:
        """Compressed K, metadata indices and half-lane claims are independent."""

        rules.validate_catalogue()
        for kind, k, compressed, valid in (
                ("F16", 32, 16, 14), ("Tf32", 16, 8, 4),
                ("F8F6F4", 64, 32, 14), ("I8", 64, 32, 14)):
            facts = rules.SparseKnownFacts(
                kind=kind, group=1, m=64, n=32, k=k,
                d_type="F32" if kind != "I8" else "S32",
                a_type={"F16": "F16", "Tf32": "TF32",
                        "F8F6F4": "E4M3", "I8": "S8"}[kind],
                b_type={"F16": "F16", "Tf32": "TF32",
                        "F8F6F4": "E4M3", "I8": "S8"}[kind],
                sparse=True, a_shared=False, sparse_selector=0,
                metadata_nibbles=(valid,), a_lane_half=0,
                d_lane_half=0, metadata_lane_half=0)
            report = rules.check_sparse_known_facts(facts)
            with self.subTest(kind=kind):
                self.assertTrue(report.known_facts_ok, report.violations)
                self.assertEqual((report.compressed_a_k, report.logical_b_k),
                                 (compressed, k))
                self.assertIn("live_metadata_contents", report.obligations)
                self.assertIn("target", report.obligations)
                self.assertIn("metadata_indices", rules.check_sparse_known_facts(
                    replace(facts, metadata_nibbles=(0,))).violations)
                self.assertIn("sparse_bit", rules.check_sparse_known_facts(
                    replace(facts, sparse=False)).violations)
                self.assertIn("sparse_shape", rules.check_sparse_known_facts(
                    replace(facts, k=k * 2)).violations)
                self.assertIn("half_path_alignment",
                              rules.check_sparse_known_facts(replace(
                                  facts, metadata_lane_half=16)).violations)
                shared = replace(facts, a_shared=True, a_lane_half=None,
                                 d_lane_half=0, metadata_lane_half=16)
                self.assertIn("half_path_alignment",
                              rules.check_sparse_known_facts(shared).violations)
                if kind in ("F8F6F4", "I8"):
                    self.assertIn("sparsity_selector",
                                  rules.check_sparse_known_facts(replace(
                                      facts, sparse_selector=1)).violations)
                    self.assertIn("scale_d", rules.check_sparse_known_facts(
                        replace(facts, scale_d=1)).violations)
                    if kind == "F8F6F4":
                        self.assertIn("b_transpose_n",
                                      rules.check_sparse_known_facts(replace(
                                          facts, n=8, transpose_b=True,
                                          b_shared_facts=rules.SharedOperandFacts(
                                              major="MN", swizzle="b64")))
                                      .violations)
                else:
                    self.assertNotIn("scale_d", rules.check_sparse_known_facts(
                        replace(facts, scale_d=15)).violations)

    def test_full_path_lane_zero_and_low_bit_obligations(self) -> None:
        """Full paths require lane zero; low-bit contents remain unproved."""

        facts = rules.SparseKnownFacts(
            kind="F16", group=1, m=128, n=32, k=32,
            d_type="F32", a_type="F16", b_type="F16", sparse=True,
            a_shared=False, sparse_selector=0, metadata_nibbles=(14,),
            a_lane_half=0, d_lane_half=0, metadata_lane_half=0)
        self.assertTrue(rules.check_sparse_known_facts(facts).known_facts_ok)
        invalid = rules.check_sparse_known_facts(replace(
            facts, a_lane_half=16, d_lane_half=16,
            metadata_lane_half=16))
        for name in ("a_lane_half_alignment", "d_lane_half_alignment",
                     "metadata_lane_half_alignment"):
            self.assertIn(name, invalid.violations)
        missing = rules.check_sparse_known_facts(replace(
            facts, a_lane_half=None, d_lane_half=None,
            metadata_lane_half=None))
        for name in ("a_lane_half", "d_lane_half", "metadata_lane_half"):
            self.assertIn(name, missing.obligations)
        low = rules.check_sparse_known_facts(replace(
            facts, kind="F8F6F4", k=64, a_type="E2M1",
            b_type="E2M3"))
        self.assertIn("a_low_bit_packing_rule", low.obligations)
        self.assertIn("b_low_bit_packing_rule", low.obligations)


if __name__ == "__main__":
    unittest.main()
