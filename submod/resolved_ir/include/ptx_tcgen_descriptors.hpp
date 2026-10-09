#pragma once

#include <ptx_frontend/resolved_ir/model/tensor_memory/tcgen05.gen.hpp>
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

/** Borrow dense f16 roles only from an exact class with matching layout payload. */
inline std::optional<TcgenMmaF16View> tcgen_mma_f16_view(
    const Instruction& instruction) noexcept {
  const auto* mma = dynamic_cast<const Tcgen05MmaF16*>(&instruction);
  if (!mma || mma->operand_layout.value >= 8)
    return std::nullopt;
  const auto layout = mma->operand_layout.value;
  const bool shared_a = layout < 4;
  const bool scaled = (layout & 1U) != 0;
  const bool masked = (layout & 2U) != 0;
  if (mma->a_register.has_value() != shared_a ||
      mma->a_tcgen_bracketed_address.has_value() == shared_a ||
      mma->scale_input_d.has_value() != scaled ||
      mma->disable_output_lane.has_value() != masked)
    return std::nullopt;
  TcgenMmaF16View view{
      .group = mma->cta_group.value,
      .d = &mma->d.value,
      .a_shared = std::nullopt,
      .a_tmem = nullptr,
      .b = {&mma->b.value, MatrixFragmentRole::B},
      .instruction = {&mma->idesc.value},
      .enable_d = &mma->enable_input_d.value,
      .disable_output_lane =
          masked ? &mma->disable_output_lane->value : nullptr,
      .scale_d = scaled ? &mma->scale_input_d->value : nullptr};
  if (shared_a)
    view.a_shared = TcgenMmaSharedDescriptorView{&mma->a_register->value,
                                                 MatrixFragmentRole::A};
  else
    view.a_tmem = &mma->a_tcgen_bracketed_address->value;
  return view;
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

/** Borrow dense tf32 roles only from an exact class with matching layout payload. */
inline std::optional<TcgenMmaTf32View> tcgen_mma_tf32_view(
    const Instruction& instruction) noexcept {
  const auto* mma = dynamic_cast<const Tcgen05MmaTf32*>(&instruction);
  if (!mma || mma->operand_layout.value >= 8)
    return std::nullopt;
  const auto layout = mma->operand_layout.value;
  const bool shared_a = layout < 4;
  const bool scaled = (layout & 1U) != 0;
  const bool masked = (layout & 2U) != 0;
  if (mma->a_register.has_value() != shared_a ||
      mma->a_tcgen_bracketed_address.has_value() == shared_a ||
      mma->scale_input_d.has_value() != scaled ||
      mma->disable_output_lane.has_value() != masked)
    return std::nullopt;
  TcgenMmaTf32View view{
      .group = mma->cta_group.value,
      .d = &mma->d.value,
      .a_shared = std::nullopt,
      .a_tmem = nullptr,
      .b = {&mma->b.value, MatrixFragmentRole::B},
      .instruction = {&mma->idesc.value},
      .enable_d = &mma->enable_input_d.value,
      .disable_output_lane =
          masked ? &mma->disable_output_lane->value : nullptr,
      .scale_d = scaled ? &mma->scale_input_d->value : nullptr};
  if (shared_a)
    view.a_shared = TcgenMmaSharedDescriptorView{&mma->a_register->value,
                                                 MatrixFragmentRole::A};
  else
    view.a_tmem = &mma->a_tcgen_bracketed_address->value;
  return view;
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

/** Borrow dense i8 roles only from an exact class with matching layout payload. */
inline std::optional<TcgenMmaI8View> tcgen_mma_i8_view(
    const Instruction& instruction) noexcept {
  const auto* mma = dynamic_cast<const Tcgen05MmaI8*>(&instruction);
  if (!mma || mma->operand_layout.value >= 4)
    return std::nullopt;
  const auto layout = mma->operand_layout.value;
  const bool shared_a = layout < 2;
  const bool masked = (layout & 1U) != 0;
  if (mma->a_register.has_value() != shared_a ||
      mma->a_tcgen_bracketed_address.has_value() == shared_a ||
      mma->disable_output_lane.has_value() != masked)
    return std::nullopt;
  TcgenMmaI8View view{.group = mma->cta_group.value,
                      .d = &mma->d.value,
                      .a_shared = std::nullopt,
                      .a_tmem = nullptr,
                      .b = {&mma->b.value, MatrixFragmentRole::B},
                      .instruction = {&mma->idesc.value},
                      .enable_d = &mma->enable_input_d.value,
                      .disable_output_lane =
                          masked ? &mma->disable_output_lane->value : nullptr};
  if (shared_a)
    view.a_shared = TcgenMmaSharedDescriptorView{&mma->a_register->value,
                                                 MatrixFragmentRole::A};
  else
    view.a_tmem = &mma->a_tcgen_bracketed_address->value;
  return view;
}

/** Borrowed dense f8f6f4 MMA operands selected from one exact owned form.
 *
 * Pointers remain valid only while the owning instruction lives. Descriptor
 * register contents stay opaque, and source f8f6f4 has no D-scale operand.
 */
struct TcgenMmaF8F6F4View {
  /** Typed group copied from the written qualifier. */
  TcgenCtaGroup group;
  /** Destination Tensor Memory address borrowed from the instruction. */
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

/** Borrow f8f6f4 roles only from the exact class and matching layout fields. */
inline std::optional<TcgenMmaF8F6F4View> tcgen_mma_f8f6f4_view(
    const Instruction& instruction) noexcept {
  const auto* mma = dynamic_cast<const Tcgen05MmaF8f6f4*>(&instruction);
  if (!mma || mma->operand_layout.value >= 4)
    return std::nullopt;
  const auto layout = mma->operand_layout.value;
  const bool shared_a = layout < 2;
  const bool masked = (layout & 1U) != 0;
  if (mma->a_register.has_value() != shared_a ||
      mma->a_tcgen_bracketed_address.has_value() == shared_a ||
      mma->disable_output_lane.has_value() != masked)
    return std::nullopt;
  TcgenMmaF8F6F4View view{
      .group = mma->cta_group.value,
      .d = &mma->d.value,
      .a_shared = std::nullopt,
      .a_tmem = nullptr,
      .b = {&mma->b.value, MatrixFragmentRole::B},
      .instruction = {&mma->idesc.value},
      .enable_d = &mma->enable_input_d.value,
      .disable_output_lane =
          masked ? &mma->disable_output_lane->value : nullptr};
  if (shared_a)
    view.a_shared = TcgenMmaSharedDescriptorView{&mma->a_register->value,
                                                 MatrixFragmentRole::A};
  else
    view.a_tmem = &mma->a_tcgen_bracketed_address->value;
  return view;
}

/** Borrowed dense MX8 roles selected from one exact owned block-scale form.
 *  Scale addresses remain opaque Tensor Memory operands. The selector records
 *  source omission in both its typed value and its location span.
 */
struct TcgenMmaMx8View {
  /** Group selected by the written qualifier. */
  TcgenCtaGroup group;
  /** Written selector, including an omitted-source state. */
  const WithLocs<TcgenScaleVectorSize>* scale_selector;
  /** Destination Tensor Memory address borrowed from the instruction. */
  const TensorMemoryAddress* d;
  /** Shared A descriptor, absent for Tensor Memory A. */
  std::optional<TcgenMmaSharedDescriptorView> a_shared;
  /** Tensor Memory A address, absent for shared A. */
  const TensorMemoryAddress* a_tmem;
  /** Shared B descriptor register. */
  TcgenMmaSharedDescriptorView b;
  /** Instruction descriptor register; its live value is opaque. */
  TcgenInstructionDescriptorView instruction;
  /** Scale A Tensor Memory address, not a decoded sub-column offset. */
  const TensorMemoryAddress* scale_a;
  /** Scale B Tensor Memory address, not a decoded sub-column offset. */
  const TensorMemoryAddress* scale_b;
  /** Required predicate source. */
  const ResolvedPredicateSource* enable_d;
};

/** Borrow MX8 roles only when the exact class and placement layout agree. */
inline std::optional<TcgenMmaMx8View> tcgen_mma_mx8_view(
    const Instruction& instruction) noexcept {
  const auto* mma = dynamic_cast<const Tcgen05MmaMxf8f6f4*>(&instruction);
  if (!mma || mma->operand_layout.value >= 2)
    return std::nullopt;
  const bool shared_a = mma->operand_layout.value == 0;
  if (mma->a_register.has_value() != shared_a ||
      mma->a_tcgen_bracketed_address.has_value() == shared_a)
    return std::nullopt;
  TcgenMmaMx8View view{.group = mma->cta_group.value,
                       .scale_selector = &mma->scale_vector_size,
                       .d = &mma->d.value,
                       .a_shared = std::nullopt,
                       .a_tmem = nullptr,
                       .b = {&mma->b.value, MatrixFragmentRole::B},
                       .instruction = {&mma->idesc.value},
                       .scale_a = &mma->scale_a.value,
                       .scale_b = &mma->scale_b.value,
                       .enable_d = &mma->enable_input_d.value};
  if (shared_a)
    view.a_shared = TcgenMmaSharedDescriptorView{&mma->a_register->value,
                                                 MatrixFragmentRole::A};
  else
    view.a_tmem = &mma->a_tcgen_bracketed_address->value;
  return view;
}

/** Reuse the common borrowed block-scale role shape for dense MX4. */
using TcgenMmaMx4View = TcgenMmaMx8View;

/** Borrow MX4 roles only when the exact class and placement layout agree. */
inline std::optional<TcgenMmaMx4View> tcgen_mma_mx4_view(
    const Instruction& instruction) noexcept {
  const auto* mma = dynamic_cast<const Tcgen05MmaMxf4*>(&instruction);
  if (!mma || mma->operand_layout.value >= 2)
    return std::nullopt;
  const bool shared_a = mma->operand_layout.value == 0;
  if (mma->a_register.has_value() != shared_a ||
      mma->a_tcgen_bracketed_address.has_value() == shared_a)
    return std::nullopt;
  TcgenMmaMx4View view{.group = mma->cta_group.value,
                       .scale_selector = &mma->scale_vector_size,
                       .d = &mma->d.value,
                       .a_shared = std::nullopt,
                       .a_tmem = nullptr,
                       .b = {&mma->b.value, MatrixFragmentRole::B},
                       .instruction = {&mma->idesc.value},
                       .scale_a = &mma->scale_a.value,
                       .scale_b = &mma->scale_b.value,
                       .enable_d = &mma->enable_input_d.value};
  if (shared_a)
    view.a_shared = TcgenMmaSharedDescriptorView{&mma->a_register->value,
                                                 MatrixFragmentRole::A};
  else
    view.a_tmem = &mma->a_tcgen_bracketed_address->value;
  return view;
}

/** Reuse the borrowed block-scale role shape for dense MX NV four-bit. */
using TcgenMmaMxNvView = TcgenMmaMx8View;

/** Borrow MX NV roles only from its exact source class and placement layout. */
inline std::optional<TcgenMmaMxNvView> tcgen_mma_mxnv_view(
    const Instruction& instruction) noexcept {
  const auto* mma = dynamic_cast<const Tcgen05MmaMxf4nvf4*>(&instruction);
  if (!mma || mma->operand_layout.value >= 2)
    return std::nullopt;
  const bool shared_a = mma->operand_layout.value == 0;
  if (mma->a_register.has_value() != shared_a ||
      mma->a_tcgen_bracketed_address.has_value() == shared_a)
    return std::nullopt;
  TcgenMmaMxNvView view{.group = mma->cta_group.value,
                        .scale_selector = &mma->scale_vector_size,
                        .d = &mma->d.value,
                        .a_shared = std::nullopt,
                        .a_tmem = nullptr,
                        .b = {&mma->b.value, MatrixFragmentRole::B},
                        .instruction = {&mma->idesc.value},
                        .scale_a = &mma->scale_a.value,
                        .scale_b = &mma->scale_b.value,
                        .enable_d = &mma->enable_input_d.value};
  if (shared_a)
    view.a_shared = TcgenMmaSharedDescriptorView{&mma->a_register->value,
                                                 MatrixFragmentRole::A};
  else
    view.a_tmem = &mma->a_tcgen_bracketed_address->value;
  return view;
}

}  // namespace ptx_frontend::resolved_ir
