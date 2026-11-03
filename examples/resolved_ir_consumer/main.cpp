#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <iostream>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <ptx_frontend/base/ptx_integer.hpp>
#include <ptx_frontend/base/ptx_target.hpp>
#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/cst/ptx_cst_parser.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_descriptors.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>
#include <ptx_frontend/resolved_ir/ptx_tcgen_descriptors.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#ifdef PTX_PACKED_ATOMIC_CAPTURE_GENERATED_VIEWS
namespace ptx_frontend::resolved_ir::packed_atomic_projection {
/** Actual member borrowed only during the immediately enclosed generated check call. */
const WithLocs<RegOrImm>* expected = nullptr;
/** Logical format selected by that actual instruction, independent of current payload mutations. */
ScalarType logical = ScalarType::Invalid;
/** Count actual generated src views, never a hand-built or visitor projection. */
unsigned observed = 0;
/** Accumulate exact pointer/value/location/descriptor mirror checks. */
bool mirrors = true;
#define PACKED_ATOMIC_OPERAND_PARAMETERS                           \
  std::span<const checker::OperandDescriptor> descriptors,         \
      std::span<const checker::FieldView> fields,                  \
      std::span<const checker::OperandView> views,                 \
      std::span<const checker::OperandTypeCompatibilityDescriptor> \
          compatibility, const checker::Context &context,          \
      const MatrixInstructionDescriptor *matrix
/** Call the matching library checker after the test-only link wrapper observes its arguments. */
extern "C" checker::CheckResult
    original_operands(PACKED_ATOMIC_OPERAND_PARAMETERS) asm(
        "__real__ZN12ptx_frontend11resolved_ir7checker14check_"
        "operandsESt4spanIKNS1_17OperandDescriptorELm18446744073709551615EES2_"
        "IKNS1_9FieldViewELm18446744073709551615EES2_IKNS1_"
        "11OperandViewELm18446744073709551615EES2_IKNS1_"
        "34OperandTypeCompatibilityDescriptorELm18446744073709551615EERKNS1_"
        "7ContextEPKNS0_27MatrixInstructionDescriptorE");
/** Intercept only the real generated checker call; production code and API remain untouched. */
extern "C" checker::CheckResult
    generated_operands(PACKED_ATOMIC_OPERAND_PARAMETERS) asm(
        "__wrap__ZN12ptx_frontend11resolved_ir7checker14check_"
        "operandsESt4spanIKNS1_17OperandDescriptorELm18446744073709551615EES2_"
        "IKNS1_9FieldViewELm18446744073709551615EES2_IKNS1_"
        "11OperandViewELm18446744073709551615EES2_IKNS1_"
        "34OperandTypeCompatibilityDescriptorELm18446744073709551615EERKNS1_"
        "7ContextEPKNS0_27MatrixInstructionDescriptorE");
/** Consume borrowed views synchronously, then forward the real checker result unchanged. */
checker::CheckResult generated_operands(PACKED_ATOMIC_OPERAND_PARAMETERS) {
  if (expected)
    for (const auto& view : views)
      if (view.field_id == "src") {
        ++observed;
        const auto descriptor =
            std::ranges::find(descriptors, std::string_view("src"),
                              &checker::OperandDescriptor::target_field_id);
        bool valid = descriptor != descriptors.end() &&
                     descriptor->type_expression.fixed_scalar_type == logical &&
                     descriptor->register_width_policy ==
                         base::ScalarTypeSizePolicy::SameWidth &&
                     view.locations.data() == expected->locs.data() &&
                     std::ranges::equal(view.locations, expected->locs);
        if (const auto* reg =
                std::get_if<ResolvedRegisterRef>(&expected->value))
          valid = valid &&
                  view.actual_shape == checker::OperandShape::Register &&
                  view.register_ref == reg &&
                  view.register_type == reg->declared_type &&
                  view.register_symbol_id == reg->symbol_id &&
                  view.register_class == reg->register_class &&
                  view.register_vector_width == reg->vector_width &&
                  !view.immediate_type && !view.immediate_bits &&
                  !view.integer_source_bits;
        else if (const auto* word =
                     std::get_if<ResolvedImmediate>(&expected->value))
          valid = valid &&
                  view.actual_shape == checker::OperandShape::Immediate &&
                  !view.register_ref && !view.register_type &&
                  !view.register_symbol_id && !view.register_class &&
                  !view.register_vector_width &&
                  view.immediate_type == word->type &&
                  view.immediate_bits == word->bits &&
                  view.immediate_is_negative == word->is_negative &&
                  view.integer_source_bits == word->integer_source_bits;
        else
          valid = false;
        mirrors = mirrors && valid;
      }
  return original_operands(descriptors, fields, views, compatibility, context,
                           matrix);
}
#undef PACKED_ATOMIC_OPERAND_PARAMETERS
}  // namespace ptx_frontend::resolved_ir::packed_atomic_projection
#endif

#if defined(PTX_PACKED_SETP_CHECKER_WRAP)
namespace ptx_frontend::resolved_ir::packed_setp_test_wrap {
/** Expected values own no library borrows except the explicit member during one check. */
struct Capture {
  /** The current instruction keeps this direct member alive for the synchronous check. */
  const WithLocs<RegOrImm>* member = nullptr;
  /** Existing direct source field and independently known logical format. */
  std::string_view field;
  ScalarType logical = ScalarType::Invalid;
  /** Independent complete ASCII token range; end is exclusive. */
  SourceRange range;
  /** Expected source arm/type/word/origin, derived from the case rather than the view. */
  bool immediate = false;
  ScalarType type = ScalarType::Invalid;
  uint64_t bits = 0;
  std::optional<uint64_t> source_bits;
  bool negative = false;
  /** Register spelling is a case literal; no callback-supplied pointer is retained. */
  std::string_view spelling;
  /** Only value results escape interception; incoming views/spans never do. */
  unsigned calls = 0;
  bool valid = true;
};
/** One thread's synchronous observation; cleared before the owner can move or die. */
thread_local Capture* active = nullptr;
/** Restore any outer observation even if the real checker exits exceptionally. */
struct Scope {
  /** Previous test-owned capture; never a callback-owned borrow. */
  Capture* previous;
  /** Install an observation only for the surrounding check expression. */
  explicit Scope(Capture& value) : previous(active) { active = &value; }
  /** End interception before any caller destroys or reassigns an owner. */
  ~Scope() { active = previous; }
};
/** Compute an expected source-token range independently of parser/IR locations. */
SourceRange token_range(std::string_view text, bool first) {
  const auto operation = text.find("setp.eq");
  const auto comma = text.find(',', operation);
  const auto sibling =
      text.find(',', comma == std::string_view::npos ? comma : comma + 1);
  const auto start =
      first ? (comma == std::string_view::npos ? comma : comma + 1)
            : (sibling == std::string_view::npos ? sibling : sibling + 1);
  const auto end = text.find_first_of(",;", start);
  if (operation == std::string_view::npos || start == std::string_view::npos ||
      end == std::string_view::npos)
    throw std::logic_error("Expected complete packed SETP source token.");
  SourcePos position{1, 1};
  for (const char character : text.substr(0, start)) {
    if (character == '\n') {
      ++position.line;
      position.column = 1;
    } else
      ++position.column;
  }
  return {position,
          {position.line, position.column + static_cast<int32_t>(end - start)}};
}
}  // namespace ptx_frontend::resolved_ir::packed_setp_test_wrap
#define PACKED_SETP_PARAMS                                                    \
  std::span<const ptx_frontend::resolved_ir::checker::OperandDescriptor>      \
      descriptors,                                                            \
      std::span<const ptx_frontend::resolved_ir::checker::FieldView> fields,  \
      std::span<const ptx_frontend::resolved_ir::checker::OperandView> views, \
      std::span<const ptx_frontend::resolved_ir::checker::                    \
                    OperandTypeCompatibilityDescriptor>                       \
          compatibility,                                                      \
      const ptx_frontend::resolved_ir::checker::Context &context,             \
      const ptx_frontend::resolved_ir::MatrixInstructionDescriptor *matrix
/** Original checker selected by the test-only linker wrap, with identical arguments/result. */
extern "C" ptx_frontend::resolved_ir::checker::CheckResult
    packed_setp_real_operands(PACKED_SETP_PARAMS) asm(
        "__real__ZN12ptx_frontend11resolved_ir7checker14check_"
        "operandsESt4spanIKNS1_17OperandDescriptorELm18446744073709551615EES2_"
        "IKNS1_9FieldViewELm18446744073709551615EES2_IKNS1_"
        "11OperandViewELm18446744073709551615EES2_IKNS1_"
        "34OperandTypeCompatibilityDescriptorELm18446744073709551615EERKNS1_"
        "7ContextEPKNS0_27MatrixInstructionDescriptorE");
/** Inspect actual generated OperandViews, then forward without changing checker behavior. */
extern "C" ptx_frontend::resolved_ir::checker::CheckResult
    packed_setp_wrap_operands(PACKED_SETP_PARAMS) asm(
        "__wrap__ZN12ptx_frontend11resolved_ir7checker14check_"
        "operandsESt4spanIKNS1_17OperandDescriptorELm18446744073709551615EES2_"
        "IKNS1_9FieldViewELm18446744073709551615EES2_IKNS1_"
        "11OperandViewELm18446744073709551615EES2_IKNS1_"
        "34OperandTypeCompatibilityDescriptorELm18446744073709551615EERKNS1_"
        "7ContextEPKNS0_27MatrixInstructionDescriptorE");
ptx_frontend::resolved_ir::checker::CheckResult packed_setp_wrap_operands(
    PACKED_SETP_PARAMS) {
  using namespace ptx_frontend::resolved_ir;
  if (auto* capture = packed_setp_test_wrap::active) {
    for (const auto& view : views)
      if (view.field_id == capture->field) {
        ++capture->calls;
        const auto descriptor = std::find_if(
            descriptors.begin(), descriptors.end(), [&](const auto& item) {
              return item.target_field_id == capture->field;
            });
        bool valid = capture->member && capture->member->locs.size() == 1 &&
                     view.locations.size() == 1;
        valid =
            valid && view.locations.data() == capture->member->locs.data() &&
            view.locations.front() == capture->range &&
            descriptor != descriptors.end() &&
            descriptor->integer_immediate_type == ScalarType::B32 &&
            descriptor->type_expression.kind ==
                checker::OperandTypeExpressionKind::FixedScalar &&
            descriptor->type_expression.fixed_scalar_type == capture->logical &&
            descriptor->register_width_policy ==
                ptx_frontend::base::ScalarTypeSizePolicy::SameWidth &&
            descriptor->role == checker::OperandRole::Source &&
            descriptor->access == checker::OperandAccess::Read &&
            descriptor->allowed_shapes == (checker::OperandShape::Register |
                                           checker::OperandShape::Immediate);
        if (valid && capture->immediate) {
          const auto* actual =
              std::get_if<ResolvedImmediate>(&capture->member->value);
          valid = actual &&
                  view.actual_shape == checker::OperandShape::Immediate &&
                  !view.register_ref && actual->type == capture->type &&
                  actual->bits == capture->bits &&
                  actual->integer_source_bits == capture->source_bits &&
                  actual->is_negative == capture->negative &&
                  view.immediate_type == actual->type &&
                  view.immediate_bits == actual->bits &&
                  view.integer_source_bits == actual->integer_source_bits &&
                  view.immediate_is_negative == actual->is_negative;
        } else if (valid) {
          const auto* actual =
              std::get_if<ResolvedRegisterRef>(&capture->member->value);
          valid = actual && actual->spelling == capture->spelling &&
                  actual->declared_type == capture->type && actual->symbol_id &&
                  actual->register_class == ResolvedRegisterClass::General &&
                  !actual->vector_width &&
                  view.actual_shape == checker::OperandShape::Register &&
                  view.register_ref == actual &&
                  view.register_type == actual->declared_type &&
                  view.register_symbol_id == actual->symbol_id &&
                  view.register_class == actual->register_class &&
                  view.register_vector_width == actual->vector_width &&
                  !view.immediate_type;
        }
        capture->valid = capture->valid && valid;
      }
  }
  return packed_setp_real_operands(descriptors, fields, views, compatibility,
                                   context, matrix);
}
#undef PACKED_SETP_PARAMS
#endif

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

/** Verify installed catalog metadata and owned header gates without source storage. */
bool checkInstalledHeaderCompatibility() {
  namespace ir = ptx_frontend::resolved_ir;
  namespace base = ptx_frontend::base;
  const auto sm = base::find_target_profile("sm_100f");
  const auto compute = base::find_target_profile("compute_100f");
  const auto legacy = base::find_target_profile("compute_12");
  if (!sm || !compute || !legacy ||
      !base::same_target_identity(sm->identity, compute->identity) ||
      sm->minimum_ptx_version.major != 8 ||
      sm->minimum_ptx_version.minor != 8 ||
      compute->minimum_ptx_version.major != sm->minimum_ptx_version.major ||
      compute->minimum_ptx_version.minor != sm->minimum_ptx_version.minor ||
      sm->supports_map_f64_to_f32 || !legacy->supports_map_f64_to_f32)
    return false;
  for (const auto source :
       {".version 1.0\n.target sm_80\n",
        ".version 2.2\n.target sm_20\n.address_size 32\n",
        ".version 2.3\n.target sm_20, debug\n",
        ".version 9.3\n.target compute_80, map_f64_to_f32\n"}) {
    std::optional<ir::ResolvedModule> owned;
    {
      ptx_frontend::PtxSyntaxParser parser{source};
      const auto ast = parser.parseModule();
      if (!ast || !ast.diagnostics.empty() ||
          ir::resolveAndValidateModule(*ast))
        return false;
      auto resolved = ir::resolveModuleOnly(*ast);
      if (!resolved)
        return false;
      owned.emplace(std::move(*resolved));
    }
    if (ir::validateModule(*owned) ||
        ir::validateModule(*owned,
                           ir::ModuleValidationPolicy::AvailableContext))
      return false;
  }
  return true;
}

/** Verify installed legacy forms have no synthesized synchronization controls. */
template <typename T>
concept InstalledMaskFreeVote = !requires(T instruction) {
  instruction.sync;
} && !requires(T instruction) { instruction.membermask; };
static_assert(InstalledMaskFreeVote<ptx_frontend::resolved_ir::VoteAllPred> &&
              InstalledMaskFreeVote<ptx_frontend::resolved_ir::VoteAnyPred> &&
              InstalledMaskFreeVote<ptx_frontend::resolved_ir::VoteUniPred> &&
              InstalledMaskFreeVote<ptx_frontend::resolved_ir::VoteBallotB32>);

/** Exercise all installed legacy vote modes and complements across their catalog. */
bool checkInstalledLegacyVote() {
  namespace ir = ptx_frontend::resolved_ir;
  for (const auto sm : {12u, 13u, 20u, 30u, 50u, 52u}) {
    std::optional<ir::ResolvedModule> owned;
    {
      const auto profile =
          ptx_frontend::base::find_target_profile("sm_" + std::to_string(sm));
      if (!profile)
        return false;
      const auto minimum = profile->minimum_ptx_version;
      std::string source = ".version " + std::to_string(minimum.major) + "." +
                           std::to_string(minimum.minor) + "\n.target sm_" +
                           std::to_string(sm) + R"ptx(
.func f() { .reg .pred %p<2>; .reg .b32 %b;
 vote.all.pred %p1, %p0; vote.all.pred %p1, !%p0;
 vote.any.pred %p1, %p0; vote.any.pred %p1, !%p0;
 vote.uni.pred %p1, %p0; vote.uni.pred %p1, !%p0;
)ptx";
      if (sm >= 20)
        source += "vote.ballot.b32 %b, %p0; vote.ballot.b32 %b, !%p0;\n";
      source += "}\n";
      ptx_frontend::PtxSyntaxParser parser{source};
      const auto ast = parser.parseModule();
      if (!ast || !ast.diagnostics.empty())
        return false;
      auto resolved = ir::resolveAndValidateModule(*ast);
      if (!resolved || !ir::validateModule(*ast, *resolved))
        return false;
      owned.emplace(std::move(*resolved));
    }
    if (!ir::validateModule(*owned))
      return false;
    const auto& body = owned->functions.front().body;
    if (body.size() != (sm >= 20 ? 8u : 6u))
      return false;
    for (size_t index = 0; index < body.size(); ++index) {
      const ir::ResolvedPredicate* predicate = nullptr;
      if (index < 2) {
        const auto* vote =
            dynamic_cast<const ir::VoteAllPred*>(body[index].get());
        if (!vote || !ir::VoteAllPred::all ||
            ir::VoteAllPred::type != ir::ScalarType::Pred)
          return false;
        predicate = &vote->predicate.value;
      } else if (index < 4) {
        const auto* vote =
            dynamic_cast<const ir::VoteAnyPred*>(body[index].get());
        if (!vote || !ir::VoteAnyPred::any ||
            ir::VoteAnyPred::type != ir::ScalarType::Pred)
          return false;
        predicate = &vote->predicate.value;
      } else if (index < 6) {
        const auto* vote =
            dynamic_cast<const ir::VoteUniPred*>(body[index].get());
        if (!vote || !ir::VoteUniPred::uni ||
            ir::VoteUniPred::type != ir::ScalarType::Pred)
          return false;
        predicate = &vote->predicate.value;
      } else {
        const auto* vote =
            dynamic_cast<const ir::VoteBallotB32*>(body[index].get());
        if (!vote || !ir::VoteBallotB32::ballot ||
            ir::VoteBallotB32::type != ir::ScalarType::B32 ||
            vote->dst.value.declared_type != ir::ScalarType::B32)
          return false;
        predicate = &vote->predicate.value;
      }
      if (predicate->negated != (index % 2 == 1) ||
          predicate->register_ref.declared_type != ir::ScalarType::Pred)
        return false;
      ir::checker::Context context{
          .target = {.ptx_version = {1, 2},
                     .sm_version = sm,
                     .identity = ptx_frontend::base::parse_target_identity(
                         "sm_" + std::to_string(sm))}};
      if (!body[index]->check(context))
        return false;
      context.target.sm_version = 70;
      context.target.identity =
          ptx_frontend::base::parse_target_identity("sm_70");
      if (body[index]->check(context))
        return false;
    }
  }
  return true;
}

static_assert(InstalledMaskFreeVote<ptx_frontend::resolved_ir::ShflUpB32> &&
              InstalledMaskFreeVote<ptx_frontend::resolved_ir::ShflDownB32> &&
              InstalledMaskFreeVote<ptx_frontend::resolved_ir::ShflBflyB32> &&
              InstalledMaskFreeVote<ptx_frontend::resolved_ir::ShflIdxB32>);

/** Exercise both installed shuffle layouts and b/c controls after AST destruction. */
bool checkInstalledLegacyShuffle() {
  namespace ir = ptx_frontend::resolved_ir;
  for (const auto target : {"sm_30", "sm_50", "sm_52"}) {
    std::optional<ir::ResolvedModule> owned;
    {
      const auto profile = ptx_frontend::base::find_target_profile(target);
      if (!profile)
        return false;
      const auto minimum = profile->minimum_ptx_version;
      std::string source =
          ".version " + std::to_string(minimum.major) + "." +
          std::to_string(minimum.minor) + "\n.target " + std::string(target) +
          "\n.func f() { .reg .b32 %b<2>; .reg .pred %p; .reg .u32 %u<2>;\n";
      for (const auto mode : {"up", "down", "bfly", "idx"}) {
        source += "shfl." + std::string(mode) + ".b32 %b0, %b1, 1, 31;\n";
        source += "shfl." + std::string(mode) + ".b32 %b0|%p, %b1, %u0, %u1;\n";
      }
      source += "}\n";
      ptx_frontend::PtxSyntaxParser parser{source};
      const auto ast = parser.parseModule();
      if (!ast || !ast.diagnostics.empty())
        return false;
      auto resolved = ir::resolveAndValidateModule(*ast);
      if (!resolved || !ir::validateModule(*ast, *resolved))
        return false;
      owned.emplace(std::move(*resolved));
    }
    if (!ir::validateModule(*owned))
      return false;
    const auto& body = owned->functions.front().body;
    if (body.size() != 8)
      return false;
    for (size_t index = 0; index < body.size(); ++index) {
      const bool paired = index % 2 == 1;
      /** Inspect only one of the four installed legacy concrete forms. */
      const auto inspect = [&]<typename T>(const T* form)
        requires(std::same_as<T, ir::ShflUpB32> ||
                 std::same_as<T, ir::ShflDownB32> ||
                 std::same_as<T, ir::ShflBflyB32> ||
                 std::same_as<T, ir::ShflIdxB32>)
      {
        if (!form || T::type != ir::ScalarType::B32 ||
            form->dst_register.has_value() == paired ||
            form->dst_shfl_destination.has_value() != paired ||
            form->src.value.spelling != "%b1" || !form->src.value.symbol_id ||
            form->src.locs.empty())
          return false;
        if (paired) {
          const auto& destination = *form->dst_shfl_destination;
          if (!destination.value.data || !destination.value.predicate ||
              destination.value.data->value.spelling != "%b0" ||
              destination.value.predicate->value.register_ref.spelling !=
                  "%p" ||
              destination.value.predicate->value.register_ref.declared_type !=
                  ir::ScalarType::Pred ||
              destination.locs.empty())
            return false;
        } else if (form->dst_register->value.spelling != "%b0" ||
                   form->dst_register->value.declared_type !=
                       ir::ScalarType::B32 ||
                   form->dst_register->locs.empty()) {
          return false;
        }
        return std::holds_alternative<ir::ResolvedRegisterRef>(
                   form->lane.value) == paired &&
               std::holds_alternative<ir::ResolvedRegisterRef>(
                   form->clamp.value) == paired &&
               !form->lane.locs.empty() && !form->clamp.locs.empty();
      };
      bool valid = false;
      switch (index / 2) {
        case 0:
          valid =
              inspect(dynamic_cast<const ir::ShflUpB32*>(body[index].get()));
          break;
        case 1:
          valid =
              inspect(dynamic_cast<const ir::ShflDownB32*>(body[index].get()));
          break;
        case 2:
          valid =
              inspect(dynamic_cast<const ir::ShflBflyB32*>(body[index].get()));
          break;
        case 3:
          valid =
              inspect(dynamic_cast<const ir::ShflIdxB32*>(body[index].get()));
          break;
      }
      if (!valid)
        return false;
      const ir::checker::Context modern{
          .target = {
              .ptx_version = {9, 3},
              .sm_version = 70,
              .identity = ptx_frontend::base::parse_target_identity("sm_70")}};
      if (body[index]->check(modern))
        return false;
    }
  }
  return true;
}

/** Detect whether an installed form exposes a programmer rounding member. */
template <typename T>
concept HasInstalledRounding =
    requires(T instruction) { instruction.rounding; };
static_assert(!HasInstalledRounding<ptx_frontend::resolved_ir::MadImplicitF32>);

/** Exercise every implicit MAD target/flag combination using the installed form. */
bool checkInstalledImplicitMad() {
  namespace ir = ptx_frontend::resolved_ir;
  for (const auto target : {"sm_10", "sm_11", "sm_12", "sm_13"}) {
    for (const auto flags : {"", ".ftz", ".sat", ".ftz.sat"}) {
      std::optional<ir::ResolvedModule> owned;
      {
        const auto profile = ptx_frontend::base::find_target_profile(target);
        if (!profile)
          return false;
        const auto minimum = profile->minimum_ptx_version;
        const auto source = ".version " + std::to_string(minimum.major) + "." +
                            std::to_string(minimum.minor) + "\n.target " +
                            std::string(target) +
                            "\n.func f() { .reg .f32 %f<4>; .reg .b32 %r; mad" +
                            std::string(flags) + ".f32 %r, %f1, 1.0, %f3; }\n";
        ptx_frontend::PtxSyntaxParser parser{source};
        const auto ast = parser.parseModule();
        if (!ast || !ast.diagnostics.empty())
          return false;
        auto resolved = ir::resolveAndValidateModule(*ast);
        if (!resolved || !ir::validateModule(*ast, *resolved))
          return false;
        owned.emplace(std::move(*resolved));
      }
      if (!ir::validateModule(*owned))
        return false;
      const auto& function = owned->functions.front();
      const auto* mad =
          dynamic_cast<const ir::MadImplicitF32*>(function.body[0].get());
      if (!mad || function.instruction_opcodes.front() != "mad" ||
          ir::MadImplicitF32::type != ir::ScalarType::F32 ||
          mad->dst.value.declared_type != ir::ScalarType::B32 ||
          std::get<ir::ResolvedImmediate>(mad->src2.value).type !=
              ir::ScalarType::F32 ||
          mad->ftz.value != (std::string_view(flags).find(".ftz") !=
                             std::string_view::npos) ||
          mad->saturate.value !=
              (std::string_view(flags).find(".sat") != std::string_view::npos))
        return false;
      ir::checker::Context context{
          .target = {
              .ptx_version = {1, 0},
              .sm_version = static_cast<unsigned>(std::stoi(target + 3)),
              .identity = ptx_frontend::base::parse_target_identity(target)}};
      if (!mad->check(context))
        return false;
      context.target.sm_version = 20;
      context.target.identity =
          ptx_frontend::base::parse_target_identity("sm_20");
      if (mad->check(context))
        return false;
    }
  }
  return true;
}

/** Check actual mapped forms and strict owned revalidation through installed APIs. */
bool checkInstalledMappedF64() {
  namespace ir = ptx_frontend::resolved_ir;
  for (const auto target : {"sm_10", "sm_11", "sm_12"}) {
    std::optional<ir::ResolvedModule> owned;
    {
      const std::string source = ".version 1.4\n.target " +
                                 std::string(target) + R"ptx(, map_f64_to_f32
.func f() { .reg .f64 %d<3>; .reg .b64 %rd;
  div.rn.f64 %rd, %d1, 1.0;
  rcp.rn.f64 %d0, %rd;
  sqrt.rn.f64 %d0, %d1;
}
)ptx";
      ptx_frontend::PtxSyntaxParser parser{source};
      auto ast = parser.parseModule();
      if (!ast || !ast.diagnostics.empty())
        return false;
      auto resolved = ir::resolveAndValidateModule(*ast);
      if (!resolved || !ir::validateModule(*ast, *resolved))
        return false;
      owned.emplace(std::move(*resolved));
    }
    if (!ir::validateModule(*owned))
      return false;
    const auto& body = owned->functions.front().body;
    const auto* div = dynamic_cast<const ir::DivRnF64*>(body[0].get());
    const auto* rcp = dynamic_cast<const ir::RcpRnF64*>(body[1].get());
    const auto* sqrt = dynamic_cast<const ir::SqrtRnF64*>(body[2].get());
    if (!div || !rcp || !sqrt || ir::DivRnF64::type != ir::ScalarType::F64 ||
        ir::RcpRnF64::type != ir::ScalarType::F64 ||
        ir::SqrtRnF64::type != ir::ScalarType::F64 ||
        div->dst.value.declared_type != ir::ScalarType::B64 ||
        std::get<ir::ResolvedImmediate>(div->src2.value).type !=
            ir::ScalarType::F64)
      return false;
    ir::checker::Context context{
        .target = {
            .ptx_version = {1, 4},
            .sm_version = static_cast<unsigned>(std::stoi(target + 3)),
            .identity = ptx_frontend::base::parse_target_identity(target),
            .map_f64_to_f32 = true}};
    for (const auto& instruction : body) {
      if (!instruction->check(context))
        return false;
      context.target.map_f64_to_f32 = false;
      if (instruction->check(context))
        return false;
      context.target.map_f64_to_f32 = true;
    }
    auto& region = owned->header.regions[1];
    region.map_f64_to_f32 = false;
    if (ir::validateModule(*owned))
      return false;
    region.map_f64_to_f32 = true;
    region.target_options.back() = "map_f64_to_f32x";
    if (ir::validateModule(*owned))
      return false;
    region.target_options.back() = "map_f64_to_f32";
    if (!ir::validateModule(*owned))
      return false;
  }
  return true;
}

