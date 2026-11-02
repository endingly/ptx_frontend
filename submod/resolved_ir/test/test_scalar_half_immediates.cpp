#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/model/arithmetic/add.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/fma.gen.hpp>
#include <ptx_frontend/resolved_ir/model/control_flow/call.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/cvt.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** One admitted scalar format role, with exactly one substituted source. */
struct HalfSourceCase {
  /** Written instruction; '$' denotes one half source field. */
  std::string_view instruction;
};
constexpr std::array half_sources{
    HalfSourceCase{"cvt.f32.f16 f0,$;"},
    HalfSourceCase{"add.f32.f16 f0,$,f1;"},
    HalfSourceCase{"sub.f32.f16 f0,$,f1;"},
    HalfSourceCase{"fma.rn.f32.f16 f0,$,h2,f1;"},
    HalfSourceCase{"fma.rn.f32.f16 f0,h1,$,f1;"},
    HalfSourceCase{"set.eq.f16.f16 h0,$,h2;"},
    HalfSourceCase{"set.eq.f16.f16 h0,h1,$;"},
    HalfSourceCase{"set.eq.and.f16.f16 h0,$,h2,p;"},
    HalfSourceCase{"set.eq.and.f16.f16 h0,h1,$,p;"},
    HalfSourceCase{"set.eq.bf16.f16 b,$,h2;"},
    HalfSourceCase{"set.eq.bf16.f16 b,h1,$;"},
    HalfSourceCase{"set.eq.and.bf16.f16 b,$,h2,p;"},
    HalfSourceCase{"set.eq.and.bf16.f16 b,h1,$,p;"},
    HalfSourceCase{"set.eq.u32.f16 u,$,h2;"},
    HalfSourceCase{"set.eq.u32.f16 u,h1,$;"},
    HalfSourceCase{"set.eq.and.u32.f16 u,$,h2,p;"},
    HalfSourceCase{"set.eq.and.u32.f16 u,h1,$,p;"},
    HalfSourceCase{"setp.eq.f16 p,$,h2;"},
    HalfSourceCase{"setp.eq.f16 p,h1,$;"},
    HalfSourceCase{"setp.eq.and.f16 p,$,h2,q;"},
    HalfSourceCase{"setp.eq.and.f16 p,h1,$,q;"},
};
/** Build a legal complete profile without changing the logical operand type. */
std::string half_source(const HalfSourceCase& item, std::string_view operand) {
  std::string instruction(item.instruction);
  instruction.replace(instruction.find('$'), 1, operand);
  return ".version 9.3\n.target sm_100a\n.entry kernel(){\n"
         ".reg .f16 h0,h1,h2; .reg .b16 b; .reg .f32 f0,f1; "
         ".reg .u32 u; .reg .f64 d; .reg .pred p,q;\n" +
         instruction + "\n}";
}

