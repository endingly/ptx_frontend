"""Public loaders for normalized PTX instruction specifications."""

from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from functools import cache
from typing import Any, TypeVar

from ptx_frontend.base.utils import file_stem_to_pascal_case
from .load_yaml import load_yaml
from .model import (
    InstructionSpec,
    ModifierPresence,
    ModifierSpec,
    VariantSpec,
    modifier_spellings,
)
from .normalize import normalize_instruction_spec
from jsonschema import Draft202012Validator
from importlib.resources.abc import Traversable
from .resources import packaged_spec_dir, packaged_spec_schema

PTX_INSTRUCTION_SCHEMA = packaged_spec_schema()


@dataclass(frozen=True)
class CodegenDatabase:
    """Normalized PTX ISA input shared by syntax and resolved IR generators."""

    spec_schema: str
    instructions: tuple[InstructionSpec, ...]


@dataclass(frozen=True)
class CodegenCategoryInputs:
    """Specification files contributing instructions to one codegen category."""

    category: str
    spec_files: tuple[Traversable, ...]


@dataclass(frozen=True)
class _NormalizedSpecFile:
    """One validated source specification and its normalized instructions."""

    path: Traversable
    schema: str
    instructions: tuple[InstructionSpec, ...]


def discover_spec_files(
    spec_dir: Traversable,
) -> tuple[Traversable, ...]:
    """Return all PTX instruction specification files in stable order."""

    def _walk_spec_files(root: Traversable) -> Iterator[Traversable]:
        """Yield spec YAML files below ``root`` without relying on ``rglob``."""

        for entry in sorted(root.iterdir(), key=lambda item: item.name):
            if entry.is_dir():
                yield from _walk_spec_files(entry)
            elif entry.name.endswith(".yaml") and not entry.name.endswith(
                ".schema.yaml"
            ):
                yield entry

    return tuple(_walk_spec_files(spec_dir))


def load_codegen_database(*, spec_dir: Traversable) -> CodegenDatabase:
    """Load and normalize all PTX ISA specifications below ``spec_dir``."""

    spec_files = discover_spec_files(spec_dir)
    if not spec_files:
        raise ValueError(f"no PTX instruction specs found in {spec_dir}")

    return load_codegen_database_from_files(
        spec_files=spec_files,
    )


@cache
def _instruction_schema_validator() -> Draft202012Validator:
    """Load the ISA schema once for a database-loading process."""

    return Draft202012Validator(load_yaml(PTX_INSTRUCTION_SCHEMA))


def _validate_instruction_schema(path: Traversable, spec: dict[str, Any]) -> None:
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


def _validate_merge_contract(opcode: str, definitions: list[InstructionSpec]) -> None:
    categories = {definition.codegen_category for definition in definitions}
    if len(categories) != 1:
        raise ValueError(
            f"opcode {opcode!r} definitions disagree on codegen_category: "
            f"{sorted(categories)}"
        )


def _validate_merged_instruction(instruction: InstructionSpec) -> None:
    variant_ids = [variant.name for variant in instruction.variants]
    expected_prefix = f"{instruction.opcode}_"
    invalid_ids = [name for name in variant_ids if not name.startswith(expected_prefix)]
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
        file_stem_to_pascal_case(_variant_name_without_opcode(instruction.opcode, name))
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
) -> set[tuple[str, ...]]:
    """Return the union of canonical and declared alias modifier languages."""

    slot_names: set[str] = set()
    owners_by_spelling: dict[str, list[tuple[str, ModifierPresence]]] = {}
    for modifier in variant.modifiers:
        if modifier.name in slot_names:
            raise ValueError(
                f"opcode {opcode!r} variant {variant.name!r} repeats modifier "
                f"slot {modifier.name!r}"
            )
        slot_names.add(modifier.name)

        spellings = set(modifier_spellings(modifier))
        if modifier.presence is ModifierPresence.OPTIONAL:
            if not spellings:
                raise ValueError(
                    f"opcode {opcode!r} variant {variant.name!r} optional "
                    f"modifier {modifier.name!r} has no source spelling"
                )
        elif modifier.presence is not ModifierPresence.ABSENT:
            if not spellings:
                raise ValueError(
                    f"opcode {opcode!r} variant {variant.name!r} active "
                    f"modifier {modifier.name!r} has no source spelling"
                )

        if modifier.presence is not ModifierPresence.ABSENT:
            for spelling in spellings:
                owners = owners_by_spelling.setdefault(spelling, [])
                if owners and (
                    modifier.presence is ModifierPresence.OPTIONAL
                    or any(
                        presence is ModifierPresence.OPTIONAL for _, presence in owners
                    )
                ):
                    raise ValueError(
                        f"opcode {opcode!r} variant {variant.name!r} maps "
                        f"modifier spelling {spelling!r} to an optional slot; "
                        "repeated spellings require only required/fixed slots"
                    )
                owners.append((modifier.name, modifier.presence))

    modifiers_by_name = {modifier.name: modifier for modifier in variant.modifiers}
    bindings: dict[tuple[str, ...], tuple[str, ...]] = {}
    orders = (
        tuple(modifier.name for modifier in variant.modifiers),
        *variant.modifier_order_aliases,
    )
    language: set[tuple[str, ...]] = set()
    # Layouts are arity-selected alternatives rather than competing variants, so
    # the variant accepts the union of their languages and each layout
    # contributes only the spellings it permits.
    for forbidden in _layout_forbidden_sets(variant):
        for order in orders:
            order_language = _modifier_order_language(
                tuple(modifiers_by_name[slot_name] for slot_name in order),
                forbidden,
            )
            for sequence, binding in order_language.items():
                previous_binding = bindings.setdefault(sequence, binding)
                if previous_binding != binding:
                    raise ValueError(
                        f"opcode {opcode!r} variant {variant.name!r} modifier "
                        f"order aliases bind {sequence!r} to different slot identities"
                    )
            language.update(order_language)
    return language


