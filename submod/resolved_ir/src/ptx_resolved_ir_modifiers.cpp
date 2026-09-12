#include "ptx_resolved_ir_private.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <unordered_set>

#include "resolved_value_domains.gen.hpp"

namespace ptx_frontend::resolved_ir {
using check_end::OperandPresence;
using check_end::OperandSyntaxShape;
using check_end::ResolvedFieldDescriptor;
using check_end::ResolvedInstructionDescriptor;
using check_end::ResolvedModifierBindingDescriptor;
using check_end::ResolvedModifierDefaultKind;
using check_end::ResolvedOperandBindingDescriptor;
using check_end::ResolvedOperandLayoutDescriptor;
using check_end::ResolvedValueKind;
using check_end::ResolvedVariantDescriptor;
using check_end::SyntaxInstructionDescriptor;
using check_end::SyntaxModifierDescriptor;
using check_end::SyntaxOperandLayoutDescriptor;
using check_end::SyntaxOperandSlotDescriptor;
using check_end::SyntaxVariantDescriptor;

namespace detail {
template <typename T, size_t N>
constexpr std::optional<T> lookup_ptx_suffix(
    const std::array<generated_detail::PtxSuffixEntry<T>, N>& entries,
    std::string_view spelling) {
  if (spelling.starts_with('.'))
    spelling.remove_prefix(1);
  for (const auto& entry : entries) {
    if (entry.suffix == spelling)
      return entry.value;
  }
  return std::nullopt;
}

std::optional<ScalarType> scalar_type_from_ptx_name(std::string_view spelling) {
  return lookup_ptx_suffix(generated_detail::kScalarTypes, spelling);
}

std::expected<WithLocs<ScalarType>, ResolveDiagnostic> resolve_scalar_type(
    const syntax_ast::AstModifier& modifier) {
  const auto type = scalar_type_from_ptx_name(modifier.syntax.text);
  if (!type) {
    return std::unexpected(ResolveDiagnostic{
        .range = modifier.syntax.range,
        .message =
            fmt::format("Unknown scalar type '{}'.", modifier.syntax.text),
    });
  }
  return WithLocs<ScalarType>{*type, modifier.syntax.range};
}

std::optional<RoundingMode> rounding_mode_from_ptx_name(
    std::string_view spelling) {
  return lookup_ptx_suffix(generated_detail::kRoundingModes, spelling);
}

std::expected<WithLocs<RoundingMode>, ResolveDiagnostic> resolve_rounding_mode(
    const syntax_ast::AstModifier& modifier) {
  const auto mode = rounding_mode_from_ptx_name(modifier.syntax.text);
  if (!mode) {
    return std::unexpected(ResolveDiagnostic{
        .range = modifier.syntax.range,
        .message =
            fmt::format("Unknown rounding mode '{}'.", modifier.syntax.text),
    });
  }
  return WithLocs<RoundingMode>{*mode, modifier.syntax.range};
}

std::optional<ComparisonOperator> comparison_operator_from_ptx_name(
    std::string_view spelling) {
  return lookup_ptx_suffix(generated_detail::kComparisonOperators, spelling);
}

std::expected<WithLocs<ComparisonOperator>, ResolveDiagnostic>
resolve_comparison_operator(const syntax_ast::AstModifier& modifier) {
  const auto value = comparison_operator_from_ptx_name(modifier.syntax.text);
  if (!value) {
    return std::unexpected(ResolveDiagnostic{
        .range = modifier.syntax.range,
        .message = fmt::format("Unknown comparison operator '{}'.",
                               modifier.syntax.text),
    });
  }
  return WithLocs<ComparisonOperator>{*value, modifier.syntax.range};
}

std::optional<BooleanOperator> boolean_operator_from_ptx_name(
    std::string_view spelling) {
  return lookup_ptx_suffix(generated_detail::kBooleanOperators, spelling);
}

std::expected<WithLocs<BooleanOperator>, ResolveDiagnostic>
resolve_boolean_operator(const syntax_ast::AstModifier& modifier) {
  const auto value = boolean_operator_from_ptx_name(modifier.syntax.text);
  if (!value) {
    return std::unexpected(ResolveDiagnostic{
        .range = modifier.syntax.range,
        .message =
            fmt::format("Unknown boolean operator '{}'.", modifier.syntax.text),
    });
  }
  return WithLocs<BooleanOperator>{*value, modifier.syntax.range};
}

std::optional<CacheOperator> cache_operator_from_ptx_name(
    std::string_view spelling) {
  return lookup_ptx_suffix(generated_detail::kCacheOperators, spelling);
}

std::expected<WithLocs<CacheOperator>, ResolveDiagnostic>
resolve_cache_operator(const syntax_ast::AstModifier& modifier) {
  const auto cache_operator =
      cache_operator_from_ptx_name(modifier.syntax.text);
  if (!cache_operator) {
    return std::unexpected(ResolveDiagnostic{
        .range = modifier.syntax.range,
        .message =
            fmt::format("Unknown cache operator '{}'.", modifier.syntax.text),
    });
  }
  return WithLocs<CacheOperator>{*cache_operator, modifier.syntax.range};
}

std::optional<EvictionPriority> eviction_priority_from_ptx_name(
    std::string_view spelling) {
  if (spelling.starts_with(".L1::") || spelling.starts_with(".L2::"))
    spelling.remove_prefix(std::string_view{".L1::"}.size());
  return lookup_ptx_suffix(generated_detail::kEvictionPriorities, spelling);
}

std::expected<WithLocs<EvictionPriority>, ResolveDiagnostic>
resolve_eviction_priority(const syntax_ast::AstModifier& modifier) {
  const auto value = eviction_priority_from_ptx_name(modifier.syntax.text);
  if (!value) {
    return std::unexpected(ResolveDiagnostic{
        .range = modifier.syntax.range,
        .message = fmt::format("Unknown eviction priority '{}'.",
                               modifier.syntax.text),
    });
  }
  return WithLocs<EvictionPriority>{*value, modifier.syntax.range};
}

std::optional<PrefetchSize> prefetch_size_from_ptx_name(
    std::string_view spelling) {
  return lookup_ptx_suffix(generated_detail::kPrefetchSizes, spelling);
}

std::expected<WithLocs<PrefetchSize>, ResolveDiagnostic> resolve_prefetch_size(
    const syntax_ast::AstModifier& modifier) {
  const auto value = prefetch_size_from_ptx_name(modifier.syntax.text);
  if (!value) {
    return std::unexpected(ResolveDiagnostic{
        .range = modifier.syntax.range,
        .message =
            fmt::format("Unknown prefetch size '{}'.", modifier.syntax.text),
    });
  }
  return WithLocs<PrefetchSize>{*value, modifier.syntax.range};
}

std::optional<MemoryConsistency> memory_consistency_from_ptx_name(
    std::string_view spelling) {
  return lookup_ptx_suffix(generated_detail::kMemoryConsistencies, spelling);
}

std::expected<WithLocs<MemoryConsistency>, ResolveDiagnostic>
resolve_memory_consistency(const syntax_ast::AstModifier& modifier) {
  const auto value = memory_consistency_from_ptx_name(modifier.syntax.text);
  if (!value) {
    return std::unexpected(ResolveDiagnostic{
        .range = modifier.syntax.range,
        .message = fmt::format("Unknown memory consistency '{}'.",
                               modifier.syntax.text),
    });
  }
  return WithLocs<MemoryConsistency>{*value, modifier.syntax.range};
}

std::optional<MemoryScope> memory_scope_from_ptx_name(
    std::string_view spelling) {
  return lookup_ptx_suffix(generated_detail::kMemoryScopes, spelling);
}

std::expected<WithLocs<MemoryScope>, ResolveDiagnostic> resolve_memory_scope(
    const syntax_ast::AstModifier& modifier) {
  const auto value = memory_scope_from_ptx_name(modifier.syntax.text);
  if (!value) {
    return std::unexpected(ResolveDiagnostic{
        .range = modifier.syntax.range,
        .message =
            fmt::format("Unknown memory scope '{}'.", modifier.syntax.text),
    });
  }
  return WithLocs<MemoryScope>{*value, modifier.syntax.range};
}

std::expected<WithLocs<MbarrierPhaseType>, ResolveDiagnostic>
resolve_mbarrier_phase_type(const syntax_ast::AstModifier& modifier) {
  const auto value = lookup_ptx_suffix(generated_detail::kMbarrierPhaseTypes,
                                       modifier.syntax.text);
  if (!value)
    return std::unexpected(ResolveDiagnostic{
        .range = modifier.syntax.range,
        .message = fmt::format("Unknown mbarrier phase type '{}'.",
                               modifier.syntax.text),
    });
  return WithLocs<MbarrierPhaseType>{*value, modifier.syntax.range};
}

std::expected<WithLocs<MbarrierLayout>, ResolveDiagnostic>
resolve_mbarrier_layout(const syntax_ast::AstModifier& modifier) {
  const auto value = lookup_ptx_suffix(generated_detail::kMbarrierLayouts,
                                       modifier.syntax.text);
  if (!value)
    return std::unexpected(ResolveDiagnostic{
        .range = modifier.syntax.range,
        .message =
            fmt::format("Unknown mbarrier layout '{}'.", modifier.syntax.text),
    });
  return WithLocs<MbarrierLayout>{*value, modifier.syntax.range};
}

std::expected<WithLocs<AsyncProxyKind>, ResolveDiagnostic>
resolve_async_proxy_kind(const syntax_ast::AstModifier& modifier) {
  const auto value = lookup_ptx_suffix(generated_detail::kAsyncProxyKinds,
                                       modifier.syntax.text);
  if (!value)
    return std::unexpected(ResolveDiagnostic{
        .range = modifier.syntax.range,
        .message =
            fmt::format("Unknown async proxy kind '{}'.", modifier.syntax.text),
    });
  return WithLocs<AsyncProxyKind>{*value, modifier.syntax.range};
}

std::expected<WithLocs<ProxyKindPair>, ResolveDiagnostic>
resolve_proxy_kind_pair(const syntax_ast::AstModifier& modifier) {
  const auto value = lookup_ptx_suffix(generated_detail::kProxyKindPairs,
                                       modifier.syntax.text);
  if (!value)
    return std::unexpected(ResolveDiagnostic{
        .range = modifier.syntax.range,
        .message =
            fmt::format("Unknown proxy kind pair '{}'.", modifier.syntax.text),
    });
  return WithLocs<ProxyKindPair>{*value, modifier.syntax.range};
}

std::optional<VectorArity> vector_arity_from_ptx_name(
    std::string_view spelling) {
  return lookup_ptx_suffix(generated_detail::kVectorArities, spelling);
}

std::expected<WithLocs<VectorArity>, ResolveDiagnostic> resolve_vector_arity(
    const syntax_ast::AstModifier& modifier) {
  const auto arity = vector_arity_from_ptx_name(modifier.syntax.text);
  if (!arity) {
    return std::unexpected(ResolveDiagnostic{
        .range = modifier.syntax.range,
        .message =
            fmt::format("Unknown vector arity '{}'.", modifier.syntax.text),
    });
  }
  return WithLocs<VectorArity>{*arity, modifier.syntax.range};
}

std::optional<MemoryStateSpace> memory_state_space_from_ptx_name(
    std::string_view spelling) {
  if (spelling == ".shared::cta" || spelling == ".shared::cluster")
    return MemoryStateSpace::Shared;
  return lookup_ptx_suffix(generated_detail::kMemoryStateSpaces, spelling);
}

std::expected<WithLocs<MemoryStateSpace>, ResolveDiagnostic>
resolve_memory_state_space(const syntax_ast::AstModifier& modifier) {
  const auto state_space =
      memory_state_space_from_ptx_name(modifier.syntax.text);
  if (!state_space) {
    return std::unexpected(ResolveDiagnostic{
        .range = modifier.syntax.range,
        .message = fmt::format("Unknown memory state space '{}'.",
                               modifier.syntax.text),
    });
  }
  return WithLocs<MemoryStateSpace>{*state_space, modifier.syntax.range};
}

/** Convert one typed modifier-parser result into the field-value union. */
template <ResolvedFieldType Value>
std::expected<ResolvedFieldValue, ResolveDiagnostic> as_modifier_field_value(
    std::expected<WithLocs<Value>, ResolveDiagnostic> value) {
  if (!value)
    return std::unexpected(value.error());
  return ResolvedFieldValue{std::move(*value)};
}

/** Parse the presence-only boolean modifier domain. */
std::expected<ResolvedFieldValue, ResolveDiagnostic> parse_bool_modifier(
    const syntax_ast::AstModifier& modifier) {
  return ResolvedFieldValue{WithLocs<bool>{true, modifier.syntax.range}};
}

#define PTX_DEFINE_TYPED_MODIFIER_PARSER(name, resolver)              \
  /** Parse the named modifier domain into its erased field value. */ \
  std::expected<ResolvedFieldValue, ResolveDiagnostic>                \
  parse_##name##_modifier(const syntax_ast::AstModifier& modifier) {  \
    return as_modifier_field_value(resolver(modifier));               \
  }

PTX_DEFINE_TYPED_MODIFIER_PARSER(scalar_type, resolve_scalar_type)
PTX_DEFINE_TYPED_MODIFIER_PARSER(rounding_mode, resolve_rounding_mode)
PTX_DEFINE_TYPED_MODIFIER_PARSER(comparison_operator,
                                 resolve_comparison_operator)
PTX_DEFINE_TYPED_MODIFIER_PARSER(boolean_operator, resolve_boolean_operator)
PTX_DEFINE_TYPED_MODIFIER_PARSER(cache_operator, resolve_cache_operator)
PTX_DEFINE_TYPED_MODIFIER_PARSER(eviction_priority, resolve_eviction_priority)
PTX_DEFINE_TYPED_MODIFIER_PARSER(prefetch_size, resolve_prefetch_size)
PTX_DEFINE_TYPED_MODIFIER_PARSER(memory_consistency, resolve_memory_consistency)
PTX_DEFINE_TYPED_MODIFIER_PARSER(memory_scope, resolve_memory_scope)
PTX_DEFINE_TYPED_MODIFIER_PARSER(vector_arity, resolve_vector_arity)
PTX_DEFINE_TYPED_MODIFIER_PARSER(memory_state_space, resolve_memory_state_space)
PTX_DEFINE_TYPED_MODIFIER_PARSER(mbarrier_phase_type,
                                 resolve_mbarrier_phase_type)
PTX_DEFINE_TYPED_MODIFIER_PARSER(mbarrier_layout, resolve_mbarrier_layout)
PTX_DEFINE_TYPED_MODIFIER_PARSER(async_proxy_kind, resolve_async_proxy_kind)
PTX_DEFINE_TYPED_MODIFIER_PARSER(proxy_kind_pair, resolve_proxy_kind_pair)

#undef PTX_DEFINE_TYPED_MODIFIER_PARSER

/** Build a boolean optional-modifier default. */
std::optional<ResolvedFieldValue> default_bool_modifier(
    const check_end::ResolvedModifierDefaultDescriptor& value) {
  return ResolvedFieldValue{WithLocs<bool>{value.bool_value}};
}

#define PTX_DEFINE_MODIFIER_DEFAULT(name, type, member, valid)              \
  /** Build the named optional-modifier default when its value is valid. */ \
  std::optional<ResolvedFieldValue> default_##name##_modifier(              \
      const check_end::ResolvedModifierDefaultDescriptor& value) {          \
    if (!(valid))                                                           \
      return std::nullopt;                                                  \
    return ResolvedFieldValue{WithLocs<type>{value.member}};                \
  }

