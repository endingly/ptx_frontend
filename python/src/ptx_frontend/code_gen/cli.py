"""Public command-line entry point for PTX frontend code generation."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import os
from pathlib import Path
import stat
import tempfile
import json

from ptx_frontend.base.utils import format_file_inplace
from ptx_frontend.code_gen.context import build_generation_context
from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.code_gen.plan import build_generation_plan
from ptx_frontend.spec.database import (
    CodegenCategoryInputs,
    load_codegen_database,
    load_codegen_database_from_files,
    load_codegen_database_with_category_inputs,
)


def parse_arguments() -> argparse.Namespace:
    """Parse command-line arguments for Resolved IR generation."""

    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--spec-dir",
        type=Path,
        required=True,
        help="Directory containing PTX instruction specifications.",
    )

    parser.add_argument(
        "--backend-spec",
        type=Path,
        required=True,
        help="PTX C++ backend specification.",
    )

    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="Generated output directory.",
    )

    parser.add_argument(
        "--jobs",
        type=positive_integer,
        default=6,
        metavar="N",
        help="Maximum concurrent artifact writers (default: 6).",
    )

    parser.add_argument(
        "--spec-file",
        action="append",
        type=Path,
        default=[],
        help=(
            "Explicit PTX instruction specification input for a "
            "category-local generation job. May be repeated."
        ),
    )

    parser.add_argument(
        "--defer-finalization",
        action="store_true",
        help="Write the full plan without cleanup or manifest publication.",
    )

    mode = parser.add_mutually_exclusive_group()

    mode.add_argument(
        "--category",
        help="Generate only artifacts owned by this codegen category.",
    )

    mode.add_argument(
        "--global-artifacts",
        action="store_true",
        help=(
            "Generate only artifacts that depend on the complete " "instruction model."
        ),
    )

    mode.add_argument(
        "--list-outputs",
        action="store_true",
        help="List every planned generated output and exit.",
    )

    mode.add_argument(
        "--describe-build",
        action="store_true",
        help=(
            "Describe category/global build inputs and outputs as JSON "
            "without writing generated files."
        ),
    )

    args = parser.parse_args()

    if args.category is not None and not args.spec_file:
        parser.error("--category requires at least one --spec-file")

    if args.category is None and args.spec_file:
        parser.error("--spec-file is valid only with --category")

    if args.defer_finalization and (
        args.category is not None or args.global_artifacts
        or args.list_outputs or args.describe_build
    ):
        parser.error("--defer-finalization requires full generation")

    return args


def main() -> None:
    args = parse_arguments()

    spec_dir = args.spec_dir.resolve()
    output_dir = args.output.resolve()
    backend_spec = args.backend_spec.resolve()
    spec_files = tuple(path.resolve() for path in args.spec_file)

    validate_directory(spec_dir, "--spec-dir")
    validate_file(backend_spec, "--backend-spec")

    for spec_file in spec_files:
        validate_file(spec_file, "--spec-file")

    backend = load_cpp_backend(backend_spec)

    category_inputs: tuple[CodegenCategoryInputs, ...] = ()
    if args.category is not None:
        database = load_codegen_database_from_files(
            spec_files=spec_files,
            category=args.category,
        )
    elif args.describe_build:
        database, category_inputs = load_codegen_database_with_category_inputs(
            spec_dir=spec_dir
        )
    else:
        database = load_codegen_database(spec_dir=spec_dir)

    context = build_generation_context(database, backend)
    plan = build_generation_plan(context, output_dir)

    if args.describe_build:
        print(
            json.dumps(
                describe_build(plan, category_inputs=category_inputs),
                indent=2,
                sort_keys=True,
            )
        )
        return

    if args.list_outputs:
        for path in plan.paths:
            print(path)
        return

    output_dir.mkdir(parents=True, exist_ok=True)

    full_generation = args.category is None and not args.global_artifacts

    if args.category is not None:
        artifacts = plan.artifacts_for_category(args.category)

    elif args.global_artifacts:
        artifacts = plan.global_artifacts

    else:
        artifacts = plan.artifacts

    write_artifacts(context, artifacts, args.jobs)

    if (full_generation or args.global_artifacts) and not getattr(
        args, "defer_finalization", False
    ):
        remove_obsolete_generated_files(output_dir, plan.paths)
        write_output_manifest(output_dir, plan.paths)


def positive_integer(value: str) -> int:
    """Parse a strictly positive worker count for the command line."""

    try:
        jobs = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "--jobs must be a positive integer"
        ) from error
    if jobs < 1:
        raise argparse.ArgumentTypeError("--jobs must be a positive integer")
    return jobs


def write_artifacts(context, artifacts, jobs: int) -> None:
    """Write independent planned artifacts, joining all workers on failure."""

    if jobs == 1:
        for artifact in artifacts:
            write_formatted_artifact(context, artifact.emit, artifact.path)
        return

    with ThreadPoolExecutor(max_workers=jobs) as executor:
        futures = [
            executor.submit(
                write_formatted_artifact, context, artifact.emit, artifact.path
            )
            for artifact in artifacts
        ]
        try:
            for future in as_completed(futures):
                future.result()
        except BaseException:
            for future in futures:
                future.cancel()
            raise


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
        stat.S_IMODE(output_path.stat().st_mode) if output_path.exists() else 0o644
    )
    descriptor, candidate_name = tempfile.mkstemp(
        prefix=f".{output_path.stem}.",
        suffix=output_path.suffix,
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


def remove_obsolete_generated_files(
    output_dir: Path, active_paths: tuple[Path, ...]
) -> None:
    """Remove only prior generator outputs absent from the active artifact plan."""

    active = {path.relative_to(output_dir).as_posix() for path in active_paths}
    obsolete = read_output_manifest(output_dir) - active
    obsolete.update(
        {
            "private/syntax_descriptor.gen.cpp",
            "private/resolved_descriptor.gen.cpp",
            "private/resolved_ir_checker_descriptor.gen.cpp",
            "public/ptx_ir/resolved/resolved_ir.gen.hpp",
            "public/resolved_instruction_union.gen.hpp",
            "public/resolved_ir.gen.hpp",
            "public/resolved_ir_resolution.gen.hpp",
            "public/resolved_ir_checker.gen.hpp",
            "private/syntax_descriptor.gen.hpp",
        }
        - active
    )
    for pattern in (
        "public/ptx_ir/resolved/resolved_ir.gen.hpp",
        "public/ptx_ir_*.gen.hpp",
        "public/resolved_ir/model/*.gen.hpp",
        "public/resolved_ir/resolution/*.gen.hpp",
        "public/resolved_ir/checker/*.gen.hpp",
        "public/ptx_frontend/resolved_ir/model/*.gen.hpp",
        "public/ptx_frontend/resolved_ir/model/*/*.gen.hpp",
        "public/ptx_frontend/resolved_ir/resolution/*.gen.hpp",
        "public/ptx_frontend/resolved_ir/checker/*.gen.hpp",
        "public/ptx_frontend/resolved_ir/model/*/*/model.gen.hpp",
        "public/ptx_frontend/resolved_ir/model/*/*/resolution.gen.hpp",
        "public/ptx_frontend/resolved_ir/model/*/*/checker.gen.hpp",
        "public/ptx_frontend/resolved_ir_experiment/*.gen.hpp",
        "public/ptx_frontend/resolved_ir_experiment/model/*/*.gen.hpp",
        "private/ptx_parser_*.gen.hpp",
        "private/ptx_parser_*.gen.cpp",
        "private/resolved_ir_*.gen.cpp",
        "private/syntax_descriptor_*.gen.cpp",
        "private/resolved_descriptor_*.gen.cpp",
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
        raise ValueError(
            "generated-output manifest contains a path outside its output root"
        )
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


def describe_build(
    plan,
    *,
    category_inputs: tuple[CodegenCategoryInputs, ...],
) -> dict[str, object]:
    """Describe category/global inputs and outputs for the native build graph."""

    inputs = {
        group.category: tuple(Path(str(path)).resolve() for path in group.spec_files)
        for group in category_inputs
    }

    categories: list[dict[str, object]] = []

    for category in sorted(inputs):
        artifacts = plan.artifacts_for_category(category)

        categories.append(
            {
                "name": category,
                "spec_files": [str(path) for path in inputs[category]],
                "outputs": [str(artifact.path) for artifact in artifacts],
            }
        )

    return {
        "categories": categories,
        "global_outputs": [str(artifact.path) for artifact in plan.global_artifacts],
        "all_outputs": [str(path) for path in plan.paths],
    }
