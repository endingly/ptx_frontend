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

    output_dir.mkdir(parents=True, exist_ok=True)

    database = load_codegen_database(
        spec_dir=spec_dir,
        backend_dir=backend_dir,
    )

    generated_files: list[Path] = []

    # -------------------------------------------------------------------------
    # Category-local generated IR
    # -------------------------------------------------------------------------

    for loaded in database.units:
        output_path = output_dir / category_ir_header_name(loaded.unit.category)

        generate_ir_header(
            loaded.unit,
            template_dir=template_dir,
            output_path=output_path,
        )

        generated_files.append(output_path)

    # -------------------------------------------------------------------------
    # Global IR registry
    # -------------------------------------------------------------------------

    ir_registry_path = output_dir / "ptx_ir_registry.gen.hpp"

    generate_ir_registry_header(
        database,
        template_dir=template_dir,
        output_path=ir_registry_path,
    )

    generated_files.append(ir_registry_path)

    # -------------------------------------------------------------------------
    # Global parser domain/string utility
    # -------------------------------------------------------------------------

    parser_util_path = output_dir / "ptx_parser_util.gen.hpp"

    generate_parser_util(
        database,
        template_dir=template_dir,
        output_path=parser_util_path,
    )

    generated_files.append(parser_util_path)

    # Generate category parser files.
    for loaded in database.units:
        parser_header, parser_source = generate_parser_category(
            database,
            loaded,
            template_dir=template_dir,
            output_dir=output_dir,
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
        output_dir=output_dir,
    )

    generated_files.extend(
        (
            registry_header,
            registry_source,
        )
    )

    format_generated_files(output_dir)


def validate_directory(path: Path, argument_name: str) -> None:
    if not path.exists():
        raise FileNotFoundError(f"{argument_name} does not exist: {path}")

    if not path.is_dir():
        raise NotADirectoryError(f"{argument_name} is not a directory: {path}")


def format_generated_files(input_dir: Path):
    generated_files = list(input_dir.glob("*.gen.hpp")) + list(
        input_dir.glob("*.gen.cpp")
    )
    for file in generated_files:
        format_file_inplace(file.__str__())


if __name__ == "__main__":
    main()
