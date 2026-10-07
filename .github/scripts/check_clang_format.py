#!/usr/bin/env python3
"""Reject formatting drift in tracked handwritten C and C++ sources."""

import argparse
from pathlib import Path
import re
import subprocess
import sys


CLANG_FORMAT_MAJOR = 21
SOURCE_SUFFIXES = frozenset({".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"})
GENERATED_COMPONENTS = frozenset({"generated", "vendor", "third_party"})
GENERATED_SUFFIXES = (".gen.c", ".gen.cc", ".gen.cpp", ".gen.cxx", ".gen.h", ".gen.hh", ".gen.hpp", ".gen.hxx")
VERSION_PATTERN = re.compile(r"clang-format version (\d+)(?:\.|\s)")


def clang_format_major(version_output: str) -> int:
    """Extract clang-format's major version from its version output."""
    match = VERSION_PATTERN.search(version_output)
    if match is None:
        raise ValueError(f"Cannot determine clang-format major version: {version_output!r}")
    return int(match.group(1))


def is_handwritten_source(path: Path) -> bool:
    """Return whether a tracked path is a handwritten C or C++ source file."""
    return (
        path.suffix in SOURCE_SUFFIXES
        and not any(component in GENERATED_COMPONENTS for component in path.parts)
        and not path.name.endswith(GENERATED_SUFFIXES)
    )


def tracked_handwritten_sources(repository_root: Path) -> list[Path]:
    """List tracked handwritten C/C++ files without consulting untracked build output."""
    result = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=repository_root,
        check=True,
        capture_output=True,
    )
    paths = (Path(raw.decode("utf-8")) for raw in result.stdout.split(b"\0") if raw)
    return sorted(path for path in paths if is_handwritten_source(path))


def formatting_failures(
    repository_root: Path, clang_format: str, sources: list[Path]
) -> list[str]:
    """Return every source that differs from the configured formatter output."""
    failures: list[str] = []
    for source in sources:
        result = subprocess.run(
            [clang_format, "--dry-run", "--Werror", "--style=file", str(source)],
            cwd=repository_root,
            check=False,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            detail = (result.stderr or result.stdout).strip()
            failures.append(f"{source}: {detail}" if detail else str(source))
    return failures


def main() -> int:
    """Check the pinned formatter and every tracked handwritten C/C++ source."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang-format", default="clang-format-21")
    parser.add_argument("--repository-root", type=Path, default=Path("."))
    arguments = parser.parse_args()
    root = arguments.repository_root.resolve()

    try:
        version = subprocess.run(
            [arguments.clang_format, "--version"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        major = clang_format_major(version)
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f"Cannot verify clang-format: {error}", file=sys.stderr)
        return 1
    if major != CLANG_FORMAT_MAJOR:
        print(
            f"Expected clang-format major {CLANG_FORMAT_MAJOR}, found {major}.",
            file=sys.stderr,
        )
        return 1

    try:
        failures = formatting_failures(
            root, arguments.clang_format, tracked_handwritten_sources(root)
        )
    except (OSError, subprocess.CalledProcessError, UnicodeError) as error:
        print(f"Cannot check formatting: {error}", file=sys.stderr)
        return 1
    if failures:
        print("clang-format drift in tracked handwritten sources:", file=sys.stderr)
        print("\n".join(failures), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