/** Return a strict owned module after source text, parser and AST destruction. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> owned_half(
    std::string source) {
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveAndValidateModule(*ast);
}
/** Exact modern profile used only by instruction-local checks. */
checker::Context half_context() {
  const auto profile = base::find_target_profile("sm_100a");
  return {
      .target = {.ptx_version = {9, 3},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
}
/** Copies, assignments and concrete instruction clones retain independent ownership. */
void expect_half_ownership(const ResolvedModule& module) {
  EXPECT_TRUE(validateModule(module));
  ResolvedModule copy(module), assigned;
  assigned = copy;
  EXPECT_TRUE(validateModule(copy));
  EXPECT_TRUE(validateModule(assigned));
  ResolvedFunction function(module.functions.back()), function_assigned;
  function_assigned = function;
  assigned.functions.back() = std::move(function_assigned);
  assigned.functions.back().body.back() = function.body.back()->clone();
  EXPECT_TRUE(validateModule(assigned));
}

TEST(ScalarHalfImmediates, AdmitsFloatingSourcesAfterMatchedRegisterControls) {
  for (const auto& item : half_sources) {
    SCOPED_TRACE(item.instruction);
    const auto control = test_helpers::parseModule(half_source(item, "h1"));
    ASSERT_MODULE_PARSE_SUCCEEDS(control);
    const auto valid = resolveAndValidateModule(*control);
    ASSERT_TRUE(valid) << valid.error().front().message;
    for (const auto literal :
         {"1.5", "1.0+0.5", "0d3ff8000000000000", "0f3fc00000"}) {
      SCOPED_TRACE(literal);
      const auto ast = test_helpers::parseModule(half_source(item, literal));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      const auto draft = resolveModuleOnly(*ast);
      EXPECT_TRUE(draft) << (draft ? "" : draft.error().front().message);
      EXPECT_TRUE(resolveAndValidateModule(*ast));
      if (draft)
        EXPECT_TRUE(validateModule(*ast, *draft));
      const auto owned = owned_half(half_source(item, literal));
      ASSERT_TRUE(owned);
      expect_half_ownership(*owned);
    }
  }
}
/** Keep the exact legacy baseline independent of modern parameter/prototype rules. */
std::string half_call_source(std::string_view argument, bool legacy,
                             bool indirect = false) {
  if (legacy)
    return ".version 1.4\n.target sm_13\n"
           ".func take(.reg .f16 x){ret;}\n.entry kernel(){ .reg .f16 h; "
           "mov.b16 h,0x3e00; call take,(" +
           std::string(argument) + ");}";
  return ".version 9.3\n.target sm_100a\n.extern .func take(.param .f16 x);\n"
         ".entry kernel(){.reg .f16 h; .reg .b64 target; "
         "proto: .callprototype _ (.param .f16 x); "
         "mov.b16 h,0x3e00; call " +
         std::string(indirect ? "target" : "take") + ",(" +
         std::string(argument) + ")" + (indirect ? ",proto" : "") + ";}";
}
TEST(ScalarHalfImmediates, AdmitsCallLiteralsBesideEstablishedRegisterActuals) {
  for (const bool legacy : {false, true})
    for (const bool indirect : {false, true}) {
      if (legacy && indirect)
        continue;
      SCOPED_TRACE(legacy);
      SCOPED_TRACE(indirect);
      const auto control =
          test_helpers::parseModule(half_call_source("h", legacy, indirect));
      ASSERT_MODULE_PARSE_SUCCEEDS(control);
      const auto valid = resolveAndValidateModule(*control);
      ASSERT_TRUE(valid) << valid.error().front().message;
      for (const auto literal :
           {"1.5", "1.0+0.5", "0d3ff8000000000000", "0f3fc00000"}) {
        SCOPED_TRACE(literal);
        const auto ast = test_helpers::parseModule(
            half_call_source(literal, legacy, indirect));
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        const auto draft = resolveModuleOnly(*ast);
        EXPECT_TRUE(draft) << (draft ? "" : draft.error().front().message);
        EXPECT_TRUE(resolveAndValidateModule(*ast));
        auto owned = owned_half(half_call_source(literal, legacy, indirect));
        ASSERT_TRUE(owned);
        const auto& call = dynamic_cast<const CallDirect&>(
            *owned->functions.back().body.back());
        const auto& value = std::get<ResolvedCallLiteral>(
            call.arguments->value.values[0].value);
        ASSERT_TRUE(value.value);
        EXPECT_EQ(value.value->type, ScalarType::F16);
        EXPECT_EQ(value.value->bits, 0x3e00U);
        EXPECT_FALSE(value.value->integer_source_bits);
        EXPECT_FALSE(value.value->is_negative);
        if (value.source_value) {
          const auto* source =
              std::get_if<declaration_semantics::FloatingConstantValue>(
                  &*value.source_value);
          ASSERT_TRUE(source);
          EXPECT_EQ(std::bit_cast<uint64_t>(source->value),
                    0x3ff8000000000000ULL);
        }
        expect_half_ownership(*owned);
      }
    }
}
/** A source expression and an independent expected binary16 word. */
struct HalfGolden {
  /** Exact lexical input; no host half arithmetic is used by this oracle. */
  std::string_view source;
  /** IEEE binary16 destination encoding. */
  uint16_t bits;
};
constexpr auto half_goldens =
    std::to_array<HalfGolden>({HalfGolden{"1.5", 0x3e00},
                               {"1.0+0.5", 0x3e00},
                               {"0d3ff8000000000000", 0x3e00},
                               {"0f3fc00000", 0x3e00},
                               {"0.0", 0x0000},
                               {"-0.0", 0x8000},
                               {"0d3ff0020000000000", 0x3c00},
                               {"0d3ff0060000000000", 0x3c02},
                               {"0d3ff0020000001000", 0x3c01},
                               {"0d3e70000000000000", 0x0001},
                               {"0d3e60000000000000", 0x0000},
                               {"0d3e60000000000001", 0x0001},
                               {"0d3f10000000000000", 0x0400},
                               {"0d3f0ffc0000000000", 0x0400},
                               {"65504.0", 0x7bff},
                               {"65520.0", 0x7c00},
                               {"-65520.0", 0xfc00},
                               {"-0d3e70000000000000", 0x8001},
                               {"0d7ff0000000000000", 0x7c00},
                               {"0dfff0000000000000", 0xfc00},
                               {"0d7ff0000000000001", 0x7e00},
                               {"0d7ff4000000000000", 0x7f00},
                               {"0d7fffffffffffffff", 0x7fff},
                               {"0dfff4000000000000", 0xff00},
                               {"0f80000000", 0x8000},
                               {"0f7f800000", 0x7c00},
                               {"0f7fc00000", 0x7e00},
                               {"(-0d0000000000000000)", 0x8000}});

TEST(ScalarHalfImmediates, ConvertsDirectlyToBinary16GoldenWords) {
  for (const auto& item : half_goldens) {
    SCOPED_TRACE(item.source);
    auto owned = owned_half(half_source(half_sources.front(), item.source));
    ASSERT_TRUE(owned) << owned.error().front().message;
    auto& cvt =
        dynamic_cast<CvtUnroundedBase&>(*owned->functions.front().body.front());
    const auto& value = std::get<ResolvedImmediate>(cvt.src.value);
    EXPECT_EQ(value.type, ScalarType::F16);
    EXPECT_EQ(value.bits, item.bits);
    EXPECT_FALSE(value.integer_source_bits);
    EXPECT_FALSE(value.is_negative);
    EXPECT_TRUE(cvt.check(half_context()));
    expect_half_ownership(*owned);
  }
}

TEST(ScalarHalfImmediates, SeparatesInstructionRoundingFromLiteralConversion) {
  for (const auto mode : {"rn", "rz", "rm", "rp"}) {
    const HalfSourceCase item{"fma.rn.f32.f16 f0,$,h2,f1;"};
    auto source = half_source(item, "0d3ff0020000001000");
    source.replace(source.find("fma.rn"), 6, "fma." + std::string(mode));
    const auto owned = owned_half(source);
    ASSERT_TRUE(owned) << owned.error().front().message;
    const auto& fma = dynamic_cast<const FmaMixedF32F16&>(
        *owned->functions.front().body.front());
    EXPECT_EQ(std::get<ResolvedImmediate>(fma.src1.value).bits, 0x3c01U);
  }
  const auto owned =
      owned_half(half_source({"cvt.ftz.f32.f16 f0,$;"}, "0d3e70000000000000"));
  ASSERT_TRUE(owned);
  const auto& cvt = dynamic_cast<const CvtUnroundedBase&>(
      *owned->functions.front().body.front());
  EXPECT_EQ(std::get<ResolvedImmediate>(cvt.src.value).bits, 1u);
}

TEST(ScalarHalfImmediates, RechecksCurrentWordsWithoutInventingLiteralHistory) {
  const auto source = half_source(half_sources.front(), "1.5");
  const auto ast = test_helpers::parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  auto owned = owned_half(source);
  ASSERT_TRUE(owned);
  auto& cvt =
      dynamic_cast<CvtUnroundedBase&>(*owned->functions.front().body.front());
  auto& immediate = std::get<ResolvedImmediate>(cvt.src.value);
  for (const auto bits : {0x3c00U, 0x8000U, 0x7c01U}) {
    immediate.bits = bits;
    EXPECT_TRUE(validateModule(*owned));
    EXPECT_TRUE(validateModule(*ast, *owned));
  }
  const auto original = immediate;
  for (int mutation = 0; mutation < 4; ++mutation) {
    SCOPED_TRACE(mutation);
    immediate = original;
    if (mutation == 0)
      immediate.bits = 0x10000;
    if (mutation == 1)
      immediate.integer_source_bits = 0;
    if (mutation == 2)
      immediate.is_negative = true;
    if (mutation == 3)
      immediate.type = ScalarType::BF16;
    EXPECT_FALSE(cvt.check(half_context()));
    EXPECT_FALSE(validateModule(*owned));
    auto copy = *owned;
    copy.functions.front().body.front() =
        owned->functions.front().body.front()->clone();
    EXPECT_FALSE(validateModule(copy));
  }
  immediate = original;
  const auto replacement =
      test_helpers::parseModule(half_source(half_sources.front(), "2.5"));
  ASSERT_MODULE_PARSE_SUCCEEDS(replacement);
  ASSERT_TRUE(resolveAndValidateModule(*replacement));
  const auto mismatch = validateModule(*replacement, *owned);
  ASSERT_FALSE(mismatch);
  EXPECT_TRUE(std::ranges::any_of(mismatch.error(), [](const auto& error) {
    return error.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
  }));
}

TEST(ScalarHalfImmediates, AcceptsCanonicalBfloatWordsAndWrittenSources) {
  auto owned = owned_half(half_source({"add.f32.bf16 f0,$,f1;"}, "b"));
  ASSERT_TRUE(owned);
  auto& add =
      dynamic_cast<AddMixedF32&>(*owned->functions.front().body.front());
  add.src.value = ResolvedImmediate{.bits = 0x3fc0, .type = ScalarType::BF16};
  EXPECT_TRUE(add.check(half_context()));
  expect_half_ownership(*owned);
  for (int mutation = 0; mutation < 3; ++mutation) {
    auto& immediate = std::get<ResolvedImmediate>(add.src.value);
    immediate = {.bits = 0x3fc0, .type = ScalarType::BF16};
    if (mutation == 0)
      immediate.bits |= uint64_t{1} << 16;
    if (mutation == 1)
      immediate.integer_source_bits = 0;
    if (mutation == 2)
      immediate.type = ScalarType::F16;
    EXPECT_FALSE(add.check(half_context()));
    EXPECT_FALSE(validateModule(*owned));
  }
  // Shared source admission and the logical BF16 converter now agree.
  const auto written =
      test_helpers::parseModule(half_source({"add.f32.bf16 f0,$,f1;"}, "1.5"));
  ASSERT_MODULE_PARSE_SUCCEEDS(written);
  EXPECT_TRUE(resolveAndValidateModule(*written));
}

TEST(ScalarHalfImmediates, CallsRecomputeAgainstPreservedPrecastSources) {
  for (const bool indirect : {false, true}) {
    auto owned = owned_half(half_call_source("1.0+0.5", false, indirect));
    ASSERT_TRUE(owned);
    auto& call =
        dynamic_cast<CallDirect&>(*owned->functions.back().body.back());
    auto& literal =
        std::get<ResolvedCallLiteral>(call.arguments->value.values[0].value);
    ASSERT_TRUE(literal.value);
    ASSERT_TRUE(literal.source_value);
    const auto original = *literal.value;
    for (int mutation = 0; mutation < 4; ++mutation) {
      *literal.value = original;
      if (mutation == 0)
        literal.value->bits = 0x4000;
      if (mutation == 1)
        literal.value->type = ScalarType::B16;
      if (mutation == 2)
        literal.value->bits |= uint64_t{1} << 16;
      if (mutation == 3)
        literal.value->integer_source_bits = 0;
      EXPECT_FALSE(validateModule(*owned));
      auto copy = *owned;
      copy.functions.back().body.back() =
          owned->functions.back().body.back()->clone();
      EXPECT_FALSE(validateModule(copy));
    }
    *literal.value = original;
    literal.source_value = declaration_semantics::FloatingConstantValue{2.0};
    literal.value->bits = 0x4000;
    EXPECT_TRUE(validateModule(*owned));
  }
}

TEST(ScalarHalfImmediates, PreservesIntegerAndExactSingleExpressionBoundaries) {
  for (const auto source :
       {"1", "0x3e00", "1+1", "(1.0 < 2.0)", "+0f3fc00000", "-0f3fc00000",
        "(0f3fc00000)", "0f3fc00000+0.0", "(1 ? 1.5 : 0f3fc00000)"}) {
    const auto ast =
        test_helpers::parseModule(half_source(half_sources.front(), source));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
    const auto call =
        test_helpers::parseModule(half_call_source(source, false));
    ASSERT_MODULE_PARSE_SUCCEEDS(call);
    EXPECT_FALSE(resolveAndValidateModule(*call));
  }
  // The separate explicit Bit16 SETP route is not adjudicated by these ordinary F16 negatives.
  for (const auto source : {"1.5", "0f3fc00000", "0d3ff8000000000000"}) {
    const auto bit =
        test_helpers::parseModule(half_source({"mov.b16 b,$;"}, source));
    ASSERT_MODULE_PARSE_SUCCEEDS(bit);
    EXPECT_FALSE(resolveAndValidateModule(*bit));
    const auto arithmetic =
        test_helpers::parseModule(half_source({"add.f16 h0,$,h2;"}, source));
    ASSERT_MODULE_PARSE_SUCCEEDS(arithmetic);
    EXPECT_FALSE(resolveAndValidateModule(*arithmetic));
  }
  for (const auto declaration :
       {".global .f16 value = 1.5;", ".const .f16 value = 1.5;"}) {
    const auto ast = test_helpers::parseModule(
        std::string(".version 9.3\n.target sm_100a\n") + declaration);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}

/** Distinguish missing context from a formal-width or source-identity failure. */
void expect_half_missing_context(const checker::CheckResult& result) {
  ASSERT_FALSE(result);
  EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& error) {
    return error.kind == checker::CheckDiagnosticKind::MissingValidationContext;
  }));
  EXPECT_FALSE(std::ranges::any_of(result.error(), [](const auto& error) {
    return error.message.find("Register formal total width") !=
           std::string::npos;
  }));
}

