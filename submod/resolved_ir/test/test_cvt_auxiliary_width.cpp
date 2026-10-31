#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include <ptx_frontend/resolved_ir/model/data_movement/cvt.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** One auxiliary role; '$' marks its source operand without changing data formats. */
struct AuxiliaryCase {
  /** Valid modern instruction with one auxiliary placeholder. */
  std::string_view instruction;
  /** True for logical UE8M0x2 factors; false for logical B32 randomness. */
  bool factor;
};
/** Exact changed roles; layouts sharing a source pattern remain separate variants. */
constexpr std::array auxiliary_cases{
    AuxiliaryCase{"cvt.rs.f16x2.f32 d,a,b,$;", false},
    AuxiliaryCase{"cvt.rs.bf16x2.f32 d,a,b,$;", false},
    AuxiliaryCase{"cvt.rn.scaled::n2::ue8m0.bf16x2.e4m3x2 d,h,$;", true},
    AuxiliaryCase{"cvt.rn.scaled::n2::ue8m0.bf16x2.e2m3x2 d,h,$;", true},
    AuxiliaryCase{"cvt.rn.scaled::n2::ue8m0.bf16x2.e2m1x2 d,byte,$;", true},
    AuxiliaryCase{"cvt.rs.satfinite.e4m3x4.f32 d,{a,b,c,e},$;", false},
    AuxiliaryCase{"cvt.rs.satfinite.e2m1x4.f32 h,{a,b,c,e},$;", false},
    AuxiliaryCase{"cvt.rs.satfinite.e2m3x4.f32 d,{a,b,c,e},$;", false},
    AuxiliaryCase{"cvt.rn.satfinite.scaled::n2::ue8m0.s2f6x2.f32 h,a,b,$;",
                  true},
    AuxiliaryCase{"cvt.rn.satfinite.scaled::n2::ue8m0.s2f6x2.bf16x2 h,d,$;",
                  true},
    AuxiliaryCase{"cvt.rn.scaled::n2::ue8m0.bf16x2.s2f6x2 d,h,$;", true},
};
/** Keep all converted data nominally sized so only auxiliary width varies. */
std::string auxiliary_source(const AuxiliaryCase& test, std::string_view type,
                             std::string_view operand = "probe") {
  std::string instruction(test.instruction);
  instruction.replace(instruction.find('$'), 1, operand);
  return ".version 9.3\n.target sm_100a\n.entry kernel() { .reg ." +
         std::string(type) +
         " probe; .reg .b32 d; .reg .b16 h; .reg .b8 byte; "
         ".reg .f32 a,b,c,e; .reg .v2 .b64 V<2>; .reg .b128 wide; "
         ".reg .b8 narrow; .reg .f64 wrong; " +
         instruction + " }";
}
/** Match the selected exact target profile, including family capabilities. */
checker::Context auxiliary_context() {
  const auto profile = base::find_target_profile("sm_100a");
  return {
      .target = {.ptx_version = {9, 3},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
}
/** Return a draft after parser/source/AST destruction, independently of checking. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> auxiliary_draft(
    std::string source) {
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveModuleOnly(*ast);
}

TEST(CvtAuxiliaryWidth, AcceptsWiderInputsWithoutInheritingDataExceptions) {
  for (const auto& test : auxiliary_cases) {
    SCOPED_TRACE(test.instruction);
    const auto baseline = test_helpers::parseModule(
        auxiliary_source(test, test.factor ? "b16" : "b32"));
    ASSERT_TRUE(baseline && baseline.diagnostics.empty());
    ASSERT_TRUE(resolveAndValidateModule(*baseline));
    const auto ast = test_helpers::parseModule(auxiliary_source(test, "b64"));
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto draft = resolveModuleOnly(*ast);
    ASSERT_TRUE(draft);
    EXPECT_TRUE(
        draft->functions.front().body.front()->check(auxiliary_context()));
    EXPECT_TRUE(validateModule(*ast, *draft));
    EXPECT_TRUE(resolveAndValidateModule(*ast));
    auto owned = auxiliary_draft(auxiliary_source(test, "b64"));
    ASSERT_TRUE(owned);
    auto copied = *owned;
    ResolvedModule assigned;
    assigned = copied;
    assigned.functions.front().body.front() =
        assigned.functions.front().body.front()->clone();
    EXPECT_TRUE(validateModule(assigned));
  }
}
/** Borrow the two concrete representative auxiliary fields, without copying payload. */
WithLocs<ResolvedCvtSource>& auxiliary_operand(Instruction& instruction,
                                               bool factor) {
  if (factor)
    return *dynamic_cast<CvtRnBf16x2F8x2&>(instruction).scale_factor;
  return dynamic_cast<CvtRsF16x2F32&>(instruction).rbits;
}

TEST(CvtAuxiliaryWidth, KeepsAllLogicalPayloadWidthsAndRegisterShapes) {
  unsigned factors = 0;
  unsigned randoms = 0;
  const auto& descriptor = cvt_resolved_descriptor();
  for (const auto& variant : descriptor.variants) {
    for (const auto& layout : variant.operand_layouts) {
      for (const auto& binding : layout.bindings) {
        const bool factor = binding.target_field_id == "scale_factor";
        if (!factor && binding.target_field_id != "rbits")
          continue;
        SCOPED_TRACE(variant.variant_name);
        SCOPED_TRACE(layout.layout_id);
        if (factor)
          ++factors;
        else
          ++randoms;
        EXPECT_EQ(binding.register_width_policy,
                  base::ScalarTypeSizePolicy::EqualOrWider);
        EXPECT_EQ(binding.type_expression.kind,
                  checker::OperandTypeExpressionKind::FixedScalar);
        EXPECT_EQ(binding.type_expression.fixed_scalar_type,
                  factor ? ScalarType::B16 : ScalarType::B32);
        // The descriptor retains logical low16/low32 payload sizes, independently
        // of the wider register's actual dtype. No register value is simulated.
        EXPECT_EQ(
            base::scalar_size_of(binding.type_expression.fixed_scalar_type),
            factor ? 2 : 4);
        EXPECT_EQ(binding.allowed_shapes,
                  checker::OperandShape::Register |
                      checker::OperandShape::SpecialRegister);
      }
    }
  }
  EXPECT_EQ(factors, 6u);
  EXPECT_EQ(randoms, 5u);
}

TEST(CvtAuxiliaryWidth, AcceptsEligibleCarrierClassesAtEveryChangedRole) {
  for (const auto& test : auxiliary_cases) {
    SCOPED_TRACE(test.instruction);
    const std::vector<std::string_view> types =
        test.factor ? std::vector<std::string_view>{"b16", "b32", "b64", "b128"}
                    : std::vector<std::string_view>{
                          "b32", "b64", "b128", "s32", "u32",
                          "f32", "s64", "u64",  "f64", "f16x2"};
    for (const auto type : types) {
      SCOPED_TRACE(type);
      const auto ast = test_helpers::parseModule(auxiliary_source(test, type));
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      ASSERT_TRUE(resolveAndValidateModule(*ast));
      auto module = auxiliary_draft(auxiliary_source(test, type));
      ASSERT_TRUE(module);
      auto copied = *module;
      ResolvedModule assigned;
      assigned = copied;
      assigned.functions.front().body.front() =
          assigned.functions.front().body.front()->clone();
      EXPECT_TRUE(validateModule(assigned));
    }
    for (const auto type : {"b8", "pred", "bf16x2"}) {
      const auto ast = test_helpers::parseModule(auxiliary_source(test, type));
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
    const auto bad_types =
        test.factor
            ? std::vector<std::string_view>{"s16", "u16", "f16", "s32", "u32",
                                            "f32", "s64", "u64", "f64"}
            : std::vector<std::string_view>{"b16", "s16", "u16", "f16"};
    for (const auto type : bad_types) {
      SCOPED_TRACE(type);
      const auto ast = test_helpers::parseModule(auxiliary_source(test, type));
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      auto draft = resolveModuleOnly(*ast);
      ASSERT_TRUE(draft);
      EXPECT_FALSE(
          draft->functions.front().body.front()->check(auxiliary_context()));
      EXPECT_FALSE(validateModule(*ast, *draft));
      EXPECT_FALSE(resolveAndValidateModule(*ast));
      auto owned = auxiliary_draft(auxiliary_source(test, type));
      ASSERT_TRUE(owned);
      EXPECT_FALSE(validateModule(*owned));
    }
    for (const auto operand : {"0", "{probe,probe}", "V1"}) {
      const auto ast =
          test_helpers::parseModule(auxiliary_source(test, "b64", operand));
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
  }
}

TEST(CvtAuxiliaryWidth, OwnsWiderComponentsAndCoherentScalarRetargets) {
  for (const unsigned index : {0u, 2u}) {
    const auto& test = auxiliary_cases[index];
    const auto ast =
        test_helpers::parseModule(auxiliary_source(test, "b64", "V1.y"));
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    ASSERT_TRUE(resolveAndValidateModule(*ast));
    auto owned = auxiliary_draft(auxiliary_source(test, "b64", "V1.y"));
    ASSERT_TRUE(owned);
    auto copied = *owned;
    ResolvedModule assigned;
    assigned = copied;
    assigned.functions.front().body.front() =
        assigned.functions.front().body.front()->clone();
    auto& slot = auxiliary_operand(*assigned.functions.front().body.front(),
                                   test.factor);
    auto& ref = std::get<ResolvedRegisterRef>(slot.value);
    ASSERT_TRUE(ref.symbol_id);
    ASSERT_TRUE(ref.component);
    EXPECT_EQ(assigned.symbols.symbol(*ref.symbol_id).name, "V");
    EXPECT_EQ(ref.declared_type, ScalarType::B64);
    EXPECT_EQ(base::scalar_size_of(*ref.declared_type), 8u);
    EXPECT_EQ(ref.parameterized_index, 1);
    EXPECT_EQ(ref.component->lane, 1);
    EXPECT_EQ(ref.component->origin, RegisterComponentOrigin::ExplicitSelector);
    EXPECT_FALSE(ref.vector_width);
    ASSERT_FALSE(slot.locs.empty());
    EXPECT_EQ(ref.component->range, slot.locs.front());
    EXPECT_TRUE(validateModule(assigned));
    ref.component->lane = 2;
    EXPECT_FALSE(validateModule(assigned));

    for (const auto name : {"wide", "narrow", "wrong"}) {
      auto retarget = *owned;
      auto& target = std::get<ResolvedRegisterRef>(
          auxiliary_operand(*retarget.functions.front().body.front(),
                            test.factor)
              .value);
      const auto symbol = retarget.symbols.lookup(
          retarget.functions.front().declaration_scope, name);
      ASSERT_TRUE(symbol);
      target = ResolvedRegisterRef{
          .spelling = name,
          .register_class = ResolvedRegisterClass::General,
          .symbol_id = symbol->symbol,
          .declared_type = std::string_view(name) == "wide" ? ScalarType::B128
                           : std::string_view(name) == "narrow"
                               ? ScalarType::B8
                               : ScalarType::F64};
      const bool legal = std::string_view(name) == "wide" ||
                         (!test.factor && std::string_view(name) == "wrong");
      EXPECT_EQ(validateModule(retarget).has_value(), legal);
      // Original syntax identity remains retained; typed semantic mutation is independent.
      EXPECT_EQ(validateModule(*ast, retarget).has_value(), legal);
      if (legal) {
        target.declared_type = ScalarType::B32;
        EXPECT_FALSE(validateModule(retarget));
      }
    }
    auto whole = *owned;
    auto& whole_ref = std::get<ResolvedRegisterRef>(
        auxiliary_operand(*whole.functions.front().body.front(), test.factor)
            .value);
    whole_ref.component.reset();
    whole_ref.spelling = "V1";
    whole_ref.vector_width = 2;
    EXPECT_FALSE(validateModule(whole));

    const auto replacement =
        test_helpers::parseModule(auxiliary_source(test, "b64", "wide"));
    ASSERT_TRUE(replacement && replacement.diagnostics.empty());
    ASSERT_TRUE(resolveAndValidateModule(*replacement));
    const auto stale = validateModule(*replacement, *owned);
    ASSERT_FALSE(stale);
    ASSERT_EQ(stale.error().size(), 1u);
    EXPECT_EQ(stale.error().front().kind,
              checker::CheckDiagnosticKind::ModuleSourceMismatch);
  }
}

TEST(CvtAuxiliaryWidth, PreservesIndependentDataWidthsAndQualifierCoupling) {
  auto source = auxiliary_source(auxiliary_cases[9], "b128");
  auto ast = test_helpers::parseModule(source);
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  ASSERT_TRUE(resolveAndValidateModule(*ast));
  // BF16x2 converted source remains exact B32 while its independent factor is B128.
  const auto position = source.find("h,d,probe;");
  ASSERT_NE(position, std::string::npos);
  source.replace(position, std::string("h,d,probe;").size(), "h,wide,probe;");
  ast = test_helpers::parseModule(source);
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  auto draft = resolveModuleOnly(*ast);
  ASSERT_TRUE(draft);
  const auto bad_data = validateModule(*draft);
  ASSERT_FALSE(bad_data);
  EXPECT_TRUE(std::ranges::any_of(bad_data.error(), [](const auto& diagnostic) {
    return diagnostic.kind ==
               checker::CheckDiagnosticKind::OperandTypeMismatch &&
           diagnostic.message.find("exact-width") != std::string::npos;
  }));
  EXPECT_FALSE(resolveAndValidateModule(*ast));

  source = auxiliary_source(auxiliary_cases[1], "b128");
  const auto destination = source.find("f32 d,a,b,probe;");
  ASSERT_NE(destination, std::string::npos);
  source.replace(destination, std::string("f32 d,a,b,probe;").size(),
                 "f32 wide,a,b,probe;");
  ast = test_helpers::parseModule(source);
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  EXPECT_FALSE(resolveAndValidateModule(*ast));
  for (const auto instruction :
       {"cvt.rn.bf16x2.e4m3x2 d,h,probe;",
        "cvt.rn.scaled::n2::ue8m0.bf16x2.e4m3x2 d,h;"}) {
    auto coupled_source = auxiliary_source(auxiliary_cases[2], "b128");
    const auto begin = coupled_source.find("cvt.");
    coupled_source.replace(begin, coupled_source.find(';', begin) - begin + 1,
                           instruction);
    const auto inconsistent = test_helpers::parseModule(coupled_source);
    // Missing/extra auxiliary must not gain a layout through relaxed width.
    ASSERT_TRUE(inconsistent && inconsistent.diagnostics.empty());
    EXPECT_FALSE(resolveAndValidateModule(*inconsistent));
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