def _layout_forbidden_sets(variant: VariantSpec) -> set[frozenset[str]]:
    """Return the distinct forbidden-slot sets across a variant's layouts."""

    if not variant.operand_layouts:
        return {frozenset()}
    return {frozenset(layout.forbidden_modifiers) for layout in variant.operand_layouts}


def _modifier_order_language(
    modifiers: tuple[ModifierSpec, ...],
    forbidden: frozenset[str] = frozenset(),
) -> dict[tuple[str, ...], tuple[str, ...]]:
    """Return source sequences and their slot bindings for one complete order."""

    language: dict[tuple[str, ...], tuple[str, ...]] = {(): ()}
    for modifier in modifiers:
        spellings = set(modifier_spellings(modifier))
        if modifier.name in forbidden or modifier.presence is ModifierPresence.ABSENT:
            choices: set[str | None] = {None}
        elif modifier.presence is ModifierPresence.OPTIONAL:
            choices = {None, *spellings}
        else:
            choices = set(spellings)

        language = {
            sequence if choice is None else (*sequence, choice): (
                binding if choice is None else (*binding, modifier.name)
            )
            for (sequence, binding) in language.items()
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


def _load_normalized_spec_files(
    spec_files: Iterable[Traversable],
) -> tuple[_NormalizedSpecFile, ...]:
    """Load, validate, and normalize an explicit stable set of spec files."""

    paths = tuple(spec_files)
    if not paths:
        raise ValueError("no PTX instruction specs supplied")

    records: list[_NormalizedSpecFile] = []

    for path in paths:
        spec = load_yaml(path)
        _validate_instruction_schema(path, spec)

        records.append(
            _NormalizedSpecFile(
                path=path,
                schema=str(spec["schema"]),
                instructions=tuple(normalize_instruction_spec(spec)),
            )
        )

    schema_versions = {record.schema for record in records}
    if len(schema_versions) != 1:
        raise ValueError(f"mixed PTX spec schema versions: {sorted(schema_versions)}")

    return tuple(records)


def load_codegen_database_from_files(
    *,
    spec_files: Iterable[Traversable],
    category: str | None = None,
) -> CodegenDatabase:
    """Load a codegen database from explicit specification files."""

    records = _load_normalized_spec_files(spec_files)

    definitions = tuple(
        instruction
        for record in records
        for instruction in record.instructions
        if category is None or instruction.codegen_category == category
    )

    if not definitions:
        if category is None:
            raise ValueError("no PTX instruction definitions found")
        raise ValueError(
            f"no PTX instruction definitions found for category {category!r}"
        )

    # IMPORTANT:
    # Never return `definitions` directly. Besides merging split opcode
    # definitions, this also runs the merged-instruction semantic validation
    # such as variant/modifier-language exclusivity.
    instructions = _merge_instruction_definitions(definitions)

    return CodegenDatabase(
        spec_schema=records[0].schema,
        instructions=instructions,
    )


def discover_codegen_category_inputs(
    *,
    spec_dir: Traversable,
) -> tuple[CodegenCategoryInputs, ...]:
    """Return the spec files contributing to each codegen category."""

    spec_files = discover_spec_files(spec_dir)
    if not spec_files:
        raise ValueError(f"no PTX instruction specs found in {spec_dir}")

    records = _load_normalized_spec_files(spec_files)

    files_by_category: dict[str, list[Traversable]] = {}

    for record in records:
        categories = {
            instruction.codegen_category for instruction in record.instructions
        }

        for category in categories:
            files_by_category.setdefault(category, []).append(record.path)

    return tuple(
        CodegenCategoryInputs(
            category=category,
            spec_files=tuple(files_by_category[category]),
        )
        for category in sorted(files_by_category)
    )


def load_spec_database(*, spec_dir: Traversable) -> CodegenDatabase:
    """Load and normalize PTX instruction specs from ``spec_dir``."""

    return load_codegen_database(spec_dir=spec_dir)


def load_packaged_spec_database() -> CodegenDatabase:
    """Load the PTX instruction specs shipped with the installed wheel."""

    return load_spec_database(spec_dir=packaged_spec_dir())


__all__ = [
    "discover_spec_files",
    "load_packaged_spec_database",
    "load_spec_database",
    "CodegenDatabase",
    "load_codegen_database",
    "CodegenCategoryInputs",
    "discover_codegen_category_inputs",
    "load_codegen_database_from_files",
]
