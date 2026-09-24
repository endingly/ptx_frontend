#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/data_movement/cvta/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/cvta/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/cvta/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse one standalone instruction for a generated-opcode test. */
syntax_ast::AstInstruction parse_instruction(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseInstruction();
  EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  return std::move(*ast);
}

TEST(ResolveCvta, SelectsStateSpaceRegisterVariants) {
  /** One accepted spelling and its generated semantic variant contract. */
  struct Case {
    std::string_view source;
    Cvta::VariantType variant;
    MemoryStateSpace state_space;
    ScalarType type;
  };
  constexpr std::array cases{
      Case{"cvta.global.u64 %rd0, %rd1;", Cvta::VariantType::GlobalU64,
           MemoryStateSpace::Global, ScalarType::U64},
      Case{"cvta.to.global.u64 %rd0, %rd1;", Cvta::VariantType::ToGlobalU64,
           MemoryStateSpace::Global, ScalarType::U64},
      Case{"cvta.global.u32 %r0, %r1;", Cvta::VariantType::GlobalU32,
           MemoryStateSpace::Global, ScalarType::U32},
      Case{"cvta.to.global.u32 %r0, %r1;", Cvta::VariantType::ToGlobalU32,
           MemoryStateSpace::Global, ScalarType::U32},
      Case{"cvta.local.u32 %r0, %r1;", Cvta::VariantType::LocalU32,
           MemoryStateSpace::Local, ScalarType::U32},
      Case{"cvta.to.local.u32 %r0, %r1;", Cvta::VariantType::ToLocalU32,
           MemoryStateSpace::Local, ScalarType::U32},
      Case{"cvta.local.u64 %rd0, %rd1;", Cvta::VariantType::LocalU64,
           MemoryStateSpace::Local, ScalarType::U64},
      Case{"cvta.to.local.u64 %rd0, %rd1;", Cvta::VariantType::ToLocalU64,
           MemoryStateSpace::Local, ScalarType::U64},
      Case{"cvta.shared.u32 %r0, %r1;", Cvta::VariantType::SharedU32,
           MemoryStateSpace::Shared, ScalarType::U32},
      Case{"cvta.to.shared.u32 %r0, %r1;", Cvta::VariantType::ToSharedU32,
           MemoryStateSpace::Shared, ScalarType::U32},
      Case{"cvta.shared.u64 %rd0, %rd1;", Cvta::VariantType::SharedU64,
           MemoryStateSpace::Shared, ScalarType::U64},
      Case{"cvta.to.shared.u64 %rd0, %rd1;", Cvta::VariantType::ToSharedU64,
           MemoryStateSpace::Shared, ScalarType::U64},
      Case{"cvta.const.u32 %r0, %r1;", Cvta::VariantType::ConstU32,
           MemoryStateSpace::Constant, ScalarType::U32},
      Case{"cvta.to.const.u32 %r0, %r1;", Cvta::VariantType::ToConstU32,
           MemoryStateSpace::Constant, ScalarType::U32},
      Case{"cvta.const.u64 %rd0, %rd1;", Cvta::VariantType::ConstU64,
           MemoryStateSpace::Constant, ScalarType::U64},
      Case{"cvta.to.const.u64 %rd0, %rd1;", Cvta::VariantType::ToConstU64,
           MemoryStateSpace::Constant, ScalarType::U64},
      Case{"cvta.param.u32 %r0, %r1;", Cvta::VariantType::ParamU32,
           MemoryStateSpace::Parameter, ScalarType::U32},
      Case{"cvta.to.param.u32 %r0, %r1;", Cvta::VariantType::ToParamU32,
           MemoryStateSpace::Parameter, ScalarType::U32},
      Case{"cvta.param.u64 %rd0, %rd1;", Cvta::VariantType::ParamU64,
           MemoryStateSpace::Parameter, ScalarType::U64},
      Case{"cvta.to.param.u64 %rd0, %rd1;", Cvta::VariantType::ToParamU64,
           MemoryStateSpace::Parameter, ScalarType::U64},
      Case{"cvta.shared::cta.u32 %r0, %r1;", Cvta::VariantType::SharedCtaU32,
           MemoryStateSpace::Shared, ScalarType::U32},
      Case{"cvta.to.shared::cta.u32 %r0, %r1;",
           Cvta::VariantType::ToSharedCtaU32, MemoryStateSpace::Shared,
           ScalarType::U32},
      Case{"cvta.shared::cta.u64 %rd0, %rd1;", Cvta::VariantType::SharedCtaU64,
           MemoryStateSpace::Shared, ScalarType::U64},
      Case{"cvta.to.shared::cta.u64 %rd0, %rd1;",
           Cvta::VariantType::ToSharedCtaU64, MemoryStateSpace::Shared,
           ScalarType::U64},
      Case{"cvta.shared::cluster.u32 %r0, %r1;",
           Cvta::VariantType::SharedClusterU32, MemoryStateSpace::Shared,
           ScalarType::U32},
      Case{"cvta.to.shared::cluster.u32 %r0, %r1;",
           Cvta::VariantType::ToSharedClusterU32, MemoryStateSpace::Shared,
           ScalarType::U32},
      Case{"cvta.shared::cluster.u64 %rd0, %rd1;",
           Cvta::VariantType::SharedClusterU64, MemoryStateSpace::Shared,
           ScalarType::U64},
      Case{"cvta.to.shared::cluster.u64 %rd0, %rd1;",
           Cvta::VariantType::ToSharedClusterU64, MemoryStateSpace::Shared,
           ScalarType::U64},
      Case{"cvta.param::entry.u32 %r0, %r1;", Cvta::VariantType::ParamEntryU32,
           MemoryStateSpace::Parameter, ScalarType::U32},
      Case{"cvta.to.param::entry.u32 %r0, %r1;",
           Cvta::VariantType::ToParamEntryU32, MemoryStateSpace::Parameter,
           ScalarType::U32},
      Case{"cvta.param::entry.u64 %rd0, %rd1;",
           Cvta::VariantType::ParamEntryU64, MemoryStateSpace::Parameter,
           ScalarType::U64},
      Case{"cvta.to.param::entry.u64 %rd0, %rd1;",
           Cvta::VariantType::ToParamEntryU64, MemoryStateSpace::Parameter,
           ScalarType::U64},
  };

  for (const auto& test : cases) {
    SCOPED_TRACE(test.source);
    const auto selected = selectVariant<Cvta>(parse_instruction(test.source));
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(*selected, test.variant);

    const auto resolved = resolve<Cvta>(parse_instruction(test.source));
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    std::visit(
        [&](const auto& variant) {
          using Variant = std::remove_cvref_t<decltype(variant)>;
          EXPECT_EQ(Variant::state_space, test.state_space);
          EXPECT_EQ(Variant::type, test.type);
        },
        resolved->variant);
  }
}

