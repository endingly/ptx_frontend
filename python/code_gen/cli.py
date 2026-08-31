"""Public command-line entry point for PTX frontend code generation."""

from __future__ import annotations

import argparse
from pathlib import Path

from base.utils import format_file_inplace
from code_gen.cpp_backend import configure_cpp_backend, get_cpp_backend
from code_gen.database import CodegenDatabase, load_codegen_database
from code_gen.gen_resolved_checker_descriptor import (
    generate_resolved_checker_descriptor_source,
)
from code_gen.gen_resolved_descriptor import generate_resolved_descriptor_source
from code_gen.gen_resolved_ir import (
    generate_resolved_dispatch_source,
    generate_resolved_ir_header,
    generate_resolved_ir_source,
)
from code_gen.gen_resolved_value_domains import generate_resolved_value_domain_header
from code_gen.gen_syntax_ast_arch import generate_syntax_descriptor_source


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate PTX frontend C++ artifacts from PTX ISA YAML specifications."
    )
    parser.add_argument("--spec-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--backend-spec", required=True, type=Path)
    parser.add_argument("--list-outputs", action="store_true")
    return parser.parse_args()


def main() -> None:
    args = parse_arguments()
    spec_dir, output_dir, backend_spec = (
        args.spec_dir.resolve(), args.output.resolve(), args.backend_spec.resolve()
    )
    validate_directory(spec_dir, "--spec-dir")
    validate_file(backend_spec, "--backend-spec")
    configure_cpp_backend(backend_spec)
    backend = get_cpp_backend()
    database = load_codegen_database(spec_dir=spec_dir)
    if backend.spec_schema != database.spec_schema:
        raise ValueError(
            f"C++ backend expects spec schema {backend.spec_schema!r}, got {database.spec_schema!r}"
        )
    if args.list_outputs:
        for path in expected_generated_files(database, output_dir):
            print(path)
        return
    output_dir.mkdir(parents=True, exist_ok=True)
    remove_legacy_generated_files(output_dir)
    generated_files = [
        output_dir / "private/resolved_value_domains.gen.hpp",
        output_dir / "public/resolved_ir.gen.hpp",
        output_dir / "private/resolved_ir_dispatch.gen.cpp",
    ]
    generate_resolved_value_domain_header(backend, output_path=generated_files[0])
    generate_resolved_ir_header(database, output_path=generated_files[1])
    generate_resolved_dispatch_source(database, output_path=generated_files[2])
    for category in instruction_categories(database):
        output_path = resolved_ir_category_source_path(output_dir, category)
        generate_resolved_ir_source(database, category=category, output_path=output_path)
        generated_files.append(output_path)
    generated_files.extend([
        output_dir / "private/syntax_descriptor.gen.cpp",
        output_dir / "private/resolved_descriptor.gen.cpp",
        output_dir / "private/resolved_ir_checker_descriptor.gen.cpp",
    ])
    generate_syntax_descriptor_source(database, output_path=generated_files[-3])
    generate_resolved_descriptor_source(database, output_path=generated_files[-2])
    generate_resolved_checker_descriptor_source(database, output_path=generated_files[-1])
    for generated_file in generated_files:
        format_file_inplace(str(generated_file))


def validate_directory(path: Path, option: str) -> None:
    if not path.exists():
        raise FileNotFoundError(f"{option} does not exist: {path}")
    if not path.is_dir():
        raise NotADirectoryError(f"{option} is not a directory: {path}")


def validate_file(path: Path, option: str) -> None:
    if not path.exists():
        raise FileNotFoundError(f"{option} does not exist: {path}")
    if not path.is_file():
        raise IsADirectoryError(f"{option} is not a file: {path}")


def instruction_categories(database: CodegenDatabase) -> tuple[str, ...]:
    return tuple(sorted({instruction.codegen_category for instruction in database.instructions}))


def resolved_ir_category_source_path(output_dir: Path, category: str) -> Path:
    return output_dir / f"private/resolved_ir_{category}.gen.cpp"


def expected_generated_files(database: CodegenDatabase, output_dir: Path) -> tuple[Path, ...]:
    return (
        output_dir / "private/resolved_value_domains.gen.hpp",
        output_dir / "public/resolved_ir.gen.hpp",
        output_dir / "private/resolved_ir_dispatch.gen.cpp",
        *(resolved_ir_category_source_path(output_dir, category) for category in instruction_categories(database)),
        output_dir / "private/syntax_descriptor.gen.cpp",
        output_dir / "private/resolved_descriptor.gen.cpp",
        output_dir / "private/resolved_ir_checker_descriptor.gen.cpp",
    )


def remove_legacy_generated_files(output_dir: Path) -> None:
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
