#pragma once

#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_foundation.hpp>

namespace ptx_frontend::resolved_ir::checker {

/** Category of a public instruction-check diagnostic. */
enum class CheckDiagnosticKind : uint8_t {
  UnknownTarget,
  UnsupportedPtxVersion,
  UnsupportedSmVersion,
  UnsupportedTargetFamily,
  UnsupportedAvailability,
  MissingVariantDescriptor,
  MissingOperand,
  UnexpectedOperand,
  UnsupportedOperandShape,
  InvalidOperandLayoutTag,
  OperandLayoutPayloadMismatch,
  MissingTypeField,
  MissingStateSpaceField,
  MissingVectorArityField,
  OperandTypeMismatch,
  AddressStateSpaceMismatch,
  AddressAlignmentMismatch,
  ParameterDirectionMismatch,
  ParameterQualifierMismatch,
  InvalidVectorOperand,
  MemoryConsistencyViolation,
  ImmediateValueMismatch,
  RuleViolation,
  ModuleSourceMismatch,
  MissingValidationContext,
  ModifierValueDomainMismatch,
};

/** A checker failure anchored to a stable resolved-IR source range. */
struct CheckDiagnostic {
  CheckDiagnosticKind kind;
  SourceRange range;
  std::string message;
};
using CheckDiagnostics = std::vector<CheckDiagnostic>;
using CheckResult = std::expected<void, CheckDiagnostics>;

namespace detail {
template <typename Function, typename Variant>
concept VariantCheckFunction =
    std::invocable<Function&, const Variant&> &&
    std::same_as<std::invoke_result_t<Function&, const Variant&>, CheckResult>;
}  // namespace detail

/** Caller-owned context for checking a single resolved instruction. */
struct Context {
  TargetInfo target;
  SourceRange instruction_range;
};

/** Return whether a generated availability requirement accepts ``target``. */
bool is_available(const AvailabilityDescriptor&, const TargetInfo&) noexcept;
/** Find generated checker metadata by the resolved C++ variant name. */
const VariantDescriptor* find_variant_descriptor(const InstructionDescriptor&,
                                                 std::string_view) noexcept;
/** Run common target-availability checks for an already selected variant. */
CheckResult check_availability(const VariantDescriptor&, const Context&);
/** Find a selected variant and run checker logic shared by all opcode rules. */
CheckResult check_common(const InstructionDescriptor&, std::string_view,
                         const Context&);
/** Check descriptor-driven operand shape, type, and declaration constraints. */
CheckResult check_operands(std::span<const OperandDescriptor>,
                           std::span<const FieldView>,
                           std::span<const OperandView>,
                           std::span<const OperandTypeCompatibilityDescriptor>,
                           const Context&);
/** Verify that a resolved instruction retained a valid selected layout tag. */
CheckResult check_operand_layout_tag(std::string_view, uint16_t, size_t,
                                     const Context&);
/** Check target requirements contributed by a selected operand layout. */
CheckResult check_operand_layout_availability(const VariantDescriptor&,
                                              uint16_t, const Context&);
/** Check target requirements of selected dynamic modifier values. */
CheckResult check_modifier_value_availability(
    std::span<const ModifierValueAvailabilityDescriptor>,
    std::span<const ModifierValueView>, const Context&);
/** Check selected modifier values against the variant's semantic value domain. */
CheckResult check_modifier_value_domain(
    std::span<const ModifierValueDomainDescriptor>,
    std::span<const ModifierValueView>, const Context&);
/** Check generated ld/st memory-order and address-space cross constraints. */
CheckResult check_memory_consistency(
    const VariantDescriptor::MemoryConsistencyDescriptor&,
    std::span<const FieldView>, std::span<const OperandView>, const Context&);
/** Check a generated natural-alignment rule when an address is statically known. */
CheckResult check_address_alignment(const AddressAlignmentConstraint&,
                                    std::span<const FieldView>,
                                    std::span<const OperandView>,
                                    const Context&);
/** Check generated PTX memory-vector cross constraints. */
CheckResult check_memory_vector(
    const VariantDescriptor::MemoryVectorDescriptor&,
    std::span<const FieldView>, std::span<const OperandView>, const Context&);
/** Check an immediate operand against an exact generated integer allowlist. */
CheckResult check_immediate_value(
    const VariantDescriptor::ImmediateValueDescriptor&,
    std::span<const OperandView>, const Context&);
/** Check that an immediate operand is divisible by a generated divisor. */
CheckResult check_immediate_multiple_of(
    const VariantDescriptor::ImmediateMultipleOfDescriptor&,
    std::span<const OperandView>, const Context&);
/** Check an immediate operand against inclusive generated integer bounds. */
CheckResult check_immediate_range(
    const VariantDescriptor::ImmediateRangeDescriptor&,
    std::span<const OperandView>, const Context&);

/** Check one generated resolved instruction specialization. */
template <typename T>
CheckResult check(const T& instruction, const Context& context);

}  // namespace ptx_frontend::resolved_ir::checker

// Generated specializations are public checking API, not model API.
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_model.hpp>
#include "resolved_ir_checker.gen.hpp"