TEST(ScalarHalfImmediates, DefersMissingCallContextAndRetainsFormalAbiGates) {
  for (const auto header : {"", ".version 1.4\n", ".target sm_13\n",
                            ".version 9.3\n", ".target sm_80\n"}) {
    auto source = half_call_source("1.5", true);
    source.erase(0, source.find(".func"));
    source.insert(0, header);
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto draft = resolveModuleOnly(*ast);
    ASSERT_TRUE(draft) << draft.error().front().message;
    EXPECT_TRUE(
        validateModule(*ast, *draft, ModuleValidationPolicy::AvailableContext));
    EXPECT_TRUE(
        validateModule(*draft, ModuleValidationPolicy::AvailableContext));
    expect_half_missing_context(validateModule(*ast, *draft));
    expect_half_missing_context(validateModule(*draft));
    const auto& call =
        dynamic_cast<const CallDirect&>(*draft->functions.back().body.back());
    EXPECT_EQ(
        std::get<ResolvedCallLiteral>(call.arguments->value.values[0].value)
            .value->bits,
        0x3e00U);
  }
  auto owned = owned_half(half_call_source("1.5", true));
  ASSERT_TRUE(owned);
  for (auto& region : owned->header.regions) {
    region.version = checker::PtxVersion{9, 3};
    if (!region.target_options.empty())
      region.target_options.front() = "sm_90";
  }
  for (auto& function : owned->functions) {
    function.source_version = checker::PtxVersion{9, 3};
    function.source_target = "sm_90";
  }
  const auto modern = validateModule(*owned);
  ASSERT_FALSE(modern);
  EXPECT_TRUE(std::ranges::any_of(modern.error(), [](const auto& error) {
    return error.message.find("Register formal total width") !=
           std::string::npos;
  }));
  EXPECT_FALSE(std::ranges::any_of(modern.error(), [](const auto& error) {
    return error.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
  }));
  auto vector = half_call_source("1.5", false);
  for (size_t offset = 0;
       (offset = vector.find(".param .f16 x", offset)) != std::string::npos;) {
    vector.replace(offset, 13, ".param .v2 .f16 x");
    offset += 17;
  }
  const auto ast = test_helpers::parseModule(vector);
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  EXPECT_FALSE(resolveAndValidateModule(*ast));
}

