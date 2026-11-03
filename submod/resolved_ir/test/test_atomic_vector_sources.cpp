#include <gtest/gtest.h>
#include <iostream>

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include "ptx_resolved_ir_private.hpp"

#if defined(PTX_ATOMIC_VALUE_CHECKER_WRAP)
namespace ptx_frontend::resolved_ir::atomic_value_test_wrap {
/** Expected actual member and independent source facts for one synchronous generated check. */
struct Capture {
  /** Borrowed only while the current exact-class instruction remains live. */
  const WithLocs<ResolvedValueVector>* member = nullptr;
  /** Independent logical and physical types from the selected finite fixture. */
  ScalarType logical = ScalarType::Invalid;
  ScalarType physical = ScalarType::Invalid;
  /** Independently counted scalar lanes and exact literal word; no conversion oracle here. */
  size_t arity = 0;
  uint64_t bits = 0;
  /** Register-only, all-immediate, or first-immediate mixed brace mode. */
  unsigned mode = 0;
  /** Independently found written scalar ranges, owned by the caller during checking. */
  std::vector<SourceRange> ranges;
  /** Copied observations, never retained callback views. */
  size_t calls = 0;
  bool valid = true;
};
/** One thread's synchronous test observation; never a production observer hook. */
inline thread_local Capture* active = nullptr;
/** Restore an enclosing observation even if the real checker throws. */
class Scope {
  /** Previous stack-owned capture restored before its enclosing scope resumes. */
  Capture* previous_;

 public:
  /** Borrow a live capture only for this stack scope. */
  explicit Scope(Capture& value) : previous_(active) { active = &value; }
  /** Release every capture borrow when the synchronous call finishes. */
  ~Scope() { active = previous_; }
};
/** Independently locate each source-brace leaf in the ASCII fixture, not in owned IR. */
inline std::vector<SourceRange> ranges(std::string_view text) {
  const auto address = text.find("[data],");
  const auto open =
      address == std::string_view::npos ? address : text.find('{', address);
  if (open == std::string_view::npos)
    throw std::logic_error("Missing actual source brace.");
  std::vector<SourceRange> result;
  size_t begin = open + 1;
  for (;;) {
    const auto end = text.find_first_of(",}", begin);
    if (end == std::string_view::npos || end == begin)
      throw std::logic_error("Missing scalar source leaf.");
    SourcePos position{1, 1};
    for (const char character : text.substr(0, begin)) {
      if (character == '\n') {
        ++position.line;
        position.column = 1;
      } else
        ++position.column;
    }
    result.push_back(
        {position,
         {position.line, position.column + static_cast<int32_t>(end - begin)}});
    if (text[end] == '}')
      break;
    begin = end + 1;
  }
  return result;
}
}  // namespace ptx_frontend::resolved_ir::atomic_value_test_wrap
/** Forward the real library checker without changing any argument or result. */
extern "C" ptx_frontend::resolved_ir::checker::
    CheckResult
    atomic_value_real_operands(std::span<const ptx_frontend::resolved_ir::checker::OperandDescriptor>, std::span<const ptx_frontend::resolved_ir::checker::FieldView>, std::span<const ptx_frontend::resolved_ir::checker::OperandView>, std::span<const ptx_frontend::resolved_ir::checker::OperandTypeCompatibilityDescriptor>, const ptx_frontend::resolved_ir::checker::Context&, const ptx_frontend::resolved_ir::MatrixInstructionDescriptor*) asm(
        "__real__ZN12ptx_frontend11resolved_ir7checker14check_"
        "operandsESt4spanIKNS1_17OperandDescriptorELm18446744073709551615EES2_"
        "IKNS1_9FieldViewELm18446744073709551615EES2_IKNS1_"
        "11OperandViewELm18446744073709551615EES2_IKNS1_"
        "34OperandTypeCompatibilityDescriptorELm18446744073709551615EERKNS1_"
        "7ContextEPKNS0_27MatrixInstructionDescriptorE");
/** Observe actual generated projections only inside the synchronous Instruction::check call. */
extern "C" ptx_frontend::resolved_ir::checker::
    CheckResult
    atomic_value_wrap_operands(std::span<const ptx_frontend::resolved_ir::checker::OperandDescriptor>, std::span<const ptx_frontend::resolved_ir::checker::FieldView>, std::span<const ptx_frontend::resolved_ir::checker::OperandView>, std::span<const ptx_frontend::resolved_ir::checker::OperandTypeCompatibilityDescriptor>, const ptx_frontend::resolved_ir::checker::Context&, const ptx_frontend::resolved_ir::MatrixInstructionDescriptor*) asm(
        "__wrap__ZN12ptx_frontend11resolved_ir7checker14check_"
        "operandsESt4spanIKNS1_17OperandDescriptorELm18446744073709551615EES2_"
        "IKNS1_9FieldViewELm18446744073709551615EES2_IKNS1_"
        "11OperandViewELm18446744073709551615EES2_IKNS1_"
        "34OperandTypeCompatibilityDescriptorELm18446744073709551615EERKNS1_"
        "7ContextEPKNS0_27MatrixInstructionDescriptorE");
ptx_frontend::resolved_ir::checker::CheckResult atomic_value_wrap_operands(
    std::span<const ptx_frontend::resolved_ir::checker::OperandDescriptor>
        descriptors,
    std::span<const ptx_frontend::resolved_ir::checker::FieldView> fields,
    std::span<const ptx_frontend::resolved_ir::checker::OperandView> views,
    std::span<const ptx_frontend::resolved_ir::checker::
                  OperandTypeCompatibilityDescriptor>
        compatibility,
    const ptx_frontend::resolved_ir::checker::Context& context,
    const ptx_frontend::resolved_ir::MatrixInstructionDescriptor* matrix) {
  using namespace ptx_frontend::resolved_ir;
  if (auto* expected = atomic_value_test_wrap::active) {
    for (const auto& view : views)
      if (view.field_id == "src") {
        ++expected->calls;
        const auto descriptor =
            std::ranges::find(descriptors, std::string_view("src"),
                              &checker::OperandDescriptor::target_field_id);
        const auto* member = expected->member;
        bool valid =
            member && member->value.elements.size() == expected->arity &&
            member->locs.size() == expected->arity &&
            expected->ranges.size() == expected->arity &&
            descriptor != descriptors.end() &&
            descriptor->source_value_vector &&
            descriptor->access == checker::OperandAccess::Read &&
            descriptor->role == checker::OperandRole::Source &&
            descriptor->vector_type_policy ==
                checker::VectorTypePolicy::Element &&
            descriptor->register_width_policy ==
                ptx_frontend::base::ScalarTypeSizePolicy::SameWidth &&
            !descriptor->allow_named_vector && !descriptor->allow_vector_sink &&
            view.actual_shape == checker::OperandShape::Vector &&
            view.value_vector == &member->value && !view.register_vector &&
            !view.mov_value_vector && view.vector_arity == expected->arity &&
            view.locations.data() == member->locs.data() &&
            std::ranges::equal(view.locations, expected->ranges) &&
            member->locs == expected->ranges;
        if (valid)
          for (size_t index = 0; index < expected->arity; ++index) {
            const bool immediate_lane =
                expected->mode == 1 || (expected->mode == 2 && index == 0);
            if (immediate_lane) {
              const auto* immediate = std::get_if<ResolvedImmediate>(
                  &member->value.elements[index]);
              valid = valid && immediate &&
                      view.vector_element_immediates[index] == immediate &&
                      !view.vector_element_registers[index] &&
                      view.vector_element_shapes[index] ==
                          checker::OperandShape::Immediate &&
                      immediate->type == expected->logical &&
                      immediate->bits == expected->bits &&
                      !immediate->integer_source_bits &&
                      !immediate->is_negative &&
                      view.vector_element_types[index] == immediate->type &&
                      view.vector_immediate_bits[index] == immediate->bits &&
                      view.vector_immediate_source_bits[index] ==
                          immediate->integer_source_bits &&
                      view.vector_immediate_negative[index] ==
                          immediate->is_negative;
            } else {
              const auto* reg = std::get_if<ResolvedRegisterRef>(
                  &member->value.elements[index]);
              valid = valid && reg &&
                      view.vector_element_registers[index] == reg &&
                      !view.vector_element_immediates[index] &&
                      view.vector_element_shapes[index] ==
                          checker::OperandShape::Register &&
                      reg->spelling == "s" + std::to_string(index) &&
                      reg->symbol_id &&
                      reg->register_class == ResolvedRegisterClass::General &&
                      !reg->vector_width &&
                      reg->declared_type == expected->physical &&
                      view.vector_element_types[index] == *reg->declared_type &&
                      !view.vector_immediate_bits[index].has_value() &&
                      !view.vector_immediate_source_bits[index] &&
                      !view.vector_immediate_negative[index];
            }
          }
        expected->valid = expected->valid && valid;
      }
  }
  return atomic_value_real_operands(descriptors, fields, views, compatibility,
                                    context, matrix);
}
#endif

