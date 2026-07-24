from __future__ import annotations

import copy
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import yaml
from jsonschema import Draft202012Validator


REPO_ROOT = Path(__file__).resolve().parents[2]
PYTHON_ROOT = REPO_ROOT / "python"

if str(PYTHON_ROOT) not in sys.path:
    sys.path.insert(0, str(PYTHON_ROOT))


from code_gen.database import load_codegen_database
from code_gen.gen_parser_category import (
    build_parser_category_view,
    operand_kind_cpp,
)
from scripts.gen_all import expected_generated_files


class CodegenGoldenTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.spec_dir = REPO_ROOT / "instructions/ptx_spec"
        cls.backend_dir = REPO_ROOT / "instructions/ptx_cpp_backend_spec"
        cls.template_dir = REPO_ROOT / "python/templates"
        cls.database = load_codegen_database(
            spec_dir=cls.spec_dir,
            backend_dir=cls.backend_dir,
        )

    def test_generated_output_manifest(self) -> None:
        output_dir = Path("/generated")
        relative_paths = [
            path.relative_to(output_dir).as_posix()
            for path in expected_generated_files(self.database, output_dir)
        ]

        self.assertEqual(
            relative_paths,
            [
                "public/ptx_ir_integer_arithmetic.gen.hpp",
                "private/ptx_parser_integer_arithmetic.gen.hpp",
                "private/ptx_parser_integer_arithmetic.gen.cpp",
                "public/ptx_ir_registry.gen.hpp",
                "private/ptx_parser_util.gen.hpp",
                "private/ptx_parser_registry.gen.hpp",
                "private/ptx_parser_registry.gen.cpp",
            ],
        )

    def test_parser_view_golden(self) -> None:
        loaded = self.database.units[0]
        view = build_parser_category_view(database=self.database, loaded=loaded)
        instruction = view.instructions[0]

        snapshot = [
            f"instruction={instruction.opcode}:{instruction.cpp_type}",
            "modifiers=" + ",".join(item.spec_name for item in instruction.modifiers),
            "operands="
            + ",".join(
                f"{item.spec_name}:{item.kind_cpp}" for item in instruction.operands
            ),
        ]
        snapshot.extend(
            f"variant={variant.name}:ptx={variant.min_ptx_major}."
            f"{variant.min_ptx_minor}:sm={variant.min_sm}:family={variant.family}"
            for variant in instruction.variants
        )

        golden_path = REPO_ROOT / "test/golden/parser_view.txt"
        self.assertEqual("\n".join(snapshot) + "\n", golden_path.read_text())

    def test_full_codegen_is_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            for output in (first, second):
                subprocess.run(
                    [
                        sys.executable,
                        str(REPO_ROOT / "python/scripts/gen_all.py"),
                        "--spec-dir",
                        str(self.spec_dir),
                        "--backend-dir",
                        str(self.backend_dir),
                        "--template-dir",
                        str(self.template_dir),
                        "--output",
                        output,
                    ],
                    check=True,
                    cwd=REPO_ROOT,
                )

            first_root = Path(first)
            second_root = Path(second)
            for first_path in expected_generated_files(self.database, first_root):
                relative = first_path.relative_to(first_root)
                self.assertEqual(
                    first_path.read_bytes(),
                    (second_root / relative).read_bytes(),
                    str(relative),
                )

    def test_unsupported_operand_kind_fails_early(self) -> None:
        with self.assertRaisesRegex(ValueError, "does not support operand kind"):
            operand_kind_cpp(
                opcode="example",
                operand_name="optional_dst",
                kind="optional_reg",
            )


class SchemaCapabilityTest(unittest.TestCase):
    def test_current_backend_matches_schema(self) -> None:
        schema = yaml.safe_load(
            (REPO_ROOT / "instructions/schemas/ptx-cpp-backend-v1.schema.yaml")
            .read_text()
        )
        backend = yaml.safe_load(
            (REPO_ROOT / "instructions/ptx_cpp_backend_spec/integer_arith.yaml")
            .read_text()
        )

        self.assertEqual(list(Draft202012Validator(schema).iter_errors(backend)), [])

    def test_custom_emit_is_not_advertised(self) -> None:
        schema = yaml.safe_load(
            (REPO_ROOT / "instructions/schemas/ptx-cpp-backend-v1.schema.yaml")
            .read_text()
        )
        backend = yaml.safe_load(
            (REPO_ROOT / "instructions/ptx_cpp_backend_spec/integer_arith.yaml")
            .read_text()
        )
        unsupported = copy.deepcopy(backend)
        unsupported["instructions"]["add"]["emit"] = {
            "kind": "custom",
            "custom_handler": "parse_add",
        }

        errors = list(Draft202012Validator(schema).iter_errors(unsupported))
        self.assertTrue(errors)


if __name__ == "__main__":
    unittest.main()
