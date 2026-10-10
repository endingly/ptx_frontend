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

/** Project an owned tensor operand into descriptor and coordinate checks. */
OperandView project_tensor_operand(
    std::string_view field_id, const WithLocs<ResolvedTensorOperand>& operand);
/** Recheck a bound transport handle's scalar types, arity, and owned ranges. */
CheckResult check_fabric_handle(const WithLocs<ResolvedFabricHandle>& handle,
                                bool counted, const Context& context);
/** Recheck surface coordinate topology, scalar carriers, and indirect gates. */
CheckResult check_surface_static_payload(
    const SurfaceInstructionDescriptor& descriptor,
    SurfaceSelectedTypes selected_types, const ResolvedSurfaceAccess& access,
    const Context& context);
/** Recheck required surface query brackets and direct/indirect resource kind. */
CheckResult check_surface_query_static_payload(
    const SurfaceInstructionDescriptor& descriptor,
    const ResolvedSurfaceQueryResource& resource, const Context& context);
/** Recheck stack carrier roles, fragment ownership and alignment provenance. */
CheckResult check_stack_static_payload(
    const StackInstructionDescriptor& descriptor,
    const ResolvedStackToken* token,
    const ResolvedLocalAllocationResult* result, const RegOrImm* size,
    const ResolvedImmediate* alignment, const Context& context);
/** Recheck mutable texture lane, type, destination, and layout contracts. */
CheckResult check_texture_static_payload(
    const TextureInstructionDescriptor& descriptor,
    TextureSelectedTypes selected_types, const ResolvedTextureAccess& access,
    const ResolvedTextureResult& result, bool residency_required,
    const Context& context);
/** Recheck source-independent query resource kind and handle carrier facts. */
CheckResult check_texture_query_static_payload(
    const TextureInstructionDescriptor& descriptor,
    const ResolvedTextureQueryResource& resource, const Context& context);
/** Project an owned im2col information pack for generated operand checks. */
OperandView project_tensor_im2col_info(
    std::string_view field_id,
    const WithLocs<ResolvedTensorIm2colInfo>& operand);
/** Recheck owned info provenance, scalar carriers, and generated U16 bounds. */
CheckResult check_tensor_im2col_info(
    const WithLocs<ResolvedTensorOperand>& tensor,
    const WithLocs<ResolvedTensorIm2colInfo>& info,
    std::span<const uint16_t> maximum_values, const Context& context);
/** Recheck scalar pointer metadata on a new tensor read after AST release. */
CheckResult check_tensor_read_addresses(
    const WithLocs<ResolvedTensorOperand>& tensor, const Context& context);
/** Recheck tensor map, destination, and barrier pointers on a load. */
CheckResult check_tensor_read_addresses(
    const WithLocs<ResolvedTensorOperand>& tensor,
    const WithLocs<ResolvedAddress>& dst, const WithLocs<ResolvedAddress>& mbar,
    const Context& context);