PTX_DEFINE_MODIFIER_DEFAULT(scalar_type, ScalarType, scalar_type,
                            value.scalar_type != ScalarType::Invalid)
PTX_DEFINE_MODIFIER_DEFAULT(rounding_mode, RoundingMode, rounding_mode,
                            value.rounding_mode != RoundingMode::Invalid)
PTX_DEFINE_MODIFIER_DEFAULT(cache_operator, CacheOperator, cache_operator, true)
PTX_DEFINE_MODIFIER_DEFAULT(eviction_priority, EvictionPriority,
                            eviction_priority, true)
PTX_DEFINE_MODIFIER_DEFAULT(prefetch_size, PrefetchSize, prefetch_size, true)
PTX_DEFINE_MODIFIER_DEFAULT(memory_consistency, MemoryConsistency,
                            memory_consistency, true)
PTX_DEFINE_MODIFIER_DEFAULT(memory_scope, MemoryScope, memory_scope, true)
PTX_DEFINE_MODIFIER_DEFAULT(memory_state_space, MemoryStateSpace,
                            memory_state_space,
                            value.memory_state_space !=
                                MemoryStateSpace::Invalid)
PTX_DEFINE_MODIFIER_DEFAULT(mbarrier_phase_type, MbarrierPhaseType,
                            mbarrier_phase_type, true)
