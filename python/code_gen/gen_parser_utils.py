from dataclasses import dataclass
from pathlib import Path

from jinja2 import Environment, FileSystemLoader, StrictUndefined
import re
from code_gen.database import CodegenDatabase


@dataclass(frozen=True)
class DomainValueView:
    spelling: str
    cpp_expr: str


@dataclass(frozen=True)
class DomainView:
    name: str
    cpp_type: str
    parse_func: str
    to_string_func: str
    table_name: str
    values: tuple[DomainValueView, ...]


def to_pascal_case(name: str) -> str:
    return "".join(
        part[:1].upper() + part[1:] for part in re.split(r"[^A-Za-z0-9]+", name) if part
    )


def build_domain_views(
    database: CodegenDatabase,
) -> tuple[DomainView, ...]:
    result: list[DomainView] = []

    for name in sorted(database.domains):
        domain = database.domains[name]
        pascal = to_pascal_case(name)

        result.append(
            DomainView(
                name=name,
                cpp_type=domain.cpp_type,
                parse_func=f"parse{pascal}",
                to_string_func=f"toString{pascal}",
                table_name=f"k{pascal}Entries",
                values=tuple(
                    DomainValueView(
                        spelling=spelling,
                        cpp_expr=cpp_expr,
                    )
                    for spelling, cpp_expr in sorted(domain.values.items())
                ),
            )
        )

    return tuple(result)


def generate_parser_util(
    database: CodegenDatabase,
    *,
    template_dir: Path,
    output_path: Path,
) -> None:
    env = Environment(
        loader=FileSystemLoader(str(template_dir)),
        undefined=StrictUndefined,
        trim_blocks=True,
        lstrip_blocks=True,
    )

    template = env.get_template("ptx_parser_util.gen.hpp.j2")

    content = template.render(
        namespace=database.namespace,
        domains=build_domain_views(database),
    )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(content, encoding="utf-8")
