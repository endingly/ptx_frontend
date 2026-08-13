#pragma once

#include <compare>
#include <concepts>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "ptx_ir/base.hpp"
#include "ptx_ir/source_loc.hpp"

namespace ptx_frontend::resolved_ir::checker {

namespace detail {

/** Combine variant-specific lambdas into the visitor accepted by std::visit. */
template <typename Function>
concept OverloadFunctionObject =
    std::is_class_v<std::remove_cvref_t<Function>> &&
    requires { &std::remove_cvref_t<Function>::operator(); };

template <OverloadFunctionObject... Functions>
struct Overloaded : Functions... {
  using Functions::operator()...;
};

template <OverloadFunctionObject... Functions>
Overloaded(Functions...) -> Overloaded<Functions...>;

}  // namespace detail

enum class OperandShape : uint16_t {
  Register = 1 << 0,
  Predicate = 1 << 1,
  Immediate = 1 << 2,
  Address = 1 << 3,
  Symbol = 1 << 4,
  Vector = 1 << 5,
};

constexpr OperandShape operator|(OperandShape lhs, OperandShape rhs) {
  using Underlying = std::underlying_type_t<OperandShape>;
  return static_cast<OperandShape>(static_cast<Underlying>(lhs) |
                                   static_cast<Underlying>(rhs));
}

enum class OperandRole : uint8_t {
  Destination,
  Source,
  Address,
  Predicate,
  BranchTarget,
  Barrier,
  ThreadCount,
};

enum class OperandAccess : uint8_t {
  Read,
  Write,
  ReadWrite,
};

/** A compile-time-normalized scalar-type source for one operand. */
enum class OperandTypeExpressionKind : uint8_t {
  None,
  FixedScalar,
  ModifierField,
};

/**
 * Type information generated from YAML rather than parsed at C++ runtime.
 *
 * ``modifier_field_id`` names the resolved modifier field for
 * ``modifier(name)`` expressions.  It is otherwise empty.
 */
struct TypeExpressionDescriptor {
  OperandTypeExpressionKind kind = OperandTypeExpressionKind::None;
  ScalarType fixed_scalar_type = ScalarType::Invalid;
  std::string_view modifier_field_id{};
};

/** Semantic constraints for one operand position in a resolved layout. */
struct OperandDescriptor {
  std::string_view target_field_id;
  TypeExpressionDescriptor type_expression;
  OperandRole role;
  OperandAccess access;
  OperandShape allowed_shapes;
};

/** A non-owning semantic view of one resolved modifier field. */
struct FieldView {
  std::string_view field_id;
  std::optional<ScalarType> scalar_type;
  std::span<const SourceRange> locations;
};

/** A non-owning semantic view of one resolved operand field. */
struct OperandView {
  std::string_view field_id;
  OperandShape actual_shape;
  std::optional<ScalarType> immediate_type;
  std::span<const SourceRange> locations;
};

/** A PTX ISA version used by both checker descriptors and compilation targets. */
struct PtxVersion {
  uint16_t major = 0;
  uint16_t minor = 0;

