"""Line-oriented spelling evidence from frozen nvcc PTX output for M12."""

from __future__ import annotations

from collections import Counter
from pathlib import Path
import re

from code_gen.database import CodegenDatabase, _variant_modifier_language


_ENTRY = re.compile(r"^\.visible\s+\.entry\s+([A-Za-z_][A-Za-z0-9_]*)\(")
_SPELLING = re.compile(r"^[A-Za-z][A-Za-z0-9_.:]*$")
_TARGET_NAME = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def canonical_bytes(data: bytes, label: str) -> bytes:
    """Return UTF-8 bytes with CRLF normalized to LF."""

    canonical = data.replace(b"\r\n", b"\n")
    if b"\r" in canonical:
        raise ValueError(f"{label}: bare CR is not canonical")
    try:
        canonical.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ValueError(f"{label}: fixture is not UTF-8") from error
    return canonical


def normalize_nvcc_ptx(data: bytes, label: str) -> bytes:
    """Canonicalize nvcc PTX line endings, trailing whitespace, and final blanks."""

    text = canonical_bytes(data, label).decode("utf-8")
    lines = [line.rstrip(" \t\v\f") for line in text.split("\n")]
    while lines and not lines[-1]:
        lines.pop()
    return ("\n".join(lines) + "\n").encode("utf-8")


def target_directives(source: str) -> list[tuple[str, tuple[str, ...]]]:
    """Return PTX target architectures and comma-separated options in source order."""

    directives = []
    for line in source.splitlines():
        stripped = line.strip()
        if not (
            stripped.startswith(".target")
            and (len(stripped) == 7 or stripped[7].isspace())
        ):
            continue
        fields = [field.strip() for field in stripped[len(".target") :].split(",")]
        if not fields or any(not _TARGET_NAME.fullmatch(field) for field in fields):
            raise ValueError(f"invalid .target directive: {line!r}")
        directives.append((fields[0], tuple(fields[1:])))
    return directives


def target_directive_architectures(source: str) -> list[str]:
    """Return each target directive's architecture operand in source order."""

    return [target for target, _ in target_directives(source)]


def fixture_targets(path: Path) -> list[str]:
    """Return ordered target directives; comma-separated operands are options."""

    targets = target_directive_architectures(path.read_text(encoding="utf-8"))
    if not targets:
        raise ValueError(f"{path}: missing .target directive")
    return targets


def classify_instruction_spelling(spelling: str, database: CodegenDatabase) -> str:
    """Return the opcode-and-modifier catalog blocker for one spelling.

    This intentionally does not inspect operands, target/profile availability, or
    C++ checker behavior.
    """

    for instruction in sorted(database.instructions, key=lambda item: -len(item.opcode)):
        opcode = instruction.opcode
        if spelling != opcode and not spelling.startswith(f"{opcode}."):
            continue
        modifiers = (
            ()
            if spelling == opcode
            else tuple(f".{part}" for part in spelling[len(opcode) + 1 :].split("."))
        )
        if any(
            modifiers in language
            for variant in instruction.variants
            for language in (_variant_modifier_language(opcode, variant),)
        ):
            return "none"
        return "unsupported_variant"
    return "unsupported_opcode"


def extract_natural_ptx(path: Path) -> tuple[str, tuple[str, ...], Counter[str]]:
    """Extract target, entry names, and instruction spellings from nvcc PTX lines.

    This intentionally recognizes only the one-instruction-per-line form emitted by
    nvcc: directives, declarations, labels, braces, comments, and predicates are
    handled structurally rather than attempting to parse PTX operands.
    """

    source = path.read_text(encoding="utf-8")
    directives = target_directives(source)
    if len(directives) != 1 or directives[0][1]:
        raise ValueError(f"{path}: target must name exactly one architecture")
    target = directives[0][0]
    entries: list[str] = []
    spellings: Counter[str] = Counter()
    for raw_line in source.splitlines():
        line = raw_line.partition("//")[0].strip()
        if not line:
            continue
        if line.startswith(".target"):
            continue
        if match := _ENTRY.match(line):
            entries.append(match.group(1))
            continue
        if line.startswith(".") or line in {"(", ")", "{", "}"} or line.endswith(":"):
            continue
        if line.startswith("@"):
            parts = line.split(maxsplit=1)
            if len(parts) != 2:
                raise ValueError(f"{path}: malformed predicated instruction")
            line = parts[1]
        if not line.endswith(";"):
            raise ValueError(f"{path}: unrecognized PTX statement {line!r}")
        spelling = line[:-1].split(None, 1)[0]
        if not _SPELLING.fullmatch(spelling):
            raise ValueError(f"{path}: invalid instruction spelling {spelling!r}")
        spellings[spelling] += 1
    if not entries:
        raise ValueError(f"{path}: missing .visible .entry declaration")
    return target, tuple(entries), spellings


def build_natural_manifest(
    fixtures: dict[str, Path], database: CodegenDatabase, root: Path | None = None
) -> dict[str, object]:
    """Build deterministic profile-local opcode-and-modifier catalog evidence.

    ``first_blocker`` is not full frontend support evidence; the M12 C++ corpus
    tests cover parse, resolve, and checker behavior.
    """

    records: list[dict[str, object]] = []
    for profile, path in sorted(fixtures.items()):
        target, entries, spellings = extract_natural_ptx(path)
        if target != profile:
            raise ValueError(f"{path}: target {target!r} does not match {profile!r}")
        instructions = []
        for spelling, occurrences in sorted(spellings.items()):
            first_blocker = classify_instruction_spelling(spelling, database)
            instructions.append(
                {
                    "spelling": spelling,
                    "occurrences": occurrences,
                    "first_blocker": first_blocker,
                }
            )
        records.append(
            {
                "profile": profile,
                "path": path.relative_to(root).as_posix() if root else path.as_posix(),
                "target": target,
                "entries": list(entries),
                "instructions": instructions,
            }
        )
    return {
        "schema": "ptx_frontend.m12_natural_manifest/v1",
        "source": "corpus/m12/natural_kernel.cu",
        "generator": "nvcc V13.3.33",
        "frequency": "instruction occurrences per target profile",
        "fixtures": records,
    }
