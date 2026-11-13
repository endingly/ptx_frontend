#!/usr/bin/env python3
"""Generate and run a finite, side-effect-preserving PTX assembler comparison."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent


def digest(path):
    """Return the SHA256 of an existing file."""
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(command):
    """Capture a bounded subprocess, preserving diagnostics and timeout status."""
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        return {"command": command, "exit_code": result.returncode,
                "stdout": result.stdout[:65536], "stderr": result.stderr[:65536],
                "stdout_sha256": hashlib.sha256(result.stdout.encode()).hexdigest(),
                "stderr_sha256": hashlib.sha256(result.stderr.encode()).hexdigest(),
                "truncated": len(result.stdout) > 65536 or len(result.stderr) > 65536}
    except subprocess.TimeoutExpired as error:
        return {"command": command, "exit_code": None, "classification": "timeout",
                "stdout": str(error.stdout or "")[:65536],
                "stderr": str(error.stderr or "")[:65536]}


def module(body, declarations="", directives="", version="9.3", target="sm_80"):
    """Wrap an instruction in a complete kernel with an observable output pointer."""
    return (f".version {version}\n.target {target}\n.address_size 64\n{directives}"
            ".visible .entry k(.param .u64 output) {\n.reg .u64 addr;\n"
            "ld.param.u64 addr, [output];\n" + declarations + body + "ret;\n}\n")


def fixtures():
    """Construct only the explicitly selected ambiguity pairs and controls."""
    cases = []

    def add(name, source, target="sm_80"):
        cases.append((name, source, target))

    add("control-valid", module("mov.u32 value, 1;\nst.volatile.global.u32 [addr], value;\n", ".reg .u32 value;\n"))
    add("control-invalid-opcode", module("not_an_instruction;\n"))
    for name, directives in [
        ("file-single", '.file 1 "example.cu"\n'),
        ("file-repeated-example", '.file 1 "example.cu"\n.file 2 "kernel.cu"\n.file 1 "kernel.cu", 1339013327, 64118\n'),
        ("file-repeated-identical", '.file 1 "example.cu"\n.file 1 "example.cu"\n')]:
        add(name, module("mov.u32 value, 1;\nst.volatile.global.u32 [addr], value;\n", ".reg .u32 value;\n", directives=directives))
    for pm in [0, 7]:
        for width in [32, 64]:
            add(f"pm{pm}-64-mov-u{width}", module(
                f"mov.u{width} value, %pm{pm}_64;\nst.volatile.global.u{width} [addr], value;\n",
                f".reg .u{width} value;\n"))
    for kind in ["b32", "u32", "s32"]:
        add(f"bfe-{kind}", module(f"bfe.{kind} value, source, 1, 4;\nst.volatile.global.b32 [addr], value;\n",
            ".reg .b32 value, source;\nld.volatile.global.b32 source, [addr];\n"))
    for mode, width in [("wrap", 2), ("wrap", 33), ("clamp", 32), ("clamp", 33)]:
        add(f"bmsk-{mode}-width-{width}", module(
            f"bmsk.{mode}.b32 value, 1, {width};\nst.volatile.global.b32 [addr], value;\n",
            ".reg .b32 value;\n"))
    for op in ["max", "add"]:
        add(f"multimem-red-{op}-f64", module(
            f"multimem.red.relaxed.gpu.{op}.f64 [addr], value;\n",
            ".reg .f64 value;\nmov.f64 value, 0d3ff0000000000000;\n", target="sm_90"), "sm_90")
    for version, target, kind in [("1.0", "sm_10", "u16"), ("2.0", "sm_20", "u32"),
                                  ("9.3", "sm_80", "u16"), ("9.3", "sm_80", "u32")]:
        if version in ["1.0", "2.0"]:
            add(f"tid-v{version.replace('.', '-')}-{kind}",
                f".version {version}\n.target {target}\n.entry kernel() {{\n.reg .v4 .{kind} V;\nmov.v4.{kind} V, %tid;\nret;\n}}\n", target)
            continue
        add(f"tid-v{version.replace('.', '-')}-{kind}", module(
            f"mov.v4.{kind} values, %tid;\nst.volatile.global.v4.{kind} [addr], values;\n",
            f".reg .v4 .{kind} values;\n", version=version, target=target), target)
    for special, kind in [("ntid", "u16"), ("ctaid", "u16"), ("nctaid", "u16"),
                          ("tid", "s16"), ("tid", "b16")]:
        add(f"{special}-v9-3-{kind}", module(
            f"mov.v4.{kind} values, %{special};\nst.volatile.global.v4.{kind} [addr], values;\n",
            f".reg .v4 .{kind} values;\n"))
    for shape in ["v2-f64", "v4-f64", "v2-pred"]:
        vec, kind = shape.split("-")
        # Declaration checks do not depend on optimizer elimination of instructions.
        add(f"reg-{shape}", module("mov.u32 value, 1;\nst.volatile.global.u32 [addr], value;\n", f".reg .{vec} .{kind} values;\n.reg .u32 value;\n"))
    return cases


def classify(result):
    """Keep tool availability/target failures distinct from language rejection."""
    if result.get("classification") == "timeout":
        return "timeout"
    if result["exit_code"] == 0:
        return "accepted"
    diagnostics = result["stderr"] + result["stdout"]
    if "not defined for option 'gpu-name'" in diagnostics or "Unsupported .version" in diagnostics:
        return "unsupported-PTX/target"
    if "Unresolved extern" in diagnostics or "link" in diagnostics.lower():
        return "link/layout-failure"
    return "rejected-input"


def main():
    """Write complete fixtures, raw outcomes, and tool provenance."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ptxas", required=True)
    parser.add_argument("--frontend", required=True)
    parser.add_argument("--output-dir", type=Path, default=ROOT,
                        help="Separate output directory for a new revision's evidence")
    args = parser.parse_args()
    output_dir = args.output_dir.resolve()
    (output_dir / "fixtures").mkdir(parents=True, exist_ok=True)
    provenance = {"ptxas_path": args.ptxas, "ptxas_sha256": digest(args.ptxas),
                  "frontend_path": args.frontend, "frontend_sha256": digest(args.frontend),
                  "version": run([args.ptxas, "--version"]),
                  "help": run([args.ptxas, "--help"]),
                  "flags": ["-O0", "--gpu-name=<fixture target>", "--output-file=<temporary cubin>"],
                  "timeout_seconds_per_process": 30}
    records = []
    with tempfile.TemporaryDirectory(prefix="ptx93-ptxas-results-") as tmp:
        for name, source, target in fixtures():
            path = output_dir / "fixtures" / (name + ".ptx")
            path.write_text(source)
            cubin = Path(tmp) / (name + ".cubin")
            assembled = run([args.ptxas, "-O0", "--gpu-name=" + target,
                             "--output-file=" + str(cubin), str(path)])
            assembled["classification"] = classify(assembled)
            if cubin.exists():
                assembled["output_sha256"] = digest(cubin)
                assembled["output_bytes"] = cubin.stat().st_size
            frontend = run([args.frontend, str(path)])
            frontend["classification"] = classify(frontend)
            records.append({"case": name, "fixture": "fixtures/" + path.name,
                            "sha256": digest(path), "target": target,
                            "ptxas": assembled, "frontend": frontend})
    (output_dir / "results.json").write_text(json.dumps({"provenance": provenance, "cases": records}, indent=2) + "\n")
    for record in records:
        print(record["case"], record["ptxas"]["classification"], record["frontend"]["classification"])


if __name__ == "__main__":
    main()
