#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/model/arithmetic/mad.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Detect an exposed rounding member without inventing a default for omission. */
template <typename T>
concept HasRoundingMember = requires(T instruction) { instruction.rounding; };
static_assert(!HasRoundingMember<MadImplicitF32>);

/** Build a complete function with typed floating, bit and invalid-width controls. */
std::string implicitMadModule(std::string_view operation,
                              std::string_view target = "sm_10",
                              std::string_view version = {}) {
  std::string selected_version{version};
  if (selected_version.empty()) {
    const auto profile = base::find_target_profile(target);
    if (profile)
      selected_version = std::to_string(profile->minimum_ptx_version.major) +
                         "." +
                         std::to_string(profile->minimum_ptx_version.minor);
  }
  return ".version " + selected_version + "\n.target " + std::string(target) +
         "\n.func f() { .reg .f32 %f<4>; .reg .b32 %r<4>; "
         ".reg .f64 %d<4>; .reg .u32 %u; " +
         std::string(operation) + " }\n";
}

/** Require AST-backed and strict owned checks to agree on a resolved source. */
void expectMadAvailability(const std::string& source, bool available) {
  PtxSyntaxParser parser{source};
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast);
  ASSERT_TRUE(ast.diagnostics.empty());
  const auto resolved = resolveModuleOnly(*ast);
  ASSERT_TRUE(resolved) << resolved.error().front().message;
  EXPECT_EQ(validateModule(*ast, *resolved).has_value(), available);
  EXPECT_EQ(validateModule(*resolved).has_value(), available);
}

/** All supported sm1x targets and both optional flags retain no-rounding identity. */
TEST(LegacyImplicitMad, OwnsEveryTargetAndFlagCombinationWithoutRounding) {
  for (const auto target : {"sm_10", "sm_11", "sm_12", "sm_13"}) {
    for (const auto flags : {"", ".ftz", ".sat", ".ftz.sat"}) {
      SCOPED_TRACE(target);
      SCOPED_TRACE(flags);
      const auto operation =
          "mad" + std::string(flags) + ".f32 %r0, %f1, 1.0, %r3;";
      std::optional<ResolvedModule> owned;
      {
        const auto source = implicitMadModule(operation, target);
        PtxSyntaxParser parser{source};
        const auto ast = parser.parseModule();
        ASSERT_TRUE(ast);
        auto resolved = resolveAndValidateModule(*ast);
        ASSERT_TRUE(resolved) << resolved.error().front().message;
        ASSERT_TRUE(validateModule(*ast, *resolved));
        owned.emplace(std::move(*resolved));
        PtxSyntaxParser instruction_parser{operation};
        const auto instruction_ast = instruction_parser.parseInstruction();
        ASSERT_TRUE(instruction_ast);
        EXPECT_EQ(instruction_ast->opcode.syntax.text, "mad");
        EXPECT_TRUE(std::ranges::none_of(
            instruction_ast->modifiers, [](const auto& modifier) {
              return modifier.syntax.text == ".rn" ||
                     modifier.syntax.text == ".rz" ||
                     modifier.syntax.text == ".rm" ||
                     modifier.syntax.text == ".rp";
            }));
      }
      ASSERT_TRUE(validateModule(*owned));
      const auto& function = owned->functions.front();
      ASSERT_EQ(function.body.size(), 1u);
      EXPECT_EQ(function.source_target, target);
      EXPECT_EQ(function.instruction_opcodes.front(), "mad");
      const auto* mad =
          dynamic_cast<const MadImplicitF32*>(function.body[0].get());
      ASSERT_NE(mad, nullptr);
      EXPECT_EQ(mad->instruction_kind(), MadImplicitF32::kind);
      EXPECT_EQ(MadImplicitF32::type, ScalarType::F32);
      const bool ftz =
          std::string_view(flags).find(".ftz") != std::string_view::npos;
      const bool sat =
          std::string_view(flags).find(".sat") != std::string_view::npos;
      EXPECT_EQ(mad->ftz.value, ftz);
      EXPECT_EQ(mad->saturate.value, sat);
      EXPECT_EQ(mad->ftz.locs.empty(), !ftz);
      EXPECT_EQ(mad->saturate.locs.empty(), !sat);
      EXPECT_EQ(mad->dst.value.declared_type, ScalarType::B32);
      EXPECT_EQ(std::get<ResolvedRegisterRef>(mad->src1.value).declared_type,
                ScalarType::F32);
      EXPECT_EQ(std::get<ResolvedImmediate>(mad->src2.value).type,
                ScalarType::F32);
      EXPECT_EQ(std::get<ResolvedRegisterRef>(mad->src3.value).declared_type,
                ScalarType::B32);
      EXPECT_FALSE(mad->dst.locs.empty());
      EXPECT_FALSE(mad->src1.locs.empty());
      EXPECT_FALSE(mad->src2.locs.empty());
      EXPECT_FALSE(mad->src3.locs.empty());
      auto clone = mad->clone();
      EXPECT_EQ(clone->instruction_kind(), MadImplicitF32::kind);
      auto& mutable_mad = dynamic_cast<MadImplicitF32&>(*function.body[0]);
      auto& immediate = std::get<ResolvedImmediate>(mutable_mad.src2.value);
      immediate.type = ScalarType::F64;
      EXPECT_FALSE(validateModule(*owned));
      immediate.type = ScalarType::F32;
      ASSERT_TRUE(validateModule(*owned));
    }
  }
}

