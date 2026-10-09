"""Prospective source and known-value checks for dense unscaled f8f6f4 MMA.

Caller-known fields are independent of live instruction and matrix registers.
Unresolved low-bit layout and packing rules remain explicit obligations.
"""

from dataclasses import replace
from itertools import product
import unittest

from ptx_frontend.code_gen.emit import tcgen_mma_operations as emitter
from ptx_frontend.spec import tcgen_descriptor_domains as descriptor
from ptx_frontend.spec import tcgen_mma_operations as rules
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.model import SemanticRule
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.normalize.tcgen_mma import (
    normalize_f8f6f4_known_facts,
    normalize_f8f6f4_source_topology,
)
from ptx_frontend.spec.resources import packaged_spec_dir


_TYPES = ("E4M3", "E5M2", "E2M3", "E3M2", "E2M1")
_LOW = frozenset(("E2M3", "E3M2", "E2M1"))


def _known(**changes):
    """Supply a complete ordinary group-one claim with separate shared facts."""

    base = rules.F8F6F4KnownFacts(
        group=1, m=64, n=16, k=32, d_type="F32", a_type="E4M3",
        b_type="E5M2", sparse=False, a_shared=True,
        transpose_a=False, transpose_b=False,
        a_shared_facts=rules.SharedOperandFacts("K", "B32"),
        b_shared_facts=rules.SharedOperandFacts("K", "B32"))
    return replace(base, **changes)