/** Validate independent source-option and entry-parameter metadata together. */
bool checkInstalledCombinedSourceStates() {
  namespace ir = ptx_frontend::resolved_ir;
  for (const bool debug : {false, true}) {
    std::optional<ir::ResolvedModule> owned;
    {
      const std::string source = std::string{".version 9.3\n.target sm_90"} +
                                 (debug ? ", debug" : "") + R"ptx(
.address_size 64
.section .debug_info {.b8 0}
.global .u32 memory;
.entry kernel(.param .u32 input_map) {
  .reg .u32 %r;
  ld.global.L2::cache_hint.u32 %r, [memory];
  multimem.ld_reduce.gpu.add.u32 %r, [memory];
  prefetch.tensormap [input_map];
}
)ptx";
      ptx_frontend::PtxSyntaxParser parser{source};
      const auto ast = parser.parseModule();
      if (!ast || !ast.diagnostics.empty())
        return false;
      auto resolved = ir::resolveAndValidateModule(*ast);
      if (!resolved || !ir::validateModule(*ast, *resolved))
        return false;
      owned.emplace(std::move(*resolved));
    }
    const auto strict = [&] {
      return ir::validateModule(
          *owned, ir::ModuleValidationPolicy::RequireCompleteContext);
    };
    auto& region = owned->header.regions.back();
    const auto& body = owned->functions.front().body;
    const auto& load =
        dynamic_cast<const ir::LdGlobalU32L2CacheHint&>(*body[0]);
    const auto& reduction =
        dynamic_cast<const ir::MultimemLdReduceIntegerAddScalarDefaultWeak&>(
            *body[1]);
    if (!strict() || region.map_f64_to_f32 ||
        region.target_options.size() != (debug ? 2u : 1u) ||
        load.cache_policy ||
        reduction.semantics.value != ir::MemoryConsistency::Omitted ||
        !reduction.semantics.locs.empty() ||
        reduction.scope.value != ir::MemoryScope::Gpu ||
        reduction.scope.locs.empty())
      return false;
    region.map_f64_to_f32 = true;
    if (strict())
      return false;
    region.map_f64_to_f32 = false;
    auto& hint = dynamic_cast<ir::PrefetchGenericTensormap&>(*body[2]);
    auto& parameter = std::get<ir::ResolvedSymbolRef>(hint.address.value.base);
    parameter.enclosing_function_kind = ir::EnclosingFunctionKind::Device;
    hint.address.value.enclosing_function_kind =
        ir::EnclosingFunctionKind::Device;
    if (strict())
      return false;
    parameter.enclosing_function_kind = ir::EnclosingFunctionKind::Entry;
    hint.address.value.enclosing_function_kind =
        ir::EnclosingFunctionKind::Entry;
    if (!strict())
      return false;
  }
  return true;
}

/** Exercise exact packed words through installed helper and owned native call interfaces. */
bool checkInstalledPackedWords() {
  namespace ir = ptx_frontend::resolved_ir;
  namespace base = ptx_frontend::base;
  for (const auto type : {base::ScalarType::F16x2, base::ScalarType::BF16x2,
                          base::ScalarType::E4m3x4, base::ScalarType::E5m2x4}) {
    const ptx_frontend::syntax_ast::AstImmediate leaf{
        .syntax = {.text = "0f7f800001", .range = {{1, 1}, {1, 11}}},
        .kind = base::AstImmediateKind::F32Hex};
    const auto value = ir::resolve_immediate_literal(leaf, type);
    if (!value || value->type != type || value->bits != 0x7f800001 ||
        value->integer_source_bits || value->is_negative)
      return false;
  }
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser(R"ptx(
.version 9.3
.target sm_100a
.address_size 64
.func take(.param .f16x2 input) { ret; }
.entry kernel() { call take,(0f7f800001); }
)ptx");
    const auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return false;
    auto module = ir::resolveAndValidateModule(*ast);
    if (!module || !ir::validateModule(*ast, *module))
      return false;
    owned = std::move(*module);
  }
  if (!ir::validateModule(*owned))
    return false;
  auto copy = *owned;
  auto& call =
      dynamic_cast<ir::CallDirect&>(*copy.functions.back().body.back());
  auto& literal = std::get<ir::ResolvedCallLiteral>(
      call.arguments->value.values.front().value);
  if (!literal.value || literal.value->bits != 0x7f800001)
    return false;
  literal.value->bits ^= 1;
  if (ir::validateModule(copy))
    return false;
  literal.value->bits ^= 1;
  return ir::validateModule(copy).has_value();
}

/** Inspect exported packed element and vector parameter contracts after source release. */
bool checkInstalledParameterDomain() {
  namespace ir = ptx_frontend::resolved_ir;
  namespace base = ptx_frontend::base;
  if (ptx_frontend::declaration_semantics::parameterScalarType(".f16x2") !=
      base::ScalarType::F16x2)
    return false;
  const auto owned = []() -> std::optional<ir::ResolvedModule> {
    ptx_frontend::PtxSyntaxParser parser(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.extern .func (.param .v2 .f16x2 result) exchange(.param .v2 .f16x2 input);
.entry kernel() { .param .v2 .f16x2 a, result; call (result), exchange, (a); }
)ptx");
    const auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return std::nullopt;
    auto module = ir::resolveAndValidateModule(*ast);
    if (!module)
      return std::nullopt;
    return std::move(*module);
  }();
  if (!owned)
    return false;
  auto copy = *owned;
  const auto& formal =
      copy.functions.front().contract.signature.parameters.front();
  const auto& record = copy.functions.back().parameter_declarations.front();
  return formal.scalar_type == base::ScalarType::F16x2 &&
         formal.vector_shape == ptx_frontend::call_argument_compatibility::
                                    CallArgumentVectorShape::V2 &&
         record.scalar_type == base::ScalarType::F16x2 &&
         record.vector_width == 2 && record.byte_extent == 8 &&
         record.alignment == 8 && ir::validateModule(copy).has_value();
}

/** Exercise migrated public dimensions and derived ABI totals after syntax death. */
bool checkInstalledHeaderArrayRank() {
  namespace ir = ptx_frontend::resolved_ir;
  namespace ast = ptx_frontend::syntax_ast;
  namespace cst = ptx_frontend::syntax_cst;
  namespace semantics = ptx_frontend::declaration_semantics;
  const auto owned = []() -> std::optional<ir::ResolvedModule> {
    const std::string source = R"ptx(
.version 9.3
.target sm_80, texmode_independent
.address_size 64
.extern .func take(.param .v2 .u32 input[2][3]);
.entry kernel(.param .texref texture[2][3], .param .u32 number[3][2]) {
  .param .v2 .u32 a[6];
  .reg .b64 target;
  proto: .callprototype _ (.param .v2 .u32 input[3][2]);
  call take, (a);
  call target, (a), proto;
}
)ptx";
    ptx_frontend::PtxCstParser cst_parser(source);
    const auto concrete = cst_parser.parseModule();
    if (!concrete || !concrete.diagnostics.empty() ||
        concrete->sourceText() != source)
      return std::nullopt;
    const auto& concrete_function =
        std::get<cst::CstFunction>(concrete->module()->items.back());
    if (!concrete_function.parameters ||
        concrete_function.parameters->parameters[0].array_dimensions.size() !=
            2)
      return std::nullopt;
    ptx_frontend::PtxSyntaxParser parser(source);
    const auto parsed = parser.parseModule();
    if (!parsed || !parsed.diagnostics.empty())
      return std::nullopt;
    const auto& function = std::get<ast::AstFunction>(parsed->items.back());
    if (function.parameters[0].array_dimensions.size() != 2)
      return std::nullopt;
    auto module = ir::resolveAndValidateModule(*parsed);
    if (!module)
      return std::nullopt;
    return std::move(*module);
  }();
  if (!owned)
    return false;
  auto copy = *owned;
  const auto& function = copy.functions.back();
  const auto& prototype =
      function.call_prototypes.front().signature.parameters.front();
  return function.opaque_entry_parameters.front().array_extents ==
             std::vector<std::optional<uint64_t>>{2, 3} &&
         function.parameter_declarations.front().array_extents ==
             std::vector<std::optional<uint64_t>>{3, 2} &&
         function.parameter_declarations.front().byte_extent == 24 &&
         prototype.array_extents.size() == 2 &&
         semantics::call_argument_properties(prototype).array_size == 6 &&
         ir::validateModule(copy).has_value();
}

/** Exercise installed value-vector provenance after source destruction and copies. */
bool checkInstalledValueVectorSources() {
  namespace ir = ptx_frontend::resolved_ir;
  auto owned = [] {
    const std::string source = R"ptx(
.version 9.3
.target sm_100
.address_size 64
.global .surfref surf;
.entry kernel() {
.reg .v2 .b32 V<2>;
.reg .s32 x;
sust.b.1d.v2.b32.trap [surf,x], V1;
sust.b.1d.v2.b32.trap [surf,x], {V1.r,1};
}
)ptx";
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return std::expected<ir::ResolvedModule, ir::ModuleResolveDiagnostics>{
          std::unexpected(ir::ModuleResolveDiagnostics{})};
    return ir::resolveAndValidateModule(*ast);
  }();
  if (!owned)
    return false;
  auto copied = *owned;
  ir::ResolvedModule assigned;
  assigned = copied;
  assigned.functions.front().body.front() =
      copied.functions.front().body.front()->clone();
  /** Inspect actual payload pointers only during each synchronous callback. */
  struct ValueObserver final : ir::detail::IReferenceObserver {
    /** Count complete named and brace sources separately. */
    size_t named{}, brace{};
    /** Accumulate validity without retaining borrowed payload storage. */
    bool valid = true;
    /** Check the installed source helper, copy equality, and borrowed view field. */
    void value_vector(const ir::ResolvedValueVector& value,
                      std::span<const ptx_frontend::SourceRange> ranges,
                      ir::checker::AddressSymbolResolutionPolicy) override {
      const auto copy = value;
      ir::checker::OperandView view{
          .field_id = "data",
          .actual_shape = ir::checker::OperandShape::Vector,
          .value_vector = &value,
          .vector_arity = value.elements.size(),
          .locations = ranges};
      valid = valid && copy == value && view.value_vector == &value &&
              ir::valid_value_vector_source(value, ranges, true);
      if (value.source.kind == ir::ResolvedVectorSourceKind::NamedVector) {
        ++named;
        valid = valid && value.source.whole_base &&
                std::get<ir::ResolvedRegisterRef>(value.elements.front())
                        .component->origin ==
                    ir::RegisterComponentOrigin::NamedProjection;
      } else {
        ++brace;
        valid = valid && !value.source.whole_base &&
                std::holds_alternative<ir::ResolvedImmediate>(
                    value.elements.back());
      }
    }
  } observer;
  for (const auto& instruction : assigned.functions.front().body)
    instruction->visit_references(observer);
  return observer.valid && observer.named == 1 && observer.brace == 1 &&
         ir::validateModule(copied) && ir::validateModule(assigned);
}

/** Verify the installed optional token policy and owned form parity without AST storage. */
bool checkInstalledMbarrierTokenForms() {
  namespace ir = ptx_frontend::resolved_ir;
  std::array<size_t, 3> forms{};
  size_t reports = 0;
  for (const auto& variant : ir::mbarrier_resolved_descriptor().variants)
    for (const auto& layout : variant.operand_layouts)
      for (const auto& binding : layout.bindings) {
        if (binding.mbarrier_state_token_form)
          ++forms[static_cast<size_t>(*binding.mbarrier_state_token_form)];
        else if (binding.target_field_id == "report_value")
          ++reports;
      }
  if (forms != std::array<size_t, 3>{49, 32, 12} || reports != 24)
    return false;
  ir::checker::OperandDescriptor generic{
      .target_field_id = "data",
      .type_expression =
          {.kind = ir::checker::OperandTypeExpressionKind::FixedScalar,
           .fixed_scalar_type = ir::ScalarType::B64},
      .role = ir::checker::OperandRole::Destination,
      .access = ir::checker::OperandAccess::Write,
      .allowed_shapes = ir::checker::OperandShape::Register,
      .allow_destination_sink = true};
  if (generic.mbarrier_state_token_form.has_value())
    return false;
  static constexpr std::array<std::string_view, 1> capabilities{"cluster"};
  const ir::checker::Context context{.target = {.ptx_version = {9, 3},
                                                .sm_version = 90,
                                                .capabilities = capabilities}};
  const ir::checker::OperandView sink{
      .field_id = "data",
      .actual_shape = ir::checker::OperandShape::Register,
      .is_sink = true};
  if (!ir::checker::check_operands(std::span{&generic, 1}, {},
                                   std::span{&sink, 1}, {}, context))
    return false;
  auto owned = [] {
    const std::string source = R"ptx(
.version 9.3
.target sm_90
.address_size 64
.shared .align 8 .b64 barrier;
.entry kernel() {
.reg .b64 q;
.reg .pred p;
mbarrier.arrive.shared::cta.b64 q,[barrier];
mbarrier.arrive.shared::cluster.b64 _,[barrier];
mbarrier.test_wait.b64 p,[barrier],q;
}
)ptx";
    ptx_frontend::PtxSyntaxParser parser{source};
    const auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return std::expected<ir::ResolvedModule, ir::ModuleResolveDiagnostics>{
          std::unexpected(ir::ModuleResolveDiagnostics{})};
    return ir::resolveAndValidateModule(*ast);
  }();
  if (!owned)
    return false;
  auto copied = *owned;
  ir::ResolvedModule assigned;
  assigned = copied;
  assigned.functions.front().body.back() =
      copied.functions.front().body.back()->clone();
  if (!ir::validateModule(copied) || !ir::validateModule(assigned))
    return false;
  /** Borrow one actual token while its owned instruction remains alive. */
  struct TokenObserver final : ir::detail::IReferenceObserver {
    /** Borrowed only until that instruction is changed or destroyed. */
    const ir::ResolvedMbarrierStateToken* token = nullptr;
    /** Inspect the installed optional-register payload synchronously. */
    void mbarrier_state_token(
        const ir::ResolvedMbarrierStateToken& value,
        std::span<const ptx_frontend::SourceRange>,
        ir::checker::AddressSymbolResolutionPolicy) override {
      token = &value;
    }
  } observer;
  assigned.functions.front().body.back()->visit_references(observer);
  if (!observer.token || !observer.token->register_ref)
    return false;
  const_cast<ir::ResolvedMbarrierStateToken&>(*observer.token)
      .register_ref.reset();
  const auto direct = assigned.functions.front().body.back()->check(context);
  const auto invalid = ir::validateModule(assigned);
  return !direct && !invalid && direct.error().size() == 1 &&
         direct.error().front().kind ==
             ir::checker::CheckDiagnosticKind::UnsupportedOperandShape;
}

/** Exercise migrated half RegOrImm fields and formal-driven values after AST death. */
bool checkInstalledScalarHalfImmediates() {
  namespace ir = ptx_frontend::resolved_ir;
  auto owned = [] {
    const std::string source = R"ptx(
.version 9.3
.target sm_100a
.extern .func take(.param .f16 input);
.entry kernel() {
.reg .f16 h;
.reg .f32 f;
.reg .u32 u;
.reg .pred p;
.reg .b64 target;
proto: .callprototype _ (.param .f16 input);
cvt.f32.f16 f,1.5;
fma.rn.f32.f16 f,1.5,h,0.0;
set.eq.u32.f16 u,1.5,h;
setp.eq.f16 p,1.5,h;
call take,(1.5);
call target,(1.0+0.5),proto;
}
)ptx";
    ptx_frontend::PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return std::expected<ir::ResolvedModule, ir::ModuleResolveDiagnostics>{
          std::unexpected(ir::ModuleResolveDiagnostics{})};
    return ir::resolveAndValidateModule(*ast);
  }();
  if (!owned || owned->functions.back().body.size() != 6)
    return false;
  const auto& body = owned->functions.back().body;
  const auto& cvt = dynamic_cast<const ir::CvtUnroundedBase&>(*body[0]);
  const auto& fma = dynamic_cast<const ir::FmaMixedF32F16&>(*body[1]);
  const auto& set = dynamic_cast<const ir::SetHalfIntegerF16&>(*body[2]);
  const auto& setp = dynamic_cast<const ir::SetpF16&>(*body[3]);
  for (const auto* value :
       {std::get_if<ir::ResolvedImmediate>(&cvt.src.value),
        std::get_if<ir::ResolvedImmediate>(&fma.src1.value),
        std::get_if<ir::ResolvedImmediate>(&set.src1.value),
        std::get_if<ir::ResolvedImmediate>(&setp.src1.value)})
    if (!value || value->type != ir::ScalarType::F16 || value->bits != 0x3e00 ||
        value->integer_source_bits || value->is_negative)
      return false;
  if (!std::holds_alternative<ir::ResolvedRegisterRef>(fma.src2.value))
    return false;
  for (size_t index : {4U, 5U}) {
    const auto& call = dynamic_cast<const ir::CallDirect&>(*body[index]);
    const auto& literal = std::get<ir::ResolvedCallLiteral>(
        call.arguments->value.values[0].value);
    if (!literal.value || literal.value->type != ir::ScalarType::F16 ||
        literal.value->bits != 0x3e00 || literal.value->integer_source_bits)
      return false;
    if (index == 5 &&
        (!literal.source_value ||
         !std::holds_alternative<
             ptx_frontend::declaration_semantics::FloatingConstantValue>(
             *literal.source_value)))
      return false;
  }
  auto copied = *owned;
  ir::ResolvedModule assigned;
  assigned = copied;
  assigned.functions.back().body[1] = copied.functions.back().body[1]->clone();
  if (!ir::validateModule(copied) || !ir::validateModule(assigned))
    return false;
  auto& altered =
      dynamic_cast<ir::FmaMixedF32F16&>(*assigned.functions.back().body[1]);
  std::get<ir::ResolvedImmediate>(altered.src1.value).integer_source_bits = 0;
  return !ir::validateModule(assigned);
}

/** Inspect all migrated pack sources and Narrow metadata using only installed types. */
bool checkInstalledPackIntegerSources() {
  namespace ir = ptx_frontend::resolved_ir;
  auto owned = [] {
    const std::string source = R"ptx(
.version 9.3
.target sm_75
.entry kernel() {
.reg .u32 d;
.reg .s32 a;
mov.b32 a,7;
cvt.pack.sat.u8.s32.b32 d,1,-2,0;
cvt.pack.sat.u16.s32 d,0x100000001U,a;
cvt.pack.sat.s4.s32.b32 d,(1+2),0xffffffff,0f3f800000;
}
)ptx";
    ptx_frontend::PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return std::expected<ir::ResolvedModule, ir::ModuleResolveDiagnostics>{
          std::unexpected(ir::ModuleResolveDiagnostics{})};
    return ir::resolveAndValidateModule(*ast);
  }();
  if (!owned || owned->functions.front().body.size() != 4)
    return false;
  auto& body = owned->functions.front().body;
  auto& byte = dynamic_cast<ir::CvtPackSatU8S32B32&>(*body[1]);
  auto& half = dynamic_cast<ir::CvtPackSat16S32&>(*body[2]);
  auto& small = dynamic_cast<ir::CvtPackSatSmallS32B32&>(*body[3]);
  const auto* one = std::get_if<ir::ResolvedImmediate>(&byte.src1.value);
  const auto* negative = std::get_if<ir::ResolvedImmediate>(&byte.src2.value);
  const auto* narrowed = std::get_if<ir::ResolvedImmediate>(&half.src1.value);
  const auto* reg = std::get_if<ir::ResolvedRegisterRef>(&half.src2.value);
  const auto* expression =
      std::get_if<ir::ResolvedImmediate>(&small.src1.value);
  const auto* unsigned_word =
      std::get_if<ir::ResolvedImmediate>(&small.src2.value);
  if (!one || !negative || !narrowed || !reg || !expression || !unsigned_word ||
      one->type != ir::ScalarType::S32 || one->bits != 1 ||
      negative->bits != 0xfffffffeU || !negative->is_negative ||
      negative->integer_source_bits != 0xfffffffffffffffeULL ||
      narrowed->bits != 1 || narrowed->integer_source_bits != 0x100000001ULL ||
      !reg->symbol_id || reg->declared_type != ir::ScalarType::S32 ||
      expression->bits != 3 || unsigned_word->bits != 0xffffffffU ||
      unsigned_word->is_negative)
    return false;
  half.src2.value = ir::ResolvedImmediate{
      .bits = 2, .type = ir::ScalarType::S32, .integer_source_bits = 2};
  if (!ir::validateModule(*owned))
    return false;
  auto copied = *owned;
  ir::ResolvedModule assigned;
  assigned = copied;
  assigned.functions.front().body.back() =
      copied.functions.front().body.back()->clone();
  if (!ir::validateModule(copied) || !ir::validateModule(assigned))
    return false;
  auto& forged = dynamic_cast<ir::CvtPackSatSmallS32B32&>(
      *assigned.functions.front().body.back());
  std::get<ir::ResolvedImmediate>(forged.src1.value).bits = 4;
  return !ir::validateModule(assigned);
}

/** Inspect the three installed quad ValueVector fields after destroying all source state. */
bool checkInstalledStochasticQuadSources() {
  namespace ir = ptx_frontend::resolved_ir;
  auto owned = [] {
    const std::string source = R"ptx(
.version 9.3
.target sm_100a
.entry kernel() {
.reg .b32 d,r; .reg .b16 h; .reg .f32 a;
.reg .v4 .f32 V<2>;
mov.b32 r,0; mov.f32 a,1.0;
cvt.rs.satfinite.e4m3x4.f32 d,{a,1.5,0f3fc00000,0d3ff8000000000000},r;
cvt.rs.satfinite.e2m1x4.f32 h,V1,r;
cvt.rs.satfinite.e2m3x4.f32 d,{1.5,1.0+0.5,0f3fc00000,0d3ff8000000000000},r;
}
)ptx";
    ptx_frontend::PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return std::expected<ir::ResolvedModule, ir::ModuleResolveDiagnostics>{
          std::unexpected(ir::ModuleResolveDiagnostics{})};
    return ir::resolveAndValidateModule(*ast);
  }();
  if (!owned || owned->functions.front().body.size() != 5)
    return false;
  auto& body = owned->functions.front().body;
  auto& mixed = dynamic_cast<ir::CvtRsSatfiniteF8x4F32&>(*body[2]).src.value;
  auto& named = dynamic_cast<ir::CvtRsSatfiniteF4x4F32&>(*body[3]).src.value;
  auto& constants =
      dynamic_cast<ir::CvtRsSatfiniteF6x4F32&>(*body[4]).src.value;
  if (mixed.source.kind != ir::ResolvedVectorSourceKind::BraceList ||
      named.source.kind != ir::ResolvedVectorSourceKind::NamedVector ||
      !named.source.whole_base ||
      named.source.whole_base->value.register_ref.parameterized_index != 1u ||
      named.source.whole_base->value.register_ref.vector_width != 4u ||
      mixed.elements.size() != 4 || named.elements.size() != 4 ||
      constants.elements.size() != 4)
    return false;
  for (size_t lane = 0; lane < 4; ++lane) {
    const auto& immediate =
        std::get<ir::ResolvedImmediate>(constants.elements[lane]);
    const auto& reg = std::get<ir::ResolvedRegisterRef>(named.elements[lane]);
    if (immediate.bits != 0x3fc00000 || immediate.type != ir::ScalarType::F32 ||
        immediate.integer_source_bits || !reg.component ||
        reg.component->origin != ir::RegisterComponentOrigin::NamedProjection ||
        reg.component->lane != lane || reg.vector_width)
      return false;
  }
  ir::ResolvedModule copied(*owned), assigned;
  assigned = copied;
  assigned.functions.front().body.back() =
      copied.functions.front().body.back()->clone();
  if (!ir::validateModule(copied) || !ir::validateModule(assigned))
    return false;
  auto& corrupt = dynamic_cast<ir::CvtRsSatfiniteF4x4F32&>(
      *assigned.functions.front().body[3]);
  corrupt.src.value.source.whole_base.reset();
  return !ir::validateModule(assigned);
}

/** Read all six installed optional FTZ fields and own them beyond source lifetime. */
bool checkInstalledHalfSetFtz() {
  namespace ir = ptx_frontend::resolved_ir;
  auto owned = [] {
    const std::string source = R"ptx(
.version 4.2
.target sm_53
.entry kernel() {
.reg .f16 h; .reg .b16 ba,bb; .reg .s32 ia,ib;
.reg .f64 da,db; .reg .pred p;
mov.b16 ba,1; mov.b16 bb,2; mov.s32 ia,1; mov.s32 ib,2;
mov.f64 da,1.0; mov.f64 db,2.0; mov.pred p,1;
set.eq.ftz.f16.b16 h,ba,bb;
set.eq.and.ftz.f16.b16 h,ba,bb,p;
set.eq.ftz.f16.s32 h,ia,ib;
set.eq.and.ftz.f16.s32 h,ia,ib,p;
set.eq.ftz.f16.f64 h,da,db;
set.eq.and.ftz.f16.f64 h,da,db,p;
set.eq.f16.b16 h,ba,bb;
set.eq.and.f16.b16 h,ba,bb,p;
set.eq.f16.s32 h,ia,ib;
set.eq.and.f16.s32 h,ia,ib,p;
set.eq.f16.f64 h,da,db;
set.eq.and.f16.f64 h,da,db,p;
}
)ptx";
    ptx_frontend::PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return std::expected<ir::ResolvedModule, ir::ModuleResolveDiagnostics>{
          std::unexpected(ir::ModuleResolveDiagnostics{})};
    return ir::resolveAndValidateModule(*ast);
  }();
  if (!owned || owned->functions.front().body.size() != 19)
    return false;
  auto& body = owned->functions.front().body;
  std::array<ptx_frontend::WithLocs<bool>*, 12> flags{
      &dynamic_cast<ir::SetHalfF16Bit&>(*body[7]).ftz,
      &dynamic_cast<ir::SetHalfF16BitBoolean&>(*body[8]).ftz,
      &dynamic_cast<ir::SetHalfF16Integer&>(*body[9]).ftz,
      &dynamic_cast<ir::SetHalfF16IntegerBoolean&>(*body[10]).ftz,
      &dynamic_cast<ir::SetHalfF16F64&>(*body[11]).ftz,
      &dynamic_cast<ir::SetHalfF16F64Boolean&>(*body[12]).ftz,
      &dynamic_cast<ir::SetHalfF16Bit&>(*body[13]).ftz,
      &dynamic_cast<ir::SetHalfF16BitBoolean&>(*body[14]).ftz,
      &dynamic_cast<ir::SetHalfF16Integer&>(*body[15]).ftz,
      &dynamic_cast<ir::SetHalfF16IntegerBoolean&>(*body[16]).ftz,
      &dynamic_cast<ir::SetHalfF16F64&>(*body[17]).ftz,
      &dynamic_cast<ir::SetHalfF16F64Boolean&>(*body[18]).ftz};
  for (size_t index = 0; index < flags.size(); ++index)
    if (flags[index]->value != (index < 6) ||
        flags[index]->locs.empty() == (index < 6))
      return false;
  ir::ResolvedModule copied(*owned), assigned;
  assigned = copied;
  ir::ResolvedFunction function(copied.functions.front());
  assigned.functions.front() = function;
  assigned.functions.front().body.back() = function.body.back()->clone();
  if (!ir::validateModule(copied) || !ir::validateModule(assigned))
    return false;
  auto& forged = dynamic_cast<ir::SetHalfF16F64Boolean&>(
      *assigned.functions.front().body.back());
  forged.ftz.value = true;
  return !ir::validateModule(assigned);
}