/** Exact identity, generic flavor and the PTX1.0 floor are conjunctive gates. */
TEST(LegacyImplicitMad, RequiresExactLegacyIdentityAndOriginalPtxFloor) {
  PtxSyntaxParser parser{"mad.f32 %f0, %f1, %f2, %f3;"};
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast);
  const auto resolved = resolveMad(*ast);
  ASSERT_TRUE(resolved);
  for (const auto sm : {10u, 11u, 12u, 13u}) {
    const auto spelling = "sm_" + std::to_string(sm);
    checker::TargetInfo target{
        .ptx_version = {1, 0},
        .sm_version = sm,
        .identity = base::parse_target_identity(spelling)};
    const auto check = [&] {
      return (*resolved)->check(checker::Context{.target = target});
    };
    EXPECT_TRUE(check());
    target.ptx_version = {0, 9};
    EXPECT_FALSE(check());
    target.ptx_version = {1, 0};
    target.identity.reset();
    EXPECT_FALSE(check());
    for (const auto suffix : {"a", "f"}) {
      target.identity = base::parse_target_identity(spelling + suffix);
      EXPECT_FALSE(check());
    }
  }
  for (const auto target : {"sm_20", "sm_80", "sm_100", "sm_100a", "sm_100f"})
    expectMadAvailability(
        implicitMadModule("mad.f32 %f0, %f1, %f2, %f3;", target, "9.3"), false);
}

/** Explicit FP32 rounding still rejects sm1x, with independent positive controls. */
TEST(LegacyImplicitMad, PreservesExplicitAndIntegerAvailability) {
  for (const auto rounding : {"rn", "rz", "rm", "rp"}) {
    const auto operation =
        "mad." + std::string(rounding) + ".f32 %f0, %f1, %f2, %f3;";
    for (const auto target : {"sm_10", "sm_11", "sm_12", "sm_13"})
      expectMadAvailability(implicitMadModule(operation, target, "9.3"), false);
    expectMadAvailability(implicitMadModule(operation, "sm_20", "2.0"), true);
    expectMadAvailability(implicitMadModule(operation, "sm_80", "9.3"), true);
    const auto f64 =
        "mad." + std::string(rounding) + ".f64 %d0, %d1, %d2, %d3;";
    expectMadAvailability(implicitMadModule(f64, "sm_13"), true);
    expectMadAvailability(implicitMadModule(f64, "sm_80", "9.3"), true);
  }
  for (const auto target : {"sm_10", "sm_11", "sm_12", "sm_13", "sm_80"})
    expectMadAvailability(
        implicitMadModule("mad.lo.u32 %u, %u, %u, %u;", target), true);
}

/** Omission never enables other types, illegal flag order or malformed operands. */
TEST(LegacyImplicitMad, RejectsInvalidTypesWidthsOrderAndCounts) {
  for (const auto operation : {
           "mad.f64 %d0, %d1, %d2, %d3;",
           "mad.f16 %r0, %r1, %r2, %r3;",
           "mad.sat.ftz.f32 %f0, %f1, %f2, %f3;",
           "mad.ftz.rn.f32 %f0, %f1, %f2, %f3;",
           "mad.f32.ftz %f0, %f1, %f2, %f3;",
           "mad.f32 %f0, %f1, %f2;",
           "mad.f32 %f0, %f1, %f2, %f3, %f0;",
           "mad.f32 _, %f1, %f2, %f3;",
           "mad.f32 %f0, 1, %f2, %f3;",
           "mad.f32 %d0, %f1, %f2, %f3;",
           "mad.f32 %f0, %d1, %f2, %f3;",
           "mad.f32 %f0, %f1, %d2, %f3;",
           "mad.f32 %f0, %f1, %f2, %d3;",
           "mad.f32 %u, %f1, %f2, %f3;",
           "mad.f32 %f0, %u, %f2, %f3;",
           "mad.f32 %f0, %f1, %u, %f3;",
           "mad.f32 %f0, %f1, %f2, %u;",
       }) {
    SCOPED_TRACE(operation);
    const auto source = implicitMadModule(operation);
    PtxSyntaxParser parser{source};
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}

/** Retargeting evaluates availability without changing the owned source form. */
TEST(LegacyImplicitMad, RetargetsWithoutSynthesizingRoundingOrFma) {
  const auto original_source =
      implicitMadModule("mad.ftz.sat.f32 %f0, %f1, %f2, %f3;");
  const auto replacement_source =
      implicitMadModule("mad.ftz.sat.f32 %f0, %f1, %f2, %f3;", "sm_80", "9.3");
  PtxSyntaxParser original{original_source};
  PtxSyntaxParser replacement{replacement_source};
  const auto ast = original.parseModule();
  const auto retargeted = replacement.parseModule();
  ASSERT_TRUE(ast);
  ASSERT_TRUE(retargeted);
  const auto resolved = resolveAndValidateModule(*ast);
  ASSERT_TRUE(resolved);
  EXPECT_FALSE(validateModule(*retargeted, *resolved));
  EXPECT_TRUE(validateModule(*resolved));
  EXPECT_EQ(resolved->functions.front().body.front()->instruction_kind(),
            MadImplicitF32::kind);
  EXPECT_EQ(resolved->functions.front().instruction_opcodes.front(), "mad");
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
