"""Fixed-template adapters for typed final-form checker models."""
from ptx_frontend.code_gen.rendering import render_template
from ptx_frontend.code_gen.render_models.resolved_checks import (
    named_array_policy, build_check, _build_cross_rule_checks, CrossRuleChecksModel,
)
from ptx_frontend.code_gen.context import GenerationInstruction
from ptx_frontend.ir.resolved_ir import ResolvedInstruction, ResolvedVariant
from ptx_frontend.spec.model import CodegenUnit
def emit_check(entry: GenerationInstruction, variant: ResolvedVariant, variant_index: int, backend: CodegenUnit) -> str:
    """Render the supported checker fragment through a fixed template."""
    return render_template("resolved/checks/method.j2", build_check(entry, variant, variant_index, backend))

def _emit_cross_rule_checks(instruction: ResolvedInstruction, variant: ResolvedVariant, checker_variant_expr: str, *, address_qualifier_expr: str) -> str:
    """Render the supported checker fragment through a fixed template."""
    return render_template("resolved/checks/cross_rules.j2", CrossRuleChecksModel(_build_cross_rule_checks(instruction, variant, checker_variant_expr, address_qualifier_expr=address_qualifier_expr)))
