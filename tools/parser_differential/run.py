#!/usr/bin/env python3
"""Build isolated parser snapshots and compare complete public syntax identity."""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import random
import re
import subprocess
import tarfile
from pathlib import Path
from typing import Any


REFERENCE = "2bd78d605f7fc545c11b77f42e9cd5bc7fe376a4"
ARCHIVE_PATHS = ["cmake", "submod/common", "submod/base", "submod/lexer",
                 "submod/cst", "submod/syntax", "corpus/m9"]
SCHEMA_HEADERS = {
    "submod/common/include/source_loc.hpp": {"SourcePos", "SourceRange", "ResultWithDiagnostics"},
    "submod/lexer/include/ptx_token.hpp": {"Trivia", "PtxToken"},
    "submod/cst/include/ptx_cst_parser.hpp": {"CstParseDiagnostic"},
    "submod/syntax/include/ptx_syntax_parser.hpp": {"SyntaxParseDiagnostic"},
    "submod/syntax/include/ptx_syntax_lower.hpp": {"AstLowerDiagnostic"},
    "submod/cst/include/ptx_cst.hpp": None,
    "submod/syntax/include/ptx_syntax_ast.hpp": None,
}
PUBLIC_METHODS = {
    "SourcePos": {"SourcePos", "operator=="},
    "SourceRange": {"SourceRange", "operator=="},
    "ResultWithDiagnostics": {"has_value", "operator bool", "operator*", "operator->"},
    "CstConstantOperand": {"CstConstantOperand", "~CstConstantOperand", "operator="},
    "AstConstantOperand": {"AstConstantOperand", "~AstConstantOperand", "operator="},
    "CstFile": {"token", "sourceRange", "sourceText", "instruction", "module"},
    "AstFile": {"instruction", "module"},
}


def digest(data: bytes) -> str:
    """Return a stable content identity for build and corpus evidence."""
    return hashlib.sha256(data).hexdigest()


def input_manifest(root: Path) -> dict[str, str]:
    """Hash tracked and untracked build sources, excluding compiler/cache outputs."""
    suffixes = {".cpp", ".c", ".cc", ".cxx", ".h", ".hpp", ".hh", ".hxx",
                ".inc", ".tpp", ".def", ".y", ".l", ".cmake", ".in", ".py"}
    paths = []
    for directory in ARCHIVE_PATHS:
        if directory == "corpus/m9":
            continue
        paths.extend(path for path in (root / directory).rglob("*")
                     if path.is_file() and "__pycache__" not in path.parts
                     and (path.suffix in suffixes or path.name == "CMakeLists.txt"))
    return {str(path.relative_to(root)): digest(path.read_bytes()) for path in sorted(paths)}


def command(args: list[str], **kwargs: Any) -> subprocess.CompletedProcess:
    """Run a checked command without shell interpolation."""
    return subprocess.run(args, check=True, **kwargs)


def archive_tree(repo: Path, revision: str, output: Path) -> tuple[Path, dict]:
    """Reconstruct immutable tracked dependencies and verify every archived byte."""
    commit = command(["git", "-C", str(repo), "rev-parse", f"{revision}^{{commit}}"],
                     capture_output=True, text=True).stdout.strip()
    archive = command(["git", "-C", str(repo), "archive", commit, *ARCHIVE_PATHS],
                      capture_output=True).stdout
    root = output / f"source-{commit}"
    root.mkdir(parents=True, exist_ok=True)
    archived_files = set()
    with tarfile.open(fileobj=io.BytesIO(archive)) as source:
        for member in source.getmembers():
            if member.isdir():
                (root / member.name).mkdir(parents=True, exist_ok=True)
                continue
            if not member.isfile():
                raise RuntimeError(f"unsupported archive member: {member.name}")
            archived_files.add(member.name)
            destination = root / member.name
            contents = source.extractfile(member).read()
            if destination.exists():
                if destination.read_bytes() != contents:
                    raise RuntimeError(f"frozen source modified: {destination}")
            else:
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(contents)
    extra_files = {str(path.relative_to(root)) for path in root.rglob("*") if path.is_file()} - archived_files
    if extra_files:
        raise RuntimeError(f"unarchived frozen source files: {sorted(extra_files)}")
    return root, {"commit": commit, "archive_sha256": digest(archive),
                  "archive_paths": ARCHIVE_PATHS}