/** Own all nine installed optional RN controls after source/AST death and cloning. */
bool checkInstalledHalfFmaModifiers() {
  namespace ir = ptx_frontend::resolved_ir;
  auto owned = [] {
    const std::string source = R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
.reg .b16 h,a,b,c; .reg .b32 d,x,y,z;
mov.b16 a,1; mov.b16 b,2; mov.b16 c,0;
mov.b32 x,1; mov.b32 y,2; mov.b32 z,0;
fma.f16 h,a,b,c;
fma.f16x2 d,x,y,z;
fma.relu.f16 h,a,b,c;
fma.oob.f16 h,a,b,c;
fma.oob.relu.f16 h,a,b,c;
fma.bf16 h,a,b,c;
fma.bf16x2 d,x,y,z;
fma.oob.bf16 h,a,b,c;
fma.oob.bf16x2 d,x,y,z;
fma.rn.f16 h,a,b,c;
fma.rn.f16x2 d,x,y,z;
fma.rn.relu.f16 h,a,b,c;
fma.rn.oob.f16 h,a,b,c;
fma.rn.oob.relu.f16 h,a,b,c;
fma.rn.bf16 h,a,b,c;
fma.rn.bf16x2 d,x,y,z;
fma.rn.oob.bf16 h,a,b,c;
fma.rn.oob.bf16x2 d,x,y,z;
}
)ptx";
    ptx_frontend::PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return std::expected<ir::ResolvedModule, ir::ModuleResolveDiagnostics>{
          std::unexpected(ir::ModuleResolveDiagnostics{})};
    return ir::resolveAndValidateModule(*ast);
  }();
  if (!owned || owned->functions.front().body.size() != 24)
    return false;
  auto& body = owned->functions.front().body;
  std::array<ptx_frontend::WithLocs<ir::RoundingMode>*, 18> controls{
      &dynamic_cast<ir::FmaRnF16&>(*body[6]).rounding,
      &dynamic_cast<ir::FmaRnF16x2&>(*body[7]).rounding,
      &dynamic_cast<ir::FmaHalfRelu&>(*body[8]).rounding,
      &dynamic_cast<ir::FmaHalfOob&>(*body[9]).rounding,
      &dynamic_cast<ir::FmaHalfOobRelu&>(*body[10]).rounding,
      &dynamic_cast<ir::FmaBf16&>(*body[11]).rounding,
      &dynamic_cast<ir::FmaBf16x2&>(*body[12]).rounding,
      &dynamic_cast<ir::FmaBf16Oob&>(*body[13]).rounding,
      &dynamic_cast<ir::FmaBf16x2Oob&>(*body[14]).rounding,
      &dynamic_cast<ir::FmaRnF16&>(*body[15]).rounding,
      &dynamic_cast<ir::FmaRnF16x2&>(*body[16]).rounding,
      &dynamic_cast<ir::FmaHalfRelu&>(*body[17]).rounding,
      &dynamic_cast<ir::FmaHalfOob&>(*body[18]).rounding,
      &dynamic_cast<ir::FmaHalfOobRelu&>(*body[19]).rounding,
      &dynamic_cast<ir::FmaBf16&>(*body[20]).rounding,
      &dynamic_cast<ir::FmaBf16x2&>(*body[21]).rounding,
      &dynamic_cast<ir::FmaBf16Oob&>(*body[22]).rounding,
      &dynamic_cast<ir::FmaBf16x2Oob&>(*body[23]).rounding,
  };
  for (size_t index = 0; index < controls.size(); ++index)
    if (controls[index]->value != ir::RoundingMode::Rn ||
        controls[index]->locs.empty() != (index < 9))
      return false;
  ir::ResolvedModule copied(*owned), assigned;
  assigned = copied;
  ir::ResolvedFunction function(copied.functions.front());
  assigned.functions.front() = function;
  assigned.functions.front().body.back() = function.body.back()->clone();
  if (!ir::validateModule(copied) || !ir::validateModule(assigned))
    return false;
  auto& invalid =
      dynamic_cast<ir::FmaBf16x2Oob&>(*assigned.functions.front().body.back());
  invalid.rounding.value = ir::RoundingMode::Rz;
  return !ir::validateModule(assigned);
}

/** Read all twelve installed default-true completion controls beyond AST lifetime. */
bool checkInstalledRedAsyncDefault() {
  namespace ir = ptx_frontend::resolved_ir;
  auto owned = [] {
    const std::string source = R"ptx(
.version 9.3
.target sm_100
.entry kernel(){
.reg .u64 dst,bar,q; .reg .u32 u; .reg .s32 s; .reg .b32 b;
mov.u64 dst,0;mov.u64 bar,8;mov.u64 q,1;mov.u32 u,1;mov.s32 s,1;mov.b32 b,1;
red.async.relaxed.cluster.inc.u32 [dst],u,[bar];
red.async.relaxed.cluster.dec.u32 [dst],u,[bar];
red.async.relaxed.cluster.min.u32 [dst],u,[bar];
red.async.relaxed.cluster.min.s32 [dst],s,[bar];
red.async.relaxed.cluster.max.u32 [dst],u,[bar];
red.async.relaxed.cluster.max.s32 [dst],s,[bar];
red.async.relaxed.cluster.and.b32 [dst],b,[bar];
red.async.relaxed.cluster.or.b32 [dst],b,[bar];
red.async.relaxed.cluster.xor.b32 [dst],b,[bar];
red.async.relaxed.cluster.add.u32 [dst],u,[bar];
red.async.relaxed.cluster.add.s32 [dst],s,[bar];
red.async.relaxed.cluster.add.u64 [dst],q,[bar];
red.async.relaxed.cluster.mbarrier::complete_tx::bytes.inc.u32 [dst],u,[bar];
red.async.relaxed.cluster.mbarrier::complete_tx::bytes.dec.u32 [dst],u,[bar];
red.async.relaxed.cluster.mbarrier::complete_tx::bytes.min.u32 [dst],u,[bar];
red.async.relaxed.cluster.mbarrier::complete_tx::bytes.min.s32 [dst],s,[bar];
red.async.relaxed.cluster.mbarrier::complete_tx::bytes.max.u32 [dst],u,[bar];
red.async.relaxed.cluster.mbarrier::complete_tx::bytes.max.s32 [dst],s,[bar];
red.async.relaxed.cluster.mbarrier::complete_tx::bytes.and.b32 [dst],b,[bar];
red.async.relaxed.cluster.mbarrier::complete_tx::bytes.or.b32 [dst],b,[bar];
red.async.relaxed.cluster.mbarrier::complete_tx::bytes.xor.b32 [dst],b,[bar];
red.async.relaxed.cluster.mbarrier::complete_tx::bytes.add.u32 [dst],u,[bar];
red.async.relaxed.cluster.mbarrier::complete_tx::bytes.add.s32 [dst],s,[bar];
red.async.relaxed.cluster.mbarrier::complete_tx::bytes.add.u64 [dst],q,[bar];
red.async.release.sys.global.add.u32 [dst],u;
}
)ptx";
    ptx_frontend::PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return std::expected<ir::ResolvedModule, ir::ModuleResolveDiagnostics>{
          std::unexpected(ir::ModuleResolveDiagnostics{})};
    return ir::resolveAndValidateModule(*ast);
  }();
  if (!owned || owned->functions.front().body.size() != 31)
    return false;
  auto& body = owned->functions.front().body;
  std::array<ptx_frontend::WithLocs<bool>*, 24> completion{
      &dynamic_cast<ir::RedAsyncSharedIncU32&>(*body[6]).completion,
      &dynamic_cast<ir::RedAsyncSharedDecU32&>(*body[7]).completion,
      &dynamic_cast<ir::RedAsyncSharedMinU32&>(*body[8]).completion,
      &dynamic_cast<ir::RedAsyncSharedMinS32&>(*body[9]).completion,
      &dynamic_cast<ir::RedAsyncSharedMaxU32&>(*body[10]).completion,
      &dynamic_cast<ir::RedAsyncSharedMaxS32&>(*body[11]).completion,
      &dynamic_cast<ir::RedAsyncSharedAndB32&>(*body[12]).completion,
      &dynamic_cast<ir::RedAsyncSharedOrB32&>(*body[13]).completion,
      &dynamic_cast<ir::RedAsyncSharedXorB32&>(*body[14]).completion,
      &dynamic_cast<ir::RedAsyncSharedAddU32&>(*body[15]).completion,
      &dynamic_cast<ir::RedAsyncSharedAddS32&>(*body[16]).completion,
      &dynamic_cast<ir::RedAsyncSharedAddU64&>(*body[17]).completion,
      &dynamic_cast<ir::RedAsyncSharedIncU32&>(*body[18]).completion,
      &dynamic_cast<ir::RedAsyncSharedDecU32&>(*body[19]).completion,
      &dynamic_cast<ir::RedAsyncSharedMinU32&>(*body[20]).completion,
      &dynamic_cast<ir::RedAsyncSharedMinS32&>(*body[21]).completion,
      &dynamic_cast<ir::RedAsyncSharedMaxU32&>(*body[22]).completion,
      &dynamic_cast<ir::RedAsyncSharedMaxS32&>(*body[23]).completion,
      &dynamic_cast<ir::RedAsyncSharedAndB32&>(*body[24]).completion,
      &dynamic_cast<ir::RedAsyncSharedOrB32&>(*body[25]).completion,
      &dynamic_cast<ir::RedAsyncSharedXorB32&>(*body[26]).completion,
      &dynamic_cast<ir::RedAsyncSharedAddU32&>(*body[27]).completion,
      &dynamic_cast<ir::RedAsyncSharedAddS32&>(*body[28]).completion,
      &dynamic_cast<ir::RedAsyncSharedAddU64&>(*body[29]).completion,
  };
  for (size_t index = 0; index < completion.size(); ++index)
    if (!completion[index]->value ||
        completion[index]->locs.empty() != (index < 12))
      return false;
  if (body.back()->instruction_kind() != ir::RedAsyncReleaseAddU32::kind)
    return false;
  ir::ResolvedModule copied(*owned), assigned;
  assigned = copied;
  ir::ResolvedFunction function(copied.functions.front());
  assigned.functions.front() = function;
  assigned.functions.front().body[29] = function.body[29]->clone();
  if (!ir::validateModule(copied) || !ir::validateModule(assigned))
    return false;
  dynamic_cast<ir::RedAsyncSharedAddU64&>(*assigned.functions.front().body[29])
      .completion.value = false;
  return !ir::validateModule(assigned);
}

/** Verify required mask members, borrowed ordinals and AST-independent ownership. */
bool checkInstalledRequiredTcgenMasks() {
  namespace ir = ptx_frontend::resolved_ir;
  for (const bool sparse : {false, true})
    for (const std::string kind : {"f16", "tf32", "i8", "f8f6f4"})
      for (const unsigned group : {1u, 2u})
        for (const bool shared : {false, true})
          for (const bool scale : {false, true}) {
            if (scale && kind != "f16" && kind != "tf32")
              continue;
            std::string mask = "{";
            for (unsigned lane = 0; lane < (group == 1 ? 4u : 8u); ++lane)
              mask += (lane ? ",m" : "m") + std::to_string(lane);
            mask += "}";
            const std::string instruction =
                "tcgen05.mma" + std::string(sparse ? ".sp" : "") +
                ".cta_group::" + std::to_string(group) + ".kind::" + kind +
                " [d]," + (shared ? std::string("ad") : "[a]") + ",bd," +
                (sparse ? "[sp]," : "") + "i," + mask + ",p" +
                (scale ? ",15" : "") + ";";
            std::optional<ir::ResolvedModule> owned;
            {
              const std::string source =
                  ".version 9.3\n.target sm_100a\n.address_size 64\n.entry "
                  "kernel(){"
                  ".reg .b32 d,a,sp,i,m<8>; .reg .b64 ad,bd; .reg .pred p;" +
                  instruction + "}";
              ptx_frontend::PtxSyntaxParser parser(source);
              const auto ast = parser.parseModule();
              if (!ast || !ast.diagnostics.empty())
                return false;
              auto module = ir::resolveAndValidateModule(*ast);
              if (!module)
                return false;
              owned.emplace(std::move(*module));
            }
            auto& instruction_ref = *owned->functions.front().body.front();
            const ptx_frontend::WithLocs<ir::ResolvedRegisterVector>* member =
                nullptr;
            unsigned ordinal = 99;
            const ir::ResolvedRegisterVector* borrowed = nullptr;
            switch (instruction_ref.instruction_kind()) {
              case ir::Tcgen05MmaF16::kind: {
                const auto& form =
                    dynamic_cast<const ir::Tcgen05MmaF16&>(instruction_ref);
                member = &form.disable_output_lane;
                ordinal = form.operand_layout.value;
                const auto view = ir::tcgen_mma_f16_view(instruction_ref);
                if (!view)
                  return false;
                borrowed = view->disable_output_lane;
                break;
              }
              case ir::Tcgen05MmaTf32::kind: {
                const auto& form =
                    dynamic_cast<const ir::Tcgen05MmaTf32&>(instruction_ref);
                member = &form.disable_output_lane;
                ordinal = form.operand_layout.value;
                const auto view = ir::tcgen_mma_tf32_view(instruction_ref);
                if (!view)
                  return false;
                borrowed = view->disable_output_lane;
                break;
              }
              case ir::Tcgen05MmaI8::kind: {
                const auto& form =
                    dynamic_cast<const ir::Tcgen05MmaI8&>(instruction_ref);
                member = &form.disable_output_lane;
                ordinal = form.operand_layout.value;
                const auto view = ir::tcgen_mma_i8_view(instruction_ref);
                if (!view)
                  return false;
                borrowed = view->disable_output_lane;
                break;
              }
              case ir::Tcgen05MmaF8f6f4::kind: {
                const auto& form =
                    dynamic_cast<const ir::Tcgen05MmaF8f6f4&>(instruction_ref);
                member = &form.disable_output_lane;
                ordinal = form.operand_layout.value;
                const auto view = ir::tcgen_mma_f8f6f4_view(instruction_ref);
                if (!view)
                  return false;
                borrowed = view->disable_output_lane;
                break;
              }
              case ir::Tcgen05MmaSpF16::kind: {
                const auto& form =
                    dynamic_cast<const ir::Tcgen05MmaSpF16&>(instruction_ref);
                member = &form.disable_output_lane;
                ordinal = form.operand_layout.value;
                const auto view = ir::tcgen_mma_sparse_view(instruction_ref);
                if (!view)
                  return false;
                borrowed = view->disable_output_lane;
                break;
              }
              case ir::Tcgen05MmaSpTf32::kind: {
                const auto& form =
                    dynamic_cast<const ir::Tcgen05MmaSpTf32&>(instruction_ref);
                member = &form.disable_output_lane;
                ordinal = form.operand_layout.value;
                const auto view = ir::tcgen_mma_sparse_view(instruction_ref);
                if (!view)
                  return false;
                borrowed = view->disable_output_lane;
                break;
              }
              case ir::Tcgen05MmaSpI8::kind: {
                const auto& form =
                    dynamic_cast<const ir::Tcgen05MmaSpI8&>(instruction_ref);
                member = &form.disable_output_lane;
                ordinal = form.operand_layout.value;
                const auto view = ir::tcgen_mma_sparse_view(instruction_ref);
                if (!view)
                  return false;
                borrowed = view->disable_output_lane;
                break;
              }
              case ir::Tcgen05MmaSpF8f6f4::kind: {
                const auto& form = dynamic_cast<const ir::Tcgen05MmaSpF8f6f4&>(
                    instruction_ref);
                member = &form.disable_output_lane;
                ordinal = form.operand_layout.value;
                const auto view = ir::tcgen_mma_sparse_view(instruction_ref);
                if (!view)
                  return false;
                borrowed = view->disable_output_lane;
                break;
              }
              default:
                return false;
            }
            const unsigned expected =
                (kind == "f16" || kind == "tf32")
                    ? (shared ? 0u : 2u) + (scale ? 1u : 0u)
                    : (shared ? 0u : 1u);
            if (!member || borrowed != &member->value || ordinal != expected ||
                member->value.elements.size() != (group == 1 ? 4u : 8u) ||
                member->locs.empty())
              return false;
            ir::ResolvedModule copied(*owned), assigned;
            assigned = copied;
            ir::ResolvedFunction function(copied.functions.front());
            assigned.functions.front() = function;
            assigned.functions.front().body.front() =
                function.body.front()->clone();
            if (!ir::validateModule(*owned) || !ir::validateModule(copied) ||
                !ir::validateModule(assigned))
              return false;
          }
  for (const std::string instruction :
       {"tcgen05.mma.ws.cta_group::1.kind::f16 [d],ad,bd,i,p;",
        "tcgen05.mma.cta_group::1.kind::mxf8f6f4.block_scale "
        "[d],ad,bd,i,[sa],[sb],p;"}) {
    std::optional<ir::ResolvedModule> owned;
    {
      const std::string source =
          ".version 9.3\n.target sm_100a\n.address_size 64\n.entry kernel(){"
          ".reg .b32 d,i,sa,sb; .reg .b64 ad,bd; .reg .pred p;" +
          instruction + "}";
      ptx_frontend::PtxSyntaxParser parser(source);
      const auto ast = parser.parseModule();
      if (!ast || !ast.diagnostics.empty())
        return false;
      auto module = ir::resolveAndValidateModule(*ast);
      if (!module)
        return false;
      owned.emplace(std::move(*module));
    }
    if (!ir::validateModule(*owned))
      return false;
    if (const auto mx =
            ir::tcgen_mma_mx8_view(*owned->functions.front().body.front())) {
      if (!mx->scale_selector ||
          mx->scale_selector->value != ir::TcgenScaleVectorSize::Absent)
        return false;
    }
    ir::ResolvedModule copied(*owned);
    if (!ir::validateModule(copied))
      return false;
  }
  return true;
}

/** Verify typed section APIs own all numeric and symbolic leaves after source death. */
bool checkInstalledDebugSections() {
  namespace ir = ptx_frontend::resolved_ir;
  namespace ast = ptx_frontend::syntax_ast;
  std::optional<ir::ResolvedModule> owned;
  {
    const std::string source = R"ptx(.version 9.3
.target sm_90
.address_size 64
.file 1 "source.cu"
.global .b8 name;
.section .debug_info { // raw trivia remains in CST only
begin:
 .b64 18446744073709551615U,-9223372036854775808
 .b32 end-begin
 .b64 .debug_info+9223372036854775807
 .b32 .debug_str
}
.section .debug_info {end: .b8 -128,255}
.section .debug_str {name: .b8 110,97,109,101,0}
.entry kernel(){.loc 1 1 0 .loc 1 1 0, function_name name, inlined_at 1 1 0 ret;}
)ptx";
    ptx_frontend::PtxCstParser cst_parser(source);
    const auto cst = cst_parser.parseModule();
    if (!cst || !cst.diagnostics.empty() || cst->sourceText() != source)
      return false;
    const auto& raw = std::get<ptx_frontend::syntax_cst::CstSectionDirective>(
        cst->module()->items[5]);
    if (raw.payload_tokens.empty() || raw.payload.size() != 5)
      return false;
    ptx_frontend::PtxSyntaxParser parser(source);
    const auto syntax = parser.parseModule();
    if (!syntax || !syntax.diagnostics.empty())
      return false;
    const auto& section = std::get<ast::AstSectionDirective>(syntax->items[5]);
    const auto& integers = std::get<std::vector<ast::AstSectionInteger>>(
        std::get<ast::AstSectionData>(section.payload[1]).value);
    if (integers.size() != 2 ||
        integers[0].magnitude.text != "18446744073709551615U" ||
        !integers[1].sign || integers[1].sign->text != "-" ||
        integers[1].magnitude.text != "9223372036854775808")
      return false;
    const auto& difference = std::get<ast::AstSectionDifference>(
        std::get<ast::AstSectionData>(section.payload[2]).value);
    if (difference.left.text != "end" || difference.right.text != "begin")
      return false;
    auto resolved = ir::resolveAndValidateModule(*syntax);
    if (!resolved)
      return false;
    owned.emplace(std::move(*resolved));
  }
  static_assert(
      std::same_as<ir::ResolvedDebugSection, ast::AstSectionDirective>);
  if (owned->sections.size() != 3 ||
      owned->sections[0].name.text != owned->sections[1].name.text ||
      !ir::validateModule(*owned))
    return false;
  const auto& data =
      std::get<ast::AstSectionData>(owned->sections.front().payload[1]);
  if (data.width != ast::AstSectionWidth::B64 || data.type.text != ".b64")
    return false;
  const auto& integers =
      std::get<std::vector<ast::AstSectionInteger>>(data.value);
  if (integers[0].magnitude.text != "18446744073709551615U" ||
      integers[0].range.start.line != 8)
    return false;
  const auto& addend = std::get<ast::AstSectionAddend>(
      std::get<ast::AstSectionData>(owned->sections.front().payload[3]).value);
  if (addend.name.text != ".debug_info" || addend.operation.text != "+" ||
      addend.value.magnitude.text != "9223372036854775807")
    return false;
  ir::ResolvedModule copied(*owned), assigned;
  assigned = copied;
  ir::ResolvedFunction function(copied.functions.front());
  assigned.functions.front() = function;
  assigned.functions.front().body.front() = function.body.front()->clone();
  if (!ir::validateModule(copied) || !ir::validateModule(assigned))
    return false;
  auto& changed =
      std::get<std::vector<ast::AstSectionInteger>>(
          std::get<ast::AstSectionData>(assigned.sections.front().payload[1])
              .value)
          .front();
  changed.magnitude.text = "18446744073709551616";
  if (ir::validateModule(assigned) || !ir::validateModule(*owned))
    return false;
  ir::ResolvedModule moved(std::move(copied));
  assigned = std::move(moved);
  return bool(ir::validateModule(assigned));
}

/** Verify lawful owned locations and full-width leaves under finite string bounds. */
bool checkInstalledDebugLocations() {
  namespace ir = ptx_frontend::resolved_ir;
  std::optional<ir::ResolvedModule> module;
  // Retain the lexical offset and located diagnostics after the rejected AST dies.
  std::string full_width_offset;
  ptx_frontend::SourceRange full_width_offset_range;
  ir::ModuleResolveDiagnostics module_only_errors, strict_errors;
  for (const std::string_view offset : {"0", "18446744073709551615U"}) {
    const std::string source =
        ".version 9.3\n.target sm_90\n.address_size 64\n"
        ".file 18446744073709551615U \"a.cu\"\n"
        ".section .debug_str { name: .b8 0; }\n.entry loc_kernel() {\n"
        ".loc 18446744073709551615U 4294967296 0\n"
        ".loc 18446744073709551615U 1 0, function_name name + " +
        std::string(offset) +
        ", "
        "inlined_at 18446744073709551615U 4294967296 0\nret;\n}\n";
    ptx_frontend::PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return false;
    if (offset != "0") {
      const auto bound = ptx_frontend::binding::bindSymbols(*ast);
      const auto locations = bound.table.debugLocations();
      if (locations.size() != 2 || !locations[1].inline_context ||
          !locations[1].inline_context->function_name_offset)
        return false;
      const auto& leaf = *locations[1].inline_context->function_name_offset;
      const auto magnitude =
          ptx_frontend::base::parseIntegerMagnitude(leaf.text);
      if (leaf.text != offset || !magnitude || *magnitude != ~std::uint64_t{0})
        return false;
      full_width_offset = leaf.text;
      full_width_offset_range = leaf.range;
      auto lowered = ir::resolveModuleOnly(*ast);
      auto strict = ir::resolveAndValidateModule(*ast);
      if (lowered || strict)
        return false;
      module_only_errors = std::move(lowered.error());
      strict_errors = std::move(strict.error());
      continue;
    }
    auto resolved = ir::resolveAndValidateModule(*ast);
    if (!resolved)
      return false;
    module.emplace(std::move(*resolved));
  }
  if (full_width_offset != "18446744073709551615U" ||
      full_width_offset_range != ptx_frontend::SourceRange{{8, 54}, {8, 75}})
    return false;
  for (const auto* errors : {&module_only_errors, &strict_errors}) {
    bool located_bounds_rejection = false;
    for (const auto& error : *errors)
      if (error.stage() == ir::ResolveDiagnosticStage::Checking &&
          error.checker_kind ==
              ir::checker::CheckDiagnosticKind::RuleViolation &&
          error.range == full_width_offset_range &&
          error.message.find("byte bounds") != std::string::npos)
        located_bounds_rejection = true;
    if (!located_bounds_rejection)
      return false;
  }
  auto copied = *module;
  copied.functions.front().body.front() =
      module->functions.front().body.front()->clone();
  ir::ResolvedModule assigned;
  assigned = copied;
  auto moved = std::move(assigned);
  const auto records = moved.symbols.debugLocations();
  if (records.size() != 2 || !records[1].inline_context ||
      records[0].base.file_index.text != "18446744073709551615U" ||
      records[0].base.line_number.text != "4294967296" ||
      records[1].inline_context->function_name_offset->text != "0" ||
      records[1].range.start.line <= 0 ||
      records[1].inline_context->target.column_position.text != "0" ||
      !ir::validateModule(moved) || !ir::validateModule(copied))
    return false;
  const std::string replacement =
      ".version 9.3\n.target sm_90\n.address_size 64\n.file 2 \"b.cu\"\n"
      ".entry loc_kernel() { .loc 2 4294967297 0 ret; }\n";
  ptx_frontend::PtxSyntaxParser parser(replacement);
  const auto ast = parser.parseModule();
  return ast && ast.diagnostics.empty() && ir::validateModule(*ast, moved);
}

/** Call the installed exported decoder and validate independently owned raw language data. */
bool checkInstalledLanguageDomain() {
  namespace ir = ptx_frontend::resolved_ir;
  namespace sem = ptx_frontend::declaration_semantics;
  if (sem::languageCode("\"pTx\"") != 3u || sem::languageCode("1") != 1u ||
      sem::languageCode("2") != 2u || sem::languageCode("11") ||
      sem::languageCode("\"3\"") || sem::languageCode("ptx") ||
      sem::languageCode(" \"ptx\"") || sem::languageCode("\"\\x70tx\""))
    return false;
  std::optional<ir::ResolvedModule> module;
  {
    const std::string source =
        ".version 9.3\n.target sm_80\n.entry language_kernel() "
        ".language \"PTX\", 1, 0b11U, \"nvvm\", 2 "
        "{ .reg .u32 r; mov.u32 r, 1; ret; }\n";
    ptx_frontend::PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return false;
    auto resolved = ir::resolveAndValidateModule(*ast);
    if (!resolved)
      return false;
    module.emplace(std::move(*resolved));
  }
  const std::vector<std::string> expected{"\"PTX\"", "1", "0b11U", "\"nvvm\"",
                                          "2"};
  if (module->functions[0].contract.language_values != expected ||
      !ir::validateModule(*module))
    return false;
  auto copied = *module;
  ir::ResolvedModule assigned;
  assigned = copied;
  assigned.functions[0].body[0] = assigned.functions[0].body[0]->clone();
  if (assigned.functions[0].contract.language_values != expected ||
      !ir::validateModule(assigned))
    return false;
  assigned.functions[0].contract.language_values =
      std::vector<std::string>{"4", "0x3U"};
  if (!ir::validateModule(assigned))
    return false;
  assigned.functions[0].contract.language_values =
      std::vector<std::string>{"11"};
  return !ir::validateModule(assigned) && ir::validateModule(copied) &&
         ir::validateModule(*module);
}

/** Consume owning advisory records from the installed public module layout. */
bool checkInstalledPragmas() {
  namespace ir = ptx_frontend::resolved_ir;
  std::optional<ir::ResolvedModule> module;
  {
    const std::string source = R"ptx(.version 2.0
.target sm_13
.pragma "module", "";
.entry pragma_kernel() .pragma "header"; {
.pragma "body";
{ .pragma "nested", "opaque"; }
ret;
}
)ptx";
    ptx_frontend::PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return false;
    auto result = ir::resolveAndValidateModule(*ast);
    if (!result)
      return false;
    module.emplace(std::move(*result));
  }
  if (!module || module->pragmas.size() != 4 || !ir::validateModule(*module))
    return false;
  const auto& records = module->pragmas;
  if (records[0].placement != ir::ResolvedPragmaPlacement::Module ||
      records[0].scope_id != module->symbols.moduleScope() ||
      records[0].strings.size() != 2 || records[0].strings[1].text != "\"\"" ||
      records[1].placement != ir::ResolvedPragmaPlacement::EntryHeader ||
      records[1].scope_id != module->functions[0].declaration_scope ||
      records[2].placement != ir::ResolvedPragmaPlacement::Statement ||
      records[3].placement != ir::ResolvedPragmaPlacement::Statement ||
      records[3].strings[0].text != "\"nested\"" ||
      module->symbols.scope(records[3].scope_id).parent != records[2].scope_id)
    return false;
  const std::vector<std::vector<std::string>> expected{
      {"\"module\"", "\"\""},
      {"\"header\""},
      {"\"body\""},
      {"\"nested\"", "\"opaque\""}};
  for (size_t i = 0; i < records.size(); ++i) {
    const auto& record = records[i];
    if (record.strings.size() != expected[i].size() ||
        record.range.start.line == 0 || record.range.end.line == 0)
      return false;
    for (size_t j = 0; j < record.strings.size(); ++j) {
      const auto& leaf = record.strings[j];
      if (leaf.text != expected[i][j] || leaf.range.start.line == 0 ||
          leaf.range.end.line == 0)
        return false;
    }
  }
  const auto copied = *module;
  ir::ResolvedModule assigned = copied;
  assigned.pragmas.clear();
  assigned = copied;
  ir::ResolvedModule moved = std::move(assigned);
  if (!ir::validateModule(copied) || !ir::validateModule(moved) ||
      moved.pragmas[3].strings[1].text != "\"opaque\"" ||
      moved.pragmas[3].strings[1].range != records[3].strings[1].range)
    return false;
  ptx_frontend::PtxSyntaxParser replacement(
      ".version 1.5\n.target sm_13\n.entry pragma_kernel() { { } ret; }\n");
  const auto ast = replacement.parseModule();
  if (!ast || !ast.diagnostics.empty() || !ir::resolveAndValidateModule(*ast) ||
      !ir::validateModule(*ast, moved))
    return false;
  moved.pragmas[0].strings.clear();
  return !ir::validateModule(*ast, moved) && ir::validateModule(copied);
}

