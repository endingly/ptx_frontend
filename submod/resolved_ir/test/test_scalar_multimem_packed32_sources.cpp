#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#ifdef PTX_SCALAR_MM_CAPTURE_GENERATED_VIEWS
namespace ptx_frontend::resolved_ir::scalar_mm_generated {
/** Current member borrowed only during one enclosed actual generated check. */
const WithLocs<RegOrImm>* expected = nullptr;
/** Selected owner type, independent from mutable payload mirrors. */
ScalarType logical = ScalarType::Invalid;
/** Count actual generated src projections, not hand-built visitor views. */
unsigned observed = 0;
/** Track all pointer, field, cached value and range mirrors synchronously. */
bool mirrors = true;
#define SCALAR_MM_PARAMETERS                                       \
  std::span<const checker::OperandDescriptor> descriptors,         \
      std::span<const checker::FieldView> fields,                  \
      std::span<const checker::OperandView> views,                 \
      std::span<const checker::OperandTypeCompatibilityDescriptor> \
          compatibility, const checker::Context &context,          \
      const MatrixInstructionDescriptor *matrix
/** Forward to the real matching library after test-only interception. */
extern "C" checker::CheckResult original_operands(SCALAR_MM_PARAMETERS) asm(
    "__real__ZN12ptx_frontend11resolved_ir7checker14check_"
    "operandsESt4spanIKNS1_17OperandDescriptorELm18446744073709551615EES2_"
    "IKNS1_9FieldViewELm18446744073709551615EES2_IKNS1_"
    "11OperandViewELm18446744073709551615EES2_IKNS1_"
    "34OperandTypeCompatibilityDescriptorELm18446744073709551615EERKNS1_"
    "7ContextEPKNS0_27MatrixInstructionDescriptorE");
/** Link-only shim observing actual generated operands; no production API hook. */
extern "C" checker::CheckResult generated_operands(SCALAR_MM_PARAMETERS) asm(
    "__wrap__ZN12ptx_frontend11resolved_ir7checker14check_"
    "operandsESt4spanIKNS1_17OperandDescriptorELm18446744073709551615EES2_"
    "IKNS1_9FieldViewELm18446744073709551615EES2_IKNS1_"
    "11OperandViewELm18446744073709551615EES2_IKNS1_"
    "34OperandTypeCompatibilityDescriptorELm18446744073709551615EERKNS1_"
    "7ContextEPKNS0_27MatrixInstructionDescriptorE");
/** Consume all borrowed arguments before forwarding the unmodified real checker result. */
checker::CheckResult generated_operands(SCALAR_MM_PARAMETERS) {
  if (expected)
    for (const auto& view : views)
      if (view.field_id == "src") {
        ++observed;
        const auto descriptor =
            std::ranges::find(descriptors, std::string_view("src"),
                              &checker::OperandDescriptor::target_field_id);
        const auto type_field = std::ranges::find(
            fields, std::string_view("type"), &checker::FieldView::field_id);
        bool valid = descriptor != descriptors.end() &&
                     type_field != fields.end() &&
                     type_field->scalar_type == logical &&
                     descriptor->type_expression.kind ==
                         checker::OperandTypeExpressionKind::ModifierField &&
                     descriptor->type_expression.modifier_field_id == "type" &&
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
#undef SCALAR_MM_PARAMETERS
}  // namespace ptx_frontend::resolved_ir::scalar_mm_generated
#endif

namespace ptx_frontend::resolved_ir {
namespace {
/** One existing scalar source field; each retains the same two selected packed types. */
struct ScalarPackedMmCase {
  /** Existing final class identity, independent of selected logical type. */
  InstructionKind kind;
  /** Operation with '@' for the selected type and '$' for the one source. */
  std::string_view operation;
};
/** Three existing source fields, each with two selected logical type branches. */
constexpr std::array cases{
    ScalarPackedMmCase{MultimemStFloatPackedNoneScalarDefaultWeak::kind,
                       "multimem.st.weak.@ [mm],$;"},
    ScalarPackedMmCase{MultimemStFloatPackedNoneScalarDefaultOrdered::kind,
                       "multimem.st.release.gpu.@ [mm],$;"},
    ScalarPackedMmCase{MultimemRedFloatPackedAddScalarDefaultRed::kind,
                       "multimem.red.relaxed.gpu.add.@ [mm],$;"},
};
/** Selected whole-word exact-source controls; no numeric conversion oracle is constructed. */
struct PackedMmWord {
  /** Bare unsigned 0f spelling from the existing packed-word policy. */
  std::string_view source;
  /** Entire written 32-bit word, including encoded sign/NaN bits. */
  uint32_t bits;
};
/** Selected existing whole-word controls; values are raw bits, never numeric conversions. */
constexpr std::array words{
    PackedMmWord{"0f00000000", 0x00000000},
    PackedMmWord{"0f80000000", 0x80000000},
    PackedMmWord{"0f7f800001", 0x7f800001},
    PackedMmWord{"0f7fc01234", 0x7fc01234},
    PackedMmWord{"0fffffffff", 0xffffffff},
    PackedMmWord{"0f3e003e00", 0x3e003e00},
    PackedMmWord{"0f3fc03fc0", 0x3fc03fc0},
};

/** Borrow an actual changed member only during the live instruction owner's lifetime. */
WithLocs<RegOrImm>& scalarPackedMmSource(Instruction& instruction) {
  if (auto* form = dynamic_cast<MultimemStFloatPackedNoneScalarDefaultWeak*>(
          &instruction))
    return form->src;
  if (auto* form = dynamic_cast<MultimemStFloatPackedNoneScalarDefaultOrdered*>(
          &instruction))
    return form->src;
  return dynamic_cast<MultimemRedFloatPackedAddScalarDefaultRed&>(instruction)
      .src;
}

/** Build a legal complete profile without changing any unrelated selected control. */
std::string scalarPackedMmText(const ScalarPackedMmCase& item, ScalarType type,
                               std::string_view operand,
                               std::string_view extra = "",
                               std::string_view target = "sm_90") {
  std::string operation(item.operation);
  operation.replace(operation.find('@'), 1,
                    type == ScalarType::F16x2 ? "f16x2" : "bf16x2");
  operation.replace(operation.find('$'), 1, operand);
  return ".version 9.3\n.target " + std::string(target) +
         "\n.address_size 64\n.global .align 16 .b8 mm[256];\n.entry kernel(){ "
         ".reg .b32 b; .reg .f16x2 native; .reg .v2 .b32 V; " +
         std::string(extra) + operation + " }";
}

/** Return only owning state after real source, parser and AST destruction. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> ownedScalarPackedMm(
    std::string source, bool strict = true, size_t expected_instructions = 1) {
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  EXPECT_TRUE(ast && ast.diagnostics.empty()) << source;
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  auto result =
      strict ? resolveAndValidateModule(*ast) : resolveModuleOnly(*ast);
  if (result &&
      (result->functions.size() != 1 ||
       result->functions.front().body.size() != expected_instructions ||
       !result->functions.front().body.front())) {
    ADD_FAILURE() << "Packed multimem fixture must own one exact instruction: "
                  << source;
    return std::unexpected(ModuleResolveDiagnostics{});
  }
  return result;
}

/** Derive an expected use range from the independently written source fixture. */
SourceRange scalarPackedMmRange(std::string_view source,
                                std::string_view operand) {
  const auto offset = source.rfind(operand);
  if (offset == std::string_view::npos)
    return {};
  SourcePos position{1, 1};
  for (const char character : source.substr(0, offset)) {
    if (character == '\n') {
      ++position.line;
      position.column = 1;
    } else
      ++position.column;
  }
  return {
      position,
      {position.line, position.column + static_cast<int32_t>(operand.size())}};
}

/** Supply coherent complete availability facts independently of every payload mutation. */
checker::Context scalarPackedMmContext(std::string_view target = "sm_90") {
  const auto profile = base::find_target_profile(target);
  if (!profile)
    throw std::logic_error("Missing selected multimem target");
  return {
      .target = {.ptx_version = {9, 3},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
}

/** Preserve known reference failure cause and exact use location through both module APIs. */
void expectScalarMmReferenceFailure(const checker::CheckResult& result,
                                    SourceRange range, bool overflow = false) {
  ASSERT_FALSE(result);
  EXPECT_TRUE(std::ranges::any_of(result.error(), [&](const auto& diagnostic) {
    return diagnostic.kind ==
               checker::CheckDiagnosticKind::ModuleSourceMismatch &&
           diagnostic.range == range &&
           diagnostic.message.find(
               overflow ? "invalid parameterized member index"
                        : "register metadata disagrees with its owned "
                          "declaration") != std::string::npos;
  }));
}

/** Inspect the actual owner member synchronously without retaining callback references. */
struct ScalarPackedMmObserver final : detail::IReferenceObserver {
  /** Borrowed only for the duration of visit_references. */
  const WithLocs<RegOrImm>& expected;
  /** Selected logical type, kept independent of the actual current alternative. */
  ScalarType selected;
  /** Number of callbacks for exactly this owner member address. */
  size_t matches = 0;
  /** Bind one actual owner member before synchronous traversal. */
  ScalarPackedMmObserver(const WithLocs<RegOrImm>& source, ScalarType type)
      : expected(source), selected(type) {}
  /** Compare the payload address and all location mirrors while the callback is active. */
  void reg_or_imm(const RegOrImm& value, std::span<const SourceRange> ranges,
                  checker::AddressSymbolResolutionPolicy) override {
    if (&value == &expected.value) {
      ++matches;
      EXPECT_TRUE(std::ranges::equal(ranges, expected.locs));
      EXPECT_EQ(ranges.data(), expected.locs.data());
      checker::OperandDescriptor descriptor{
          .target_field_id = "src",
          .type_expression =
              {.kind = checker::OperandTypeExpressionKind::FixedScalar,
               .fixed_scalar_type = selected},
          .role = checker::OperandRole::Source,
          .access = checker::OperandAccess::Read,
          .allowed_shapes = checker::OperandShape::Register |
                            checker::OperandShape::Immediate};
      checker::OperandView view{
          .field_id = "src", .actual_shape = checker::OperandShape::Immediate};
      view.locations = ranges;
      if (const auto* word = std::get_if<ResolvedImmediate>(&value)) {
        EXPECT_EQ(word->type, selected);
        EXPECT_FALSE(word->integer_source_bits);
        EXPECT_FALSE(word->is_negative);
        EXPECT_EQ(word->bits >> 32, 0u);
        view.immediate_type = word->type;
        view.immediate_bits = word->bits;
        view.immediate_is_negative = word->is_negative;
        view.integer_source_bits = word->integer_source_bits;
      } else {
        const auto* ref = std::get_if<ResolvedRegisterRef>(&value);
        ASSERT_NE(ref, nullptr);
        EXPECT_TRUE(ref->symbol_id);
        view.actual_shape = checker::OperandShape::Register;
        view.register_type = ref->declared_type;
        view.register_symbol_id = ref->symbol_id;
        view.register_class = ref->register_class;
        view.register_ref = ref;
        view.register_vector_width = ref->vector_width;
      }
      EXPECT_TRUE(checker::check_operands({&descriptor, 1}, {}, {&view, 1}, {},
                                          scalarPackedMmContext()));
    }
  }
};

/** Existing source members reuse the common two-arm union with unchanged class identities. */
static_assert(std::same_as<
              decltype(MultimemStFloatPackedNoneScalarDefaultWeak{}.src.value),
              RegOrImm>);
static_assert(
    std::same_as<
        decltype(MultimemStFloatPackedNoneScalarDefaultOrdered{}.src.value),
        RegOrImm>);
static_assert(std::same_as<
              decltype(MultimemRedFloatPackedAddScalarDefaultRed{}.src.value),
              RegOrImm>);

/** All three fields retain matched registers and exact bare-word sources in both types. */
TEST(ScalarMultimemPacked32Sources, SixBranchesMatchedRegistersAndExactWords) {
  for (const auto& item : cases)
    for (const auto type : {ScalarType::F16x2, ScalarType::BF16x2}) {
      SCOPED_TRACE(item.operation);
      auto control = ownedScalarPackedMm(scalarPackedMmText(item, type, "b"));
      ASSERT_TRUE(control);
      const auto* reg = std::get_if<ResolvedRegisterRef>(
          &scalarPackedMmSource(*control->functions.front().body.front())
               .value);
      ASSERT_NE(reg, nullptr);
      EXPECT_TRUE(reg->symbol_id);
      EXPECT_EQ(reg->declared_type, ScalarType::B32);
      auto component =
          ownedScalarPackedMm(scalarPackedMmText(item, type, "V.x"));
      ASSERT_TRUE(component);
      const auto& leaf = std::get<ResolvedRegisterRef>(
          scalarPackedMmSource(*component->functions.front().body.front())
              .value);
      ASSERT_TRUE(leaf.component);
      EXPECT_EQ(leaf.component->lane, 0u);
      EXPECT_TRUE(leaf.symbol_id);
      if (type == ScalarType::F16x2)
        EXPECT_TRUE(
            ownedScalarPackedMm(scalarPackedMmText(item, type, "native")));
      // Incompatible BF16x2/native-F16x2 source is checked with its actual cause below.
      for (const auto& word : words)
        for (const bool strict : {false, true}) {
          const auto text = scalarPackedMmText(item, type, word.source);
          auto module = ownedScalarPackedMm(text, strict);
          ASSERT_TRUE(module) << text;
          auto& instruction = *module->functions.front().body.front();
          EXPECT_EQ(instruction.instruction_kind(), item.kind);
          const auto& source = scalarPackedMmSource(instruction);
          const auto* immediate = std::get_if<ResolvedImmediate>(&source.value);
          ASSERT_NE(immediate, nullptr);
          EXPECT_EQ(immediate->type, type);
          EXPECT_EQ(immediate->bits, word.bits);
          EXPECT_FALSE(immediate->integer_source_bits);
          EXPECT_FALSE(immediate->is_negative);
          ASSERT_EQ(source.locs.size(), 1u);
          EXPECT_EQ(source.locs.front(),
                    scalarPackedMmRange(text, word.source));
          ScalarPackedMmObserver observer(source, type);
          instruction.visit_references(observer);
          EXPECT_EQ(observer.matches, 1u);
          EXPECT_TRUE(validateModule(
              *module, ModuleValidationPolicy::RequireCompleteContext));
        }
    }
}

/** Require a typed current-word failure and the independently retained source range. */
void expectScalarMmWordCause(const checker::CheckResult& checked,
                             SourceRange range,
                             checker::CheckDiagnosticKind kind) {
  ASSERT_FALSE(checked);
  ASSERT_FALSE(checked.error().empty());
  EXPECT_TRUE(std::ranges::any_of(checked.error(), [&](const auto& diagnostic) {
    return diagnostic.kind == kind && diagnostic.range == range &&
           !diagnostic.message.empty();
  }));
}

/** Canonical current packed words stay independent of erased floating producer history. */
TEST(ScalarMultimemPacked32Sources, CurrentWordsOwnershipAndBadTuples) {
  for (const auto& item : cases)
    for (const auto type : {ScalarType::F16x2, ScalarType::BF16x2}) {
      auto producer = ownedScalarPackedMm(scalarPackedMmText(item, type, "b"));
      ASSERT_TRUE(producer);
      {
        auto& source =
            scalarPackedMmSource(*producer->functions.front().body.front());
        source.value = ResolvedImmediate{.bits = 0xdeadbeef, .type = type};
      }
      ASSERT_TRUE(validateModule(*producer));
      const auto original_locs =
          scalarPackedMmSource(*producer->functions.front().body.front()).locs;
      ASSERT_EQ(original_locs.size(), 1u);
      ResolvedModule copied = *producer, assigned;
      assigned = copied;
      ResolvedFunction function = copied.functions.front(), function_assigned;
      function_assigned = function;
      assigned.functions.front() = std::move(function_assigned);
      assigned.functions.front().body.front() = function.body.front()->clone();
      producer = std::unexpected(ModuleResolveDiagnostics{});
      ASSERT_TRUE(validateModule(copied));
      ASSERT_TRUE(validateModule(assigned));
      auto& current =
          scalarPackedMmSource(*assigned.functions.front().body.front());
      ASSERT_EQ(current.locs.size(), 1u);
      EXPECT_EQ(current.locs.front(), original_locs.front());
      EXPECT_NE(current.locs.data(),
                scalarPackedMmSource(*copied.functions.front().body.front())
                    .locs.data());
      const auto valid = std::get<ResolvedImmediate>(current.value);
      for (unsigned mutation = 0; mutation < 4; ++mutation) {
        auto bad = valid;
        if (mutation == 0)
          bad.bits |= uint64_t{1} << 32;
        if (mutation == 1)
          bad.integer_source_bits = 0;
        if (mutation == 2)
          bad.is_negative = true;
        if (mutation == 3)
          bad.type = type == ScalarType::F16x2 ? ScalarType::BF16x2
                                               : ScalarType::F16x2;
        current.value = bad;
        if (mutation == 1) {
          const auto& actual = std::get<ResolvedImmediate>(current.value);
          ASSERT_TRUE(actual.integer_source_bits.has_value());
          EXPECT_EQ(*actual.integer_source_bits, 0u);
        }
        const auto expected_kind =
            mutation == 3
                ? checker::CheckDiagnosticKind::OperandTypeMismatch
                : checker::CheckDiagnosticKind::ImmediateValueMismatch;
        ASSERT_EQ(current.locs.size(), 1u);
        const auto direct = assigned.functions.front().body.front()->check(
            scalarPackedMmContext());
        ASSERT_FALSE(direct);
        EXPECT_TRUE(std::ranges::any_of(direct.error(), [&](const auto&
                                                                diagnostic) {
          return diagnostic.range == current.locs.front() &&
                 diagnostic.kind ==
                     (mutation == 3
                          ? checker::CheckDiagnosticKind::OperandTypeMismatch
                          : checker::CheckDiagnosticKind::
                                ImmediateValueMismatch);
        }));
        expectScalarMmWordCause(validateModule(assigned), original_locs.front(),
                                expected_kind);
        const std::string good_text = scalarPackedMmText(item, type, "b");
        PtxSyntaxParser good_parser(good_text);
        const auto good_ast = good_parser.parseModule();
        ASSERT_TRUE(good_ast && good_ast.diagnostics.empty());
        expectScalarMmWordCause(validateModule(*good_ast, assigned),
                                original_locs.front(), expected_kind);
        auto bad_copy = assigned;
        EXPECT_FALSE(validateModule(bad_copy));
        auto bad_clone = assigned.functions.front().body.front()->clone();
        EXPECT_FALSE(bad_clone->check(scalarPackedMmContext()));
        current.value = valid;
        ASSERT_TRUE(assigned.functions.front().body.front()->check(
            scalarPackedMmContext()));
        ASSERT_TRUE(validateModule(assigned));
        ASSERT_TRUE(validateModule(*good_ast, assigned));
      }
      std::get<ResolvedImmediate>(current.value).bits = 1;
      EXPECT_TRUE(validateModule(assigned));
    }
}

/** Preserve actual failed-source cause/stage/range without inventing a validated owner. */
void scalarMmRejectedSource(const ScalarPackedMmCase& item, ScalarType type,
                            std::string_view operand,
                            std::string_view extra = "", bool numeric = true,
                            bool syntax_source_NA = false) {
  const std::string text = scalarPackedMmText(item, type, operand, extra);
  PtxSyntaxParser parser(text);
  const auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty()) {
    EXPECT_TRUE(syntax_source_NA) << text;
    EXPECT_FALSE(ast.diagnostics.empty());
    std::cout << "scalar-mm-source|parse-result-failure|Source_N_A|" << operand
              << '\n';
    for (const auto& diagnostic : ast.diagnostics)
      std::cout << diagnostic.message << '\n';
    return;
  }
  const auto result = resolveAndValidateModule(*ast);
  ASSERT_FALSE(result) << text;
  ASSERT_FALSE(result.error().empty());
  const auto written = scalarPackedMmRange(
      text, operand == "(0f3e003e00)" || operand == "(0f3e003e00+0)"
                ? std::string_view("0f3e003e00")
                : operand);
  EXPECT_NE(written, SourceRange{});
  EXPECT_TRUE(std::ranges::any_of(result.error(), [&](const auto& diagnostic) {
    if (syntax_source_NA)
      return diagnostic.range.start.line > 0 &&
             (diagnostic.message.find("incompatible") != std::string::npos ||
              diagnostic.message.find("Operands") != std::string::npos ||
              diagnostic.message.find("constant") != std::string::npos);
    if (numeric)
      return diagnostic.stage() == ResolveDiagnosticStage::Resolution &&
             !diagnostic.checker_kind && diagnostic.range == written &&
             (diagnostic.message.find("incompatible") != std::string::npos ||
              diagnostic.message.find("cannot have a sign") !=
                  std::string::npos ||
              diagnostic.message ==
                  "Exact single-precision bit patterns cannot participate in "
                  "constant expressions.");
    if (extra.find(".reg .bf16 ") != std::string_view::npos ||
        extra.find(".reg .bf16x2 ") != std::string_view::npos)
      return diagnostic.range.start.line > 0 &&
             (diagnostic.checker_kind ==
                  checker::CheckDiagnosticKind::OperandTypeMismatch ||
              diagnostic.declaration_kind.has_value());
    return (diagnostic.range == written ||
            diagnostic.declaration_kind.has_value()) &&
           (diagnostic.checker_kind ==
                checker::CheckDiagnosticKind::OperandTypeMismatch ||
            diagnostic.declaration_kind.has_value() ||
            diagnostic.message.find("non-predicate register") !=
                std::string::npos ||
            diagnostic.message.find("Vector register") != std::string::npos);
  }));
  for (const auto& diagnostic : result.error())
    std::cout << "scalar-mm-source|" << operand
              << "|public-stage=" << int(diagnostic.stage()) << "|checker="
              << (diagnostic.checker_kind ? int(*diagnostic.checker_kind) : -1)
              << "|range=" << diagnostic.range.start.line << ':'
              << diagnostic.range.start.column << '-'
              << diagnostic.range.end.line << ':' << diagnostic.range.end.column
              << "|" << diagnostic.message << '\n';
}

/** Keep packed lexical restrictions, register domains, target floors and source identity causal. */
TEST(ScalarMultimemPacked32Sources, ExcludedOriginsAndSuppliedAstControls) {
  for (const auto& item : cases)
    for (const auto type : {ScalarType::F16x2, ScalarType::BF16x2}) {
      SCOPED_TRACE(item.operation);
      auto healthy = ownedScalarPackedMm(scalarPackedMmText(item, type, "b"));
      ASSERT_TRUE(healthy);
      for (const auto bad : {"1", "0x3e003e00", "(1+1)", "1.5",
                             "0d3ff8000000000000", "+0f3e003e00", "-0f3e003e00",
                             "(0f3e003e00)", "(0f3e003e00+0)", "(1.0+0.5)"})
        scalarMmRejectedSource(item, type, bad);
      for (const auto unsupported : {"((.u64)0f3e003e00)", "{1.5,1.5}"})
        scalarMmRejectedSource(item, type, unsupported, "", true, true);
      scalarMmRejectedSource(item, type, "V", "", false);
      for (const auto declared : {"b16", "f16", "u16", "s16", "bf16", "u32",
                                  "s32", "f32", "bf16x2", "b64", "pred"})
        scalarMmRejectedSource(item, type, "bad",
                               ".reg ." + std::string(declared) + " bad; ",
                               false);
      if (type == ScalarType::BF16x2)
        scalarMmRejectedSource(item, type, "native", "", false);
      EXPECT_FALSE(ownedScalarPackedMm(
          scalarPackedMmText(item, type, "0f00000001", "", "sm_80")));
      const auto text = scalarPackedMmText(item, type, "0f00000001");
      PtxSyntaxParser parser(text);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty()) << text;
      auto module = resolveAndValidateModule(*ast);
      ASSERT_TRUE(module);
      ASSERT_EQ(module->functions.size(), 1u);
      ASSERT_EQ(module->functions.front().body.size(), 1u);
      ASSERT_NE(module->functions.front().body.front(), nullptr);
      ASSERT_TRUE(validateModule(*ast, *module));
      auto& immediate = std::get<ResolvedImmediate>(
          scalarPackedMmSource(*module->functions.front().body.front()).value);
      immediate.integer_source_bits = 0;
      EXPECT_FALSE(validateModule(*ast, *module));
      immediate.integer_source_bits.reset();
      const auto changed_text = scalarPackedMmText(item, type, "0f00000002");
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
      auto registers = ownedScalarPackedMm(scalarPackedMmText(item, type, "b"));
      ASSERT_TRUE(registers);
      auto& reference = std::get<ResolvedRegisterRef>(
          scalarPackedMmSource(*registers->functions.front().body.front())
              .value);
      reference.register_class = ResolvedRegisterClass::Predicate;
      EXPECT_FALSE(validateModule(*registers));
    }
}
/** Current same-function donors distinguish genuine incompatible types from stale declaration caches. */
TEST(ScalarMultimemPacked32Sources, CurrentReferencesBothApisAndComponents) {
  for (const auto& item : cases)
    for (const auto type : {ScalarType::F16x2, ScalarType::BF16x2}) {
      const std::string prefix =
          ".reg .b32 alternate; .reg .f32 wrong; .reg .b64 wide; "
          "mov.b32 alternate,b; mov.b32 wrong,b; mov.b64 wide,0; ";
      const std::string text = scalarPackedMmText(item, type, "b", prefix);
      PtxSyntaxParser parser(text);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      auto module = ownedScalarPackedMm(text, true, 4);
      ASSERT_TRUE(module);
      auto& body = module->functions.front().body;
      auto& instruction = *body.back();
      auto& slot = scalarPackedMmSource(instruction);
      ASSERT_EQ(slot.locs.size(), 1u);
      const auto range = scalarPackedMmRange(text, "b");
      EXPECT_EQ(slot.locs.front(), range);
      const auto* initial = std::get_if<ResolvedRegisterRef>(&slot.value);
      ASSERT_NE(initial, nullptr);
      const auto original = *initial;
      EXPECT_TRUE(validateModule(*ast, *module));
      EXPECT_TRUE(validateModule(*module));
      for (unsigned donor = 0; donor < 3; ++donor) {
        const auto* mov = dynamic_cast<const MovScalar*>(body[donor].get());
        ASSERT_NE(mov, nullptr);
        ASSERT_TRUE(mov->dst_register);
        slot.value = mov->dst_register->value;
        if (donor == 0) {
          EXPECT_TRUE(validateModule(*module));
          EXPECT_TRUE(validateModule(*ast, *module));
        } else {
          const auto direct = instruction.check(scalarPackedMmContext());
          ASSERT_FALSE(direct);
          EXPECT_TRUE(std::ranges::any_of(direct.error(), [&](const auto& d) {
            return d.kind ==
                       checker::CheckDiagnosticKind::OperandTypeMismatch &&
                   d.range == range;
          }));
          EXPECT_FALSE(validateModule(*module));
          EXPECT_FALSE(validateModule(*ast, *module));
        }
        slot.value = original;
        EXPECT_TRUE(validateModule(*module));
        EXPECT_TRUE(validateModule(*ast, *module));
      }
      for (unsigned mutation = 0; mutation < 5; ++mutation) {
        auto bad = original;
        if (mutation == 0)
          bad.declared_type = ScalarType::F32;
        if (mutation == 1)
          bad.declared_type.reset();
        if (mutation == 2)
          bad.register_class = ResolvedRegisterClass::Predicate;
        if (mutation == 3)
          bad.symbol_id = module->functions.front().symbol_id;
        if (mutation == 4)
          bad.vector_width = 2;
        slot.value = bad;
        const auto current = validateModule(*module),
                   supplied = validateModule(*ast, *module);
        ASSERT_FALSE(current);
        ASSERT_FALSE(supplied);
        for (const auto* result : {&current, &supplied})
          EXPECT_TRUE(std::ranges::any_of(result->error(), [&](const auto& d) {
            return d.kind ==
                       checker::CheckDiagnosticKind::ModuleSourceMismatch &&
                   d.range == range;
          }));
        slot.value = original;
        EXPECT_TRUE(validateModule(*module));
        EXPECT_TRUE(validateModule(*ast, *module));
      }
      const std::string component_text = scalarPackedMmText(item, type, "V.x");
      PtxSyntaxParser cp(component_text);
      const auto component_ast = cp.parseModule();
      ASSERT_TRUE(component_ast && component_ast.diagnostics.empty());
      auto component = ownedScalarPackedMm(component_text);
      ASSERT_TRUE(component);
      auto& field =
          scalarPackedMmSource(*component->functions.front().body.front());
      auto* ref = std::get_if<ResolvedRegisterRef>(&field.value);
      ASSERT_NE(ref, nullptr);
      ASSERT_TRUE(ref->component);
      ASSERT_TRUE(ref->component->selector);
      ASSERT_EQ(field.locs.size(), 1u);
      const auto use = scalarPackedMmRange(component_text, "V.x");
      EXPECT_EQ(field.locs.front(), use);
      const auto valid = *ref;
      for (unsigned mutation = 0; mutation < 8; ++mutation) {
        *ref = valid;
        auto& part = *ref->component;
        if (mutation == 0)
          part.lane = part.declaration_width;
        if (mutation == 1)
          part.declaration_width = 4;
        if (mutation == 2)
          part.base_range.end.column++;
        if (mutation == 3)
          part.range.end.column--;
        if (mutation == 4)
          part.selector->spelling = ".y";
        if (mutation == 5)
          part.selector->range.start.column++;
        if (mutation == 6)
          ref->vector_width = 2;
        if (mutation == 7)
          ref->parameterized_index = 0;
        const auto current = validateModule(*component),
                   supplied = validateModule(*component_ast, *component);
        ASSERT_FALSE(current);
        ASSERT_FALSE(supplied);
        for (const auto* result : {&current, &supplied})
          EXPECT_TRUE(std::ranges::any_of(result->error(), [&](const auto& d) {
            return d.range == use &&
                   (d.kind ==
                        checker::CheckDiagnosticKind::ModuleSourceMismatch ||
                    d.kind ==
                        checker::CheckDiagnosticKind::InvalidVectorOperand ||
                    d.kind ==
                        checker::CheckDiagnosticKind::OperandTypeMismatch);
          }));
      }
      *ref = valid;
      EXPECT_TRUE(validateModule(*component));
      EXPECT_TRUE(validateModule(*component_ast, *component));
    }
}

/** Preserve family semantic identity independently of convenience numeric caches and old source spelling. */
TEST(ScalarMultimemPacked32Sources, FamilyIdentityAndCoherentRetargetBothApis) {
  for (const auto& item : cases)
    for (const auto type : {ScalarType::F16x2, ScalarType::BF16x2}) {
      const std::string declarations = ".reg .b32 %r<2>; ";
      const std::string text =
          scalarPackedMmText(item, type, "%r0", declarations);
      PtxSyntaxParser parser(text);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      auto baseline = ownedScalarPackedMm(text);
      ASSERT_TRUE(baseline);
      const auto& original_field =
          scalarPackedMmSource(*baseline->functions.front().body.front());
      const auto* initial =
          std::get_if<ResolvedRegisterRef>(&original_field.value);
      ASSERT_NE(initial, nullptr);
      const auto valid = *initial;
      ASSERT_TRUE(valid.symbol_id);
      ASSERT_EQ(valid.parameterized_index, 0u);
      ASSERT_EQ(valid.index, 0u);
      EXPECT_EQ(baseline->symbols.symbol(*valid.symbol_id).parameterized_count,
                2u);
      ASSERT_EQ(original_field.locs.size(), 1u);
      const auto range = scalarPackedMmRange(text, "%r0");
      EXPECT_EQ(original_field.locs.front(), range);
      for (unsigned mutation = 0; mutation < 4; ++mutation) {
        auto current = *baseline;
        auto bad = valid;
        if (mutation == 0)
          bad.parameterized_index.reset();
        if (mutation == 1)
          bad.spelling = "%r1";
        if (mutation == 2)
          bad.parameterized_index = 1;
        if (mutation == 3)
          bad.parameterized_index = 2;
        scalarPackedMmSource(*current.functions.front().body.front()).value =
            bad;
        EXPECT_EQ(current.source_identity, baseline->source_identity);
        expectScalarMmReferenceFailure(validateModule(current), range,
                                       mutation == 3);
        expectScalarMmReferenceFailure(validateModule(*ast, current), range,
                                       mutation == 3);
      }
      auto plain = ownedScalarPackedMm(
          scalarPackedMmText(item, type, "b", declarations));
      ASSERT_TRUE(plain);
      const auto* plain_ref = std::get_if<ResolvedRegisterRef>(
          &scalarPackedMmSource(*plain->functions.front().body.front()).value);
      ASSERT_NE(plain_ref, nullptr);
      for (unsigned positive = 0; positive < 4; ++positive) {
        auto current = *baseline;
        auto ref = valid;
        if (positive < 2) {
          ref.spelling = "%r1";
          ref.parameterized_index = 1;
          ref.index = 1;
        }
        if (positive == 1 || positive == 2)
          ref.index.reset();
        if (positive == 3) {
          ref = *plain_ref;
          EXPECT_FALSE(ref.parameterized_index);
        }
        scalarPackedMmSource(*current.functions.front().body.front()).value =
            ref;
        EXPECT_EQ(current.source_identity, baseline->source_identity);
        EXPECT_TRUE(validateModule(current));
        EXPECT_TRUE(validateModule(*ast, current));
        auto copied = current;
        ResolvedModule assigned;
        assigned = copied;
        ResolvedFunction function = assigned.functions.front(),
                         function_assigned;
        function_assigned = function;
        assigned.functions.front() = function_assigned;
        assigned.functions.front().body.front() =
            function_assigned.body.front()->clone();
        EXPECT_TRUE(validateModule(assigned));
        EXPECT_TRUE(validateModule(*ast, assigned));
      }
    }
}

/** Availability and address/state obligations stay separate from packed numeric-source permission. */
TEST(ScalarMultimemPacked32Sources, ContextAndAddressRemainIndependent) {
  for (const auto& item : cases)
    for (const auto type : {ScalarType::F16x2, ScalarType::BF16x2}) {
      auto good =
          ownedScalarPackedMm(scalarPackedMmText(item, type, "0f00000001"));
      ASSERT_TRUE(good);
      const auto& instruction = *good->functions.front().body.front();
      auto below = scalarPackedMmContext();
      below.target.ptx_version = {8, 0};
      EXPECT_FALSE(instruction.check(below));
      EXPECT_FALSE(instruction.check(scalarPackedMmContext("sm_80")));
      std::string partial_text = scalarPackedMmText(item, type, "0f00000001");
      const auto header = partial_text.find(".target sm_90\n");
      ASSERT_NE(header, std::string::npos);
      partial_text.erase(header, std::string_view(".target sm_90\n").size());
      PtxSyntaxParser parser(partial_text);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      auto partial = resolveModuleOnly(*ast);
      ASSERT_TRUE(partial);
      EXPECT_FALSE(validateModule(
          *partial, ModuleValidationPolicy::RequireCompleteContext));
      EXPECT_TRUE(
          validateModule(*partial, ModuleValidationPolicy::AvailableContext));
      for (const auto replacement :
           {std::string("[mm+2]"), std::string("[local_value]")}) {
        std::string text =
            scalarPackedMmText(item, type, "0f00000001",
                               ".local .align 16 .b8 local_value[256]; ");
        const auto address = text.rfind("[mm]");
        ASSERT_NE(address, std::string::npos);
        text.replace(address, 4, replacement);
        PtxSyntaxParser address_parser(text);
        const auto address_ast = address_parser.parseModule();
        ASSERT_TRUE(address_ast && address_ast.diagnostics.empty());
        const auto rejected = resolveAndValidateModule(*address_ast);
        ASSERT_FALSE(rejected);
        EXPECT_TRUE(std::ranges::any_of(rejected.error(), [](const auto& d) {
          return d.checker_kind ==
                     checker::CheckDiagnosticKind::AddressAlignmentMismatch ||
                 d.checker_kind ==
                     checker::CheckDiagnosticKind::AddressStateSpaceMismatch;
        }));
      }
    }
}

#ifdef PTX_SCALAR_MM_CAPTURE_GENERATED_VIEWS
/** Observe the native generated views for all six types/classes, distinct from the manual observer projection. */
TEST(ScalarMultimemPacked32Sources, ActualGeneratedViewsAndFactories) {
  for (const auto& item : cases)
    for (const auto type : {ScalarType::F16x2, ScalarType::BF16x2})
      for (const bool immediate : {false, true}) {
        const auto operand =
            immediate
                ? (type == ScalarType::F16x2 ? "0f3e003e00" : "0f3fc03fc0")
                : "b";
        auto module =
            ownedScalarPackedMm(scalarPackedMmText(item, type, operand));
        ASSERT_TRUE(module);
        auto& instruction = *module->functions.front().body.front();
        auto& member = scalarPackedMmSource(instruction);
        const auto before = scalar_mm_generated::observed;
        scalar_mm_generated::mirrors = true;
        scalar_mm_generated::logical = type;
        scalar_mm_generated::expected = &member;
        const auto checked = instruction.check(scalarPackedMmContext());
        scalar_mm_generated::expected = nullptr;
        EXPECT_TRUE(checked);
        EXPECT_TRUE(scalar_mm_generated::mirrors);
        EXPECT_EQ(scalar_mm_generated::observed, before + 1);
        auto clone = instruction.clone();
        auto& cloned_member = scalarPackedMmSource(*clone);
        EXPECT_NE(&cloned_member, &member);
        const auto clone_before = scalar_mm_generated::observed;
        scalar_mm_generated::expected = &cloned_member;
        const auto cloned = clone->check(scalarPackedMmContext());
        scalar_mm_generated::expected = nullptr;
        EXPECT_TRUE(cloned);
        EXPECT_TRUE(scalar_mm_generated::mirrors);
        EXPECT_EQ(scalar_mm_generated::observed, clone_before + 1);
        std::string operation(item.operation);
        operation.replace(operation.find('@'), 1,
                          type == ScalarType::F16x2 ? "f16x2" : "bf16x2");
        operation.replace(operation.find('$'), 1, immediate ? operand : "%b0");
        operation.replace(operation.find("[mm]"), 4, "[%rd0]");
        PtxSyntaxParser parser(operation);
        const auto ast = parser.parseInstruction();
        ASSERT_TRUE(ast && ast.diagnostics.empty());
        auto factory = resolveInstruction(*ast);
        ASSERT_TRUE(factory);
        ASSERT_NE(*factory, nullptr);
        EXPECT_EQ((*factory)->instruction_kind(), item.kind);
        const auto& source = scalarPackedMmSource(**factory);
        if (immediate) {
          const auto* word = std::get_if<ResolvedImmediate>(&source.value);
          ASSERT_NE(word, nullptr);
          EXPECT_EQ(word->type, type);
          EXPECT_EQ(word->bits,
                    type == ScalarType::F16x2 ? 0x3e003e00u : 0x3fc03fc0u);
        } else
          EXPECT_NE(std::get_if<ResolvedRegisterRef>(&source.value), nullptr);
      }
}
#endif

}  // namespace
}  // namespace ptx_frontend::resolved_ir
