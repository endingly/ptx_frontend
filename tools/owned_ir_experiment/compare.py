"""Compare two owned-IR builds in alternating order and retain raw data."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import statistics
import subprocess


METRICS = (
    "resolve_and_validate",
    "validate_owned",
    "copy_module",
    "traverse_opcodes",
)


def run_one(executable: Path, corpus: Path, iterations: int) -> dict[str, object]:
    """Run one isolated benchmark process and return its complete JSON line."""
    completed = subprocess.run(
        [str(executable), str(corpus), str(iterations)],
        check=True,
        capture_output=True,
        text=True,
    )
    result = json.loads(completed.stdout)
    if not isinstance(result, dict):
        raise ValueError("Benchmark did not return a JSON object")
    return result


def main() -> None:
    """Collect alternating process runs and print raw samples with medians."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline_executable", type=Path)
    parser.add_argument("candidate_executable", type=Path)
    parser.add_argument("corpus", type=Path)
    parser.add_argument("--iterations", type=int, default=200)
    parser.add_argument("--runs", type=int, default=5)
    args = parser.parse_args()
    if args.iterations <= 0 or args.runs <= 0:
        parser.error("iterations and runs must be positive")
    executables = {"baseline": args.baseline_executable,
                   "candidate": args.candidate_executable}
    raw: dict[str, list[dict[str, object]]] = {name: [] for name in executables}
    for index in range(args.runs):
        order = ("baseline", "candidate") if index % 2 == 0 else ("candidate", "baseline")
        for name in order:
            result = run_one(executables[name], args.corpus, args.iterations)
            if result.get("configuration") != "owned":
                raise ValueError(f"{name} executable is not an owned-IR build")
            raw[name].append(result)
    for key in ("functions", "instructions"):
        values = {record[key] for records in raw.values() for record in records}
        if len(values) != 1:
            raise ValueError(f"Benchmark configurations disagree about {key}")
    medians = {
        name: {
            metric: statistics.median(
                float(record[f"{metric}_median_ns"]) for record in records
            )
            for metric in METRICS
        }
        for name, records in raw.items()
    }
    print(json.dumps({"raw_process_runs": raw, "process_medians_ns": medians}, indent=2))


if __name__ == "__main__":
    main()
