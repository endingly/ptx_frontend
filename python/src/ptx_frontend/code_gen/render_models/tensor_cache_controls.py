"""Select exact Cp cache-control dispatch facts for a fixed C++ query."""

from dataclasses import dataclass

from ptx_frontend.code_gen.context import GenerationContext


@dataclass(frozen=True)
class HintedCacheModel:
    """An owned instruction class and its complete partition of layout tags."""

    name: str
    policy_layouts: tuple[int, ...]
    other_layouts: tuple[int, ...]


@dataclass(frozen=True)
class TensorCacheControlsModel:
    """Stable selected-form dispatch order for hinted and plain tensor forms."""

    hinted: tuple[HintedCacheModel, ...]
    plain: tuple[str, ...]


def build_tensor_cache_controls(context: GenerationContext) -> TensorCacheControlsModel:
    """Validate policy layout coverage and select dispatch cases in form order."""

    cp = next(entry for entry in context.entries
              if entry.specification.codegen_category == "data_movement"
              and entry.specification.opcode == "cp")
    plain = []
    hinted = []
    for variant in cp.resolved.variants:
        if variant.tensor_access_mode is None:
            continue
        name = cp.cpp_name + variant.cpp_name
        if not any(field.name == "cache_hint" for field in variant.modifier_fields):
            plain.append(name)
            continue
        policy_layouts = tuple(index for index, layout in enumerate(variant.operand_layouts)
                               if any(field.name == "cache_policy" for field in layout.fields))
        if not policy_layouts or len(policy_layouts) * 2 != len(variant.operand_layouts):
            raise ValueError(f"{variant.variant_id}: incomplete tensor policy layouts")
        hinted.append(HintedCacheModel(
            name, policy_layouts, tuple(index for index in range(len(variant.operand_layouts))
                                        if index not in policy_layouts),
        ))
    return TensorCacheControlsModel(tuple(hinted), tuple(plain))
