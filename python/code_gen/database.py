"""PTX instruction-spec database consumed by IR generators."""

from __future__ import annotations

from collections.abc import Iterable
from dataclasses import dataclass
from functools import cache
from pathlib import Path
from typing import Any, TypeVar

from base.utils import file_stem_to_pascal_case
from code_gen.load_yaml import load_yaml
from code_gen.model import InstructionSpec, VariantSpec, modifier_spellings
from code_gen.normalize import normalize_instruction_spec
from jsonschema import Draft202012Validator


PTX_INSTRUCTION_SCHEMA = (
    Path(__file__).resolve().parents[2]
    / "instructions/schemas/ptx-instr-v1.schema.yaml"
)


@dataclass(frozen=True)
class CodegenDatabase:
    """Normalized PTX ISA input shared by syntax and resolved IR generators."""

    spec_schema: str
    instructions: tuple[InstructionSpec, ...]


def discover_spec_files(spec_dir: Path) -> tuple[Path, ...]:
    """Return all PTX instruction specification files in stable order."""

    return tuple(
        sorted(
            path
            for path in spec_dir.rglob("*.yaml")
            if not path.name.endswith(".schema.yaml")
        )
    )


def load_codegen_database(*, spec_dir: Path) -> CodegenDatabase:
    """Load and normalize all PTX ISA specifications below ``spec_dir``."""

    spec_files = discover_spec_files(spec_dir)
    if not spec_files:
        raise ValueError(f"no PTX instruction specs found in {spec_dir}")

    specs = tuple((path, load_yaml(path)) for path in spec_files)
    for path, spec in specs:
        _validate_instruction_schema(path, spec)

    schema_versions = {str(spec["schema"]) for _, spec in specs}
    if len(schema_versions) != 1:
        raise ValueError(f"mixed PTX spec schema versions: {sorted(schema_versions)}")

    definitions = tuple(
        instruction
        for _, spec in specs
        for instruction in normalize_instruction_spec(spec)
    )
    instructions = _merge_instruction_definitions(definitions)
    return CodegenDatabase(
        spec_schema=next(iter(schema_versions)),
        instructions=instructions,
    )


@cache
def _instruction_schema_validator() -> Draft202012Validator:
    """Load the ISA schema once for a database-loading process."""

    return Draft202012Validator(load_yaml(PTX_INSTRUCTION_SCHEMA))


def _validate_instruction_schema(path: Path, spec: dict[str, Any]) -> None:
    """Reject a malformed ISA spec before semantic normalization begins."""

    errors = sorted(
        _instruction_schema_validator().iter_errors(spec),
        key=lambda error: list(error.path),
    )
    if not errors:
        return

    error = errors[0]
    location = ".".join(str(piece) for piece in error.path) or "<root>"
    raise ValueError(f"{path}:{location}: {error.message}")


def _merge_instruction_definitions(
    definitions: tuple[InstructionSpec, ...],
) -> tuple[InstructionSpec, ...]:
    """Merge definitions of the same opcode in stable file order."""

    grouped: dict[str, list[InstructionSpec]] = {}
    for definition in definitions:
        grouped.setdefault(definition.opcode, []).append(definition)

    merged: list[InstructionSpec] = []
    for opcode, opcode_definitions in grouped.items():
        _validate_merge_contract(opcode, opcode_definitions)

        instruction = InstructionSpec(
            opcode=opcode,
            variants=tuple(
                variant
                for definition in opcode_definitions
                for variant in definition.variants
            ),
            syntax_forms=_stable_unique(
                syntax
                for definition in opcode_definitions
                for syntax in definition.syntax_forms
            ),
            source_categories=_stable_unique(
                category
                for definition in opcode_definitions
                for category in definition.source_categories
            ),
            codegen_category=opcode_definitions[0].codegen_category,
        )
        _validate_merged_instruction(instruction)
        merged.append(instruction)

    return tuple(merged)


def _validate_merge_contract(
    opcode: str, definitions: list[InstructionSpec]
) -> None:
    categories = {definition.codegen_category for definition in definitions}
    if len(categories) != 1:
        raise ValueError(
            f"opcode {opcode!r} definitions disagree on codegen_category: "
            f"{sorted(categories)}"
        )


