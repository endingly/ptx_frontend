#pragma once

#include <ptx_frontend/resolved_ir/model/tensor_memory/tcgen05/model.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_foundation.hpp>
#include <ptx_frontend/resolved_ir/tcgen_descriptor_domains.gen.hpp>
#include <type_traits>

namespace ptx_frontend::resolved_ir {

/** Borrowed future TCGEN instruction-descriptor source role.
 *  A selected owning MMA form must establish this role; this header supplies no
 *  source factory or carrier policy. It does not authenticate known bits.
 */
struct TcgenInstructionDescriptorView {
  /** Register reference borrowed from the owning resolved instruction. */
  const ResolvedRegisterRef* source;
};

/** Borrowed future TCGEN zero-column descriptor source role.
 *  A selected owning MMA form must establish this role; this header supplies no
 *  source factory or carrier policy. It does not authenticate known bits.
 */
struct TcgenZeroColumnDescriptorView {
  /** Register reference borrowed from the owning resolved instruction. */
  const ResolvedRegisterRef* source;
};

/** A selected opaque Table 43 register with an MMA-specific A or B role. */
struct TcgenMmaSharedDescriptorView {
  /** Register borrowed from the owning selected MMA payload. */
  const ResolvedRegisterRef* source;
  /** Logical shared-matrix operand position, not a decoded word property. */
  MatrixFragmentRole role;
};

/** Borrowed dense f16 MMA operands selected from one owned instruction.
 *
 * Every pointer remains valid only while the owning instruction and selected
 * variant payload live. A register's descriptor bits are opaque; this view
 * neither decodes nor authenticates them. Check the owning instruction before
 * treating register metadata as valid.
 */
struct TcgenMmaF16View {
  /** Typed source group, copied from the written qualifier. */
  TcgenCtaGroup group;
  /** Destination Tensor Memory address, borrowed from the selected payload. */
  const TensorMemoryAddress* d;
  /** Shared A descriptor register, absent when A is in Tensor Memory. */
  std::optional<TcgenMmaSharedDescriptorView> a_shared;
  /** Tensor Memory A address, absent when A is a shared descriptor. */
  const TensorMemoryAddress* a_tmem;
  /** Shared B descriptor register. */
  TcgenMmaSharedDescriptorView b;
  /** Instruction descriptor register, distinct from a Table 43 shared word. */
  TcgenInstructionDescriptorView instruction;
  /** Required predicate source, retaining negation or constant truth. */
  const ResolvedPredicateSource* enable_d;
  /** Optional output-lane register mask. */
  const ResolvedRegisterVector* disable_output_lane;
  /** Optional D-scale source immediate with original integer bits. */
  const ResolvedImmediate* scale_d;
};

/** Return the selected dense f16 MMA role view, if tag and storage agree. */
inline std::optional<TcgenMmaF16View> tcgen_mma_f16_view(
    const Tcgen05& instruction) noexcept {
  const auto* mma = std::get_if<Tcgen05::MmaF16>(&instruction.variant);
  if (!mma || mma->operands.valueless_by_exception() ||
      mma->operand_layout.value != mma->operands.index())
    return std::nullopt;
  return std::visit(
      [&](const auto& payload) -> std::optional<TcgenMmaF16View> {
        TcgenMmaF16View view{.group = mma->cta_group.value,
                             .d = &payload.d.value,
                             .a_shared = std::nullopt,
                             .a_tmem = nullptr,
                             .b = {&payload.b.value, MatrixFragmentRole::B},
                             .instruction = {&payload.idesc.value},
                             .enable_d = &payload.enable_input_d.value,
                             .disable_output_lane = nullptr,
                             .scale_d = nullptr};
        if constexpr (std::is_same_v<
                          std::remove_cvref_t<decltype(payload.a.value)>,
                          ResolvedRegisterRef>)
          view.a_shared = TcgenMmaSharedDescriptorView{&payload.a.value,
                                                       MatrixFragmentRole::A};
        else
          view.a_tmem = &payload.a.value;
        if constexpr (requires { payload.disable_output_lane; })
          view.disable_output_lane = &payload.disable_output_lane.value;
        if constexpr (requires { payload.scale_input_d; })
          view.scale_d = &payload.scale_input_d.value;
        return view;
      },
      mma->operands);
}

/** Borrowed dense tf32 MMA operands selected from one owned instruction.
 *
 * Every pointer remains valid only while the owning instruction and selected
 * variant payload live. A register's descriptor bits are opaque; this view
 * neither decodes nor authenticates them. Check the owning instruction before
 * treating register metadata as valid.
 */
struct TcgenMmaTf32View {
  /** Typed source group, copied from the written qualifier. */
  TcgenCtaGroup group;
  /** Destination Tensor Memory address, borrowed from the selected payload. */
  const TensorMemoryAddress* d;
  /** Shared A descriptor register, absent when A is in Tensor Memory. */
  std::optional<TcgenMmaSharedDescriptorView> a_shared;
  /** Tensor Memory A address, absent when A is a shared descriptor. */
  const TensorMemoryAddress* a_tmem;
  /** Shared B descriptor register. */
  TcgenMmaSharedDescriptorView b;
  /** Instruction descriptor register, distinct from a Table 43 shared word. */
  TcgenInstructionDescriptorView instruction;
  /** Required predicate source, retaining negation or constant truth. */
  const ResolvedPredicateSource* enable_d;
  /** Optional output-lane register mask. */
  const ResolvedRegisterVector* disable_output_lane;
  /** Optional D-scale source immediate with original integer bits. */
  const ResolvedImmediate* scale_d;
};

/** Return the selected dense tf32 MMA role view, if tag and storage agree. */
inline std::optional<TcgenMmaTf32View> tcgen_mma_tf32_view(
    const Tcgen05& instruction) noexcept {
  const auto* mma = std::get_if<Tcgen05::MmaTf32>(&instruction.variant);
  if (!mma || mma->operands.valueless_by_exception() ||
      mma->operand_layout.value != mma->operands.index())
    return std::nullopt;
  return std::visit(
      [&](const auto& payload) -> std::optional<TcgenMmaTf32View> {
        TcgenMmaTf32View view{.group = mma->cta_group.value,
                              .d = &payload.d.value,
                              .a_shared = std::nullopt,
                              .a_tmem = nullptr,
                              .b = {&payload.b.value, MatrixFragmentRole::B},
                              .instruction = {&payload.idesc.value},
                              .enable_d = &payload.enable_input_d.value,
                              .disable_output_lane = nullptr,
                              .scale_d = nullptr};
        if constexpr (std::is_same_v<
                          std::remove_cvref_t<decltype(payload.a.value)>,
                          ResolvedRegisterRef>)
          view.a_shared = TcgenMmaSharedDescriptorView{&payload.a.value,
                                                       MatrixFragmentRole::A};
        else
          view.a_tmem = &payload.a.value;
        if constexpr (requires { payload.disable_output_lane; })
          view.disable_output_lane = &payload.disable_output_lane.value;
        if constexpr (requires { payload.scale_input_d; })
          view.scale_d = &payload.scale_input_d.value;
        return view;
      },
      mma->operands);
}

/** Borrowed dense i8 MMA operands selected from one owned instruction.
 *
 * Pointers remain valid only while the owning instruction and selected
 * payload live. Descriptor register contents stay opaque, and source i8 has
 * no D-scale operand. Check the owned instruction before trusting metadata.
 */
struct TcgenMmaI8View {
  /** Typed group copied from the written qualifier. */
  TcgenCtaGroup group;
  /** Destination Tensor Memory address borrowed from the payload. */
  const TensorMemoryAddress* d;
  /** Shared A descriptor, absent when A is in Tensor Memory. */
  std::optional<TcgenMmaSharedDescriptorView> a_shared;
  /** Tensor Memory A address, absent when A is shared. */
  const TensorMemoryAddress* a_tmem;
  /** Shared B descriptor register. */
  TcgenMmaSharedDescriptorView b;
  /** Instruction descriptor register, distinct from a shared word. */
  TcgenInstructionDescriptorView instruction;
  /** Required predicate source, retaining negation or constant truth. */
  const ResolvedPredicateSource* enable_d;
  /** Optional group-sized output-lane bit-register mask. */
  const ResolvedRegisterVector* disable_output_lane;
};

/** Return the selected i8 role view only when tag and storage agree. */
inline std::optional<TcgenMmaI8View> tcgen_mma_i8_view(
    const Tcgen05& instruction) noexcept {
  const auto* mma = std::get_if<Tcgen05::MmaI8>(&instruction.variant);
  if (!mma || mma->operands.valueless_by_exception() ||
      mma->operand_layout.value != mma->operands.index())
    return std::nullopt;
  return std::visit(
      [&](const auto& payload) -> std::optional<TcgenMmaI8View> {
        TcgenMmaI8View view{.group = mma->cta_group.value,
                            .d = &payload.d.value,
                            .a_shared = std::nullopt,
                            .a_tmem = nullptr,
                            .b = {&payload.b.value, MatrixFragmentRole::B},
                            .instruction = {&payload.idesc.value},
                            .enable_d = &payload.enable_input_d.value,
                            .disable_output_lane = nullptr};
        if constexpr (std::is_same_v<
                          std::remove_cvref_t<decltype(payload.a.value)>,
                          ResolvedRegisterRef>)
          view.a_shared = TcgenMmaSharedDescriptorView{&payload.a.value,
                                                       MatrixFragmentRole::A};
        else
          view.a_tmem = &payload.a.value;
        if constexpr (requires { payload.disable_output_lane; })
          view.disable_output_lane = &payload.disable_output_lane.value;
        return view;
      },
      mma->operands);
}

}  // namespace ptx_frontend::resolved_ir
