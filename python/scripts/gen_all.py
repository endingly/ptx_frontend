#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path
import sys

# Make python/ importable both from the source tree and from the installed
# codegen component, where this script is installed beside base/code_gen/ir.
PYTHON_ROOT = Path(__file__).resolve().parents[1]

if str(PYTHON_ROOT) not in sys.path:
    sys.path.insert(0, str(PYTHON_ROOT))


from code_gen.database import CodegenDatabase, load_codegen_database
from code_gen.cpp_backend import configure_cpp_backend, get_cpp_backend
from code_gen.gen_resolved_descriptor import generate_resolved_descriptor_source
from code_gen.gen_resolved_checker_descriptor import (
    generate_resolved_checker_descriptor_source,
)
from code_gen.gen_resolved_ir import (
    generate_resolved_dispatch_source,
    generate_resolved_ir_header,
    generate_resolved_ir_source,
)
from code_gen.gen_resolved_value_domains import (
    generate_resolved_value_domain_header,
)
from code_gen.gen_syntax_ast_arch import generate_syntax_descriptor_source
from base.utils import format_file_inplace


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Generate all PTX frontend C++ artifacts from PTX ISA YAML "
            "specifications."
        )
    )

    parser.add_argument(
        "--spec-dir",
        required=True,
        type=Path,
        help="Directory containing PTX instruction spec YAML files.",
    )

    parser.add_argument(
        "--output",
        required=True,
        type=Path,
        help="Directory receiving generated C++ files.",
    )

    parser.add_argument(
        "--backend-spec",
        required=True,
        type=Path,
        help="Consumer-selected C++ backend mapping YAML.",
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
    output_dir: Path = args.output.resolve()
    backend_spec: Path = args.backend_spec.resolve()

    validate_directory(spec_dir, "--spec-dir")
    validate_file(backend_spec, "--backend-spec")
    configure_cpp_backend(backend_spec)
    backend = get_cpp_backend()

    database = load_codegen_database(spec_dir=spec_dir)
    if backend.spec_schema != database.spec_schema:
        raise ValueError(
            "C++ backend expects spec schema "
            f"{backend.spec_schema!r}, got {database.spec_schema!r}"
        )

    if args.list_outputs:
        for path in expected_generated_files(database, output_dir):
            print(path)
        return

    output_dir.mkdir(parents=True, exist_ok=True)
    remove_legacy_generated_files(output_dir)

    generated_files: list[Path] = []

    resolved_value_domains_path = (
        output_dir / "private/resolved_value_domains.gen.hpp"
    )
    generate_resolved_value_domain_header(
        backend,
        output_path=resolved_value_domains_path,
    )
    generated_files.append(resolved_value_domains_path)

    resolved_ir_path = output_dir / "public/resolved_ir.gen.hpp"
    generate_resolved_ir_header(database, output_path=resolved_ir_path)
    generated_files.append(resolved_ir_path)

    resolved_dispatch_path = output_dir / "private/resolved_ir_dispatch.gen.cpp"
    generate_resolved_dispatch_source(
        database,
        output_path=resolved_dispatch_path,
    )
    generated_files.append(resolved_dispatch_path)

    for category in instruction_categories(database):
        category_source_path = resolved_ir_category_source_path(
            output_dir, category
        )
        generate_resolved_ir_source(
            database,
            category=category,
            output_path=category_source_path,
        )
        generated_files.append(category_source_path)

    syntax_descriptor_path = output_dir / "private/syntax_descriptor.gen.cpp"
    generate_syntax_descriptor_source(
        database,
        output_path=syntax_descriptor_path,
    )
    generated_files.append(syntax_descriptor_path)

    resolved_descriptor_path = output_dir / "private/resolved_descriptor.gen.cpp"
    generate_resolved_descriptor_source(
        database,
        output_path=resolved_descriptor_path,
    )
    generated_files.append(resolved_descriptor_path)

    checker_descriptor_path = (
        output_dir / "private/resolved_ir_checker_descriptor.gen.cpp"
    )
    generate_resolved_checker_descriptor_source(
        database,
        output_path=checker_descriptor_path,
    )
    generated_files.append(checker_descriptor_path)

    format_generated_files(generated_files)


def validate_directory(path: Path, argument_name: str) -> None:
    if not path.exists():
        raise FileNotFoundError(f"{argument_name} does not exist: {path}")
    if not path.is_dir():
        raise NotADirectoryError(f"{argument_name} is not a directory: {path}")


def validate_file(path: Path, argument_name: str) -> None:
    if not path.exists():
        raise FileNotFoundError(f"{argument_name} does not exist: {path}")
    if not path.is_file():
        raise IsADirectoryError(f"{argument_name} is not a file: {path}")


def expected_generated_files(database, output_dir: Path) -> tuple[Path, ...]:
    return (
        output_dir / "public/resolved_ir.gen.hpp",
        output_dir / "private/resolved_value_domains.gen.hpp",
        output_dir / "private/resolved_ir_dispatch.gen.cpp",
        *(
            resolved_ir_category_source_path(output_dir, category)
            for category in instruction_categories(database)
        ),
        output_dir / "private/syntax_descriptor.gen.cpp",
        output_dir / "private/resolved_descriptor.gen.cpp",
        output_dir / "private/resolved_ir_checker_descriptor.gen.cpp",
    )


def instruction_categories(database: CodegenDatabase) -> tuple[str, ...]:
    """Return stable category names used to partition generated definitions."""

    return tuple(
        sorted(
            {
                instruction.codegen_category
                for instruction in database.instructions
            }
        )
    )


def resolved_ir_category_source_path(output_dir: Path, category: str) -> Path:
    return output_dir / f"private/resolved_ir_{category}.gen.cpp"


def remove_legacy_generated_files(output_dir: Path) -> None:
    """Remove artifacts from the retired direct-IR/parser generator path."""

    legacy_patterns = (
        "public/ptx_ir/resolved/resolved_ir.gen.hpp",
        "public/ptx_ir_*.gen.hpp",
        "private/ptx_parser_*.gen.hpp",
        "private/ptx_parser_*.gen.cpp",
        "private/syntax_descriptor.gen.hpp",
    )
    for pattern in legacy_patterns:
        for path in output_dir.glob(pattern):
            path.unlink()

    checker_descriptor_name = "resolved_ir_checker_descriptor.gen.cpp"
    for path in (output_dir / "private").glob("resolved_ir_*.gen.cpp"):
        if path.name != checker_descriptor_name:
            path.unlink()


def format_generated_files(generated_files: list[Path]) -> None:
    for file in generated_files:
        format_file_inplace(str(file))


if __name__ == "__main__":
    main()