TEST(ResolveCvta, RejectsMalformedModifierOrderingAndUnsupportedSpaces) {
  for (const auto source :
       {"cvta.global.to.u64 %rd0, %rd1;", "cvta.u64.global %rd0, %rd1;",
        "cvta.generic.u32 %r0, %r1;"}) {
    const auto selected = selectVariant<Cvta>(parse_instruction(source));
    SCOPED_TRACE(source);
    EXPECT_FALSE(selected.has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

/** Source range used by the standalone CVTA checker case. */
const SourceRange kInstructionRange{{4, 3}, {4, 17}};

TEST(ResolvedIrChecker, ChecksGeneratedCvtaGlobalU64Availability) {
  PtxSyntaxParser parser("cvta.to.global.u64 %rd0, %rd1;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  const auto cvta = resolve<Cvta>(*ast);
  ASSERT_TRUE(cvta.has_value()) << cvta.error().message;
  const auto old_ptx =
      check(*cvta, Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
                           .instruction_range = ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm =
      check(*cvta, Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
                           .instruction_range = ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(
      check(*cvta, Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
                           .instruction_range = ast->range})
          .has_value());
}

/** Validate generated `cvta` target minima across the new state spaces. */
TEST(ResolvedIrChecker, ChecksGeneratedCvtaAvailabilityBoundaries) {
  const auto expect_availability =
      [](std::string_view source, Context rejected_by_ptx,
         Context rejected_by_sm, Context supported) {
        PtxSyntaxParser parser(source);
        const auto ast = parser.parseInstruction();
        ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
        const auto cvta = resolve<Cvta>(*ast);
        ASSERT_TRUE(cvta.has_value()) << cvta.error().message;
        EXPECT_FALSE(check(*cvta, rejected_by_ptx).has_value());
        EXPECT_FALSE(check(*cvta, rejected_by_sm).has_value());
        EXPECT_TRUE(check(*cvta, supported).has_value());
      };

  expect_availability(
      "cvta.local.u32 %r0, %r1;",
      Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
              .instruction_range = kInstructionRange},
      Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
              .instruction_range = kInstructionRange},
      Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
              .instruction_range = kInstructionRange});
  expect_availability(
      "cvta.to.shared.u64 %rd0, %rd1;",
      Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
              .instruction_range = kInstructionRange},
      Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
              .instruction_range = kInstructionRange},
      Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
              .instruction_range = kInstructionRange});
  expect_availability(
      "cvta.const.u32 %r0, %r1;",
      Context{.target = {.ptx_version = {3, 0}, .sm_version = 20},
              .instruction_range = kInstructionRange},
      Context{.target = {.ptx_version = {3, 1}, .sm_version = 19},
              .instruction_range = kInstructionRange},
      Context{.target = {.ptx_version = {3, 1}, .sm_version = 20},
              .instruction_range = kInstructionRange});
  expect_availability(
      "cvta.to.const.u64 %rd0, %rd1;",
      Context{.target = {.ptx_version = {3, 0}, .sm_version = 20},
              .instruction_range = kInstructionRange},
      Context{.target = {.ptx_version = {3, 1}, .sm_version = 19},
              .instruction_range = kInstructionRange},
      Context{.target = {.ptx_version = {3, 1}, .sm_version = 20},
              .instruction_range = kInstructionRange});
  expect_availability(
      "cvta.param.u32 %r0, %r1;",
      Context{.target = {.ptx_version = {7, 6}, .sm_version = 70},
              .instruction_range = kInstructionRange},
      Context{.target = {.ptx_version = {7, 7}, .sm_version = 69},
              .instruction_range = kInstructionRange},
      Context{.target = {.ptx_version = {7, 7}, .sm_version = 70},
              .instruction_range = kInstructionRange});
  expect_availability(
      "cvta.to.param.u64 %rd0, %rd1;",
      Context{.target = {.ptx_version = {7, 6}, .sm_version = 70},
              .instruction_range = kInstructionRange},
      Context{.target = {.ptx_version = {7, 7}, .sm_version = 69},
              .instruction_range = kInstructionRange},
      Context{.target = {.ptx_version = {7, 7}, .sm_version = 70},
              .instruction_range = kInstructionRange});
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