def public_fields(source: str, selected: set[str] | None,
                  methods: dict[str, set[str]] | None = None) -> dict[str, list[str]]:
    """Check the repository's simple public struct declarations, failing on drift.

    This intentionally supports the current field-only public records rather than
    parsing general C++. Only explicitly known methods and nested enums are skipped.
    Unrecognized declarations fail instead of silently producing partial schemas.
    """
    source = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)
    methods = PUBLIC_METHODS if methods is None else methods
    records = {}
    for match in re.finditer(r"\bstruct\s+(\w+)\s*\{", source):
        name = match[1]
        if selected is not None and name not in selected:
            continue
        depth, end = 1, match.end()
        while depth:
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        body = source[match.end():end - 1]
        declarations = []
        cursor, start = 0, 0

        def known_method(declaration: str) -> bool:
            """Reject callable fields/default expressions before skipping known methods."""
            prefix = declaration.split("(", 1)[0].strip()
            method = re.search(r"(operator(?: bool|==|=|\*|->)|~?\w+)\s*$", prefix)
            if not method or method[1] not in methods.get(name, set()):
                raise RuntimeError(f"unsupported parenthesized declaration {name}: {declaration}")
            before_name = prefix[:method.start()]
            if "=" in before_name or "(" in before_name or "<" in before_name:
                raise RuntimeError(f"unsupported parenthesized declaration {name}: {declaration}")
            return True

        while cursor < len(body):
            if body[cursor] == "{":
                prefix = body[start:cursor]
                function_body = "(" in prefix and not prefix.strip().startswith("enum ")
                if function_body:
                    known_method(prefix)
                nesting, closing = 1, cursor + 1
                while nesting:
                    nesting += (body[closing] == "{") - (body[closing] == "}")
                    closing += 1
                cursor = closing
                if function_body:
                    start = closing
                continue
            if body[cursor] == ";":
                declarations.append(body[start:cursor])
                start = cursor + 1
            cursor += 1
        if body[start:].strip():
            raise RuntimeError(f"unsupported trailing declaration {name}: {body[start:]}")
        fields = []
        for declaration in declarations:
            declaration = declaration.strip()
            if not declaration or declaration.startswith("enum "):
                continue
            if "(" in declaration or ")" in declaration:
                known_method(declaration)
                continue
            field = re.search(r"\b(\w+)\s*(?:\{\}|=\s*[^;]+)?\s*$", declaration)
            if not field:
                raise RuntimeError(f"unsupported public declaration {name}: {declaration}")
            fields.append(field[1])
        records[name] = fields
    return records


def check_schema(root: Path, tool: Path) -> None:
    """Require one explicit serializer field for every selected public field."""
    expected = {}
    for header, selected in SCHEMA_HEADERS.items():
        expected.update(public_fields((root / header).read_text(), selected))
    schema = (tool / "snapshot_fields.inc").read_text()
    actual = {}
    for match in re.finditer(r"SNAPSHOT_BEGIN\([^)]*::(\w+)\)(.*?)SNAPSHOT_END\(\)",
                             schema, re.S):
        actual[match[1]] = re.findall(r"SNAPSHOT_FIELD\((\w+)\)", match[2])
    # The result serializer is templated and implemented outside the record table.
    actual["ResultWithDiagnostics"] = re.findall(
        r"^\s+SNAPSHOT_RESULT_FIELD\((\w+)\);", (tool / "snapshot.cpp").read_text(), re.M)
    if actual != expected:
        changes = {name: {"public": expected.get(name), "snapshot": actual.get(name)}
                   for name in expected.keys() | actual.keys()
                   if expected.get(name) != actual.get(name)}
        raise RuntimeError(f"public snapshot schema drift: {json.dumps(changes)}")


def build(tool: Path, source: Path, output: Path, prefix: Path, compiler: str,
          backend: str, generated: bool = False) -> Path:
    """Build only lexer/CST/AST dependencies in an independent output tree."""
    command(["cmake", "-S", str(tool), "-B", str(output), "-G", "Ninja",
             f"-DPTX_SNAPSHOT_SOURCE_ROOT={source}",
             f"-DCMAKE_PREFIX_PATH={prefix}", f"-DCMAKE_CXX_COMPILER={compiler}",
             "-DCMAKE_BUILD_TYPE=Debug", f"-DPTX_FRONTEND_USE_BISON_PARSER={backend}",
             f"-DPTX_SNAPSHOT_GENERATED={'ON' if generated else 'OFF'}"])
    command(["cmake", "--build", str(output), "--target", "parser_snapshot", "-j", "4"])
    return output / "parser_snapshot"


