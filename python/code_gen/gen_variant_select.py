from dataclasses import dataclass
import model
from naming import file_stem_to_pascal_case


@dataclass
class VariantSelectInfoItem:
    """
    Information about a single variant selection item.
    """

    target_variant_name: str
    allowed_modifiers: set[str]


def gen_select_map(instructions: model.InstructionSpec) -> list[VariantSelectInfoItem]:
    """Generate a list of VariantSelectInfoItem for each instruction variant."""
    select_map: list[VariantSelectInfoItem] = []
    for variant in instructions.variants:
        allowed_modifiers = {modifier.name for modifier in variant.modifiers}
        select_map.append(
            VariantSelectInfoItem(
                target_variant_name=file_stem_to_pascal_case(variant.name),
                allowed_modifiers=allowed_modifiers,
            )
        )
    return select_map
