#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <concepts>
#include <optional>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection/setp.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include "../src/ptx_resolved_ir_private.hpp"
#include "test_syntax_parse_helpers.hpp"

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

namespace ptx_frontend::resolved_ir {
namespace {
/** Return an existing packed source; the instruction owns the borrowed field. */
WithLocs<RegOrImm>& packed_setp_source(Instruction& instruction, bool first) {
  if (auto* value = dynamic_cast<SetpF16x2*>(&instruction))
    return first ? value->src1 : value->src2;
  if (auto* value = dynamic_cast<SetpF16x2Boolean*>(&instruction))
    return first ? value->src1 : value->src2;
  if (auto* value = dynamic_cast<SetpBf16x2*>(&instruction))
    return first ? value->src1 : value->src2;
  if (auto* value = dynamic_cast<SetpBf16x2Boolean*>(&instruction))
    return first ? value->src1 : value->src2;
  throw std::runtime_error("Expected a packed SETP form.");
}
/** Prepare one legal source field without changing its sibling or target context. */
std::string packed_setp_module(bool bf16, bool boolean, bool first,
                               std::string_view source) {
  const std::string sibling = "b";
  return ".version 9.3\n.target sm_90\n.entry kernel(){.reg .pred p,q,r; "
         ".reg .b32 b; .reg .f16x2 h;\nsetp.eq" +
         std::string(boolean ? ".and" : "") +
         (bf16 ? ".bf16x2 p|q," : ".f16x2 p|q,") +
         (first ? std::string(source) : sibling) + "," +
         (first ? sibling : std::string(source)) + (boolean ? ",r;" : ";") +
         "\n}";
}
/** Own the producer text copy so its buffer, parser and AST die before returning IR. */
ResolvedModule own_packed_setp(std::string source) {
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    throw std::runtime_error("SETP fixture parse failure");
  auto module = resolveAndValidateModule(*ast);
  if (!module)
    throw std::runtime_error(module.error().empty()
                                 ? "SETP resolution failed without diagnostics"
                                 : module.error().front().message);
  if (module->functions.size() != 1 ||
      module->functions.front().body.size() != 1)
    throw std::runtime_error(
        "SETP fixture requires one real function and instruction");
  return std::move(*module);
}

/** Frozen independent source expectations; words are encoded bits, not numeric host conversions. */
struct PackedSetpSourceCase {
  /** Source spelling owns no transient parser state. */
  std::string_view text;
  /** Exact narrowed/current payload and evaluated64bit source cache. */
  uint64_t bits, source_bits;
  /** True only for a signed negative source, not an unsigned high-bit pattern. */
  bool negative;
};
constexpr std::array raw_cases{
    PackedSetpSourceCase{"1", 1, 1, false},
    PackedSetpSourceCase{"0x3fc03fc0", 0x3fc03fc0, 0x3fc03fc0, false},
    PackedSetpSourceCase{"-1", 0xffffffff, UINT64_MAX, true},
    PackedSetpSourceCase{"0x100000001", 1, 0x100000001, false},
    PackedSetpSourceCase{"18446744073709551615U", 0xffffffff, UINT64_MAX,
                         false},
    PackedSetpSourceCase{"(0x100000000+1)", 1, 0x100000001, false},
    PackedSetpSourceCase{"(1.5<2.0)", 1, 1, false},
    PackedSetpSourceCase{"!0", 1, 1, false},
    PackedSetpSourceCase{"WARP_SZ", 32, 32, false},
};
/** Exact words are independent of any host floating representation. */
struct PackedWordCase {
  /** Bare unsigned spelling and its complete 32-bit encoding. */
  std::string_view text;
  uint64_t bits;
};
constexpr std::array packed_cases{
    PackedWordCase{"0f00000000", 0},
    PackedWordCase{"0f3fc03fc0", 0x3fc03fc0},
    PackedWordCase{"0f80000000", 0x80000000},
    PackedWordCase{"0f7f817c01", 0x7f817c01},
    PackedWordCase{"0fffffffff", 0xffffffff},
};
/** Reacquire actual fields after source death, copies, assignments and polymorphic cloning. */
void expectPackedSetpLifetime(const ResolvedModule& module, bool first,
                              const syntax_ast::AstModule& supplied_ast) {
  auto copied = module;
  ResolvedModule assigned;
  assigned = copied;
  auto moved = std::move(copied);
  ResolvedFunction function = assigned.functions.front(), function_assigned;
  function_assigned = function;
  assigned.functions.front() = function_assigned;
  assigned.functions.front().body.front() =
      function_assigned.body.front()->clone();
  for (auto* owner : {&assigned, &moved}) {
    const auto& field =
        packed_setp_source(*owner->functions.front().body.front(), first);
    ASSERT_EQ(field.locs.size(), 1u);
    EXPECT_NE(field.locs.front(), SourceRange{});
    EXPECT_TRUE(validateModule(*owner));
    EXPECT_TRUE(validateModule(supplied_ast, *owner));
  }
}

TEST(PackedSetpSources,
     RoutesIntegerAndFloatingCategoriesAcrossAllEightFields) {
  for (bool bf16 : {false, true})
    for (bool boolean : {false, true})
      for (bool first : {false, true}) {
        const auto register_text =
            packed_setp_module(bf16, boolean, first, "b");
        auto register_control = own_packed_setp(register_text);
        const auto register_ast = test_helpers::parseModule(register_text);
        ASSERT_MODULE_PARSE_SUCCEEDS(register_ast);
        ASSERT_TRUE(validateModule(register_control));
        ASSERT_TRUE(validateModule(*register_ast, register_control));
        ASSERT_TRUE(std::holds_alternative<ResolvedRegisterRef>(
            packed_setp_source(*register_control.functions.front().body.front(),
                               first)
                .value));
        for (const auto& item : raw_cases) {
          SCOPED_TRACE(std::string(item.text));
          const auto source =
              packed_setp_module(bf16, boolean, first, item.text);
          auto module = own_packed_setp(source);
          const auto ast = test_helpers::parseModule(source);
          ASSERT_MODULE_PARSE_SUCCEEDS(ast);
          const auto& word = std::get<ResolvedImmediate>(
              packed_setp_source(*module.functions[0].body[0], first).value);
          EXPECT_EQ(word.type, ScalarType::B32);
          EXPECT_EQ(word.bits, item.bits);
          EXPECT_EQ(word.integer_source_bits, item.source_bits);
          EXPECT_EQ(word.is_negative, item.negative);
          EXPECT_TRUE(validateModule(module));
          EXPECT_TRUE(validateModule(*ast, module));
          expectPackedSetpLifetime(module, first, *ast);
        }
        for (const auto& item : packed_cases) {
          SCOPED_TRACE(std::string(item.text));
          const auto source =
              packed_setp_module(bf16, boolean, first, item.text);
          auto module = own_packed_setp(source);
          const auto ast = test_helpers::parseModule(source);
          ASSERT_MODULE_PARSE_SUCCEEDS(ast);
          const auto& word = std::get<ResolvedImmediate>(
              packed_setp_source(*module.functions[0].body[0], first).value);
          EXPECT_EQ(word.type, bf16 ? ScalarType::BF16x2 : ScalarType::F16x2);
          EXPECT_EQ(word.bits, item.bits);
          EXPECT_FALSE(word.integer_source_bits);
          EXPECT_FALSE(word.is_negative);
          EXPECT_TRUE(validateModule(*ast, module));
          expectPackedSetpLifetime(module, first, *ast);
        }
      }
}

/** Current arms are typed data; optional raw source caches constrain only retained facts. */
TEST(PackedSetpSources, ChecksCurrentArmsInsteadOfErasedProducerHistory) {
  for (bool bf16 : {false, true})
    for (bool boolean : {false, true})
      for (bool first : {false, true}) {
        const auto text = packed_setp_module(bf16, boolean, first, "1");
        auto baseline = own_packed_setp(text);
        const auto ast = test_helpers::parseModule(text);
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        const checker::Context context{
            .target = {.ptx_version = {9, 3}, .sm_version = 90},
            .instruction_range =
                baseline.functions.front().instruction_ranges.front()};
        for (int mutation = 0; mutation < 10; ++mutation) {
          SCOPED_TRACE(mutation);
          auto module = baseline;
          auto& value = std::get<ResolvedImmediate>(
              packed_setp_source(*module.functions.front().body.front(), first)
                  .value);
          const auto logical = bf16 ? ScalarType::BF16x2 : ScalarType::F16x2;
          if (mutation == 0)
            value.type = ScalarType::F32;
          if (mutation == 1)
            value.type = bf16 ? ScalarType::F16x2 : ScalarType::BF16x2;
          if (mutation == 2)
            value.bits = 0x100000000;
          if (mutation == 3)
            value.bits = 2;
          if (mutation == 4) {
            value.integer_source_bits = 0;
            // A retained evaluated zero cannot explain the current low word one.
          }
          if (mutation == 5)
            value.is_negative = true;
          if (mutation == 6) {
            value.type = logical;
            value.integer_source_bits = 0;
          }
          if (mutation == 7) {
            value.type = logical;
            value.integer_source_bits.reset();
            value.bits = 0x100000000;
          }
          if (mutation == 8) {
            value.type = logical;
            value.integer_source_bits.reset();
            value.is_negative = true;
          }
          if (mutation == 9) {
            value.bits = 0xffffffff;
            value.integer_source_bits = UINT64_MAX;
            value.is_negative = false;
          }
          const auto direct =
              module.functions.front().body.front()->check(context);
          const auto current = validateModule(module);
          const auto supplied = validateModule(*ast, module);
          EXPECT_EQ(bool(direct), mutation >= 9);
          EXPECT_EQ(bool(current), mutation >= 9);
          EXPECT_EQ(bool(supplied), mutation >= 9);
          if (mutation < 9) {
            ASSERT_FALSE(direct);
            ASSERT_FALSE(direct.error().empty());
            EXPECT_EQ(direct.error().front().range,
                      packed_setp_source(*module.functions.front().body.front(),
                                         first)
                          .locs.front());
            EXPECT_EQ(
                direct.error().front().kind,
                mutation < 2
                    ? checker::CheckDiagnosticKind::OperandTypeMismatch
                    : checker::CheckDiagnosticKind::ImmediateValueMismatch);
          }
          if (mutation == 4 || mutation == 6) {
            ASSERT_TRUE(value.integer_source_bits.has_value());
            EXPECT_EQ(*value.integer_source_bits, 0u);
          }
          if (mutation < 9) {
            const auto kind =
                mutation < 2
                    ? checker::CheckDiagnosticKind::OperandTypeMismatch
                    : checker::CheckDiagnosticKind::ImmediateValueMismatch;
            const auto range =
                packed_setp_source(*baseline.functions.front().body.front(),
                                   first)
                    .locs.front();
            for (const auto* result : {&current, &supplied}) {
              ASSERT_FALSE(*result);
              EXPECT_TRUE(std::ranges::any_of(
                  result->error(), [&](const auto& diagnostic) {
                    return diagnostic.kind == kind && diagnostic.range == range;
                  }));
            }
          }
          value = std::get<ResolvedImmediate>(
              packed_setp_source(*baseline.functions.front().body.front(),
                                 first)
                  .value);
          EXPECT_TRUE(module.functions.front().body.front()->check(context));
          EXPECT_TRUE(validateModule(module));
          EXPECT_TRUE(validateModule(*ast, module));
          // Cache-free current Bit32 outcomes require their own actual-parent evidence.
          // The scalar F16/Bit16 decision supplies no packed-word oracle.
        }
        auto logical_donor = own_packed_setp(
            packed_setp_module(bf16, boolean, first, "0f00000002"));
        auto logical_edit = baseline;
        packed_setp_source(*logical_edit.functions.front().body.front(), first)
            .value = packed_setp_source(
                         *logical_donor.functions.front().body.front(), first)
                         .value;
        EXPECT_TRUE(validateModule(logical_edit));
        EXPECT_TRUE(validateModule(*ast, logical_edit));
        expectPackedSetpLifetime(logical_edit, first, *ast);
        const auto changed = test_helpers::parseModule(
            packed_setp_module(bf16, boolean, first, "2"));
        ASSERT_MODULE_PARSE_SUCCEEDS(changed);
        ASSERT_TRUE(resolveAndValidateModule(*changed));
        const auto mismatch = validateModule(*changed, baseline);
        ASSERT_FALSE(mismatch);
        EXPECT_TRUE(std::any_of(
            mismatch.error().begin(), mismatch.error().end(),
            [](const auto& d) {
              return d.kind ==
                     checker::CheckDiagnosticKind::ModuleSourceMismatch;
            }));
      }
}

/** Synchronous observer verifies actual fields without retaining callback borrows. */
struct PackedSourceObserver final : detail::IReferenceObserver {
  /** Expected source addresses remain owned by the instruction throughout visiting. */
  const RegOrImm* first = nullptr;
  const RegOrImm* second = nullptr;
  /** Number of observed actual source fields, and accumulated validity. */
  size_t count = 0;
  bool valid = true;
  /** Inspect the active arm and its actual range during the callback only. */
  void reg_or_imm(const RegOrImm& value, std::span<const SourceRange> ranges,
                  checker::AddressSymbolResolutionPolicy) override {
    valid = valid && (count == 0 ? &value == first : &value == second) &&
            ranges.size() == 1 && ranges.front() != SourceRange{};
    if (const auto* word = std::get_if<ResolvedImmediate>(&value)) {
      checker::OperandView view{
          .field_id = "src",
          .actual_shape = checker::OperandShape::Immediate,
          .immediate_type = word->type,
          .immediate_bits = word->bits,
          .immediate_is_negative = word->is_negative,
          .locations = ranges,
          .integer_source_bits = word->integer_source_bits};
      valid = valid && view.immediate_bits == word->bits &&
              view.locations.data() == ranges.data();
    } else {
      const auto& reg = std::get<ResolvedRegisterRef>(value);
      checker::OperandView view{.field_id = "src",
                                .actual_shape = checker::OperandShape::Register,
                                .register_type = reg.declared_type,
                                .register_ref = &reg,
                                .locations = ranges};
      valid = valid && view.register_ref == &reg;
    }
    ++count;
  }
};

/** Real factory payloads and observer callbacks must share each migrated source field. */
TEST(PackedSetpSources, PublicMembersFactoryAndBorrowedViewsMatchActualArms) {
  static_assert(std::same_as<decltype(SetpF16x2{}.src1), WithLocs<RegOrImm>>);
  static_assert(std::same_as<decltype(SetpF16x2{}.src2), WithLocs<RegOrImm>>);
  static_assert(
      std::same_as<decltype(SetpF16x2Boolean{}.src1), WithLocs<RegOrImm>>);
  static_assert(
      std::same_as<decltype(SetpF16x2Boolean{}.src2), WithLocs<RegOrImm>>);
  static_assert(std::same_as<decltype(SetpBf16x2{}.src1), WithLocs<RegOrImm>>);
  static_assert(std::same_as<decltype(SetpBf16x2{}.src2), WithLocs<RegOrImm>>);
  static_assert(
      std::same_as<decltype(SetpBf16x2Boolean{}.src1), WithLocs<RegOrImm>>);
  static_assert(
      std::same_as<decltype(SetpBf16x2Boolean{}.src2), WithLocs<RegOrImm>>);
  for (bool bf16 : {false, true})
    for (bool boolean : {false, true})
      for (const auto spelling : {"b", "1", "0f7f817c01"}) {
        auto module =
            own_packed_setp(packed_setp_module(bf16, boolean, true, spelling));
        auto& instruction = *module.functions.front().body.front();
        PackedSourceObserver observer;
        observer.first = &packed_setp_source(instruction, true).value;
        observer.second = &packed_setp_source(instruction, false).value;
        instruction.visit_references(observer);
        EXPECT_TRUE(observer.valid);
        EXPECT_EQ(observer.count, 2u);
        EXPECT_EQ(*observer.first, packed_setp_source(instruction, true).value);
      }
}

/** Manual metadata uses existing scalar facts, without mnemonic authentication or new shape tags. */
TEST(PackedSetpSources, ManualMetadataFailsClosedAndKeepsAbsentDefaults) {
  using namespace checker;
  EXPECT_FALSE(OperandDescriptor{}.integer_immediate_type);
  const SourceRange range{{1, 1}, {1, 2}};
  for (const auto logical : {ScalarType::F16x2, ScalarType::BF16x2}) {
    OperandDescriptor descriptor{
        .target_field_id = "src",
        .type_expression = {.kind = OperandTypeExpressionKind::FixedScalar,
                            .fixed_scalar_type = logical},
        .role = OperandRole::Source,
        .access = OperandAccess::Read,
        .allowed_shapes = OperandShape::Register | OperandShape::Immediate,
        .integer_immediate_type = ScalarType::B32};
    OperandView view{.field_id = "src",
                     .actual_shape = OperandShape::Immediate,
                     .immediate_type = ScalarType::B32,
                     .immediate_bits = 1,
                     .immediate_is_negative = false,
                     .locations = std::span{&range, 1},
                     .integer_source_bits = 1};
    const Context context{.instruction_range = range};
    const auto check = [&] {
      return check_operands(std::span{&descriptor, 1}, {}, std::span{&view, 1},
                            {}, context);
    };
    ASSERT_TRUE(check());
    view.immediate_type = logical;
    view.integer_source_bits.reset();
    ASSERT_TRUE(check());
    view.immediate_type = ScalarType::B32;
    view.immediate_bits.reset();
    const auto no_bits = check();
    ASSERT_FALSE(no_bits);
    ASSERT_FALSE(no_bits.error().empty());
    EXPECT_EQ(no_bits.error().front().kind,
              CheckDiagnosticKind::ImmediateValueMismatch);
    view.immediate_bits = 1;
    view.immediate_type.reset();
    const auto no_type = check();
    ASSERT_FALSE(no_type);
    ASSERT_FALSE(no_type.error().empty());
    EXPECT_EQ(no_type.error().front().kind,
              CheckDiagnosticKind::ImmediateValueMismatch);
    view.immediate_type = ScalarType::B32;
    view.integer_source_bits = 1;
    const auto valid = descriptor;
    for (const auto actual :
         {OperandShape::Immediate, OperandShape::Register}) {
      view.actual_shape = actual;
      for (int mutation = 0; mutation < 16; ++mutation) {
        descriptor = valid;
        SCOPED_TRACE(mutation);
        if (mutation < 6) {
          constexpr std::array invalid{ScalarType::S16,   ScalarType::U16,
                                       ScalarType::F16x2, ScalarType::B8,
                                       ScalarType::B16,   ScalarType::Invalid};
          descriptor.integer_immediate_type = invalid[mutation];
        }
        if (mutation == 6)
          descriptor.type_expression.fixed_scalar_type = ScalarType::F32;
        if (mutation == 7)
          descriptor.type_expression.fixed_scalar_type = ScalarType::B32;
        if (mutation == 8)
          descriptor.type_expression.fixed_scalar_type = ScalarType::F16;
        if (mutation == 9)
          descriptor.type_expression.kind =
              OperandTypeExpressionKind::ModifierField;
        if (mutation == 10)
          descriptor.access = OperandAccess::Write;
        if (mutation == 11)
          descriptor.access = OperandAccess::ReadWrite;
        if (mutation == 12)
          descriptor.role = OperandRole::Destination;
        if (mutation == 13)
          descriptor.allowed_shapes = OperandShape::Register;
        if (mutation == 14)
          descriptor.allowed_shapes = OperandShape::Immediate;
        if (mutation == 15)
          descriptor.allowed_shapes =
              OperandShape::Vector | OperandShape::Immediate;
        const auto rejected = check();
        ASSERT_FALSE(rejected);
        ASSERT_FALSE(rejected.error().empty());
        EXPECT_EQ(rejected.error().front().kind,
                  CheckDiagnosticKind::RuleViolation);
        EXPECT_EQ(rejected.error().front().range, range);
      }
    }
    descriptor = valid;
    descriptor.integer_immediate_type.reset();
    view.actual_shape = OperandShape::Immediate;
    view.immediate_type = logical;
    view.integer_source_bits.reset();
    EXPECT_TRUE(check());
  }
}

/** Bare words reuse the common helper; numeric constructors remain unavailable. */
TEST(PackedSetpSources, RetainsRootSyntaxAndModifierBoundaries) {
  for (bool bf16 : {false, true})
    for (bool boolean : {false, true}) {
      auto healthy = own_packed_setp(
          packed_setp_module(bf16, boolean, true, "0f3fc03fc0"));
      EXPECT_TRUE(validateModule(healthy));
      for (bool first : {false, true}) {
        auto mixed = packed_setp_module(bf16, boolean, first, "1");
        const std::string_view sibling = first ? ",b" : "b,";
        const auto pos = mixed.rfind(sibling);
        ASSERT_NE(pos, std::string::npos);
        mixed.replace(pos, sibling.size(),
                      first ? ",0f3fc03fc0" : "0f3fc03fc0,");
        auto module = own_packed_setp(mixed);
        EXPECT_EQ(std::get<ResolvedImmediate>(
                      packed_setp_source(*module.functions.front().body.front(),
                                         first)
                          .value)
                      .type,
                  ScalarType::B32);
        EXPECT_EQ(std::get<ResolvedImmediate>(
                      packed_setp_source(*module.functions.front().body.front(),
                                         !first)
                          .value)
                      .type,
                  bf16 ? ScalarType::BF16x2 : ScalarType::F16x2);
        auto ftz = mixed;
        ftz.insert(ftz.rfind(bf16 ? ".bf16x2" : ".f16x2"), ".ftz");
        const auto ast = test_helpers::parseModule(ftz);
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        EXPECT_EQ(bool(resolveAndValidateModule(*ast)), !bf16);
      }
      const auto no_half_hex = test_helpers::parseModule(
          packed_setp_module(bf16, boolean, true, "0h3c00"));
      EXPECT_FALSE(no_half_hex && no_half_hex.diagnostics.empty());
      for (const auto text :
           {"1.0", "1.5", "0d3ff0000000000000", "+0f3fc03fc0", "-0f3fc03fc0",
            "(0f3fc03fc0)", "0f3fc03fc0+1.0", "(1?0f3fc03fc0:0f3fc03fc0)"}) {
        const auto ast = test_helpers::parseModule(
            packed_setp_module(bf16, boolean, true, text));
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        EXPECT_FALSE(resolveAndValidateModule(*ast));
      }
    }
}

/** Engaged manual resolver metadata must describe the existing RegOrImm field before dispatch. */
TEST(PackedSetpSources,
     ManualResolverChecksActualFieldKindBeforeRegisterDispatch) {
  using namespace check_end;
  ResolvedOperandBindingDescriptor binding{
      .target_field_id = "src",
      .type_expression = {.kind =
                              checker::OperandTypeExpressionKind::FixedScalar,
                          .fixed_scalar_type = ScalarType::F16x2},
      .role = checker::OperandRole::Source,
      .access = checker::OperandAccess::Read,
      .allowed_shapes =
          checker::OperandShape::Register | checker::OperandShape::Immediate,
      .integer_immediate_type = ScalarType::B32};
  for (const auto actual : {"%h0", "1", "0f3fc03fc0"}) {
    const auto parsed = test_helpers::parseInstruction(
        std::string("setp.eq.f16x2 p|q,h,") + actual + ";");
    ASSERT_TRUE(parsed && parsed.diagnostics.empty());
    ResolvedFieldDescriptor field{.field_id = "src",
                                  .value_kind = ResolvedValueKind::RegOrImm};
    const auto resolved = detail::resolve_operand_value(
        field, binding, parsed->operands.back(), {}, nullptr);
    ASSERT_TRUE(resolved) << resolved.error().message;
    for (const auto wrong :
         {ResolvedValueKind::Register, ResolvedValueKind::Immediate,
          ResolvedValueKind::ValueVector}) {
      field.value_kind = wrong;
      const auto result = detail::resolve_operand_value(
          field, binding, parsed->operands.back(), {}, nullptr);
      ASSERT_FALSE(result);
      EXPECT_EQ(result.error().range,
                syntax_ast::sourceRange(parsed->operands.back()));
      EXPECT_NE(result.error().message.find("integer immediate alternative"),
                std::string::npos);
    }
  }
}

/** Register declarations stay independent of the logical comparison format and raw immediate arm. */
TEST(PackedSetpSources,
     RetainsPhysicalRegisterDomainsAndUnknownStandaloneFacts) {
  for (bool bf16 : {false, true}) {
    auto module = own_packed_setp(packed_setp_module(bf16, false, true, "b"));
    auto& source =
        packed_setp_source(*module.functions.front().body.front(), true);
    const auto saved = source;
    const checker::Context ctx{
        .target = {.ptx_version = {9, 3}, .sm_version = 90}};
    for (const auto type : {ScalarType::S32, ScalarType::U32, ScalarType::F32,
                            ScalarType::B64, ScalarType::BF16x2}) {
      source = saved;
      auto& reg = std::get<ResolvedRegisterRef>(source.value);
      reg.declared_type = type;
      EXPECT_FALSE(module.functions.front().body.front()->check(ctx));
      EXPECT_FALSE(validateModule(module));
    }
    source = saved;
    std::get<ResolvedRegisterRef>(source.value).register_class =
        ResolvedRegisterClass::Predicate;
    EXPECT_FALSE(module.functions.front().body.front()->check(ctx));
    source = saved;
    std::get<ResolvedRegisterRef>(source.value).vector_width = 2;
    EXPECT_FALSE(module.functions.front().body.front()->check(ctx));
    source = saved;
    auto& unknown = std::get<ResolvedRegisterRef>(source.value);
    unknown.symbol_id.reset();
    unknown.declared_type.reset();
    EXPECT_TRUE(module.functions.front().body.front()->check(ctx));
    source = saved;
    EXPECT_TRUE(validateModule(module));
    const auto special = test_helpers::parseModule(
        packed_setp_module(bf16, false, true, "%tid.x"));
    ASSERT_MODULE_PARSE_SUCCEEDS(special);
    EXPECT_FALSE(resolveAndValidateModule(*special));
    auto whole = packed_setp_module(bf16, false, true, "V");
    whole.insert(whole.find("setp.eq"), ".reg .v2 .b32 V; ");
    const auto vector = test_helpers::parseModule(whole);
    ASSERT_MODULE_PARSE_SUCCEEDS(vector);
    EXPECT_FALSE(resolveAndValidateModule(*vector));
  }
}

/** Current declaration identity constrains references independently of retained syntax. */
TEST(PackedSetpSources,
     CurrentRegisterIdentityAndCoherentRetargetRemainIndependent) {
  for (bool bf16 : {false, true})
    for (bool boolean : {false, true})
      for (bool first : {false, true}) {
        auto text = packed_setp_module(bf16, boolean, first, "b");
        text.replace(text.find(".b32 b;"), std::string_view(".b32 b;").size(),
                     ".b32 b,c;");
        const auto ast = test_helpers::parseModule(text);
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        auto baseline = own_packed_setp(text);
        auto donor_text = packed_setp_module(bf16, boolean, first, "c");
        donor_text.replace(donor_text.find(".b32 b;"),
                           std::string_view(".b32 b;").size(), ".b32 b,c;");
        auto donor = own_packed_setp(donor_text);
        const auto current =
            packed_setp_source(*donor.functions.front().body.front(), first)
                .value;
        auto coherent = baseline;
        packed_setp_source(*coherent.functions.front().body.front(), first)
            .value = current;
        EXPECT_TRUE(validateModule(coherent));
        EXPECT_TRUE(validateModule(*ast, coherent));
        expectPackedSetpLifetime(coherent, first, *ast);
        // Missing direct locations use the containing instruction range; reference facts remain checked.
        auto no_direct_location = baseline;
        packed_setp_source(*no_direct_location.functions.front().body.front(),
                           first)
            .locs.clear();
        EXPECT_EQ(no_direct_location.source_identity, baseline.source_identity);
        EXPECT_TRUE(validateModule(no_direct_location));
        EXPECT_TRUE(validateModule(*ast, no_direct_location));
        for (int mutation = 0; mutation < 3; ++mutation) {
          SCOPED_TRACE(mutation);
          auto bad = baseline;
          auto& source =
              packed_setp_source(*bad.functions.front().body.front(), first);
          ASSERT_EQ(source.locs.size(), 1u);
          auto& reg = std::get<ResolvedRegisterRef>(source.value);
          if (mutation == 0)
            reg.symbol_id.reset();
          if (mutation == 1)
            reg.symbol_id = std::get<ResolvedRegisterRef>(current).symbol_id;
          if (mutation == 2)
            reg.register_class = ResolvedRegisterClass::Predicate;
          EXPECT_FALSE(validateModule(bad));
          EXPECT_FALSE(validateModule(*ast, bad));
        }
      }
  auto native = own_packed_setp(packed_setp_module(false, false, true, "h"));
  EXPECT_TRUE(validateModule(native));
}

/** Partial context is deferred rather than certified; complete targets keep the original floors. */
TEST(PackedSetpSources, PreservesAvailabilityMissingAndUnknownContext) {
  for (bool bf16 : {false, true})
    for (bool boolean : {false, true})
      for (bool first : {false, true}) {
        const auto text = packed_setp_module(bf16, boolean, first, "1");
        auto owned = own_packed_setp(text);
        for (const std::string token : {".version 9.3\n", ".target sm_90\n"}) {
          auto partial = packed_setp_module(bf16, boolean, first, "b");
          partial.erase(partial.find(token), token.size());
          const auto ast = test_helpers::parseModule(partial);
          ASSERT_MODULE_PARSE_SUCCEEDS(ast);
          auto module = resolveModuleOnly(*ast);
          ASSERT_TRUE(module);
          EXPECT_TRUE(validateModule(*module,
                                     ModuleValidationPolicy::AvailableContext));
          EXPECT_TRUE(validateModule(*ast, *module,
                                     ModuleValidationPolicy::AvailableContext));
          auto& field = packed_setp_source(
              *module->functions.front().body.front(), first);
          const auto original = field.value;
          auto& reg = std::get<ResolvedRegisterRef>(field.value);
          reg.declared_type = ScalarType::F64;
          EXPECT_FALSE(validateModule(
              *module, ModuleValidationPolicy::AvailableContext));
          EXPECT_FALSE(validateModule(
              *ast, *module, ModuleValidationPolicy::AvailableContext));
          field.value = original;
          EXPECT_TRUE(validateModule(*module,
                                     ModuleValidationPolicy::AvailableContext));
          EXPECT_TRUE(validateModule(*ast, *module,
                                     ModuleValidationPolicy::AvailableContext));
          const auto strict = validateModule(*module);
          ASSERT_FALSE(strict);
          EXPECT_TRUE(std::any_of(
              strict.error().begin(), strict.error().end(), [](const auto& d) {
                return d.kind ==
                       checker::CheckDiagnosticKind::MissingValidationContext;
              }));
        }
        auto unknown = owned;
        for (auto& region : unknown.header.regions)
          for (auto& option : region.target_options)
            if (option.starts_with("sm_"))
              option = "sm_999";
        for (auto& f : unknown.functions)
          f.source_target = "sm_999";
        const auto rejected = validateModule(unknown);
        ASSERT_FALSE(rejected);
        EXPECT_TRUE(std::any_of(
            rejected.error().begin(), rejected.error().end(),
            [](const auto& d) {
              return d.kind == checker::CheckDiagnosticKind::UnknownTarget;
            }));
        checker::Context good{
            .target = {.ptx_version = bf16 ? checker::PtxVersion{7, 8}
                                           : checker::PtxVersion{4, 2},
                       .sm_version = bf16 ? 90u : 53u}};
        EXPECT_TRUE(owned.functions.front().body.front()->check(good));
        auto low = good;
        low.target.ptx_version =
            bf16 ? checker::PtxVersion{7, 7} : checker::PtxVersion{4, 1};
        EXPECT_FALSE(owned.functions.front().body.front()->check(low));
        low = good;
        --low.target.sm_version;
        EXPECT_FALSE(owned.functions.front().body.front()->check(low));
      }
}

/** Inspect actual generated check arguments under the required test-only linker interception. */
TEST(PackedSetpSources,
     ActualGeneratedOperandViewsRetainMemberIdentityAndFacts) {
#if !defined(PTX_PACKED_SETP_CHECKER_WRAP)
  GTEST_SKIP() << "Requires PTX_PACKED_SETP_CHECKER_WRAP and the recorded "
                  "--wrap checker symbol.";
#else
  for (bool bf16 : {false, true})
    for (bool boolean : {false, true})
      for (bool first : {false, true})
        for (const auto spelling :
             {"b", "h", "1", "-1", "0x100000001", "0f7f817c01"}) {
          if (bf16 && std::string_view(spelling) == "h")
            continue;
          const auto text = packed_setp_module(bf16, boolean, first, spelling);
          auto owner = own_packed_setp(text);
          auto copied = owner;
          copied.functions.front().body.front() =
              owner.functions.front().body.front()->clone();
          for (auto* current : {&owner, &copied}) {
            ASSERT_EQ(current->functions.front().instruction_ranges.size(), 1u);
            auto& instruction = *current->functions.front().body.front();
            auto& field = packed_setp_source(instruction, first);
            packed_setp_test_wrap::Capture capture;
            capture.member = &field;
            capture.field = first ? "src1" : "src2";
            capture.logical = bf16 ? ScalarType::BF16x2 : ScalarType::F16x2;
            capture.range = packed_setp_test_wrap::token_range(text, first);
            const std::string_view word(spelling);
            capture.immediate = word != "b" && word != "h";
            capture.type = word == "h" ? ScalarType::F16x2 : ScalarType::B32;
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
            const checker::Context context{
                .target = {.ptx_version = {9, 3}, .sm_version = 90},
                .instruction_range =
                    current->functions.front().instruction_ranges.front()};
            checker::CheckResult checked;
            {
              packed_setp_test_wrap::Scope observe(capture);
              checked = instruction.check(context);
            }
            EXPECT_TRUE(checked);
            EXPECT_TRUE(capture.valid);
            EXPECT_EQ(capture.calls, 1u);
            const auto expected_kind =
                bf16 ? (boolean ? SetpBf16x2Boolean::kind : SetpBf16x2::kind)
                     : (boolean ? SetpF16x2Boolean::kind : SetpF16x2::kind);
            EXPECT_EQ(instruction.instruction_kind(), expected_kind);
          }
        }
#endif
}

/** Current known references must still match real declarations, not a leftover numbered cache. */
TEST(PackedSetpSources,
     KnownRegisterFamiliesComponentsAndBothDatasetsStayJoined) {
  for (bool bf16 : {false, true})
    for (bool boolean : {false, true})
      for (bool first : {false, true}) {
        auto text = packed_setp_module(bf16, boolean, first, "%r1");
        text.insert(text.find("setp.eq"),
                    ".reg .b32 %r<2>,c; .reg .v2 .b32 V<2>; ");
        const auto ast = test_helpers::parseModule(text);
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        auto owner = own_packed_setp(text);
        auto& field =
            packed_setp_source(*owner.functions.front().body.front(), first);
        const auto valid = std::get<ResolvedRegisterRef>(field.value);
        ASSERT_TRUE(valid.symbol_id);
        ASSERT_TRUE(valid.parameterized_index);
        EXPECT_EQ(*valid.parameterized_index, 1u);
        auto cleared = valid;
        cleared.index.reset();
        field.value = cleared;
        EXPECT_TRUE(validateModule(owner));
        EXPECT_TRUE(validateModule(*ast, owner));
        field.value = valid;
        ASSERT_EQ(field.locs.size(), 1u);
        const auto saved_range = field.locs.front();
        const checker::Context direct_context{
            .target = {.ptx_version = {9, 3}, .sm_version = 90},
            .instruction_range =
                owner.functions.front().instruction_ranges.front()};
        for (const auto type :
             {ScalarType::B8, ScalarType::B16, ScalarType::B32, ScalarType::B64,
              ScalarType::B128, ScalarType::F16, ScalarType::F32,
              ScalarType::F64, ScalarType::S32, ScalarType::U32,
              ScalarType::F16x2, ScalarType::BF16x2, ScalarType::E4m3x4}) {
          auto cached = valid;
          cached.declared_type = type;
          field.value = cached;
          const bool direct_allowed =
              type == ScalarType::B32 || (!bf16 && type == ScalarType::F16x2);
          EXPECT_EQ(
              bool(owner.functions.front().body.front()->check(direct_context)),
              direct_allowed);
          if (type != ScalarType::B32) {
            EXPECT_FALSE(validateModule(owner));
            EXPECT_FALSE(validateModule(*ast, owner));
          }
          field.value = valid;
          EXPECT_TRUE(validateModule(owner));
          EXPECT_TRUE(validateModule(*ast, owner));
        }
        for (unsigned mutation = 0; mutation < 13; ++mutation) {
          auto bad = valid;
          if (mutation == 0)
            bad.symbol_id.reset();
          if (mutation == 1)
            bad.symbol_id = binding::SymbolId{UINT32_MAX};
          if (mutation == 2)
            bad.symbol_id = owner.functions.front().symbol_id;
          if (mutation == 3)
            bad.parameterized_index.reset();
          if (mutation == 4)
            bad.parameterized_index = 0;
          if (mutation == 5)
            bad.parameterized_index = 2;
          if (mutation == 6)
            bad.parameterized_index = UINT32_MAX;
          if (mutation == 7)
            bad.spelling = "%r0";
          if (mutation == 8)
            bad.spelling = "%r2";
          if (mutation == 9)
            bad.spelling = "absent";
          if (mutation == 10)
            bad.register_class = ResolvedRegisterClass::Predicate;
          if (mutation == 11)
            bad.declared_type.reset();
          if (mutation == 12)
            bad.vector_width = 2;
          field.value = bad;
          const auto current = validateModule(owner);
          const auto supplied = validateModule(*ast, owner);
          ASSERT_FALSE(current);
          ASSERT_FALSE(supplied);
          for (const auto* result : {&current, &supplied})
            EXPECT_TRUE(std::any_of(
                result->error().begin(), result->error().end(),
                [&](const auto& diagnostic) {
                  return diagnostic.kind == checker::CheckDiagnosticKind::
                                                ModuleSourceMismatch &&
                         diagnostic.range == saved_range;
                }));
          field.value = valid;
          EXPECT_TRUE(validateModule(owner));
          EXPECT_TRUE(validateModule(*ast, owner));
        }
        auto component_text = packed_setp_module(bf16, boolean, first, "V1.x");
        component_text.insert(component_text.find("setp.eq"),
                              ".reg .v2 .b32 V<2>; ");
        const auto component_ast = test_helpers::parseModule(component_text);
        ASSERT_MODULE_PARSE_SUCCEEDS(component_ast);
        auto component_owner = own_packed_setp(component_text);
        auto& selected = packed_setp_source(
            *component_owner.functions.front().body.front(), first);
        const auto component = std::get<ResolvedRegisterRef>(selected.value);
        ASSERT_TRUE(component.component);
        ASSERT_TRUE(component.component->selector);
        ASSERT_TRUE(component.parameterized_index);
        for (unsigned mutation = 0; mutation < 9; ++mutation) {
          auto bad = component;
          if (mutation == 0)
            bad.component->lane = 2;
          if (mutation == 1)
            bad.component->declaration_width = 4;
          if (mutation == 2)
            bad.component->base_range.end.column++;
          if (mutation == 3)
            bad.component->range.end.column--;
          if (mutation == 4)
            bad.component->selector->spelling = ".y";
          if (mutation == 5)
            bad.component->selector->range.start.column++;
          if (mutation == 6)
            bad.vector_width = 2;
          if (mutation == 7)
            bad.parameterized_index = 0;
          if (mutation == 8)
            bad.component->selector.reset();
          selected.value = bad;
          EXPECT_FALSE(validateModule(component_owner));
          EXPECT_FALSE(validateModule(*component_ast, component_owner));
          selected.value = component;
          EXPECT_TRUE(validateModule(component_owner));
          EXPECT_TRUE(validateModule(*component_ast, component_owner));
        }
        auto donor_text = packed_setp_module(bf16, boolean, first, "V0.y");
        donor_text.insert(donor_text.find("setp.eq"), ".reg .v2 .b32 V<2>; ");
        auto donor = own_packed_setp(donor_text);
        selected.value =
            packed_setp_source(*donor.functions.front().body.front(), first)
                .value;
        EXPECT_TRUE(validateModule(component_owner));
        EXPECT_TRUE(validateModule(*component_ast, component_owner));
        selected.value = component;
        const auto changed_ast = test_helpers::parseModule(donor_text);
        ASSERT_MODULE_PARSE_SUCCEEDS(changed_ast);
        ASSERT_TRUE(resolveAndValidateModule(*changed_ast));
        const auto mismatch = validateModule(*changed_ast, component_owner);
        ASSERT_FALSE(mismatch);
        EXPECT_TRUE(std::any_of(
            mismatch.error().begin(), mismatch.error().end(),
            [](const auto& diagnostic) {
              return diagnostic.kind ==
                     checker::CheckDiagnosticKind::ModuleSourceMismatch;
            }));
      }
}

/** Source failures retain their actual stage and cannot masquerade as owned producer proofs. */
TEST(PackedSetpSources, SourceFailuresAndRegisterDomainsHaveQualifiedCauses) {
  for (bool bf16 : {false, true})
    for (bool boolean : {false, true})
      for (bool first : {false, true}) {
        for (const auto type : {"b8", "b16", "b64", "u32", "s32", "f32", "f64",
                                "bf16x2", "pred"}) {
          auto text = packed_setp_module(bf16, boolean, first, "bad");
          text.insert(text.find("setp.eq"),
                      ".reg ." + std::string(type) + " bad; ");
          const auto ast = test_helpers::parseModule(text);
          ASSERT_MODULE_PARSE_SUCCEEDS(ast);
          const auto rejected = resolveAndValidateModule(*ast);
          ASSERT_FALSE(rejected);
          ASSERT_FALSE(rejected.error().empty());
          const auto start = text.rfind("bad");
          ASSERT_NE(start, std::string::npos);
          SourcePos position{1, 1};
          for (const char character : std::string_view(text).substr(0, start)) {
            if (character == '\n') {
              ++position.line;
              position.column = 1;
            } else
              ++position.column;
          }
          const SourceRange expected_range{
              position, {position.line, position.column + 3}};
          const bool instruction_only = std::string_view(type) == "bf16x2";
          if (!instruction_only)
            EXPECT_TRUE(
                std::any_of(rejected.error().begin(), rejected.error().end(),
                            [&](const auto& diagnostic) {
                              return diagnostic.range == expected_range &&
                                     (std::string_view(type) == "pred" ||
                                      diagnostic.checker_kind ==
                                          checker::CheckDiagnosticKind::
                                              OperandTypeMismatch);
                            }));
          EXPECT_TRUE(std::any_of(
              rejected.error().begin(), rejected.error().end(),
              [&](const auto& diagnostic) {
                return diagnostic.stage() ==
                       (instruction_only
                            ? ResolveDiagnosticStage::DeclarationSemantics
                        : std::string_view(type) == "pred"
                            ? ResolveDiagnosticStage::Resolution
                            : ResolveDiagnosticStage::Checking);
              }));
          // Rejected source has no strict owner; current-reference tests use a separate healthy producer.
        }
      }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