/** Borrow source members only from this actual instruction's bounded owner lifetime. */
std::array<const ptx_frontend::WithLocs<ptx_frontend::resolved_ir::RegOrImm>*,
           2>
installedSetpSources(
    const ptx_frontend::resolved_ir::Instruction& instruction) {
  namespace ir = ptx_frontend::resolved_ir;
  if (const auto* p = dynamic_cast<const ir::SetpF16*>(&instruction))
    return {&p->src1, &p->src2};
  if (const auto* p = dynamic_cast<const ir::SetpF16Boolean*>(&instruction))
    return {&p->src1, &p->src2};
  if (const auto* p = dynamic_cast<const ir::SetpBf16*>(&instruction))
    return {&p->src1, &p->src2};
  if (const auto* p = dynamic_cast<const ir::SetpBf16Boolean*>(&instruction))
    return {&p->src1, &p->src2};
  throw std::runtime_error("Expected scalar SETP sources");
}

/** Require new descriptor ABI, both policy arms and all four migrated BF16 members. */
bool checkInstalledExplicitBit16Setp() {
  namespace ir = ptx_frontend::resolved_ir;
  static_assert(
      std::same_as<decltype(ir::SetpBf16{}.src1.value), ir::RegOrImm>);
  static_assert(
      std::same_as<decltype(ir::SetpBf16{}.src2.value), ir::RegOrImm>);
  static_assert(
      std::same_as<decltype(ir::SetpBf16Boolean{}.src1.value), ir::RegOrImm>);
  static_assert(
      std::same_as<decltype(ir::SetpBf16Boolean{}.src2.value), ir::RegOrImm>);
  ir::checker::OperandDescriptor legacy{.target_field_id = "legacy"};
  if (legacy.integer_immediate_type)
    return false;
  const ptx_frontend::SourceRange range{{1, 1}, {1, 2}};
  ir::checker::OperandDescriptor policy{
      .target_field_id = "src",
      .type_expression =
          {.kind = ir::checker::OperandTypeExpressionKind::FixedScalar,
           .fixed_scalar_type = ir::ScalarType::BF16},
      .role = ir::checker::OperandRole::Source,
      .access = ir::checker::OperandAccess::Read,
      .allowed_shapes = ir::checker::OperandShape::Register |
                        ir::checker::OperandShape::Immediate,
      .integer_immediate_type = ir::ScalarType::B16};
  ir::checker::OperandView view{
      .field_id = "src",
      .actual_shape = ir::checker::OperandShape::Immediate,
      .immediate_type = ir::ScalarType::B16,
      .immediate_bits = 1,
      .immediate_is_negative = false,
      .locations = std::span{&range, 1}};
  const auto check = [&] {
    return ir::checker::check_operands(std::span{&policy, 1}, {},
                                       std::span{&view, 1}, {}, {});
  };
  if (!check())
    return false;
  view.immediate_type = ir::ScalarType::BF16;
  view.immediate_bits = 0x3f80;
  if (!check())
    return false;
  policy.integer_immediate_type = ir::ScalarType::U16;
  const auto invalid = check();
  if (invalid ||
      invalid.error().front().kind !=
          ir::checker::CheckDiagnosticKind::RuleViolation ||
      invalid.error().front().range != range)
    return false;
  for (bool bf : {false, true})
    for (bool boolean : {false, true})
      for (bool raw_first : {false, true}) {
        std::optional<ir::ResolvedModule> module;
        {
          const std::string operation =
              std::string("setp.eq") + (boolean ? ".and" : "") +
              (bf ? ".bf16" : ".f16") + " p," +
              (raw_first ? "1,1.0" : "1.0,1") + (boolean ? ",q;" : ";");
          const std::string text =
              ".version 9.3\n.target sm_90\n.entry k(){.reg .pred p,q;" +
              operation + "}";
          ptx_frontend::PtxSyntaxParser parser(text);
          const auto ast = parser.parseModule();
          if (!ast || !ast.diagnostics.empty())
            return false;
          auto result = ir::resolveAndValidateModule(*ast);
          if (!result || !ir::validateModule(*ast, *result))
            return false;
          module.emplace(std::move(*result));
        }
        auto copied = *module;
        ir::ResolvedModule assigned;
        assigned = copied;
        auto function = assigned.functions.front();
        assigned.functions.front() = function;
        assigned.functions.front().body.front() =
            function.body.front()->clone();
        if (!ir::validateModule(assigned))
          return false;
        const auto fields =
            installedSetpSources(*assigned.functions.front().body.front());
        for (size_t i = 0; i < fields.size(); ++i) {
          const auto* word =
              std::get_if<ir::ResolvedImmediate>(&fields[i]->value);
          if (!word || fields[i]->locs.size() != 1 ||
              fields[i]->locs.front() == ptx_frontend::SourceRange{})
            return false;
          const bool raw = (i == 0) == raw_first;
          if (word->type != (raw  ? ir::ScalarType::B16
                             : bf ? ir::ScalarType::BF16
                                  : ir::ScalarType::F16) ||
              word->bits != (raw  ? 1u
                             : bf ? 0x3f80u
                                  : 0x3c00u) ||
              bool(word->integer_source_bits) != raw || word->is_negative)
            return false;
        }
        if (!assigned.functions.front().body.front()->check(
                ir::checker::Context{
                    .target = {.ptx_version = {9, 3}, .sm_version = 90}}))
          return false;
      }
  return true;
}

/** Borrow both changed members from an actual installed exact-class object. */
std::array<const ptx_frontend::WithLocs<ptx_frontend::resolved_ir::RegOrImm>*,
           2>
installedOrdinaryBf16Sources(
    const ptx_frontend::resolved_ir::Instruction& instruction) {
  namespace ir = ptx_frontend::resolved_ir;
  if (const auto* form = dynamic_cast<const ir::FmaMixedF32Bf16*>(&instruction))
    return {&form->src1, &form->src2};
  if (const auto* form =
          dynamic_cast<const ir::SetHalfIntegerBf16*>(&instruction))
    return {&form->src1, &form->src2};
  const auto& form =
      dynamic_cast<const ir::SetHalfIntegerBf16Boolean&>(instruction);
  return {&form.src1, &form.src2};
}

/** Compare actual installed callback addresses only during synchronous traversal. */
struct InstalledOrdinaryBf16Observer final
    : ptx_frontend::resolved_ir::detail::IReferenceObserver {
  /** Borrowed fields; the inspected instruction outlives the traversal. */
  std::array<const ptx_frontend::WithLocs<ptx_frontend::resolved_ir::RegOrImm>*,
             2>
      expected;
  /** One synchronous callback for each exact source member. */
  std::array<bool, 2> seen{};
  /** Bind fields owned by the instruction being visited. */
  explicit InstalledOrdinaryBf16Observer(decltype(expected) fields)
      : expected(fields) {}
  /** Check real member and range addresses without retaining callback arguments. */
  void reg_or_imm(
      const ptx_frontend::resolved_ir::RegOrImm& value,
      std::span<const ptx_frontend::SourceRange> ranges,
      ptx_frontend::resolved_ir::checker::AddressSymbolResolutionPolicy)
      override {
    for (size_t index = 0; index < 2; ++index)
      if (&value == &expected[index]->value &&
          ranges.size() == expected[index]->locs.size()) {
        bool equal = true;
        for (size_t lane = 0; lane < ranges.size(); ++lane)
          equal = equal && ranges[lane] == expected[index]->locs[lane];
        seen[index] = equal;
      }
  }
};

/** Exercise all six migrated members through the existing installed resolver/class API. */
bool checkInstalledOrdinaryBf16FmaSet() {
  namespace ir = ptx_frontend::resolved_ir;
  static_assert(
      std::same_as<decltype(ir::FmaMixedF32Bf16{}.src1.value), ir::RegOrImm>);
  static_assert(
      std::same_as<decltype(ir::FmaMixedF32Bf16{}.src2.value), ir::RegOrImm>);
  static_assert(std::same_as<decltype(ir::SetHalfIntegerBf16{}.src1.value),
                             ir::RegOrImm>);
  static_assert(std::same_as<decltype(ir::SetHalfIntegerBf16{}.src2.value),
                             ir::RegOrImm>);
  static_assert(
      std::same_as<decltype(ir::SetHalfIntegerBf16Boolean{}.src1.value),
                   ir::RegOrImm>);
  static_assert(
      std::same_as<decltype(ir::SetHalfIntegerBf16Boolean{}.src2.value),
                   ir::RegOrImm>);
  static_assert(ir::FmaMixedF32Bf16::is_instruction &&
                ir::SetHalfIntegerBf16::is_instruction &&
                ir::SetHalfIntegerBf16Boolean::is_instruction);
  static_assert(ir::FmaMixedF32Bf16::input_type == ir::ScalarType::BF16);
  std::optional<ir::ResolvedModule> owned;
  {
    const std::string source = R"ptx(
.version 9.3
.target sm_100a
.entry kernel(){
 .reg .b16 a,b; .reg .f32 f; .reg .u32 u; .reg .pred p;
 fma.rn.f32.bf16 f,1.5,b,f;
 fma.rn.f32.bf16 f,a,1.5,f;
 set.eq.u32.bf16 u,1.5,b;
 set.eq.u32.bf16 u,a,1.5;
 set.eq.and.u32.bf16 u,1.5,b,p;
 set.eq.and.u32.bf16 u,a,1.5,p;
}
)ptx";
    ptx_frontend::PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return false;
    auto result = ir::resolveAndValidateModule(*ast);
    if (!result || !ir::validateModule(*ast, *result))
      return false;
    owned.emplace(std::move(*result));
  }
  if (owned->functions.front().body.size() != 6)
    return false;
  for (size_t index = 0; index < 6; ++index) {
    const auto& instruction = *owned->functions.front().body[index];
    const auto fields = installedOrdinaryBf16Sources(instruction);
    InstalledOrdinaryBf16Observer observer(fields);
    instruction.visit_references(observer);
    if (!observer.seen[0] || !observer.seen[1])
      return false;
    const auto* immediate =
        std::get_if<ir::ResolvedImmediate>(&fields[index % 2]->value);
    const auto* reg =
        std::get_if<ir::ResolvedRegisterRef>(&fields[1 - index % 2]->value);
    if (!immediate || immediate->type != ir::ScalarType::BF16 ||
        immediate->bits != 0x3fc0 || immediate->integer_source_bits ||
        immediate->is_negative || !reg || !reg->symbol_id ||
        reg->declared_type != ir::ScalarType::B16 || fields[0]->locs.empty() ||
        fields[1]->locs.empty())
      return false;
    const auto clone = instruction.clone();
    if (clone->instruction_kind() != instruction.instruction_kind() ||
        installedOrdinaryBf16Sources(*clone)[0]->value != fields[0]->value)
      return false;
  }
  auto copied = *owned;
  ir::ResolvedModule assigned;
  assigned = copied;
  owned.reset();
  return ir::validateModule(copied).has_value() &&
         ir::validateModule(assigned).has_value();
}

#if defined(PTX_FRONTEND_OBSERVE_ATOMIC_F16_VIEWS)
namespace atomic_f16_generated_projection {
namespace ir = ptx_frontend::resolved_ir;
/** Borrow only during one synchronous generated check on a live concrete instruction. */
const ptx_frontend::WithLocs<ir::RegOrImm>* expected = nullptr;
/** Count actual generated source projections without substituting a manual view. */
size_t observed = 0;
/** Preserve all assertion failures while still returning the real checker result. */
bool mirrors_valid = true;
#define ATOMIC_VIEW_PARAMS                                             \
  std::span<const ir::checker::OperandDescriptor> descriptors,         \
      std::span<const ir::checker::FieldView> fields,                  \
      std::span<const ir::checker::OperandView> views,                 \
      std::span<const ir::checker::OperandTypeCompatibilityDescriptor> \
          compatibility, const ir::checker::Context &context,          \
      const ir::MatrixInstructionDescriptor *matrix
/** Invoke the original installed checker after inspecting its generated arguments. */
extern "C" ir::checker::CheckResult
    real_atomic_operands(ATOMIC_VIEW_PARAMS) asm(
        "__real__ZN12ptx_frontend11resolved_ir7checker14check_"
        "operandsESt4spanIKNS1_17OperandDescriptorELm18446744073709551615EES2_"
        "IKNS1_9FieldViewELm18446744073709551615EES2_IKNS1_"
        "11OperandViewELm18446744073709551615EES2_IKNS1_"
        "34OperandTypeCompatibilityDescriptorELm18446744073709551615EERKNS1_"
        "7ContextEPKNS0_27MatrixInstructionDescriptorE");
/** Test-only link interception of the unchanged public checker signature. */
extern "C" ir::checker::CheckResult
    wrap_atomic_operands(ATOMIC_VIEW_PARAMS) asm(
        "__wrap__ZN12ptx_frontend11resolved_ir7checker14check_"
        "operandsESt4spanIKNS1_17OperandDescriptorELm18446744073709551615EES2_"
        "IKNS1_9FieldViewELm18446744073709551615EES2_IKNS1_"
        "11OperandViewELm18446744073709551615EES2_IKNS1_"
        "34OperandTypeCompatibilityDescriptorELm18446744073709551615EERKNS1_"
        "7ContextEPKNS0_27MatrixInstructionDescriptorE");
/** Compare exact current payload pointers, mirrors, metadata and original range storage. */
ir::checker::CheckResult wrap_atomic_operands(ATOMIC_VIEW_PARAMS) {
  if (expected)
    for (const auto& view : views)
      if (view.field_id == "src") {
        ++observed;
        bool valid = view.locations.data() == expected->locs.data() &&
                     std::ranges::equal(view.locations, expected->locs);
        const auto binding =
            std::ranges::find(descriptors, std::string_view{"src"},
                              &ir::checker::OperandDescriptor::target_field_id);
        valid =
            valid && binding != descriptors.end() &&
            binding->type_expression.kind ==
                ir::checker::OperandTypeExpressionKind::FixedScalar &&
            binding->type_expression.fixed_scalar_type == ir::ScalarType::F16 &&
            binding->register_width_policy ==
                ptx_frontend::base::ScalarTypeSizePolicy::SameWidth &&
            binding->allowed_shapes == (ir::checker::OperandShape::Register |
                                        ir::checker::OperandShape::Immediate) &&
            !binding->integer_immediate_type && !binding->source_value_vector;
        if (const auto* reg =
                std::get_if<ir::ResolvedRegisterRef>(&expected->value)) {
          valid = valid &&
                  view.actual_shape == ir::checker::OperandShape::Register &&
                  view.register_ref == reg &&
                  view.register_type == reg->declared_type &&
                  view.register_symbol_id == reg->symbol_id &&
                  view.register_class == reg->register_class &&
                  view.register_vector_width == reg->vector_width &&
                  !view.immediate_type && !view.immediate_bits;
        } else if (const auto* word =
                       std::get_if<ir::ResolvedImmediate>(&expected->value)) {
          valid = valid &&
                  view.actual_shape == ir::checker::OperandShape::Immediate &&
                  !view.register_ref && view.immediate_type == word->type &&
                  view.immediate_bits == word->bits &&
                  view.immediate_is_negative == word->is_negative &&
                  view.integer_source_bits == word->integer_source_bits;
        } else
          valid = false;
        mirrors_valid = mirrors_valid && valid;
      }
  return real_atomic_operands(descriptors, fields, views, compatibility,
                              context, matrix);
}
#undef ATOMIC_VIEW_PARAMS
}  // namespace atomic_f16_generated_projection
#endif

/** Verify the real callback without retaining any callback borrow after the visit. */
struct InstalledAtomicF16Capture final
    : ptx_frontend::resolved_ir::detail::IReferenceObserver {
  /** Actual field is owned by the instruction throughout this synchronous traversal. */
  const ptx_frontend::WithLocs<ptx_frontend::resolved_ir::RegOrImm>& expected;
  /** Accumulated pointer/range validity and exact migrated source callback count. */
  bool valid = true;
  size_t matches = 0;
  /** Bind the owning field before entering the visitor. */
  explicit InstalledAtomicF16Capture(
      const ptx_frontend::WithLocs<ptx_frontend::resolved_ir::RegOrImm>& field)
      : expected(field) {}
  /** Observe the actual variant storage, not a copied immediate or register adapter. */
  void reg_or_imm(
      const ptx_frontend::resolved_ir::RegOrImm& value,
      std::span<const ptx_frontend::SourceRange> ranges,
      ptx_frontend::resolved_ir::checker::AddressSymbolResolutionPolicy)
      override {
    ++matches;
    valid = valid && &value == &expected.value &&
            ranges.data() == expected.locs.data() &&
            std::ranges::equal(ranges, expected.locs);
  }
};
/** Compute end-exclusive source coordinates independently of all generated location caches. */
ptx_frontend::SourceRange installedAtomicRange(std::string_view text,
                                               size_t offset, size_t size) {
  ptx_frontend::SourcePos start{1, 1};
  for (char character : text.substr(0, offset)) {
    if (character == '\n') {
      ++start.line;
      start.column = 1;
    } else
      ++start.column;
  }
  return {start, {start.line, start.column + static_cast<int32_t>(size)}};
}
/** Check real factory/member/callback behavior for two classes and both selected layouts. */
bool checkInstalledScalarAtomicF16Sources() {
  namespace ir = ptx_frontend::resolved_ir;
  static_assert(std::same_as<decltype(ir::AtomGlobalAddNoftzF16{}.src),
                             ptx_frontend::WithLocs<ir::RegOrImm>>);
  static_assert(std::same_as<decltype(ir::RedGlobalAddNoftzF16{}.src),
                             ptx_frontend::WithLocs<ir::RegOrImm>>);
  static_assert(std::is_constructible_v<ir::ResolvedFieldValue,
                                        ptx_frontend::WithLocs<ir::RegOrImm>>);
  static_assert(std::is_copy_constructible_v<ir::AtomGlobalAddNoftzF16> &&
                std::is_copy_assignable_v<ir::AtomGlobalAddNoftzF16>);
  static_assert(std::is_copy_constructible_v<ir::RedGlobalAddNoftzF16> &&
                std::is_copy_assignable_v<ir::RedGlobalAddNoftzF16>);
#if defined(PTX_FRONTEND_OBSERVE_ATOMIC_F16_VIEWS)
  atomic_f16_generated_projection::observed = 0;
  atomic_f16_generated_projection::mirrors_valid = true;
#endif
  for (bool atom : {false, true})
    for (bool policy : {false, true})
      for (unsigned arm = 0; arm < 3; ++arm) {
        const std::string_view spelling = arm == 0   ? "b"
                                          : arm == 1 ? "h"
                                                     : "1.5";
        std::optional<ir::ResolvedModule> module;
        ptx_frontend::SourceRange expected_range;
        {
          std::string source =
              ".version 9.3\n.target sm_90\n.address_size 64\n.global .align 2 "
              ".b16 memory;\n"
              ".entry k(){.reg .b16 d,b;.reg .f16 h;.reg .b64 policy;" +
              std::string(atom ? "atom" : "red") + ".global.add.noftz" +
              (policy ? ".L2::cache_hint" : "") + ".f16 " +
              (atom ? "d,[memory]," : "[memory],");
          const auto offset = source.size();
          source += spelling;
          source += policy ? ",policy;}" : ";}";
          expected_range =
              installedAtomicRange(source, offset, spelling.size());
          ptx_frontend::PtxSyntaxParser parser(source);
          const auto ast = parser.parseModule();
          if (!ast || !ast.diagnostics.empty())
            return false;
          auto resolved = ir::resolveAndValidateModule(*ast);
          if (!resolved || !ir::validateModule(*ast, *resolved))
            return false;
          module.emplace(std::move(*resolved));
        }
        if (module->functions.size() != 1 ||
            module->functions.front().body.size() != 1)
          return false;
        auto copied = *module;
        ir::ResolvedModule assigned;
        assigned = copied;
        ir::ResolvedFunction function(copied.functions.front()),
            function_assigned;
        function_assigned = function;
        assigned.functions.front() = function_assigned;
        assigned.functions.front().body.front() =
            function.body.front()->clone();
        module.reset();
        auto& instruction = *assigned.functions.front().body.front();
        const ptx_frontend::WithLocs<ir::RegOrImm>* source = nullptr;
        if (atom) {
          const auto* exact =
              dynamic_cast<const ir::AtomGlobalAddNoftzF16*>(&instruction);
          if (!exact || exact->operand_layout.value != (policy ? 1u : 0u) ||
              bool(exact->cache_policy) != policy)
            return false;
          source = &exact->src;
        } else {
          const auto* exact =
              dynamic_cast<const ir::RedGlobalAddNoftzF16*>(&instruction);
          if (!exact || exact->operand_layout.value != (policy ? 1u : 0u) ||
              bool(exact->cache_policy) != policy)
            return false;
          source = &exact->src;
        }
        if (source->locs.size() != 1 || source->locs.front() != expected_range)
          return false;
        if (arm == 2) {
          const auto* word = std::get_if<ir::ResolvedImmediate>(&source->value);
          if (!word || word->type != ir::ScalarType::F16 ||
              word->bits != 0x3e00 || word->integer_source_bits ||
              word->is_negative)
            return false;
        } else {
          const auto* reg =
              std::get_if<ir::ResolvedRegisterRef>(&source->value);
          if (!reg || !reg->symbol_id || reg->spelling != spelling ||
              reg->declared_type !=
                  (arm == 0 ? ir::ScalarType::B16 : ir::ScalarType::F16) ||
              reg->register_class != ir::ResolvedRegisterClass::General ||
              reg->vector_width || reg->component)
            return false;
        }
        ir::ResolvedFieldValue field_value = *source;
        const auto* field_copy =
            std::get_if<ptx_frontend::WithLocs<ir::RegOrImm>>(&field_value);
        if (!field_copy || *field_copy != *source)
          return false;
        InstalledAtomicF16Capture capture(*source);
        instruction.visit_references(capture);
        if (!capture.valid || capture.matches != 1 ||
            !ir::validateModule(
                assigned, ir::ModuleValidationPolicy::RequireCompleteContext))
          return false;
#if defined(PTX_FRONTEND_OBSERVE_ATOMIC_F16_VIEWS)
        const auto before = atomic_f16_generated_projection::observed;
        atomic_f16_generated_projection::expected = source;
#endif
        const auto checked = instruction.check(
            {.target = {.ptx_version = {9, 3}, .sm_version = 90},
             .instruction_range =
                 assigned.functions.front().instruction_ranges.front()});
#if defined(PTX_FRONTEND_OBSERVE_ATOMIC_F16_VIEWS)
        atomic_f16_generated_projection::expected = nullptr;
        if (atomic_f16_generated_projection::observed != before + 1 ||
            !atomic_f16_generated_projection::mirrors_valid)
          return false;
#endif
        if (!checked)
          return false;
        const std::string factory_operand = arm == 0   ? "%b0"
                                            : arm == 1 ? "%h0"
                                                       : "1.5";
        const std::string operation =
            std::string(atom ? "atom" : "red") + ".global.add.noftz" +
            (policy ? ".L2::cache_hint" : "") + ".f16 " +
            (atom ? "%d0,[%rd0]," : "[%rd0],") + factory_operand +
            (policy ? ",%policy0;" : ";");
        ptx_frontend::PtxSyntaxParser factory_parser(operation);
        const auto factory_ast = factory_parser.parseInstruction();
        if (!factory_ast || !factory_ast.diagnostics.empty())
          return false;
        auto factory = ir::resolveInstruction(*factory_ast);
        if (!factory ||
            (*factory)->instruction_kind() != instruction.instruction_kind())
          return false;
        auto clone = (*factory)->clone();
        if (!clone ||
            clone->instruction_kind() != instruction.instruction_kind())
          return false;
        const ptx_frontend::WithLocs<ir::RegOrImm>* cloned_source = nullptr;
        if (atom) {
          const auto* exact =
              dynamic_cast<const ir::AtomGlobalAddNoftzF16*>(clone.get());
          if (!exact)
            return false;
          cloned_source = &exact->src;
        } else {
          const auto* exact =
              dynamic_cast<const ir::RedGlobalAddNoftzF16*>(clone.get());
          if (!exact)
            return false;
          cloned_source = &exact->src;
        }
        if (arm == 2) {
          const auto* word =
              std::get_if<ir::ResolvedImmediate>(&cloned_source->value);
          if (!word || word->type != ir::ScalarType::F16 ||
              word->bits != 0x3e00 || word->integer_source_bits ||
              word->is_negative)
            return false;
        } else if (!std::get_if<ir::ResolvedRegisterRef>(&cloned_source->value))
          return false;
      }
#if defined(PTX_FRONTEND_OBSERVE_ATOMIC_F16_VIEWS)
  if (atomic_f16_generated_projection::observed != 12 ||
      !atomic_f16_generated_projection::mirrors_valid)
    return false;
  std::cout
      << "ATOMIC_GENERATED_VIEWS 12 actual borrowed projections checked\n";
#endif
  return true;
}

/** Borrow the installed src member only while its actual instruction owner remains alive. */
const ptx_frontend::WithLocs<ptx_frontend::resolved_ir::RegOrImm>&
installedAtomicBf16Source(
    const ptx_frontend::resolved_ir::Instruction& instruction) {
  namespace ir = ptx_frontend::resolved_ir;
  if (const auto* atom =
          dynamic_cast<const ir::AtomGlobalAddNoftzBf16*>(&instruction))
    return atom->src;
  return dynamic_cast<const ir::RedGlobalAddNoftzBf16&>(instruction).src;
}

