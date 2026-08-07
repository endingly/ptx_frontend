#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path
import sys

# Make python/ importable when this script is directly executed from the
# repository root.
PYTHON_ROOT = Path(__file__).resolve().parents[1]

if str(PYTHON_ROOT) not in sys.path:
    sys.path.insert(0, str(PYTHON_ROOT))


from code_gen.database import load_codegen_database
from code_gen.gen_ir import generate_ir_header
from code_gen.gen_ir_registry import (
    category_ir_header_name,
    generate_ir_registry_header,
)
from code_gen.gen_parser_category import (
    generate_parser_category,
)
from code_gen.gen_parser_registry import (
    generate_parser_registry,
)
from code_gen.gen_parser_utils import generate_parser_util
from code_gen.gen_resolved_ir import generate_resolved_ir_header
from code_gen.gen_syntax_ast_arch import generate_syntax_descriptor_header
from code_gen.naming import (
    category_parser_header_name,
    category_parser_source_name,
)
from base.utils import format_file_inplace


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Generate all PTX frontend C++ artifacts from PTX spec and "
            "C++ backend YAML directories."
        )
    )

    parser.add_argument(
        "--spec-dir",
        required=True,
        type=Path,
        help="Directory containing PTX instruction spec YAML files.",
    )

    parser.add_argument(
        "--backend-dir",
        required=True,
        type=Path,
        help="Directory containing C++ backend mapping YAML files.",
    )

    parser.add_argument(
        "--template-dir",
        required=True,
        type=Path,
        help="Directory containing Jinja2 templates.",
    )

    parser.add_argument(
        "--output",
        required=True,
        type=Path,
        help="Directory receiving generated C++ files.",
    )

    parser.add_argument(
        "--list-outputs",
        action="store_true",
        help="Print generated output paths without writing files.",
    )

    return parser.parse_args()


def main() -> None:
    args = parse_arguments()

    spec_dir: Path = args.spec_dir.resolve()
    backend_dir: Path = args.backend_dir.resolve()
    template_dir: Path = args.template_dir.resolve()
    output_dir: Path = args.output.resolve()

    validate_directory(spec_dir, "--spec-dir")
    validate_directory(backend_dir, "--backend-dir")
    validate_directory(template_dir, "--template-dir")

    database = load_codegen_database(
        spec_dir=spec_dir,
        backend_dir=backend_dir,
    )

    if args.list_outputs:
        for path in expected_generated_files(database, output_dir):
            print(path)
        return

    output_dir.mkdir(parents=True, exist_ok=True)

    generated_files: list[Path] = []

    # -------------------------------------------------------------------------
    # Category-local generated IR
    # -------------------------------------------------------------------------

    for loaded in database.units:
        # ir struct file in public dir
        output_path = (
            output_dir / "public" / category_ir_header_name(loaded.unit.category)
        )

        generate_ir_header(
            loaded.unit,
            template_dir=template_dir,
            output_path=output_path,
        )

        generated_files.append(output_path)

    # -------------------------------------------------------------------------
    # Global IR registry
    # -------------------------------------------------------------------------

    ir_registry_path = output_dir / "public/ptx_ir_registry.gen.hpp"

    generate_ir_registry_header(
        database,
        template_dir=template_dir,
        output_path=ir_registry_path,
    )

    generated_files.append(ir_registry_path)

    # -------------------------------------------------------------------------
    # Public generated Resolved IR instruction declarations
    # -------------------------------------------------------------------------

    resolved_ir_path = output_dir / "public/resolved_ir.gen.hpp"

    generate_resolved_ir_header(
        database,
        output_path=resolved_ir_path,
    )

    generated_files.append(resolved_ir_path)

    # -------------------------------------------------------------------------
    # Global parser domain/string utility
    # -------------------------------------------------------------------------

    parser_util_path = output_dir / "private/ptx_parser_util.gen.hpp"

    generate_parser_util(
        database,
        template_dir=template_dir,
        output_path=parser_util_path,
    )

    generated_files.append(parser_util_path)

    # -------------------------------------------------------------------------
    # Resolved IR syntax descriptor implementations
    # -------------------------------------------------------------------------

    syntax_descriptor_path = output_dir / "private/syntax_descriptor.gen.hpp"

    generate_syntax_descriptor_header(
        database,
        output_path=syntax_descriptor_path,
    )

    generated_files.append(syntax_descriptor_path)

    # Generate category parser files.
    for loaded in database.units:
        parser_header, parser_source = generate_parser_category(
            database,
            loaded,
            template_dir=template_dir,
            output_dir=output_dir / "private",
        )

        generated_files.extend(
            (
                parser_header,
                parser_source,
            )
        )

    # Generate global parser registry.
    registry_header, registry_source = generate_parser_registry(
        database,
        template_dir=template_dir,
        output_dir=output_dir / "private",
    )

    generated_files.extend(
        (
            registry_header,
            registry_source,
        )
    )

    format_generated_files(generated_files)


def validate_directory(path: Path, argument_name: str) -> None:
    if not path.exists():
        raise FileNotFoundError(f"{argument_name} does not exist: {path}")

    if not path.is_dir():
        raise NotADirectoryError(f"{argument_name} is not a directory: {path}")


def expected_generated_files(database, output_dir: Path) -> tuple[Path, ...]:
    files: list[Path] = []

    for loaded in database.units:
        category = loaded.unit.category
        files.extend(
            (
                output_dir / "public" / category_ir_header_name(category),
                output_dir / "private" / category_parser_header_name(category),
                output_dir / "private" / category_parser_source_name(category),
            )
        )

    files.extend(
        (
            output_dir / "public/ptx_ir_registry.gen.hpp",
            output_dir / "public/resolved_ir.gen.hpp",
            output_dir / "private/ptx_parser_util.gen.hpp",
            output_dir / "private/syntax_descriptor.gen.hpp",
            output_dir / "private/ptx_parser_registry.gen.hpp",
            output_dir / "private/ptx_parser_registry.gen.cpp",
        )
    )

    return tuple(files)


def format_generated_files(generated_files: list[Path]) -> None:
    for file in generated_files:
        format_file_inplace(str(file))


if __name__ == "__main__":
    main()
