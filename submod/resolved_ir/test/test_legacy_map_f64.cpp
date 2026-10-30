#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <ptx_frontend/resolved_ir/model/arithmetic/div.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/rcp.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/sqrt.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Canonical source operations eligible for the exact legacy source option. */
constexpr std::array<std::string_view, 3> kMappedOperations{
    "div.rn.f64 %d0, %d1, %d2;", "rcp.rn.f64 %d0, %d1;",
    "sqrt.rn.f64 %d0, %d1;"};

/** Build one function with independently bound floating and bit containers. */
std::string mappedModule(std::string_view operation, std::string_view target,
                         std::string_view options = {},
                         std::string_view version = "1.4") {
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) + std::string(options) +
         "\n.func f() { .reg .f64 %d<3>; .reg .b64 %rd<3>; "
         ".reg .f32 %f<3>; .reg .u64 %u; " +
         std::string(operation) + " }\n";
}

/** Require the same result through AST-backed and strictly owned checking. */
void expectModuleAvailability(const std::string& source, bool available) {
  PtxSyntaxParser parser{source};
  auto ast = parser.parseModule();
  ASSERT_TRUE(ast);
  ASSERT_TRUE(ast.diagnostics.empty());
  auto resolved = resolveModuleOnly(*ast);
  ASSERT_TRUE(resolved) << resolved.error().front().message;
  EXPECT_EQ(validateModule(*ast, *resolved).has_value(), available);
  EXPECT_EQ(validateModule(*resolved).has_value(), available);
}

/** Every affected operation requires exact spelling on each supported old target. */
TEST(LegacyMapF64, ChecksEachOperationAndSourceOptionBoundary) {
  for (const auto operation : kMappedOperations) {
    SCOPED_TRACE(operation);
    for (const auto target : {"sm_10", "sm_11", "sm_12"}) {
      SCOPED_TRACE(target);
      expectModuleAvailability(
          mappedModule(operation, target, ", map_f64_to_f32"), true);
      expectModuleAvailability(mappedModule(operation, target), false);
      expectModuleAvailability(
          mappedModule(operation, target, ", map_f64_to_f32", "1.3"), false);
      for (const auto option : {", map_f64_to_f32x", ", map_f64_to_f32_extra",
                                ", MAP_f64_to_f32"}) {
        const auto invalid_source = mappedModule(operation, target, option);
        PtxSyntaxParser invalid_parser{invalid_source};
        const auto invalid_ast = invalid_parser.parseModule();
        EXPECT_FALSE(invalid_ast && invalid_ast.diagnostics.empty());
      }
    }
    for (const auto target : {"sm_13", "sm_80"}) {
      expectModuleAvailability(
          mappedModule(operation, target, {},
                       target == std::string_view{"sm_80"} ? "7.0" : "1.4"),
          true);
      expectModuleAvailability(mappedModule(operation, target, {}, "1.3"),
                               false);
    }
  }
}

/** Exact legacy alternatives need typed option, identity, generic flavor and PTX. */
TEST(LegacyMapF64, ChecksConcreteFormsAgainstExactTargetIdentity) {
  for (const auto operation : kMappedOperations) {
    SCOPED_TRACE(operation);
    PtxSyntaxParser parser{operation};
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast);
    const auto resolved = resolveInstruction(*ast);
    ASSERT_TRUE(resolved);
    for (const auto sm : {10u, 11u, 12u}) {
      const auto spelling = "sm_" + std::to_string(sm);
      checker::TargetInfo target{
          .ptx_version = {1, 4},
          .sm_version = sm,
          .identity = base::parse_target_identity(spelling),
          .map_f64_to_f32 = true,
      };
      const auto check = [&] {
        return (*resolved)->check(checker::Context{.target = target});
      };
      ASSERT_TRUE(check());
      target.map_f64_to_f32 = false;
      EXPECT_FALSE(check());
      target.map_f64_to_f32 = true;
      target.ptx_version = {1, 3};
      EXPECT_FALSE(check());
      target.ptx_version = {1, 4};
      target.identity.reset();
      EXPECT_FALSE(check());
      for (const auto wrong : {"sm_9", "sm_10a", "sm_10f", "sm_11a", "sm_11f",
                               "sm_12a", "sm_12f"}) {
        target.identity = base::parse_target_identity(wrong);
        EXPECT_FALSE(check()) << wrong;
      }
    }
  }
}

