from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest

from jsonschema import Draft202012Validator
import yaml


REPO_ROOT = Path(__file__).resolve().parents[3]
PYTHON_ROOT = REPO_ROOT / "python"

if str(PYTHON_ROOT) not in sys.path:
    sys.path.insert(0, str(PYTHON_ROOT))


from code_gen.database import load_codegen_database
from code_gen.cpp_backend import configure_cpp_backend
from code_gen.gen_resolved_descriptor import generate_resolved_descriptor_source
from code_gen.gen_resolved_checker_descriptor import (
    generate_resolved_checker_descriptor_source,
)
from code_gen.gen_resolved_ir import (
    generate_resolved_ir_header,
    generate_resolved_ir_source,
)
from code_gen.gen_syntax_ast_arch import generate_syntax_descriptor_source
from code_gen.load_yaml import load_yaml
from code_gen.normalize import normalize_instruction_spec, normalize_operand
from ir.resolved_ir import ResolvedOperandShape, from_instruction_spec
from ir.syntax_ast import (
    OPERAND_SYNTAX_SHAPES,
    OperandSyntaxShape,
    from_InstructionSpec,
)


def setUpModule() -> None:
    configure_cpp_backend(REPO_ROOT / "instructions/ptx_cpp_backend_spec/ptx_frontend.yaml")


def _operand(kind: str, name: str, **extra: object) -> dict[str, object]:
    return {"name": name, "kind": kind, "role": "src", "access": "read", **extra}


def _modern_instruction() -> dict[str, object]:
    return {
        "schema": "ptx-instr/v1",
        "ptx_isa": "9.3",
        "category": "test",
        "codegen_category": "test",
        "instructions": [{
            "opcode": "modern",
            "variants": [{
                "name": "modern_primitives",
                "availability": {"ptx": "9.3", "sm": 0},
                "operands": [
                    _operand("descriptor", "desc", type_tag="tensor_descriptor"),
                    _operand("typed_token", "token", type_tag="collector_token"),
                    _operand(
                        "tensor_coordinate", "coordinate",
                        cardinality={"min": 1, "max": 5},
                        element_kinds=["reg", "imm"],
                    ),
                    _operand(
                        "matrix_fragment", "fragment",
                        cardinality={"min": 1, "max": 64},
                        element_kinds=["reg"],
                    ),
                ],
            }],
        }],
    }


class ModernOperandPrimitiveTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.schema = load_yaml(REPO_ROOT / "instructions/schemas/ptx-instr-v1.schema.yaml")
        cls.operand_validator = Draft202012Validator({
            "$schema": cls.schema["$schema"],
            "$defs": cls.schema["$defs"],
            "$ref": "#/$defs/operand",
        })

    def test_schema_rejects_invalid_primitive_metadata(self) -> None:
        valid = _modern_instruction()["instructions"][0]["variants"][0]["operands"]
        self.assertTrue(all(not list(self.operand_validator.iter_errors(item)) for item in valid))
        reversed_coordinate = _operand(
            "tensor_coordinate", "coordinate",
            cardinality={"min": 1, "max": 5}, element_kinds=["imm", "reg"],
        )
        self.assertFalse(list(self.operand_validator.iter_errors(reversed_coordinate)))
        for operand in (
            _operand("reg", "value", type_tag="ordinary_register"),
            _operand("descriptor", "desc"),
            _operand(
                "descriptor", "desc", type_tag="tensor_descriptor",
                cardinality={"min": 1, "max": 5},
            ),
            _operand("vector", "values", element_kinds=["reg"]),
            _operand("typed_token", "token", type_tag="Not_snake"),
            _operand("typed_token", "token", type_tag="tag_"),
            _operand("typed_token", "token", type_tag="tag__two"),
            _operand(
                "tensor_coordinate", "coordinate",
                cardinality={"min": 0, "max": 5}, element_kinds=["reg", "imm"],
            ),
            _operand(
                "tensor_coordinate", "coordinate",
                cardinality={"min": 1, "max": 6}, element_kinds=["reg", "imm"],
            ),
            _operand(
                "matrix_fragment", "fragment",
                cardinality={"min": 1, "max": 65}, element_kinds=["reg"],
            ),
            _operand(
                "matrix_fragment", "fragment",
                cardinality={"min": 1, "max": 64}, element_kinds=["imm"],
            ),
            _operand(
                "tensor_coordinate", "coordinate",
                cardinality={"min": 1, "max": 5}, element_kinds=["reg"],
            ),
            _operand(
                "tensor_coordinate", "coordinate",
                cardinality={"min": 1, "max": 5}, element_kinds=["imm"],
            ),
            _operand(
                "tensor_coordinate", "coordinate",
                cardinality={"min": 1, "max": 5},
                element_kinds=["reg", "imm"], vector={"arity": 2},
            ),
        ):
            self.assertTrue(list(self.operand_validator.iter_errors(operand)))

    def test_normalizer_rejects_relational_and_element_kind_errors(self) -> None:
        for operand in (
            _operand("descriptor", "desc", type_tag="tag_"),
            _operand("typed_token", "token", type_tag="tag__two"),
            _operand(
                "tensor_coordinate", "coordinate",
                cardinality={"min": 5, "max": 1}, element_kinds=["reg", "imm"],
            ),
            _operand(
                "tensor_coordinate", "coordinate",
                cardinality={"min": 1, "max": 5}, element_kinds=["reg"],
            ),
            _operand(
                "matrix_fragment", "fragment",
                cardinality={"min": 1, "max": 64}, element_kinds=["reg", "imm"],
            ),
        ):
            with self.assertRaises(ValueError):
                normalize_operand(operand)

    def test_normalizer_rejects_incomparable_modern_pack_layouts(self) -> None:
        spec = _modern_instruction()
        variant = spec["instructions"][0]["variants"][0]
        variant.pop("operands")
        variant["operand_layouts"] = [
            {
                "name": "one_to_three",
                "operands": [
                    _operand(
                        "matrix_fragment", "fragment",
                        cardinality={"min": 1, "max": 3}, element_kinds=["reg"],
                    )
                ],
            },
            {
                "name": "two_to_five",
                "operands": [
                    _operand(
                        "matrix_fragment", "fragment",
                        cardinality={"min": 2, "max": 5}, element_kinds=["reg"],
                    )
                ],
            },
        ]
        with self.assertRaisesRegex(ValueError, "overlapping syntax"):
            normalize_instruction_spec(spec)

    def test_normalizer_rejects_type_tag_only_layout_difference(self) -> None:
        spec = _modern_instruction()
        variant = spec["instructions"][0]["variants"][0]
        variant.pop("operands")
        variant["operand_layouts"] = [
            {
                "name": "first_tag",
                "operands": [
                    _operand("descriptor", "descriptor", type_tag="first_tag")
                ],
            },
            {
                "name": "second_tag",
                "operands": [
                    _operand("descriptor", "descriptor", type_tag="second_tag")
                ],
            },
        ]
        with self.assertRaisesRegex(ValueError, "overlapping syntax"):
            normalize_instruction_spec(spec)

    def test_mbarrier_domain_fixture_emits_typed_token_and_defaults(self) -> None:
        database = load_codegen_database(
            spec_dir=REPO_ROOT / "submod/resolved_ir/test/fixtures"
        )
        specification = next(
            instruction
            for instruction in database.instructions
            if instruction.opcode == "synthetic_mbarrier_domain"
        )
        instruction = from_instruction_spec(specification)
        variant = instruction.variants[0]
        self.assertEqual(
            [(field.name, field.cpp_type) for field in variant.fields],
            [
                ("phase_type", "WithLocs<MbarrierPhaseType>"),
                ("layout", "WithLocs<MbarrierLayout>"),
                ("state", "WithLocs<ResolvedMbarrierStateToken>"),
            ],
        )
        self.assertEqual(
            [binding.default_value.value for binding in variant.modifier_bindings],
            ["phase_type::primary", "layout::v0"],
        )
        self.assertEqual(
            variant.operand_layouts[0].bindings[0].allowed_shapes,
            (ResolvedOperandShape.REGISTER,),
        )

        with tempfile.TemporaryDirectory() as directory:
            directory_path = Path(directory)
            header_path = directory_path / "resolved_ir.gen.hpp"
            descriptor_path = directory_path / "resolved_descriptor.gen.cpp"
            checker_path = directory_path / "checker.gen.cpp"
            generate_resolved_ir_header(database, output_path=header_path)
            generate_resolved_descriptor_source(database, output_path=descriptor_path)
            generate_resolved_checker_descriptor_source(
                database, output_path=checker_path
            )
            header = header_path.read_text(encoding="utf-8")
            descriptor = descriptor_path.read_text(encoding="utf-8")
            checker = checker_path.read_text(encoding="utf-8")

        self.assertIn("WithLocs<ResolvedMbarrierStateToken> state;", header)
        self.assertIn("MbarrierPhaseType::Primary", descriptor)
        self.assertIn("MbarrierLayout::V0", descriptor)
        self.assertIn("MbarrierPhaseType::Conditional", checker)
        self.assertIn("MbarrierLayout::V1", checker)

    def test_normalizer_accepts_strictly_contained_modern_pack_layout(self) -> None:
        spec = _modern_instruction()
        variant = spec["instructions"][0]["variants"][0]
        variant.pop("operands")
        variant["operand_layouts"] = [
            {
                "name": "narrow",
                "operands": [
                    _operand(
                        "matrix_fragment", "fragment",
                        cardinality={"min": 1, "max": 2}, element_kinds=["reg"],
                    )
                ],
            },
            {
                "name": "wide",
                "operands": [
                    _operand(
                        "matrix_fragment", "fragment",
                        cardinality={"min": 1, "max": 5}, element_kinds=["reg"],
                    )
                ],
            },
        ]
        self.assertEqual(
            [layout.name for layout in normalize_instruction_spec(spec)[0].variants[0].operand_layouts],
            ["narrow", "wide"],
        )
        self.assertEqual(
            OPERAND_SYNTAX_SHAPES["matrix_fragment"],
            OperandSyntaxShape.VECTOR_PACK,
        )

    def test_full_synthetic_model_and_codegen_round_trip(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            directory_path = Path(directory)
            (directory_path / "modern.yaml").write_text(
                yaml.safe_dump(_modern_instruction(), sort_keys=False),
                encoding="utf-8",
            )
            database = load_codegen_database(spec_dir=directory_path)
            instruction = database.instructions[0]
            operands = instruction.variants[0].operand_layouts[0].operands
            self.assertEqual(
                [(operand.type_tag, operand.minimum_elements,
                  operand.maximum_elements, operand.element_kinds)
                 for operand in operands],
                [
                    ("tensor_descriptor", None, None, ()),
                    ("collector_token", None, None, ()),
                    (None, 1, 5, ("reg", "imm")),
                    (None, 1, 64, ("reg",)),
                ],
            )

            syntax = from_InstructionSpec(instruction)
            slots = syntax.variants[0].operand_layouts[0].slots
            self.assertEqual(
                slots[0].allowed_syntax_shapes, OperandSyntaxShape.IDENTIFIER_REF
            )
            self.assertEqual(
                slots[2].allowed_syntax_shapes, OperandSyntaxShape.VECTOR_PACK
            )
            self.assertEqual(
                slots[2].allowed_element_shapes,
                OperandSyntaxShape.IDENTIFIER_REF | OperandSyntaxShape.IMMEDIATE,
            )

            resolved = from_instruction_spec(instruction)
            bindings = resolved.variants[0].operand_layouts[0].bindings
            self.assertEqual(bindings[0].type_tag, "tensor_descriptor")
            self.assertEqual(bindings[1].type_tag, "collector_token")
            self.assertEqual(
                (bindings[2].minimum_elements, bindings[2].maximum_elements),
                (1, 5),
            )
            self.assertEqual(
                bindings[2].allowed_element_shapes,
                (ResolvedOperandShape.REGISTER, ResolvedOperandShape.IMMEDIATE),
            )
            self.assertEqual(
                (bindings[3].minimum_elements, bindings[3].maximum_elements),
                (1, 64),
            )

            header_path = directory_path / "resolved_ir.gen.hpp"
            descriptor_path = directory_path / "resolved_descriptor.gen.cpp"
            source_path = directory_path / "resolved_ir_test.gen.cpp"
            syntax_path = directory_path / "syntax_descriptor.gen.cpp"
            generate_resolved_ir_header(database, output_path=header_path)
            generate_resolved_descriptor_source(database, output_path=descriptor_path)
            generate_resolved_ir_source(
                database, category="test", output_path=source_path
            )
            generate_syntax_descriptor_source(database, output_path=syntax_path)
            header = header_path.read_text(encoding="utf-8")
            descriptor = descriptor_path.read_text(encoding="utf-8")
            source = source_path.read_text(encoding="utf-8")
            syntax_source = syntax_path.read_text(encoding="utf-8")

        self.assertIn("WithLocs<ResolvedRegisterRef> desc;", header)
        self.assertIn("WithLocs<ResolvedTensorCoordinate> coordinate;", header)
        self.assertIn("WithLocs<ResolvedRegisterVector> fragment;", header)
        self.assertIn('.type_tag = "tensor_descriptor",', descriptor)
        self.assertIn(".minimum_elements = 1,", descriptor)
        self.assertIn(".maximum_elements = 64,", descriptor)
        self.assertIn(".allowed_element_shapes = check_end::OperandShape::Register | "
                      "check_end::OperandShape::Immediate,", descriptor)
        self.assertIn(".vector_arity = static_cast<uint8_t>(", source)
        self.assertIn('.type_tag = "tensor_descriptor",', syntax_source)
        self.assertIn(".minimum_elements = 1,", syntax_source)
        self.assertIn(".maximum_elements = 64,", syntax_source)
        self.assertIn(
            ".allowed_element_shapes = check_end::OperandSyntaxShape::Identifier | "
            "check_end::OperandSyntaxShape::Immediate,",
            syntax_source,
        )
        self.assertIn("view.vector_element_shapes[index] =", source)
        self.assertIn("std::get_if<ResolvedRegisterRef>(&element)", source)
        self.assertIn("register_ref->declared_type.value_or(ScalarType::Invalid)", source)
        self.assertIn("view.vector_element_types[index] = immediate.type;", source)


if __name__ == "__main__":
    unittest.main()
