#!/usr/bin/env python3
"""Measure compilation invalidation from one temporary SELP model change.

Build the selected Debug or Release test target once before running this script.
The probe temporarily adds a condition-code annotation to the SELP U32 variant,
which changes its opcode model leaf (or the category header in older builds)
without changing C++ names or layouts.
The original spec and generated files are restored even if compilation fails.
Run this script separately against a baseline checkout and the candidate tree.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import time


PROBE_OLD = "      - name: selp_u32\n        description:"
PROBE_NEW = (
    "      - name: selp_u32\n"
    "        condition_code_effect: carry_out\n"
    "        description:"
)
STAT_KEYS = ("direct_cache_hit", "preprocessed_cache_hit", "cache_miss", "cleanups_performed")
CACHE_SIZE_KEYS = ("cache_size_kibibyte", "max_cache_size_kibibyte")


def aggregate_dependency_count(build_dir: Path, test_sources: tuple[Path, ...]) -> int:
    """Count test source objects whose Ninja dependencies reach the global model."""
    object_root = build_dir / "submod/resolved_ir/CMakeFiles/test_resolved_ir.dir/test"
    objects = tuple(object_root / f"{source.name}.o" for source in test_sources)
    if not all(path.is_file() for path in objects):
        raise RuntimeError(f"Warm test objects are missing below {object_root}")
    command = ["ninja", "-C", str(build_dir), "-t", "deps"]
    command.extend(str(path.relative_to(build_dir)) for path in objects)
    output = subprocess.check_output(command, text=True)
    dependent: set[str] = set()
    current: str | None = None
    for line in output.splitlines():
        if ": #deps " in line:
            current = line.split(": #deps ", 1)[0]
        elif current and (
            "/resolved_ir.gen.hpp" in line
            or "/resolved_instruction_union.gen.hpp" in line
        ):
            dependent.add(current)
    return len(dependent)


def cache_stats() -> dict[str, int]:
    """Read cumulative ccache counters without resetting a shared cache."""
    output = subprocess.check_output(["ccache", "--print-stats", "--format=json"], text=True)
    stats = json.loads(output)
    return {key: int(stats.get(key, 0)) for key in (*STAT_KEYS, *CACHE_SIZE_KEYS)}


def build_phase(build_dir: Path, target: str, jobs: int, log: Path) -> dict[str, object]:
    """Build one phase and record elapsed time, C++ compiles, and cache deltas."""
    before = cache_stats()
    start = time.monotonic()
    with log.open("w", encoding="utf-8") as stream:
        command = ["cmake", "--build", str(build_dir), "--target", target, "-j", str(jobs)]
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, check=False)
    elapsed = time.monotonic() - start
    after = cache_stats()
    output = log.read_text(encoding="utf-8")
    phase = {
        "target": target,
        "seconds": round(elapsed, 3),
        "cpp_compiles": len(re.findall(r"Building CXX object", output)),
        "test_tu_compiles": len(
            re.findall(r"Building CXX object .*test_resolved_ir\.dir/test/[^\n]+\.cpp\.o", output)
        ),
        "ccache": {key: after[key] - before[key] for key in STAT_KEYS},
        "ccache_size_kib_before": before["cache_size_kibibyte"],
        "ccache_size_kib_after": after["cache_size_kibibyte"],
        "ccache_max_size_kib": after["max_cache_size_kibibyte"],
        "exit_code": result.returncode,
        "log": str(log),
    }
    if result.returncode:
        raise RuntimeError(f"{target} failed; see {log}")
    return phase


def main() -> None:
    """Apply the temporary model probe and measure production and test builds."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    source = args.source_root.resolve()
    build_dir = args.build_dir.resolve()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    spec = source / "python/src/ptx_frontend/spec/resources/ptx_spec/comparison_and_selection.yaml"
    original = spec.read_text(encoding="utf-8")
    match = re.search(r"^codegen_category:\s*([a-z_]+)\s*$", original, re.MULTILINE)
    if not match:
        raise RuntimeError("Spec codegen category is missing")
    category = match.group(1)
    opcode_header = (
        build_dir
        / f"submod/resolved_ir/generated/public/ptx_frontend/resolved_ir/model/{category}/selp/model.gen.hpp"
    )
    category_header = (
        build_dir
        / f"submod/resolved_ir/generated/public/ptx_frontend/resolved_ir/model/{category}.gen.hpp"
    )
    generated = opcode_header if opcode_header.is_file() else category_header
    if original.count(PROBE_OLD) != 1 or PROBE_NEW in original:
        raise RuntimeError("SELP U32 probe anchor is missing or already modified")
    if not generated.is_file():
        raise RuntimeError(f"Warm generated header is missing: {generated}")
    original_generated = generated.read_bytes()
    test_sources = tuple((source / "submod/resolved_ir/test").glob("*.cpp"))
    aggregate_include = "#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>"
    record: dict[str, object] = {
        "source": str(source),
        "build_dir": str(build_dir),
        "probe_category": category,
        "probe_model_header": str(generated),
        "head": subprocess.check_output(
            ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
        ).strip(),
        "test_source_tus": len(test_sources),
        "aggregate_including_test_source_tus": sum(
            aggregate_include in path.read_text(encoding="utf-8") for path in test_sources
        ),
        "aggregate_dependent_test_source_tus": aggregate_dependency_count(
            build_dir, test_sources
        ),
    }
    try:
        spec.write_text(original.replace(PROBE_OLD, PROBE_NEW, 1), encoding="utf-8")
        record["production"] = build_phase(
            build_dir, "ptx_frontend_resolved_ir", args.jobs, output.with_suffix(".production.log")
        )
        record["model_header_changed"] = generated.read_bytes() != original_generated
        if not record["model_header_changed"]:
            raise RuntimeError(f"Spec probe did not change {generated.name}")
        record["test_support"] = build_phase(
            build_dir, "test_resolved_ir", args.jobs, output.with_suffix(".test.log")
        )
    finally:
        spec.write_text(original, encoding="utf-8")
        with output.with_suffix(".restore.log").open("w", encoding="utf-8") as stream:
            restored = subprocess.run(
                ["cmake", "--build", str(build_dir), "--target", "resolved_ir_codegen", "-j", str(args.jobs)],
                stdout=stream,
                stderr=subprocess.STDOUT,
                check=False,
            )
        record["restore_exit_code"] = restored.returncode
        output.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
        if restored.returncode:
            raise RuntimeError(f"Code generation restoration failed; see {output.with_suffix('.restore.log')}")
    print(output.read_text(encoding="utf-8"))


if __name__ == "__main__":
    main()
