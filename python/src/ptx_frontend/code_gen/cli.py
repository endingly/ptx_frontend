"""Public command-line entry point for PTX frontend code generation."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import stat
import tempfile

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
    remove_obsolete_generated_files(output_dir, plan.paths)
    for artifact in plan.artifacts:
        write_formatted_artifact(context, artifact.emit, artifact.path)
    write_output_manifest(output_dir, plan.paths)


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


def write_formatted_artifact(context, emit, output_path: Path) -> None:
    """Format a sibling candidate and replace ``output_path`` only if changed."""

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_mode = (
        stat.S_IMODE(output_path.stat().st_mode)
        if output_path.exists()
        else 0o644
    )
    descriptor, candidate_name = tempfile.mkstemp(
        prefix=f".{output_path.stem}.", suffix=output_path.suffix,
        dir=output_path.parent,
    )
    os.close(descriptor)
    candidate = Path(candidate_name)
    try:
        emit(context, output_path=candidate)
        format_file_inplace(str(candidate))
        candidate_bytes = candidate.read_bytes()
        if not output_path.exists() or output_path.read_bytes() != candidate_bytes:
            candidate.chmod(output_mode)
            os.replace(candidate, output_path)
        else:
            candidate.unlink()
    finally:
        if candidate.exists():
            candidate.unlink()


def remove_obsolete_generated_files(output_dir: Path, active_paths: tuple[Path, ...]) -> None:
    """Remove only prior generator outputs absent from the active artifact plan."""

    active = {path.relative_to(output_dir).as_posix() for path in active_paths}
    obsolete = read_output_manifest(output_dir) - active
    obsolete.update({
        "private/syntax_descriptor.gen.cpp",
        "private/resolved_descriptor.gen.cpp",
        "private/resolved_ir_checker_descriptor.gen.cpp",
        "public/ptx_ir/resolved/resolved_ir.gen.hpp",
        "private/syntax_descriptor.gen.hpp",
    } - active)
    for pattern in (
        "public/ptx_ir/resolved/resolved_ir.gen.hpp",
        "public/ptx_ir_*.gen.hpp",
        "private/ptx_parser_*.gen.hpp",
        "private/ptx_parser_*.gen.cpp",
        "private/resolved_ir_*.gen.cpp",
    ):
        obsolete.update(
            path.relative_to(output_dir).as_posix()
            for path in output_dir.glob(pattern)
            if path.relative_to(output_dir).as_posix() not in active
        )
    for relative_path in obsolete:
        path = output_dir / relative_path
        if path.is_file():
            path.unlink()


def read_output_manifest(output_dir: Path) -> set[str]:
    """Read the previous plan-owned output set without treating other files as owned."""

    manifest = output_dir / ".ptx_resolved_ir_outputs.txt"
    if not manifest.is_file():
        return set()
    paths = set(manifest.read_text(encoding="utf-8").splitlines())
    if any(not is_output_relative_path(path) for path in paths):
        raise ValueError("generated-output manifest contains a path outside its output root")
    return paths


def write_output_manifest(output_dir: Path, active_paths: tuple[Path, ...]) -> None:
    """Record the output paths owned by the successfully completed plan."""

    manifest = output_dir / ".ptx_resolved_ir_outputs.txt"
    paths = sorted(path.relative_to(output_dir).as_posix() for path in active_paths)
    content = "\n".join(paths) + "\n"
    if not manifest.is_file() or manifest.read_text(encoding="utf-8") != content:
        manifest.write_text(content, encoding="utf-8")


def is_output_relative_path(path: str) -> bool:
    """Return whether a manifest entry cannot escape its generated output root."""

    candidate = Path(path)
    return bool(path) and not candidate.is_absolute() and ".." not in candidate.parts
