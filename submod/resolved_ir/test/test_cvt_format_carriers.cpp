#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/model/data_movement/cvt.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Complete modern context with known-valid FP8 and intrinsic availability. */
std::string format_source(std::string declarations, std::string instruction) {
  return ".version 9.3\n.target sm_100a\n.entry kernel() { " + declarations +
         instruction + " }";
}
/** Match the exact modern profile rather than assume family capabilities. */
checker::Context format_context() {
  const auto profile = base::find_target_profile("sm_100a");
  return {
      .target = {.ptx_version = {9, 3},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
}
/** Reparse source, require a draft, and expose source/direct/owned validation separately. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> format_draft(
    std::string source) {
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveModuleOnly(*ast);
}

/** Logical FP8 format requires Bit declarations, independently of permitted width. */
TEST(CvtFormatCarriers,
     RejectsNonBitFp8DestinationsAtSourceDirectAndOwnedStages) {
  for (const auto type : {"b16", "b32", "b128"}) {
    const auto ast = test_helpers::parseModule(
        format_source(".reg ." + std::string(type) + " dst; .reg .f32 a,b; ",
                      "cvt.rn.satfinite.e4m3x2.f32 dst,a,b;"));
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    EXPECT_TRUE(resolveAndValidateModule(*ast));
  }
  for (const auto type : {"s16", "u16", "f16", "s32", "u32", "f32"}) {
    SCOPED_TRACE(type);
    const auto source =
        format_source(".reg ." + std::string(type) + " dst; .reg .f32 a,b; ",
                      "cvt.rn.satfinite.e4m3x2.f32 dst,a,b;");
    const auto ast = test_helpers::parseModule(source);
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto module = resolveModuleOnly(*ast);
    ASSERT_TRUE(module) << (module.error().empty()
                                ? ""
                                : module.error().front().message);
    const auto direct =
        module->functions.front().body.front()->check(format_context());
    ASSERT_FALSE(direct);
    const auto& form = dynamic_cast<const CvtRnSatfiniteF8x2F32&>(
        *module->functions.front().body.front());
    ASSERT_FALSE(form.dst.locs.empty());
    const auto carrier =
        std::ranges::find_if(direct.error(), [](const auto& diagnostic) {
          return diagnostic.message.find("requires Bit register storage") !=
                 std::string::npos;
        });
    ASSERT_NE(carrier, direct.error().end());
    EXPECT_EQ(carrier->kind, checker::CheckDiagnosticKind::OperandTypeMismatch);
    EXPECT_EQ(carrier->range, form.dst.locs.front());
    EXPECT_NE(carrier->message.find(".e4m3x2"), std::string::npos);
    EXPECT_NE(carrier->message.find("'dst'"), std::string::npos);
    EXPECT_FALSE(validateModule(*ast, *module));
    EXPECT_FALSE(resolveAndValidateModule(*ast));
    auto owned = format_draft(source);
    ASSERT_TRUE(owned);
    auto copied = *owned;
    copied.functions.front().body.front() =
        copied.functions.front().body.front()->clone();
    EXPECT_FALSE(validateModule(copied));
  }
}

/** Intrinsic canonical dtype participates in the existing BF16 exact-width exception. */
TEST(CvtFormatCarriers, RejectsWideBitIntrinsicForExactBfloatSource) {
  const auto baseline = test_helpers::parseModule(format_source(
      ".reg .f32 dst; .reg .b16 bits; ", "cvt.f32.bf16 dst,bits;"));
  ASSERT_TRUE(baseline && baseline.diagnostics.empty());
  ASSERT_TRUE(resolveAndValidateModule(*baseline));
  const auto intrinsic = base::lookup("%envreg0");
  ASSERT_TRUE(intrinsic);
  EXPECT_EQ(intrinsic->element_type, ScalarType::B32);
  const auto source =
      format_source(".reg .f32 dst; ", "cvt.f32.bf16 dst,%envreg0;");
  const auto ast = test_helpers::parseModule(source);
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  auto module = resolveModuleOnly(*ast);
  ASSERT_TRUE(module);
  EXPECT_FALSE(module->functions.front().body.front()->check(format_context()));
  EXPECT_FALSE(validateModule(*ast, *module));
  EXPECT_FALSE(resolveAndValidateModule(*ast));
  auto owned = format_draft(source);
  ASSERT_TRUE(owned);
  EXPECT_FALSE(validateModule(*owned));
}
/** One written data role, with its logical format and independent width exception. */
struct FormatCase {
  std::string_view instruction;
  std::string_view role;
  std::string_view bit_type;
  bool exact_width = false;
};

/** Shared declarations keep auxiliary and unmodified data roles legal. */
std::string case_source(const FormatCase& test, std::string_view type,
                        std::string_view replacement = "probe") {
  std::string instruction(test.instruction);
  instruction.replace(instruction.find('$'), 1, replacement);
  return format_source(
      ".reg ." + std::string(type) +
          " probe; .reg .b32 d,r; .reg .b16 h; .reg .b8 byte; "
          ".reg .f32 a,b,c,e; .reg .v2 .b32 V<2>; "
          ".reg .b128 wide; .reg .f32 wrong; .reg .f16x2 native; ",
      instruction);
}

/** Exercise each native exception role, including physical-Bit source layouts. */
constexpr std::array native_cases{
    FormatCase{"cvt.rn.f16x2.f32 $,a,b;", "dst", "b32"},
    FormatCase{"cvt.rz.f16x2.f32 $,a,b;", "dst", "b32"},
    FormatCase{"cvt.rs.f16x2.f32 $,a,b,r;", "dst", "b32"},
    FormatCase{"cvt.rn.satfinite.e4m3x2.f16x2 h,$;", "src", "b32"},
    FormatCase{"cvt.rn.f16x2.e4m3x2 $,h;", "dst", "b32"},
    FormatCase{"cvt.rn.satfinite.e2m1x2.f16x2 byte,$;", "src", "b32"},
    FormatCase{"cvt.rn.satfinite.e2m3x2.f16x2 h,$;", "src", "b32"},
    FormatCase{"cvt.rn.f16x2.e2m3x2 $,h;", "dst", "b32"},
    FormatCase{"cvt.rn.f16x2.e2m1x2 $,byte;", "dst", "b32"},
};

/** Representative logical formats and distinct factor role, not modifier products. */
constexpr std::array encoded_cases{
    FormatCase{"cvt.rn.bf16.f32 $,a;", "dst", "b16", true},
    FormatCase{"cvt.f32.bf16 a,$;", "src", "b16", true},
    FormatCase{"cvt.rn.bf16x2.f32 $,a,b;", "dst", "b32", true},
    FormatCase{"cvt.rn.satfinite.e4m3x2.bf16x2 h,$;", "src", "b32", true},
    FormatCase{"cvt.rna.tf32.f32 $,a;", "dst", "b32", true},
    FormatCase{"cvt.rn.satfinite.e4m3x2.f32 $,a,b;", "dst", "b16"},
    FormatCase{"cvt.rn.satfinite.e5m2x2.f32 $,a,b;", "dst", "b16"},
    FormatCase{"cvt.rn.f16x2.e4m3x2 d,$;", "src", "b16"},
    FormatCase{"cvt.rn.f16x2.e5m2x2 d,$;", "src", "b16"},
    FormatCase{"cvt.rn.satfinite.e2m1x2.f16x2 $,d;", "dst", "b8"},
    FormatCase{"cvt.rn.satfinite.e2m3x2.f32 $,a,b;", "dst", "b16"},
    FormatCase{"cvt.rn.satfinite.e3m2x2.f32 $,a,b;", "dst", "b16"},
    FormatCase{"cvt.rn.f16x2.e2m1x2 d,$;", "src", "b8"},
    FormatCase{"cvt.rn.f16x2.e2m3x2 d,$;", "src", "b16"},
    FormatCase{"cvt.rn.f16x2.e3m2x2 d,$;", "src", "b16"},
    FormatCase{"cvt.rs.satfinite.e4m3x4.f32 $,{a,b,c,e},r;", "dst", "b32"},
    FormatCase{"cvt.rs.satfinite.e5m2x4.f32 $,{a,b,c,e},r;", "dst", "b32"},
    FormatCase{"cvt.rs.satfinite.e2m1x4.f32 $,{a,b,c,e},r;", "dst", "b16"},
    FormatCase{"cvt.rs.satfinite.e2m3x4.f32 $,{a,b,c,e},r;", "dst", "b32"},
    FormatCase{"cvt.rs.satfinite.e3m2x4.f32 $,{a,b,c,e},r;", "dst", "b32"},
    FormatCase{"cvt.rz.ue8m0x2.f32 $,a,b;", "dst", "b16"},
    FormatCase{"cvt.rn.bf16x2.ue8m0x2 d,$;", "src", "b16"},
    FormatCase{"cvt.rn.satfinite.s2f6x2.f32 $,a,b;", "dst", "b16"},
    FormatCase{"cvt.rn.bf16x2.s2f6x2 d,$;", "src", "b16"},
    FormatCase{"cvt.rn.scaled::n2::ue8m0.bf16x2.e4m3x2 d,h,$;", "scale_factor",
               "b16"},
    FormatCase{"cvt.rn.scaled::n2::ue8m0.bf16x2.e2m3x2 d,h,$;", "scale_factor",
               "b16"},
    FormatCase{"cvt.rn.scaled::n2::ue8m0.bf16x2.e2m1x2 d,byte,$;",
               "scale_factor", "b16"},
    FormatCase{"cvt.rn.satfinite.scaled::n2::ue8m0.s2f6x2.f32 h,a,b,$;",
               "scale_factor", "b16"},
    FormatCase{"cvt.rn.satfinite.scaled::n2::ue8m0.s2f6x2.bf16x2 h,d,$;",
               "scale_factor", "b16"},
    FormatCase{"cvt.rn.scaled::n2::ue8m0.bf16x2.s2f6x2 d,h,$;", "scale_factor",
               "b16"},
};

TEST(CvtFormatCarriers, PreservesEveryNativeExceptionRoleAndWiderBits) {
  for (const auto& test : native_cases) {
    SCOPED_TRACE(test.instruction);
    for (const auto type : {"f16x2", "b32", "b64", "b128"}) {
      SCOPED_TRACE(type);
      auto module = format_draft(case_source(test, type));
      ASSERT_TRUE(module);
      EXPECT_TRUE(validateModule(*module));
      auto copied = *module;
      ResolvedModule assigned;
      assigned = copied;
      assigned.functions.front().body.front() =
          assigned.functions.front().body.front()->clone();
      EXPECT_TRUE(validateModule(assigned));
      const auto ast = test_helpers::parseModule(case_source(test, type));
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      EXPECT_TRUE(resolveAndValidateModule(*ast));
    }
    for (const auto type : {"s32", "u32", "f32", "b16", "pred", "bf16x2"}) {
      const auto ast = test_helpers::parseModule(case_source(test, type));
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      auto draft = resolveModuleOnly(*ast);
      if (draft) {
        EXPECT_FALSE(validateModule(*ast, *draft));
        EXPECT_FALSE(validateModule(*draft));
      }
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
  }
}

TEST(CvtFormatCarriers, ChecksEncodedKindsIndependentlyOfDataWidths) {
  for (const auto& test : encoded_cases) {
    SCOPED_TRACE(test.instruction);
    auto valid = format_draft(case_source(test, test.bit_type));
    ASSERT_TRUE(valid);
    const auto checked = validateModule(*valid);
    ASSERT_TRUE(checked) << checked.error().front().message;
    auto copied = *valid;
    copied.functions.front().body.front() =
        copied.functions.front().body.front()->clone();
    EXPECT_TRUE(validateModule(copied));
    const auto baseline =
        test_helpers::parseModule(case_source(test, test.bit_type));
    ASSERT_TRUE(baseline && baseline.diagnostics.empty());
    EXPECT_TRUE(resolveAndValidateModule(*baseline));
    // Data widening is independent of declaration kind; auxiliary widening is
    // tested separately from these nominal factor controls.
    if (!test.exact_width && test.role != "scale_factor") {
      for (const auto width : {"b16", "b32", "b64", "b128"}) {
        if (base::find_scalar_type_metadata("." + std::string(width))
                ->size_bytes <
            base::find_scalar_type_metadata("." + std::string(test.bit_type))
                ->size_bytes)
          continue;
        auto wide = format_draft(case_source(test, width));
        ASSERT_TRUE(wide);
        EXPECT_TRUE(validateModule(*wide));
      }
    }
    for (const auto type :
         {"s16", "u16", "f16", "s32", "u32", "f32", "s64", "f64"}) {
      const auto ast = test_helpers::parseModule(case_source(test, type));
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      auto draft = resolveModuleOnly(*ast);
      if (draft) {
        EXPECT_FALSE(validateModule(*ast, *draft));
        EXPECT_FALSE(validateModule(*draft));
      }
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
    if (test.exact_width && test.role != "scale_factor") {
      auto wide = format_draft(case_source(test, "b128"));
      ASSERT_TRUE(wide);
      EXPECT_FALSE(validateModule(*wide));
    }
  }
}

TEST(CvtFormatCarriers, PreservesComponentIdentityAndOwnedRetargets) {
  const FormatCase test{"cvt.rn.satfinite.e4m3x2.f32 $,a,b;", "dst", "b16"};
  auto module = format_draft(case_source(test, "b16", "V1.y"));
  ASSERT_TRUE(module);
  auto copied = *module;
  ResolvedModule assigned;
  assigned = copied;
  assigned.functions.front().body.front() =
      assigned.functions.front().body.front()->clone();
  auto& form = dynamic_cast<CvtRnSatfiniteF8x2F32&>(
      *assigned.functions.front().body.front());
  auto& ref = form.dst.value;
  ASSERT_TRUE(ref.symbol_id);
  ASSERT_TRUE(ref.component);
  EXPECT_EQ(ref.declared_type, ScalarType::B32);
  EXPECT_EQ(ref.parameterized_index, 1);
  EXPECT_EQ(ref.component->lane, 1);
  EXPECT_FALSE(ref.vector_width);
  EXPECT_EQ(ref.component->origin, RegisterComponentOrigin::ExplicitSelector);
  EXPECT_EQ(assigned.symbols.symbol(*ref.symbol_id).name, "V");
  ASSERT_FALSE(form.dst.locs.empty());
  EXPECT_EQ(ref.component->range, form.dst.locs.front());
  EXPECT_TRUE(validateModule(assigned));
  ref.component->lane = 2;
  EXPECT_FALSE(validateModule(assigned));
  for (const auto name : {"wide", "wrong"}) {
    auto retarget = *module;
    auto& target = dynamic_cast<CvtRnSatfiniteF8x2F32&>(
                       *retarget.functions.front().body.front())
                       .dst.value;
    const auto symbol = retarget.symbols.lookup(
        retarget.functions.front().declaration_scope, name);
    ASSERT_TRUE(symbol);
    target = ResolvedRegisterRef{
        .spelling = name,
        .register_class = ResolvedRegisterClass::General,
        .symbol_id = symbol->symbol,
        .declared_type = std::string_view(name) == "wide" ? ScalarType::B128
                                                          : ScalarType::F32};
    EXPECT_EQ(validateModule(retarget).has_value(),
              std::string_view(name) == "wide");
    if (std::string_view(name) == "wide") {
      target.declared_type = ScalarType::B32;
      EXPECT_FALSE(validateModule(retarget));
    }
  }
  auto whole = format_draft(case_source(test, "b16", "V1"));
  EXPECT_FALSE(whole);
  auto owned_whole = *module;
  auto& whole_ref = dynamic_cast<CvtRnSatfiniteF8x2F32&>(
                        *owned_whole.functions.front().body.front())
                        .dst.value;
  whole_ref.component.reset();
  whole_ref.spelling = "V1";
  whole_ref.vector_width = 2;
  EXPECT_FALSE(validateModule(owned_whole));

  const auto native_ast =
      test_helpers::parseModule(case_source(native_cases[4], "b32"));
  ASSERT_TRUE(native_ast && native_ast.diagnostics.empty());
  auto native = resolveModuleOnly(*native_ast);
  ASSERT_TRUE(native);
  for (const auto name : {"native", "wide", "wrong"}) {
    auto retarget = *native;
    auto& target =
        dynamic_cast<CvtRnF16x2F8x2&>(*retarget.functions.front().body.front())
            .dst.value;
    const auto symbol = retarget.symbols.lookup(
        retarget.functions.front().declaration_scope, name);
    ASSERT_TRUE(symbol);
    target.symbol_id = symbol->symbol;
    target.spelling = name;
    target.declared_type = std::string_view(name) == "native"
                               ? ScalarType::F16x2
                           : std::string_view(name) == "wide" ? ScalarType::B128
                                                              : ScalarType::F32;
    EXPECT_EQ(validateModule(retarget).has_value(),
              std::string_view(name) != "wrong");
    // The original AST still matches retained syntax identity; current typed
    // references are validated independently of that original spelling.
    EXPECT_EQ(validateModule(*native_ast, retarget).has_value(),
              std::string_view(name) != "wrong");
  }
  const auto replacement =
      test_helpers::parseModule(case_source(native_cases[4], "b32", "native"));
  ASSERT_TRUE(replacement && replacement.diagnostics.empty());
  ASSERT_TRUE(resolveAndValidateModule(*replacement));
  const auto stale = validateModule(*replacement, *native);
  ASSERT_FALSE(stale);
  ASSERT_EQ(stale.error().size(), 1u);
  EXPECT_EQ(stale.error().front().kind,
            checker::CheckDiagnosticKind::ModuleSourceMismatch);

  auto source = format_draft(case_source(
      FormatCase{"cvt.rn.f16x2.e4m3x2 d,$;", "src", "b16"}, "b16", "%envreg0"));
  ASSERT_TRUE(source);
  EXPECT_TRUE(validateModule(*source));
  auto integer = format_draft(case_source(
      FormatCase{"cvt.rn.f16x2.e4m3x2 d,$;", "src", "b16"}, "b16", "%laneid"));
  ASSERT_TRUE(integer);
  EXPECT_FALSE(validateModule(*integer));
}

TEST(CvtFormatCarriers, KeepsRandomBitsAndOrdinaryConversionsIndependent) {
  const FormatCase random{"cvt.rs.f16x2.f32 d,a,b,$;", "rbits", "b32"};
  for (const auto type : {"b32", "s32", "u32", "f32"}) {
    const auto ast = test_helpers::parseModule(case_source(random, type));
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    EXPECT_TRUE(resolveAndValidateModule(*ast));
    auto owned = format_draft(case_source(random, type));
    ASSERT_TRUE(owned);
    auto copied = *owned;
    copied.functions.front().body.front() =
        copied.functions.front().body.front()->clone();
    EXPECT_TRUE(validateModule(copied));
  }
  for (const auto instruction :
       {"cvt.f32.f16 a,h;", "cvt.u32.u32 d,r;", "cvt.rni.u32.f32 d,a;",
        "cvt.pack.sat.u2.s32.b32 d,si,sj,r;"}) {
    SCOPED_TRACE(instruction);
    const auto ast = test_helpers::parseModule(format_source(
        ".reg .f32 a; .reg .b16 h; .reg .b32 d,r; .reg .s32 si,sj; ",
        instruction));
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    const auto result = resolveAndValidateModule(*ast);
    EXPECT_TRUE(result) << (result.error().empty()
                                ? ""
                                : result.error().front().message);
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