/** Check both installed exact classes/layouts with register and converted immediate sources. */
bool checkInstalledScalarAtomicBf16() {
  namespace ir = ptx_frontend::resolved_ir;
  static_assert(std::same_as<decltype(ir::AtomGlobalAddNoftzBf16{}.src.value),
                             ir::RegOrImm>);
  static_assert(std::same_as<decltype(ir::RedGlobalAddNoftzBf16{}.src.value),
                             ir::RegOrImm>);
  static_assert(std::derived_from<ir::AtomGlobalAddNoftzBf16, ir::Instruction>);
  static_assert(std::derived_from<ir::RedGlobalAddNoftzBf16, ir::Instruction>);
  static_assert(std::is_copy_constructible_v<ir::AtomGlobalAddNoftzBf16> &&
                std::is_copy_assignable_v<ir::AtomGlobalAddNoftzBf16>);
  static_assert(std::is_move_constructible_v<ir::RedGlobalAddNoftzBf16> &&
                std::is_copy_assignable_v<ir::RedGlobalAddNoftzBf16>);
  /** Emit a precise contract label when a future installed fixture fails. */
  const auto require = [](bool condition, std::string_view contract) {
    if (!condition)
      std::cerr << "atomic BF16 consumer: " << contract << '\n';
    return condition;
  };
  const std::array<std::string_view, 4> numbered{
      "atom.global.add.noftz.bf16 %b0,[%rd0],$;",
      "atom.global.add.noftz.L2::cache_hint.bf16 %b0,[%rd0],$,%rd1;",
      "red.global.add.noftz.bf16 [%rd0],$;",
      "red.global.add.noftz.L2::cache_hint.bf16 [%rd0],$,%rd1;"};
  for (size_t layout = 0; layout < numbered.size(); ++layout)
    for (const auto value : {"%b1", "1.5"}) {
      std::string operation(numbered[layout]);
      operation.replace(operation.find('$'), 1, value);
      ptx_frontend::PtxSyntaxParser parser(operation);
      const auto ast = parser.parseInstruction();
      if (!require(ast && ast.diagnostics.empty(),
                   "standalone numbered factory source parses"))
        return false;
      auto instruction = ir::resolveInstruction(*ast);
      const auto kind = layout < 2 ? ir::AtomGlobalAddNoftzBf16::kind
                                   : ir::RedGlobalAddNoftzBf16::kind;
      if (!require(instruction && *instruction &&
                       (*instruction)->instruction_kind() == kind,
                   "existing generic factory selects exact atomic class"))
        return false;
      const auto clone = (*instruction)->clone();
      if (!require(clone && clone->instruction_kind() == kind,
                   "standalone exact factory clone"))
        return false;
      const auto& field = installedAtomicBf16Source(*clone);
      if (std::string_view(value) == "1.5") {
        const auto* word = std::get_if<ir::ResolvedImmediate>(&field.value);
        if (!require(word && word->type == ir::ScalarType::BF16 &&
                         word->bits == 0x3fc0 && !word->integer_source_bits &&
                         !word->is_negative,
                     "standalone factory uses existing BF16 converter"))
          return false;
      } else if (!require(std::get_if<ir::ResolvedRegisterRef>(&field.value),
                          "standalone factory retains register alternative"))
        return false;
      // Standalone construction has no bound declaration or target legality claim.
    }
  std::optional<ir::ResolvedModule> owned;
  {
    const std::string source = R"ptx(
.version 9.3
.target sm_90
.global .align 2 .b16 data;
.entry kernel(){
 .reg .b16 h,d; .reg .b64 policy;
 atom.global.add.noftz.bf16 d,[data],h;
 atom.global.add.noftz.bf16 d,[data],1.5;
 atom.global.add.noftz.L2::cache_hint.bf16 d,[data],h,policy;
 atom.global.add.noftz.L2::cache_hint.bf16 d,[data],1.5,policy;
 red.global.add.noftz.bf16 [data],h;
 red.global.add.noftz.bf16 [data],1.5;
 red.global.add.noftz.L2::cache_hint.bf16 [data],h,policy;
 red.global.add.noftz.L2::cache_hint.bf16 [data],1.5,policy;
}
)ptx";
    ptx_frontend::PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    if (!require(ast.has_value(), "complete aligned source parses") ||
        !require(ast.diagnostics.empty(), "parser diagnostics are empty"))
      return false;
    auto result = ir::resolveAndValidateModule(*ast);
    if (!require(result.has_value(),
                 "both classes/layouts resolve and strictly validate"))
      return false;
    if (!require(ir::validateModule(*ast, *result).has_value(),
                 "provided AST association validates"))
      return false;
    owned.emplace(std::move(*result));
  }
  if (!require(owned && owned->functions.size() == 1,
               "one owned function survives AST death") ||
      !require(owned->functions.front().body.size() == 8,
               "all eight independent source controls are owned"))
    return false;
  /** Observe actual members synchronously; no callback payload/range pointer escapes. */
  struct Observer final : ir::detail::IReferenceObserver {
    /** Borrowed actual source, valid only through visit_references. */
    const ptx_frontend::WithLocs<ir::RegOrImm>& expected;
    /** True after the callback matches the owner address and all source ranges. */
    bool seen = false;
    /** Borrow one live owner for synchronous inspection. */
    explicit Observer(const ptx_frontend::WithLocs<ir::RegOrImm>& source)
        : expected(source) {}
    /** Inspect the real member without constructing a surrogate callback value. */
    void reg_or_imm(const ir::RegOrImm& value,
                    std::span<const ptx_frontend::SourceRange> ranges,
                    ir::checker::AddressSymbolResolutionPolicy) override {
      if (&value != &expected.value || ranges.size() != expected.locs.size())
        return;
      seen = true;
      for (size_t index = 0; index < ranges.size(); ++index)
        seen = seen && ranges[index] == expected.locs[index];
    }
  };
  for (size_t index = 0; index < 8; ++index) {
    if (!require(owned->functions.front().body[index] != nullptr,
                 "owned instruction exists"))
      return false;
    const auto& instruction = *owned->functions.front().body[index];
    const auto expected_kind = index < 4 ? ir::AtomGlobalAddNoftzBf16::kind
                                         : ir::RedGlobalAddNoftzBf16::kind;
    if (!require(instruction.instruction_kind() == expected_kind,
                 "exact installed final class selected"))
      return false;
    const auto& source = installedAtomicBf16Source(instruction);
    const bool hinted = index % 4 >= 2;
    if (!require(!source.locs.empty(), "source range retained"))
      return false;
    if (index % 2 == 1) {
      const auto* immediate = std::get_if<ir::ResolvedImmediate>(&source.value);
      if (!require(immediate && immediate->type == ir::ScalarType::BF16 &&
                       immediate->bits == 0x3fc0 &&
                       !immediate->integer_source_bits &&
                       !immediate->is_negative,
                   "logical BF16 immediate word/flags"))
        return false;
    } else {
      const auto* reg = std::get_if<ir::ResolvedRegisterRef>(&source.value);
      if (!require(reg && reg->symbol_id &&
                       reg->declared_type == ir::ScalarType::B16,
                   "existing actual B16 register carrier and SymbolId"))
        return false;
    }
    const auto& policy =
        index < 4 ? dynamic_cast<const ir::AtomGlobalAddNoftzBf16&>(instruction)
                        .cache_policy
                  : dynamic_cast<const ir::RedGlobalAddNoftzBf16&>(instruction)
                        .cache_policy;
    if (!require(policy.has_value() == hinted,
                 "cache-policy optional presence matches layout"))
      return false;
    if (hinted && !require(policy->value.declared_type == ir::ScalarType::B64,
                           "policy remains a B64 register"))
      return false;
    if (index < 4) {
      const auto& atom =
          dynamic_cast<const ir::AtomGlobalAddNoftzBf16&>(instruction);
      if (!require(atom.dst.value.register_ref.has_value(),
                   "atomic destination exists") ||
          !require(
              atom.dst.value.register_ref->declared_type == ir::ScalarType::B16,
              "atomic destination keeps exact B16 carrier"))
        return false;
    }
    Observer observer(source);
    instruction.visit_references(observer);
    if (!require(observer.seen,
                 "observer receives actual owned source and ranges"))
      return false;
    const auto clone = instruction.clone();
    if (!require(
            clone &&
                clone->instruction_kind() == instruction.instruction_kind() &&
                installedAtomicBf16Source(*clone) == source,
            "existing exact-class clone preserves migrated payload"))
      return false;
  }
  auto copied = *owned;
  ir::ResolvedModule assigned;
  assigned = copied;
  owned.reset();
  ir::ResolvedFunction function = assigned.functions.front(), function_assigned;
  function_assigned = function;
  assigned.functions.front() = std::move(function_assigned);
  for (size_t index = 0; index < function.body.size(); ++index)
    assigned.functions.front().body[index] = function.body[index]->clone();
  ir::ResolvedModule moved = std::move(assigned);
  assigned = std::move(moved);
  for (const auto* module : {&copied, &assigned})
    for (const auto& instruction : module->functions.front().body) {
      const auto& field = installedAtomicBf16Source(*instruction);
      Observer observer(field);
      instruction->visit_references(observer);
      if (!require(observer.seen && !field.locs.empty(),
                   "reacquired copied/cloned callback and locations"))
        return false;
    }
  return require(ir::validateModule(copied).has_value(),
                 "copied owner validates after producer death") &&
         require(ir::validateModule(assigned).has_value(),
                 "assigned owner validates after producer death");
}

/** Exercise installed packed SETP fields with real source-dead nonempty payloads. */
bool checkInstalledPackedSetpSources() {
  namespace ir = ptx_frontend::resolved_ir;
  static_assert(std::same_as<decltype(ir::SetpF16x2{}.src1),
                             ptx_frontend::WithLocs<ir::RegOrImm>>);
  static_assert(std::same_as<decltype(ir::SetpF16x2{}.src2),
                             ptx_frontend::WithLocs<ir::RegOrImm>>);
  static_assert(std::same_as<decltype(ir::SetpF16x2Boolean{}.src1),
                             ptx_frontend::WithLocs<ir::RegOrImm>>);
  static_assert(std::same_as<decltype(ir::SetpF16x2Boolean{}.src2),
                             ptx_frontend::WithLocs<ir::RegOrImm>>);
  static_assert(std::same_as<decltype(ir::SetpBf16x2{}.src1),
                             ptx_frontend::WithLocs<ir::RegOrImm>>);
  static_assert(std::same_as<decltype(ir::SetpBf16x2{}.src2),
                             ptx_frontend::WithLocs<ir::RegOrImm>>);
  static_assert(std::same_as<decltype(ir::SetpBf16x2Boolean{}.src1),
                             ptx_frontend::WithLocs<ir::RegOrImm>>);
  static_assert(std::same_as<decltype(ir::SetpBf16x2Boolean{}.src2),
                             ptx_frontend::WithLocs<ir::RegOrImm>>);
  if (ir::checker::OperandDescriptor{}.integer_immediate_type)
    return false;
  /** Compare actual callback fields with independent case expectations synchronously. */
  struct PackedObserver final : ir::detail::IReferenceObserver {
    /** Field addresses borrow the current instruction only for the following visit. */
    std::array<const ir::RegOrImm*, 2> members{};
    /** Exact written operand ranges, computed before source/AST destruction. */
    std::array<ptx_frontend::SourceRange, 2> ranges{};
    /** Selected operand, producer arm and logical format are independent case inputs. */
    size_t selected = 0;
    unsigned mode = 0;
    ir::ScalarType logical = ir::ScalarType::F16x2;
    /** Accumulated validity and descriptor-order callback count. */
    bool valid = true;
    size_t count = 0;
    /** Inspect the original member address and exact typed payload during the callback. */
    void reg_or_imm(const ir::RegOrImm& value,
                    std::span<const ptx_frontend::SourceRange> locations,
                    ir::checker::AddressSymbolResolutionPolicy) override {
      if (count >= members.size()) {
        valid = false;
        return;
      }
      valid = valid && &value == members[count] && locations.size() == 1 &&
              locations.front() == ranges[count];
      const unsigned expected_mode = count == selected ? mode : 0;
      if (expected_mode == 0) {
        const auto* reg = std::get_if<ir::ResolvedRegisterRef>(&value);
        valid = valid && reg && reg->spelling == "b" && reg->symbol_id &&
                reg->declared_type == ir::ScalarType::B32 &&
                reg->register_class == ir::ResolvedRegisterClass::General &&
                !reg->vector_width && !reg->component;
      } else {
        const auto* word = std::get_if<ir::ResolvedImmediate>(&value);
        const auto expected_type =
            expected_mode == 1 ? ir::ScalarType::B32 : logical;
        const uint64_t expected_bits =
            expected_mode == 1 ? uint64_t{1} : uint64_t{0x7f817c01};
        const auto expected_origin =
            expected_mode == 1 ? std::optional<uint64_t>{1} : std::nullopt;
        valid = valid && word && word->type == expected_type &&
                word->bits == expected_bits &&
                word->integer_source_bits == expected_origin &&
                !word->is_negative;
      }
      ++count;
    }
  };
  /** Return actual source members without retaining transient factory or observer values. */
  const auto sources = [](const ir::Instruction& instruction)
      -> std::array<const ptx_frontend::WithLocs<ir::RegOrImm>*, 2> {
    if (const auto* form = dynamic_cast<const ir::SetpF16x2*>(&instruction))
      return {&form->src1, &form->src2};
    if (const auto* form =
            dynamic_cast<const ir::SetpF16x2Boolean*>(&instruction))
      return {&form->src1, &form->src2};
    if (const auto* form = dynamic_cast<const ir::SetpBf16x2*>(&instruction))
      return {&form->src1, &form->src2};
    if (const auto* form =
            dynamic_cast<const ir::SetpBf16x2Boolean*>(&instruction))
      return {&form->src1, &form->src2};
    return {};
  };
  for (const bool bf16 : {false, true})
    for (const bool boolean : {false, true})
      for (size_t selected = 0; selected < 2; ++selected)
        for (unsigned mode = 0; mode < 3; ++mode) {
          const std::string_view spelling = mode == 0   ? "b"
                                            : mode == 1 ? "1"
                                                        : "0f7f817c01";
          const auto logical =
              bf16 ? ir::ScalarType::BF16x2 : ir::ScalarType::F16x2;
          std::optional<ir::ResolvedModule> owned;
          std::array<ptx_frontend::SourceRange, 2> expected_ranges{};
          {
            std::string text =
                ".version 9.3\n.target sm_90\n.entry kernel(){"
                ".reg .pred p,q,r; .reg .b32 b; setp.eq";
            text += boolean ? ".and" : "";
            text += bf16 ? ".bf16x2 p|q," : ".f16x2 p|q,";
            // Both operands are on the third line; end columns are exclusive.
            const auto line_start = text.rfind('\n') + 1;
            for (size_t index = 0; index < 2; ++index) {
              if (index != 0)
                text += ',';
              const auto operand =
                  index == selected ? spelling : std::string_view{"b"};
              const auto start_column =
                  static_cast<int32_t>(text.size() - line_start + 1);
              expected_ranges[index] = {
                  {3, start_column},
                  {3, start_column + static_cast<int32_t>(operand.size())}};
              text += operand;
            }
            text += boolean ? ",r; }" : "; }";
            ptx_frontend::PtxSyntaxParser parser(text);
            const auto ast = parser.parseModule();
            if (!ast || !ast.diagnostics.empty())
              return false;
            auto resolved = ir::resolveAndValidateModule(*ast);
            if (!resolved)
              return false;
            owned.emplace(std::move(*resolved));
          }
          if (owned->functions.size() != 1 ||
              owned->functions.front().body.size() != 1)
            return false;
          auto copied = *owned;
          ir::ResolvedModule assigned;
          assigned = copied;
          auto function = copied.functions.front();
          assigned.functions.front() = function;
          assigned.functions.front().body.front() =
              function.body.front()->clone();
          owned.reset();
          for (const auto* module : {&copied, &assigned}) {
            const auto& instruction = *module->functions.front().body.front();
            const bool correct_class =
                bf16 ? (boolean ? dynamic_cast<const ir::SetpBf16x2Boolean*>(
                                      &instruction) != nullptr
                                : dynamic_cast<const ir::SetpBf16x2*>(
                                      &instruction) != nullptr)
                     : (boolean ? dynamic_cast<const ir::SetpF16x2Boolean*>(
                                      &instruction) != nullptr
                                : dynamic_cast<const ir::SetpF16x2*>(
                                      &instruction) != nullptr);
            if (!correct_class)
              return false;
            const auto fields = sources(instruction);
            if (!fields[0] || !fields[1])
              return false;
            PackedObserver observer;
            observer.selected = selected;
            observer.mode = mode;
            observer.logical = logical;
            observer.ranges = expected_ranges;
            for (size_t index = 0; index < 2; ++index) {
              if (fields[index]->locs.size() != 1 ||
                  fields[index]->locs.front() != expected_ranges[index])
                return false;
              observer.members[index] = &fields[index]->value;
            }
            instruction.visit_references(observer);
            if (!observer.valid || observer.count != 2 ||
                !ir::validateModule(*module))
              return false;
          }
        }
  return true;
}

#if defined(PTX_PACKED_SETP_CHECKER_WRAP)
/** Reacquire the selected migrated member from the real current exact class. */
ptx_frontend::WithLocs<ptx_frontend::resolved_ir::RegOrImm>&
installedPackedSetpField(ptx_frontend::resolved_ir::Instruction& instruction,
                         bool first) {
  namespace ir = ptx_frontend::resolved_ir;
  if (auto* value = dynamic_cast<ir::SetpF16x2*>(&instruction))
    return first ? value->src1 : value->src2;
  if (auto* value = dynamic_cast<ir::SetpF16x2Boolean*>(&instruction))
    return first ? value->src1 : value->src2;
  if (auto* value = dynamic_cast<ir::SetpBf16x2*>(&instruction))
    return first ? value->src1 : value->src2;
  if (auto* value = dynamic_cast<ir::SetpBf16x2Boolean*>(&instruction))
    return first ? value->src1 : value->src2;
  throw std::logic_error("Expected current packed SETP class.");
}
/** Verify real generated projections and the contextual source factory using only installed interfaces. */
bool checkInstalledPackedSetpGeneratedViews() {
  namespace ir = ptx_frontend::resolved_ir;
  static_assert(std::is_copy_constructible_v<ir::SetpF16x2> &&
                std::is_copy_assignable_v<ir::SetpF16x2Boolean>);
  static_assert(std::is_base_of_v<ir::Instruction, ir::SetpBf16x2> &&
                std::is_default_constructible_v<ir::SetpBf16x2Boolean>);
  ir::SetpF16x2 default_form;
  if (!std::holds_alternative<ir::ResolvedRegisterRef>(default_form.src1.value))
    return false;
  for (bool bf16 : {false, true})
    for (bool boolean : {false, true})
      for (bool first : {false, true})
        for (const auto spelling :
             {"b", "h", "1", "-1", "0x100000001", "0f7f817c01"}) {
          const std::string_view word(spelling);
          if (bf16 && word == "h")
            continue;
          const std::string source =
              ".version 9.3\n.target sm_90\n.entry kernel(){.reg .pred p,q,r; "
              ".reg .b32 b; .reg .f16x2 h;\nsetp.eq" +
              std::string(boolean ? ".and" : "") +
              (bf16 ? ".bf16x2 p|q," : ".f16x2 p|q,") +
              (first ? std::string(word) : "b") + "," +
              (first ? "b" : std::string(word)) + (boolean ? ",r;" : ";") +
              "\n}";
          std::optional<ir::ResolvedModule> owner;
          {
            ptx_frontend::PtxSyntaxParser parser(source);
            const auto ast = parser.parseModule();
            if (!ast || !ast.diagnostics.empty())
              return false;
            auto produced = ir::resolveAndValidateModule(*ast);
            if (!produced || !ir::validateModule(*ast, *produced))
              return false;
            owner.emplace(std::move(*produced));
          }
          if (owner->functions.size() != 1 ||
              owner->functions.front().body.size() != 1 ||
              owner->functions.front().instruction_ranges.size() != 1)
            return false;
          auto copied = *owner;
          ir::ResolvedModule assigned;
          assigned = copied;
          auto function = copied.functions.front();
          assigned.functions.front() = function;
          assigned.functions.front().body.front() =
              function.body.front()->clone();
          for (auto* current : {&copied, &assigned}) {
            auto& instruction = *current->functions.front().body.front();
            auto& member = installedPackedSetpField(instruction, first);
            ir::packed_setp_test_wrap::Capture capture;
            capture.member = &member;
            capture.field = first ? "src1" : "src2";
            capture.logical =
                bf16 ? ir::ScalarType::BF16x2 : ir::ScalarType::F16x2;
            capture.range =
                ir::packed_setp_test_wrap::token_range(source, first);
            capture.immediate = word != "b" && word != "h";
            capture.type =
                word == "h" ? ir::ScalarType::F16x2 : ir::ScalarType::B32;
            capture.spelling = capture.immediate ? std::string_view{} : word;
            if (word == "1") {
              capture.bits = 1;
              capture.source_bits = 1;
            }
            if (word == "-1") {
              capture.bits = 0xffffffff;
              capture.source_bits = UINT64_MAX;
              capture.negative = true;
            }
            if (word == "0x100000001") {
              capture.bits = 1;
              capture.source_bits = 0x100000001;
            }
            if (word == "0f7f817c01") {
              capture.type = capture.logical;
              capture.bits = 0x7f817c01;
            }
            ir::checker::CheckResult checked;
            {
              ir::packed_setp_test_wrap::Scope observe(capture);
              checked = instruction.check(
                  {.target = {.ptx_version = {9, 3}, .sm_version = 90},
                   .instruction_range =
                       current->functions.front().instruction_ranges.front()});
            }
            if (!checked || !capture.valid || capture.calls != 1 ||
                !ir::validateModule(*current))
              return false;
          }
          auto operation = source.substr(source.find("setp.eq"));
          operation.erase(operation.rfind(';') + 1);
          ptx_frontend::PtxSyntaxParser single_parser(operation);
          const auto single = single_parser.parseInstruction();
          if (!single || !single.diagnostics.empty())
            return false;
          auto& instruction = *owner->functions.front().body.front();
          const auto* sibling = std::get_if<ir::ResolvedRegisterRef>(
              &installedPackedSetpField(instruction, !first).value);
          if (!sibling || !sibling->symbol_id)
            return false;
          const auto& function_owner = owner->functions.front();
          const ir::ResolveContext context{
              .symbols = owner->symbols,
              .scope = owner->symbols.symbol(*sibling->symbol_id).scope,
              .function_scope =
                  owner->symbols.symbol(function_owner.symbol_id).owned_scope,
              .function_is_entry = true};
          auto factory = ir::resolveInstruction(*single, context);
          if (!factory ||
              (*factory)->instruction_kind() != instruction.instruction_kind())
            return false;
          auto clone = (*factory)->clone();
          if (!clone ||
              clone->instruction_kind() != instruction.instruction_kind())
            return false;
          const auto& actual = installedPackedSetpField(*clone, first).value;
          if ((word == "b" || word == "h") !=
              std::holds_alternative<ir::ResolvedRegisterRef>(actual))
            return false;
        }
  return true;
}
#endif

/** One finite installed source branch, retaining exact class/type/arity/layout facts. */
struct InstalledAtomicValueCase {
  /** Stable fixture identity used for descriptive failure output. */
  std::string_view id;
  /** Exact existing public final class. */
  ptx_frontend::resolved_ir::InstructionKind kind;
  /** Presence of the unchanged register-vector destination. */
  bool atom;
  /** Presence of the unchanged B64 cache-policy register. */
  bool hinted;
  /** Required scalar lane count. */
  unsigned arity;
  /** Logical immediate conversion/use type. */
  ptx_frontend::resolved_ir::ScalarType logical;
  /** Existing physical bit declaration used by both register control roles. */
  std::string_view physical;
  /** Exact already-supported modifier/control prefix. */
  std::string_view operation;
  /** Accepted common numeric or bare packed-word producer. */
  std::string_view literal;
  /** Existing converter golden or exact raw packed word. */
  uint64_t bits;
};

/** Borrow the actual installed member from its live exact-class instruction. */
const ptx_frontend::WithLocs<ptx_frontend::resolved_ir::ResolvedValueVector>&
installedAtomicValueSource(
    const ptx_frontend::resolved_ir::Instruction& instruction) {
  namespace ir = ptx_frontend::resolved_ir;
  if (const auto* form =
          dynamic_cast<const ir::AtomVectorAddNoftzF16*>(&instruction))
    return form->src;
  if (const auto* form =
          dynamic_cast<const ir::AtomVectorMinNoftzF16*>(&instruction))
    return form->src;
  if (const auto* form =
          dynamic_cast<const ir::AtomVectorMaxNoftzF16*>(&instruction))
    return form->src;
  if (const auto* form =
          dynamic_cast<const ir::AtomVectorAddNoftzBf16*>(&instruction))
    return form->src;
  if (const auto* form =
          dynamic_cast<const ir::AtomVectorMinNoftzBf16*>(&instruction))
    return form->src;
  if (const auto* form =
          dynamic_cast<const ir::AtomVectorMaxNoftzBf16*>(&instruction))
    return form->src;
  if (const auto* form =
          dynamic_cast<const ir::AtomVectorAddNoftzF16x2*>(&instruction))
    return form->src;
  if (const auto* form =
          dynamic_cast<const ir::AtomVectorMinNoftzF16x2*>(&instruction))
    return form->src;
  if (const auto* form =
          dynamic_cast<const ir::AtomVectorMaxNoftzF16x2*>(&instruction))
    return form->src;
  if (const auto* form =
          dynamic_cast<const ir::AtomVectorAddNoftzBf16x2*>(&instruction))
    return form->src;
  if (const auto* form =
          dynamic_cast<const ir::AtomVectorMinNoftzBf16x2*>(&instruction))
    return form->src;
  if (const auto* form =
          dynamic_cast<const ir::AtomVectorMaxNoftzBf16x2*>(&instruction))
    return form->src;
  if (const auto* form =
          dynamic_cast<const ir::AtomVectorAddF32*>(&instruction))
    return form->src;
  if (const auto* form =
          dynamic_cast<const ir::RedVectorAddNoftzF16*>(&instruction))
    return form->src;
  if (const auto* form =
          dynamic_cast<const ir::RedVectorMinNoftzF16*>(&instruction))
    return form->src;
  if (const auto* form =
          dynamic_cast<const ir::RedVectorMaxNoftzF16*>(&instruction))
    return form->src;
  if (const auto* form =
          dynamic_cast<const ir::RedVectorAddNoftzBf16*>(&instruction))
    return form->src;
  if (const auto* form =
          dynamic_cast<const ir::RedVectorMinNoftzBf16*>(&instruction))
    return form->src;
  if (const auto* form =
          dynamic_cast<const ir::RedVectorMaxNoftzBf16*>(&instruction))
    return form->src;
  if (const auto* form =
          dynamic_cast<const ir::RedVectorAddNoftzF16x2*>(&instruction))
    return form->src;
  if (const auto* form =
          dynamic_cast<const ir::RedVectorMinNoftzF16x2*>(&instruction))
    return form->src;
  if (const auto* form =
          dynamic_cast<const ir::RedVectorMaxNoftzF16x2*>(&instruction))
    return form->src;
  if (const auto* form =
          dynamic_cast<const ir::RedVectorAddNoftzBf16x2*>(&instruction))
    return form->src;
  if (const auto* form =
          dynamic_cast<const ir::RedVectorMinNoftzBf16x2*>(&instruction))
    return form->src;
  if (const auto* form =
          dynamic_cast<const ir::RedVectorMaxNoftzBf16x2*>(&instruction))
    return form->src;
  if (const auto* form = dynamic_cast<const ir::RedVectorAddF32*>(&instruction))
    return form->src;
  throw std::logic_error("Expected an installed vector atomic source class.");
}

/** Build a source brace fixture without changing arity or making a whole-vector grant. */
std::string installedAtomicValuePack(unsigned arity, std::string_view value,
                                     bool registers) {
  std::string result = "{";
  for (unsigned index = 0; index < arity; ++index)
    result += (index ? "," : "") + std::string(value) +
              (registers ? std::to_string(index) : "");
  return result + "}";
}

