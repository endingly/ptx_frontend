"""Line-oriented spelling evidence from frozen nvcc PTX output for M12."""

from __future__ import annotations

from collections import Counter
from pathlib import Path
import re

from code_gen.database import CodegenDatabase, _variant_modifier_language


_ENTRY = re.compile(r"^\.visible\s+\.entry\s+([A-Za-z_][A-Za-z0-9_]*)\(")
_TARGET = re.compile(r"^\.target\s+([A-Za-z_][A-Za-z0-9_]*)$")
_SPELLING = re.compile(r"^[A-Za-z][A-Za-z0-9_.:]*$")


def classify_instruction_spelling(spelling: str, database: CodegenDatabase) -> str:
    """Classify one complete instruction spelling against normalized variants."""

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

    target: str | None = None
    entries: list[str] = []
    spellings: Counter[str] = Counter()
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.partition("//")[0].strip()
        if not line:
            continue
        if line.startswith(".target"):
            if match := _TARGET.match(line):
                if target is not None:
                    raise ValueError(f"{path}: multiple .target directives")
                target = match.group(1)
                continue
            raise ValueError(f"{path}: target must name exactly one architecture")
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
    if target is None:
        raise ValueError(f"{path}: missing .target directive")
    if not entries:
        raise ValueError(f"{path}: missing .visible .entry declaration")
    return target, tuple(entries), spellings


def build_natural_manifest(
    fixtures: dict[str, Path], database: CodegenDatabase
) -> dict[str, object]:
    """Build deterministic profile-local spelling evidence from frozen PTX."""

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
                    "outcome": "supported"
                    if first_blocker == "none"
                    else "unsupported",
                    "first_blocker": first_blocker,
                }
            )
        records.append(
            {
                "profile": profile,
                "path": path.as_posix(),
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