PTX_DEFINE_MODIFIER_DEFAULT(mbarrier_layout, MbarrierLayout, mbarrier_layout,
                            true)
PTX_DEFINE_MODIFIER_DEFAULT(async_proxy_kind, AsyncProxyKind, async_proxy_kind,
                            true)
PTX_DEFINE_MODIFIER_DEFAULT(proxy_kind_pair, ProxyKindPair, proxy_kind_pair,
                            true)

#undef PTX_DEFINE_MODIFIER_DEFAULT

/** Describes how an optional modifier lacking a generated default is rejected. */
enum class ModifierDefaultPolicy : uint8_t {
  Supported,
  UnsupportedDomain,
  NonModifierDomain,
};

/** Type-erased parser for one modifier value domain. */
using ModifierParser = std::expected<ResolvedFieldValue, ResolveDiagnostic> (*)(
    const syntax_ast::AstModifier&);
/** Type-erased materializer for one generated optional-modifier default. */
using ModifierDefaultBuilder = std::optional<ResolvedFieldValue> (*)(
    const check_end::ResolvedModifierDefaultDescriptor&);

/** Complete mechanical association for one modifier value domain. */
struct ModifierDomainMapping {
  /** Generated default union alternative expected for an omitted modifier. */
  ResolvedModifierDefaultKind default_kind;
  /** Parser used when the source modifier is present. */
  ModifierParser parser;
  /** Materializer for an omitted default; null outside supported domains. */
  ModifierDefaultBuilder default_builder;
  /** Diagnostic domain name retained by invalid-default diagnostics. */
  std::string_view diagnostic_name;
  /** Rejection category for domains without a generated default. */
  ModifierDefaultPolicy default_policy;
};

