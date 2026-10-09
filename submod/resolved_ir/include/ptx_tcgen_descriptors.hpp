#pragma once

#include <concepts>
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

/** Borrowed non-block-scaled sparse MMA roles from one exact owned form.
 *
 * The pointers remain valid only while the owning instruction lives.
 * Descriptor and sparse metadata contents stay opaque; the metadata pointer
 * identifies a typed source address, not authenticated live indices.
 */
struct TcgenMmaSparseView {
  /** Exact source kind, independent of live instruction-descriptor bits. */
  TcgenMmaKind kind;
  /** CTA group copied from the written qualifier. */
  TcgenCtaGroup group;
  /** Destination Tensor Memory address. */
  const TensorMemoryAddress* d;
  /** Shared A descriptor, absent for Tensor Memory A. */
  std::optional<TcgenMmaSharedDescriptorView> a_shared;
  /** Tensor Memory A address, absent for shared A. */
  const TensorMemoryAddress* a_tmem;
  /** Shared B descriptor. */
  TcgenMmaSharedDescriptorView b;
  /** Required sparse metadata Tensor Memory address. */
  const TensorMemoryAddress* metadata;
  /** Opaque instruction descriptor register. */
  TcgenInstructionDescriptorView instruction;
  /** Required input-D predicate source. */
  const ResolvedPredicateSource* enable_d;
  /** Optional group-sized output-lane mask. */
  const ResolvedRegisterVector* disable_output_lane;
  /** Optional source D-scale immediate, only for f16 or tf32. */
  const ResolvedImmediate* scale_d;
};

namespace tcgen_sparse_detail {
/** The four supported non-block-scaled sparse final classes. */
template <typename Form>
concept SparseMmaForm = std::same_as<Form, Tcgen05MmaSpF16> ||
                        std::same_as<Form, Tcgen05MmaSpTf32> ||
                        std::same_as<Form, Tcgen05MmaSpF8f6f4> ||
                        std::same_as<Form, Tcgen05MmaSpI8>;

/** Select the common role topology without dispatching on member names. */
template <SparseMmaForm Form>
std::optional<TcgenMmaSparseView> borrow(const Form& mma,
                                         TcgenMmaKind kind) noexcept {
  constexpr bool scaled_kind = std::same_as<Form, Tcgen05MmaSpF16> ||
                               std::same_as<Form, Tcgen05MmaSpTf32>;
  constexpr unsigned layout_count = scaled_kind ? 8 : 4;
  if (mma.operand_layout.value >= layout_count)
    return std::nullopt;
  const auto layout = mma.operand_layout.value;
  const bool shared_a = layout < (scaled_kind ? 4U : 2U);
  const bool masked = (layout & (scaled_kind ? 2U : 1U)) != 0;
  const bool scaled = scaled_kind && (layout & 1U) != 0;
  if (mma.a_register.has_value() != shared_a ||
      mma.a_tcgen_bracketed_address.has_value() == shared_a ||
      mma.disable_output_lane.has_value() != masked)
    return std::nullopt;
  if constexpr (scaled_kind) {
    if (mma.scale_input_d.has_value() != scaled)
      return std::nullopt;
  }
  TcgenMmaSparseView view{
      .kind = kind,
      .group = mma.cta_group.value,
      .d = &mma.d.value,
      .a_shared = std::nullopt,
      .a_tmem = nullptr,
      .b = {&mma.b.value, MatrixFragmentRole::B},
      .metadata = &mma.sp_meta.value,
      .instruction = {&mma.idesc.value},
      .enable_d = &mma.enable_input_d.value,
      .disable_output_lane = masked ? &mma.disable_output_lane->value : nullptr,
      .scale_d = nullptr};
  if constexpr (scaled_kind) {
    if (scaled)
      view.scale_d = &mma.scale_input_d->value;
  }
  if (shared_a)
    view.a_shared = TcgenMmaSharedDescriptorView{&mma.a_register->value,
                                                 MatrixFragmentRole::A};
  else
    view.a_tmem = &mma.a_tcgen_bracketed_address->value;
  return view;
}
}  // namespace tcgen_sparse_detail

