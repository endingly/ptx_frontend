"""Protect current dense and sparse warp MMA topology and metadata bounds."""

from collections import Counter
from copy import deepcopy
import unittest

from ptx_frontend.spec.database import load_codegen_database
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.model import MatrixBitOperation, MatrixFamily, MatrixKind
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.resources import packaged_spec_dir


SPEC_PATH = packaged_spec_dir() / "warp_level_matrix_multiply_accumulate.yaml"


class WarpMatrixMmaCoverageTests(unittest.TestCase):
    """Keep exact fragment, ordering, selector, and target cohorts visible."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the same normalized inventory used by the C++ generator."""

        database = load_codegen_database(spec_dir=packaged_spec_dir())
        mma = next(item for item in database.instructions if item.opcode == "mma")
        cls.variants = {item.name: item for item in mma.variants}

    def test_dense_and_sparse_inventory(self) -> None:
        """Require all current topology cohorts and exclude sparse b1/f64."""

        families = Counter(item.matrix.family for item in self.variants.values())
        self.assertEqual(families, {MatrixFamily.MMA: 175,
                                    MatrixFamily.MMA_SPARSE: 185})
        dense = [item.matrix for item in self.variants.values()
                 if item.matrix.family is MatrixFamily.MMA]
        sparse = [item.matrix for item in self.variants.values()
                  if item.matrix.family is MatrixFamily.MMA_SPARSE]
        self.assertTrue(any(matrix.shape.k == 16 and matrix.elements[0][1].value == "f64"
                            and matrix.fragments[1].register_count == 8
                            for matrix in dense))
        self.assertTrue(any(matrix.kind is MatrixKind.F8F6F4 for matrix in sparse))
        self.assertFalse(any(matrix.elements[1][1].value in {"b1", "f64"}
                             for matrix in sparse))
        self.assertEqual({matrix.sparse_order.value for matrix in sparse},
                         {"native", "ordered"})

    def test_block_scale_and_bit_operation_controls(self) -> None:
        """Expose exact selector masks and bit operations in public descriptors."""

        scaled = [item.matrix for item in self.variants.values()
                  if item.matrix.scale_vector_size]
        self.assertTrue(scaled)
        for matrix in scaled:
            self.assertEqual({selector.role.value for selector in matrix.scale_selectors},
                             {"a", "b"})
            expected_mask = {1: 0b1111, 2: 0b0101, 4: 0b0001}[
                matrix.scale_vector_size]
            self.assertEqual({selector.byte_mask for selector in matrix.scale_selectors},
                             {expected_mask})
            self.assertEqual({selector.thread_max for selector in matrix.scale_selectors},
                             {1, 3})
        bits = [item.matrix for item in self.variants.values()
                if item.matrix.bit_operation is not MatrixBitOperation.NONE]
        self.assertEqual({item.bit_operation for item in bits},
                         {MatrixBitOperation.XOR, MatrixBitOperation.AND})
        self.assertTrue(all(item.elements[1][1].value == "b1" for item in bits))

    def test_sparse_selector_and_modern_availability(self) -> None:
        """Check metadata selector ranges and exact modern target clauses."""

        fp8 = self.variants[
            "mma_sp_sp_sync_aligned_m16n8k32_row_col_f32_e4m3_e4m3_f32"
        ]
        self.assertEqual(fp8.immediate_ranges[0].maximum, 1)
        ordered = self.variants[
            "mma_sp_ordered_metadata_sync_aligned_m16n8k64_row_col_f32_e4m3_e4m3_f32"
        ]
        self.assertEqual(ordered.immediate_ranges[0].maximum, 0)
        modern = next(item for item in self.variants.values()
                      if item.matrix.kind is MatrixKind.MXF8F6F4)
        self.assertEqual(len(modern.availability["any_of"]), 2)
        self.assertEqual(modern.matrix.scale_vector_size, 1)

    def test_rejects_topology_and_selector_drift(self) -> None:
        """Canonical metadata cannot misstate suffixes or selector operand type."""

        source = load_yaml(SPEC_PATH)
        mma = next(item for item in source["instructions"] if item["opcode"] == "mma")
        sample = next(item for item in mma["variants"]
                      if item["name"].startswith("mma_sync_aligned_m16n8k32_row_col_kind_mxf8f6f4")
                      and "scale_vec_1" in item["name"])
        for mutate in (
            lambda item: item["matrix"].update(scale_vector_size=4),
            lambda item: item["matrix"]["scale_selectors"].update(
                scale_b_selector="a"),
            lambda item: next(op for op in item["operands"]
                              if op["name"] == "scale_a_selector").update(type="u32"),
        ):
            changed = deepcopy(sample)
            mutate(changed)
            specification = {"category": "test", "codegen_category": "matrix",
                             "instructions": [{"opcode": "mma", "variants": [changed]}]}
            with self.assertRaises((TypeError, ValueError)):
                normalize_instruction_spec(specification)


if __name__ == "__main__":
    unittest.main()
