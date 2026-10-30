"""Keep the PTX 9.3 WMMA topology and static contract complete."""

from copy import deepcopy
from dataclasses import replace
from collections import Counter

from ptx_frontend.ir.resolved_ir import from_instruction_spec
from ptx_frontend.code_gen.render_models.availability import build_availability
from ptx_frontend.code_gen.rendering import render_template
import unittest

from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.model import MatrixFamily, ModifierPresence, OperandKind
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.resources import packaged_spec_dir


SPEC_DIR = packaged_spec_dir()
WMMA_FILE = SPEC_DIR / "warp_level_matrix_multiply_accumulate.yaml"


def _change_first_layout_count(item: dict) -> None:
    """Break a YAML alias before making only one stride layout inconsistent."""

    operand = item["operand_layouts"][0]["operands"][0]
    item["operand_layouts"][0]["operands"][0] = {
        **operand, "cardinality": {"min": 1, "max": 1},
    }


class WmmaCoverageTests(unittest.TestCase):
    """Guard the documented WMMA cohorts, fragments, strides, and exclusions."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the same normalized database consumed by code generation."""

        database = get_packaged_spec_database()
        instruction = next(item for item in database.instructions if item.opcode == "wmma")
        cls.variants = {variant.name: variant for variant in instruction.variants}

    def test_complete_family_inventory_and_shapes(self) -> None:
        """Require load, store, and compute cohorts to have distinct topology counts."""

        by_family = {
            family: [variant for variant in self.variants.values()
                     if variant.matrix is not None and variant.matrix.family is family]
            for family in (MatrixFamily.WMMA_LOAD, MatrixFamily.WMMA_STORE,
                           MatrixFamily.WMMA_MMA)
        }
        self.assertEqual(
            {family: len(items) for family, items in by_family.items()},
            {MatrixFamily.WMMA_LOAD: 424,
             MatrixFamily.WMMA_STORE: 140,
             MatrixFamily.WMMA_MMA: 144},
        )
        self.assertEqual(sum(map(len, by_family.values())), len(self.variants))
        self.assertEqual(
            {tuple((item.matrix.shape.m, item.matrix.shape.n, item.matrix.shape.k))
             for item in by_family[MatrixFamily.WMMA_MMA]},
            {(16, 16, 16), (8, 32, 16), (32, 8, 16),
             (16, 16, 8), (8, 8, 4), (8, 8, 32), (8, 8, 128)},
        )

    def test_load_and_store_stride_address_contract(self) -> None:
        """Check both stride layouts, fragment alignment, and qualifier floor."""

        f16 = self.variants["wmma_load_a_m16n16k16_row_generic_f16"]
        self.assertEqual(tuple(layout.name for layout in f16.operand_layouts),
                         ("implicit_stride", "explicit_stride"))
        self.assertEqual(tuple(len(layout.operands) for layout in f16.operand_layouts),
                         (2, 3))
        self.assertIs(f16.operand_layouts[1].operands[-1].kind,
                      OperandKind.REGISTER_OR_IMMEDIATE)
        self.assertEqual(f16.address_alignments[0].alignment, 32)
        self.assertEqual(f16.immediate_ranges[0].minimum, 16)
        self.assertEqual(f16.immediate_multiple_of.divisor, 16)
        self.assertEqual(f16.availability, {"ptx": "6.3", "sm": 70})

        f64 = self.variants["wmma_load_c_m8n8k4_col_shared_cta_f64"]
        self.assertEqual(f64.matrix.fragments[0].register_count, 2)
        self.assertEqual(f64.address_alignments[0].alignment, 16)
        self.assertEqual(f64.immediate_ranges[0].minimum, 8)
        self.assertEqual(f64.immediate_multiple_of.divisor, 2)
        self.assertEqual(f64.availability, {"ptx": "7.8", "sm": 80})
        self.assertEqual(f64.matrix.c_layout.value, "col")

        stored = self.variants["wmma_store_d_m8n8k4_row_global_f64"]
        self.assertEqual(stored.matrix.fragments[0].register_count, 2)
        self.assertEqual(stored.matrix.d_layout.value, "row")
        self.assertEqual(stored.address_alignments[0].alignment, 16)

    def test_mma_input_pairing_rounding_and_removed_saturation(self) -> None:
        """Keep integer pairing and FP64 fragments distinct from old FP syntax."""

        fp64 = self.variants["wmma_mma_m8n8k4_row_col_f64_f64_f64_f64"]
        self.assertEqual(tuple(fragment.register_count
                               for fragment in fp64.matrix.fragments), (2, 1, 1, 2))
        modifiers = {modifier.name: modifier for modifier in fp64.modifiers}
        self.assertIs(modifiers["rounding"].presence, ModifierPresence.OPTIONAL)
        self.assertEqual(modifiers["rounding"].default, "rn")
        self.assertEqual(tuple(item.value for item in modifiers["rounding"].values),
                         ("rn", "rz", "rm", "rp"))
        self.assertNotIn("satfinite", modifiers)

        integer = self.variants["wmma_mma_m8n32k16_row_col_s32_s8_s8_s32"]
        self.assertIs({modifier.name: modifier for modifier in integer.modifiers}
                      ["satfinite"].presence, ModifierPresence.OPTIONAL)
        self.assertFalse(any(name.startswith("wmma_mma_") and "_s8_u8_" in name
                             for name in self.variants))
        bit_and = self.variants["wmma_mma_m8n8k128_row_col_s32_b1_b1_s32_and_popc"]
        self.assertEqual(bit_and.availability, {"ptx": "7.1", "sm": 80})
        self.assertEqual(tuple(modifier.name for modifier in bit_and.modifiers[:4]),
                         ("operation", "bit_operation", "popc", "sync"))
        self.assertTrue(all(
            next(modifier for modifier in variant.modifiers
                 if modifier.name == "aligned").presence is ModifierPresence.FIXED
            for variant in self.variants.values()
            if not variant.name.endswith("_implicit_aligned")
        ))

    def test_exact_historical_pairing_and_generated_omission(self) -> None:
        """All historical forms copy contracts and omit the source aligned field."""
        historical = {name: item for name, item in self.variants.items()
                      if name.endswith("_implicit_aligned")}
        self.assertEqual(len(historical), 156)
        self.assertEqual(Counter((v.matrix.family, v.availability["ptx"])
                                 for v in historical.values()), {
            (MatrixFamily.WMMA_LOAD, "6.0"): 24,
            (MatrixFamily.WMMA_LOAD, "6.1"): 48,
            (MatrixFamily.WMMA_STORE, "6.0"): 12,
            (MatrixFamily.WMMA_STORE, "6.1"): 24,
            (MatrixFamily.WMMA_MMA, "6.0"): 16,
            (MatrixFamily.WMMA_MMA, "6.1"): 32,
        })
        raw = load_yaml(WMMA_FILE)
        instruction = next(i for i in raw["instructions"] if i["opcode"] == "wmma")
        raw_forms = {v["name"]: v for v in instruction["variants"]}
        normalized = next(i for i in get_packaged_spec_database().instructions
                          if i.opcode == "wmma")
        lowered = dict(zip((v.name for v in normalized.variants),
                           from_instruction_spec(normalized).variants))
        for name, item in historical.items():
            with self.subTest(name=name):
                modern = self.variants[name.removesuffix("_implicit_aligned")]
                self.assertEqual(item.availability, {
                    "ptx": "6.0" if item.matrix.shape.m == 16 else "6.1",
                    "sm": 70, "ptx_before": "6.3"})
                self.assertEqual(modern.availability, {"ptx": "6.3", "sm": 70})
                expected = deepcopy(raw_forms[modern.name])
                expected["name"] = name
                expected["availability"] = raw_forms[name]["availability"]
                expected["modifiers"] = [m for m in expected["modifiers"]
                                         if m["name"] != "aligned"]
                for alias in expected.get("modifier_order_aliases", []):
                    alias.remove("aligned")
                self.assertEqual(raw_forms[name], expected)
                self.assertEqual(replace(item, name=modern.name,
                    availability=modern.availability, modifiers=modern.modifiers,
                    modifier_order_aliases=modern.modifier_order_aliases), modern)
                self.assertFalse(any(m.name == "aligned" for m in item.modifiers))
                self.assertFalse(any(f.source_name == "aligned"
                                     for f in lowered[name].modifier_fields))
                gate = build_availability(item.availability)
                self.assertEqual(gate.minimum.ptx_before, (6, 3))
                self.assertIn("ptx_before", render_template(
                    "fragments/availability.j2", gate))
        modern = [v for v in self.variants.values() if v.name not in historical]
        self.assertEqual(len(modern), 552)
        eligible = {v.name + "_implicit_aligned" for v in modern
                    if v.availability == {"ptx": "6.3", "sm": 70}
                    and "_shared_cta_" not in v.name
                    and set(raw_forms[v.name]["matrix"]["elements"].values()) <= {"f16", "f32"}}
        self.assertEqual(set(historical), eligible)

    def test_f16_saturation_exact_contract(self) -> None:
        """Saturation changes only the selected flag value on all FP16 MMA forms."""
        raw = load_yaml(WMMA_FILE)
        forms = next(i["variants"] for i in raw["instructions"]
                     if i["opcode"] == "wmma")
        eligible = [v for v in forms if v.get("matrix", {}).get("family") == "wmma_mma"
                    and v["matrix"]["elements"]["a"] == "f16"]
        self.assertEqual(len(eligible), 96)
        self.assertEqual(sum(v["name"].endswith("_implicit_aligned")
                             for v in eligible), 48)
        for raw_form in eligible:
            with self.subTest(name=raw_form["name"]):
                expected = {"name": "satfinite", "kind": "flag",
                            "presence": "optional", "default": False,
                            "token": ".satfinite", "values": [
                                {"value": False},
                                {"value": True, "availability": {"ptx_before": "6.5"}}]}
                self.assertEqual(raw_form["modifiers"][-1], expected)
                for alias in raw_form["modifier_order_aliases"]:
                    self.assertEqual(alias[-1], "satfinite")
                    self.assertEqual(len(alias), len(raw_form["modifiers"]))
                form = self.variants[raw_form["name"]]
                flag = form.modifiers[-1]
                self.assertEqual(flag.name, "satfinite")
                self.assertIs(flag.presence, ModifierPresence.OPTIONAL)
                self.assertIs(flag.default, False)
                self.assertEqual(flag.token, ".satfinite")
                self.assertEqual(tuple(v.value for v in flag.values), (False, True))
                self.assertFalse(flag.values[0].availability)
                self.assertEqual(flag.values[1].availability, {"ptx_before": "6.5"})
                self.assertNotIn("ptx_before", form.availability
                                 if not form.name.endswith("_implicit_aligned") else {})
        self.assertEqual(len(forms), 708)

    def test_normalizer_rejects_wmma_metadata_drift(self) -> None:
        """A canonical variant cannot lie about its C type or register count."""

        source = load_yaml(WMMA_FILE)
        instruction = next(item for item in source["instructions"]
                           if item["opcode"] == "wmma")
        raw = next(item for item in instruction["variants"]
                   if item["name"] == "wmma_load_c_m8n8k4_col_shared_cta_f64")
        for mutate in (
            lambda item: item["matrix"]["elements"].update(c="f32"),
            _change_first_layout_count,
        ):
            changed = deepcopy(raw)
            mutate(changed)
            spec = {"category": "test", "codegen_category": "matrix",
                    "instructions": [{"opcode": "wmma", "variants": [changed]}]}
            with self.subTest(change=changed["name"]), self.assertRaises(ValueError):
                normalize_instruction_spec(spec)


if __name__ == "__main__":
    unittest.main()
