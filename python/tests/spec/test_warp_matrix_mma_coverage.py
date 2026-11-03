"""Protect current dense and sparse warp MMA topology and metadata bounds."""

from collections import Counter
from copy import deepcopy
import unittest

from ptx_frontend.ir.resolved_ir import from_instruction_spec
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.model import (MatrixBitOperation, MatrixFamily, MatrixKind,
                                     OperandKind, OperandRegisterWidthPolicy,
                                     OperandImmediateConversionPolicy)
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.resources import packaged_spec_dir


SPEC_PATH = packaged_spec_dir() / "warp_level_matrix_multiply_accumulate.yaml"


class WarpMatrixMmaCoverageTests(unittest.TestCase):
    """Keep exact fragment, ordering, selector, and target cohorts visible."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the same normalized inventory used by the C++ generator."""

        database = get_packaged_spec_database()
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

    def test_sparse_mxf4_target_and_scale_version_floors(self) -> None:
        """Keep exact target introductions and the later scale combination distinct."""

        forms = [item for item in self.variants.values()
                 if item.matrix.family is MatrixFamily.MMA_SPARSE
                 and item.matrix.kind in {MatrixKind.MXF4, MatrixKind.MXF4NVF4}]
        self.assertEqual(len(forms), 5)
        late = [item for item in forms
                if item.matrix.kind is MatrixKind.MXF4NVF4
                and item.matrix.scale_vector_size == 4
                and item.matrix.scale_type.value == "ue8m0"]
        self.assertEqual(len(late), 1)
        for form in forms:
            with self.subTest(form=form.name):
                minimum = "9.1" if form is late[0] else "8.7"
                self.assertEqual(form.availability["any_of"], [
                    {"ptx": minimum, "target": "sm_120a"},
                    {"ptx": "9.1" if form is late[0] else "8.8",
                     "target": "sm_121a"},
                ])

    def test_all_scale_selector_contracts_use_same_width_u16(self) -> None:
        """Close all A/B contracts without changing tuple, range or topology facts."""
        rows = []
        cohorts = Counter()
        variants = set()
        for variant in self.variants.values():
            for layout in variant.operand_layouts:
                for operand in layout.operands:
                    if operand.kind is not OperandKind.MATRIX_SCALE_SELECTOR:
                        continue
                    rows.append((variant.name, layout.name, operand.name))
                    variants.add(variant.name)
                    matrix = variant.matrix
                    self.assertIsNotNone(matrix)
                    cohorts[(matrix.family, matrix.kind, matrix.scale_vector_size)] += 1
                    self.assertEqual(operand.type_expression.scalar_type, "u16")
                    self.assertIs(operand.register_width_policy, OperandRegisterWidthPolicy.SAME_WIDTH)
                    self.assertEqual((operand.minimum_elements, operand.maximum_elements), (2, 2))
                    self.assertEqual(tuple(kind.value for kind in operand.element_kinds), ("reg", "imm"))
                    self.assertIs(operand.immediate_conversion_policy,
                                  OperandImmediateConversionPolicy.REQUIRE_TARGET_RANGE)
                    self.assertIn(operand.name, ("scale_a_selector", "scale_b_selector"))
        self.assertEqual(len(rows), 220)
        self.assertEqual(len(set(rows)), 220)
        self.assertEqual(len(variants), 110)
        self.assertEqual(len(cohorts), 8)
        for family in (MatrixFamily.MMA, MatrixFamily.MMA_SPARSE):
            self.assertEqual(cohorts[(family, MatrixKind.MXF8F6F4, 1)], 100)
            self.assertEqual(cohorts[(family, MatrixKind.MXF4, 2)], 4)
            self.assertEqual(cohorts[(family, MatrixKind.MXF4NVF4, 2)], 2)
            self.assertEqual(cohorts[(family, MatrixKind.MXF4NVF4, 4)], 4)

    def test_f64_canonical_shape_and_distinct_adjacent_gates(self) -> None:
        """Bind the single canonical shape to exact fragments, modifiers and IR."""
        name = "mma_sync_aligned_m8n8k4_row_col_f64_f64_f64_f64"
        form = self.variants[name]
        self.assertIs(form.matrix.family, MatrixFamily.MMA)
        self.assertEqual((form.matrix.shape.m, form.matrix.shape.n, form.matrix.shape.k),
                         (8, 8, 4))
        self.assertEqual((form.matrix.a_layout.value, form.matrix.b_layout.value),
                         ("row", "col"))
        self.assertEqual(tuple((f.role.value, f.register_count, f.register_type,
                                f.element_type.value) for f in form.matrix.fragments),
                         (("d", 2, "f64", "f64"), ("a", 1, "f64", "f64"),
                          ("b", 1, "f64", "f64"), ("c", 2, "f64", "f64")))
        self.assertEqual(form.availability, {"ptx": "7.0", "sm": 80})
        modifiers = {m.name: m for m in form.modifiers}
        self.assertEqual(modifiers["shape"].token, ".m8n8k4")
        self.assertEqual(modifiers["shape"].presence.value, "fixed")
        self.assertEqual(modifiers["rounding"].default, "rn")
        self.assertEqual(tuple(v.value for v in modifiers["rounding"].values),
                         ("rn", "rz", "rm", "rp"))
        for role in ("a", "b", "c", "d"):
            self.assertEqual(modifiers[f"{role}_type"].value, "f64")
        mma = next(i for i in get_packaged_spec_database().instructions
                   if i.opcode == "mma")
        lowered = next(v for v in from_instruction_spec(mma).variants
                       if v.variant_id == name)
        self.assertEqual(lowered.matrix, form.matrix)
        self.assertEqual(lowered.cpp_name, "SyncAlignedM8n8k4RowColF64F64F64F64")
        for k in (4, 8, 16):
            adjacent = self.variants[f"mma_sync_aligned_m16n8k{k}_row_col_f64_f64_f64_f64"]
            self.assertEqual(adjacent.availability, {"ptx": "7.8", "sm": 90})
        raw = next(i for i in load_yaml(SPEC_PATH)["instructions"] if i["opcode"] == "mma")
        self.assertEqual([v["name"] for v in raw["variants"]
                          if v["name"].startswith("mma_sync_aligned_m8n8k4_")
                          and "_f64_" in v["name"]], [name])
        self.assertFalse(any(m.get("token") == ".m8n84"
                             for v in raw["variants"] for m in v["modifiers"]))

    def test_f64_fixed_shape_and_suffix_drift_reject(self) -> None:
        """The typo cannot replace the canonical token or its shape/type facts."""
        raw = next(i for i in load_yaml(SPEC_PATH)["instructions"] if i["opcode"] == "mma")
        form = next(v for v in raw["variants"] if v["name"] ==
                    "mma_sync_aligned_m8n8k4_row_col_f64_f64_f64_f64")
        for change in ("spelling", "shape", "type", "nonexact_count"):
            changed = deepcopy(form)
            if change == "spelling":
                next(m for m in changed["modifiers"] if m["name"] == "shape")["token"] = ".m8n84"
            elif change == "shape":
                changed["matrix"]["k"] = 5
            elif change == "type":
                next(m for m in changed["modifiers"] if m["name"] == "a_type")["value"] = "f32"
            else:
                changed["operands"][0]["cardinality"] = {"min": 1, "max": 2}
            specification = {"category": "test", "codegen_category": "matrix",
                             "instructions": [{"opcode": "mma", "variants": [changed]}]}
            with self.subTest(change=change), self.assertRaises((TypeError, ValueError)):
                normalize_instruction_spec(specification)

    def test_sparse_kind_exact_fifty_tuple_projection(self) -> None:
        """Bind all low-bit ordered raw identities to exact normalized and IR roles."""
        types = ("e4m3", "e5m2", "e3m2", "e2m3", "e2m1")
        expected = {f"mma_sp_ordered_metadata_sync_aligned_m16n8k64_row_col_kind_f8f6f4_{dc}_{a}_{b}_{dc}"
                    for a in types for b in types for dc in ("f16", "f32")}
        forms = {name: v for name, v in self.variants.items()
                 if v.matrix.family is MatrixFamily.MMA_SPARSE
                 and v.matrix.kind is MatrixKind.F8F6F4}
        self.assertEqual(set(forms), expected)
        self.assertEqual(len(forms), 50)
        mma = next(i for i in get_packaged_spec_database().instructions if i.opcode == "mma")
        lowered = {v.variant_id: v for v in from_instruction_spec(mma).variants}
        raw = next(i for i in load_yaml(SPEC_PATH)["instructions"] if i["opcode"] == "mma")
        raw_forms = {v["name"]: v for v in raw["variants"]}
        for name, form in forms.items():
            with self.subTest(name=name):
                matrix = form.matrix
                self.assertEqual((matrix.shape.m, matrix.shape.n, matrix.shape.k), (16, 8, 64))
                self.assertEqual(matrix.sparse_order.value, "ordered")
                self.assertEqual((matrix.a_layout.value, matrix.b_layout.value), ("row", "col"))
                elements = {role.value: element for role, element in matrix.elements}
                self.assertEqual(elements["d"].value, elements["c"].value)
                dc = elements["d"].value
                count, carrier = (2, "f16x2") if dc == "f16" else (4, "f32")
                self.assertEqual(tuple((f.role.value, f.register_count, f.register_type)
                                      for f in matrix.fragments),
                                 (("d", count, carrier), ("a", 4, "b32"),
                                  ("b", 4, "b32"), ("c", count, carrier)))
                self.assertEqual(tuple(f.element_type for f in matrix.fragments),
                                 tuple(elements[r] for r in ("d", "a", "b", "c")))
                modifiers = {m.name: m for m in form.modifiers}
                self.assertEqual(modifiers["kind"].token, ".kind::f8f6f4")
                self.assertEqual(modifiers["kind"].presence.value, "fixed")
                self.assertEqual(modifiers["sparse"].token, ".sp::ordered_metadata")
                operands = {o.name: o for o in form.operand_layouts[0].operands}
                self.assertIs(operands["metadata"].kind, OperandKind.REGISTER)
                self.assertEqual(operands["metadata"].type_expression.scalar_type, "b32")
                self.assertEqual((form.immediate_ranges[0].operand,
                                  form.immediate_ranges[0].minimum,
                                  form.immediate_ranges[0].maximum), ("selector", 0, 0))
                self.assertEqual(form.availability, {"any_of": [
                    {"ptx": "8.7", "target": "sm_120a"},
                    {"ptx": "8.8", "sm": 120, "family": "sm_120f"}]})
                self.assertEqual(lowered[name].matrix, matrix)
                self.assertEqual(raw_forms[name]["matrix"]["kind"], "f8f6f4")
                self.assertEqual(raw_forms[name]["matrix"]["sparse_order"], "ordered")

    def test_sparse_kind_fixed_token_and_order_drift_reject(self) -> None:
        """Fixed kind/order tokens must agree with the declared matrix metadata."""
        raw = next(i for i in load_yaml(SPEC_PATH)["instructions"] if i["opcode"] == "mma")
        form = next(v for v in raw["variants"] if v["name"] ==
                    "mma_sp_ordered_metadata_sync_aligned_m16n8k64_row_col_kind_f8f6f4_f32_e3m2_e2m3_f32")
        for change in ("missing_dot", "unknown_kind", "order"):
            changed = deepcopy(form)
            if change == "order":
                changed["matrix"]["sparse_order"] = "native"
            else:
                modifier = next(m for m in changed["modifiers"] if m["name"] == "kind")
                modifier["token"] = ("kind::f8f6f4" if change == "missing_dot"
                                     else ".kind::unknown")
            specification = {"category": "test", "codegen_category": "matrix",
                             "instructions": [{"opcode": "mma", "variants": [changed]}]}
            with self.subTest(change=change), self.assertRaises((TypeError, ValueError)):
                normalize_instruction_spec(specification)

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