/** Inspect actual installed values while the source owner outlives each synchronous callback. */
bool checkInstalledAtomicValueVectors() {
  namespace ir = ptx_frontend::resolved_ir;
  static_assert(std::same_as<decltype(ir::AtomVectorAddNoftzF16{}.src),
                             ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
                std::default_initializable<ir::AtomVectorAddNoftzF16> &&
                std::copy_constructible<ir::AtomVectorAddNoftzF16> &&
                std::is_copy_assignable_v<ir::AtomVectorAddNoftzF16> &&
                std::derived_from<ir::AtomVectorAddNoftzF16, ir::Instruction>);
  static_assert(std::same_as<decltype(ir::AtomVectorMinNoftzF16{}.src),
                             ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
                std::default_initializable<ir::AtomVectorMinNoftzF16> &&
                std::copy_constructible<ir::AtomVectorMinNoftzF16> &&
                std::is_copy_assignable_v<ir::AtomVectorMinNoftzF16> &&
                std::derived_from<ir::AtomVectorMinNoftzF16, ir::Instruction>);
  static_assert(std::same_as<decltype(ir::AtomVectorMaxNoftzF16{}.src),
                             ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
                std::default_initializable<ir::AtomVectorMaxNoftzF16> &&
                std::copy_constructible<ir::AtomVectorMaxNoftzF16> &&
                std::is_copy_assignable_v<ir::AtomVectorMaxNoftzF16> &&
                std::derived_from<ir::AtomVectorMaxNoftzF16, ir::Instruction>);
  static_assert(std::same_as<decltype(ir::AtomVectorAddNoftzBf16{}.src),
                             ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
                std::default_initializable<ir::AtomVectorAddNoftzBf16> &&
                std::copy_constructible<ir::AtomVectorAddNoftzBf16> &&
                std::is_copy_assignable_v<ir::AtomVectorAddNoftzBf16> &&
                std::derived_from<ir::AtomVectorAddNoftzBf16, ir::Instruction>);
  static_assert(std::same_as<decltype(ir::AtomVectorMinNoftzBf16{}.src),
                             ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
                std::default_initializable<ir::AtomVectorMinNoftzBf16> &&
                std::copy_constructible<ir::AtomVectorMinNoftzBf16> &&
                std::is_copy_assignable_v<ir::AtomVectorMinNoftzBf16> &&
                std::derived_from<ir::AtomVectorMinNoftzBf16, ir::Instruction>);
  static_assert(std::same_as<decltype(ir::AtomVectorMaxNoftzBf16{}.src),
                             ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
                std::default_initializable<ir::AtomVectorMaxNoftzBf16> &&
                std::copy_constructible<ir::AtomVectorMaxNoftzBf16> &&
                std::is_copy_assignable_v<ir::AtomVectorMaxNoftzBf16> &&
                std::derived_from<ir::AtomVectorMaxNoftzBf16, ir::Instruction>);
  static_assert(
      std::same_as<decltype(ir::AtomVectorAddNoftzF16x2{}.src),
                   ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
      std::default_initializable<ir::AtomVectorAddNoftzF16x2> &&
      std::copy_constructible<ir::AtomVectorAddNoftzF16x2> &&
      std::is_copy_assignable_v<ir::AtomVectorAddNoftzF16x2> &&
      std::derived_from<ir::AtomVectorAddNoftzF16x2, ir::Instruction>);
  static_assert(
      std::same_as<decltype(ir::AtomVectorMinNoftzF16x2{}.src),
                   ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
      std::default_initializable<ir::AtomVectorMinNoftzF16x2> &&
      std::copy_constructible<ir::AtomVectorMinNoftzF16x2> &&
      std::is_copy_assignable_v<ir::AtomVectorMinNoftzF16x2> &&
      std::derived_from<ir::AtomVectorMinNoftzF16x2, ir::Instruction>);
  static_assert(
      std::same_as<decltype(ir::AtomVectorMaxNoftzF16x2{}.src),
                   ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
      std::default_initializable<ir::AtomVectorMaxNoftzF16x2> &&
      std::copy_constructible<ir::AtomVectorMaxNoftzF16x2> &&
      std::is_copy_assignable_v<ir::AtomVectorMaxNoftzF16x2> &&
      std::derived_from<ir::AtomVectorMaxNoftzF16x2, ir::Instruction>);
  static_assert(
      std::same_as<decltype(ir::AtomVectorAddNoftzBf16x2{}.src),
                   ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
      std::default_initializable<ir::AtomVectorAddNoftzBf16x2> &&
      std::copy_constructible<ir::AtomVectorAddNoftzBf16x2> &&
      std::is_copy_assignable_v<ir::AtomVectorAddNoftzBf16x2> &&
      std::derived_from<ir::AtomVectorAddNoftzBf16x2, ir::Instruction>);
  static_assert(
      std::same_as<decltype(ir::AtomVectorMinNoftzBf16x2{}.src),
                   ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
      std::default_initializable<ir::AtomVectorMinNoftzBf16x2> &&
      std::copy_constructible<ir::AtomVectorMinNoftzBf16x2> &&
      std::is_copy_assignable_v<ir::AtomVectorMinNoftzBf16x2> &&
      std::derived_from<ir::AtomVectorMinNoftzBf16x2, ir::Instruction>);
  static_assert(
      std::same_as<decltype(ir::AtomVectorMaxNoftzBf16x2{}.src),
                   ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
      std::default_initializable<ir::AtomVectorMaxNoftzBf16x2> &&
      std::copy_constructible<ir::AtomVectorMaxNoftzBf16x2> &&
      std::is_copy_assignable_v<ir::AtomVectorMaxNoftzBf16x2> &&
      std::derived_from<ir::AtomVectorMaxNoftzBf16x2, ir::Instruction>);
  static_assert(std::same_as<decltype(ir::AtomVectorAddF32{}.src),
                             ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
                std::default_initializable<ir::AtomVectorAddF32> &&
                std::copy_constructible<ir::AtomVectorAddF32> &&
                std::is_copy_assignable_v<ir::AtomVectorAddF32> &&
                std::derived_from<ir::AtomVectorAddF32, ir::Instruction>);
  static_assert(std::same_as<decltype(ir::RedVectorAddNoftzF16{}.src),
                             ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
                std::default_initializable<ir::RedVectorAddNoftzF16> &&
                std::copy_constructible<ir::RedVectorAddNoftzF16> &&
                std::is_copy_assignable_v<ir::RedVectorAddNoftzF16> &&
                std::derived_from<ir::RedVectorAddNoftzF16, ir::Instruction>);
  static_assert(std::same_as<decltype(ir::RedVectorMinNoftzF16{}.src),
                             ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
                std::default_initializable<ir::RedVectorMinNoftzF16> &&
                std::copy_constructible<ir::RedVectorMinNoftzF16> &&
                std::is_copy_assignable_v<ir::RedVectorMinNoftzF16> &&
                std::derived_from<ir::RedVectorMinNoftzF16, ir::Instruction>);
  static_assert(std::same_as<decltype(ir::RedVectorMaxNoftzF16{}.src),
                             ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
                std::default_initializable<ir::RedVectorMaxNoftzF16> &&
                std::copy_constructible<ir::RedVectorMaxNoftzF16> &&
                std::is_copy_assignable_v<ir::RedVectorMaxNoftzF16> &&
                std::derived_from<ir::RedVectorMaxNoftzF16, ir::Instruction>);
  static_assert(std::same_as<decltype(ir::RedVectorAddNoftzBf16{}.src),
                             ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
                std::default_initializable<ir::RedVectorAddNoftzBf16> &&
                std::copy_constructible<ir::RedVectorAddNoftzBf16> &&
                std::is_copy_assignable_v<ir::RedVectorAddNoftzBf16> &&
                std::derived_from<ir::RedVectorAddNoftzBf16, ir::Instruction>);
  static_assert(std::same_as<decltype(ir::RedVectorMinNoftzBf16{}.src),
                             ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
                std::default_initializable<ir::RedVectorMinNoftzBf16> &&
                std::copy_constructible<ir::RedVectorMinNoftzBf16> &&
                std::is_copy_assignable_v<ir::RedVectorMinNoftzBf16> &&
                std::derived_from<ir::RedVectorMinNoftzBf16, ir::Instruction>);
  static_assert(std::same_as<decltype(ir::RedVectorMaxNoftzBf16{}.src),
                             ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
                std::default_initializable<ir::RedVectorMaxNoftzBf16> &&
                std::copy_constructible<ir::RedVectorMaxNoftzBf16> &&
                std::is_copy_assignable_v<ir::RedVectorMaxNoftzBf16> &&
                std::derived_from<ir::RedVectorMaxNoftzBf16, ir::Instruction>);
  static_assert(std::same_as<decltype(ir::RedVectorAddNoftzF16x2{}.src),
                             ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
                std::default_initializable<ir::RedVectorAddNoftzF16x2> &&
                std::copy_constructible<ir::RedVectorAddNoftzF16x2> &&
                std::is_copy_assignable_v<ir::RedVectorAddNoftzF16x2> &&
                std::derived_from<ir::RedVectorAddNoftzF16x2, ir::Instruction>);
  static_assert(std::same_as<decltype(ir::RedVectorMinNoftzF16x2{}.src),
                             ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
                std::default_initializable<ir::RedVectorMinNoftzF16x2> &&
                std::copy_constructible<ir::RedVectorMinNoftzF16x2> &&
                std::is_copy_assignable_v<ir::RedVectorMinNoftzF16x2> &&
                std::derived_from<ir::RedVectorMinNoftzF16x2, ir::Instruction>);
  static_assert(std::same_as<decltype(ir::RedVectorMaxNoftzF16x2{}.src),
                             ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
                std::default_initializable<ir::RedVectorMaxNoftzF16x2> &&
                std::copy_constructible<ir::RedVectorMaxNoftzF16x2> &&
                std::is_copy_assignable_v<ir::RedVectorMaxNoftzF16x2> &&
                std::derived_from<ir::RedVectorMaxNoftzF16x2, ir::Instruction>);
  static_assert(
      std::same_as<decltype(ir::RedVectorAddNoftzBf16x2{}.src),
                   ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
      std::default_initializable<ir::RedVectorAddNoftzBf16x2> &&
      std::copy_constructible<ir::RedVectorAddNoftzBf16x2> &&
      std::is_copy_assignable_v<ir::RedVectorAddNoftzBf16x2> &&
      std::derived_from<ir::RedVectorAddNoftzBf16x2, ir::Instruction>);
  static_assert(
      std::same_as<decltype(ir::RedVectorMinNoftzBf16x2{}.src),
                   ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
      std::default_initializable<ir::RedVectorMinNoftzBf16x2> &&
      std::copy_constructible<ir::RedVectorMinNoftzBf16x2> &&
      std::is_copy_assignable_v<ir::RedVectorMinNoftzBf16x2> &&
      std::derived_from<ir::RedVectorMinNoftzBf16x2, ir::Instruction>);
  static_assert(
      std::same_as<decltype(ir::RedVectorMaxNoftzBf16x2{}.src),
                   ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
      std::default_initializable<ir::RedVectorMaxNoftzBf16x2> &&
      std::copy_constructible<ir::RedVectorMaxNoftzBf16x2> &&
      std::is_copy_assignable_v<ir::RedVectorMaxNoftzBf16x2> &&
      std::derived_from<ir::RedVectorMaxNoftzBf16x2, ir::Instruction>);
  static_assert(std::same_as<decltype(ir::RedVectorAddF32{}.src),
                             ptx_frontend::WithLocs<ir::ResolvedValueVector>> &&
                std::default_initializable<ir::RedVectorAddF32> &&
                std::copy_constructible<ir::RedVectorAddF32> &&
                std::is_copy_assignable_v<ir::RedVectorAddF32> &&
                std::derived_from<ir::RedVectorAddF32, ir::Instruction>);
  /** Exact finite class/type/arity/layout fixtures, not a program quota. */
  constexpr InstalledAtomicValueCase cases[] = {
      {"atom/atom_vector_add_noftz_f16/no_hint/src/f16/v2",
       ir::AtomVectorAddNoftzF16::kind, true, false, 2, ir::ScalarType::F16,
       "b16", "atom.global.add.noftz.v2.f16", "1.5", 0x3e00ULL},
      {"atom/atom_vector_add_noftz_f16/no_hint/src/f16/v4",
       ir::AtomVectorAddNoftzF16::kind, true, false, 4, ir::ScalarType::F16,
       "b16", "atom.global.add.noftz.v4.f16", "1.5", 0x3e00ULL},
      {"atom/atom_vector_add_noftz_f16/no_hint/src/f16/v8",
       ir::AtomVectorAddNoftzF16::kind, true, false, 8, ir::ScalarType::F16,
       "b16", "atom.global.add.noftz.v8.f16", "1.5", 0x3e00ULL},
      {"atom/atom_vector_add_noftz_f16/with_policy/src/f16/v2",
       ir::AtomVectorAddNoftzF16::kind, true, true, 2, ir::ScalarType::F16,
       "b16", "atom.global.add.noftz.L2::cache_hint.v2.f16", "1.5", 0x3e00ULL},
      {"atom/atom_vector_add_noftz_f16/with_policy/src/f16/v4",
       ir::AtomVectorAddNoftzF16::kind, true, true, 4, ir::ScalarType::F16,
       "b16", "atom.global.add.noftz.L2::cache_hint.v4.f16", "1.5", 0x3e00ULL},
      {"atom/atom_vector_add_noftz_f16/with_policy/src/f16/v8",
       ir::AtomVectorAddNoftzF16::kind, true, true, 8, ir::ScalarType::F16,
       "b16", "atom.global.add.noftz.L2::cache_hint.v8.f16", "1.5", 0x3e00ULL},
      {"atom/atom_vector_min_noftz_f16/no_hint/src/f16/v2",
       ir::AtomVectorMinNoftzF16::kind, true, false, 2, ir::ScalarType::F16,
       "b16", "atom.global.min.noftz.v2.f16", "1.5", 0x3e00ULL},
      {"atom/atom_vector_min_noftz_f16/no_hint/src/f16/v4",
       ir::AtomVectorMinNoftzF16::kind, true, false, 4, ir::ScalarType::F16,
       "b16", "atom.global.min.noftz.v4.f16", "1.5", 0x3e00ULL},
      {"atom/atom_vector_min_noftz_f16/no_hint/src/f16/v8",
       ir::AtomVectorMinNoftzF16::kind, true, false, 8, ir::ScalarType::F16,
       "b16", "atom.global.min.noftz.v8.f16", "1.5", 0x3e00ULL},
      {"atom/atom_vector_min_noftz_f16/with_policy/src/f16/v2",
       ir::AtomVectorMinNoftzF16::kind, true, true, 2, ir::ScalarType::F16,
       "b16", "atom.global.min.noftz.L2::cache_hint.v2.f16", "1.5", 0x3e00ULL},
      {"atom/atom_vector_min_noftz_f16/with_policy/src/f16/v4",
       ir::AtomVectorMinNoftzF16::kind, true, true, 4, ir::ScalarType::F16,
       "b16", "atom.global.min.noftz.L2::cache_hint.v4.f16", "1.5", 0x3e00ULL},
      {"atom/atom_vector_min_noftz_f16/with_policy/src/f16/v8",
       ir::AtomVectorMinNoftzF16::kind, true, true, 8, ir::ScalarType::F16,
       "b16", "atom.global.min.noftz.L2::cache_hint.v8.f16", "1.5", 0x3e00ULL},
      {"atom/atom_vector_max_noftz_f16/no_hint/src/f16/v2",
       ir::AtomVectorMaxNoftzF16::kind, true, false, 2, ir::ScalarType::F16,
       "b16", "atom.global.max.noftz.v2.f16", "1.5", 0x3e00ULL},
      {"atom/atom_vector_max_noftz_f16/no_hint/src/f16/v4",
       ir::AtomVectorMaxNoftzF16::kind, true, false, 4, ir::ScalarType::F16,
       "b16", "atom.global.max.noftz.v4.f16", "1.5", 0x3e00ULL},
      {"atom/atom_vector_max_noftz_f16/no_hint/src/f16/v8",
       ir::AtomVectorMaxNoftzF16::kind, true, false, 8, ir::ScalarType::F16,
       "b16", "atom.global.max.noftz.v8.f16", "1.5", 0x3e00ULL},
      {"atom/atom_vector_max_noftz_f16/with_policy/src/f16/v2",
       ir::AtomVectorMaxNoftzF16::kind, true, true, 2, ir::ScalarType::F16,
       "b16", "atom.global.max.noftz.L2::cache_hint.v2.f16", "1.5", 0x3e00ULL},
      {"atom/atom_vector_max_noftz_f16/with_policy/src/f16/v4",
       ir::AtomVectorMaxNoftzF16::kind, true, true, 4, ir::ScalarType::F16,
       "b16", "atom.global.max.noftz.L2::cache_hint.v4.f16", "1.5", 0x3e00ULL},
      {"atom/atom_vector_max_noftz_f16/with_policy/src/f16/v8",
       ir::AtomVectorMaxNoftzF16::kind, true, true, 8, ir::ScalarType::F16,
       "b16", "atom.global.max.noftz.L2::cache_hint.v8.f16", "1.5", 0x3e00ULL},
      {"atom/atom_vector_add_noftz_bf16/no_hint/src/bf16/v2",
       ir::AtomVectorAddNoftzBf16::kind, true, false, 2, ir::ScalarType::BF16,
       "b16", "atom.global.add.noftz.v2.bf16", "1.5", 0x3fc0ULL},
      {"atom/atom_vector_add_noftz_bf16/no_hint/src/bf16/v4",
       ir::AtomVectorAddNoftzBf16::kind, true, false, 4, ir::ScalarType::BF16,
       "b16", "atom.global.add.noftz.v4.bf16", "1.5", 0x3fc0ULL},
      {"atom/atom_vector_add_noftz_bf16/no_hint/src/bf16/v8",
       ir::AtomVectorAddNoftzBf16::kind, true, false, 8, ir::ScalarType::BF16,
       "b16", "atom.global.add.noftz.v8.bf16", "1.5", 0x3fc0ULL},
      {"atom/atom_vector_add_noftz_bf16/with_policy/src/bf16/v2",
       ir::AtomVectorAddNoftzBf16::kind, true, true, 2, ir::ScalarType::BF16,
       "b16", "atom.global.add.noftz.L2::cache_hint.v2.bf16", "1.5", 0x3fc0ULL},
      {"atom/atom_vector_add_noftz_bf16/with_policy/src/bf16/v4",
       ir::AtomVectorAddNoftzBf16::kind, true, true, 4, ir::ScalarType::BF16,
       "b16", "atom.global.add.noftz.L2::cache_hint.v4.bf16", "1.5", 0x3fc0ULL},
      {"atom/atom_vector_add_noftz_bf16/with_policy/src/bf16/v8",
       ir::AtomVectorAddNoftzBf16::kind, true, true, 8, ir::ScalarType::BF16,
       "b16", "atom.global.add.noftz.L2::cache_hint.v8.bf16", "1.5", 0x3fc0ULL},
      {"atom/atom_vector_min_noftz_bf16/no_hint/src/bf16/v2",
       ir::AtomVectorMinNoftzBf16::kind, true, false, 2, ir::ScalarType::BF16,
       "b16", "atom.global.min.noftz.v2.bf16", "1.5", 0x3fc0ULL},
      {"atom/atom_vector_min_noftz_bf16/no_hint/src/bf16/v4",
       ir::AtomVectorMinNoftzBf16::kind, true, false, 4, ir::ScalarType::BF16,
       "b16", "atom.global.min.noftz.v4.bf16", "1.5", 0x3fc0ULL},
      {"atom/atom_vector_min_noftz_bf16/no_hint/src/bf16/v8",
       ir::AtomVectorMinNoftzBf16::kind, true, false, 8, ir::ScalarType::BF16,
       "b16", "atom.global.min.noftz.v8.bf16", "1.5", 0x3fc0ULL},
      {"atom/atom_vector_min_noftz_bf16/with_policy/src/bf16/v2",
       ir::AtomVectorMinNoftzBf16::kind, true, true, 2, ir::ScalarType::BF16,
       "b16", "atom.global.min.noftz.L2::cache_hint.v2.bf16", "1.5", 0x3fc0ULL},
      {"atom/atom_vector_min_noftz_bf16/with_policy/src/bf16/v4",
       ir::AtomVectorMinNoftzBf16::kind, true, true, 4, ir::ScalarType::BF16,
       "b16", "atom.global.min.noftz.L2::cache_hint.v4.bf16", "1.5", 0x3fc0ULL},
      {"atom/atom_vector_min_noftz_bf16/with_policy/src/bf16/v8",
       ir::AtomVectorMinNoftzBf16::kind, true, true, 8, ir::ScalarType::BF16,
       "b16", "atom.global.min.noftz.L2::cache_hint.v8.bf16", "1.5", 0x3fc0ULL},
      {"atom/atom_vector_max_noftz_bf16/no_hint/src/bf16/v2",
       ir::AtomVectorMaxNoftzBf16::kind, true, false, 2, ir::ScalarType::BF16,
       "b16", "atom.global.max.noftz.v2.bf16", "1.5", 0x3fc0ULL},
      {"atom/atom_vector_max_noftz_bf16/no_hint/src/bf16/v4",
       ir::AtomVectorMaxNoftzBf16::kind, true, false, 4, ir::ScalarType::BF16,
       "b16", "atom.global.max.noftz.v4.bf16", "1.5", 0x3fc0ULL},
      {"atom/atom_vector_max_noftz_bf16/no_hint/src/bf16/v8",
       ir::AtomVectorMaxNoftzBf16::kind, true, false, 8, ir::ScalarType::BF16,
       "b16", "atom.global.max.noftz.v8.bf16", "1.5", 0x3fc0ULL},
      {"atom/atom_vector_max_noftz_bf16/with_policy/src/bf16/v2",
       ir::AtomVectorMaxNoftzBf16::kind, true, true, 2, ir::ScalarType::BF16,
       "b16", "atom.global.max.noftz.L2::cache_hint.v2.bf16", "1.5", 0x3fc0ULL},
      {"atom/atom_vector_max_noftz_bf16/with_policy/src/bf16/v4",
       ir::AtomVectorMaxNoftzBf16::kind, true, true, 4, ir::ScalarType::BF16,
       "b16", "atom.global.max.noftz.L2::cache_hint.v4.bf16", "1.5", 0x3fc0ULL},
      {"atom/atom_vector_max_noftz_bf16/with_policy/src/bf16/v8",
       ir::AtomVectorMaxNoftzBf16::kind, true, true, 8, ir::ScalarType::BF16,
       "b16", "atom.global.max.noftz.L2::cache_hint.v8.bf16", "1.5", 0x3fc0ULL},
      {"atom/atom_vector_add_noftz_f16x2/no_hint/src/f16x2/v2",
       ir::AtomVectorAddNoftzF16x2::kind, true, false, 2, ir::ScalarType::F16x2,
       "b32", "atom.global.add.noftz.v2.f16x2", "0f3e003e00", 0x3e003e00ULL},
      {"atom/atom_vector_add_noftz_f16x2/no_hint/src/f16x2/v4",
       ir::AtomVectorAddNoftzF16x2::kind, true, false, 4, ir::ScalarType::F16x2,
       "b32", "atom.global.add.noftz.v4.f16x2", "0f3e003e00", 0x3e003e00ULL},
      {"atom/atom_vector_add_noftz_f16x2/with_policy/src/f16x2/v2",
       ir::AtomVectorAddNoftzF16x2::kind, true, true, 2, ir::ScalarType::F16x2,
       "b32", "atom.global.add.noftz.L2::cache_hint.v2.f16x2", "0f3e003e00",
       0x3e003e00ULL},
      {"atom/atom_vector_add_noftz_f16x2/with_policy/src/f16x2/v4",
       ir::AtomVectorAddNoftzF16x2::kind, true, true, 4, ir::ScalarType::F16x2,
       "b32", "atom.global.add.noftz.L2::cache_hint.v4.f16x2", "0f3e003e00",
       0x3e003e00ULL},
      {"atom/atom_vector_min_noftz_f16x2/no_hint/src/f16x2/v2",
       ir::AtomVectorMinNoftzF16x2::kind, true, false, 2, ir::ScalarType::F16x2,
       "b32", "atom.global.min.noftz.v2.f16x2", "0f3e003e00", 0x3e003e00ULL},
      {"atom/atom_vector_min_noftz_f16x2/no_hint/src/f16x2/v4",
       ir::AtomVectorMinNoftzF16x2::kind, true, false, 4, ir::ScalarType::F16x2,
       "b32", "atom.global.min.noftz.v4.f16x2", "0f3e003e00", 0x3e003e00ULL},
      {"atom/atom_vector_min_noftz_f16x2/with_policy/src/f16x2/v2",
       ir::AtomVectorMinNoftzF16x2::kind, true, true, 2, ir::ScalarType::F16x2,
       "b32", "atom.global.min.noftz.L2::cache_hint.v2.f16x2", "0f3e003e00",
       0x3e003e00ULL},
      {"atom/atom_vector_min_noftz_f16x2/with_policy/src/f16x2/v4",
       ir::AtomVectorMinNoftzF16x2::kind, true, true, 4, ir::ScalarType::F16x2,
       "b32", "atom.global.min.noftz.L2::cache_hint.v4.f16x2", "0f3e003e00",
       0x3e003e00ULL},
      {"atom/atom_vector_max_noftz_f16x2/no_hint/src/f16x2/v2",
       ir::AtomVectorMaxNoftzF16x2::kind, true, false, 2, ir::ScalarType::F16x2,
       "b32", "atom.global.max.noftz.v2.f16x2", "0f3e003e00", 0x3e003e00ULL},
      {"atom/atom_vector_max_noftz_f16x2/no_hint/src/f16x2/v4",
       ir::AtomVectorMaxNoftzF16x2::kind, true, false, 4, ir::ScalarType::F16x2,
       "b32", "atom.global.max.noftz.v4.f16x2", "0f3e003e00", 0x3e003e00ULL},
      {"atom/atom_vector_max_noftz_f16x2/with_policy/src/f16x2/v2",
       ir::AtomVectorMaxNoftzF16x2::kind, true, true, 2, ir::ScalarType::F16x2,
       "b32", "atom.global.max.noftz.L2::cache_hint.v2.f16x2", "0f3e003e00",
       0x3e003e00ULL},
      {"atom/atom_vector_max_noftz_f16x2/with_policy/src/f16x2/v4",
       ir::AtomVectorMaxNoftzF16x2::kind, true, true, 4, ir::ScalarType::F16x2,
       "b32", "atom.global.max.noftz.L2::cache_hint.v4.f16x2", "0f3e003e00",
       0x3e003e00ULL},
      {"atom/atom_vector_add_noftz_bf16x2/no_hint/src/bf16x2/v2",
       ir::AtomVectorAddNoftzBf16x2::kind, true, false, 2,
       ir::ScalarType::BF16x2, "b32", "atom.global.add.noftz.v2.bf16x2",
       "0f3fc03fc0", 0x3fc03fc0ULL},
      {"atom/atom_vector_add_noftz_bf16x2/no_hint/src/bf16x2/v4",
       ir::AtomVectorAddNoftzBf16x2::kind, true, false, 4,
       ir::ScalarType::BF16x2, "b32", "atom.global.add.noftz.v4.bf16x2",
       "0f3fc03fc0", 0x3fc03fc0ULL},
      {"atom/atom_vector_add_noftz_bf16x2/with_policy/src/bf16x2/v2",
       ir::AtomVectorAddNoftzBf16x2::kind, true, true, 2,
       ir::ScalarType::BF16x2, "b32",
       "atom.global.add.noftz.L2::cache_hint.v2.bf16x2", "0f3fc03fc0",
       0x3fc03fc0ULL},
      {"atom/atom_vector_add_noftz_bf16x2/with_policy/src/bf16x2/v4",
       ir::AtomVectorAddNoftzBf16x2::kind, true, true, 4,
       ir::ScalarType::BF16x2, "b32",
       "atom.global.add.noftz.L2::cache_hint.v4.bf16x2", "0f3fc03fc0",
       0x3fc03fc0ULL},
      {"atom/atom_vector_min_noftz_bf16x2/no_hint/src/bf16x2/v2",
       ir::AtomVectorMinNoftzBf16x2::kind, true, false, 2,
       ir::ScalarType::BF16x2, "b32", "atom.global.min.noftz.v2.bf16x2",
       "0f3fc03fc0", 0x3fc03fc0ULL},
      {"atom/atom_vector_min_noftz_bf16x2/no_hint/src/bf16x2/v4",
       ir::AtomVectorMinNoftzBf16x2::kind, true, false, 4,
       ir::ScalarType::BF16x2, "b32", "atom.global.min.noftz.v4.bf16x2",
       "0f3fc03fc0", 0x3fc03fc0ULL},
      {"atom/atom_vector_min_noftz_bf16x2/with_policy/src/bf16x2/v2",
       ir::AtomVectorMinNoftzBf16x2::kind, true, true, 2,
       ir::ScalarType::BF16x2, "b32",
       "atom.global.min.noftz.L2::cache_hint.v2.bf16x2", "0f3fc03fc0",
       0x3fc03fc0ULL},
      {"atom/atom_vector_min_noftz_bf16x2/with_policy/src/bf16x2/v4",
       ir::AtomVectorMinNoftzBf16x2::kind, true, true, 4,
       ir::ScalarType::BF16x2, "b32",
       "atom.global.min.noftz.L2::cache_hint.v4.bf16x2", "0f3fc03fc0",
       0x3fc03fc0ULL},
      {"atom/atom_vector_max_noftz_bf16x2/no_hint/src/bf16x2/v2",
       ir::AtomVectorMaxNoftzBf16x2::kind, true, false, 2,
       ir::ScalarType::BF16x2, "b32", "atom.global.max.noftz.v2.bf16x2",
       "0f3fc03fc0", 0x3fc03fc0ULL},
      {"atom/atom_vector_max_noftz_bf16x2/no_hint/src/bf16x2/v4",
       ir::AtomVectorMaxNoftzBf16x2::kind, true, false, 4,
       ir::ScalarType::BF16x2, "b32", "atom.global.max.noftz.v4.bf16x2",
       "0f3fc03fc0", 0x3fc03fc0ULL},
      {"atom/atom_vector_max_noftz_bf16x2/with_policy/src/bf16x2/v2",
       ir::AtomVectorMaxNoftzBf16x2::kind, true, true, 2,
       ir::ScalarType::BF16x2, "b32",
       "atom.global.max.noftz.L2::cache_hint.v2.bf16x2", "0f3fc03fc0",
       0x3fc03fc0ULL},
      {"atom/atom_vector_max_noftz_bf16x2/with_policy/src/bf16x2/v4",
       ir::AtomVectorMaxNoftzBf16x2::kind, true, true, 4,
       ir::ScalarType::BF16x2, "b32",
       "atom.global.max.noftz.L2::cache_hint.v4.bf16x2", "0f3fc03fc0",
       0x3fc03fc0ULL},
      {"atom/atom_vector_add_f32/no_hint/src/f32/v2",
       ir::AtomVectorAddF32::kind, true, false, 2, ir::ScalarType::F32, "b32",
       "atom.global.add.v2.f32", "1.5", 0x3fc00000ULL},
      {"atom/atom_vector_add_f32/no_hint/src/f32/v4",
       ir::AtomVectorAddF32::kind, true, false, 4, ir::ScalarType::F32, "b32",
       "atom.global.add.v4.f32", "1.5", 0x3fc00000ULL},
      {"atom/atom_vector_add_f32/with_policy/src/f32/v2",
       ir::AtomVectorAddF32::kind, true, true, 2, ir::ScalarType::F32, "b32",
       "atom.global.add.L2::cache_hint.v2.f32", "1.5", 0x3fc00000ULL},
      {"atom/atom_vector_add_f32/with_policy/src/f32/v4",
       ir::AtomVectorAddF32::kind, true, true, 4, ir::ScalarType::F32, "b32",
       "atom.global.add.L2::cache_hint.v4.f32", "1.5", 0x3fc00000ULL},
      {"red/red_vector_add_noftz_f16/no_hint/src/f16/v2",
       ir::RedVectorAddNoftzF16::kind, false, false, 2, ir::ScalarType::F16,
       "b16", "red.global.add.noftz.v2.f16", "1.5", 0x3e00ULL},
      {"red/red_vector_add_noftz_f16/no_hint/src/f16/v4",
       ir::RedVectorAddNoftzF16::kind, false, false, 4, ir::ScalarType::F16,
       "b16", "red.global.add.noftz.v4.f16", "1.5", 0x3e00ULL},
      {"red/red_vector_add_noftz_f16/no_hint/src/f16/v8",
       ir::RedVectorAddNoftzF16::kind, false, false, 8, ir::ScalarType::F16,
       "b16", "red.global.add.noftz.v8.f16", "1.5", 0x3e00ULL},
      {"red/red_vector_add_noftz_f16/with_policy/src/f16/v2",
       ir::RedVectorAddNoftzF16::kind, false, true, 2, ir::ScalarType::F16,
       "b16", "red.global.add.noftz.L2::cache_hint.v2.f16", "1.5", 0x3e00ULL},
      {"red/red_vector_add_noftz_f16/with_policy/src/f16/v4",
       ir::RedVectorAddNoftzF16::kind, false, true, 4, ir::ScalarType::F16,
       "b16", "red.global.add.noftz.L2::cache_hint.v4.f16", "1.5", 0x3e00ULL},
      {"red/red_vector_add_noftz_f16/with_policy/src/f16/v8",
       ir::RedVectorAddNoftzF16::kind, false, true, 8, ir::ScalarType::F16,
       "b16", "red.global.add.noftz.L2::cache_hint.v8.f16", "1.5", 0x3e00ULL},
      {"red/red_vector_min_noftz_f16/no_hint/src/f16/v2",
       ir::RedVectorMinNoftzF16::kind, false, false, 2, ir::ScalarType::F16,
       "b16", "red.global.min.noftz.v2.f16", "1.5", 0x3e00ULL},
      {"red/red_vector_min_noftz_f16/no_hint/src/f16/v4",
       ir::RedVectorMinNoftzF16::kind, false, false, 4, ir::ScalarType::F16,
       "b16", "red.global.min.noftz.v4.f16", "1.5", 0x3e00ULL},
      {"red/red_vector_min_noftz_f16/no_hint/src/f16/v8",
       ir::RedVectorMinNoftzF16::kind, false, false, 8, ir::ScalarType::F16,
       "b16", "red.global.min.noftz.v8.f16", "1.5", 0x3e00ULL},
      {"red/red_vector_min_noftz_f16/with_policy/src/f16/v2",
       ir::RedVectorMinNoftzF16::kind, false, true, 2, ir::ScalarType::F16,
       "b16", "red.global.min.noftz.L2::cache_hint.v2.f16", "1.5", 0x3e00ULL},
      {"red/red_vector_min_noftz_f16/with_policy/src/f16/v4",
       ir::RedVectorMinNoftzF16::kind, false, true, 4, ir::ScalarType::F16,
       "b16", "red.global.min.noftz.L2::cache_hint.v4.f16", "1.5", 0x3e00ULL},
      {"red/red_vector_min_noftz_f16/with_policy/src/f16/v8",
       ir::RedVectorMinNoftzF16::kind, false, true, 8, ir::ScalarType::F16,
       "b16", "red.global.min.noftz.L2::cache_hint.v8.f16", "1.5", 0x3e00ULL},
      {"red/red_vector_max_noftz_f16/no_hint/src/f16/v2",
       ir::RedVectorMaxNoftzF16::kind, false, false, 2, ir::ScalarType::F16,
       "b16", "red.global.max.noftz.v2.f16", "1.5", 0x3e00ULL},
      {"red/red_vector_max_noftz_f16/no_hint/src/f16/v4",
       ir::RedVectorMaxNoftzF16::kind, false, false, 4, ir::ScalarType::F16,
       "b16", "red.global.max.noftz.v4.f16", "1.5", 0x3e00ULL},
      {"red/red_vector_max_noftz_f16/no_hint/src/f16/v8",
       ir::RedVectorMaxNoftzF16::kind, false, false, 8, ir::ScalarType::F16,
       "b16", "red.global.max.noftz.v8.f16", "1.5", 0x3e00ULL},
      {"red/red_vector_max_noftz_f16/with_policy/src/f16/v2",
       ir::RedVectorMaxNoftzF16::kind, false, true, 2, ir::ScalarType::F16,
       "b16", "red.global.max.noftz.L2::cache_hint.v2.f16", "1.5", 0x3e00ULL},
      {"red/red_vector_max_noftz_f16/with_policy/src/f16/v4",
       ir::RedVectorMaxNoftzF16::kind, false, true, 4, ir::ScalarType::F16,
       "b16", "red.global.max.noftz.L2::cache_hint.v4.f16", "1.5", 0x3e00ULL},
      {"red/red_vector_max_noftz_f16/with_policy/src/f16/v8",
       ir::RedVectorMaxNoftzF16::kind, false, true, 8, ir::ScalarType::F16,
       "b16", "red.global.max.noftz.L2::cache_hint.v8.f16", "1.5", 0x3e00ULL},
      {"red/red_vector_add_noftz_bf16/no_hint/src/bf16/v2",
       ir::RedVectorAddNoftzBf16::kind, false, false, 2, ir::ScalarType::BF16,
       "b16", "red.global.add.noftz.v2.bf16", "1.5", 0x3fc0ULL},
      {"red/red_vector_add_noftz_bf16/no_hint/src/bf16/v4",
       ir::RedVectorAddNoftzBf16::kind, false, false, 4, ir::ScalarType::BF16,
       "b16", "red.global.add.noftz.v4.bf16", "1.5", 0x3fc0ULL},
      {"red/red_vector_add_noftz_bf16/no_hint/src/bf16/v8",
       ir::RedVectorAddNoftzBf16::kind, false, false, 8, ir::ScalarType::BF16,
       "b16", "red.global.add.noftz.v8.bf16", "1.5", 0x3fc0ULL},
      {"red/red_vector_add_noftz_bf16/with_policy/src/bf16/v2",
       ir::RedVectorAddNoftzBf16::kind, false, true, 2, ir::ScalarType::BF16,
       "b16", "red.global.add.noftz.L2::cache_hint.v2.bf16", "1.5", 0x3fc0ULL},
      {"red/red_vector_add_noftz_bf16/with_policy/src/bf16/v4",
       ir::RedVectorAddNoftzBf16::kind, false, true, 4, ir::ScalarType::BF16,
       "b16", "red.global.add.noftz.L2::cache_hint.v4.bf16", "1.5", 0x3fc0ULL},
      {"red/red_vector_add_noftz_bf16/with_policy/src/bf16/v8",
       ir::RedVectorAddNoftzBf16::kind, false, true, 8, ir::ScalarType::BF16,
       "b16", "red.global.add.noftz.L2::cache_hint.v8.bf16", "1.5", 0x3fc0ULL},
      {"red/red_vector_min_noftz_bf16/no_hint/src/bf16/v2",
       ir::RedVectorMinNoftzBf16::kind, false, false, 2, ir::ScalarType::BF16,
       "b16", "red.global.min.noftz.v2.bf16", "1.5", 0x3fc0ULL},
      {"red/red_vector_min_noftz_bf16/no_hint/src/bf16/v4",
       ir::RedVectorMinNoftzBf16::kind, false, false, 4, ir::ScalarType::BF16,
       "b16", "red.global.min.noftz.v4.bf16", "1.5", 0x3fc0ULL},
      {"red/red_vector_min_noftz_bf16/no_hint/src/bf16/v8",
       ir::RedVectorMinNoftzBf16::kind, false, false, 8, ir::ScalarType::BF16,
       "b16", "red.global.min.noftz.v8.bf16", "1.5", 0x3fc0ULL},
      {"red/red_vector_min_noftz_bf16/with_policy/src/bf16/v2",
       ir::RedVectorMinNoftzBf16::kind, false, true, 2, ir::ScalarType::BF16,
       "b16", "red.global.min.noftz.L2::cache_hint.v2.bf16", "1.5", 0x3fc0ULL},
      {"red/red_vector_min_noftz_bf16/with_policy/src/bf16/v4",
       ir::RedVectorMinNoftzBf16::kind, false, true, 4, ir::ScalarType::BF16,
       "b16", "red.global.min.noftz.L2::cache_hint.v4.bf16", "1.5", 0x3fc0ULL},
      {"red/red_vector_min_noftz_bf16/with_policy/src/bf16/v8",
       ir::RedVectorMinNoftzBf16::kind, false, true, 8, ir::ScalarType::BF16,
       "b16", "red.global.min.noftz.L2::cache_hint.v8.bf16", "1.5", 0x3fc0ULL},
      {"red/red_vector_max_noftz_bf16/no_hint/src/bf16/v2",
       ir::RedVectorMaxNoftzBf16::kind, false, false, 2, ir::ScalarType::BF16,
       "b16", "red.global.max.noftz.v2.bf16", "1.5", 0x3fc0ULL},
      {"red/red_vector_max_noftz_bf16/no_hint/src/bf16/v4",
       ir::RedVectorMaxNoftzBf16::kind, false, false, 4, ir::ScalarType::BF16,
       "b16", "red.global.max.noftz.v4.bf16", "1.5", 0x3fc0ULL},
      {"red/red_vector_max_noftz_bf16/no_hint/src/bf16/v8",
       ir::RedVectorMaxNoftzBf16::kind, false, false, 8, ir::ScalarType::BF16,
       "b16", "red.global.max.noftz.v8.bf16", "1.5", 0x3fc0ULL},
      {"red/red_vector_max_noftz_bf16/with_policy/src/bf16/v2",
       ir::RedVectorMaxNoftzBf16::kind, false, true, 2, ir::ScalarType::BF16,
       "b16", "red.global.max.noftz.L2::cache_hint.v2.bf16", "1.5", 0x3fc0ULL},
      {"red/red_vector_max_noftz_bf16/with_policy/src/bf16/v4",
       ir::RedVectorMaxNoftzBf16::kind, false, true, 4, ir::ScalarType::BF16,
       "b16", "red.global.max.noftz.L2::cache_hint.v4.bf16", "1.5", 0x3fc0ULL},
      {"red/red_vector_max_noftz_bf16/with_policy/src/bf16/v8",
       ir::RedVectorMaxNoftzBf16::kind, false, true, 8, ir::ScalarType::BF16,
       "b16", "red.global.max.noftz.L2::cache_hint.v8.bf16", "1.5", 0x3fc0ULL},
      {"red/red_vector_add_noftz_f16x2/no_hint/src/f16x2/v2",
       ir::RedVectorAddNoftzF16x2::kind, false, false, 2, ir::ScalarType::F16x2,
       "b32", "red.global.add.noftz.v2.f16x2", "0f3e003e00", 0x3e003e00ULL},
      {"red/red_vector_add_noftz_f16x2/no_hint/src/f16x2/v4",
       ir::RedVectorAddNoftzF16x2::kind, false, false, 4, ir::ScalarType::F16x2,
       "b32", "red.global.add.noftz.v4.f16x2", "0f3e003e00", 0x3e003e00ULL},
      {"red/red_vector_add_noftz_f16x2/with_policy/src/f16x2/v2",
       ir::RedVectorAddNoftzF16x2::kind, false, true, 2, ir::ScalarType::F16x2,
       "b32", "red.global.add.noftz.L2::cache_hint.v2.f16x2", "0f3e003e00",
       0x3e003e00ULL},
      {"red/red_vector_add_noftz_f16x2/with_policy/src/f16x2/v4",
       ir::RedVectorAddNoftzF16x2::kind, false, true, 4, ir::ScalarType::F16x2,
       "b32", "red.global.add.noftz.L2::cache_hint.v4.f16x2", "0f3e003e00",
       0x3e003e00ULL},
      {"red/red_vector_min_noftz_f16x2/no_hint/src/f16x2/v2",
       ir::RedVectorMinNoftzF16x2::kind, false, false, 2, ir::ScalarType::F16x2,
       "b32", "red.global.min.noftz.v2.f16x2", "0f3e003e00", 0x3e003e00ULL},
      {"red/red_vector_min_noftz_f16x2/no_hint/src/f16x2/v4",
       ir::RedVectorMinNoftzF16x2::kind, false, false, 4, ir::ScalarType::F16x2,
       "b32", "red.global.min.noftz.v4.f16x2", "0f3e003e00", 0x3e003e00ULL},
      {"red/red_vector_min_noftz_f16x2/with_policy/src/f16x2/v2",
       ir::RedVectorMinNoftzF16x2::kind, false, true, 2, ir::ScalarType::F16x2,
       "b32", "red.global.min.noftz.L2::cache_hint.v2.f16x2", "0f3e003e00",
       0x3e003e00ULL},
      {"red/red_vector_min_noftz_f16x2/with_policy/src/f16x2/v4",
       ir::RedVectorMinNoftzF16x2::kind, false, true, 4, ir::ScalarType::F16x2,
       "b32", "red.global.min.noftz.L2::cache_hint.v4.f16x2", "0f3e003e00",
       0x3e003e00ULL},
      {"red/red_vector_max_noftz_f16x2/no_hint/src/f16x2/v2",
       ir::RedVectorMaxNoftzF16x2::kind, false, false, 2, ir::ScalarType::F16x2,
       "b32", "red.global.max.noftz.v2.f16x2", "0f3e003e00", 0x3e003e00ULL},
      {"red/red_vector_max_noftz_f16x2/no_hint/src/f16x2/v4",
       ir::RedVectorMaxNoftzF16x2::kind, false, false, 4, ir::ScalarType::F16x2,
       "b32", "red.global.max.noftz.v4.f16x2", "0f3e003e00", 0x3e003e00ULL},
      {"red/red_vector_max_noftz_f16x2/with_policy/src/f16x2/v2",
       ir::RedVectorMaxNoftzF16x2::kind, false, true, 2, ir::ScalarType::F16x2,
       "b32", "red.global.max.noftz.L2::cache_hint.v2.f16x2", "0f3e003e00",
       0x3e003e00ULL},
      {"red/red_vector_max_noftz_f16x2/with_policy/src/f16x2/v4",
       ir::RedVectorMaxNoftzF16x2::kind, false, true, 4, ir::ScalarType::F16x2,
       "b32", "red.global.max.noftz.L2::cache_hint.v4.f16x2", "0f3e003e00",
       0x3e003e00ULL},
      {"red/red_vector_add_noftz_bf16x2/no_hint/src/bf16x2/v2",
       ir::RedVectorAddNoftzBf16x2::kind, false, false, 2,
       ir::ScalarType::BF16x2, "b32", "red.global.add.noftz.v2.bf16x2",
       "0f3fc03fc0", 0x3fc03fc0ULL},
      {"red/red_vector_add_noftz_bf16x2/no_hint/src/bf16x2/v4",
       ir::RedVectorAddNoftzBf16x2::kind, false, false, 4,
       ir::ScalarType::BF16x2, "b32", "red.global.add.noftz.v4.bf16x2",
       "0f3fc03fc0", 0x3fc03fc0ULL},
      {"red/red_vector_add_noftz_bf16x2/with_policy/src/bf16x2/v2",
       ir::RedVectorAddNoftzBf16x2::kind, false, true, 2,
       ir::ScalarType::BF16x2, "b32",
       "red.global.add.noftz.L2::cache_hint.v2.bf16x2", "0f3fc03fc0",
       0x3fc03fc0ULL},
      {"red/red_vector_add_noftz_bf16x2/with_policy/src/bf16x2/v4",
       ir::RedVectorAddNoftzBf16x2::kind, false, true, 4,
       ir::ScalarType::BF16x2, "b32",
       "red.global.add.noftz.L2::cache_hint.v4.bf16x2", "0f3fc03fc0",
       0x3fc03fc0ULL},
      {"red/red_vector_min_noftz_bf16x2/no_hint/src/bf16x2/v2",
       ir::RedVectorMinNoftzBf16x2::kind, false, false, 2,
       ir::ScalarType::BF16x2, "b32", "red.global.min.noftz.v2.bf16x2",
       "0f3fc03fc0", 0x3fc03fc0ULL},
      {"red/red_vector_min_noftz_bf16x2/no_hint/src/bf16x2/v4",
       ir::RedVectorMinNoftzBf16x2::kind, false, false, 4,
       ir::ScalarType::BF16x2, "b32", "red.global.min.noftz.v4.bf16x2",
       "0f3fc03fc0", 0x3fc03fc0ULL},
      {"red/red_vector_min_noftz_bf16x2/with_policy/src/bf16x2/v2",
       ir::RedVectorMinNoftzBf16x2::kind, false, true, 2,
       ir::ScalarType::BF16x2, "b32",
       "red.global.min.noftz.L2::cache_hint.v2.bf16x2", "0f3fc03fc0",
       0x3fc03fc0ULL},
      {"red/red_vector_min_noftz_bf16x2/with_policy/src/bf16x2/v4",
       ir::RedVectorMinNoftzBf16x2::kind, false, true, 4,
       ir::ScalarType::BF16x2, "b32",
       "red.global.min.noftz.L2::cache_hint.v4.bf16x2", "0f3fc03fc0",
       0x3fc03fc0ULL},
      {"red/red_vector_max_noftz_bf16x2/no_hint/src/bf16x2/v2",
       ir::RedVectorMaxNoftzBf16x2::kind, false, false, 2,
       ir::ScalarType::BF16x2, "b32", "red.global.max.noftz.v2.bf16x2",
       "0f3fc03fc0", 0x3fc03fc0ULL},
      {"red/red_vector_max_noftz_bf16x2/no_hint/src/bf16x2/v4",
       ir::RedVectorMaxNoftzBf16x2::kind, false, false, 4,
       ir::ScalarType::BF16x2, "b32", "red.global.max.noftz.v4.bf16x2",
       "0f3fc03fc0", 0x3fc03fc0ULL},
      {"red/red_vector_max_noftz_bf16x2/with_policy/src/bf16x2/v2",
       ir::RedVectorMaxNoftzBf16x2::kind, false, true, 2,
       ir::ScalarType::BF16x2, "b32",
       "red.global.max.noftz.L2::cache_hint.v2.bf16x2", "0f3fc03fc0",
       0x3fc03fc0ULL},
      {"red/red_vector_max_noftz_bf16x2/with_policy/src/bf16x2/v4",
       ir::RedVectorMaxNoftzBf16x2::kind, false, true, 4,
       ir::ScalarType::BF16x2, "b32",
       "red.global.max.noftz.L2::cache_hint.v4.bf16x2", "0f3fc03fc0",
       0x3fc03fc0ULL},
      {"red/red_vector_add_f32/no_hint/src/f32/v2", ir::RedVectorAddF32::kind,
       false, false, 2, ir::ScalarType::F32, "b32", "red.global.add.v2.f32",
       "1.5", 0x3fc00000ULL},
      {"red/red_vector_add_f32/no_hint/src/f32/v4", ir::RedVectorAddF32::kind,
       false, false, 4, ir::ScalarType::F32, "b32", "red.global.add.v4.f32",
       "1.5", 0x3fc00000ULL},
      {"red/red_vector_add_f32/with_policy/src/f32/v2",
       ir::RedVectorAddF32::kind, false, true, 2, ir::ScalarType::F32, "b32",
       "red.global.add.L2::cache_hint.v2.f32", "1.5", 0x3fc00000ULL},
      {"red/red_vector_add_f32/with_policy/src/f32/v4",
       ir::RedVectorAddF32::kind, false, true, 4, ir::ScalarType::F32, "b32",
       "red.global.add.L2::cache_hint.v4.f32", "1.5", 0x3fc00000ULL},
  };
  for (const auto& item : cases)
    for (unsigned mode = 0; mode < 3; ++mode) {
      const bool literal = mode == 1;
      const bool mixed = mode == 2;
      /** Include branch identity in every installed-fixture failure. */
      const auto require = [&](bool condition, std::string_view contract) {
        if (!condition)
          std::cerr << "atomic ValueVector consumer " << item.id << ": "
                    << contract << '\n';
        return condition;
      };
      std::optional<ir::ResolvedModule> owned;
      std::vector<ptx_frontend::SourceRange> written_ranges;
      {
        std::string text =
            ".version 9.3\n.target sm_90\n.global .align 16 .b8 "
            "data[256];\n.entry kernel(){ .reg ." +
            std::string(item.physical) + " d<8>,s<8>; .reg .b64 policy; " +
            std::string(item.operation) + " ";
        if (item.atom)
          text += installedAtomicValuePack(item.arity, "d", true) + ",";
        auto pack = installedAtomicValuePack(
            item.arity, literal ? item.literal : "s", !literal);
        if (mixed) {
          const auto comma = pack.find(',');
          if (comma == std::string::npos)
            return false;
          pack.replace(1, comma - 1, item.literal);
        }
        text += "[data]," + pack;
        text += item.hinted ? ",policy; }" : "; }";
#if defined(PTX_ATOMIC_VALUE_CHECKER_WRAP)
        written_ranges = ir::atomic_value_test_wrap::ranges(text);
#endif
        ptx_frontend::PtxSyntaxParser parser(text);
        const auto ast = parser.parseModule();
        if (!require(ast.has_value(), "complete source parses") ||
            !require(ast.diagnostics.empty(), "parse diagnostics empty"))
          return false;
        auto result = ir::resolveAndValidateModule(*ast);
        if (!require(result.has_value(),
                     "register/literal source strictly resolves") ||
            !require(ir::validateModule(*ast, *result).has_value(),
                     "source identity validates"))
          return false;
        owned.emplace(std::move(*result));
      }
      if (!require(owned && owned->functions.size() == 1,
                   "one owner survives real AST/source death") ||
          !require(owned->functions.front().body.size() == 1 &&
                       owned->functions.front().body.front(),
                   "one concrete instruction is owned"))
        return false;
      {
        const auto& instruction = *owned->functions.front().body.front();
        if (!require(instruction.instruction_kind() == item.kind,
                     "exact class retained"))
          return false;
        const auto& source = installedAtomicValueSource(instruction);
        if (!require(source.value.elements.size() == item.arity &&
                         source.locs.size() == item.arity,
                     "all actual lanes and locations retained"))
          return false;
        for (size_t lane_index = 0; lane_index < source.value.elements.size();
             ++lane_index) {
          const auto& lane = source.value.elements[lane_index];
          if (literal || (mixed && lane_index == 0)) {
            const auto* value = std::get_if<ir::ResolvedImmediate>(&lane);
            if (!require(value && value->type == item.logical &&
                             value->bits == item.bits &&
                             !value->integer_source_bits && !value->is_negative,
                         "actual immediate type/bits/origin flags"))
              return false;
          } else {
            const auto* value = std::get_if<ir::ResolvedRegisterRef>(&lane);
            if (!require(value && value->symbol_id &&
                             value->register_class ==
                                 ir::ResolvedRegisterClass::General,
                         "actual register carrier and SymbolId"))
              return false;
          }
        }
        /** Callback arguments are consumed while the actual field remains live. */
        struct Observer final : ir::detail::IReferenceObserver {
          /** Borrowed owner for this one traversal. */
          const ptx_frontend::WithLocs<ir::ResolvedValueVector>& owner;
          /** Copied result/count, never a retained callback pointer or span. */
          bool valid = true;
          size_t calls = 0;
          /** Bind a live exact member. */
          explicit Observer(
              const ptx_frontend::WithLocs<ir::ResolvedValueVector>& source)
              : owner(source) {}
          /** Check every actual lane location synchronously. */
          void value_vector(
              const ir::ResolvedValueVector& value,
              std::span<const ptx_frontend::SourceRange> ranges,
              ir::checker::AddressSymbolResolutionPolicy) override {
            if (&value != &owner.value)
              return;
            ++calls;
            valid = valid && ranges.size() == owner.locs.size() &&
                    ranges.data() == owner.locs.data();
            if (!valid)
              return;
            for (size_t index = 0; index < ranges.size(); ++index)
              valid = valid && ranges[index] == owner.locs[index];
          }
        } observer(source);
        instruction.visit_references(observer);
        if (!require(observer.calls == 1 && observer.valid,
                     "actual value_vector observer payload/ranges"))
          return false;
        const auto clone = instruction.clone();
        if (!require(clone && clone->instruction_kind() == item.kind &&
                         installedAtomicValueSource(*clone) == source,
                     "exact-class clone retains owned source"))
          return false;
      }
      auto copied = *owned;
      ir::ResolvedModule assigned;
      assigned = copied;
      ir::ResolvedFunction function = copied.functions.front(),
                           function_assigned;
      function_assigned = function;
      assigned.functions.front() = std::move(function_assigned);
      assigned.functions.front().body.front() = function.body.front()->clone();
      owned.reset();
#if defined(PTX_ATOMIC_VALUE_CHECKER_WRAP)
      for (auto* current : {&copied, &assigned}) {
        if (current->functions.size() != 1 ||
            current->functions.front().body.size() != 1 ||
            current->functions.front().instruction_ranges.size() != 1 ||
            !current->functions.front().body.front())
          return false;
        const auto& instruction = *current->functions.front().body.front();
        const auto& source = installedAtomicValueSource(instruction);
        ir::atomic_value_test_wrap::Capture capture;
        capture.member = &source;
        capture.logical = item.logical;
        capture.physical =
            item.physical == "b16" ? ir::ScalarType::B16 : ir::ScalarType::B32;
        capture.arity = item.arity;
        capture.bits = item.bits;
        capture.mode = mode;
        capture.ranges = written_ranges;
        const ir::checker::Context context{
            .target = {.ptx_version = {9, 3}, .sm_version = 90},
            .instruction_range =
                current->functions.front().instruction_ranges.front()};
        ir::checker::CheckResult checked;
        {
          ir::atomic_value_test_wrap::Scope scope(capture);
          checked = instruction.check(context);
        }
        if (!require(bool(checked) && capture.valid && capture.calls == 1,
                     "actual generated owner/lane pointers, mirrors, ranges "
                     "and values"))
          return false;
        std::string operation(item.operation);
        operation += " ";
        if (item.atom)
          operation += installedAtomicValuePack(item.arity, "d", true) + ",";
        auto pack = installedAtomicValuePack(
            item.arity, literal ? item.literal : "s", !literal);
        if (mixed) {
          const auto comma = pack.find(',');
          if (comma == std::string::npos)
            return false;
          pack.replace(1, comma - 1, item.literal);
        }
        operation += "[data]," + pack + (item.hinted ? ",policy;" : ";");
        ptx_frontend::PtxSyntaxParser single_parser(operation);
        const auto single = single_parser.parseInstruction();
        if (!require(single && single.diagnostics.empty(),
                     "public source instruction factory parses"))
          return false;
        const auto scope =
            current->symbols.symbol(current->functions.front().symbol_id)
                .owned_scope;
        if (!require(scope.has_value(),
                     "actual function declaration owns its scope"))
          return false;
        const ir::ResolveContext declarations{.symbols = current->symbols,
                                              .scope = *scope,
                                              .function_scope = *scope,
                                              .function_is_entry = true};
        auto factory = ir::resolveInstruction(*single, declarations);
        if (!require(factory && (*factory)->instruction_kind() == item.kind,
                     "public source factory retains exact class"))
          return false;
        const auto factory_clone = (*factory)->clone();
        if (!require(
                factory_clone && factory_clone->instruction_kind() == item.kind,
                "factory clone retains exact class"))
          return false;
        const auto& factory_source = installedAtomicValueSource(*factory_clone);
        if (!require(factory_source.value.elements.size() == item.arity,
                     "factory has actual public source member"))
          return false;
        for (size_t index = 0; index < item.arity; ++index) {
          const bool immediate = literal || (mixed && index == 0);
          if (!require(
                  immediate == std::holds_alternative<ir::ResolvedImmediate>(
                                   factory_source.value.elements[index]),
                  "standard variant exposes actual factory arm"))
            return false;
          if (immediate) {
            const auto* value = std::get_if<ir::ResolvedImmediate>(
                &factory_source.value.elements[index]);
            if (!require(value && value->type == item.logical &&
                             value->bits == item.bits &&
                             !value->integer_source_bits && !value->is_negative,
                         "factory owned typed word/origin matches independent "
                         "fixture"))
              return false;
          } else {
            const auto* value = std::get_if<ir::ResolvedRegisterRef>(
                &factory_source.value.elements[index]);
            if (!require(value && value->symbol_id &&
                             value->spelling == "s" + std::to_string(index),
                         "factory actual declaration-bound register"))
              return false;
          }
        }
      }
#endif
      if (!require(ir::validateModule(copied).has_value() &&
                       ir::validateModule(assigned).has_value(),
                   "independent copied/assigned owners validate"))
        return false;
    }
  return true;
}