/** One private table owns modifier kind, parser, and default associations. */
#define PTX_MODIFIER_DOMAIN_TABLE(X)                                          \
  X(Bool, Bool, parse_bool_modifier, default_bool_modifier, "boolean",        \
    Supported)                                                                \
  X(ScalarType, ScalarType, parse_scalar_type_modifier,                       \
    default_scalar_type_modifier, "scalar-type", Supported)                   \
  X(RoundingMode, RoundingMode, parse_rounding_mode_modifier,                 \
    default_rounding_mode_modifier, "rounding-mode", Supported)               \
  X(ComparisonOperator, None, parse_comparison_operator_modifier, nullptr,    \
    "comparison-operator", UnsupportedDomain)                                 \
  X(BooleanOperator, None, parse_boolean_operator_modifier, nullptr,          \
    "boolean-operator", UnsupportedDomain)                                    \
  X(CacheOperator, CacheOperator, parse_cache_operator_modifier,              \
    default_cache_operator_modifier, "cache-operator", Supported)             \
  X(EvictionPriority, EvictionPriority, parse_eviction_priority_modifier,     \
    default_eviction_priority_modifier, "eviction-priority", Supported)       \
  X(PrefetchSize, PrefetchSize, parse_prefetch_size_modifier,                 \
    default_prefetch_size_modifier, "prefetch size", Supported)               \
  X(MemoryConsistency, MemoryConsistency, parse_memory_consistency_modifier,  \
    default_memory_consistency_modifier, "memory-consistency", Supported)     \
  X(MemoryScope, MemoryScope, parse_memory_scope_modifier,                    \
    default_memory_scope_modifier, "memory-scope", Supported)                 \
  X(VectorArity, None, parse_vector_arity_modifier, nullptr, "vector-arity",  \
    NonModifierDomain)                                                        \
  X(MemoryStateSpace, MemoryStateSpace, parse_memory_state_space_modifier,    \
    default_memory_state_space_modifier, "memory-state-space", Supported)     \
  X(MbarrierPhaseType, MbarrierPhaseType, parse_mbarrier_phase_type_modifier, \
    default_mbarrier_phase_type_modifier, "mbarrier phase-type", Supported)   \
  X(MbarrierLayout, MbarrierLayout, parse_mbarrier_layout_modifier,           \
    default_mbarrier_layout_modifier, "mbarrier layout", Supported)           \
  X(AsyncProxyKind, AsyncProxyKind, parse_async_proxy_kind_modifier,          \
    default_async_proxy_kind_modifier, "async proxy", Supported)              \
  X(ProxyKindPair, ProxyKindPair, parse_proxy_kind_pair_modifier,             \
    default_proxy_kind_pair_modifier, "proxy pair", Supported)

