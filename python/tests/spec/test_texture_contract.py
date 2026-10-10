"""Texture form contracts and feature gates from the canonical ISA input."""

import unittest
from copy import deepcopy
from pathlib import Path

from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.database import load_codegen_database_from_files
from ptx_frontend.spec.normalize.instruction import _normalize_texture_contract
from ptx_frontend.spec.normalize.instruction import normalize_instruction_spec
from ptx_frontend.ir.resolved_ir import from_instruction_spec
from ptx_frontend.code_gen.emit.resolved_model import _texture_descriptor
from ptx_frontend.spec.model import (
    OpaqueResourceKind, TextureComponent, TextureMipmapMode,
    TextureQuery, TextureResourceRole,
)


SPEC = Path(__file__).resolve().parents[2] / "src/ptx_frontend/spec/resources/ptx_spec/texture.yaml"


class TextureContractTests(unittest.TestCase):
    """Reject contradictory typed forms and preserve normative matrix floors."""

    def test_rejects_invalid_access_combinations(self) -> None:
        """Geometry and optional controls cannot escape their closed domains."""
        invalid = (
            ("tld4", {"geometry": "3d", "component": "r", "result_arity": 4}),
            ("tex", {"geometry": "cube", "result_arity": 4,
                     "allows_offset": True}),
            ("tex", {"geometry": "2dms", "mipmap": "level",
                     "result_arity": 4}),
            ("tex", {"geometry": "2dms", "result_arity": 4,
                     "allows_compare": True}),
        )
        for opcode, contract in invalid:
            with self.subTest(opcode=opcode, contract=contract):
                with self.assertRaises(ValueError):
                    _normalize_texture_contract(contract, opcode)

    def test_rejects_nonaccess_controls(self) -> None:
        """Queries and opaque-kind tests have no access-only features."""
        with self.assertRaises(ValueError):
            _normalize_texture_contract(
                {"query": "width", "allows_residency": True}, "txq")
        with self.assertRaises(ValueError):
            _normalize_texture_contract(
                {"tested_kind": "texture", "allows_offset": True}, "istypep")

    def test_base_and_compare_matrix(self) -> None:
        """Explicit base is gated while f32 compare covers non-3d non-MS forms."""
        spec = load_yaml(SPEC)
        tex = next(item for item in spec["instructions"]
                   if item["opcode"] == "tex")
        base = [row for row in tex["variants"]
                if row["texture"]["mipmap"] == "base"]
        self.assertTrue(base)
        for row in base:
            self.assertGreaterEqual(float(row["availability"]["ptx"]), 3.1)
            self.assertGreaterEqual(row["availability"]["sm"], 20)
        compare_geometry = {
            row["texture"]["geometry"] for row in tex["variants"]
            if row["texture"]["allows_compare"]
        }
        self.assertEqual(compare_geometry,
                         {"1d", "2d", "a1d", "a2d", "cube", "acube"})
        self.assertTrue(all(
            operand.get("texture_unbracketed") and
            operand.get("texture_legacy_v4_coordinates") and
            operand.get("type") == {"expr": "modifier(ctype)"}
            for row in tex["variants"]
            for layout in row["operand_layouts"]
            for operand in layout["operands"]
            if operand["kind"] == "texture_access"
        ))

    def test_rendered_access_descriptor(self) -> None:
        """Selected type and layout gates survive normalization into C++."""
        forms = normalize_instruction_spec(load_yaml(SPEC))
        tex = from_instruction_spec(next(form for form in forms
                                          if form.opcode == "tex"))
        rendered = _texture_descriptor(tex.variants[0])
        self.assertIn(".result_type = dtype.value", rendered)
        self.assertIn(".coordinate_type = ctype.value", rendered)
        self.assertIn(".minimum_ptx_version = {3, 1}", rendered)
        self.assertIn(".minimum_sm_version = 20", rendered)
        self.assertIn("texture_layout_requires_residency", rendered)
        self.assertIn("case 0: return false", rendered)
        self.assertIn("return true", rendered)

    def test_all_families_enter_codegen_partition(self) -> None:
        """The source category maps all four families into a stable emitter shard."""
        database = load_codegen_database_from_files(
            spec_files=(SPEC,), category="data_movement")
        self.assertEqual({entry.opcode for entry in database.instructions},
                         {"tex", "tld4", "txq", "istypep"})
        for entry in database.instructions:
            with self.subTest(opcode=entry.opcode):
                rendered = _texture_descriptor(
                    from_instruction_spec(entry).variants[0])
                self.assertIn("TextureInstructionDescriptor texture_contract",
                              rendered)

    def test_access_requires_selected_ctype_binding(self) -> None:
        """A missing or misrouted coordinate type fails before C++ emission."""
        source = load_yaml(SPEC)
        for expression in (None, {"expr": "modifier(dtype)"}):
            with self.subTest(expression=expression):
                specimen = deepcopy(source)
                specimen["instructions"] = [specimen["instructions"][0]]
                specimen["instructions"][0]["variants"] = [
                    specimen["instructions"][0]["variants"][0]]
                access = specimen["instructions"][0]["variants"][0]["operand_layouts"][0]["operands"][1]
                if expression is None:
                    access.pop("type", None)
                else:
                    access["type"] = expression
                with self.assertRaisesRegex(ValueError, "coordinate type from ctype"):
                    normalize_instruction_spec(specimen)

    def test_normalized_texture_domains_are_closed_enums(self) -> None:
        """YAML strings stop at normalization; emitters consume typed roles."""
        forms = normalize_instruction_spec(load_yaml(SPEC))
        by_opcode = {form.opcode: form for form in forms}
        tex = by_opcode["tex"].variants[0].texture
        gather = by_opcode["tld4"].variants[0].texture
        query = by_opcode["txq"].variants[0].texture
        test = by_opcode["istypep"].variants[0].texture
        self.assertIsInstance(tex.mipmap, TextureMipmapMode)
        self.assertIsInstance(gather.component, TextureComponent)
        self.assertIsInstance(query.query, TextureQuery)
        self.assertIsInstance(test.tested_kind, OpaqueResourceKind)
        self.assertIsInstance(
            by_opcode["txq"].variants[0].operand_layouts[0].operands[1].texture_resource_kind,
            TextureResourceRole,
        )


if __name__ == "__main__":
    unittest.main()
