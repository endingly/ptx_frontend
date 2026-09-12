#pragma once

#include <concepts>
#include <expected>
#include <type_traits>
#include <unordered_map>

#include <fmt/core.h>
#include <magic_enum/magic_enum.hpp>

#include <ptx_frontend/common/utils.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_descriptors.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

namespace ptx_frontend::resolved_ir::check_end {
/** Classify one complete syntax-AST operand for resolution-only matching. */
OperandSyntaxShape get_operand_syntax_shape(
    const syntax_ast::AstOperand& operand);
}  // namespace ptx_frontend::resolved_ir::check_end

namespace ptx_frontend::resolved_ir {

/** Resolver-only variant payload table retained until generated construction. */
using ResolvedFieldValue = std::variant<
    WithLocs<bool>, WithLocs<ScalarType>, WithLocs<RoundingMode>,
    WithLocs<ComparisonOperator>, WithLocs<BooleanOperator>,
    WithLocs<CacheOperator>, WithLocs<EvictionPriority>,
    WithLocs<MemoryConsistency>, WithLocs<MemoryScope>, WithLocs<VectorArity>,
    WithLocs<MemoryStateSpace>, WithLocs<MbarrierPhaseType>,
    WithLocs<MbarrierLayout>, WithLocs<AsyncProxyKind>, WithLocs<ProxyKindPair>,
    WithLocs<ResolvedRegisterRef>, WithLocs<ResolvedMbarrierStateToken>,
    WithLocs<ResolvedRegisterOrSink>, WithLocs<ResolvedImmediate>,
    WithLocs<RegOrImm>, WithLocs<ResolvedShflSyncDestination>,
    WithLocs<ResolvedPredicatePair>, WithLocs<ResolvedMovSource>,
    WithLocs<ResolvedPredicate>, WithLocs<ResolvedBranchTarget>,
    WithLocs<ResolvedBranchTargetSet>, WithLocs<ResolvedSpecialRegisterRef>,
    WithLocs<ResolvedPredicateSource>, WithLocs<ResolvedVectorRegisterRef>,
    WithLocs<ResolvedVectorSpecialRegisterRef>, WithLocs<ResolvedSymbolRef>,
    WithLocs<ResolvedAddress>, WithLocs<ResolvedRegisterVector>,
    WithLocs<ResolvedTensorCoordinate>, WithLocs<ResolvedFunctionRef>,
    WithLocs<ResolvedIndirectCallee>, WithLocs<ResolvedCallParameterRef>,
    WithLocs<ResolvedCallArguments>>;
/** Resolver-only lookup table keyed by generated field IDs. */
using ResolvedFieldMap = std::unordered_map<std::string, ResolvedFieldValue>;
/** Scratch fields consumed once when generated variants are constructed. */
struct ResolvedInstructionFields {
  std::string_view variant_name;
  ResolvedOperandLayoutTag operand_layout;
  std::optional<WithLocs<ResolvedPredicate>> execution_predicate;
  ResolvedFieldMap modifiers;
  ResolvedFieldMap operands;
};

/** Restrict helper lookups to alternatives held by the scratch field variant. */
template <typename T>
concept ResolvedFieldType = requires {
  typename WithLocs<T>;
} && []<typename... Alternatives>(std::variant<Alternatives...>*) {
  return (std::same_as<WithLocs<T>, Alternatives> || ...);
}(static_cast<ResolvedFieldValue*>(nullptr));

using ActualModifierTable =
    std::unordered_map<std::string, const syntax_ast::AstModifier*>;
std::expected<ActualModifierTable, ResolveDiagnostic> collect_actual_modifiers(
    const syntax_ast::AstInstruction& ast,
    const check_end::SyntaxVariantDescriptor& variant);
std::expected<std::string_view, ResolveDiagnostic> select_variant_name(
    const syntax_ast::AstInstruction& ast,
    const check_end::SyntaxInstructionDescriptor& instruction);
std::expected<ResolvedImmediate, ResolveDiagnostic> resolve_immediate_literal(
    const syntax_ast::AstImmediate& immediate, ScalarType type);
std::expected<WithLocs<ResolvedImmediate>, ResolveDiagnostic>
resolve_call_literal(
    const ResolvedCallLiteral& literal, SourceRange range,
    const declaration_semantics::FunctionParameterContract& formal);
std::expected<ResolvedInstructionFields, ResolveDiagnostic> resolve_fields(
    const syntax_ast::AstInstruction& ast,
    const check_end::SyntaxInstructionDescriptor& syntax_instruction,
    const check_end::ResolvedInstructionDescriptor& resolved_instruction,
    std::string_view variant_name, const ResolveContext* context = nullptr);
checker::AvailabilityDescriptor special_register_availability(
    const base::Info& info);

}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::detail {

/** AST modifier pointers indexed by descriptor slot ID during one resolution. */
using ::ptx_frontend::resolved_ir::ActualModifierTable;
using ::ptx_frontend::resolved_ir::collect_actual_modifiers;
using ::ptx_frontend::resolved_ir::resolve_call_literal;
using ::ptx_frontend::resolved_ir::resolve_fields;
using ::ptx_frontend::resolved_ir::resolve_immediate_literal;
using ::ptx_frontend::resolved_ir::select_variant_name;
using ::ptx_frontend::resolved_ir::special_register_availability;

/** Select the generated variant named by syntax descriptor matching. */
template <::ptx_frontend::resolved_ir::PtxOperator T>
std::expected<typename T::VariantType, ResolveDiagnostic> selectVariant(
    const syntax_ast::AstInstruction& ast) {
  const auto variant_name =
      select_variant_name(ast, T::get_syntax_descriptor());
  if (!variant_name)
    return std::unexpected(variant_name.error());
  const auto variant =
      magic_enum::enum_cast<typename T::VariantType>(*variant_name);
  if (!variant) {
    throw ResolveException(fmt::format(
        "Descriptor variant '{}.{}' has no matching VariantType enumerator.",
        utils::type_name<T>(), *variant_name));
  }
  return *variant;
}

/** Retrieve a typed modifier or report an inconsistent generated contract. */
template <ResolvedFieldType T>
const WithLocs<T>& resolved_modifier(const ResolvedInstructionFields& fields,
                                     std::string_view kind_id) {
  const auto it = fields.modifiers.find(std::string(kind_id));
  if (it == fields.modifiers.end()) {
    throw ResolveException(
        fmt::format("Resolved modifier field '{}' is unavailable.", kind_id));
  }
  if (const auto* value = std::get_if<WithLocs<T>>(&it->second))
    return *value;
  throw ResolveException(fmt::format(
      "Resolved modifier field '{}' has an unexpected value type.", kind_id));
}

/** Retrieve a typed operand or report an inconsistent generated contract. */
template <ResolvedFieldType T>
const WithLocs<T>& resolved_operand(const ResolvedInstructionFields& fields,
                                    std::string_view field_id) {
  const auto it = fields.operands.find(std::string(field_id));
  if (it == fields.operands.end()) {
    throw ResolveException(
        fmt::format("Resolved operand field '{}' is unavailable.", field_id));
  }
  if (const auto* value = std::get_if<WithLocs<T>>(&it->second))
    return *value;
  throw ResolveException(fmt::format(
      "Resolved operand field '{}' has an unexpected value type.", field_id));
}

}  // namespace ptx_frontend::resolved_ir::detail

namespace ptx_frontend::resolved_ir {

// Compatibility aliases preserve existing aggregate-header entry points.
using detail::resolved_modifier;
using detail::resolved_operand;
using detail::selectVariant;

}  // namespace ptx_frontend::resolved_ir