def _validate_merged_instruction(instruction: InstructionSpec) -> None:
    variant_ids = [variant.name for variant in instruction.variants]
    expected_prefix = f"{instruction.opcode}_"
    invalid_ids = [
        name for name in variant_ids if not name.startswith(expected_prefix)
    ]
    if invalid_ids:
        raise ValueError(
            f"opcode {instruction.opcode!r} has variant ids without required "
            f"prefix {expected_prefix!r}: {invalid_ids}"
        )
    duplicate_ids = _duplicates(variant_ids)
    if duplicate_ids:
        raise ValueError(
            f"opcode {instruction.opcode!r} has duplicate variant ids after "
            f"definition merge: {sorted(duplicate_ids)}"
        )

    cpp_names = [
        file_stem_to_pascal_case(
            _variant_name_without_opcode(instruction.opcode, name)
        )
        for name in variant_ids
    ]
    duplicate_cpp_names = _duplicates(cpp_names)
    if duplicate_cpp_names:
        raise ValueError(
            f"opcode {instruction.opcode!r} has variant names that collide in "
            f"C++: {sorted(duplicate_cpp_names)}"
        )

    _validate_variant_modifier_exclusivity(instruction)


def _validate_variant_modifier_exclusivity(instruction: InstructionSpec) -> None:
    languages = [
        _variant_modifier_language(instruction.opcode, variant)
        for variant in instruction.variants
    ]
    for left_index, left in enumerate(instruction.variants):
        for right_index in range(left_index + 1, len(instruction.variants)):
            right = instruction.variants[right_index]
            if languages[left_index] & languages[right_index]:
                raise ValueError(
                    f"opcode {instruction.opcode!r} variants {left.name!r} and "
                    f"{right.name!r} accept an overlapping modifier combination"
                )


def _variant_modifier_language(
    opcode: str, variant: VariantSpec
) -> set[frozenset[str]]:
    """Return every unordered source-modifier set accepted by one variant.

    Modifier slot names are local to the variant. A spelling may belong to a
    differently named slot in another variant, but it must have exactly one
    active owner inside this variant so the C++ matcher can bind greedily and
    uniquely.
    """

    slot_names: set[str] = set()
    owner_by_spelling: dict[str, str] = {}
    language: set[frozenset[str]] = {frozenset()}
    for modifier in variant.modifiers:
        if modifier.name in slot_names:
            raise ValueError(
                f"opcode {opcode!r} variant {variant.name!r} repeats modifier "
                f"slot {modifier.name!r}"
            )
        slot_names.add(modifier.name)

        spellings = set(modifier_spellings(modifier))
        if modifier.presence == "absent":
            choices: set[str | None] = {None}
        elif modifier.presence == "optional":
            if not spellings:
                raise ValueError(
                    f"opcode {opcode!r} variant {variant.name!r} optional "
                    f"modifier {modifier.name!r} has no source spelling"
                )
            choices = {None, *spellings}
        else:
            if not spellings:
                raise ValueError(
                    f"opcode {opcode!r} variant {variant.name!r} active "
                    f"modifier {modifier.name!r} has no source spelling"
                )
            choices = set(spellings)

        if modifier.presence != "absent":
            for spelling in spellings:
                previous = owner_by_spelling.setdefault(spelling, modifier.name)
                if previous != modifier.name:
                    raise ValueError(
                        f"opcode {opcode!r} variant {variant.name!r} maps "
                        f"modifier spelling {spelling!r} to both active slots "
                        f"{previous!r} and {modifier.name!r}"
                    )

        language = {
            combination
            if choice is None
            else combination | frozenset((choice,))
            for combination in language
            for choice in choices
        }

    return language


def _variant_name_without_opcode(opcode: str, variant_name: str) -> str:
    prefix = f"{opcode}_"
    return variant_name.removeprefix(prefix)


T = TypeVar("T")


def _stable_unique(values: Iterable[T]) -> tuple[T, ...]:
    return tuple(dict.fromkeys(values))


def _duplicates(values: list[str]) -> set[str]:
    seen: set[str] = set()
    duplicates: set[str] = set()
    for value in values:
        if value in seen:
            duplicates.add(value)
        seen.add(value)
    return duplicates