TEST(ScalarHalfImmediates, SeparatesCvtModelTemplatesFromRoundingLegality) {
  for (const auto instruction :
       {"cvt.f32.f16 f0,$;", "cvt.rmi.s16.f16 b,$;", "cvt.rzi.f16.f16 h0,$;",
        "cvt.rzi.s32.f16 u,$;", "cvt.rzi.u32.f16 u,$;",
        "cvt.rn.bf16.f16 b,$;"}) {
    SCOPED_TRACE(instruction);
    const auto baseline = owned_half(half_source({instruction}, "h1"));
    ASSERT_TRUE(baseline) << baseline.error().front().message;
    const auto actual = owned_half(half_source({instruction}, "1.5"));
    ASSERT_TRUE(actual) << actual.error().front().message;
    expect_half_ownership(*actual);
  }
  // These current model templates have no F16-source positive under independent rounding checks.
  for (const auto instruction :
       {"cvt.s32.f16 u,$;", "cvt.rn.f64.f16 d,$;", "cvt.rn.f16.f16 h0,$;",
        "cvt.rn.f32.f16 f0,$;"}) {
    const auto source = half_source({instruction}, "h1");
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}

TEST(ScalarHalfImmediates, PreservesAllExplicitHalfArithmeticSourceRoles) {
  constexpr auto cases = std::to_array<std::pair<std::string_view, size_t>>(
      {{"abs.f16", 1},
       {"add.f16", 2},
       {"sub.f16", 2},
       {"mul.f16", 2},
       {"fma.rn.f16", 3},
       {"fma.rn.relu.f16", 3},
       {"fma.rn.oob.f16", 3},
       {"fma.rn.oob.relu.f16", 3},
       {"max.f16", 2},
       {"min.f16", 2},
       {"neg.f16", 1},
       {"ex2.approx.f16", 1},
       {"tanh.approx.f16", 1}});
  size_t roles = 0;
  for (const auto& [opcode, arity] : cases)
    for (size_t selected = 0; selected < arity; ++selected) {
      std::string instruction = std::string(opcode) + " h0";
      for (size_t index = 0; index < arity; ++index)
        instruction += index == selected ? ",$" : ",h1";
      instruction += ';';
      SCOPED_TRACE(instruction);
      const auto baseline = owned_half(half_source({instruction}, "h1"));
      ASSERT_TRUE(baseline) << baseline.error().front().message;
      const auto ast =
          test_helpers::parseModule(half_source({instruction}, "1.5"));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      EXPECT_FALSE(resolveModuleOnly(*ast));
      ++roles;
    }
  EXPECT_EQ(roles, 26u);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
