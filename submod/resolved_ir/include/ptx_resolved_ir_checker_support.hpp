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
  InvalidExecutionPredicate,
  ModifierNotAllowedForLayout,
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
/** Reject mutation of owned matrix topology after resolution or AST release. */
CheckResult check_matrix_metadata(const WithLocs<MatrixInstructionDescriptor>&,
                                  const MatrixInstructionDescriptor&,
                                  const Context&);
/** Check the common predicate-register contract of an instruction guard. */
CheckResult check_execution_predicate(
    const std::optional<WithLocs<ResolvedPredicate>>&, const Context&);
/** Check descriptor-driven operand shape, type, and declaration constraints. */
CheckResult check_operands(std::span<const OperandDescriptor>,
                           std::span<const FieldView>,
                           std::span<const OperandView>,
                           std::span<const OperandTypeCompatibilityDescriptor>,
                           const Context&,
                           const MatrixInstructionDescriptor* matrix = nullptr);
/** Verify that a resolved instruction retained a valid selected layout tag. */
CheckResult check_operand_layout_tag(std::string_view, uint16_t, size_t,
                                     const Context&);
/** Check target requirements contributed by a selected operand layout. */
CheckResult check_operand_layout_availability(const VariantDescriptor&,
                                              uint16_t, const Context&);
/** Check that spelled modifiers are admitted by the selected operand layout. */
CheckResult check_operand_layout_modifiers(const VariantDescriptor&, uint16_t,
                                           std::span<const ModifierValueView>,
                                           const Context&);
/** Check target requirements of selected dynamic modifier values. */
CheckResult check_modifier_value_availability(
    std::span<const ModifierValueAvailabilityDescriptor>,
    std::span<const ModifierValueView>, const Context&);
/** Check selected modifier values against the variant's semantic value domain. */
CheckResult check_modifier_value_domain(
    std::span<const ModifierValueDomainDescriptor>,
    std::span<const ModifierValueView>, const Context&);
/** Check the typed scalar conversion relation selected by a generated `cvt` form. */
CheckResult check_cvt_rule(std::span<const ModifierValueView>,
                           std::span<const OperandView>, const Context&);
/** Check typed fractional bounds and statically known createpolicy range sizes. */
CheckResult check_createpolicy_rule(std::span<const OperandView>,
                                    const Context&);
/** Check typed non-bulk copy source control and statically known zero-fill size. */
CheckResult check_cp_async_rule(std::span<const FieldView>,
                                std::span<const OperandView>, const Context&);
/** Check generated ld/st memory-order and address-space cross constraints. */
CheckResult check_memory_consistency(
    const VariantDescriptor::MemoryConsistencyDescriptor&,
    std::span<const FieldView>, std::span<const OperandView>, const Context&);
/** Check independent atomic address suffix, provenance, and target floors. */
CheckResult check_atomic_qualifiers(
    const VariantDescriptor::AtomicAddressQualifierDescriptor&,
    const WithLocs<AtomicAddressQualifier>&, std::span<const FieldView>,
    std::span<const OperandView>, const Context&);
/** MMIO release reductions require system scope. */
CheckResult check_red_async_release_qualifiers(std::span<const FieldView>,
                                               const Context&);
/** Gate the 32-bit st.bulk size register at its PTX 9.0 introduction. */
CheckResult check_st_bulk_size_width(std::span<const OperandView>,
                                     const Context&);
/** Validate converted Tensor Memory allocation count, address, and slot roles. */
CheckResult check_tcgen_allocation_rule(TcgenAllocationAction,
                                        std::span<const OperandView>,
                                        const Context&);
/** Recheck the owned allocation result-slot register before view projection. */
CheckResult check_tcgen_allocation_result_slot(const WithLocs<ResolvedAddress>&,
                                               const Context&);
/** Recheck the owned TCGEN commit mbarrier address and its known shared role. */
CheckResult check_tcgen_commit_address(const WithLocs<ResolvedAddress>&,
                                       const Context&);
/** Recheck the owned multicast mask's scalar 16-bit register domain. */
CheckResult check_tcgen_commit_mask(const WithLocs<ResolvedRegisterRef>&,
                                    const Context&);
/** Enforce exact register-transfer shape, repeat, and reduction cardinality. */
CheckResult check_tcgen_transfer_rule(std::span<const FieldView>,
                                      std::span<const OperandView>, bool,
                                      const Context&);
/** Recheck the owned simple bracket address after checker view projection. */
CheckResult check_tcgen_transfer_address(const WithLocs<TensorMemoryAddress>&,
                                         const Context&);
/** Validate emitted copy shape/multicast and paired destination/source formats. */
CheckResult check_tcgen_copy_rule(std::span<const FieldView>,
                                  std::span<const TcgenCopyShapePair>,
                                  std::span<const uint8_t>, const Context&);
/** Recheck the owned opaque Table 43 register after lossy operand projection. */
CheckResult check_tcgen_copy_descriptor(const WithLocs<ResolvedRegisterRef>&,
                                        const Context&);
/** Recheck selected dense f16/tf32 MMA sources before lossy operand views.
 *
 * Exactly one A role is supplied. Optional mask and scale are represented by
 * null pointers only when the corresponding source operand is absent.
 */
CheckResult check_tcgen_mma_sources(
    const WithLocs<TcgenCtaGroup>&, const WithLocs<TensorMemoryAddress>&,
    const WithLocs<TensorMemoryAddress>*, const WithLocs<ResolvedRegisterRef>*,
    const WithLocs<ResolvedRegisterRef>&, const WithLocs<ResolvedRegisterRef>&,
    const WithLocs<ResolvedRegisterVector>*,
    const WithLocs<ResolvedPredicateSource>&,
    const WithLocs<ResolvedImmediate>*, const Context&);
/** Preserve the accepted f16 source-check entry point. */
CheckResult check_tcgen_mma_f16_sources(
    const WithLocs<TcgenCtaGroup>&, const WithLocs<TensorMemoryAddress>&,
    const WithLocs<TensorMemoryAddress>*, const WithLocs<ResolvedRegisterRef>*,
    const WithLocs<ResolvedRegisterRef>&, const WithLocs<ResolvedRegisterRef>&,
    const WithLocs<ResolvedRegisterVector>*,
    const WithLocs<ResolvedPredicateSource>&,
    const WithLocs<ResolvedImmediate>*, const Context&);
/** Recheck known converted lane alignment for Tensor Memory row shift. */
CheckResult check_tcgen_shift_address(const WithLocs<TensorMemoryAddress>&,
                                      const Context&);
/** Recheck all actual fragment register lanes and reject writable sinks. */
CheckResult check_tcgen_transfer_fragment(
    const WithLocs<ResolvedRegisterVector>&, const Context&);
/** Recheck a separate reduction result's scalar register metadata. */
CheckResult check_tcgen_reduction_result(const WithLocs<ResolvedRegisterRef>&,
                                         const Context&);
/** Reject invalid owned split-offset source-kind structure. */
CheckResult check_tcgen_half_split_offset(const WithLocs<TcgenHalfSplitOffset>&,
                                          const Context&);
/** Reject `.unified` address suffixes not explicitly admitted by a variant. */
CheckResult check_unified_address_suffix(const VariantDescriptor&,
                                         std::span<const FieldView>,
                                         std::span<const OperandView>,
                                         const Context&);
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
