from dataclasses import dataclass
from pathlib import Path
import re

from jinja2 import Environment, FileSystemLoader, StrictUndefined

from code_gen.database import CodegenDatabase

# -----------------------------------------------------------------------------
# Template-facing view model
# -----------------------------------------------------------------------------


@dataclass(frozen=True)
class IrRegistryInstructionView:
    """
    One instruction alternative in the generated PtxInstruction std::variant.

    Example:

        opcode: add
        cpp_type: InstrIntegerAdd
        category: integer_arithmetic
    """

    opcode: str
    cpp_type: str
    category: str


@dataclass(frozen=True)
class IrRegistryHeaderView:
    namespace: str

    std_includes: tuple[str, ...]
    project_includes: tuple[str, ...]

    instructions: tuple[IrRegistryInstructionView, ...]


# -----------------------------------------------------------------------------
# Public API
# -----------------------------------------------------------------------------


def generate_ir_registry_header(
    database: CodegenDatabase,
    *,
    template_dir: Path,
    output_path: Path,
    template_name: str = "ptx_ir_registry.gen.hpp.j2",
) -> None:
    """
    Generate the global IR registry header.

    The output contains:

        template <OperandLike Operand>
        using PtxInstruction = std::variant<...>;

        using ParsedInstruction = PtxInstruction<ParsedOp>;
    """

    view = build_ir_registry_header_view(database)

    content = render_ir_registry_header(
        view=view,
        template_dir=template_dir,
        template_name=template_name,
    )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(content, encoding="utf-8")


def render_ir_registry_header(
    *,
    view: IrRegistryHeaderView,
    template_dir: Path,
    template_name: str,
) -> str:
    env = Environment(
        loader=FileSystemLoader(str(template_dir)),
        undefined=StrictUndefined,
        trim_blocks=True,
        lstrip_blocks=True,
        keep_trailing_newline=True,
    )

    template = env.get_template(template_name)

    return template.render(
        namespace=view.namespace,
        std_includes=view.std_includes,
        project_includes=view.project_includes,
        instructions=view.instructions,
    )


def build_ir_registry_header_view(
    database: CodegenDatabase,
) -> IrRegistryHeaderView:
    if not database.instructions:
        raise ValueError(
            "cannot generate ptx_ir_registry.gen.hpp: "
            "the codegen database contains no instructions"
        )

    validate_unique_categories(database)
    validate_unique_cpp_instruction_types(database)

    instructions = tuple(
        IrRegistryInstructionView(
            opcode=bound.spec.opcode,
            cpp_type=bound.backend.cpp,
            category=bound.category,
        )
        for bound in database.instructions
    )

    category_headers = build_category_ir_headers(database)

    return IrRegistryHeaderView(
        namespace=database.namespace,
        std_includes=(
            "cstddef",
            "variant",
        ),
        project_includes=(
            "ptx_ir/base.hpp",
            *category_headers,
        ),
        instructions=instructions,
    )


# -----------------------------------------------------------------------------
# Generated filename helpers
# -----------------------------------------------------------------------------


def category_ir_header_name(category: str) -> str:
    """
    Convert a spec category to its generated IR header name.

    Examples:

        integer_arithmetic
            -> ptx_ir_integer_arithmetic.gen.hpp

        Floating Point
            -> ptx_ir_floating_point.gen.hpp

        data-movement
            -> ptx_ir_data_movement.gen.hpp

    gen_all.py must use this same function when generating category IR files.
    """

    stem = to_file_stem(category)

    if not stem:
        raise ValueError(
            f"cannot derive generated IR filename from category {category!r}"
        )

    return f"ptx_ir_{stem}.gen.hpp"


def to_file_stem(value: str) -> str:
    """
    Convert an arbitrary category name to a deterministic snake-case-like
    filename component.
    """

    stem = re.sub(r"[^A-Za-z0-9]+", "_", value)
    stem = re.sub(r"_+", "_", stem)
    return stem.strip("_").lower()


# -----------------------------------------------------------------------------
# View helpers
# -----------------------------------------------------------------------------


def build_category_ir_headers(
    database: CodegenDatabase,
) -> tuple[str, ...]:
    """
    Return category IR headers in database unit order.

    The order is deterministic because discover_codegen_inputs() sorts its
    input paths before building CodegenDatabase.
    """

    headers: list[str] = []
    seen: set[str] = set()

    for loaded in database.units:
        header = category_ir_header_name(loaded.unit.category)

        if header in seen:
            continue

        seen.add(header)
        headers.append(header)

    return tuple(headers)


# -----------------------------------------------------------------------------
# Validation
# -----------------------------------------------------------------------------


def validate_unique_categories(
    database: CodegenDatabase,
) -> None:
    """
    The current generation layout emits one IR header per category.

    Therefore, two CodegenUnits may not currently use the same category,
    because both would generate the same output filename.

    This restriction can later be relaxed by introducing CategoryGroup.
    """

    owners: dict[str, Path] = {}

    for loaded in database.units:
        category = loaded.unit.category
        previous = owners.get(category)

        if previous is not None:
            raise ValueError(
                f"category {category!r} is defined by multiple codegen units: "
                f"{previous} and {loaded.source.spec_path}; "
                "the current generator emits one IR header per category"
            )

        owners[category] = loaded.source.spec_path


def validate_unique_cpp_instruction_types(
    database: CodegenDatabase,
) -> None:
    """
    std::variant may technically contain duplicate types, but that would make
    std::get<T>/std::holds_alternative<T> unusable and indicates a backend
    configuration error.

    The global C++ instruction type must therefore be unique.
    """

    owners: dict[str, tuple[str, Path]] = {}

    for bound in database.instructions:
        cpp_type = bound.backend.cpp
        previous = owners.get(cpp_type)

        if previous is not None:
            previous_opcode, previous_path = previous

            raise ValueError(
                f"generated instruction type {cpp_type!r} is used by both "
                f"opcode {previous_opcode!r} in {previous_path} and "
                f"opcode {bound.spec.opcode!r} in "
                f"{bound.source.backend_path}"
            )

        owners[cpp_type] = (
            bound.spec.opcode,
            bound.source.backend_path,
        )