/** Exercise exact-form and module resolution from the installed component. */
/** Borrow one of the four migrated sources while its actual instruction owner survives. */
ptx_frontend::WithLocs<ptx_frontend::resolved_ir::RegOrImm>*
installedPackedAtomicMember(
    ptx_frontend::resolved_ir::Instruction& instruction) {
  namespace ir = ptx_frontend::resolved_ir;
  if (auto* p = dynamic_cast<ir::AtomGlobalAddNoftzF16x2*>(&instruction))
    return &p->src;
  if (auto* p = dynamic_cast<ir::AtomGlobalAddNoftzBf16x2*>(&instruction))
    return &p->src;
  if (auto* p = dynamic_cast<ir::RedGlobalAddNoftzF16x2*>(&instruction))
    return &p->src;
  if (auto* p = dynamic_cast<ir::RedGlobalAddNoftzBf16x2*>(&instruction))
    return &p->src;
  return nullptr;
}

/** Consume current installed classes, factory, variants and observers using matching archives. */
bool checkInstalledPackedAtomicSources() {
  namespace ir = ptx_frontend::resolved_ir;
  static_assert(std::same_as<decltype(ir::AtomGlobalAddNoftzF16x2{}.src.value),
                             ir::RegOrImm>);
  static_assert(std::same_as<decltype(ir::AtomGlobalAddNoftzBf16x2{}.src.value),
                             ir::RegOrImm>);
  static_assert(std::same_as<decltype(ir::RedGlobalAddNoftzF16x2{}.src.value),
                             ir::RegOrImm>);
  static_assert(std::same_as<decltype(ir::RedGlobalAddNoftzBf16x2{}.src.value),
                             ir::RegOrImm>);
  static_assert(std::variant_size_v<ir::RegOrImm> == 2);
  static_assert(std::same_as<std::variant_alternative_t<0, ir::RegOrImm>,
                             ir::ResolvedRegisterRef>);
  static_assert(std::same_as<std::variant_alternative_t<1, ir::RegOrImm>,
                             ir::ResolvedImmediate>);
  const auto profile = ptx_frontend::base::find_target_profile("sm_90");
  if (!profile)
    return false;
  const ir::checker::Context context{
      .target = {.ptx_version = {9, 3},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
  /** Observe real src values synchronously, with metadata confined to each bracket. */
  struct Observer final : ir::detail::IReferenceObserver {
    /** Actual owner member, retained only until the enclosing traversal returns. */
    const ir::RegOrImm* expected;
    /** Complete target context supplied by this installed consumer. */
    const ir::checker::Context& context;
    /** Required logical source format for this exact generated class. */
    ir::ScalarType type;
    /** Descriptor borrowed only between operand_context(d) and its nullptr reset. */
    const ir::checker::OperandDescriptor* descriptor = nullptr;
    /** Number of observed src fields, excluding the cache policy. */
    unsigned count = 0;
    /** Aggregate real value, pointer, location and OperandView checks. */
    bool okay = true;
    /** Bind a synchronous observer to an owner and a complete context. */
    Observer(const ir::RegOrImm& value, const ir::checker::Context& target,
             ir::ScalarType logical)
        : expected(&value), context(target), type(logical) {}
    /** Preserve source-only metadata within the current operand bracket. */
    void operand_context(const ir::checker::OperandDescriptor* value,
                         std::optional<ir::MemoryStateSpace>) override {
      descriptor = value && value->target_field_id == "src" ? value : nullptr;
    }
    /** Consume actual alternatives and values without retaining callback arguments. */
    void reg_or_imm(const ir::RegOrImm& value,
                    std::span<const ptx_frontend::SourceRange> locations,
                    ir::checker::AddressSymbolResolutionPolicy) override {
      if (&value != expected)
        return;
      const ir::checker::OperandDescriptor projection{
          .target_field_id = "src",
          .type_expression =
              {.kind = ir::checker::OperandTypeExpressionKind::FixedScalar,
               .fixed_scalar_type = type},
          .role = ir::checker::OperandRole::Source,
          .access = ir::checker::OperandAccess::Read,
          .allowed_shapes = ir::checker::OperandShape::Register |
                            ir::checker::OperandShape::Immediate};
      ++count;
      okay &= &value == expected && !locations.empty();
      ir::checker::OperandView view{
          .field_id = "src",
          .actual_shape = ir::checker::OperandShape::Immediate};
      view.locations = locations;
      if (const auto* word = std::get_if<ir::ResolvedImmediate>(&value)) {
        okay &= word->type == type &&
                word->bits == (type == ir::ScalarType::BF16x2 ? 0x3fc03fc0u
                                                              : 0x3e003e00u) &&
                !word->integer_source_bits && !word->is_negative;
        view.immediate_type = word->type;
        view.immediate_bits = word->bits;
        view.immediate_is_negative = word->is_negative;
        view.integer_source_bits = word->integer_source_bits;
      } else if (const auto* reg =
                     std::get_if<ir::ResolvedRegisterRef>(&value)) {
        okay &= reg->symbol_id.has_value() &&
                reg->declared_type == ir::ScalarType::B32;
        view.actual_shape = ir::checker::OperandShape::Register;
        view.register_type = reg->declared_type;
        view.register_symbol_id = reg->symbol_id;
        view.register_class = reg->register_class;
        view.register_ref = reg;
        view.register_vector_width = reg->vector_width;
      } else {
        okay = false;
        return;
      }
      okay &= ir::checker::check_operands({&projection, 1}, {}, {&view, 1}, {},
                                          context)
                  .has_value();
    }
  };
  for (bool reduction : {false, true})
    for (bool bfloat : {false, true})
      for (bool policy : {false, true})
        for (bool immediate : {false, true}) {
          const auto logical =
              bfloat ? ir::ScalarType::BF16x2 : ir::ScalarType::F16x2;
          const auto word = bfloat ? 0x3fc03fc0u : 0x3e003e00u;
          const std::string statement =
              std::string(reduction ? "red" : "atom") + ".global.add.noftz" +
              (policy ? ".L2::cache_hint" : "") +
              (bfloat ? ".bf16x2 " : ".f16x2 ") + (reduction ? "" : "%dst,") +
              "[cell]," +
              (immediate ? (bfloat ? "0f3fc03fc0" : "0f3e003e00") : "%bits") +
              (policy ? ",%policy;" : ";");
          const auto expected_kind =
              reduction
                  ? (bfloat ? ir::InstructionKind::RedGlobalAddNoftzBf16x2
                            : ir::InstructionKind::RedGlobalAddNoftzF16x2)
                  : (bfloat ? ir::InstructionKind::AtomGlobalAddNoftzBf16x2
                            : ir::InstructionKind::AtomGlobalAddNoftzF16x2);
          {
            std::string standalone = statement;
            for (const auto& replacement :
                 std::array<std::pair<std::string_view, std::string_view>, 4>{
                     {{"%dst", "%b0"},
                      {"%bits", "%b1"},
                      {"[cell]", "[%rd0]"},
                      {"%policy", "%rd1"}}}) {
              const auto position = standalone.find(replacement.first);
              if (position != std::string::npos)
                standalone.replace(position, replacement.first.size(),
                                   replacement.second);
            }
            ptx_frontend::PtxSyntaxParser factory_parser(standalone);
            const auto syntax = factory_parser.parseInstruction();
            if (!syntax || !syntax.diagnostics.empty())
              return false;
            const auto factory = ir::resolveInstruction(*syntax);
            if (!factory || !*factory ||
                (*factory)->instruction_kind() != expected_kind)
              return false;
            auto* member = installedPackedAtomicMember(**factory);
            if (!member || member->locs.empty())
              return false;
            if (immediate) {
              const auto* value =
                  std::get_if<ir::ResolvedImmediate>(&member->value);
              if (!value || value->type != logical || value->bits != word)
                return false;
            } else if (!std::get_if<ir::ResolvedRegisterRef>(&member->value))
              return false;
          }
          std::optional<ir::ResolvedModule> module;
          {
            const std::string text =
                ".version 9.3\n.target sm_90\n.address_size 64\n"
                ".global .align 4 .b32 cell;\n.entry k(){.reg .b32 %dst,%bits;"
                ".reg .b64 %policy;" +
                statement + "}";
            ptx_frontend::PtxSyntaxParser parser(text);
            const auto ast = parser.parseModule();
            if (!ast || !ast.diagnostics.empty())
              return false;
            auto result = ir::resolveAndValidateModule(*ast);
            if (!result || !ir::validateModule(*ast, *result))
              return false;
            module.emplace(std::move(*result));
          }
          if (module->functions.size() != 1 ||
              module->functions.front().body.size() != 1)
            return false;
          auto copy = *module;
          ir::ResolvedModule assigned;
          assigned = copy;
          auto moved = std::move(assigned);
          ir::ResolvedFunction function_copy = moved.functions.front();
          ir::ResolvedFunction function_assigned;
          function_assigned = function_copy;
          auto clone = moved.functions.front().body.front()->clone();
          module.reset();
          if (!ir::validateModule(moved) ||
              function_assigned.body.size() != 1 || !clone)
            return false;
          for (auto* instruction :
               {moved.functions.front().body.front().get(),
                function_assigned.body.front().get(), clone.get()}) {
            if (!instruction ||
                instruction->instruction_kind() != expected_kind ||
                !instruction->check(context))
              return false;
            auto* member = installedPackedAtomicMember(*instruction);
            if (!member || member->locs.empty())
              return false;
#ifdef PTX_PACKED_ATOMIC_CAPTURE_GENERATED_VIEWS
            const auto generated_before =
                ir::packed_atomic_projection::observed;
            ir::packed_atomic_projection::mirrors = true;
            ir::packed_atomic_projection::logical = logical;
            ir::packed_atomic_projection::expected = member;
            const auto generated_check = instruction->check(context);
            ir::packed_atomic_projection::expected = nullptr;
            if (!generated_check || !ir::packed_atomic_projection::mirrors ||
                ir::packed_atomic_projection::observed != generated_before + 1)
              return false;
#endif
            Observer observer(member->value, context, logical);
            instruction->visit_references(observer);
            if (!observer.okay || observer.count != 1 || observer.descriptor)
              return false;
            if (immediate) {
              auto* value = std::get_if<ir::ResolvedImmediate>(&member->value);
              if (!value)
                return false;
              const auto saved = *value;
              *value = ir::ResolvedImmediate{.bits = word, .type = logical};
              if (!instruction->check(context))
                return false;
              value->bits |= std::uint64_t{1} << 32;
              if (instruction->check(context))
                return false;
              *value = saved;
            }
          }
          if (!ir::validateModule(moved))
            return false;
        }
  return true;
}

int main() {
  if (!checkInstalledAtomicValueVectors())
    return 1;
  if (!checkInstalledPackedSetpSources())
    return 1;
#if defined(PTX_PACKED_SETP_CHECKER_WRAP)
  if (!checkInstalledPackedSetpGeneratedViews())
    return 1;
#endif
  if (!checkInstalledPackedAtomicSources())
    return 1;
  if (!checkInstalledScalarAtomicBf16())
    return 1;
  if (!checkInstalledScalarAtomicF16Sources())
    return 40;
  if (!checkInstalledOrdinaryBf16FmaSet())
    return 1;
  if (!checkInstalledExplicitBit16Setp())
    return 1;
  if (!checkInstalledPragmas())
    return 39;
  if (!checkInstalledLanguageDomain())
    return 38;
  if (!checkInstalledDebugLocations())
    return 37;
  if (!checkInstalledDebugSections())
    return 36;
  if (!checkInstalledRequiredTcgenMasks())
    return 35;
  if (!checkInstalledRedAsyncDefault())
    return 34;
  if (!checkInstalledHalfFmaModifiers())
    return 33;
  if (!checkInstalledHalfSetFtz())
    return 32;
  if (!checkInstalledStochasticQuadSources())
    return 31;
  if (!checkInstalledPackIntegerSources())
    return 30;
  if (!checkInstalledScalarHalfImmediates())
    return 29;
  if (!checkInstalledMbarrierTokenForms())
    return 1;
  if (!checkInstalledValueVectorSources())
    return 1;
  if (!checkInstalledPackedWords())
    return 1;
  if (!checkInstalledHeaderArrayRank() || !checkInstalledParameterDomain())
    return 1;
  if (!checkInstalledHeaderCompatibility())
    return 1;
  if (!checkInstalledCombinedSourceStates())
    return 1;
  if (!checkInstalledLegacyShuffle())
    return 1;
  if (!checkInstalledLegacyVote())
    return 1;
  if (!checkInstalledImplicitMad())
    return 1;
  if (!checkInstalledMappedF64())
    return 1;
  namespace ir = ptx_frontend::resolved_ir;
  ptx_frontend::PtxSyntaxParser parser("add.u32 %r0, %r1, 7;");
  auto ast = parser.parseInstruction();
  if (!ast) {
    std::cerr << "installed Add fixture did not parse\n";
    return 1;
  }
  auto instruction = ir::resolveAdd(*ast);
  if (!instruction || (*instruction)->instruction_kind() !=
                          ir::InstructionKind::AddIntegerNoSat) {
    std::cerr << "installed Add fixture did not resolve\n";
    return 1;
  }
  static constexpr std::array<std::string_view, 1> families{"sm_120f"};
  const ir::checker::Context context{
      .target = {.ptx_version = {9, 3},
                 .sm_version = 120,
                 .enabled_family_features = families}};
  if (!(*instruction)->check(context)) {
    std::cerr << "installed Add fixture did not check\n";
    return 1;
  }
  auto clone = (*instruction)->clone();
  ir::detail::IReferenceObserver observer;
  clone->visit_references(observer);
  if (clone->instruction_kind() != ir::InstructionKind::AddIntegerNoSat)
    return 1;

  constexpr std::string_view module_source = R"ptx(
.version 8.0
.target sm_80
.address_size 64
.visible .entry kernel() {
  .reg .u32 %r<2>;
  .reg .v2 .u32 V;
  .reg .u64 %rd;
  add.u32 V.x, V.g, 7;
  ld.v2.u32 V, [%rd];
  mov.v2.u32 V, {V.x, 1+2};
  mov.v2.u32 {%r0,%r1}, V;
}
)ptx";
  ptx_frontend::PtxSyntaxParser module_parser{module_source};
  auto module_ast = module_parser.parseModule();
  if (!module_ast || !module_ast.diagnostics.empty()) {
    std::cerr << "installed module fixture did not parse\n";
    return 1;
  }
  auto module = ir::resolveAndValidateModule(*module_ast);
  if (!module || module->functions.size() != 1 ||
      module->functions.front().body.size() != 4 ||
      dynamic_cast<const ir::AddIntegerNoSat*>(
          module->functions.front().body.front().get()) == nullptr) {
    std::cerr << "installed module fixture did not resolve and validate\n";
    return 1;
  }
  const auto& selected = dynamic_cast<const ir::AddIntegerNoSat&>(
      *module->functions.front().body.front());
  const auto& component = selected.dst.value;
  if (!component.component || component.vector_width ||
      component.component->origin !=
          ir::RegisterComponentOrigin::ExplicitSelector ||
      component.component->lane != 0 ||
      component.component->declaration_width != 2 ||
      !ir::valid_register_component(component) ||
      ir::ordinary_register_lane(".r") != component.component->lane) {
    std::cerr << "installed component metadata is inconsistent\n";
    return 1;
  }
  const auto& named = dynamic_cast<const ir::LdGenericVector&>(
      *module->functions.front().body[1]);
  const auto& source = named.dst.value.source;
  if (source.kind != ir::ResolvedVectorSourceKind::NamedVector ||
      !source.whole_base ||
      !ir::valid_register_vector_source(named.dst.value, named.dst.locs) ||
      named.dst.value.elements[0]->component->origin !=
          ir::RegisterComponentOrigin::NamedProjection ||
      named.dst.value.elements[0]->component->selector ||
      ir::valid_register_component(*named.dst.value.elements[0])) {
    std::cerr << "installed named vector provenance is inconsistent\n";
    return 1;
  }
  /** Inspect complete named provenance through the installed public visitor. */
  struct NamedVectorObserver final : ir::detail::IReferenceObserver {
    /** Number of complete named-vector payloads observed synchronously. */
    size_t named_count{};
    /** Consume the container without retaining any borrowed payload or ranges. */
    void register_vector(const ir::ResolvedRegisterVector& value,
                         std::span<const ptx_frontend::SourceRange> locations,
                         ir::checker::AddressSymbolResolutionPolicy) override {
      if (value.source.whole_base &&
          ir::valid_register_vector_source(value, locations))
        ++named_count;
    }
  } named_observer;
  named.visit_references(named_observer);
  if (named_observer.named_count != 1)
    return 1;
  const auto& values_mov =
      dynamic_cast<const ir::MovV4U32&>(*module->functions.front().body[2]);
  const auto& values =
      std::get<ir::ResolvedMovValueVector>(values_mov.src.value);
  if (values.source.kind != ir::ResolvedVectorSourceKind::BraceList ||
      std::get<ir::ResolvedImmediate>(values.elements[1]).bits != 3 ||
      !ir::valid_mov_vector_source(values, values_mov.src.locs))
    return 1;
  /** Observe the installed bounded MOV source without retaining borrowed values. */
  struct MovVectorObserver final : ir::detail::IReferenceObserver {
    /** Count complete named MOV sources, not fabricated individual registers. */
    size_t named_count{};
    /** Validate the owned source during its synchronous visitor callback. */
    void mov_vector_source(
        const ir::ResolvedMovVectorSource& value,
        std::span<const ptx_frontend::SourceRange> locations,
        ir::checker::AddressSymbolResolutionPolicy) override {
      if (const auto* vector = std::get_if<ir::ResolvedMovValueVector>(&value);
          vector &&
          vector->source.kind == ir::ResolvedVectorSourceKind::NamedVector &&
          ir::valid_mov_vector_source(*vector, locations))
        ++named_count;
    }
  } mov_observer;
  module->functions.front().body[3]->visit_references(mov_observer);
  if (mov_observer.named_count != 1 || !ir::validateModule(*module))
    return 1;
  ptx_frontend::PtxSyntaxParser table_parser{R"ptx(
.version 9.3
.target sm_80
.address_size 64
.func f(.reg .u32 x);
.global .u32 targets[4] = {f,f};
.entry caller() { .reg .u64 fp; call fp, (1+2), targets; }
)ptx"};
  auto table_ast = table_parser.parseModule();
  if (!table_ast || !table_ast.diagnostics.empty())
    return 1;
  auto table_module = ir::resolveAndValidateModule(*table_ast);
  if (!table_module || table_module->call_tables.size() != 1)
    return 1;
  const auto& table = table_module->call_tables.front();
  if (table.symbol_id != table_module->storage_declarations.front().symbol_id ||
      table.slots.size() != 2 || table.slots[1].byte_offset != 4 ||
      table.slots[0].canonical_function != table.slots[1].canonical_function ||
      table.signature.parameters.size() != 1)
    return 1;
  /** Exercise the installed indirect union without treating storage as metadata. */
  struct TableObserver final : ir::detail::IReferenceObserver {
    /** Owned identity copied during a synchronous visit. */
    std::optional<ptx_frontend::binding::SymbolId> table;
    /** Observe the real storage branch through the existing call visitor. */
    void indirect_callee(const ir::ResolvedIndirectCallee& callee,
                         std::span<const ptx_frontend::SourceRange>,
                         ir::checker::AddressSymbolResolutionPolicy) override {
      if (const auto* reference =
              std::get_if<ir::ResolvedCallTableRef>(&callee))
        table = reference->symbol_id;
    }
  } table_observer;
  table_module->functions.back().body.front()->visit_references(table_observer);
  if (table_observer.table != table.symbol_id ||
      !ir::validateModule(*table_module))
    return 1;
  constexpr ir::checker::AvailabilityDescriptor map_requirement{
      .any_of = {{{.minimum_ptx_version = {1, 0},
                   .has_exact_target = true,
                   .exact_target_architecture = {10},
                   .requires_map_f64_to_f32 = true}}},
      .any_of_count = 1,
  };
  ir::checker::TargetInfo legacy_target{
      .ptx_version = {1, 5},
      .sm_version = 10,
      .identity = ptx_frontend::base::parse_target_identity("sm_10"),
  };
  if (ir::checker::is_available(map_requirement, legacy_target))
    return 1;
  legacy_target.map_f64_to_f32 = true;
  if (!ir::checker::is_available(map_requirement, legacy_target))
    return 1;
  std::optional<ir::ResolvedModule> legacy_module;
  {
    const std::string legacy_source = R"ptx(.version 1.5
.target sm_10, map_f64_to_f32, texmode_unified
.func first() { ret; }
.target sm_11
.func second() { ret; }
.target sm_12, texmode_unified, map_f64_to_f32
.func third() { ret; }
)ptx";
    ptx_frontend::PtxSyntaxParser legacy_parser{legacy_source};
    auto legacy_ast = legacy_parser.parseModule();
    if (!legacy_ast || !legacy_ast.diagnostics.empty())
      return 1;
    auto legacy_resolved = ir::resolveAndValidateModule(*legacy_ast);
    if (!legacy_resolved)
      return 1;
    legacy_module.emplace(std::move(*legacy_resolved));
  }
  if (legacy_module->header.regions.size() != 4 ||
      !legacy_module->header.regions[1].map_f64_to_f32 ||
      legacy_module->header.regions[2].map_f64_to_f32 ||
      !legacy_module->header.regions[3].map_f64_to_f32 ||
      legacy_module->header.regions[1].target_options[1] != "map_f64_to_f32" ||
      legacy_module->header.regions[3].target_options[2] != "map_f64_to_f32" ||
      !ir::validateModule(*legacy_module))
    return 1;
  legacy_module->header.regions[1].map_f64_to_f32 = false;
  if (ir::validateModule(*legacy_module))
    return 1;
  legacy_module->header.regions[1].map_f64_to_f32 = true;
  legacy_module->header.regions[3].target_options.pop_back();
  if (ir::validateModule(*legacy_module))
    return 1;

  const ir::checker::ParameterAddressConstraint entry_constraint{
      .direction = ir::ParameterDirection::Input, .entry_only = true};
  if (!entry_constraint.entry_only ||
      ir::checker::ParameterAddressConstraint{}.entry_only)
    return 1;
  const auto& tensor_binding = ir::prefetch_resolved_descriptor()
                                   .variants.back()
                                   .operand_layouts.front()
                                   .bindings.front();
  if (!tensor_binding.parameter_constraint.entry_only ||
      tensor_binding.parameter_constraint.direction !=
          entry_constraint.direction ||
      tensor_binding.allowed_address_state_spaces.size() != 4)
    return 1;
  std::optional<ir::ResolvedModule> prefetch_module;
  {
    const std::string prefetch_source = R"ptx(
.version 8.0
.target sm_90
.address_size 64
.global .u32 global_map;
.const .u32 constant_map;
.entry kernel(.param .u32 input_map) {
  .local .u32 local_map;
  .shared .u32 shared_map;
  .reg .u64 %rd;
  prefetch.tensormap [constant_map];
  prefetch.tensormap [input_map];
  prefetch.tensormap [shared_map];
  prefetchu.L1 [global_map];
  prefetchu.L1 [constant_map];
  prefetchu.L1 [local_map];
  prefetchu.L1 [shared_map];
  prefetchu.L1 [%rd];
}
)ptx";
    ptx_frontend::PtxSyntaxParser prefetch_parser(prefetch_source);
    const auto prefetch_ast = prefetch_parser.parseModule();
    if (!prefetch_ast || !prefetch_ast.diagnostics.empty())
      return 1;
    auto resolved = ir::resolveAndValidateModule(*prefetch_ast);
    if (!resolved)
      return 1;
    prefetch_module.emplace(std::move(*resolved));
  }
  const auto strict_prefetch = [&] {
    return ir::validateModule(
        *prefetch_module, ir::ModuleValidationPolicy::RequireCompleteContext);
  };
  if (!strict_prefetch())
    return 1;
  auto& input_hint = dynamic_cast<ir::PrefetchGenericTensormap&>(
      *prefetch_module->functions.front().body[1]);
  auto& input = std::get<ir::ResolvedSymbolRef>(input_hint.address.value.base);
  if (!input.symbol_id ||
      input.symbol_id != prefetch_module->functions.front()
                             .parameter_declarations.front()
                             .symbol_id ||
      input.address_state_space !=
          ptx_frontend::base::DeclarationStateSpace::Parameter ||
      input.enclosing_function_kind != ir::EnclosingFunctionKind::Entry)
    return 1;
  input.enclosing_function_kind = ir::EnclosingFunctionKind::Device;
  input_hint.address.value.enclosing_function_kind =
      ir::EnclosingFunctionKind::Device;
  if (strict_prefetch())
    return 1;
  input.enclosing_function_kind = ir::EnclosingFunctionKind::Entry;
  input_hint.address.value.enclosing_function_kind =
      ir::EnclosingFunctionKind::Entry;
  if (!strict_prefetch())
    return 1;
  return 0;
}