def corpus(tool: Path, reference: Path, seed: int, fuzz_count: int) -> list[dict]:
    """Combine frozen regressions, limit boundaries and bounded deterministic fuzz."""
    cases = json.loads((tool / "corpus.json").read_text())
    for folder in ("corpus/m9", "submod/cst/fuzz/corpus"):
        for path in sorted((reference / folder).glob("*.ptx")):
            cases.append({"name": f"file/{path.relative_to(reference)}", "mode": "module",
                          "source_hex": path.read_bytes().hex()})
    for prefix in ("+", "(", "(.u64)", "1?1:"):
        for count in (126, 127, 128, 129, 4096):
            suffix = ")" * count if prefix == "(" else ""
            expression = prefix * count + "1" + suffix
            cases.append({"name": f"limit/{prefix}/{count}", "mode": "instruction",
                          "source": f"mov.u64 %r, {expression};"})
            cases.append({"name": f"limit-malformed/{prefix}/{count}",
                          "mode": "instruction", "source": "mov.u64 %r, " + prefix * count + ";"})
    for count in (126, 127, 128, 129, 255, 256, 257, 4096):
        cases.append({"name": f"initializer-depth/{count}", "mode": "module",
                      "source": ".global .u32 a[] = " + "{" * count + "1" + "}" * count
                      + "; .entry survivor() { ret; }"})
        cases.append({"name": f"block-depth/{count}", "mode": "module",
                      "source": ".entry k() {" + "{" * count + "ret;" + "}" * count + "}"})
    randomizer = random.Random(seed)
    seeds = [case.get("source", "").encode() for case in cases[:30]]
    alphabet = b".entry func global u32 %r 01289+-*/(){}[];,:?\n\r\t\x00\x7f\xff"
    for index in range(fuzz_count):
        if index % 3 == 0:
            value = bytes(randomizer.randrange(256) for _ in range(randomizer.randrange(96)))
        else:
            value = bytearray(randomizer.choice(seeds))
            for _ in range(randomizer.randrange(1, 6)):
                position = randomizer.randrange(len(value) + 1)
                if randomizer.randrange(2) and value:
                    del value[min(position, len(value) - 1)]
                else:
                    value[position:position] = bytes([randomizer.choice(alphabet)])
            value = bytes(value)
        cases.append({"name": f"fuzz/{seed}/{index}", "mode": "module" if index % 2 else "instruction",
                      "source_hex": value.hex()})
    return cases


def snapshot(binary: Path, case: dict, *, timeout: float = 10) -> dict:
    """Capture one bounded fresh-process snapshot, including arbitrary source bytes."""
    source = bytes.fromhex(case["source_hex"]) if "source_hex" in case else case["source"].encode()
    result = command([str(binary), case["mode"]], input=source,
                     capture_output=True, timeout=timeout)
    return decode_snapshot(result.stdout)


def select_cases(cases: list[dict], mode: str, pattern: str | None) -> tuple[list[dict], dict]:
    """Apply an explicit root/name selection and report every excluded case."""
    expression = re.compile(pattern) if pattern is not None else None
    selected = [case for case in cases if (mode == "all" or case["mode"] == mode)
                and (expression is None or expression.search(case["name"]))]
    return selected, {"mode": mode, "case_filter": pattern, "available_count": len(cases),
                      "selected_count": len(selected), "excluded_count": len(cases) - len(selected)}


def decode_snapshot(contents: bytes | str) -> dict:
    """Reject duplicate JSON keys so type/field collisions cannot hide identity."""
    def object_pairs(pairs: list[tuple[str, Any]]) -> dict:
        """Build one object only when every serialized field occurs exactly once."""
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate snapshot field: {key}")
            result[key] = value
        return result
    return json.loads(contents, object_pairs_hook=object_pairs)


def difference(expected: Any, actual: Any, path: str = "$") -> str | None:
    """Return the first exact structured mismatch with an actionable field path."""
    if type(expected) is not type(actual):
        return f"{path}: type {type(expected).__name__} != {type(actual).__name__}"
    if isinstance(expected, dict):
        if expected.keys() != actual.keys():
            return f"{path}: keys {sorted(expected)} != {sorted(actual)}"
        for key in expected:
            result = difference(expected[key], actual[key], f"{path}.{key}")
            if result:
                return result
    elif isinstance(expected, list):
        if len(expected) != len(actual):
            return f"{path}: length {len(expected)} != {len(actual)}"
        for index, (left, right) in enumerate(zip(expected, actual)):
            result = difference(left, right, f"{path}[{index}]")
            if result:
                return result
    elif expected != actual:
        return f"{path}: {expected!r} != {actual!r}"
    return None


def compare_files(reference: Path, candidate: Path) -> int:
    """Expose the exact comparator as a CLI with nonzero mismatch status."""
    mismatch = difference(decode_snapshot(reference.read_text()), decode_snapshot(candidate.read_text()))
    if mismatch:
        print(mismatch)
        return 1
    return 0


