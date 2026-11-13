#!/usr/bin/env python3
"""Copy read-only seeds to ignored scratch, replay, then run bounded fuzz campaigns."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


def main():
    """Run both targets with fixed budgets and retain commands, logs and artifacts."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path("out/build/fuzz-asan"))
    parser.add_argument("--scratch", type=Path, default=Path("out/fuzz-smoke"))
    parser.add_argument("--runs", type=int, default=2000)
    parser.add_argument("--seconds", type=int, default=60)
    parser.add_argument("--seed", type=int, default=93)
    args = parser.parse_args()
    if args.runs < 1 or args.seconds < 1:
        parser.error("runs and seconds must be positive")
    root = Path(__file__).resolve().parents[1]
    build = args.build.resolve()
    scratch = args.scratch.resolve()
    if root / "out" not in scratch.parents:
        parser.error("scratch must be inside this repository's ignored out directory")
    scratch.mkdir(parents=True, exist_ok=True)
    records = []
    environment = os.environ.copy()
    environment["ASAN_OPTIONS"] = "abort_on_error=1:detect_leaks=1"
    environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"

    def file_hash(path):
        """Hash large executables without retaining their bytes in memory."""
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
        return digest.hexdigest()

    metadata_files = [build / name for name in
                      ("fuzz_frontend_canary", "fuzz_lexer_cst", "fuzz_frontend",
                       "compile_commands.json", "CMakeCache.txt")]
    metadata_files.append(Path(__file__).resolve())
    (scratch / "run-metadata.json").write_text(json.dumps({
        "files": {str(path): file_hash(path) for path in metadata_files if path.is_file()},
        "environment": {name: environment[name] for name in ("ASAN_OPTIONS", "UBSAN_OPTIONS")},
    }, indent=2) + "\n")

    def run(command, log_name, timeout):
        """Retain a complete replay command and output, and fail on any tool error."""
        started = time.monotonic()
        record = {"command": [str(x) for x in command], "log": log_name}
        records.append(record)
        try:
            with (scratch / log_name).open("w") as log:
                result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                        env=environment, timeout=timeout, check=False)
            record["returncode"] = result.returncode
            if result.returncode:
                raise RuntimeError(f"failed ({result.returncode}): {scratch / log_name}")
        except subprocess.TimeoutExpired:
            record["timed_out"] = True
            raise
        finally:
            record["seconds"] = round(time.monotonic() - started, 3)
            (scratch / "summary.json").write_text(json.dumps(records, indent=2) + "\n")

    run([build / "fuzz_frontend_canary"], "canary.log", 30)
    sources = [root / "fuzz/corpus", root / "submod/cst/fuzz/corpus",
               root / "corpus/ptx93_real_inputs/generated", root / "corpus/m12"]
    manifest = []
    for directory in sources:
        for source in sorted(directory.rglob("*")):
            if source.is_file() and (directory in sources[:2] or source.suffix == ".ptx"):
                data = source.read_bytes()
                manifest.append((source.relative_to(root).as_posix(), data))
    (scratch / "seeds.json").write_text(json.dumps(
        [{"path": path, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
         for path, data in manifest], indent=2) + "\n")
    initial_corpora = {}
    for target in ("fuzz_lexer_cst", "fuzz_frontend"):
        corpus = scratch / target / "corpus"
        artifacts = scratch / target / "artifacts"
        corpus.mkdir(parents=True, exist_ok=True)
        artifacts.mkdir(parents=True, exist_ok=True)
        for path, data in manifest:
            (corpus / hashlib.sha256(path.encode()).hexdigest()).write_bytes(data)
        initial_corpora[target] = [
            {"name": item.name, "bytes": item.stat().st_size,
             "sha256": hashlib.sha256(item.read_bytes()).hexdigest()}
            for item in sorted(corpus.iterdir()) if item.is_file()]
        (scratch / "initial-corpora.json").write_text(
            json.dumps(initial_corpora, indent=2) + "\n")
        common = [str(build / target), f"-seed={args.seed}", "-max_len=65536",
                  "-timeout=5", "-rss_limit_mb=2048", f"-artifact_prefix={artifacts}/",
                  f"-dict={root / 'fuzz/ptx.dict'}"]
        run(common + ["-runs=0", str(corpus)], target + "-replay.log", args.seconds + 60)
        run(common + [f"-runs={args.runs}", f"-max_total_time={args.seconds}", str(corpus)],
            target + "-campaign.log", args.seconds + 60)
    print(f"Fuzz smoke passed; {len(manifest)} source seeds; evidence: {scratch}")


if __name__ == "__main__":
    main()