/** Reject static negative coordinates for shared-to-global tensor stores. */
CheckResult check_tensor_store_coordinates(const OperandView& operand,
                                           const Context& context);

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
/** Recheck the written tensor CTA-group value and its owned token location. */
CheckResult check_tensor_cta_group(const WithLocs<TensorCtaGroup>&,
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
/** Check exact tensor-map field codes, typed-value conversion, and target gates. */
CheckResult check_tensor_map_replace_rule(std::span<const FieldView>,
                                          std::span<const OperandView>,
                                          const Context&);
/** Require a source-backed, exactly 128-byte tensor-map proxy copy. */
CheckResult check_tensor_map_cp_fenceproxy_rule(std::span<const OperandView>,
                                                const Context&);
/** Require a known tensor-map address-register type to have 32 or 64 bits. */
CheckResult check_tensor_map_address_register_width(
    const WithLocs<ResolvedAddress>&, const Context&);
/** Recheck both owned scalar pointer bases of a tiled tensor reduction. */
CheckResult check_tensor_reduction_addresses(
    const WithLocs<ResolvedTensorOperand>&, const WithLocs<ResolvedAddress>&,
    const Context&);
/** Recheck all five scalar coordinate carriers after syntax ownership ends. */
CheckResult check_tensor_gather_scatter_coordinates(
    const WithLocs<ResolvedTensorOperand>&, const Context&);
/** Recheck a multicast mask's owned scalar carrier and U16 source conversion. */
CheckResult check_tensor_multicast_mask(const WithLocs<RegOrImm>&,
                                        const Context&);
/** Require a written cache hint with preserved source provenance. */
CheckResult check_tensor_cache_hint(const WithLocs<bool>&, const Context&);
/** Recheck the owned scalar B64 policy carrier after syntax ownership ends. */
CheckResult check_tensor_cache_policy(const WithLocs<RegOrImm>&,
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
/** Validate converted Tensor Memory allocation count and slot roles. */
CheckResult check_tcgen_allocation_rule(TcgenAllocationAction,
                                        std::span<const OperandView>,
                                        const Context&);
/** Recheck the owned allocation result-slot register. */
CheckResult check_tcgen_allocation_result_slot(const WithLocs<ResolvedAddress>&,
                                               const Context&);
/** Recheck a TCGEN commit barrier address. */
CheckResult check_tcgen_commit_address(const WithLocs<ResolvedAddress>&,
                                       const Context&);
/** Recheck a TCGEN multicast mask's scalar register domain. */
CheckResult check_tcgen_commit_mask(const WithLocs<ResolvedRegisterRef>&,
                                    const Context&);
/** Enforce register-transfer shape, repeat and reduction cardinality. */
CheckResult check_tcgen_transfer_rule(std::span<const FieldView>,
                                      std::span<const OperandView>, bool,
                                      const Context&);
/** Recheck an owned simple bracket address. */
CheckResult check_tcgen_transfer_address(const WithLocs<TensorMemoryAddress>&,
                                         const Context&);
/** Validate copy shape/multicast and paired destination/source formats. */
CheckResult check_tcgen_copy_rule(std::span<const FieldView>,
                                  std::span<const TcgenCopyShapePair>,
                                  std::span<const uint8_t>, const Context&);
/** Recheck an opaque Table 43 register after operand projection. */
CheckResult check_tcgen_copy_descriptor(const WithLocs<ResolvedRegisterRef>&,
                                        const Context&);
/** Recheck dense Tensor Memory MMA sources before lossy operand views. */
CheckResult check_tcgen_mma_sources(
    const WithLocs<TcgenCtaGroup>&, const WithLocs<TensorMemoryAddress>&,
    const WithLocs<TensorMemoryAddress>*, const WithLocs<ResolvedRegisterRef>*,
    const WithLocs<ResolvedRegisterRef>&, const WithLocs<ResolvedRegisterRef>&,
    const WithLocs<ResolvedRegisterVector>*,
    const WithLocs<ResolvedPredicateSource>&,
    const WithLocs<ResolvedImmediate>*, const WithLocs<TcgenScaleVectorSize>*,
    const WithLocs<TensorMemoryAddress>*, const WithLocs<TensorMemoryAddress>*,
    const WithLocs<bool>*, const WithLocs<TcgenCollectorControl>*,
    const WithLocs<TensorMemoryAddress>*, const WithLocs<ResolvedRegisterRef>*,
    bool, bool, const Context&);
/** Retain the f16 source-check entry point. */
CheckResult check_tcgen_mma_f16_sources(
    const WithLocs<TcgenCtaGroup>&, const WithLocs<TensorMemoryAddress>&,
    const WithLocs<TensorMemoryAddress>*, const WithLocs<ResolvedRegisterRef>*,
    const WithLocs<ResolvedRegisterRef>&, const WithLocs<ResolvedRegisterRef>&,
    const WithLocs<ResolvedRegisterVector>*,
    const WithLocs<ResolvedPredicateSource>&,
    const WithLocs<ResolvedImmediate>*, const Context&);
/** Recheck converted lane alignment for Tensor Memory row shift. */
CheckResult check_tcgen_shift_address(const WithLocs<TensorMemoryAddress>&,
                                      const Context&);
/** Recheck all fragment lanes and reject writable sinks. */
CheckResult check_tcgen_transfer_fragment(
    const WithLocs<ResolvedRegisterVector>&, const Context&);
/** Recheck a separate reduction result's scalar register metadata. */
CheckResult check_tcgen_reduction_result(const WithLocs<ResolvedRegisterRef>&,
                                         const Context&);
/** Reject invalid owned split-offset source structure. */
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