/** Number of contiguous modifier domains at the start of ResolvedValueKind. */
constexpr size_t kModifierDomainCount =
    static_cast<size_t>(ResolvedValueKind::Register);
static_assert(kModifierDomainCount ==
                  static_cast<size_t>(ResolvedValueKind::ProxyKindPair) + 1,
              "ResolvedValueKind modifier domains must remain contiguous; "
              "extend PTX_MODIFIER_DOMAIN_TABLE for a new modifier kind.");

#define PTX_COUNT_MODIFIER_DOMAIN(...) +1
constexpr size_t kModifierDomainTableRows =
    0 PTX_MODIFIER_DOMAIN_TABLE(PTX_COUNT_MODIFIER_DOMAIN);
#undef PTX_COUNT_MODIFIER_DOMAIN
static_assert(kModifierDomainTableRows == kModifierDomainCount,
              "PTX_MODIFIER_DOMAIN_TABLE must cover every modifier domain.");

/** Build direct-indexed mappings from the one private domain table. */
consteval std::array<ModifierDomainMapping, kModifierDomainCount>
make_modifier_domain_mappings() {
  std::array<ModifierDomainMapping, kModifierDomainCount> mappings{};
#define PTX_ASSIGN_MODIFIER_DOMAIN(row_kind, row_default_kind, row_parser, \
                                   row_builder, row_name, row_policy)      \
  mappings[static_cast<size_t>(ResolvedValueKind::row_kind)] = {           \
      .default_kind = ResolvedModifierDefaultKind::row_default_kind,       \
      .parser = row_parser,                                                \
      .default_builder = row_builder,                                      \
      .diagnostic_name = row_name,                                         \
      .default_policy = ModifierDefaultPolicy::row_policy};
  PTX_MODIFIER_DOMAIN_TABLE(PTX_ASSIGN_MODIFIER_DOMAIN)