class TcgenMmaF8F6F4ContractTests(unittest.TestCase):
    """Exercise only the bounded canonical source and known-value contract."""

    def test_canonical_source_four_layouts_two_groups_no_scale(self) -> None:
        """Keep an unscaled typed source form without an adjacent alias."""

        raw = load_yaml(packaged_spec_dir() /
                        "tensor_memory_data_movement.yaml")
        tcgen = normalize_instruction_spec(raw)[0]
        mma = next(item for item in tcgen.variants
                   if item.name == "tcgen05_mma_f8f6f4")
        self.assertIs(mma.rule, SemanticRule.TENSOR_MEMORY_MMA)
        self.assertEqual(mma.modifier_order_aliases, ())
        self.assertEqual({item.name for item in mma.modifiers},
                         {"mma", "cta_group", "kind", "ashift", "collector"})
        self.assertEqual({item.token for item in mma.modifiers
                          if item.name == "kind"}, {".kind::f8f6f4"})
        self.assertEqual({layout.name for layout in mma.operand_layouts}, {
            "shared_mask", "shared_no_mask", "tensor_mask", "tensor_no_mask"
        })
        for layout in mma.operand_layouts:
            names = tuple(item.name for item in layout.operands)
            self.assertEqual(names[:4], ("d", "a", "b", "idesc"))
            self.assertEqual(names[-1], "enable_input_d")
            self.assertNotIn("scale_input_d", names)
            self.assertNotIn("saturate", names)
            self.assertNotIn("scale_a", names)
            self.assertNotIn("scale_b", names)
        for group, count in ((1, 4), (2, 8)):
            for placement, mask in product(("shared", "tensor"), (None, count)):
                with self.subTest(group=group, placement=placement, mask=mask):
                    source = normalize_f8f6f4_source_topology({
                        "group": group, "a_placement": placement,
                        "mask_count": mask})
                    self.assertEqual((source.group, source.a_placement,
                                      source.mask_count),
                                     (group, placement, mask))
        base = {"group": 1, "a_placement": "shared", "mask_count": None}
        for changed in ({"group": 3}, {"a_placement": "global"},
                        {"mask_count": 0}, {"mask_count": 5},
                        {"scale_source_value": 0}, {"saturate": True},
                        {"block_scale": True}):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                normalize_f8f6f4_source_topology({**base, **changed})

    def test_table_42_rows_include_group_one_n8(self) -> None:
        """The group-one step-eight grid includes every multiple of eight."""

        rules.validate_catalogue()
        self.assertEqual(len(rules.F8F6F4_SHAPES), 4)
        self.assertEqual({(row.group, row.d_type) for row in
                          rules.F8F6F4_SHAPES},
                         {(1, "F16"), (1, "F32"),
                          (2, "F16"), (2, "F32")})
        for dtype in ("F16", "F32"):
            group_one = [row for row in rules.F8F6F4_SHAPES
                         if row.group == 1 and row.d_type == dtype]
            self.assertEqual({(row.n_first, row.n_step, row.n_last)
                              for row in group_one},
                             {(8, 8, 256)})
            for row in group_one:
                self.assertEqual((row.m_values, row.k), ((64, 128), 32))
                self.assertEqual(set(row.a_types), set(_TYPES))
                self.assertEqual(set(row.b_types), set(_TYPES))
            group_two = next(row for row in rules.F8F6F4_SHAPES
                             if row.group == 2 and row.d_type == dtype)
            self.assertEqual((group_two.m_values, group_two.n_first,
                              group_two.n_step, group_two.n_last,
                              group_two.k), ((128, 256), 16, 16, 256, 32))
        for n in (8, 16, 24, 32, 256):
            report = rules.check_f8f6f4_known_facts(_known(n=n))
            self.assertNotIn("shape_or_output_type", report.violations)
        for n in (0, 7, 257):
            self.assertIn("shape_or_output_type",
                          rules.check_f8f6f4_known_facts(
                              _known(n=n)).violations)
        self.assertIn("shape_or_output_type",
                      rules.check_f8f6f4_known_facts(
                          _known(k=16)).violations)
        self.assertEqual({(row.group, row.m, row.layout, row.half_path)
                          for row in rules.F8F6F4_PATHS}, {
                              (1, 64, "F", True), (1, 128, "D", False),
                              (2, 128, "B", False), (2, 256, "A", False)})

    def test_all_25_defined_pairs_and_low_bit_packing_obligations(self) -> None:
        """Mixed low-bit pairs are allowed while physical packing remains open."""

        kind = next(item for item in descriptor.KINDS
                    if item.name == "F8F6F4")
        self.assertFalse(kind.saturation)
        self.assertTrue(kind.negate)
        self.assertEqual({name for _, name in kind.d_types}, {"F16", "F32"})
        for dtype, a_type, b_type in product(("F16", "F32"), _TYPES,
                                             _TYPES):
            with self.subTest(dtype=dtype, a_type=a_type, b_type=b_type):
                report = rules.check_f8f6f4_known_facts(
                    _known(d_type=dtype, a_type=a_type, b_type=b_type))
                self.assertTrue(report.known_facts_ok)
                self.assertNotIn("mixed_input_pair", report.obligations)
                self.assertEqual("a_low_bit_packing_rule" in
                                 report.obligations, a_type in _LOW)
                self.assertEqual("b_low_bit_packing_rule" in
                                 report.obligations, b_type in _LOW)
        tensor_a = rules.check_f8f6f4_known_facts(_known(
            a_type="E2M1", a_shared=False, a_shared_facts=None,
            a_lane_half=0, d_lane_half=16))
        self.assertIn("a_low_bit_packing_rule", tensor_a.obligations)
        self.assertNotIn("a_shared_word", tensor_a.obligations)
        self.assertIn("half_path_alignment", tensor_a.violations)
        self.assertEqual(normalize_f8f6f4_known_facts({"group": 1,
            "b_shared_facts": {"major": "K"}}).b_shared_facts.major, "K")

    def test_role_width_transpose_and_scoped_b_n(self) -> None:
        """Low-bit transpose is missing by role; atom32 stays forbidden."""

        normal = rules.SharedOperandFacts("MN", "B64")
        for role in ("a", "b"):
            for low in _LOW:
                with self.subTest(role=role, low=low):
                    changed = {f"{role}_type": low,
                               f"transpose_{role}": True,
                               f"{role}_shared_facts": normal}
                    report = rules.check_f8f6f4_known_facts(
                        _known(**changed))
                    self.assertIn(f"{role}_transpose_layout_rule",
                                  report.obligations)
                    self.assertNotIn(f"{role}_transpose_swizzle",
                                     report.violations)
                    changed[f"{role}_shared_facts"] = (
                        rules.SharedOperandFacts("MN", "B128Atom32"))
                    atom32 = rules.check_f8f6f4_known_facts(
                        _known(**changed))
                    self.assertIn(f"{role}_transpose_swizzle",
                                  atom32.violations)
        for n, valid in ((8, False), (16, True), (24, False),
                         (32, True)):
            eight_b = rules.check_f8f6f4_known_facts(_known(
                a_type="E2M1", b_type="E4M3", n=n, transpose_b=True,
                b_shared_facts=normal))
            self.assertEqual("b_transpose_n" not in eight_b.violations,
                             valid)
            four_b = rules.check_f8f6f4_known_facts(_known(
                a_type="E4M3", b_type="E2M1", n=n, transpose_b=True,
                b_shared_facts=normal))
            self.assertNotIn("b_transpose_n", four_b.violations)
            self.assertIn("b_transpose_layout_rule", four_b.obligations)

    def test_four_branch_targets_and_single_generated_catalogue(self) -> None:
        """Source and known facts use ordinary non-i8 unscaled targets."""

        self.assertEqual({(row.feature, row.exact, row.ptx_major,
                           row.ptx_minor, row.scaled_d)
                          for row in rules.F8F6F4_TARGET_GATES}, {
                              ("sm_100a", True, 8, 6, False),
                              ("sm_100f", False, 8, 8, False),
                              ("sm_110a", True, 9, 0, False),
                              ("sm_110f", False, 9, 0, False)})
        header = emitter.render_tcgen_mma_header()
        source = emitter.render_tcgen_mma_source()
        self.assertIn("TcgenF8F6F4ShapeRow", header)
        self.assertIn("check_tcgen_f8f6f4_known_operation", source)
        self.assertIn("validate_tcgen_instruction_defined_fields", source)
        self.assertIn("validate_tcgen_shared_defined_fields", source)
        self.assertIn("TcgenMmaKind::F8F6F4", source)


if __name__ == "__main__":
    unittest.main()