/** Unchanged cohorts retain their old gates even when the source option is set. */
TEST(LegacyMapF64, DoesNotEnableAdjacentFloatingForms) {
  for (const auto operation : {
           "add.f64 %d0, %d1, %d2;",
           "mul.f64 %d0, %d1, %d2;",
           "mad.rn.f64 %d0, %d1, %d2, %d0;",
           "min.f64 %d0, %d1, %d2;",
           "rsqrt.approx.f64 %d0, %d1;",
           "rcp.approx.ftz.f64 %d0, %d1;",
           "div.rn.f32 %f0, %f1, %f2;",
           "rcp.rn.f32 %f0, %f1;",
           "sqrt.rn.f32 %f0, %f1;",
           "div.rz.f64 %d0, %d1, %d2;",
           "div.rm.f64 %d0, %d1, %d2;",
           "div.rp.f64 %d0, %d1, %d2;",
           "rcp.rz.f64 %d0, %d1;",
           "rcp.rm.f64 %d0, %d1;",
           "rcp.rp.f64 %d0, %d1;",
           "sqrt.rz.f64 %d0, %d1;",
           "sqrt.rm.f64 %d0, %d1;",
           "sqrt.rp.f64 %d0, %d1;",
       }) {
    SCOPED_TRACE(operation);
    for (const auto target : {"sm_10", "sm_11", "sm_12"})
      expectModuleAvailability(
          mappedModule(operation, target, ", map_f64_to_f32", "9.3"), false);
    expectModuleAvailability(mappedModule(operation, "sm_80", {}, "9.3"), true);
  }
}

/** The option never relaxes syntax, rounding, operand count, type or width. */
TEST(LegacyMapF64, PreservesInvalidSyntaxAndOperandContracts) {
  for (const auto operation : {
           "div.f64 %d0, %d1, %d2;",
           "rcp.f64 %d0, %d1;",
           "sqrt.f64 %d0, %d1;",
           "div.rn.ftz.f64 %d0, %d1, %d2;",
           "rcp.rn.ftz.f64 %d0, %d1;",
           "sqrt.rn.ftz.f64 %d0, %d1;",
           "div.approx.f64 %d0, %d1, %d2;",
           "rcp.approx.f64 %d0, %d1;",
           "sqrt.approx.f64 %d0, %d1;",
           "div.rn.f64 %d0, %d1;",
           "rcp.rn.f64 %d0, %d1, %d2;",
           "sqrt.rn.f64 %d0;",
           "div.rn.f64 %f0, %d1, %d2;",
           "div.rn.f64 %d0, %f1, %d2;",
           "div.rn.f64 %d0, %d1, %f2;",
           "rcp.rn.f64 %f0, %d1;",
           "rcp.rn.f64 %d0, %f1;",
           "sqrt.rn.f64 %f0, %d1;",
           "sqrt.rn.f64 %d0, %f1;",
           "div.rn.f64 %d0, %u, %d2;",
           "rcp.rn.f64 %d0, %u;",
           "sqrt.rn.f64 %u, %d1;",
       }) {
    SCOPED_TRACE(operation);
    const auto source = mappedModule(operation, "sm_10", ", map_f64_to_f32");
    PtxSyntaxParser parser{source};
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}

/** Preserve explicit source identities and f64/64-bit operands after AST destruction. */
TEST(LegacyMapF64, OwnsConcreteFormsAndResistsHeaderForgery) {
  for (const auto target : {"sm_10", "sm_11", "sm_12"}) {
    std::optional<ResolvedModule> owned;
    {
      const auto source = mappedModule(
          "div.rn.f64 %rd0, %d1, 1.0; rcp.rn.f64 %d0, %rd1; "
          "sqrt.rn.f64 %rd2, 1.0;",
          target, ", texmode_unified, map_f64_to_f32", "1.5");
      PtxSyntaxParser parser{source};
      auto ast = parser.parseModule();
      ASSERT_TRUE(ast);
      auto resolved = resolveAndValidateModule(*ast);
      ASSERT_TRUE(resolved) << resolved.error().front().message;
      owned.emplace(std::move(*resolved));
      ASSERT_TRUE(validateModule(*ast, *owned));
    }
    ASSERT_TRUE(validateModule(*owned));
    const auto& function = owned->functions.front();
    ASSERT_EQ(function.body.size(), 3u);
    EXPECT_EQ(function.instruction_opcodes,
              (std::vector<std::string>{"div", "rcp", "sqrt"}));
    EXPECT_EQ(function.instruction_ranges.size(), 3u);
    const auto& div = dynamic_cast<const DivRnF64&>(*function.body[0]);
    const auto& rcp = dynamic_cast<const RcpRnF64&>(*function.body[1]);
    const auto& sqrt = dynamic_cast<const SqrtRnF64&>(*function.body[2]);
    EXPECT_EQ(DivRnF64::type, ScalarType::F64);
    EXPECT_EQ(RcpRnF64::type, ScalarType::F64);
    EXPECT_EQ(SqrtRnF64::type, ScalarType::F64);
    EXPECT_EQ(DivRnF64::rounding, RoundingMode::Rn);
    EXPECT_EQ(RcpRnF64::rounding, RoundingMode::Rn);
    EXPECT_EQ(SqrtRnF64::rounding, RoundingMode::Rn);
    EXPECT_EQ(div.dst.value.declared_type, ScalarType::B64);
    EXPECT_EQ(std::get<ResolvedRegisterRef>(div.src1.value).declared_type,
              ScalarType::F64);
    EXPECT_EQ(std::get<ResolvedImmediate>(div.src2.value).type,
              ScalarType::F64);
    EXPECT_EQ(rcp.dst.value.declared_type, ScalarType::F64);
    EXPECT_EQ(std::get<ResolvedRegisterRef>(rcp.src.value).declared_type,
              ScalarType::B64);
    EXPECT_EQ(sqrt.dst.value.declared_type, ScalarType::B64);
    EXPECT_EQ(std::get<ResolvedImmediate>(sqrt.src.value).type,
              ScalarType::F64);
    auto& region = owned->header.regions[1];
    EXPECT_EQ(region.target_options,
              (std::vector<std::string>{target, "texmode_unified",
                                        "map_f64_to_f32"}));
    const auto expect_mismatch = [&] {
      const auto result = validateModule(*owned);
      ASSERT_FALSE(result);
      EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& error) {
        return error.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
      }));
    };
    region.map_f64_to_f32 = false;
    expect_mismatch();
    region.map_f64_to_f32 = true;
    region.target_options.back() = "map_f64_to_f32x";
    expect_mismatch();
    region.target_options.pop_back();
    expect_mismatch();
    region.target_options.push_back("map_f64_to_f32");
    ASSERT_TRUE(validateModule(*owned));
  }
}

