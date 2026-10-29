"""Fixed-template adapters for typed checker operand and modifier views."""
from ptx_frontend.ir.resolved_ir import ResolvedField, ResolvedVariant
from ptx_frontend.spec.model import CodegenUnit
from ptx_frontend.code_gen.rendering import render_template
from ptx_frontend.code_gen.render_models.operand_views import (
    build_check_modifier_view, build_check_modifier_value_view, build_check_operand_view,
    _modifier_default_cpp_value,
)

def emit_check_modifier_view(field: ResolvedField, backend: CodegenUnit, *, form_cpp_name: str, member_expr: str) -> str:
    """Render the typed view using its fixed C++ template."""
    return render_template("resolved/views/fragment.j2", build_check_modifier_view(field, backend, form_cpp_name=form_cpp_name, member_expr=member_expr))

def emit_check_modifier_value_view(variant: ResolvedVariant, field: ResolvedField, backend: CodegenUnit, slot_index: int, *, form_cpp_name: str, member_expr: str) -> str:
    """Render the typed view using its fixed C++ template."""
    return render_template("resolved/views/fragment.j2", build_check_modifier_value_view(variant, field, backend, slot_index, form_cpp_name=form_cpp_name, member_expr=member_expr))

def emit_check_operand_view(field: ResolvedField, member_expr: str, backend: CodegenUnit) -> str:
    """Render the typed view using its fixed C++ template."""
    return render_template("resolved/views/fragment.j2", build_check_operand_view(field, member_expr, backend))
