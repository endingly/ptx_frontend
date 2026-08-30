#!/usr/bin/env python3

"""Regenerate the frozen M12 nvcc PTX corpus."""

from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


PYTHON_ROOT = Path(__file__).resolve().parents[1]
ROOT = PYTHON_ROOT.parent

if str(PYTHON_ROOT) not in sys.path:
    sys.path.insert(0, str(PYTHON_ROOT))


from code_gen.database import load_codegen_database
from code_gen.m12_natural_corpus import (
    build_natural_manifest,
    canonical_bytes,
    fixture_targets,
    normalize_nvcc_ptx,
    target_directives,
)


TARGETS = ("sm_80", "sm_90a", "sm_100")
VERSION = "Cuda compilation tools, release 13.3, V13.3.33"
PROVENANCE_VERSION = "ptx_frontend.corpus_provenance/v3"
LANES = (
    (
        "common_kernel",
        "inline_ptx_wrapper",
        "nvcc-compiled project-authored CUDA inline-PTX fixture, not natural compiler emission",
    ),
    (
        "natural_kernel",
        "natural_compiler_emission",
        "ordinary CUDA source compiled by nvcc without inline PTX",
    ),
)


class RegenerationError(ValueError):
    pass


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="report stale outputs without writing")
    parser.add_argument("--nvcc", default="nvcc", help="nvcc executable (default: %(default)s)")
    return parser.parse_args(argv)


def verify_nvcc(nvcc: str) -> None:
    try:
        result = subprocess.run(
            [nvcc, "--version"], text=True, capture_output=True, check=False
        )
    except OSError as error:
        raise RegenerationError(f"cannot execute nvcc {nvcc!r}: {error}") from error
    if result.returncode or VERSION not in (result.stdout + "\n" + result.stderr).splitlines():
        raise RegenerationError(f"nvcc must report exactly {VERSION}")


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=2) + "\n").encode("utf-8")


def desired_provenance(root: Path, generated: dict[str, bytes]) -> bytes:
    document = json.loads((root / "corpus/provenance.json").read_bytes())
    sources = [
        {
            "path": f"corpus/m12/{lane}.cu",
            "sha256": hashlib.sha256(
                canonical_bytes(
                    (root / f"corpus/m12/{lane}.cu").read_bytes(),
                    f"corpus/m12/{lane}.cu",
                )
            ).hexdigest(),
        }
        for lane, _, _ in LANES
    ]
    wanted = {
        relative: {
            "path": relative,
            "kind": kind,
            "sha256": hashlib.sha256(data).hexdigest(),
            "generator": "nvcc V13.3.33",
            "toolkit": "CUDA Toolkit 13.3",
            "targets": [target],
            "source_path": f"corpus/m12/{lane}.cu",
            "notes": source,
            "license": "MIT",
        }
        for lane, kind, source in LANES
        for target in TARGETS
        for relative, data in [
            (f"corpus/m12/{lane}_{target.replace('_', '')}.ptx", generated[f"corpus/m12/{lane}_{target.replace('_', '')}.ptx"])
        ]
    }
    seen = set()
    fixtures = []
    for record in document["fixtures"]:
        replacement = wanted.get(record["path"])
        fixtures.append(replacement or record)
        if replacement:
            seen.add(record["path"])
    fixtures.extend(wanted[path] for path in wanted if path not in seen)
    desired = {
        "schema": PROVENANCE_VERSION,
        "sources": sources,
        "fixtures": fixtures,
    }
    return json_bytes(desired)


def compile_outputs(root: Path, nvcc: str) -> tuple[dict[str, bytes], dict[str, object]]:
    generated: dict[str, bytes] = {}
    with tempfile.TemporaryDirectory(prefix="ptx-m12-") as directory:
        temporary = Path(directory)
        for lane, _, _ in LANES:
            source = root / f"corpus/m12/{lane}.cu"
            for target in TARGETS:
                relative = f"corpus/m12/{lane}_{target.replace('_', '')}.ptx"
                output = temporary / relative
                output.parent.mkdir(parents=True, exist_ok=True)
                result = subprocess.run(
                    [nvcc, "-ptx", f"-arch={target}", str(source), "-o", str(output)],
                    text=True,
                    capture_output=True,
                    check=False,
                )
                if result.returncode:
                    raise RegenerationError(
                        f"{relative}: nvcc failed: {result.stderr.strip() or result.stdout.strip()}"
                    )
                try:
                    data = normalize_nvcc_ptx(output.read_bytes(), relative)
                except OSError as error:
                    raise RegenerationError(f"{relative}: nvcc did not create output") from error
                output.write_bytes(data)
                if fixture_targets(output) != [target] or target_directives(
                    output.read_text(encoding="utf-8")
                ) != [(target, ())]:
                    raise RegenerationError(f"{relative}: expected exactly .target {target}")
                generated[relative] = data

        database = load_codegen_database(spec_dir=root / "instructions/ptx_spec")
        natural = build_natural_manifest(
            {
                target: temporary
                / f"corpus/m12/natural_kernel_{target.replace('_', '')}.ptx"
                for target in TARGETS
            },
            database,
            root=temporary,
        )
    return generated, natural


def atomic_replace(path: Path, data: bytes) -> None:
    descriptor, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
        os.replace(temporary, path)
    except BaseException:
        Path(temporary).unlink(missing_ok=True)
        raise


def print_diff(path: Path, current: bytes, desired: bytes) -> None:
    current_text = current.decode("utf-8", errors="replace").splitlines(keepends=True)
    desired_text = desired.decode("utf-8", errors="replace").splitlines(keepends=True)
    sys.stdout.writelines(
        difflib.unified_diff(current_text, desired_text, fromfile=str(path), tofile=str(path))
    )


def regenerate(root: Path, nvcc: str, check: bool) -> int:
    verify_nvcc(nvcc)
    generated, natural = compile_outputs(root, nvcc)
    desired = {
        **generated,
        "corpus/m12/natural_manifest.json": json_bytes(natural),
    }
    desired["corpus/provenance.json"] = desired_provenance(root, generated)
    changed = []
    for relative, data in desired.items():
        path = root / relative
        current = path.read_bytes() if path.exists() else b""
        if current != data:
            changed.append((path, current, data))
    if check:
        for change in changed:
            print_diff(*change)
        return int(bool(changed))
    for path, _, data in changed:
        atomic_replace(path, data)
    return 0


def main(argv: list[str] | None = None, root: Path = ROOT) -> int:
    arguments = parse_arguments(argv)
    try:
        return regenerate(root, arguments.nvcc, arguments.check)
    except (OSError, RegenerationError, ValueError) as error:
        print(f"regenerate_m12_corpus: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