namespace ptx_frontend::resolved_ir {
namespace {
/** One exact selected type/arity/layout source branch, never a runtime quota. */
struct AtomicValueCase {
  /** Stable adopted branch identity, used only in test diagnostics. */
  std::string_view id;
  /** Existing exact final class identity. */
  InstructionKind kind;
  /** Whether this branch retains a register-vector destination. */
  bool atom;
  /** Whether this branch retains the optional B64 policy operand. */
  bool hinted;
  /** Exact selected number of scalar lanes. */
  unsigned arity;
  /** Logical type used only for immediate conversion and current typed words. */
  ScalarType logical;
  /** Existing physical bit carrier used for destination and register controls. */
  std::string_view physical;
  /** Exact legal control sequence through the selected type. */
  std::string_view operation;
  /** Accepted scalar numeric or bare exact-word producer. */
  std::string_view literal;
  /** Existing converter golden or exact raw packed word. */
  uint64_t bits;
  /** Comma-separated unchanged physical register-domain spellings. */
  std::string_view allowed;
};
/** All128 finite branches:80numeric and48exact-packed, preserving existing arities. */
constexpr AtomicValueCase branches[] = {
    {"atom/atom_vector_add_noftz_f16/no_hint/src/f16/v2",
     AtomVectorAddNoftzF16::kind, true, false, 2, ScalarType::F16, "b16",
     "atom.global.add.noftz.v2.f16", "1.5", 0x3e00ULL, "b16,f16,u16,s16"},
    {"atom/atom_vector_add_noftz_f16/no_hint/src/f16/v4",
     AtomVectorAddNoftzF16::kind, true, false, 4, ScalarType::F16, "b16",
     "atom.global.add.noftz.v4.f16", "1.5", 0x3e00ULL, "b16,f16,u16,s16"},
    {"atom/atom_vector_add_noftz_f16/no_hint/src/f16/v8",
     AtomVectorAddNoftzF16::kind, true, false, 8, ScalarType::F16, "b16",
     "atom.global.add.noftz.v8.f16", "1.5", 0x3e00ULL, "b16,f16,u16,s16"},
    {"atom/atom_vector_add_noftz_f16/with_policy/src/f16/v2",
     AtomVectorAddNoftzF16::kind, true, true, 2, ScalarType::F16, "b16",
     "atom.global.add.noftz.L2::cache_hint.v2.f16", "1.5", 0x3e00ULL,
     "b16,f16,u16,s16"},
    {"atom/atom_vector_add_noftz_f16/with_policy/src/f16/v4",
     AtomVectorAddNoftzF16::kind, true, true, 4, ScalarType::F16, "b16",
     "atom.global.add.noftz.L2::cache_hint.v4.f16", "1.5", 0x3e00ULL,
     "b16,f16,u16,s16"},
    {"atom/atom_vector_add_noftz_f16/with_policy/src/f16/v8",
     AtomVectorAddNoftzF16::kind, true, true, 8, ScalarType::F16, "b16",
     "atom.global.add.noftz.L2::cache_hint.v8.f16", "1.5", 0x3e00ULL,
     "b16,f16,u16,s16"},
    {"atom/atom_vector_min_noftz_f16/no_hint/src/f16/v2",
     AtomVectorMinNoftzF16::kind, true, false, 2, ScalarType::F16, "b16",
     "atom.global.min.noftz.v2.f16", "1.5", 0x3e00ULL, "b16,f16,u16,s16"},
    {"atom/atom_vector_min_noftz_f16/no_hint/src/f16/v4",
     AtomVectorMinNoftzF16::kind, true, false, 4, ScalarType::F16, "b16",
     "atom.global.min.noftz.v4.f16", "1.5", 0x3e00ULL, "b16,f16,u16,s16"},
    {"atom/atom_vector_min_noftz_f16/no_hint/src/f16/v8",
     AtomVectorMinNoftzF16::kind, true, false, 8, ScalarType::F16, "b16",
     "atom.global.min.noftz.v8.f16", "1.5", 0x3e00ULL, "b16,f16,u16,s16"},
    {"atom/atom_vector_min_noftz_f16/with_policy/src/f16/v2",
     AtomVectorMinNoftzF16::kind, true, true, 2, ScalarType::F16, "b16",
     "atom.global.min.noftz.L2::cache_hint.v2.f16", "1.5", 0x3e00ULL,
     "b16,f16,u16,s16"},
    {"atom/atom_vector_min_noftz_f16/with_policy/src/f16/v4",
     AtomVectorMinNoftzF16::kind, true, true, 4, ScalarType::F16, "b16",
     "atom.global.min.noftz.L2::cache_hint.v4.f16", "1.5", 0x3e00ULL,
     "b16,f16,u16,s16"},
    {"atom/atom_vector_min_noftz_f16/with_policy/src/f16/v8",
     AtomVectorMinNoftzF16::kind, true, true, 8, ScalarType::F16, "b16",
     "atom.global.min.noftz.L2::cache_hint.v8.f16", "1.5", 0x3e00ULL,
     "b16,f16,u16,s16"},
    {"atom/atom_vector_max_noftz_f16/no_hint/src/f16/v2",
     AtomVectorMaxNoftzF16::kind, true, false, 2, ScalarType::F16, "b16",
     "atom.global.max.noftz.v2.f16", "1.5", 0x3e00ULL, "b16,f16,u16,s16"},
    {"atom/atom_vector_max_noftz_f16/no_hint/src/f16/v4",
     AtomVectorMaxNoftzF16::kind, true, false, 4, ScalarType::F16, "b16",
     "atom.global.max.noftz.v4.f16", "1.5", 0x3e00ULL, "b16,f16,u16,s16"},
    {"atom/atom_vector_max_noftz_f16/no_hint/src/f16/v8",
     AtomVectorMaxNoftzF16::kind, true, false, 8, ScalarType::F16, "b16",
     "atom.global.max.noftz.v8.f16", "1.5", 0x3e00ULL, "b16,f16,u16,s16"},
    {"atom/atom_vector_max_noftz_f16/with_policy/src/f16/v2",
     AtomVectorMaxNoftzF16::kind, true, true, 2, ScalarType::F16, "b16",
     "atom.global.max.noftz.L2::cache_hint.v2.f16", "1.5", 0x3e00ULL,
     "b16,f16,u16,s16"},
    {"atom/atom_vector_max_noftz_f16/with_policy/src/f16/v4",
     AtomVectorMaxNoftzF16::kind, true, true, 4, ScalarType::F16, "b16",
     "atom.global.max.noftz.L2::cache_hint.v4.f16", "1.5", 0x3e00ULL,
     "b16,f16,u16,s16"},
    {"atom/atom_vector_max_noftz_f16/with_policy/src/f16/v8",
     AtomVectorMaxNoftzF16::kind, true, true, 8, ScalarType::F16, "b16",
     "atom.global.max.noftz.L2::cache_hint.v8.f16", "1.5", 0x3e00ULL,
     "b16,f16,u16,s16"},
    {"atom/atom_vector_add_noftz_bf16/no_hint/src/bf16/v2",
     AtomVectorAddNoftzBf16::kind, true, false, 2, ScalarType::BF16, "b16",
     "atom.global.add.noftz.v2.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"atom/atom_vector_add_noftz_bf16/no_hint/src/bf16/v4",
     AtomVectorAddNoftzBf16::kind, true, false, 4, ScalarType::BF16, "b16",
     "atom.global.add.noftz.v4.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"atom/atom_vector_add_noftz_bf16/no_hint/src/bf16/v8",
     AtomVectorAddNoftzBf16::kind, true, false, 8, ScalarType::BF16, "b16",
     "atom.global.add.noftz.v8.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"atom/atom_vector_add_noftz_bf16/with_policy/src/bf16/v2",
     AtomVectorAddNoftzBf16::kind, true, true, 2, ScalarType::BF16, "b16",
     "atom.global.add.noftz.L2::cache_hint.v2.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"atom/atom_vector_add_noftz_bf16/with_policy/src/bf16/v4",
     AtomVectorAddNoftzBf16::kind, true, true, 4, ScalarType::BF16, "b16",
     "atom.global.add.noftz.L2::cache_hint.v4.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"atom/atom_vector_add_noftz_bf16/with_policy/src/bf16/v8",
     AtomVectorAddNoftzBf16::kind, true, true, 8, ScalarType::BF16, "b16",
     "atom.global.add.noftz.L2::cache_hint.v8.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"atom/atom_vector_min_noftz_bf16/no_hint/src/bf16/v2",
     AtomVectorMinNoftzBf16::kind, true, false, 2, ScalarType::BF16, "b16",
     "atom.global.min.noftz.v2.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"atom/atom_vector_min_noftz_bf16/no_hint/src/bf16/v4",
     AtomVectorMinNoftzBf16::kind, true, false, 4, ScalarType::BF16, "b16",
     "atom.global.min.noftz.v4.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"atom/atom_vector_min_noftz_bf16/no_hint/src/bf16/v8",
     AtomVectorMinNoftzBf16::kind, true, false, 8, ScalarType::BF16, "b16",
     "atom.global.min.noftz.v8.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"atom/atom_vector_min_noftz_bf16/with_policy/src/bf16/v2",
     AtomVectorMinNoftzBf16::kind, true, true, 2, ScalarType::BF16, "b16",
     "atom.global.min.noftz.L2::cache_hint.v2.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"atom/atom_vector_min_noftz_bf16/with_policy/src/bf16/v4",
     AtomVectorMinNoftzBf16::kind, true, true, 4, ScalarType::BF16, "b16",
     "atom.global.min.noftz.L2::cache_hint.v4.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"atom/atom_vector_min_noftz_bf16/with_policy/src/bf16/v8",
     AtomVectorMinNoftzBf16::kind, true, true, 8, ScalarType::BF16, "b16",
     "atom.global.min.noftz.L2::cache_hint.v8.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"atom/atom_vector_max_noftz_bf16/no_hint/src/bf16/v2",
     AtomVectorMaxNoftzBf16::kind, true, false, 2, ScalarType::BF16, "b16",
     "atom.global.max.noftz.v2.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"atom/atom_vector_max_noftz_bf16/no_hint/src/bf16/v4",
     AtomVectorMaxNoftzBf16::kind, true, false, 4, ScalarType::BF16, "b16",
     "atom.global.max.noftz.v4.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"atom/atom_vector_max_noftz_bf16/no_hint/src/bf16/v8",
     AtomVectorMaxNoftzBf16::kind, true, false, 8, ScalarType::BF16, "b16",
     "atom.global.max.noftz.v8.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"atom/atom_vector_max_noftz_bf16/with_policy/src/bf16/v2",
     AtomVectorMaxNoftzBf16::kind, true, true, 2, ScalarType::BF16, "b16",
     "atom.global.max.noftz.L2::cache_hint.v2.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"atom/atom_vector_max_noftz_bf16/with_policy/src/bf16/v4",
     AtomVectorMaxNoftzBf16::kind, true, true, 4, ScalarType::BF16, "b16",
     "atom.global.max.noftz.L2::cache_hint.v4.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"atom/atom_vector_max_noftz_bf16/with_policy/src/bf16/v8",
     AtomVectorMaxNoftzBf16::kind, true, true, 8, ScalarType::BF16, "b16",
     "atom.global.max.noftz.L2::cache_hint.v8.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"atom/atom_vector_add_noftz_f16x2/no_hint/src/f16x2/v2",
     AtomVectorAddNoftzF16x2::kind, true, false, 2, ScalarType::F16x2, "b32",
     "atom.global.add.noftz.v2.f16x2", "0f3e003e00", 0x3e003e00ULL,
     "b32,f16x2"},
    {"atom/atom_vector_add_noftz_f16x2/no_hint/src/f16x2/v4",
     AtomVectorAddNoftzF16x2::kind, true, false, 4, ScalarType::F16x2, "b32",
     "atom.global.add.noftz.v4.f16x2", "0f3e003e00", 0x3e003e00ULL,
     "b32,f16x2"},
    {"atom/atom_vector_add_noftz_f16x2/with_policy/src/f16x2/v2",
     AtomVectorAddNoftzF16x2::kind, true, true, 2, ScalarType::F16x2, "b32",
     "atom.global.add.noftz.L2::cache_hint.v2.f16x2", "0f3e003e00",
     0x3e003e00ULL, "b32,f16x2"},
    {"atom/atom_vector_add_noftz_f16x2/with_policy/src/f16x2/v4",
     AtomVectorAddNoftzF16x2::kind, true, true, 4, ScalarType::F16x2, "b32",
     "atom.global.add.noftz.L2::cache_hint.v4.f16x2", "0f3e003e00",
     0x3e003e00ULL, "b32,f16x2"},
    {"atom/atom_vector_min_noftz_f16x2/no_hint/src/f16x2/v2",
     AtomVectorMinNoftzF16x2::kind, true, false, 2, ScalarType::F16x2, "b32",
     "atom.global.min.noftz.v2.f16x2", "0f3e003e00", 0x3e003e00ULL,
     "b32,f16x2"},
    {"atom/atom_vector_min_noftz_f16x2/no_hint/src/f16x2/v4",
     AtomVectorMinNoftzF16x2::kind, true, false, 4, ScalarType::F16x2, "b32",
     "atom.global.min.noftz.v4.f16x2", "0f3e003e00", 0x3e003e00ULL,
     "b32,f16x2"},
    {"atom/atom_vector_min_noftz_f16x2/with_policy/src/f16x2/v2",
     AtomVectorMinNoftzF16x2::kind, true, true, 2, ScalarType::F16x2, "b32",
     "atom.global.min.noftz.L2::cache_hint.v2.f16x2", "0f3e003e00",
     0x3e003e00ULL, "b32,f16x2"},
    {"atom/atom_vector_min_noftz_f16x2/with_policy/src/f16x2/v4",
     AtomVectorMinNoftzF16x2::kind, true, true, 4, ScalarType::F16x2, "b32",
     "atom.global.min.noftz.L2::cache_hint.v4.f16x2", "0f3e003e00",
     0x3e003e00ULL, "b32,f16x2"},
    {"atom/atom_vector_max_noftz_f16x2/no_hint/src/f16x2/v2",
     AtomVectorMaxNoftzF16x2::kind, true, false, 2, ScalarType::F16x2, "b32",
     "atom.global.max.noftz.v2.f16x2", "0f3e003e00", 0x3e003e00ULL,
     "b32,f16x2"},
    {"atom/atom_vector_max_noftz_f16x2/no_hint/src/f16x2/v4",
     AtomVectorMaxNoftzF16x2::kind, true, false, 4, ScalarType::F16x2, "b32",
     "atom.global.max.noftz.v4.f16x2", "0f3e003e00", 0x3e003e00ULL,
     "b32,f16x2"},
    {"atom/atom_vector_max_noftz_f16x2/with_policy/src/f16x2/v2",
     AtomVectorMaxNoftzF16x2::kind, true, true, 2, ScalarType::F16x2, "b32",
     "atom.global.max.noftz.L2::cache_hint.v2.f16x2", "0f3e003e00",
     0x3e003e00ULL, "b32,f16x2"},
    {"atom/atom_vector_max_noftz_f16x2/with_policy/src/f16x2/v4",
     AtomVectorMaxNoftzF16x2::kind, true, true, 4, ScalarType::F16x2, "b32",
     "atom.global.max.noftz.L2::cache_hint.v4.f16x2", "0f3e003e00",
     0x3e003e00ULL, "b32,f16x2"},
    {"atom/atom_vector_add_noftz_bf16x2/no_hint/src/bf16x2/v2",
     AtomVectorAddNoftzBf16x2::kind, true, false, 2, ScalarType::BF16x2, "b32",
     "atom.global.add.noftz.v2.bf16x2", "0f3fc03fc0", 0x3fc03fc0ULL, "b32"},
    {"atom/atom_vector_add_noftz_bf16x2/no_hint/src/bf16x2/v4",
     AtomVectorAddNoftzBf16x2::kind, true, false, 4, ScalarType::BF16x2, "b32",
     "atom.global.add.noftz.v4.bf16x2", "0f3fc03fc0", 0x3fc03fc0ULL, "b32"},
    {"atom/atom_vector_add_noftz_bf16x2/with_policy/src/bf16x2/v2",
     AtomVectorAddNoftzBf16x2::kind, true, true, 2, ScalarType::BF16x2, "b32",
     "atom.global.add.noftz.L2::cache_hint.v2.bf16x2", "0f3fc03fc0",
     0x3fc03fc0ULL, "b32"},
    {"atom/atom_vector_add_noftz_bf16x2/with_policy/src/bf16x2/v4",
     AtomVectorAddNoftzBf16x2::kind, true, true, 4, ScalarType::BF16x2, "b32",
     "atom.global.add.noftz.L2::cache_hint.v4.bf16x2", "0f3fc03fc0",
     0x3fc03fc0ULL, "b32"},
    {"atom/atom_vector_min_noftz_bf16x2/no_hint/src/bf16x2/v2",
     AtomVectorMinNoftzBf16x2::kind, true, false, 2, ScalarType::BF16x2, "b32",
     "atom.global.min.noftz.v2.bf16x2", "0f3fc03fc0", 0x3fc03fc0ULL, "b32"},
    {"atom/atom_vector_min_noftz_bf16x2/no_hint/src/bf16x2/v4",
     AtomVectorMinNoftzBf16x2::kind, true, false, 4, ScalarType::BF16x2, "b32",
     "atom.global.min.noftz.v4.bf16x2", "0f3fc03fc0", 0x3fc03fc0ULL, "b32"},
    {"atom/atom_vector_min_noftz_bf16x2/with_policy/src/bf16x2/v2",
     AtomVectorMinNoftzBf16x2::kind, true, true, 2, ScalarType::BF16x2, "b32",
     "atom.global.min.noftz.L2::cache_hint.v2.bf16x2", "0f3fc03fc0",
     0x3fc03fc0ULL, "b32"},
    {"atom/atom_vector_min_noftz_bf16x2/with_policy/src/bf16x2/v4",
     AtomVectorMinNoftzBf16x2::kind, true, true, 4, ScalarType::BF16x2, "b32",
     "atom.global.min.noftz.L2::cache_hint.v4.bf16x2", "0f3fc03fc0",
     0x3fc03fc0ULL, "b32"},
    {"atom/atom_vector_max_noftz_bf16x2/no_hint/src/bf16x2/v2",
     AtomVectorMaxNoftzBf16x2::kind, true, false, 2, ScalarType::BF16x2, "b32",
     "atom.global.max.noftz.v2.bf16x2", "0f3fc03fc0", 0x3fc03fc0ULL, "b32"},
    {"atom/atom_vector_max_noftz_bf16x2/no_hint/src/bf16x2/v4",
     AtomVectorMaxNoftzBf16x2::kind, true, false, 4, ScalarType::BF16x2, "b32",
     "atom.global.max.noftz.v4.bf16x2", "0f3fc03fc0", 0x3fc03fc0ULL, "b32"},
    {"atom/atom_vector_max_noftz_bf16x2/with_policy/src/bf16x2/v2",
     AtomVectorMaxNoftzBf16x2::kind, true, true, 2, ScalarType::BF16x2, "b32",
     "atom.global.max.noftz.L2::cache_hint.v2.bf16x2", "0f3fc03fc0",
     0x3fc03fc0ULL, "b32"},
    {"atom/atom_vector_max_noftz_bf16x2/with_policy/src/bf16x2/v4",
     AtomVectorMaxNoftzBf16x2::kind, true, true, 4, ScalarType::BF16x2, "b32",
     "atom.global.max.noftz.L2::cache_hint.v4.bf16x2", "0f3fc03fc0",
     0x3fc03fc0ULL, "b32"},
    {"atom/atom_vector_add_f32/no_hint/src/f32/v2", AtomVectorAddF32::kind,
     true, false, 2, ScalarType::F32, "b32", "atom.global.add.v2.f32", "1.5",
     0x3fc00000ULL, "b32,f32,u32,s32"},
    {"atom/atom_vector_add_f32/no_hint/src/f32/v4", AtomVectorAddF32::kind,
     true, false, 4, ScalarType::F32, "b32", "atom.global.add.v4.f32", "1.5",
     0x3fc00000ULL, "b32,f32,u32,s32"},
    {"atom/atom_vector_add_f32/with_policy/src/f32/v2", AtomVectorAddF32::kind,
     true, true, 2, ScalarType::F32, "b32",
     "atom.global.add.L2::cache_hint.v2.f32", "1.5", 0x3fc00000ULL,
     "b32,f32,u32,s32"},
    {"atom/atom_vector_add_f32/with_policy/src/f32/v4", AtomVectorAddF32::kind,
     true, true, 4, ScalarType::F32, "b32",
     "atom.global.add.L2::cache_hint.v4.f32", "1.5", 0x3fc00000ULL,
     "b32,f32,u32,s32"},
    {"red/red_vector_add_noftz_f16/no_hint/src/f16/v2",
     RedVectorAddNoftzF16::kind, false, false, 2, ScalarType::F16, "b16",
     "red.global.add.noftz.v2.f16", "1.5", 0x3e00ULL, "b16,f16,u16,s16"},
    {"red/red_vector_add_noftz_f16/no_hint/src/f16/v4",
     RedVectorAddNoftzF16::kind, false, false, 4, ScalarType::F16, "b16",
     "red.global.add.noftz.v4.f16", "1.5", 0x3e00ULL, "b16,f16,u16,s16"},
    {"red/red_vector_add_noftz_f16/no_hint/src/f16/v8",
     RedVectorAddNoftzF16::kind, false, false, 8, ScalarType::F16, "b16",
     "red.global.add.noftz.v8.f16", "1.5", 0x3e00ULL, "b16,f16,u16,s16"},
    {"red/red_vector_add_noftz_f16/with_policy/src/f16/v2",
     RedVectorAddNoftzF16::kind, false, true, 2, ScalarType::F16, "b16",
     "red.global.add.noftz.L2::cache_hint.v2.f16", "1.5", 0x3e00ULL,
     "b16,f16,u16,s16"},
    {"red/red_vector_add_noftz_f16/with_policy/src/f16/v4",
     RedVectorAddNoftzF16::kind, false, true, 4, ScalarType::F16, "b16",
     "red.global.add.noftz.L2::cache_hint.v4.f16", "1.5", 0x3e00ULL,
     "b16,f16,u16,s16"},
    {"red/red_vector_add_noftz_f16/with_policy/src/f16/v8",
     RedVectorAddNoftzF16::kind, false, true, 8, ScalarType::F16, "b16",
     "red.global.add.noftz.L2::cache_hint.v8.f16", "1.5", 0x3e00ULL,
     "b16,f16,u16,s16"},
    {"red/red_vector_min_noftz_f16/no_hint/src/f16/v2",
     RedVectorMinNoftzF16::kind, false, false, 2, ScalarType::F16, "b16",
     "red.global.min.noftz.v2.f16", "1.5", 0x3e00ULL, "b16,f16,u16,s16"},
    {"red/red_vector_min_noftz_f16/no_hint/src/f16/v4",
     RedVectorMinNoftzF16::kind, false, false, 4, ScalarType::F16, "b16",
     "red.global.min.noftz.v4.f16", "1.5", 0x3e00ULL, "b16,f16,u16,s16"},
    {"red/red_vector_min_noftz_f16/no_hint/src/f16/v8",
     RedVectorMinNoftzF16::kind, false, false, 8, ScalarType::F16, "b16",
     "red.global.min.noftz.v8.f16", "1.5", 0x3e00ULL, "b16,f16,u16,s16"},
    {"red/red_vector_min_noftz_f16/with_policy/src/f16/v2",
     RedVectorMinNoftzF16::kind, false, true, 2, ScalarType::F16, "b16",
     "red.global.min.noftz.L2::cache_hint.v2.f16", "1.5", 0x3e00ULL,
     "b16,f16,u16,s16"},
    {"red/red_vector_min_noftz_f16/with_policy/src/f16/v4",
     RedVectorMinNoftzF16::kind, false, true, 4, ScalarType::F16, "b16",
     "red.global.min.noftz.L2::cache_hint.v4.f16", "1.5", 0x3e00ULL,
     "b16,f16,u16,s16"},
    {"red/red_vector_min_noftz_f16/with_policy/src/f16/v8",
     RedVectorMinNoftzF16::kind, false, true, 8, ScalarType::F16, "b16",
     "red.global.min.noftz.L2::cache_hint.v8.f16", "1.5", 0x3e00ULL,
     "b16,f16,u16,s16"},
    {"red/red_vector_max_noftz_f16/no_hint/src/f16/v2",
     RedVectorMaxNoftzF16::kind, false, false, 2, ScalarType::F16, "b16",
     "red.global.max.noftz.v2.f16", "1.5", 0x3e00ULL, "b16,f16,u16,s16"},
    {"red/red_vector_max_noftz_f16/no_hint/src/f16/v4",
     RedVectorMaxNoftzF16::kind, false, false, 4, ScalarType::F16, "b16",
     "red.global.max.noftz.v4.f16", "1.5", 0x3e00ULL, "b16,f16,u16,s16"},
    {"red/red_vector_max_noftz_f16/no_hint/src/f16/v8",
     RedVectorMaxNoftzF16::kind, false, false, 8, ScalarType::F16, "b16",
     "red.global.max.noftz.v8.f16", "1.5", 0x3e00ULL, "b16,f16,u16,s16"},
    {"red/red_vector_max_noftz_f16/with_policy/src/f16/v2",
     RedVectorMaxNoftzF16::kind, false, true, 2, ScalarType::F16, "b16",
     "red.global.max.noftz.L2::cache_hint.v2.f16", "1.5", 0x3e00ULL,
     "b16,f16,u16,s16"},
    {"red/red_vector_max_noftz_f16/with_policy/src/f16/v4",
     RedVectorMaxNoftzF16::kind, false, true, 4, ScalarType::F16, "b16",
     "red.global.max.noftz.L2::cache_hint.v4.f16", "1.5", 0x3e00ULL,
     "b16,f16,u16,s16"},
    {"red/red_vector_max_noftz_f16/with_policy/src/f16/v8",
     RedVectorMaxNoftzF16::kind, false, true, 8, ScalarType::F16, "b16",
     "red.global.max.noftz.L2::cache_hint.v8.f16", "1.5", 0x3e00ULL,
     "b16,f16,u16,s16"},
    {"red/red_vector_add_noftz_bf16/no_hint/src/bf16/v2",
     RedVectorAddNoftzBf16::kind, false, false, 2, ScalarType::BF16, "b16",
     "red.global.add.noftz.v2.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"red/red_vector_add_noftz_bf16/no_hint/src/bf16/v4",
     RedVectorAddNoftzBf16::kind, false, false, 4, ScalarType::BF16, "b16",
     "red.global.add.noftz.v4.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"red/red_vector_add_noftz_bf16/no_hint/src/bf16/v8",
     RedVectorAddNoftzBf16::kind, false, false, 8, ScalarType::BF16, "b16",
     "red.global.add.noftz.v8.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"red/red_vector_add_noftz_bf16/with_policy/src/bf16/v2",
     RedVectorAddNoftzBf16::kind, false, true, 2, ScalarType::BF16, "b16",
     "red.global.add.noftz.L2::cache_hint.v2.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"red/red_vector_add_noftz_bf16/with_policy/src/bf16/v4",
     RedVectorAddNoftzBf16::kind, false, true, 4, ScalarType::BF16, "b16",
     "red.global.add.noftz.L2::cache_hint.v4.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"red/red_vector_add_noftz_bf16/with_policy/src/bf16/v8",
     RedVectorAddNoftzBf16::kind, false, true, 8, ScalarType::BF16, "b16",
     "red.global.add.noftz.L2::cache_hint.v8.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"red/red_vector_min_noftz_bf16/no_hint/src/bf16/v2",
     RedVectorMinNoftzBf16::kind, false, false, 2, ScalarType::BF16, "b16",
     "red.global.min.noftz.v2.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"red/red_vector_min_noftz_bf16/no_hint/src/bf16/v4",
     RedVectorMinNoftzBf16::kind, false, false, 4, ScalarType::BF16, "b16",
     "red.global.min.noftz.v4.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"red/red_vector_min_noftz_bf16/no_hint/src/bf16/v8",
     RedVectorMinNoftzBf16::kind, false, false, 8, ScalarType::BF16, "b16",
     "red.global.min.noftz.v8.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"red/red_vector_min_noftz_bf16/with_policy/src/bf16/v2",
     RedVectorMinNoftzBf16::kind, false, true, 2, ScalarType::BF16, "b16",
     "red.global.min.noftz.L2::cache_hint.v2.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"red/red_vector_min_noftz_bf16/with_policy/src/bf16/v4",
     RedVectorMinNoftzBf16::kind, false, true, 4, ScalarType::BF16, "b16",
     "red.global.min.noftz.L2::cache_hint.v4.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"red/red_vector_min_noftz_bf16/with_policy/src/bf16/v8",
     RedVectorMinNoftzBf16::kind, false, true, 8, ScalarType::BF16, "b16",
     "red.global.min.noftz.L2::cache_hint.v8.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"red/red_vector_max_noftz_bf16/no_hint/src/bf16/v2",
     RedVectorMaxNoftzBf16::kind, false, false, 2, ScalarType::BF16, "b16",
     "red.global.max.noftz.v2.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"red/red_vector_max_noftz_bf16/no_hint/src/bf16/v4",
     RedVectorMaxNoftzBf16::kind, false, false, 4, ScalarType::BF16, "b16",
     "red.global.max.noftz.v4.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"red/red_vector_max_noftz_bf16/no_hint/src/bf16/v8",
     RedVectorMaxNoftzBf16::kind, false, false, 8, ScalarType::BF16, "b16",
     "red.global.max.noftz.v8.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"red/red_vector_max_noftz_bf16/with_policy/src/bf16/v2",
     RedVectorMaxNoftzBf16::kind, false, true, 2, ScalarType::BF16, "b16",
     "red.global.max.noftz.L2::cache_hint.v2.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"red/red_vector_max_noftz_bf16/with_policy/src/bf16/v4",
     RedVectorMaxNoftzBf16::kind, false, true, 4, ScalarType::BF16, "b16",
     "red.global.max.noftz.L2::cache_hint.v4.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"red/red_vector_max_noftz_bf16/with_policy/src/bf16/v8",
     RedVectorMaxNoftzBf16::kind, false, true, 8, ScalarType::BF16, "b16",
     "red.global.max.noftz.L2::cache_hint.v8.bf16", "1.5", 0x3fc0ULL, "b16"},
    {"red/red_vector_add_noftz_f16x2/no_hint/src/f16x2/v2",
     RedVectorAddNoftzF16x2::kind, false, false, 2, ScalarType::F16x2, "b32",
     "red.global.add.noftz.v2.f16x2", "0f3e003e00", 0x3e003e00ULL, "b32,f16x2"},
    {"red/red_vector_add_noftz_f16x2/no_hint/src/f16x2/v4",
     RedVectorAddNoftzF16x2::kind, false, false, 4, ScalarType::F16x2, "b32",
     "red.global.add.noftz.v4.f16x2", "0f3e003e00", 0x3e003e00ULL, "b32,f16x2"},
    {"red/red_vector_add_noftz_f16x2/with_policy/src/f16x2/v2",
     RedVectorAddNoftzF16x2::kind, false, true, 2, ScalarType::F16x2, "b32",
     "red.global.add.noftz.L2::cache_hint.v2.f16x2", "0f3e003e00",
     0x3e003e00ULL, "b32,f16x2"},
    {"red/red_vector_add_noftz_f16x2/with_policy/src/f16x2/v4",
     RedVectorAddNoftzF16x2::kind, false, true, 4, ScalarType::F16x2, "b32",
     "red.global.add.noftz.L2::cache_hint.v4.f16x2", "0f3e003e00",
     0x3e003e00ULL, "b32,f16x2"},
    {"red/red_vector_min_noftz_f16x2/no_hint/src/f16x2/v2",
     RedVectorMinNoftzF16x2::kind, false, false, 2, ScalarType::F16x2, "b32",
     "red.global.min.noftz.v2.f16x2", "0f3e003e00", 0x3e003e00ULL, "b32,f16x2"},
    {"red/red_vector_min_noftz_f16x2/no_hint/src/f16x2/v4",
     RedVectorMinNoftzF16x2::kind, false, false, 4, ScalarType::F16x2, "b32",
     "red.global.min.noftz.v4.f16x2", "0f3e003e00", 0x3e003e00ULL, "b32,f16x2"},
    {"red/red_vector_min_noftz_f16x2/with_policy/src/f16x2/v2",
     RedVectorMinNoftzF16x2::kind, false, true, 2, ScalarType::F16x2, "b32",
     "red.global.min.noftz.L2::cache_hint.v2.f16x2", "0f3e003e00",
     0x3e003e00ULL, "b32,f16x2"},
    {"red/red_vector_min_noftz_f16x2/with_policy/src/f16x2/v4",
     RedVectorMinNoftzF16x2::kind, false, true, 4, ScalarType::F16x2, "b32",
     "red.global.min.noftz.L2::cache_hint.v4.f16x2", "0f3e003e00",
     0x3e003e00ULL, "b32,f16x2"},
    {"red/red_vector_max_noftz_f16x2/no_hint/src/f16x2/v2",
     RedVectorMaxNoftzF16x2::kind, false, false, 2, ScalarType::F16x2, "b32",
     "red.global.max.noftz.v2.f16x2", "0f3e003e00", 0x3e003e00ULL, "b32,f16x2"},
    {"red/red_vector_max_noftz_f16x2/no_hint/src/f16x2/v4",
     RedVectorMaxNoftzF16x2::kind, false, false, 4, ScalarType::F16x2, "b32",
     "red.global.max.noftz.v4.f16x2", "0f3e003e00", 0x3e003e00ULL, "b32,f16x2"},
    {"red/red_vector_max_noftz_f16x2/with_policy/src/f16x2/v2",
     RedVectorMaxNoftzF16x2::kind, false, true, 2, ScalarType::F16x2, "b32",
     "red.global.max.noftz.L2::cache_hint.v2.f16x2", "0f3e003e00",
     0x3e003e00ULL, "b32,f16x2"},
    {"red/red_vector_max_noftz_f16x2/with_policy/src/f16x2/v4",
     RedVectorMaxNoftzF16x2::kind, false, true, 4, ScalarType::F16x2, "b32",
     "red.global.max.noftz.L2::cache_hint.v4.f16x2", "0f3e003e00",
     0x3e003e00ULL, "b32,f16x2"},
    {"red/red_vector_add_noftz_bf16x2/no_hint/src/bf16x2/v2",
     RedVectorAddNoftzBf16x2::kind, false, false, 2, ScalarType::BF16x2, "b32",
     "red.global.add.noftz.v2.bf16x2", "0f3fc03fc0", 0x3fc03fc0ULL, "b32"},
    {"red/red_vector_add_noftz_bf16x2/no_hint/src/bf16x2/v4",
     RedVectorAddNoftzBf16x2::kind, false, false, 4, ScalarType::BF16x2, "b32",
     "red.global.add.noftz.v4.bf16x2", "0f3fc03fc0", 0x3fc03fc0ULL, "b32"},
    {"red/red_vector_add_noftz_bf16x2/with_policy/src/bf16x2/v2",
     RedVectorAddNoftzBf16x2::kind, false, true, 2, ScalarType::BF16x2, "b32",
     "red.global.add.noftz.L2::cache_hint.v2.bf16x2", "0f3fc03fc0",
     0x3fc03fc0ULL, "b32"},
    {"red/red_vector_add_noftz_bf16x2/with_policy/src/bf16x2/v4",
     RedVectorAddNoftzBf16x2::kind, false, true, 4, ScalarType::BF16x2, "b32",
     "red.global.add.noftz.L2::cache_hint.v4.bf16x2", "0f3fc03fc0",
     0x3fc03fc0ULL, "b32"},
    {"red/red_vector_min_noftz_bf16x2/no_hint/src/bf16x2/v2",
     RedVectorMinNoftzBf16x2::kind, false, false, 2, ScalarType::BF16x2, "b32",
     "red.global.min.noftz.v2.bf16x2", "0f3fc03fc0", 0x3fc03fc0ULL, "b32"},
    {"red/red_vector_min_noftz_bf16x2/no_hint/src/bf16x2/v4",
     RedVectorMinNoftzBf16x2::kind, false, false, 4, ScalarType::BF16x2, "b32",
     "red.global.min.noftz.v4.bf16x2", "0f3fc03fc0", 0x3fc03fc0ULL, "b32"},
    {"red/red_vector_min_noftz_bf16x2/with_policy/src/bf16x2/v2",
     RedVectorMinNoftzBf16x2::kind, false, true, 2, ScalarType::BF16x2, "b32",
     "red.global.min.noftz.L2::cache_hint.v2.bf16x2", "0f3fc03fc0",
     0x3fc03fc0ULL, "b32"},
    {"red/red_vector_min_noftz_bf16x2/with_policy/src/bf16x2/v4",
     RedVectorMinNoftzBf16x2::kind, false, true, 4, ScalarType::BF16x2, "b32",
     "red.global.min.noftz.L2::cache_hint.v4.bf16x2", "0f3fc03fc0",
     0x3fc03fc0ULL, "b32"},
    {"red/red_vector_max_noftz_bf16x2/no_hint/src/bf16x2/v2",
     RedVectorMaxNoftzBf16x2::kind, false, false, 2, ScalarType::BF16x2, "b32",
     "red.global.max.noftz.v2.bf16x2", "0f3fc03fc0", 0x3fc03fc0ULL, "b32"},
    {"red/red_vector_max_noftz_bf16x2/no_hint/src/bf16x2/v4",
     RedVectorMaxNoftzBf16x2::kind, false, false, 4, ScalarType::BF16x2, "b32",
     "red.global.max.noftz.v4.bf16x2", "0f3fc03fc0", 0x3fc03fc0ULL, "b32"},
    {"red/red_vector_max_noftz_bf16x2/with_policy/src/bf16x2/v2",
     RedVectorMaxNoftzBf16x2::kind, false, true, 2, ScalarType::BF16x2, "b32",
     "red.global.max.noftz.L2::cache_hint.v2.bf16x2", "0f3fc03fc0",
     0x3fc03fc0ULL, "b32"},
    {"red/red_vector_max_noftz_bf16x2/with_policy/src/bf16x2/v4",
     RedVectorMaxNoftzBf16x2::kind, false, true, 4, ScalarType::BF16x2, "b32",
     "red.global.max.noftz.L2::cache_hint.v4.bf16x2", "0f3fc03fc0",
     0x3fc03fc0ULL, "b32"},
    {"red/red_vector_add_f32/no_hint/src/f32/v2", RedVectorAddF32::kind, false,
     false, 2, ScalarType::F32, "b32", "red.global.add.v2.f32", "1.5",
     0x3fc00000ULL, "b32,f32,u32,s32"},
    {"red/red_vector_add_f32/no_hint/src/f32/v4", RedVectorAddF32::kind, false,
     false, 4, ScalarType::F32, "b32", "red.global.add.v4.f32", "1.5",
     0x3fc00000ULL, "b32,f32,u32,s32"},
    {"red/red_vector_add_f32/with_policy/src/f32/v2", RedVectorAddF32::kind,
     false, true, 2, ScalarType::F32, "b32",
     "red.global.add.L2::cache_hint.v2.f32", "1.5", 0x3fc00000ULL,
     "b32,f32,u32,s32"},
    {"red/red_vector_add_f32/with_policy/src/f32/v4", RedVectorAddF32::kind,
     false, true, 4, ScalarType::F32, "b32",
     "red.global.add.L2::cache_hint.v4.f32", "1.5", 0x3fc00000ULL,
     "b32,f32,u32,s32"},
};

/** Borrow the actual ValueVector member from the live exact-class owner. */
WithLocs<ResolvedValueVector>& atomicValueSource(Instruction& instruction) {
  if (auto* form = dynamic_cast<AtomVectorAddNoftzF16*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<AtomVectorMinNoftzF16*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<AtomVectorMaxNoftzF16*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<AtomVectorAddNoftzBf16*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<AtomVectorMinNoftzBf16*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<AtomVectorMaxNoftzBf16*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<AtomVectorAddNoftzF16x2*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<AtomVectorMinNoftzF16x2*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<AtomVectorMaxNoftzF16x2*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<AtomVectorAddNoftzBf16x2*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<AtomVectorMinNoftzBf16x2*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<AtomVectorMaxNoftzBf16x2*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<AtomVectorAddF32*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<RedVectorAddNoftzF16*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<RedVectorMinNoftzF16*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<RedVectorMaxNoftzF16*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<RedVectorAddNoftzBf16*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<RedVectorMinNoftzBf16*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<RedVectorMaxNoftzBf16*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<RedVectorAddNoftzF16x2*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<RedVectorMinNoftzF16x2*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<RedVectorMaxNoftzF16x2*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<RedVectorAddNoftzBf16x2*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<RedVectorMinNoftzBf16x2*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<RedVectorMaxNoftzBf16x2*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<RedVectorAddF32*>(&instruction))
    return form->src;
  throw std::logic_error("Expected an exact vector atomic source class.");
}

/** Build a brace list from scalar register family members or one repeated literal. */
std::string atomicValuePack(unsigned arity, std::string_view prefix,
                            bool registers) {
  std::string result = "{";
  for (unsigned lane = 0; lane < arity; ++lane)
    result += (lane ? "," : "") + std::string(prefix) +
              (registers ? std::to_string(lane) : "");
  return result + "}";
}

/** Produce matched complete source; physical declaration changes never alter the logical type. */
std::string atomicValueText(const AtomicValueCase& item, bool literals,
                            std::string_view declared = "",
                            bool mixed = false) {
  const auto source_type = declared.empty() ? item.physical : declared;
  std::string pack =
      atomicValuePack(item.arity, literals ? item.literal : "s", !literals);
  if (mixed) {
    const auto end = pack.find(',');
    pack.replace(1, end - 1, item.literal);
  }
  std::string operation(item.operation);
  operation += " ";
  if (item.atom)
    operation += atomicValuePack(item.arity, "d", true) + ",";
  operation += "[data]," + pack + (item.hinted ? ",policy;" : ";");
  return ".version 9.3\n.target sm_90\n.global .align 16 .b8 "
         "data[256];\n.entry kernel(){ .reg ." +
         std::string(item.physical) + " d<8>; .reg ." +
         std::string(source_type) + " s<8>; .reg .b64 policy; " + operation +
         " }";
}

/** Resolve and release real source/parser/AST, guarding every container before inspection. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> ownedAtomicValue(
    std::string text, bool strict = true) {
  PtxSyntaxParser parser(text);
  const auto ast = parser.parseModule();
  EXPECT_TRUE(ast && ast.diagnostics.empty()) << text;
  if (!ast || !ast.diagnostics.empty())
    throw std::runtime_error("Atomic ValueVector fixture failed to parse: " +
                             text);
  auto result =
      strict ? resolveAndValidateModule(*ast) : resolveModuleOnly(*ast);
  if (result && (result->functions.size() != 1 ||
                 result->functions.front().body.size() != 1 ||
                 !result->functions.front().body.front())) {
    ADD_FAILURE()
        << "Atomic ValueVector fixture requires one owned instruction: "
        << text;
    throw std::runtime_error(
        "Atomic ValueVector fixture has unexpected owned structure: " + text);
  }
  return result;
}

/** Build views only from the actual owner's payload and location storage for synchronous checking. */
checker::OperandView atomicValueView(
    const WithLocs<ResolvedValueVector>& owner) {
  checker::OperandView view{.field_id = "src",
                            .actual_shape = checker::OperandShape::Vector,
                            .value_vector = &owner.value,
                            .vector_arity = owner.value.elements.size(),
                            .locations = owner.locs};
  for (size_t index = 0; index < owner.value.elements.size(); ++index) {
    if (const auto* reg =
            std::get_if<ResolvedRegisterRef>(&owner.value.elements[index])) {
      view.vector_element_shapes[index] = checker::OperandShape::Register;
      view.vector_element_types[index] =
          reg->declared_type.value_or(ScalarType::Invalid);
      view.vector_element_registers[index] = reg;
    } else {
      const auto& immediate =
          std::get<ResolvedImmediate>(owner.value.elements[index]);
      view.vector_element_shapes[index] = checker::OperandShape::Immediate;
      view.vector_element_types[index] = immediate.type;
      view.vector_element_immediates[index] = &immediate;
      view.vector_immediate_bits[index] = immediate.bits;
      view.vector_immediate_source_bits[index] = immediate.integer_source_bits;
      view.vector_immediate_negative[index] = immediate.is_negative;
    }
  }
  return view;
}

/** Locate each repeated written literal independently of the resolver's owned range mirrors. */
std::vector<SourceRange> atomicLiteralRanges(std::string_view text,
                                             std::string_view literal,
                                             unsigned arity) {
  std::vector<SourceRange> ranges;
  size_t offset = 0;
  for (unsigned lane = 0; lane < arity; ++lane) {
    offset = text.find(literal, offset);
    if (offset == std::string_view::npos)
      return {};
    SourcePos start{1, 1};
    for (const char character : text.substr(0, offset)) {
      if (character == '\n') {
        ++start.line;
        start.column = 1;
      } else
        ++start.column;
    }
    ranges.push_back(
        {start,
         {start.line, start.column + static_cast<int32_t>(literal.size())}});
    offset += literal.size();
  }
  return ranges;
}

/** Consume actual callback borrows synchronously; do not retain callback pointers or spans. */
struct AtomicValueObserver final : detail::IReferenceObserver {
  /** Expected owner, borrowed only while its instruction is alive. */
  const WithLocs<ResolvedValueVector>& owner;
  /** Number of callbacks for that actual owner address. */
  size_t calls = 0;
  /** Bind a live member for the duration of traversal. */
  explicit AtomicValueObserver(const WithLocs<ResolvedValueVector>& value)
      : owner(value) {}
  /** Compare actual payload and all ranges inside the callback only. */
  void value_vector(const ResolvedValueVector& value,
                    std::span<const SourceRange> ranges,
                    checker::AddressSymbolResolutionPolicy) override {
    if (&value != &owner.value)
      return;
    ++calls;
    EXPECT_TRUE(std::ranges::equal(ranges, owner.locs));
  }
};

/** Every branch has real matched physical carriers, Float sources and actual owner callbacks. */
TEST(AtomicVectorSources, AllBranchesRegisterLiteralAndMixedSources) {
  for (const auto& item : branches) {
    SCOPED_TRACE(item.id);
    auto control = ownedAtomicValue(atomicValueText(item, false));
    ASSERT_TRUE(control);
    size_t start = 0;
    while (start < item.allowed.size()) {
      const auto end = item.allowed.find(',', start);
      const auto carrier = item.allowed.substr(
          start, end == std::string_view::npos ? end : end - start);
      ASSERT_TRUE(ownedAtomicValue(atomicValueText(item, false, carrier)));
      ASSERT_TRUE(
          ownedAtomicValue(atomicValueText(item, false, carrier, true)));
      if (end == std::string_view::npos)
        break;
      start = end + 1;
    }
    for (const bool strict : {false, true}) {
      const auto text = atomicValueText(item, true);
      auto module = ownedAtomicValue(text, strict);
      ASSERT_TRUE(module);
      auto& instruction = *module->functions.front().body.front();
      EXPECT_EQ(instruction.instruction_kind(), item.kind);
      auto& source = atomicValueSource(instruction);
      ASSERT_EQ(source.value.elements.size(), item.arity);
      ASSERT_EQ(source.locs.size(), item.arity);
      const auto expected_ranges =
          atomicLiteralRanges(text, item.literal, item.arity);
      ASSERT_EQ(expected_ranges.size(), item.arity);
      EXPECT_EQ(source.locs, expected_ranges);
      EXPECT_EQ(source.value.source.kind, ResolvedVectorSourceKind::BraceList);
      for (const auto& lane : source.value.elements) {
        const auto* immediate = std::get_if<ResolvedImmediate>(&lane);
        ASSERT_NE(immediate, nullptr);
        EXPECT_EQ(immediate->type, item.logical);
        EXPECT_EQ(immediate->bits, item.bits);
        EXPECT_FALSE(immediate->integer_source_bits);
        EXPECT_FALSE(immediate->is_negative);
      }
      const auto view = atomicValueView(source);
      EXPECT_EQ(view.value_vector, &source.value);
      EXPECT_EQ(view.vector_arity, item.arity);
      AtomicValueObserver observer(source);
      instruction.visit_references(observer);
      EXPECT_EQ(observer.calls, 1u);
      EXPECT_TRUE(validateModule(
          *module, ModuleValidationPolicy::RequireCompleteContext));
    }
  }
}

/** Ordinary canonical current words survive real owner copies and reject retained bad tuples. */
TEST(AtomicVectorSources, CurrentWordsCopiesAndCausalLaneMutations) {
  for (const auto& item : branches) {
    SCOPED_TRACE(item.id);
    auto producer = ownedAtomicValue(atomicValueText(item, false));
    ASSERT_TRUE(producer);
    {
      auto& source =
          atomicValueSource(*producer->functions.front().body.front());
      source.value.elements[0] =
          ResolvedImmediate{.bits = item.bits, .type = item.logical};
    }
    ASSERT_TRUE(validateModule(*producer));
    ResolvedModule copied = *producer, assigned;
    assigned = copied;
    ResolvedFunction function = copied.functions.front(), function_assigned;
    function_assigned = function;
    assigned.functions.front() = std::move(function_assigned);
    assigned.functions.front().body.front() = function.body.front()->clone();
    producer = std::unexpected(ModuleResolveDiagnostics{});
    ASSERT_TRUE(validateModule(copied));
    ASSERT_TRUE(validateModule(assigned));
    auto& source = atomicValueSource(*assigned.functions.front().body.front());
    const auto valid = std::get<ResolvedImmediate>(source.value.elements[0]);
    for (unsigned mutation = 0; mutation < 4; ++mutation) {
      auto bad = valid;
      if (mutation == 0)
        bad.bits |= uint64_t{1} << (item.logical == ScalarType::F16 ||
                                            item.logical == ScalarType::BF16
                                        ? 16
                                        : 32);
      if (mutation == 1)
        bad.integer_source_bits = 0;
      if (mutation == 2)
        bad.is_negative = true;
      if (mutation == 3)
        bad.type =
            item.physical == "b16"
                ? ScalarType::B32
                : ScalarType::
                      F64;  // Definite wrong width; no packed/F32 Bit32 oracle.
      source.value.elements[0] = bad;
      EXPECT_FALSE(validateModule(assigned));
      source.value.elements[0] = valid;
      EXPECT_TRUE(validateModule(assigned));
    }
    std::get<ResolvedImmediate>(source.value.elements[0]).bits = 1;
    EXPECT_TRUE(validateModule(assigned));
  }
}

/** Integer origins and changed supplied syntax never become floating literals by physical lists. */
TEST(AtomicVectorSources, IntegerSourceAndSourceIdentityNegatives) {
  for (const auto& original : branches) {
    SCOPED_TRACE(original.id);
    auto item = original;
    item.literal = "1";
    EXPECT_FALSE(ownedAtomicValue(atomicValueText(item, true)));
    const auto text = atomicValueText(original, true);
    PtxSyntaxParser parser(text);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty()) << text;
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module);
    ASSERT_EQ(module->functions.size(), 1u);
    ASSERT_EQ(module->functions.front().body.size(), 1u);
    ASSERT_NE(module->functions.front().body.front(), nullptr);
    auto& source = atomicValueSource(*module->functions.front().body.front());
    std::get<ResolvedImmediate>(source.value.elements[0]).integer_source_bits =
        0;
    EXPECT_FALSE(validateModule(*ast, *module));
    std::get<ResolvedImmediate>(source.value.elements[0])
        .integer_source_bits.reset();
    item.literal = original.logical == ScalarType::F16x2 ||
                           original.logical == ScalarType::BF16x2
                       ? "0f00000002"
                       : "2.5";
    const auto changed_text = atomicValueText(item, true);
    PtxSyntaxParser changed_parser(changed_text);
    const auto changed = changed_parser.parseModule();
    ASSERT_TRUE(changed && changed.diagnostics.empty()) << changed_text;
    ASSERT_TRUE(resolveAndValidateModule(*changed));
    const auto mismatch = validateModule(*changed, *module);
    ASSERT_FALSE(mismatch);
    EXPECT_TRUE(
        std::ranges::any_of(mismatch.error(), [](const auto& diagnostic) {
          return diagnostic.kind ==
                 checker::CheckDiagnosticKind::ModuleSourceMismatch;
        }));
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir {
namespace {
/** An explicit lane list selects type membership, while ValueVector width stays independent. */
TEST(AtomicVectorSources,
     GenericElementListCannotWaivePhysicalWidthOrOwnerMirrors) {
  const SourceRange parent{{1, 1}, {1, 8}};
  WithLocs<ResolvedValueVector> owner;
  owner.value.source.range = parent;
  owner.value.elements = {
      ResolvedRegisterRef{.spelling = "a",
                          .register_class = ResolvedRegisterClass::General,
                          .declared_type = ScalarType::B64},
      ResolvedRegisterRef{.spelling = "b",
                          .register_class = ResolvedRegisterClass::General,
                          .declared_type = ScalarType::B64}};
  owner.locs = {SourceRange{{1, 2}, {1, 3}}, SourceRange{{1, 5}, {1, 6}}};
  constexpr uint8_t arities[]{2};
  constexpr ScalarType allowed[]{ScalarType::B64};
  checker::OperandDescriptor descriptor{
      .target_field_id = "src",
      .type_expression = {.kind =
                              checker::OperandTypeExpressionKind::FixedScalar,
                          .fixed_scalar_type = ScalarType::B64},
      .register_width_policy = base::ScalarTypeSizePolicy::SameWidth,
      .role = checker::OperandRole::Source,
      .access = checker::OperandAccess::Read,
      .allowed_shapes = checker::OperandShape::Vector,
      .allowed_vector_arities = arities,
      .vector_type_policy = checker::VectorTypePolicy::Element,
      .allowed_register_types = allowed,
      .source_value_vector = true};
  auto view = atomicValueView(owner);
  const auto check = [&] {
    return checker::check_operands(std::span{&descriptor, 1}, {},
                                   std::span{&view, 1}, {}, {});
  };
  ASSERT_TRUE(check());
  descriptor.type_expression.fixed_scalar_type = ScalarType::F16;
  EXPECT_TRUE(detail::element_register_type_allowed(
      ScalarType::B64, ScalarType::F16, base::ScalarTypeSizePolicy::SameWidth,
      allowed));  // Domain alone does not waive width.
  EXPECT_FALSE(check());
  descriptor.type_expression.fixed_scalar_type = ScalarType::B64;
  ASSERT_TRUE(check());
  const auto saved = view;
  view.value_vector = nullptr;
  EXPECT_FALSE(check());
  view = saved;
  view.vector_element_registers[0] =
      &std::get<ResolvedRegisterRef>(owner.value.elements[1]);
  EXPECT_FALSE(check());
  view = saved;
  view.vector_element_types[0] = ScalarType::B32;
  EXPECT_FALSE(check());
  view = saved;
  EXPECT_TRUE(check());
  std::get<ResolvedRegisterRef>(owner.value.elements[0]).register_class =
      ResolvedRegisterClass::Predicate;
  view = atomicValueView(owner);
  EXPECT_FALSE(check());
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir {
namespace {
/** Register-only family filtering stays independent of immediate Float lanes and named syntax. */
TEST(AtomicVectorSources, RegisterFamiliesAndBraceOnlySource) {
  for (const auto& item : branches) {
    if (item.logical != ScalarType::F16 && item.logical != ScalarType::F32)
      continue;
    const auto floating = item.logical == ScalarType::F16 ? "f16" : "f32";
    const auto integer = item.logical == ScalarType::F16 ? "u16" : "u32";
    ASSERT_TRUE(ownedAtomicValue(atomicValueText(item, false, floating)));
    ASSERT_TRUE(ownedAtomicValue(atomicValueText(item, false, integer, true)));
    auto mixed = atomicValueText(item, false, floating);
    const auto use = mixed.find("s1");
    ASSERT_NE(use, std::string::npos);
    mixed.replace(use, 2, "integer_lane");
    const auto operation = mixed.find(item.operation);
    ASSERT_NE(operation, std::string::npos);
    mixed.insert(operation,
                 ".reg ." + std::string(integer) + " integer_lane; ");
    EXPECT_FALSE(ownedAtomicValue(mixed));
    if (item.arity == 8)
      continue;  // No lawful named-v8 producer exists.
    auto named = atomicValueText(item, false);
    const auto pack = atomicValuePack(item.arity, "s", true);
    const auto source = named.find(pack);
    ASSERT_NE(source, std::string::npos);
    named.replace(source, pack.size(), "V");
    const auto instruction = named.find(item.operation);
    ASSERT_NE(instruction, std::string::npos);
    named.insert(instruction, ".reg .v" + std::to_string(item.arity) + " ." +
                                  std::string(item.physical) + " V; ");
    EXPECT_FALSE(ownedAtomicValue(named));
  }
}

/** Packed numeric construction remains reference-qualified unavailable despite typed word capacity. */
TEST(AtomicVectorSources, PackedConstructorsStaySourceExcluded) {
  for (const auto& original : branches) {
    if (original.logical != ScalarType::F16x2 &&
        original.logical != ScalarType::BF16x2)
      continue;
    ASSERT_TRUE(ownedAtomicValue(atomicValueText(original, true)));
    for (const auto raw : {std::pair{"0f7f800001", uint64_t{0x7f800001}},
                           std::pair{"0f80000000", uint64_t{0x80000000}},
                           std::pair{"0fffffffff", uint64_t{0xffffffff}}}) {
      auto item = original;
      item.literal = raw.first;
      auto module = ownedAtomicValue(atomicValueText(item, true));
      ASSERT_TRUE(module);
      const auto& value =
          atomicValueSource(*module->functions.front().body.front());
      for (const auto& lane : value.value.elements) {
        const auto* immediate = std::get_if<ResolvedImmediate>(&lane);
        ASSERT_NE(immediate, nullptr);
        EXPECT_EQ(immediate->bits, raw.second);
        EXPECT_EQ(immediate->type, original.logical);
        EXPECT_FALSE(immediate->integer_source_bits);
        EXPECT_FALSE(immediate->is_negative);
      }
    }
    for (const auto lexical :
         {"1.5", "0d3ff8000000000000", "+0f3e003e00", "-0f3e003e00",
          "(0f3e003e00)", "(0f3e003e00+0)"}) {
      auto item = original;
      item.literal = lexical;
      EXPECT_FALSE(ownedAtomicValue(atomicValueText(item, true)));
    }
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir {
namespace {
/** Compute an independent end-exclusive range for one ASCII fixture token. */
SourceRange atomicWrittenRange(std::string_view text, size_t offset,
                               size_t length) {
  if (offset == std::string_view::npos || offset + length > text.size())
    throw std::logic_error("Missing written fixture token.");
  SourcePos first{1, 1}, last;
  for (char character : text.substr(0, offset)) {
    if (character == '\n') {
      ++first.line;
      first.column = 1;
    } else
      ++first.column;
  }
  last = first;
  for (char character : text.substr(offset, length)) {
    if (character == '\n') {
      ++last.line;
      last.column = 1;
    } else
      ++last.column;
  }
  return {first, last};
}

/** Exact current source-leaf ranges independently derived from the ASCII brace fixture. */
std::vector<SourceRange> atomicSourceRanges(std::string_view text) {
  const auto address = text.find("[data],");
  const auto begin =
      address == std::string_view::npos ? address : text.find('{', address);
  if (begin == std::string_view::npos)
    throw std::logic_error("Missing atomic source brace.");
  std::vector<SourceRange> result;
  size_t first = begin + 1;
  for (;;) {
    const auto last = text.find_first_of(",}", first);
    if (last == std::string_view::npos || first == last)
      throw std::logic_error("Missing atomic scalar leaf.");
    SourcePos position{1, 1};
    for (char character : text.substr(0, first)) {
      if (character == '\n') {
        ++position.line;
        position.column = 1;
      } else
        ++position.column;
    }
    result.push_back({position,
                      {position.line,
                       position.column + static_cast<int32_t>(last - first)}});
    if (text[last] == '}')
      return result;
    first = last + 1;
  }
}

/** Actual generated vector/member/lane borrows survive owners and never use manual views. */
TEST(AtomicVectorSources, ActualGeneratedViewsForAllBranchesAndOwnerCopies) {
#if !defined(PTX_ATOMIC_VALUE_CHECKER_WRAP)
  GTEST_SKIP() << "Requires PTX_ATOMIC_VALUE_CHECKER_WRAP and the recorded "
                  "actual checker --wrap symbol.";
#else
  for (const auto& item : branches)
    for (unsigned mode = 0; mode < 3; ++mode) {
      SCOPED_TRACE(item.id);
      SCOPED_TRACE(mode);
      const auto text = atomicValueText(item, mode == 1, "", mode == 2);
      auto produced = ownedAtomicValue(text);
      ASSERT_TRUE(produced);
      ResolvedModule copied = *produced, assigned;
      assigned = copied;
      ResolvedFunction function = copied.functions.front(), function_assigned;
      function_assigned = function;
      assigned.functions.front() = std::move(function_assigned);
      assigned.functions.front().body.front() = function.body.front()->clone();
      produced = std::unexpected(ModuleResolveDiagnostics{});
      for (auto* owner : {&copied, &assigned}) {
        ASSERT_EQ(owner->functions.size(), 1u);
        ASSERT_EQ(owner->functions.front().body.size(), 1u);
        ASSERT_EQ(owner->functions.front().instruction_ranges.size(), 1u);
        auto& instruction = *owner->functions.front().body.front();
        auto& source = atomicValueSource(instruction);
        atomic_value_test_wrap::Capture capture;
        capture.member = &source;
        capture.logical = item.logical;
        capture.physical =
            item.physical == "b16" ? ScalarType::B16 : ScalarType::B32;
        capture.arity = item.arity;
        capture.bits = item.bits;
        capture.mode = mode;
        capture.ranges = atomicSourceRanges(text);
        const checker::Context context{
            .target = {.ptx_version = {9, 3}, .sm_version = 90},
            .instruction_range =
                owner->functions.front().instruction_ranges.front()};
        checker::CheckResult checked;
        {
          atomic_value_test_wrap::Scope scope(capture);
          checked = instruction.check(context);
        }
        EXPECT_TRUE(checked);
        EXPECT_TRUE(capture.valid);
        EXPECT_EQ(capture.calls, 1u);
        EXPECT_EQ(instruction.instruction_kind(), item.kind);
        EXPECT_TRUE(validateModule(*owner));
        AtomicValueObserver observer(source);
        instruction.visit_references(observer);
        EXPECT_EQ(observer.calls, 1u);
      }
    }
#endif
}

/** Mutate actual register variants, preserving independent actual-table and written-source checks. */
TEST(AtomicVectorSources, CurrentRegisterFactsFamiliesAndBothModuleApis) {
  for (const auto& item : branches) {
    SCOPED_TRACE(item.id);
    auto text = atomicValueText(item, false);
    const auto declaration = text.find(" s<8>");
    ASSERT_NE(declaration, std::string::npos);
    text.replace(declaration, 5, " %s<8>");
    const auto pack = text.find("[data],{");
    ASSERT_NE(pack, std::string::npos);
    text.replace(pack + 7, atomicValuePack(item.arity, "s", true).size(),
                 atomicValuePack(item.arity, "%s", true));
    PtxSyntaxParser parser(text);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto owner = resolveAndValidateModule(*ast);
    ASSERT_TRUE(owner);
    const auto source_identity = owner->source_identity;
    auto& instruction = *owner->functions.front().body.front();
    auto& source = atomicValueSource(instruction);
    ASSERT_EQ(source.value.elements.size(), item.arity);
    ASSERT_EQ(source.locs.size(), item.arity);
    auto* actual = std::get_if<ResolvedRegisterRef>(&source.value.elements[0]);
    ASSERT_NE(actual, nullptr);
    const auto healthy = *actual;
    ASSERT_TRUE(healthy.symbol_id);
    ASSERT_TRUE(healthy.parameterized_index);
    EXPECT_EQ(*healthy.parameterized_index, 0u);
    ASSERT_EQ(healthy.index, 0u);
    const auto saved_range = source.locs.front();
    EXPECT_EQ(saved_range, atomicSourceRanges(text).front());
    actual->index.reset();
    EXPECT_TRUE(validateModule(*owner));
    EXPECT_TRUE(validateModule(*ast, *owner));
    *actual = healthy;
    const checker::Context context{
        .target = {.ptx_version = {9, 3}, .sm_version = 90},
        .instruction_range =
            owner->functions.front().instruction_ranges.front()};
    const std::pair<ScalarType, std::string_view> types[] = {
        {ScalarType::B8, "b8"},         {ScalarType::B16, "b16"},
        {ScalarType::B32, "b32"},       {ScalarType::B64, "b64"},
        {ScalarType::B128, "b128"},     {ScalarType::F16, "f16"},
        {ScalarType::BF16, "bf16"},     {ScalarType::F16x2, "f16x2"},
        {ScalarType::BF16x2, "bf16x2"}, {ScalarType::F32, "f32"},
        {ScalarType::F64, "f64"},       {ScalarType::U16, "u16"},
        {ScalarType::S16, "s16"},       {ScalarType::U32, "u32"},
        {ScalarType::S32, "s32"},       {ScalarType::E4m3x4, "e4m3x4"}};
    for (const auto& [type, name] : types) {
      actual->declared_type = type;
      const auto needle = "," + std::string(name) + ",";
      const bool allowed =
          ("," + std::string(item.allowed) + ",").find(needle) !=
          std::string::npos;
      EXPECT_EQ(bool(instruction.check(context)), allowed);
      if (name != item.physical) {
        EXPECT_FALSE(validateModule(*owner));
        EXPECT_FALSE(validateModule(*ast, *owner));
      }
      *actual = healthy;
      EXPECT_TRUE(validateModule(*owner));
      EXPECT_TRUE(validateModule(*ast, *owner));
    }
    for (unsigned mutation = 0; mutation < 13; ++mutation) {
      SCOPED_TRACE(mutation);
      *actual = healthy;
      if (mutation == 0)
        actual->symbol_id.reset();
      if (mutation == 1)
        actual->symbol_id = binding::SymbolId{UINT32_MAX};
      if (mutation == 2)
        actual->symbol_id = owner->functions.front().symbol_id;
      if (mutation == 3)
        actual->parameterized_index.reset();
      if (mutation == 4)
        actual->parameterized_index = 1;
      if (mutation == 5)
        actual->parameterized_index = 8;
      if (mutation == 6)
        actual->parameterized_index = UINT32_MAX;
      if (mutation == 7)
        actual->spelling = "%s1";
      if (mutation == 8)
        actual->spelling = "%s8";
      if (mutation == 9)
        actual->spelling = "absent";
      if (mutation == 10)
        actual->register_class = ResolvedRegisterClass::Predicate;
      if (mutation == 11)
        actual->declared_type.reset();
      if (mutation == 12)
        actual->vector_width = 2;
      const auto current = validateModule(*owner),
                 supplied = validateModule(*ast, *owner);
      ASSERT_FALSE(current);
      ASSERT_FALSE(supplied);
      for (const auto* result : {&current, &supplied}) {
        EXPECT_FALSE(result->error().empty());
        EXPECT_TRUE(
            std::ranges::any_of(result->error(), [&](const auto& diagnostic) {
              return diagnostic.kind ==
                         checker::CheckDiagnosticKind::ModuleSourceMismatch &&
                     diagnostic.range == saved_range;
            }));
      }
      *actual = healthy;
      source.locs[0] = saved_range;
      EXPECT_TRUE(validateModule(*owner));
      EXPECT_TRUE(validateModule(*ast, *owner));
    }
    // Ordinary brace leaves require valid contained ranges, not exact lexical-token endpoints.
    const auto range_control = source;
    ASSERT_FALSE(source.locs.empty());
    ASSERT_NE(source.value.source.range, SourceRange{});
    ASSERT_NE(context.instruction_range, SourceRange{});
    source.locs[0].end.column++;
    EXPECT_NE(source.locs[0], saved_range);
    EXPECT_NE(source.locs[0].start, source.locs[0].end);
    EXPECT_EQ(source.locs[0].start, saved_range.start);
    EXPECT_EQ(source.locs[0].end.line, saved_range.end.line);
    EXPECT_EQ(source.locs[0].end.column, saved_range.end.column + 1);
    EXPECT_EQ(source.value, range_control.value);
    EXPECT_EQ(*actual, healthy);
    ASSERT_EQ(source.locs.size(), range_control.locs.size());
    for (size_t lane = 1; lane < source.locs.size(); ++lane)
      EXPECT_EQ(source.locs[lane], range_control.locs[lane]);
    ASSERT_TRUE(valid_value_vector_source(source.value, source.locs, true,
                                          context.instruction_range));
    EXPECT_EQ(owner->source_identity, source_identity);
    EXPECT_TRUE(validateModule(*owner));
    EXPECT_TRUE(validateModule(*ast, *owner));
    source.locs[0] = saved_range;
    EXPECT_TRUE(validateModule(*owner));
    EXPECT_TRUE(validateModule(*ast, *owner));
    auto donor_text = text;
    const auto brace = donor_text.find("[data],{");
    ASSERT_NE(brace, std::string::npos);
    donor_text.replace(brace + 8, 3, "%s1");
    auto donor = ownedAtomicValue(donor_text);
    ASSERT_TRUE(donor);
    const auto* donor_ref = std::get_if<ResolvedRegisterRef>(
        &atomicValueSource(*donor->functions.front().body.front())
             .value.elements[0]);
    ASSERT_NE(donor_ref, nullptr);
    *actual = *donor_ref;
    EXPECT_TRUE(validateModule(*owner));
    EXPECT_TRUE(validateModule(*ast, *owner));
    actual->index.reset();
    EXPECT_TRUE(validateModule(*owner));
    EXPECT_TRUE(validateModule(*ast, *owner));
    *actual = healthy;
    PtxSyntaxParser changed_parser(donor_text);
    const auto changed = changed_parser.parseModule();
    ASSERT_TRUE(changed && changed.diagnostics.empty());
    ASSERT_TRUE(resolveAndValidateModule(*changed));
    const auto mismatch = validateModule(*changed, *owner);
    ASSERT_FALSE(mismatch);
    EXPECT_TRUE(
        std::ranges::any_of(mismatch.error(), [](const auto& diagnostic) {
          return diagnostic.kind ==
                 checker::CheckDiagnosticKind::ModuleSourceMismatch;
        }));
  }
}

/** Ordinary vector components remain scalar leaves with complete family/base/selector facts. */
TEST(AtomicVectorSources, ComponentLaneProvenanceAndCoherentDonor) {
  for (const auto& item : branches) {
    SCOPED_TRACE(item.id);
    auto text = atomicValueText(item, false);
    const auto instruction = text.find(item.operation);
    ASSERT_NE(instruction, std::string::npos);
    text.insert(instruction,
                ".reg .v2 ." + std::string(item.physical) + " V<2>; ");
    const auto brace = text.find("[data],{");
    ASSERT_NE(brace, std::string::npos);
    text.replace(brace + 8, 2, "V1.x");
    PtxSyntaxParser parser(text);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto owner = resolveAndValidateModule(*ast);
    ASSERT_TRUE(owner);
    auto& source = atomicValueSource(*owner->functions.front().body.front());
    auto* actual = std::get_if<ResolvedRegisterRef>(&source.value.elements[0]);
    ASSERT_NE(actual, nullptr);
    const auto healthy = *actual;
    ASSERT_TRUE(healthy.component);
    ASSERT_TRUE(healthy.component->selector);
    ASSERT_TRUE(healthy.parameterized_index);
    for (unsigned mutation = 0; mutation < 10; ++mutation) {
      *actual = healthy;
      if (mutation == 0)
        actual->component->lane = 2;
      if (mutation == 1)
        actual->component->declaration_width = 4;
      if (mutation == 2)
        actual->component->base_range.end.column++;
      if (mutation == 3)
        actual->component->range.end.column--;
      if (mutation == 4)
        actual->component->selector->spelling = ".y";
      if (mutation == 5)
        actual->component->selector->range.start.column++;
      if (mutation == 6)
        actual->vector_width = 2;
      if (mutation == 7)
        actual->parameterized_index = 0;
      if (mutation == 8)
        actual->component->selector.reset();
      if (mutation == 9)
        actual->component.reset();
      EXPECT_FALSE(validateModule(*owner));
      EXPECT_FALSE(validateModule(*ast, *owner));
      *actual = healthy;
      EXPECT_TRUE(validateModule(*owner));
      EXPECT_TRUE(validateModule(*ast, *owner));
    }
    auto donor_text = text;
    const auto use = donor_text.find("V1.x");
    ASSERT_NE(use, std::string::npos);
    donor_text.replace(use, 4, "V0.y");
    auto donor = ownedAtomicValue(donor_text);
    ASSERT_TRUE(donor);
    const auto* value = std::get_if<ResolvedRegisterRef>(
        &atomicValueSource(*donor->functions.front().body.front())
             .value.elements[0]);
    ASSERT_NE(value, nullptr);
    *actual = *value;
    EXPECT_TRUE(validateModule(*owner));
    EXPECT_TRUE(validateModule(*ast, *owner));
    *actual = healthy;
  }
}

/** Negative source production has a precise stage/cause/range and no fictional owned payload. */
TEST(AtomicVectorSources, SourceCausesAndPartialContextStayIndependent) {
  for (const auto& item : branches) {
    SCOPED_TRACE(item.id);
    ASSERT_TRUE(ownedAtomicValue(atomicValueText(item, false)));
    auto bad = item;
    bad.literal = "1";
    const auto text = atomicValueText(bad, true);
    PtxSyntaxParser parser(text);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    const auto rejected = resolveAndValidateModule(*ast);
    ASSERT_FALSE(rejected);
    ASSERT_FALSE(rejected.error().empty());
    const auto ranges = atomicSourceRanges(text);
    ASSERT_EQ(ranges.size(), item.arity);
    EXPECT_TRUE(std::ranges::any_of(rejected.error(), [&](const auto&
                                                              diagnostic) {
      return diagnostic.stage() == ResolveDiagnosticStage::Resolution &&
             !diagnostic.checker_kind && diagnostic.range == ranges.front() &&
             !diagnostic.message.empty();
    }));  // No successful strict owner exists for this source; current tests use healthy producers.
    const auto wrong = atomicValueText(item, false, "b64");
    PtxSyntaxParser wrong_parser(wrong);
    const auto wrong_ast = wrong_parser.parseModule();
    ASSERT_TRUE(wrong_ast && wrong_ast.diagnostics.empty());
    const auto wrong_result = resolveAndValidateModule(*wrong_ast);
    ASSERT_FALSE(wrong_result);
    ASSERT_FALSE(wrong_result.error().empty());
    const auto wrong_ranges = atomicSourceRanges(wrong);
    EXPECT_TRUE(std::ranges::any_of(wrong_result.error(), [&](const auto&
                                                                  diagnostic) {
      return diagnostic.stage() == ResolveDiagnosticStage::Resolution &&
             !diagnostic.checker_kind &&
             diagnostic.range == wrong_ranges.front() &&
             diagnostic.message ==
                 "Value-vector register lane has an incompatible scalar type.";
    }));
    auto partial = atomicValueText(item, false);
    const auto header = partial.find(".global");
    ASSERT_NE(header, std::string::npos);
    partial.erase(0, header);
    PtxSyntaxParser partial_parser(partial);
    const auto partial_ast = partial_parser.parseModule();
    ASSERT_TRUE(partial_ast && partial_ast.diagnostics.empty());
    auto owner = resolveModuleOnly(*partial_ast);
    ASSERT_TRUE(owner);
    EXPECT_TRUE(
        validateModule(*owner, ModuleValidationPolicy::AvailableContext));
    EXPECT_TRUE(validateModule(*partial_ast, *owner,
                               ModuleValidationPolicy::AvailableContext));
    const auto strict =
        validateModule(*owner, ModuleValidationPolicy::RequireCompleteContext);
    ASSERT_FALSE(strict);
    EXPECT_TRUE(std::ranges::any_of(strict.error(), [](const auto& diagnostic) {
      return diagnostic.kind ==
             checker::CheckDiagnosticKind::MissingValidationContext;
    }));
    auto& source = atomicValueSource(*owner->functions.front().body.front());
    auto* current = std::get_if<ResolvedRegisterRef>(&source.value.elements[0]);
    ASSERT_NE(current, nullptr);
    const auto saved = *current;
    current->declared_type = ScalarType::F64;
    EXPECT_FALSE(
        validateModule(*owner, ModuleValidationPolicy::AvailableContext));
    EXPECT_FALSE(validateModule(*partial_ast, *owner,
                                ModuleValidationPolicy::AvailableContext));
    *current = saved;
    EXPECT_TRUE(
        validateModule(*owner, ModuleValidationPolicy::AvailableContext));
    EXPECT_TRUE(validateModule(*partial_ast, *owner,
                               ModuleValidationPolicy::AvailableContext));
    auto full = ownedAtomicValue(atomicValueText(item, false));
    ASSERT_TRUE(full);
    auto& current_instruction = *full->functions.front().body.front();
    checker::Context context{
        .target = {.ptx_version = {8, 1}, .sm_version = 90},
        .instruction_range =
            full->functions.front().instruction_ranges.front()};
    ASSERT_TRUE(current_instruction.check(context));
    context.target.ptx_version = {8, 0};
    EXPECT_FALSE(current_instruction.check(context));
    context.target.ptx_version = {8, 1};
    context.target.sm_version = 89;
    EXPECT_FALSE(current_instruction.check(context));
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir {
namespace {
/** Current canonical logical words and source-written association are independent datasets. */
TEST(AtomicVectorSources, LogicalCurrentWordsBothApisAndMirrorFailures) {
  for (const auto& item : branches) {
    SCOPED_TRACE(item.id);
    const auto text = atomicValueText(item, true);
    PtxSyntaxParser parser(text);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto owner = resolveAndValidateModule(*ast);
    ASSERT_TRUE(owner);
    auto& instruction = *owner->functions.front().body.front();
    auto& source = atomicValueSource(instruction);
    ASSERT_EQ(source.value.elements.size(), item.arity);
    ASSERT_EQ(source.locs.size(), item.arity);
    auto* actual = std::get_if<ResolvedImmediate>(&source.value.elements[0]);
    ASSERT_NE(actual, nullptr);
    const auto healthy = *actual;
    for (unsigned mutation = 0; mutation < 4; ++mutation) {
      *actual = healthy;
      if (mutation == 0)
        actual->bits |= uint64_t{1} << (item.physical == "b16" ? 16 : 32);
      if (mutation == 1)
        actual->integer_source_bits = 0;
      if (mutation == 2)
        actual->is_negative = true;
      if (mutation == 3)
        actual->type =
            item.physical == "b16" ? ScalarType::B32 : ScalarType::F64;
      if (mutation == 1) {
        ASSERT_TRUE(actual->integer_source_bits.has_value());
        EXPECT_EQ(*actual->integer_source_bits, 0u);
      }
      const checker::Context context{
          .target = {.ptx_version = {9, 3}, .sm_version = 90},
          .instruction_range =
              owner->functions.front().instruction_ranges.front()};
      const auto direct = instruction.check(context),
                 current = validateModule(*owner),
                 supplied = validateModule(*ast, *owner);
      for (const auto* result : {&direct, &current, &supplied}) {
        ASSERT_FALSE(*result);
        EXPECT_TRUE(
            std::ranges::any_of(result->error(), [&](const auto& diagnostic) {
              return diagnostic.kind ==
                         checker::CheckDiagnosticKind::ImmediateValueMismatch &&
                     diagnostic.range == source.locs.front();
            }));
      }
      *actual = healthy;
      EXPECT_TRUE(validateModule(*owner));
      EXPECT_TRUE(validateModule(*ast, *owner));
    }
    actual->bits = 1;
    EXPECT_TRUE(validateModule(*owner));
    EXPECT_TRUE(validateModule(*ast, *owner));
    *actual = healthy;
    // No assertion decides a cache-free current Bit16/Bit32 arm in any logical format.
    const uint8_t arities[]{static_cast<uint8_t>(item.arity)};
    const ScalarType allowed[]{item.physical == "b16" ? ScalarType::B16
                                                      : ScalarType::B32};
    checker::OperandDescriptor descriptor{
        .target_field_id = "src",
        .type_expression = {.kind =
                                checker::OperandTypeExpressionKind::FixedScalar,
                            .fixed_scalar_type = item.logical},
        .register_width_policy = base::ScalarTypeSizePolicy::SameWidth,
        .role = checker::OperandRole::Source,
        .access = checker::OperandAccess::Read,
        .allowed_shapes = checker::OperandShape::Vector,
        .allowed_vector_arities = arities,
        .vector_type_policy = checker::VectorTypePolicy::Element,
        .allowed_register_types = allowed,
        .source_value_vector = true};
    auto view = atomicValueView(source);
    const auto check = [&] {
      return checker::check_operands(std::span{&descriptor, 1}, {},
                                     std::span{&view, 1}, {}, {});
    };
    ASSERT_TRUE(check());
    const auto saved = view;
    auto shadow = source;
    ResolvedRegisterVector registers;
    ResolvedMovValueVector mov;
    for (unsigned mutation = 0; mutation < 10; ++mutation) {
      view = saved;
      if (mutation == 0)
        view.value_vector = nullptr;
      if (mutation == 1)
        view.value_vector = &shadow.value;
      if (mutation == 2)
        view.vector_element_immediates[0] =
            &std::get<ResolvedImmediate>(shadow.value.elements[0]);
      if (mutation == 3) {
        ASSERT_TRUE(view.vector_immediate_bits[0].has_value());
        *view.vector_immediate_bits[0] ^= 1;
      }
      if (mutation == 4)
        view.vector_immediate_source_bits[0] = 0;
      if (mutation == 5)
        view.vector_immediate_negative[0] = true;
      if (mutation == 6)
        view.vector_element_types[0] = ScalarType::F64;
      if (mutation == 7)
        view.vector_arity = item.arity - 1;
      if (mutation == 8)
        view.register_vector = &registers;
      if (mutation == 9)
        view.mov_value_vector = &mov;
      EXPECT_FALSE(check());
      view = saved;
      EXPECT_TRUE(check());
    }
    descriptor.source_value_vector = false;
    EXPECT_FALSE(check());
    descriptor.source_value_vector = true;
    EXPECT_TRUE(check());
    source.value.elements.pop_back();
    EXPECT_FALSE(validateModule(*owner));
    EXPECT_FALSE(validateModule(*ast, *owner));
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir {
namespace {
/** A coherent named projection is still outside these brace-only source descriptors. */
TEST(AtomicVectorSources, CoherentNamedSourceNeverRescuesBraceOnlyPermission) {
  for (const auto& item : branches) {
    if (item.arity == 8)
      continue;  // Named v8 is structurally unavailable, not a healthy named control.
    auto text = atomicValueText(item, false);
    const auto operation = text.find(item.operation);
    ASSERT_NE(operation, std::string::npos);
    text.insert(operation, ".reg .v" + std::to_string(item.arity) + " ." +
                               std::string(item.physical) + " V<2>; ");
    const auto brace = text.find("[data],{");
    ASSERT_NE(brace, std::string::npos);
    text.replace(brace + 8, 2, "V1.x");
    auto owner = ownedAtomicValue(text);
    ASSERT_TRUE(owner);
    auto& instruction = *owner->functions.front().body.front();
    auto& source = atomicValueSource(instruction);
    const auto saved = source;
    const auto* component =
        std::get_if<ResolvedRegisterRef>(&source.value.elements[0]);
    ASSERT_NE(component, nullptr);
    ASSERT_TRUE(component->component);
    ASSERT_TRUE(component->component->selector);
    auto base = *component;
    base.spelling = component->component->base_spelling;
    base.component.reset();
    base.vector_width = item.arity;
    const auto range = component->component->base_range;
    source.value.source = {.kind = ResolvedVectorSourceKind::NamedVector,
                           .whole_base =
                               WithLocs<ResolvedVectorRegisterRef>{
                                   ResolvedVectorRegisterRef{base}, range},
                           .range = range};
    for (size_t index = 0; index < source.value.elements.size(); ++index) {
      auto projection = base;
      projection.vector_width.reset();
      projection.component = ResolvedRegisterComponent{
          .lane = static_cast<uint8_t>(index),
          .declaration_width = static_cast<uint8_t>(item.arity),
          .origin = RegisterComponentOrigin::NamedProjection,
          .base_spelling = base.spelling,
          .base_range = range,
          .range = range};
      source.value.elements[index] = projection;
      source.locs[index] = range;
    }
    ASSERT_TRUE(valid_value_vector_source(
        source.value, source.locs, true,
        owner->functions.front().instruction_ranges.front()));
    const checker::Context context{
        .target = {.ptx_version = {9, 3}, .sm_version = 90},
        .instruction_range =
            owner->functions.front().instruction_ranges.front()};
    EXPECT_FALSE(instruction.check(context));
    EXPECT_FALSE(validateModule(*owner));
    source = saved;
    EXPECT_TRUE(validateModule(*owner));
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir {
namespace {
/** Borrow only the thirteen existing atomic destination members, never a red source cast. */
WithLocs<ResolvedRegisterVector>& atomicValueDestination(
    Instruction& instruction) {
  if (auto* form = dynamic_cast<AtomVectorAddNoftzF16*>(&instruction))
    return form->dst;
  if (auto* form = dynamic_cast<AtomVectorMinNoftzF16*>(&instruction))
    return form->dst;
  if (auto* form = dynamic_cast<AtomVectorMaxNoftzF16*>(&instruction))
    return form->dst;
  if (auto* form = dynamic_cast<AtomVectorAddNoftzBf16*>(&instruction))
    return form->dst;
  if (auto* form = dynamic_cast<AtomVectorMinNoftzBf16*>(&instruction))
    return form->dst;
  if (auto* form = dynamic_cast<AtomVectorMaxNoftzBf16*>(&instruction))
    return form->dst;
  if (auto* form = dynamic_cast<AtomVectorAddNoftzF16x2*>(&instruction))
    return form->dst;
  if (auto* form = dynamic_cast<AtomVectorMinNoftzF16x2*>(&instruction))
    return form->dst;
  if (auto* form = dynamic_cast<AtomVectorMaxNoftzF16x2*>(&instruction))
    return form->dst;
  if (auto* form = dynamic_cast<AtomVectorAddNoftzBf16x2*>(&instruction))
    return form->dst;
  if (auto* form = dynamic_cast<AtomVectorMinNoftzBf16x2*>(&instruction))
    return form->dst;
  if (auto* form = dynamic_cast<AtomVectorMaxNoftzBf16x2*>(&instruction))
    return form->dst;
  if (auto* form = dynamic_cast<AtomVectorAddF32*>(&instruction))
    return form->dst;
  throw std::logic_error(
      "Expected an atom class with an actual destination member.");
}
/** Unbound standalone facts stay unknown; real declaration ownership belongs to separate controls. */
TEST(AtomicVectorSources,
     StandaloneUnknownFactsDoNotBecomeInventedDeclarations) {
  for (const auto& item : branches) {
    std::string text(item.operation);
    text += " ";
    if (item.atom)
      text += atomicValuePack(item.arity, "%d", true) + ",";
    text += "[%rd0]," + atomicValuePack(item.arity, "%s", true) +
            (item.hinted ? ",%rd1;" : ";");
    PtxSyntaxParser parser(text);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto instruction = resolveInstruction(*ast);
    ASSERT_TRUE(instruction);
    EXPECT_EQ((*instruction)->instruction_kind(), item.kind);
    const auto& source = atomicValueSource(**instruction);
    ASSERT_EQ(source.value.elements.size(), item.arity);
    for (const auto& lane : source.value.elements) {
      const auto* reg = std::get_if<ResolvedRegisterRef>(&lane);
      ASSERT_NE(reg, nullptr);
      EXPECT_FALSE(reg->symbol_id);
      EXPECT_FALSE(reg->declared_type);
    }
    // No module-owned table or source-dead module proof is claimed for an unbound instruction.
  }
}

/** Qualifiers, address alignment, cache carriers and write sinks remain separate from source admission. */
TEST(AtomicVectorSources,
     AddressCacheDestinationAndSuffixControlsRetainCauses) {
  for (const auto& item : branches) {
    SCOPED_TRACE(item.id);
    const auto text = atomicValueText(item, false);
    ASSERT_TRUE(ownedAtomicValue(text));
    auto unaligned = text;
    const auto alignment = unaligned.find(".align 16");
    ASSERT_NE(alignment, std::string::npos);
    unaligned.replace(alignment, 9, ".align 1");
    PtxSyntaxParser address_parser(unaligned);
    const auto address_ast = address_parser.parseModule();
    ASSERT_TRUE(address_ast && address_ast.diagnostics.empty());
    const auto address_result = resolveAndValidateModule(*address_ast);
    ASSERT_FALSE(address_result);
    ASSERT_FALSE(address_result.error().empty());
    const auto address_range =
        atomicWrittenRange(unaligned, unaligned.find("[data]"), 6);
    EXPECT_TRUE(std::ranges::any_of(
        address_result.error(), [&](const auto& diagnostic) {
          return diagnostic.stage() == ResolveDiagnosticStage::Checking &&
                 diagnostic.checker_kind ==
                     checker::CheckDiagnosticKind::AddressAlignmentMismatch &&
                 diagnostic.range == address_range &&
                 diagnostic.message.find("aligned to 1 byte") !=
                     std::string::npos;
        }));
    if (item.hinted) {
      auto cache = text;
      const auto declaration = cache.find(".reg .b64 policy");
      ASSERT_NE(declaration, std::string::npos);
      cache.replace(declaration, 16, ".reg .b32 policy");
      PtxSyntaxParser cache_parser(cache);
      const auto cache_ast = cache_parser.parseModule();
      ASSERT_TRUE(cache_ast && cache_ast.diagnostics.empty());
      const auto result = resolveAndValidateModule(*cache_ast);
      ASSERT_FALSE(result);
      ASSERT_FALSE(result.error().empty());
      const auto cache_range =
          atomicWrittenRange(cache, cache.rfind("policy"), 6);
      EXPECT_TRUE(
          std::ranges::any_of(result.error(), [&](const auto& diagnostic) {
            return diagnostic.range == cache_range &&
                   diagnostic.stage() == ResolveDiagnosticStage::Checking &&
                   diagnostic.checker_kind ==
                       checker::CheckDiagnosticKind::OperandTypeMismatch;
          }));
    }
    auto suffix = item;
    std::string operation(item.operation);
    const auto noftz = operation.find(".noftz");
    if (noftz == std::string::npos)
      operation.insert(operation.find(".v"), ".noftz");
    else
      operation.erase(noftz, 6);
    suffix.operation = operation;
    const auto suffix_text = atomicValueText(suffix, false);
    PtxSyntaxParser suffix_parser(suffix_text);
    const auto suffix_ast = suffix_parser.parseModule();
    ASSERT_TRUE(suffix_ast && suffix_ast.diagnostics.empty());
    const auto suffix_result = resolveAndValidateModule(*suffix_ast);
    ASSERT_FALSE(suffix_result);
    ASSERT_FALSE(suffix_result.error().empty());
    const auto suffix_start = suffix_text.find(operation);
    const auto suffix_range = atomicWrittenRange(
        suffix_text, suffix_start,
        suffix_text.find(';', suffix_start) - suffix_start + 1);
    EXPECT_TRUE(
        std::ranges::any_of(suffix_result.error(), [&](const auto& diagnostic) {
          return diagnostic.range == suffix_range &&
                 diagnostic.stage() == ResolveDiagnosticStage::Resolution &&
                 !diagnostic.checker_kind && !diagnostic.message.empty();
        }));
    if (item.atom) {
      PtxSyntaxParser parser(text);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      auto owner = resolveAndValidateModule(*ast);
      ASSERT_TRUE(owner);
      auto& destination =
          atomicValueDestination(*owner->functions.front().body.front());
      const auto saved = destination;
      for (auto& lane : destination.value.elements)
        lane.reset();
      EXPECT_FALSE(validateModule(*owner));
      EXPECT_FALSE(validateModule(*ast, *owner));
      destination = saved;
      EXPECT_TRUE(validateModule(*owner));
      EXPECT_TRUE(validateModule(*ast, *owner));
    }
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir {
namespace {
/** Reuse the existing 1.5 golden across admitted numeric spellings, without a new converter oracle. */
TEST(AtomicVectorSources,
     NumericProfilesAndIntegerResultExpressionsStayDistinct) {
  for (const auto& original : branches) {
    SCOPED_TRACE(original.id);
    if (original.logical != ScalarType::F16x2 &&
        original.logical != ScalarType::BF16x2) {
      for (const auto literal :
           {"1.5", "0d3ff8000000000000", "(1.0+0.5)", "0f3fc00000"}) {
        auto item = original;
        item.literal = literal;
        auto owner = ownedAtomicValue(atomicValueText(item, true));
        ASSERT_TRUE(owner);
        const auto& source =
            atomicValueSource(*owner->functions.front().body.front());
        ASSERT_EQ(source.value.elements.size(), item.arity);
        for (const auto& lane : source.value.elements) {
          const auto* immediate = std::get_if<ResolvedImmediate>(&lane);
          ASSERT_NE(immediate, nullptr);
          EXPECT_EQ(immediate->type, item.logical);
          EXPECT_EQ(immediate->bits, item.bits);
          EXPECT_FALSE(immediate->integer_source_bits);
          EXPECT_FALSE(immediate->is_negative);
        }
      }
    }
    for (const auto literal : {"(1.0==1.0)", "!0", "(1.0&&1.0)"}) {
      auto item = original;
      item.literal = literal;
      const auto text = atomicValueText(item, true);
      PtxSyntaxParser parser(text);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      const auto rejected = resolveAndValidateModule(*ast);
      ASSERT_FALSE(rejected);
      ASSERT_FALSE(rejected.error().empty());
      const auto ranges = atomicSourceRanges(text);
      ASSERT_EQ(ranges.size(), item.arity);
      SCOPED_TRACE(literal);
      for (const auto& diagnostic : rejected.error()) {
        std::cerr << "atomic-expression|" << item.id << '|' << literal
                  << "|stage=" << int(diagnostic.stage()) << "|checker="
                  << (diagnostic.checker_kind ? int(*diagnostic.checker_kind)
                                              : -1)
                  << "|range=" << diagnostic.range.start.line << ':'
                  << diagnostic.range.start.column << '-'
                  << diagnostic.range.end.line << ':'
                  << diagnostic.range.end.column << '|' << diagnostic.message
                  << '\n';
      }

      const bool early_evaluation_failure =
          std::string_view(literal) == "(1.0&&1.0)";
      auto expected_range = ranges.front();
      if (early_evaluation_failure) {
        ++expected_range.start.column;
        --expected_range.end.column;
      }
      // Float logical operands fail evaluation before an Integer producer exists; retain that Source_N_A control.
      EXPECT_TRUE(
          std::ranges::any_of(rejected.error(), [&](const auto& diagnostic) {
            return diagnostic.stage() == ResolveDiagnosticStage::Resolution &&
                   !diagnostic.checker_kind &&
                   diagnostic.range == expected_range &&
                   diagnostic.message.find(early_evaluation_failure
                                               ? "Invalid constant expression"
                                               : "Integer literal") !=
                       std::string::npos;
          }));
    }
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