/** Concrete availability resets at every target region and follows the supplied AST. */
TEST(LegacyMapF64, ResetsRegionsAndUsesReplacementAst) {
  for (const auto operation : kMappedOperations) {
    const auto source = mappedModule(operation, "sm_10", ", map_f64_to_f32");
    PtxSyntaxParser original{source};
    const auto replacement_source = mappedModule(operation, "sm_10");
    PtxSyntaxParser replacement{replacement_source};
    auto ast = original.parseModule();
    auto replaced = replacement.parseModule();
    ASSERT_TRUE(ast);
    ASSERT_TRUE(replaced);
    auto owned = resolveAndValidateModule(*ast);
    ASSERT_TRUE(owned);
    EXPECT_TRUE(validateModule(*ast, *owned));
    EXPECT_FALSE(validateModule(*replaced, *owned));
    EXPECT_TRUE(validateModule(*owned));
    auto mixed = source +
                 "\n.target sm_11\n.func second() { .reg .f64 %d<3>; " +
                 std::string(operation) +
                 " }\n.target sm_12, map_f64_to_f32\n.func third() { "
                 ".reg .f64 %d<3>; " +
                 std::string(operation) + " }\n";
    PtxSyntaxParser parser{mixed};
    auto mixed_ast = parser.parseModule();
    ASSERT_TRUE(mixed_ast);
    auto resolved = resolveModuleOnly(*mixed_ast);
    ASSERT_TRUE(resolved);
    EXPECT_TRUE(resolved->header.regions[1].map_f64_to_f32);
    EXPECT_FALSE(resolved->header.regions[2].map_f64_to_f32);
    EXPECT_TRUE(resolved->header.regions[3].map_f64_to_f32);
    for (const auto& result :
         {validateModule(*mixed_ast, *resolved), validateModule(*resolved)}) {
      ASSERT_FALSE(result);
      ASSERT_EQ(result.error().size(), 1u);
      EXPECT_EQ(result.error().front().range,
                resolved->functions[1].instruction_ranges.front());
    }
    auto& region = resolved->header.regions[2];
    const auto expect_mismatch = [&] {
      const auto result = validateModule(*resolved);
      ASSERT_FALSE(result);
      EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& error) {
        return error.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
      }));
    };
    region.map_f64_to_f32 = true;
    expect_mismatch();
    region.map_f64_to_f32 = false;
    region.target_options.push_back("map_f64_to_f32");
    expect_mismatch();
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
