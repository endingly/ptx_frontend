"""Index an official PTX 9.3 HTML snapshot without asserting conformance.

Run with the retained HTML path, --repo for the checkout, and --output for an
artifact directory. This uses only the standard library; no SDK-generated
descriptor or expected-opcode list supplies the manual-side inventory.
"""

from __future__ import annotations

import argparse
import hashlib
from html.parser import HTMLParser
import json
from pathlib import Path
import re
from urllib.parse import unquote, urlsplit


class ManualIndex(HTMLParser):
    """Retain section identities and code spellings in their own headings."""

    def __init__(self) -> None:
        """Initialize independent section, heading, and document-title state."""
        super().__init__(convert_charrefs=True)
        self.sections: list[dict] = []
        self.stack: list[dict] = []
        self.heading: dict | None = None
        self.code: list[str] | None = None
        self.in_title = False
        self.title: list[str] = []
        self.all_ids: set[str] = set()

    def handle_starttag(self, tag: str, attrs: list[tuple]) -> None:
        """Open sections and capture heading-local code rather than body examples."""
        attributes = dict(attrs)
        if attributes.get("id"):
            self.all_ids.add(attributes["id"])
        if tag == "title":
            self.in_title = True
        if tag == "section":
            row = {
                "anchor": attributes.get("id", ""),
                "parent": self.stack[-1]["anchor"] if self.stack else None,
                "title": "",
                "spellings": [],
            }
            self.sections.append(row)
            self.stack.append(row)
        if re.fullmatch(r"h[1-9]", tag) and self.stack:
            self.heading = {"tag": tag, "row": self.stack[-1], "text": []}
        if tag == "code" and self.heading:
            self.code = []

    def handle_endtag(self, tag: str) -> None:
        """Close headings without attributing nested headings to ancestors."""
        if tag == "title":
            self.in_title = False
        if tag == "code" and self.code is not None and self.heading:
            self.heading["row"]["spellings"].append("".join(self.code).strip())
            self.code = None
        if self.heading and tag == self.heading["tag"]:
            title = " ".join("".join(self.heading["text"]).split())
            self.heading["row"]["title"] = title.rstrip("\uf0c1¶").strip()
            self.heading = None
        if tag == "section" and self.stack:
            self.stack.pop()

    def handle_data(self, data: str) -> None:
        """Collect decoded text only in the currently active capture contexts."""
        if self.in_title:
            self.title.append(data)
        if self.heading:
            self.heading["text"].append(data)
        if self.code is not None:
            self.code.append(data)


def canonical_roots(repo: Path) -> dict[str, list[str]]:
    """Read raw YAML opcode entry locations; this is a structural index only."""
    roots: dict[str, list[str]] = {}
    directory = repo / "python/src/ptx_frontend/spec/resources/ptx_spec"
    for path in sorted(directory.rglob("*.yaml")):
        if path.name.endswith(".schema.yaml"):
            continue
        for line, text in enumerate(path.read_text().splitlines(), 1):
            match = re.match(r"\s*- opcode:\s*[\"']?([^\s\"']+)", text)
            if match:
                roots.setdefault(match[1], []).append(
                    f"{path.relative_to(repo)}:{line}"
                )
    if not roots:
        raise ValueError("No canonical opcode entries found")
    return roots


def main() -> None:
    """Write the manual section denominator and a separate root-presence census."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manual", type=Path)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    raw = args.manual.read_bytes()
    index = ManualIndex()
    index.feed(raw.decode("utf-8"))
    title = "".join(index.title)
    if "PTX ISA 9.3 documentation" not in title:
        raise ValueError(f"Unexpected manual title: {title!r}")
    sections = [row for row in index.sections if row["anchor"]]
    if len({row["anchor"] for row in sections}) != len(sections):
        raise ValueError("Duplicate named section anchors")
    roots = canonical_roots(args.repo)
    census = []
    for row in sections:
        if not row["title"].startswith("9.") or "instruction" not in row["title"].lower():
            continue
        names = [
            name
            for code in row["spellings"]
            for name in re.split(r"[,\s]+", code)
            if re.fullmatch(r"[a-z][a-z0-9_]*(?:\.[a-zA-Z0-9_:]+)*", name)
        ]
        if names:
            base_roots = sorted({name.split(".")[0] for name in names})
            census.append((row, names, base_roots))
    args.output.mkdir(parents=True, exist_ok=True)
    section_lines = ["anchor\tparent\ttitle"] + [
        f"{row['anchor']}\t{row['parent'] or ''}\t{row['title']}" for row in sections
    ]
    (args.output / "manual_sections.tsv").write_text("\n".join(section_lines) + "\n")
    census_lines = ["anchor\ttitle\tspellings\troot_entries"]
    for row, names, base_roots in census:
        entries = [f"{root}={','.join(roots.get(root, ['MISSING']))}" for root in base_roots]
        census_lines.append(
            f"{row['anchor']}\t{row['title']}\t{','.join(names)}\t{';'.join(entries)}"
        )
    (args.output / "instruction_roots.tsv").write_text("\n".join(census_lines) + "\n")
    link_rows = []
    for line, text in enumerate((args.repo / "docs/sdk_support.md").read_text().splitlines(), 1):
        for url in re.findall(
            r"https://docs\.nvidia\.com/cuda/archive/13\.3\.0/parallel-thread-execution/[^\s)>\]]*",
            text,
        ):
            anchor = unquote(urlsplit(url).fragment)
            if anchor:
                link_rows.append((line, anchor, anchor in index.all_ids))
    invalid_links = [row for row in link_rows if not row[2]]
    link_lines = ["sdk_line\tmissing_manual_anchor"] + [
        f"{line}\t{anchor}" for line, anchor, _ in invalid_links
    ]
    (args.output / "missing_manual_links.tsv").write_text("\n".join(link_lines) + "\n")
    manual_roots = {root for _, _, names in census for root in names}
    metadata = {
        "manual_url": "https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html",
        "document_title": title,
        "html_sha256": hashlib.sha256(raw).hexdigest(),
        "html_bytes": len(raw),
        "named_sections": len(sections),
        "instruction_heading_units": len(census),
        "instruction_heading_roots": len(manual_roots),
        "canonical_roots": len(roots),
        "missing_canonical_roots": sorted(manual_roots - roots.keys()),
        "canonical_roots_without_instruction_heading": sorted(roots.keys() - manual_roots),
        "sdk_archived_fragment_link_occurrences": len(link_rows),
        "sdk_missing_fragment_occurrences": len(invalid_links),
        "sdk_distinct_missing_fragments": len({row[1] for row in invalid_links}),
        "limitation": "Heading/root presence only; no production, form, operand, modifier, type, target, or semantic conformance verdict.",
    }
    (args.output / "inventory.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    main()