def main() -> int:
    """Freeze/build both trees, compare every case, and save reproducible evidence."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", default=REFERENCE)
    parser.add_argument("--candidate-revision", help="Archive this commit instead of using live sources")
    parser.add_argument("--candidate-source", type=Path)
    parser.add_argument("--candidate-backend", choices=["ON", "OFF"], default="ON")
    parser.add_argument("--generated", action="store_true", help="Compare the private full generated entry")
    parser.add_argument("--build-root", type=Path)
    parser.add_argument("--prefix", type=Path)
    parser.add_argument("--compiler", default="clang++-21")
    parser.add_argument("--seed", type=int, default=20261012)
    parser.add_argument("--fuzz-count", type=int, default=100)
    parser.add_argument("--mode", choices=["all", "instruction", "module"], default="all",
                        help="Explicitly select the parser entry roots to compare")
    parser.add_argument("--case-filter", help="Regular expression selecting case names")
    parser.add_argument("--compare-files", nargs=2, type=Path)
    args = parser.parse_args()
    if args.compare_files:
        return compare_files(*args.compare_files)
    tool = Path(__file__).resolve().parent
    repo = tool.parents[1]
    output = (args.build_root or repo / "out/parser-differential").resolve()
    output.mkdir(parents=True, exist_ok=True)
    prefix = (args.prefix or repo / "out/build/ci-linux-clang-debug/vcpkg_installed/x64-linux").resolve()
    reference, reference_identity = archive_tree(repo, args.reference, output)
    if args.candidate_revision:
        candidate, candidate_identity = archive_tree(repo, args.candidate_revision, output)
    else:
        candidate = (args.candidate_source or repo).resolve()
        candidate_identity = {"source_root": str(candidate), "tracked_diff_sha256": digest(
            command(["git", "-C", str(candidate), "diff", "HEAD", "--", *ARCHIVE_PATHS],
                    capture_output=True).stdout), "head": command(
            ["git", "-C", str(candidate), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()}
    check_schema(reference, tool)
    check_schema(candidate, tool)
    source_inputs = {"reference": input_manifest(reference), "candidate": input_manifest(candidate)}
    left = build(tool, reference, output / "build-reference", prefix, args.compiler, "OFF")
    right = build(tool, candidate, output / "build-candidate", prefix, args.compiler,
                  args.candidate_backend, args.generated)
    available = corpus(tool, reference, args.seed, args.fuzz_count)
    try:
        cases, selection = select_cases(available, args.mode, args.case_filter)
    except re.error as error:
        parser.error(f"invalid --case-filter: {error}")
    if not cases:
        parser.error("case selection is empty")
    canonical = json.dumps(cases, sort_keys=True).encode()
    manifest = {"schema": 1, "reference": reference_identity, "candidate": candidate_identity,
                "candidate_backend": args.candidate_backend, "generated": args.generated,
                "compiler": args.compiler, "prefix": str(prefix), "seed": args.seed,
                "fuzz_count": args.fuzz_count, "case_count": len(cases),
                "case_selection": selection,
                "corpus_sha256": digest(canonical), "canonicalizations": [],
                "source_inputs": source_inputs,
                "compiler_version": command([args.compiler, "--version"], capture_output=True,
                                             text=True).stdout,
                "tool_sha256": {path.name: digest(path.read_bytes()) for path in sorted(tool.iterdir())
                                if path.is_file() and path.suffix in (".cpp", ".inc", ".py", ".json", ".txt")}}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    failures = []
    observed_types = set()
    for case in cases:
        try:
            expected, actual = snapshot(left, case), snapshot(right, case)
            mismatch = difference(expected, actual)
            observed_types.update(re.findall(r'"_type": "([^"]+)"', json.dumps(expected)))
            if mismatch:
                stem = re.sub(r"[^A-Za-z0-9_.-]", "_", case["name"])
                for label, value in (("reference", expected), ("candidate", actual)):
                    (output / f"{stem}.{label}.json").write_text(json.dumps(value, indent=2) + "\n")
                failures.append({"name": case["name"], "difference": mismatch})
        except (subprocess.SubprocessError, ValueError) as error:
            failures.append({"name": case["name"], "error": str(error)})
    report = {"case_count": len(cases), "case_selection": selection,
              "failures": failures, "observed_types": sorted(observed_types)}
    if source_inputs != {"reference": input_manifest(reference), "candidate": input_manifest(candidate)}:
        report["failures"].append({"error": "build inputs changed during differential run"})
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Compared {len(cases)} cases: {len(failures)} mismatches; evidence: {output}")
    for failure in failures[:10]:
        print(json.dumps(failure))
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