#undef PTX_ASSIGN_MODIFIER_DOMAIN
  return mappings;
}

/** Direct-indexed modifier domain mappings, verified at translation time. */
constexpr auto kModifierDomainMappings = make_modifier_domain_mappings();

/** Verify every generated modifier domain has exactly one parser association. */
consteval bool modifier_domain_mappings_are_complete() {
  for (const auto& mapping : kModifierDomainMappings) {
    if (mapping.parser == nullptr)
      return false;
    if (mapping.default_policy == ModifierDefaultPolicy::Supported &&
        mapping.default_builder == nullptr)
      return false;
  }
  return true;
}

static_assert(modifier_domain_mappings_are_complete());

/** Return the direct-indexed mapping for a modifier field, if it has one. */
const ModifierDomainMapping* modifier_domain_mapping(
    ResolvedValueKind value_kind) noexcept {
  const auto index = static_cast<size_t>(value_kind);
  if (index >= kModifierDomainMappings.size())
    return nullptr;
  return &kModifierDomainMappings[index];
}

ParameterAddressQualifier parameter_address_qualifier_from_modifier(
    std::string_view spelling) noexcept {
  if (spelling == ".param::entry")
    return ParameterAddressQualifier::Entry;
  if (spelling == ".param::func")
    return ParameterAddressQualifier::Function;
  return ParameterAddressQualifier::Default;
}

struct ModifierBindingAttempt {
  std::optional<ActualModifierTable> modifiers;
  const syntax_ast::AstModifier* duplicate = nullptr;
  std::string_view duplicate_slot;
};

/**
 * Bind source modifier spellings to the slots of one candidate variant.
 *
 * Slot IDs are variant-local. The same spelling may therefore denote `type`
 * in one variant and `result_type` in another, or occupy multiple ordered
 * required/fixed slots in the same variant. The database rejects repeated
 * spellings involving optional slots, so source modifiers greedily consume
 * descriptors in order; optional and absent slots may be skipped, but required
 * slots may not.
 */
ModifierBindingAttempt bind_modifier_order(
    const syntax_ast::AstInstruction& ast,
    const SyntaxVariantDescriptor& variant,
    std::span<const SyntaxModifierDescriptor> modifiers) {
  std::unordered_set<std::string_view> slot_ids;
  for (const auto& descriptor : modifiers) {
    if (!slot_ids.insert(descriptor.kind_id).second) {
      throw ResolveException(
          fmt::format("Variant '{}' contains duplicate modifier slot '{}'.",
                      variant.variant_name, descriptor.kind_id));
    }
  }

  ActualModifierTable result;
  size_t actual_index = 0;
  for (const auto& descriptor : modifiers) {
    if (descriptor.presence == check_end::PresenceRequirement::Absent)
      continue;
    if (actual_index < ast.modifiers.size() &&
        std::ranges::contains(descriptor.allowed_values,
                              ast.modifiers[actual_index].syntax.text)) {
      result.emplace(std::string(descriptor.kind_id),
                     &ast.modifiers[actual_index++]);
      continue;
    }
    if (descriptor.presence == check_end::PresenceRequirement::Required)
      return {};
  }

  if (actual_index == ast.modifiers.size())
    return ModifierBindingAttempt{.modifiers = std::move(result)};

  const auto& extra = ast.modifiers[actual_index];
  for (auto descriptor = modifiers.rbegin(); descriptor != modifiers.rend();
       ++descriptor) {
    if (descriptor->presence != check_end::PresenceRequirement::Absent &&
        result.contains(std::string(descriptor->kind_id)) &&
        std::ranges::contains(descriptor->allowed_values, extra.syntax.text)) {
      return ModifierBindingAttempt{
          .duplicate = &extra,
          .duplicate_slot = descriptor->kind_id,
      };
    }
  }
  return {};
}

