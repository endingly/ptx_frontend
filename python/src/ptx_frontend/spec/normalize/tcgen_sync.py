"""Validate the closed TCGEN commit and specialized-fence source contracts."""

from ptx_frontend.spec.model import (
    AsyncCompletionKind,
    ModifierKind,
    ModifierPresence,
    OperandAccess,
    OperandKind,
    OperandRole,
    OperandTypeExpressionKind,
    SemanticRule,
    VariantSpec,
)


def validate_tcgen_sync_variant(variant: VariantSpec) -> None:
    """Reject malformed synchronization tuples before typed IR is generated."""

    if variant.rule not in {SemanticRule.TENSOR_MEMORY_COMMIT,
                            SemanticRule.TENSOR_MEMORY_FENCE}:
        return
    if len(variant.operand_layouts) != 1:
        raise ValueError("TCGEN synchronization requires one flat operand layout")
    modifiers = {item.name: item for item in variant.modifiers}
    operands = variant.operand_layouts[0].operands

    def fixed_flag(name: str, token: str) -> bool:
        """Check a canonical fixed flag's semantic value and exact token."""
        item = modifiers.get(name)
        return (item is not None and item.kind is ModifierKind.FLAG
                and item.presence is ModifierPresence.FIXED
                and item.value is True and item.token == token)

    def required_flag(name: str, token: str) -> bool:
        """Preserve the written location of a one-token required flag."""
        item = modifiers.get(name)
        return (item is not None and item.kind is ModifierKind.FLAG
                and item.presence is ModifierPresence.REQUIRED
                and item.token is None
                and tuple((value.value, value.token) for value in item.values)
                == ((True, token),))

    if variant.rule is SemanticRule.TENSOR_MEMORY_FENCE:
        if variant.completion_kind is not AsyncCompletionKind.NONE or operands:
            raise ValueError("TCGEN fence has no completion mechanism or operands")
        if set(modifiers) != {"fence_direction"} or not any(
            required_flag("fence_direction", f".fence::{direction}")
            for direction in ("before_thread_sync", "after_thread_sync")
        ):
            raise ValueError("TCGEN fence requires one closed direction")
        return

    if variant.completion_kind is not AsyncCompletionKind.TCGEN_MBARRIER_ARRIVE_ONE:
        raise ValueError("TCGEN commit requires arrive-one completion")
    group = modifiers.get("cta_group")
    if (group is None or group.kind is not ModifierKind.CTA_GROUP
            or group.presence is not ModifierPresence.REQUIRED
            or tuple(value.value for value in group.values)
            not in {("cta_group::1",), ("cta_group::2",)}
            or any(value.token not in {None, f".{value.value}"}
                   for value in group.values)):
        raise ValueError("TCGEN commit requires one written CTA group")
    shared = "shared_cluster" in modifiers
    multicast = "multicast_cluster" in modifiers
    expected = {"commit", "cta_group", "arrive_one", "type"}
    if shared:
        expected.add("shared_cluster")
    if multicast:
        expected.add("multicast_cluster")
    element_type = modifiers.get("type")
    if (set(modifiers) != expected
            or not fixed_flag("commit", ".commit")
            or not fixed_flag("arrive_one", ".mbarrier::arrive::one")
            or (shared and not required_flag("shared_cluster", ".shared::cluster"))
            or (multicast and not required_flag("multicast_cluster", ".multicast::cluster"))
            or element_type is None or element_type.kind is not ModifierKind.TYPE
            or element_type.presence is not ModifierPresence.FIXED
            or element_type.value != "b64"
            or element_type.token not in {None, ".b64"}
            or variant.modifier_order_aliases):
        raise ValueError("TCGEN commit qualifier contract changed")
    if tuple(item.name for item in operands) != (("mbar", "cta_mask") if multicast else ("mbar",)):
        raise ValueError("TCGEN commit mask must match multicast")
    mbar = operands[0]
    if (mbar.kind is not OperandKind.ADDRESS or mbar.role is not OperandRole.BARRIER
            or mbar.access is not OperandAccess.READ
            or tuple(space.value for space in mbar.state_space_values) != ("shared",)
            or len(variant.address_alignments) != 1
            or variant.address_alignments[0].address_operands != ("mbar",)
            or variant.address_alignments[0].alignment != 8):
        raise ValueError("TCGEN commit requires an aligned shared barrier address")
    if multicast:
        mask = operands[1]
        if (mask.kind is not OperandKind.REGISTER or mask.role is not OperandRole.MASK
                or mask.access is not OperandAccess.READ
                or mask.type_expression is None
                or mask.type_expression.kind is not OperandTypeExpressionKind.FIXED_SCALAR
                or mask.type_expression.scalar_type != "u16"):
            raise ValueError("TCGEN multicast requires a scalar u16 register mask")
