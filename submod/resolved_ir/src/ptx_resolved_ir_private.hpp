#pragma once

#include <expected>
#include <optional>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>

namespace ptx_frontend::resolved_ir::detail {

/** Validate the explicit integer alternative independently of the actual source. */
inline bool valid_integer_immediate_type_policy(
    const checker::OperandDescriptor& binding) {
  if (!binding.integer_immediate_type)
    return true;
  return binding.type_expression.kind ==
             checker::OperandTypeExpressionKind::FixedScalar &&
         ((*binding.integer_immediate_type == ScalarType::B16 &&
           (binding.type_expression.fixed_scalar_type == ScalarType::F16 ||
            binding.type_expression.fixed_scalar_type == ScalarType::BF16)) ||
          (*binding.integer_immediate_type == ScalarType::B32 &&
           (binding.type_expression.fixed_scalar_type == ScalarType::F16x2 ||
            binding.type_expression.fixed_scalar_type ==
                ScalarType::BF16x2))) &&
         binding.role == checker::OperandRole::Source &&
         binding.access == checker::OperandAccess::Read &&
         binding.allowed_shapes == (checker::OperandShape::Register |
                                    checker::OperandShape::Immediate) &&
         binding.minimum_elements == 0 && binding.maximum_elements == 0 &&
         !binding.source_value_vector;
}

/** Resolve a real ordinary register owner before any call ABI projection. */
std::expected<WithLocs<ResolvedRegisterRef>, ResolveDiagnostic>
resolve_register(const syntax_ast::AstOperand& operand,
                 const ResolveContext* context);

/** Require retained integer result provenance before an integer-only coercion. */
std::expected<void, ResolveDiagnostic> require_integer_source(
    const ResolvedImmediate& value, SourceRange range);

/** Decode one literal without borrowing syntax or reclassifying direct float roots.
 * The integer alternative is prevalidated by its binding; unrelated callers omit it.
 */
std::expected<ResolvedImmediate, ResolveDiagnostic> resolve_immediate_value(
    const syntax_ast::AstImmediate& immediate, ScalarType type,
    bool require_target_range = false,
    std::optional<ScalarType> integer_immediate_type = std::nullopt);

/** Convert a once-evaluated category under the existing width/range policy.
 * A prevalidated integer alternative selects only IntegerConstantValue results.
 */
std::expected<ResolvedImmediate, ResolveDiagnostic> resolve_numeric_value(
    const declaration_semantics::NumericConstantValue& value, SourceRange range,
    ScalarType type, bool require_target_range = false,
    std::optional<ScalarType> integer_immediate_type = std::nullopt);

/** Evaluate an owned source expression, then apply the operand conversion. */
std::expected<ResolvedImmediate, ResolveDiagnostic> resolve_immediate_value(
    const syntax_ast::AstConstantOperand& operand, ScalarType type,
    bool require_target_range = false,
    std::optional<ScalarType> integer_immediate_type = std::nullopt);

/** Resolve either literal or expression numeric syntax. */
std::expected<ResolvedImmediate, ResolveDiagnostic> resolve_immediate_value(
    const std::variant<syntax_ast::AstImmediate,
                       syntax_ast::AstConstantOperand>& operand,
    ScalarType type, bool require_target_range = false);

/** Decode a numeric tuple/address leaf; identifiers remain invalid here. */
std::expected<ResolvedImmediate, ResolveDiagnostic> resolve_immediate_value(
    const syntax_ast::AstVectorElement& operand, ScalarType type,
    bool require_target_range = false);

/** Return a numeric source shape, including owned constant expressions. */
bool is_numeric_operand(const syntax_ast::AstOperand& operand);

/** Resolve numeric source syntax held in the shared operand variant. */
std::expected<ResolvedImmediate, ResolveDiagnostic> resolve_numeric_operand(
    const syntax_ast::AstOperand& operand, ScalarType type,
    bool require_target_range = false,
    std::optional<ScalarType> integer_immediate_type = std::nullopt);

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
    const ResolvedInstructionFields& fields, const ResolveContext* context,
    NamedArrayAddressPolicy named_array_policy =
        NamedArrayAddressPolicy::Reject);

/** Merge tex compatibility source slots under one generated resource contract. */
std::expected<ResolvedFieldValue, ResolveDiagnostic>
resolve_unbracketed_texture_access(
    const syntax_ast::AstInstruction& instruction, size_t source_index,
    size_t source_span,
    const check_end::ResolvedOperandBindingDescriptor& binding,
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
