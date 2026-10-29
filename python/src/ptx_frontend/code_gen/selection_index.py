"""Derive conservative modifier candidates from normalized syntax metadata."""

from __future__ import annotations

from ptx_frontend.ir.syntax_ast import ModifierPresence, SyntaxInstructionDescriptor


def modifier_candidate_rows(
    descriptor: SyntaxInstructionDescriptor,
) -> tuple[tuple[str, bool, int], ...]:
    """Return sorted (spelling, canonical-known, consuming-form-mask) rows.

    Absent slots contribute only to the legacy known-spelling definition. Alias
    orders contribute candidates independently; no optional combinations expand.
    """
    known = {spelling for variant in descriptor.variants
             for modifier in variant.modifiers for spelling in modifier.allowed_spellings}
    masks: dict[str, int] = dict.fromkeys(known, 0)
    for index, variant in enumerate(descriptor.variants):
        for order in (variant.modifiers, *variant.modifier_order_aliases):
            for modifier in order:
                if modifier.presence is ModifierPresence.ABSENT:
                    continue
                for spelling in modifier.allowed_spellings:
                    masks[spelling] = masks.get(spelling, 0) | (1 << index)
    return tuple((spelling, spelling in known, mask)
                 for spelling, mask in sorted(masks.items()))
