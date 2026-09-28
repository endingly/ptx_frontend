#!/usr/bin/env python3
"""Probe generated implementation invalidation after an atom-only spec edit.

Warm the resolved-IR library in the selected build directory first. The probe
temporarily renames one atom variant, builds the library, records changed source
bytes and compiler invocations, then restores the spec and rebuilds the library.
Run it against separate baseline and candidate worktrees with the same build
configuration and ``CCACHE_DISABLE=1`` for a comparable compilation count.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time


ORIGINAL = b"      - name: atom_global_add_u32\n"
PROBED = b"      - name: atom_global_add_u32_probe\n"
CATEGORY = "parallel_synchronization_and_communication"


def digest(path: Path) -> str:
    """Return the SHA-256 digest of one generated implementation source."""

    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(build_dir: Path, jobs: int, log: Path) -> dict[str, object]:
    """Build resolved IR and record wall time plus actual C++ object compiles."""

    start = time.monotonic()
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run(
            [
                "cmake", "--build", str(build_dir), "--target",
                "ptx_frontend_resolved_ir", "-j", str(jobs),
            ],
            stdout=stream,
            stderr=subprocess.STDOUT,
            check=False,
        )
    output = log.read_text(encoding="utf-8")
    return {
        "seconds": round(time.monotonic() - start, 3),
        "exit_code": result.returncode,
        "compiled_objects": re.findall(r"Building CXX object ([^\n]+\.o)", output),
        "log": str(log),
    }


def main() -> None:
    """Measure one atom variant edit without retaining probe-generated outputs."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    source_root = args.source_root.resolve()
    build_dir = args.build_dir.resolve()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)

    spec = (
        source_root
        / "python/src/ptx_frontend/spec/resources/ptx_spec"
        / f"{CATEGORY}.yaml"
    )
    original = spec.read_bytes()
    if original.count(ORIGINAL) != 1 or PROBED in original:
        raise RuntimeError("atom variant probe anchor is missing or ambiguous")
    generated = build_dir / "submod/resolved_ir/generated/private"
    sources = tuple(sorted(generated.glob(f"resolved_ir_{CATEGORY}*.gen.cpp")))
    if not sources:
        raise RuntimeError(f"warm generated source is missing below {generated}")
    before = {path.name: digest(path) for path in sources}

    record: dict[str, object] = {
        "head": subprocess.check_output(
            ["git", "-C", str(source_root), "rev-parse", "HEAD"], text=True
        ).strip(),
        "source_root": str(source_root),
        "build_dir": str(build_dir),
    }
    try:
        spec.write_bytes(original.replace(ORIGINAL, PROBED, 1))
        record["probe"] = build(
            build_dir, args.jobs, output.with_suffix(".probe.log")
        )
        record["generated_source_changed"] = {
            path.name: digest(path) != before[path.name] for path in sources
        }
    finally:
        spec.write_bytes(original)
        record["restore"] = build(
            build_dir, args.jobs, output.with_suffix(".restore.log")
        )
        output.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")

    if record["probe"]["exit_code"] or record["restore"]["exit_code"]:
        raise RuntimeError(f"probe or restoration build failed; see {output}")
    print(output.read_text(encoding="utf-8"))


if __name__ == "__main__":
    main()
