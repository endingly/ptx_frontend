#!/usr/bin/env python3
"""Require full commit pins for remote actions in local CI definitions."""

from pathlib import Path
import re
import sys


USES_PATTERN = re.compile(r"^\s*(?:-\s+)?uses:\s*([^\s#]+)")
FULL_SHA_PATTERN = re.compile(r"@[0-9a-f]{40}$")


def floating_action_references(root: Path) -> list[str]:
    """Return remote action references beneath *root* that lack a full SHA pin."""
    invalid: list[str] = []
    for relative_directory in (Path(".github/workflows"), Path(".github/actions")):
        directory = root / relative_directory
        if not directory.is_dir():
            raise FileNotFoundError(f"Missing action directory: {directory}")
        for source in sorted(directory.rglob("*")):
            if not source.is_file() or source.suffix not in (".yml", ".yaml"):
                continue
            for line_number, line in enumerate(source.read_text(encoding="utf-8").splitlines(), start=1):
                match = USES_PATTERN.match(line)
                if match is None:
                    continue
                reference = match.group(1)
                if reference.startswith(("./", "docker://")):
                    continue
                if FULL_SHA_PATTERN.search(reference) is None:
                    invalid.append(f"{source}:{line_number}: {reference}")
    return invalid


def main() -> int:
    """Check this repository and report every floating third-party action reference."""
    invalid = floating_action_references(Path("."))
    if invalid:
        print("Floating third-party action reference(s):", file=sys.stderr)
        print("\n".join(invalid), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, UnicodeError) as error:
        sys.exit(f"Cannot check action pins: {error}")
