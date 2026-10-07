#!/usr/bin/env python3
"""Publish deterministic Linux toolchain and vcpkg cache identities."""

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from typing import Mapping


def resolve_compiler(variable: str, environment: Mapping[str, str]) -> Path:
    """Resolve the required compiler named by *variable* to a real executable."""
    compiler = environment.get(variable)
    if not compiler:
        raise ValueError(f"{variable} must name a compiler executable")
    executable = shutil.which(compiler, path=environment.get("PATH"))
    if executable is None:
        raise ValueError(f"{variable} compiler is not executable: {compiler!r}")
    return Path(executable).resolve(strict=True)


def update_executable_identity(
    digest: "hashlib._Hash", label: str, executable: Path, arguments: tuple[str, ...]
) -> None:
    """Add executable path, content, version, and target details to *digest*."""
    digest.update(f"{label}:path={executable}\n".encode("utf-8"))
    digest.update(f"{label}:content=".encode("utf-8"))
    digest.update(hashlib.sha256(executable.read_bytes()).digest())
    digest.update(b"\n")
    for argument in arguments:
        digest.update(f"{label}:{argument}=".encode("utf-8"))
        digest.update(subprocess.check_output([str(executable), argument]))
        digest.update(b"\n")


def compiler_identity(environment: Mapping[str, str]) -> str:
    """Hash installed tools and OS identity, excluding runner image revisions."""
    digest = hashlib.sha256()
    digest.update(Path("/etc/os-release").read_bytes())
    digest.update(f"ImageOS={environment.get('ImageOS', 'local')}\n".encode("utf-8"))

    for variable in ("CC", "CXX"):
        update_executable_identity(
            digest, variable, resolve_compiler(variable, environment), ("--version", "-dumpmachine")
        )
    for program, argument in (("cmake", "--version"), ("ninja", "--version")):
        executable = shutil.which(program, path=environment.get("PATH"))
        if executable is None:
            raise ValueError(f"Required build tool is not executable: {program}")
        update_executable_identity(digest, program, Path(executable).resolve(strict=True), (argument,))
    return digest.hexdigest()


def vcpkg_baseline(manifest_path: Path) -> str:
    """Read and validate the immutable vcpkg builtin baseline in *manifest_path*."""
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    baseline = manifest["builtin-baseline"]
    if not isinstance(baseline, str) or re.fullmatch(r"[0-9a-f]{40}", baseline) is None:
        raise ValueError("vcpkg builtin-baseline must be a lowercase 40-character commit SHA")
    return baseline


def python_version() -> str:
    """Return the running interpreter version for Python download-cache partitioning."""
    return ".".join(map(str, sys.version_info[:3]))


def main() -> None:
    """Validate every input before appending all cache-identity outputs together."""
    environment = os.environ.copy()
    baseline = vcpkg_baseline(Path("vcpkg.json"))
    identity = compiler_identity(environment)
    interpreter = python_version()
    output_path = Path(environment["GITHUB_OUTPUT"])
    with output_path.open("a", encoding="utf-8") as output:
        output.write(f"compiler={identity}\nvcpkg-revision={baseline}\npython={interpreter}\n")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError, subprocess.CalledProcessError) as error:
        sys.exit(f"Cannot compute compiler cache identity: {error}")
