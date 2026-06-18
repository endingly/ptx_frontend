# python/code_gen/gen_parser_registry.py

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from jinja2 import Environment, FileSystemLoader, StrictUndefined

from code_gen.database import CodegenDatabase
from code_gen.naming import (
    category_parser_header_name,
)


@dataclass(frozen=True)
class ParserCandidateView:
    cpp_type: str

    match_type: str
    match_func: str
    parse_func: str

    match_variable: str


@dataclass(frozen=True)
class OpcodeDispatchView:
    opcode: str
    candidates: tuple[ParserCandidateView, ...]


@dataclass(frozen=True)
class ParserRegistryView:
    namespace: str
    parser_headers: tuple[str, ...]
    opcode_groups: tuple[OpcodeDispatchView, ...]


def generate_parser_registry(
    database: CodegenDatabase,
    *,
    template_dir: Path,
    output_dir: Path,
    header_template_name: str = ("ptx_parser_registry.gen.hpp.j2"),
    source_template_name: str = ("ptx_parser_registry.gen.cpp.j2"),
) -> tuple[Path, Path]:
    view = build_parser_registry_view(database)

    env = Environment(
        loader=FileSystemLoader(str(template_dir)),
        undefined=StrictUndefined,
        trim_blocks=True,
        lstrip_blocks=True,
        keep_trailing_newline=True,
    )

    header_template = env.get_template(header_template_name)

    source_template = env.get_template(source_template_name)

    output_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    header_path = output_dir / "ptx_parser_registry.gen.hpp"

    source_path = output_dir / "ptx_parser_registry.gen.cpp"

    header_path.write_text(
        header_template.render(
            namespace=view.namespace,
        ),
        encoding="utf-8",
    )

    source_path.write_text(
        source_template.render(
            namespace=view.namespace,
            parser_headers=view.parser_headers,
            opcode_groups=view.opcode_groups,
        ),
        encoding="utf-8",
    )

    return header_path, source_path


def build_parser_registry_view(
    database: CodegenDatabase,
) -> ParserRegistryView:
    parser_headers: list[str] = []
    seen_headers: set[str] = set()

    for loaded in database.units:
        header = category_parser_header_name(loaded.unit.category)

        if header in seen_headers:
            continue

        seen_headers.add(header)
        parser_headers.append(header)

    opcode_groups: list[OpcodeDispatchView] = []

    for opcode in sorted(database.opcode_groups):
        group = database.opcode_groups[opcode]

        candidates = tuple(
            ParserCandidateView(
                cpp_type=bound.backend.cpp,
                match_type=(f"{bound.backend.cpp}Match"),
                match_func=(f"match{bound.backend.cpp}"),
                parse_func=(f"parse{bound.backend.cpp}"),
                match_variable=(f"candidate_match_{index}"),
            )
            for index, bound in enumerate(group.candidates)
        )

        opcode_groups.append(
            OpcodeDispatchView(
                opcode=opcode,
                candidates=candidates,
            )
        )

    return ParserRegistryView(
        namespace=database.namespace,
        parser_headers=tuple(parser_headers),
        opcode_groups=tuple(opcode_groups),
    )
