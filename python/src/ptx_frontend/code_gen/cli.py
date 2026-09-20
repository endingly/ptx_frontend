"""Public command-line entry point for PTX frontend code generation."""

from __future__ import annotations

import argparse
from pathlib import Path

from ptx_frontend.base.utils import format_file_inplace
from ptx_frontend.code_gen.context import build_generation_context
from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.code_gen.plan import build_generation_plan
from ptx_frontend.spec.database import load_codegen_database


def parse_arguments() -> argparse.Namespace:
    """Parse the generator's filesystem-facing command-line options."""

    parser = argparse.ArgumentParser(
        description="Generate PTX frontend C++ artifacts from PTX ISA YAML specifications."
    )
    parser.add_argument("--spec-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--backend-spec", required=True, type=Path)
    parser.add_argument("--list-outputs", action="store_true")
    return parser.parse_args()


def main() -> None:
    """Load input files once, then execute or list the same artifact plan."""

    args = parse_arguments()
    spec_dir, output_dir, backend_spec = (
        args.spec_dir.resolve(), args.output.resolve(), args.backend_spec.resolve()
    )
    validate_directory(spec_dir, "--spec-dir")
    validate_file(backend_spec, "--backend-spec")
    context = build_generation_context(
        load_codegen_database(spec_dir=spec_dir), load_cpp_backend(backend_spec)
    )
    plan = build_generation_plan(context, output_dir)
    if args.list_outputs:
        for path in plan.paths:
            print(path)
        return
    output_dir.mkdir(parents=True, exist_ok=True)
    remove_legacy_generated_files(output_dir)
    for artifact in plan.artifacts:
        artifact.emit(context, output_path=artifact.path)
    for generated_file in plan.paths:
        format_file_inplace(str(generated_file))


def validate_directory(path: Path, option: str) -> None:
    """Reject a missing or non-directory input option."""

    if not path.exists():
        raise FileNotFoundError(f"{option} does not exist: {path}")
    if not path.is_dir():
        raise NotADirectoryError(f"{option} is not a directory: {path}")


def validate_file(path: Path, option: str) -> None:
    """Reject a missing or non-file input option."""

    if not path.exists():
        raise FileNotFoundError(f"{option} does not exist: {path}")
    if not path.is_file():
        raise IsADirectoryError(f"{option} is not a file: {path}")


def remove_legacy_generated_files(output_dir: Path) -> None:
    """Remove only obsolete output names before a non-listing generation run."""

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