/**
 * Bind against the canonical modifier order and every declared historical
 * order for one semantic variant.
 */
ModifierBindingAttempt bind_variant_modifiers(
    const syntax_ast::AstInstruction& ast,
    const SyntaxVariantDescriptor& variant) {
  ModifierBindingAttempt result;
  const auto try_order = [&](std::span<const SyntaxModifierDescriptor> order) {
    auto attempt = bind_modifier_order(ast, variant, order);
    if (attempt.modifiers) {
      if (result.modifiers && *result.modifiers != *attempt.modifiers) {
        throw ResolveException(fmt::format(
            "Variant '{}' has modifier orders with ambiguous slot bindings.",
            variant.variant_name));
      }
      if (!result.modifiers)
        result.modifiers = std::move(attempt.modifiers);
    } else if (result.duplicate == nullptr && attempt.duplicate != nullptr) {
      result.duplicate = attempt.duplicate;
      result.duplicate_slot = attempt.duplicate_slot;
    }
  };

  try_order(variant.modifiers);
  for (const auto& alias : variant.modifier_order_aliases)
    try_order(alias.modifiers);
  return result;
}

bool is_known_modifier_spelling(const SyntaxInstructionDescriptor& instruction,
                                std::string_view spelling) {
  return std::ranges::any_of(
      instruction.variants, [spelling](const auto& variant) {
        return std::ranges::any_of(
            variant.modifiers, [spelling](const auto& modifier) {
              return std::ranges::contains(modifier.allowed_values, spelling);
            });
      });
}

const ResolvedVariantDescriptor& find_resolved_variant_descriptor(
    const ResolvedInstructionDescriptor& instruction, std::string_view name) {
  const auto it =
      std::ranges::find_if(instruction.variants,
                           [name](const ResolvedVariantDescriptor& descriptor) {
                             return descriptor.variant_name == name;
                           });
  if (it == instruction.variants.end()) {
    throw ResolveException(
        fmt::format("Resolved descriptor for '{}' has no variant named '{}'.",
                    instruction.opcode_name, name));
  }
  return *it;
}

const ResolvedFieldDescriptor& find_resolved_field_descriptor(
    const ResolvedVariantDescriptor& variant, std::string_view field_id) {
  const auto it = std::ranges::find_if(
      variant.fields, [field_id](const ResolvedFieldDescriptor& descriptor) {
        return descriptor.field_id == field_id;
      });
  if (it == variant.fields.end()) {
    throw ResolveException(
        fmt::format("Resolved descriptor variant '{}' has no field named '{}'.",
                    variant.variant_name, field_id));
  }
  return *it;
}

const ResolvedFieldDescriptor& find_resolved_operand_field_descriptor(
    const ResolvedOperandLayoutDescriptor& layout, std::string_view field_id) {
  const auto it = std::ranges::find_if(
      layout.fields, [field_id](const ResolvedFieldDescriptor& descriptor) {
        return descriptor.field_id == field_id;
      });
  if (it == layout.fields.end()) {
    throw ResolveException(
        fmt::format("Resolved operand layout '{}' has no field named '{}'.",
                    layout.layout_id, field_id));
  }
  return *it;
}

const SyntaxModifierDescriptor& find_syntax_modifier_descriptor(
    const SyntaxVariantDescriptor& variant, std::string_view kind_id) {
  const auto it = std::ranges::find_if(
      variant.modifiers, [kind_id](const SyntaxModifierDescriptor& descriptor) {
        return descriptor.kind_id == kind_id;
      });
  if (it == variant.modifiers.end()) {
    throw ResolveException(
        fmt::format("Syntax descriptor variant '{}' has no modifier kind '{}'.",
                    variant.variant_name, kind_id));
  }
  return *it;
}