/** Borrow sparse roles only when an exact supported class and layout agree. */
inline std::optional<TcgenMmaSparseView> tcgen_mma_sparse_view(
    const Instruction& instruction) noexcept {
  if (const auto* form = dynamic_cast<const Tcgen05MmaSpF16*>(&instruction))
    return tcgen_sparse_detail::borrow(*form, TcgenMmaKind::F16);
  if (const auto* form = dynamic_cast<const Tcgen05MmaSpTf32*>(&instruction))
    return tcgen_sparse_detail::borrow(*form, TcgenMmaKind::Tf32);
  if (const auto* form = dynamic_cast<const Tcgen05MmaSpF8f6f4*>(&instruction))
    return tcgen_sparse_detail::borrow(*form, TcgenMmaKind::F8F6F4);
  if (const auto* form = dynamic_cast<const Tcgen05MmaSpI8*>(&instruction))
    return tcgen_sparse_detail::borrow(*form, TcgenMmaKind::I8);
  return std::nullopt;
}

/** Borrowed sparse block-scale roles; all pointers live with the owning form. */
struct TcgenMmaSparseMxView {
  /** Exact encoded kind selected by the source class. */
  TcgenMmaKind kind;
  /** Typed CTA group from the written qualifier. */
  TcgenCtaGroup group;
  /** Written selector, including its omitted-source provenance. */
  const WithLocs<TcgenScaleVectorSize>* scale_selector;
  /** Destination Tensor Memory address. */
  const TensorMemoryAddress* d;
  /** Shared A descriptor, present only for layout zero. */
  std::optional<TcgenMmaSharedDescriptorView> a_shared;
  /** Tensor Memory A address, present only for layout one. */
  const TensorMemoryAddress* a_tmem;
  /** Shared B descriptor. */
  TcgenMmaSharedDescriptorView b;
  /** Mandatory sparse metadata Tensor Memory address. */
  const TensorMemoryAddress* metadata;
  /** Opaque instruction descriptor register. */
  TcgenInstructionDescriptorView instruction;
  /** Scale A Tensor Memory address. */
  const TensorMemoryAddress* scale_a;
  /** Scale B Tensor Memory address. */
  const TensorMemoryAddress* scale_b;
  /** Input-D predicate source. */
  const ResolvedPredicateSource* enable_d;
};

namespace tcgen_sparse_mx_detail {
/** The three supported sparse block-scale final classes. */
template <typename Form>
concept SparseMxForm = std::same_as<Form, Tcgen05MmaSpMxf8f6f4> ||
                       std::same_as<Form, Tcgen05MmaSpMxf4> ||
                       std::same_as<Form, Tcgen05MmaSpMxf4nvf4>;

/** Select common roles only when the exact layout and optional A agree. */
template <SparseMxForm Form>
std::optional<TcgenMmaSparseMxView> borrow(const Form& mma,
                                           TcgenMmaKind kind) noexcept {
  if (mma.operand_layout.value >= 2)
    return std::nullopt;
  const bool shared_a = mma.operand_layout.value == 0;
  if (mma.a_register.has_value() != shared_a ||
      mma.a_tcgen_bracketed_address.has_value() == shared_a)
    return std::nullopt;
  TcgenMmaSparseMxView view{.kind = kind,
                            .group = mma.cta_group.value,
                            .scale_selector = &mma.scale_vector_size,
                            .d = &mma.d.value,
                            .a_shared = std::nullopt,
                            .a_tmem = nullptr,
                            .b = {&mma.b.value, MatrixFragmentRole::B},
                            .metadata = &mma.sp_meta.value,
                            .instruction = {&mma.idesc.value},
                            .scale_a = &mma.scale_a.value,
                            .scale_b = &mma.scale_b.value,
                            .enable_d = &mma.enable_input_d.value};
  if (shared_a)
    view.a_shared = TcgenMmaSharedDescriptorView{&mma.a_register->value,
                                                 MatrixFragmentRole::A};
  else
    view.a_tmem = &mma.a_tcgen_bracketed_address->value;
  return view;
}
}  // namespace tcgen_sparse_mx_detail

/** Borrow only one exact sparse MX class and its selected placement layout. */
inline std::optional<TcgenMmaSparseMxView> tcgen_mma_sparse_mx_view(
    const Instruction& instruction) noexcept {
  if (const auto* form =
          dynamic_cast<const Tcgen05MmaSpMxf8f6f4*>(&instruction))
    return tcgen_sparse_mx_detail::borrow(*form, TcgenMmaKind::MxF8F6F4);
  if (const auto* form = dynamic_cast<const Tcgen05MmaSpMxf4*>(&instruction))
    return tcgen_sparse_mx_detail::borrow(*form, TcgenMmaKind::MxF4);
  if (const auto* form =
          dynamic_cast<const Tcgen05MmaSpMxf4nvf4*>(&instruction))
    return tcgen_sparse_mx_detail::borrow(*form, TcgenMmaKind::MxF4NvF4);
  return std::nullopt;
}

}  // namespace ptx_frontend::resolved_ir
