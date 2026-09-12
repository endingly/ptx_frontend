#pragma once

#include <expected>
#include <optional>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>

namespace ptx_frontend::resolved_ir::detail {

/** Decode an AST immediate without borrowing it beyond the returned value. */
std::expected<ResolvedImmediate, ResolveDiagnostic> resolve_immediate_value(
    const syntax_ast::AstImmediate& immediate, ScalarType type,
    bool require_target_range = false);

/** Resolve a predicate name against an optional declaration context; no borrow escapes. */
std::expected<WithLocs<ResolvedPredicate>, ResolveDiagnostic>
resolve_predicate_identifier(const syntax_ast::AstIdentifierRef& identifier,
                             bool negated, SourceRange range,
                             const ResolveContext* context);

/** Resolve one generated operand binding while borrowing fields and context for this call only. */
std::expected<ResolvedFieldValue, ResolveDiagnostic> resolve_operand_value(
    const check_end::ResolvedFieldDescriptor& field,
    const check_end::ResolvedOperandBindingDescriptor& binding,
    const syntax_ast::AstOperand& operand,
    const ResolvedInstructionFields& fields, const ResolveContext* context);

/** Convert a state-space spelling to its parameter-address qualifier. */
ParameterAddressQualifier parameter_address_qualifier_from_modifier(
    std::string_view spelling) noexcept;

/** Return a descriptor borrowed from instruction; the instruction must outlive use. */
const check_end::SyntaxVariantDescriptor& find_syntax_variant_descriptor(
    const check_end::SyntaxInstructionDescriptor& instruction,
    std::string_view name);

/** Return a variant borrowed from instruction; the instruction must outlive use. */
const check_end::ResolvedVariantDescriptor& find_resolved_variant_descriptor(
    const check_end::ResolvedInstructionDescriptor& instruction,
    std::string_view name);

/** Return a field borrowed from variant; the variant must outlive use. */
const check_end::ResolvedFieldDescriptor& find_resolved_field_descriptor(
    const check_end::ResolvedVariantDescriptor& variant, std::string_view id);

/** Return a field borrowed from layout; the layout must outlive use. */
const check_end::ResolvedFieldDescriptor&
find_resolved_operand_field_descriptor(
    const check_end::ResolvedOperandLayoutDescriptor& layout,
    std::string_view id);

/** Return a syntax slot borrowed from variant; the variant must outlive use. */
const check_end::SyntaxModifierDescriptor& find_syntax_modifier_descriptor(
    const check_end::SyntaxVariantDescriptor& variant, std::string_view id);

/** Supply a validated default for an omitted optional modifier. */
ResolvedFieldValue resolve_default_modifier_value(
    const check_end::ResolvedFieldDescriptor& field,
    const check_end::ResolvedModifierBindingDescriptor& binding);

/** Decode a present modifier into the generated field's resolved value domain. */
std::expected<ResolvedFieldValue, ResolveDiagnostic> resolve_modifier_value(
    const check_end::ResolvedFieldDescriptor& field,
    const syntax_ast::AstModifier& modifier);

/** Parse a declared PTX scalar-type spelling for declaration-bound operands. */
std::optional<ScalarType> scalar_type_from_ptx_name(std::string_view spelling);

}  // namespace ptx_frontend::resolved_ir::detail