ResolvedFieldValue resolve_default_modifier_value(
    const ResolvedFieldDescriptor& field,
    const ResolvedModifierBindingDescriptor& binding) {
  const auto& default_value = binding.default_value;
  const auto* domain = modifier_domain_mapping(field.value_kind);
  if (domain == nullptr ||
      domain->default_policy == ModifierDefaultPolicy::NonModifierDomain) {
    throw ResolveException(fmt::format(
        "Optional modifier '{}' targets non-modifier resolved field '{}'.",
        binding.source_kind_id, field.field_id));
  }
  if (domain->default_policy == ModifierDefaultPolicy::UnsupportedDomain) {
    throw ResolveException(fmt::format(
        "Optional modifier '{}' cannot use a {} default for resolved field "
        "'{}'.",
        binding.source_kind_id, domain->diagnostic_name, field.field_id));
  }
  if (default_value.kind != domain->default_kind) {
    throw ResolveException(fmt::format(
        "Optional modifier '{}' requires a {} default for resolved field "
        "'{}'.",
        binding.source_kind_id, domain->diagnostic_name, field.field_id));
  }
  const auto resolved_default = domain->default_builder(default_value);
  if (!resolved_default) {
    throw ResolveException(fmt::format(
        "Optional modifier '{}' requires a {} default for resolved field "
        "'{}'.",
        binding.source_kind_id, domain->diagnostic_name, field.field_id));
  }
  return std::move(*resolved_default);
}

std::expected<ResolvedFieldValue, ResolveDiagnostic> resolve_modifier_value(
    const ResolvedFieldDescriptor& field,
    const syntax_ast::AstModifier& modifier) {
  const auto* domain = modifier_domain_mapping(field.value_kind);
  if (domain == nullptr) {
    throw ResolveException(
        fmt::format("Modifier '{}' has a non-modifier resolved value kind.",
                    modifier.syntax.text));
  }
  return domain->parser(modifier);
}

}  // namespace detail

std::expected<ActualModifierTable, ResolveDiagnostic> collect_actual_modifiers(
    const syntax_ast::AstInstruction& ast,
    const check_end::SyntaxVariantDescriptor& variant) {
  auto attempt = detail::bind_variant_modifiers(ast, variant);
  if (attempt.modifiers)
    return std::move(*attempt.modifiers);
  if (attempt.duplicate != nullptr) {
    return std::unexpected(ResolveDiagnostic{
        .range = attempt.duplicate->syntax.range,
        .message =
            fmt::format("Duplicate '{}' modifier.", attempt.duplicate_slot),
    });
  }
  return std::unexpected(ResolveDiagnostic{
      .range = ast.range,
      .message = fmt::format(
          "Modifier combination does not match instruction variant '{}'.",
          variant.variant_name),
  });
}

std::expected<std::string_view, ResolveDiagnostic> select_variant_name(
    const syntax_ast::AstInstruction& ast,
    const check_end::SyntaxInstructionDescriptor& instruction) {
  if (ast.opcode.syntax.text != instruction.Opcode_name) {
    return std::unexpected(ResolveDiagnostic{
        .range = ast.opcode.syntax.range,
        .message = fmt::format("Cannot resolve opcode '{}' as '{}'.",
                               ast.opcode.syntax.text, instruction.Opcode_name),
    });
  }

  for (const auto& modifier : ast.modifiers) {
    if (!detail::is_known_modifier_spelling(instruction,
                                            modifier.syntax.text)) {
      return std::unexpected(ResolveDiagnostic{
          .range = modifier.syntax.range,
          .message =
              fmt::format("Unknown modifier '{}'.", modifier.syntax.text),
      });
    }
  }

  std::optional<std::string_view> selected;
  const syntax_ast::AstModifier* duplicate = nullptr;
  std::string_view duplicate_slot;
  for (const auto& variant : instruction.variants) {
    auto attempt = detail::bind_variant_modifiers(ast, variant);
    if (!attempt.modifiers) {
      if (duplicate == nullptr && attempt.duplicate != nullptr) {
        duplicate = attempt.duplicate;
        duplicate_slot = attempt.duplicate_slot;
      }
      continue;
    }

    if (selected) {
      return std::unexpected(ResolveDiagnostic{
          .range = ast.range,
          .message = fmt::format(
              "Ambiguous modifier combination for instruction '{}'.",
              ast.opcode.syntax.text),
      });
    }
    selected = variant.variant_name;
  }

  if (selected)
    return *selected;
  if (duplicate != nullptr) {
    return std::unexpected(ResolveDiagnostic{
        .range = duplicate->syntax.range,
        .message = fmt::format("Duplicate '{}' modifier.", duplicate_slot),
    });
  }
  return std::unexpected(ResolveDiagnostic{
      .range = ast.range,
      .message = fmt::format(
          "No variant of instruction '{}' accepts this modifier combination.",
          ast.opcode.syntax.text),
  });
}

}  // namespace ptx_frontend::resolved_ir