  constexpr auto operator<=>(const PtxVersion&) const = default;
};

/**
 * The target properties relevant to instruction availability checks.
 *
 * ``families`` is borrowed: the caller owns the underlying strings for the
 * duration of the check.  Keeping the ABI view-only also lets one target
 * advertise more than one compatible family in the future.
 */
struct TargetInfo {
  PtxVersion ptx_version{};
  uint32_t sm_version = 0;
  std::span<const std::string_view> families{};
};

/** Per-variant target requirements generated from YAML ``availability``. */
struct AvailabilityDescriptor {
  PtxVersion minimum_ptx_version{};
  uint32_t minimum_sm_version = 0;
  std::string_view required_family{};
};

/** Additional target requirements for one selected operand layout. */
struct OperandLayoutDescriptor {
  std::string_view layout_name;
  AvailabilityDescriptor availability;
};

/** Runtime representation used to compare modifier availability entries. */
enum class ModifierValueKind : uint8_t {
  Bool,
  ScalarType,
  RoundingMode,
};

/** Target requirement attached to one legal semantic modifier value. */
struct ModifierValueAvailabilityDescriptor {
  std::string_view kind_id;
  ModifierValueKind value_kind;
  bool bool_value = false;
  ScalarType scalar_type = ScalarType::Invalid;
  RoundingMode rounding_mode = RoundingMode::Invalid;
  AvailabilityDescriptor availability;
};

/** Selected dynamic modifier value exposed by a generated checker wrapper. */
struct ModifierValueView {
  std::string_view kind_id;
  ModifierValueKind value_kind;
  bool bool_value = false;
  ScalarType scalar_type = ScalarType::Invalid;
  RoundingMode rounding_mode = RoundingMode::Invalid;
  bool is_present = false;
  std::span<const SourceRange> locations;
};

/**
 * Checker metadata for one resolved variant.
 *
 * ``rule_id`` is a stable YAML rule key (for example
 * ``integer_arith.add``).  The common checker does not interpret it: a
 * generated, typed wrapper will dispatch it after the target-independent
 * checks have succeeded.
 */
struct VariantDescriptor {
  std::string_view variant_name;
  AvailabilityDescriptor availability;
  std::span<const ModifierValueAvailabilityDescriptor>
      modifier_value_availabilities;
  std::span<const OperandLayoutDescriptor> operand_layouts;
  std::string_view rule_id;
};

/** Checker metadata for all variants of one resolved instruction. */
struct InstructionDescriptor {
  std::string_view opcode_name;
  std::span<const VariantDescriptor> variants;
};

enum class CheckDiagnosticKind : uint8_t {
  UnsupportedPtxVersion,
  UnsupportedSmVersion,
  UnsupportedTargetFamily,
  MissingVariantDescriptor,
  MissingOperand,
  UnexpectedOperand,
  UnsupportedOperandShape,
  InvalidOperandLayoutTag,
  OperandLayoutPayloadMismatch,
  MissingTypeField,
  OperandTypeMismatch,
  RuleViolation,
};

/** A checker diagnostic, always anchored to a resolved-IR source range. */
struct CheckDiagnostic {
  CheckDiagnosticKind kind;
  SourceRange range;
  std::string message;
};

using CheckDiagnostics = std::vector<CheckDiagnostic>;
using CheckResult = std::expected<void, CheckDiagnostics>;

namespace detail {

/**
 * Verify the actual call contract of one generated variant-check lambda.
 *
 * ``Overloaded`` cannot state this constraint itself because it does not know
 * the variant alternatives before ``std::visit`` is instantiated.
 */
template <typename Function, typename Variant>
concept VariantCheckFunction =
    std::invocable<Function&, const Variant&> &&
    std::same_as<std::invoke_result_t<Function&, const Variant&>, CheckResult>;

}  // namespace detail

/** Context supplied by the caller for checking one resolved instruction. */
struct Context {
  TargetInfo target;
  SourceRange instruction_range;
};

/** Return whether one generated availability requirement accepts ``target``. */
bool is_available(const AvailabilityDescriptor& availability,
                  const TargetInfo& target) noexcept;

/** Find generated checker metadata by the resolved C++ variant name. */
const VariantDescriptor* find_variant_descriptor(
    const InstructionDescriptor& instruction,
    std::string_view variant_name) noexcept;

/** Run common target-availability checks for one already selected variant. */
CheckResult check_availability(const VariantDescriptor& variant,
                               const Context& context);

/**
 * Find the selected variant descriptor and run all common checker logic.
 *
 * Rule-specific semantic checks intentionally remain outside this function;
 * generated typed wrappers dispatch ``VariantDescriptor::rule_id`` after this
 * common stage.
 */
CheckResult check_common(const InstructionDescriptor& instruction,
                         std::string_view variant_name, const Context& context);

/**
 * Check operand constraints independent of a particular opcode.
 *
 * The generated typed wrapper supplies views of the selected resolved layout.
 * Currently this verifies operand-field identity, allowed resolved shape, and
 * immediate types constrained by the generated ``TypeExpressionDescriptor``.
 * Register type and state-space checks will join this entry point once symbol
 * lookup is part of ``Context``.
 */
CheckResult check_operands(std::span<const OperandDescriptor> descriptors,
                           std::span<const FieldView> fields,
                           std::span<const OperandView> operands,
                           const Context& context);

/** Verify that a resolved instruction preserved a valid selected layout tag. */
CheckResult check_operand_layout_tag(std::string_view variant_name,
                                     uint16_t selected_layout,
                                     size_t layout_count,
                                     const Context& context);

/**
 * Check target requirements contributed by the selected operand layout.
 *
 * A layout without YAML ``availability`` has an empty requirement; the
 * containing variant's availability is still checked by ``check_common``.
 */
CheckResult check_operand_layout_availability(const VariantDescriptor& variant,
                                              uint16_t selected_layout,
                                              const Context& context);

/** Check availability constraints for the actual dynamic modifier values. */
CheckResult check_modifier_value_availability(
    std::span<const ModifierValueAvailabilityDescriptor> descriptors,
    std::span<const ModifierValueView> actual_values, const Context& context);

/**
 * Check one generated resolved instruction.
 *
 * Explicit specializations are generated per opcode once its rule wrapper is
 * available.  Declaring this template here gives all clients one stable
 * checker entry point without coupling this foundational ABI to generated
 * instruction types.
 */
template <typename T>
CheckResult check(const T& instruction, const Context& context);

}  // namespace ptx_frontend::resolved_ir::checker
