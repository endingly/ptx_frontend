#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/model/data_movement/cvta.gen.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Verify an exact CVTA form and its static state-space/type contract. */
template <typename T>
  requires std::derived_from<T, Instruction>
bool matches(const Instruction& instruction, MemoryStateSpace state_space,
             ScalarType type) {
  return dynamic_cast<const T*>(&instruction) != nullptr &&
         T::state_space == state_space && T::type == type;
}

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
    std::string_view variant;
    MemoryStateSpace state_space;
    ScalarType type;
    bool (*matches)(const Instruction&, MemoryStateSpace, ScalarType);
  };
  constexpr std::array cases{
      Case{"cvta.global.u64 %rd0, %rd1;", "GlobalU64", MemoryStateSpace::Global,
           ScalarType::U64, &matches<CvtaGlobalU64>},
      Case{"cvta.to.global.u64 %rd0, %rd1;", "ToGlobalU64",
           MemoryStateSpace::Global, ScalarType::U64,
           &matches<CvtaToGlobalU64>},
      Case{"cvta.global.u32 %r0, %r1;", "GlobalU32", MemoryStateSpace::Global,
           ScalarType::U32, &matches<CvtaGlobalU32>},
      Case{"cvta.to.global.u32 %r0, %r1;", "ToGlobalU32",
           MemoryStateSpace::Global, ScalarType::U32,
           &matches<CvtaToGlobalU32>},
      Case{"cvta.local.u32 %r0, %r1;", "LocalU32", MemoryStateSpace::Local,
           ScalarType::U32, &matches<CvtaLocalU32>},
      Case{"cvta.to.local.u32 %r0, %r1;", "ToLocalU32", MemoryStateSpace::Local,
           ScalarType::U32, &matches<CvtaToLocalU32>},
      Case{"cvta.local.u64 %rd0, %rd1;", "LocalU64", MemoryStateSpace::Local,
           ScalarType::U64, &matches<CvtaLocalU64>},
      Case{"cvta.to.local.u64 %rd0, %rd1;", "ToLocalU64",
           MemoryStateSpace::Local, ScalarType::U64, &matches<CvtaToLocalU64>},
      Case{"cvta.shared.u32 %r0, %r1;", "SharedU32", MemoryStateSpace::Shared,
           ScalarType::U32, &matches<CvtaSharedU32>},
      Case{"cvta.to.shared.u32 %r0, %r1;", "ToSharedU32",
           MemoryStateSpace::Shared, ScalarType::U32,
           &matches<CvtaToSharedU32>},
      Case{"cvta.shared.u64 %rd0, %rd1;", "SharedU64", MemoryStateSpace::Shared,
           ScalarType::U64, &matches<CvtaSharedU64>},
      Case{"cvta.to.shared.u64 %rd0, %rd1;", "ToSharedU64",
           MemoryStateSpace::Shared, ScalarType::U64,
           &matches<CvtaToSharedU64>},
      Case{"cvta.const.u32 %r0, %r1;", "ConstU32", MemoryStateSpace::Constant,
           ScalarType::U32, &matches<CvtaConstU32>},
      Case{"cvta.to.const.u32 %r0, %r1;", "ToConstU32",
           MemoryStateSpace::Constant, ScalarType::U32,
           &matches<CvtaToConstU32>},
      Case{"cvta.const.u64 %rd0, %rd1;", "ConstU64", MemoryStateSpace::Constant,
           ScalarType::U64, &matches<CvtaConstU64>},
      Case{"cvta.to.const.u64 %rd0, %rd1;", "ToConstU64",
           MemoryStateSpace::Constant, ScalarType::U64,
           &matches<CvtaToConstU64>},
      Case{"cvta.param.u32 %r0, %r1;", "ParamU32", MemoryStateSpace::Parameter,
           ScalarType::U32, &matches<CvtaParamU32>},
      Case{"cvta.to.param.u32 %r0, %r1;", "ToParamU32",
           MemoryStateSpace::Parameter, ScalarType::U32,
           &matches<CvtaToParamU32>},
      Case{"cvta.param.u64 %rd0, %rd1;", "ParamU64",
           MemoryStateSpace::Parameter, ScalarType::U64,
           &matches<CvtaParamU64>},
      Case{"cvta.to.param.u64 %rd0, %rd1;", "ToParamU64",
           MemoryStateSpace::Parameter, ScalarType::U64,
           &matches<CvtaToParamU64>},
      Case{"cvta.shared::cta.u32 %r0, %r1;", "SharedCtaU32",
           MemoryStateSpace::Shared, ScalarType::U32,
           &matches<CvtaSharedCtaU32>},
      Case{"cvta.to.shared::cta.u32 %r0, %r1;", "ToSharedCtaU32",
           MemoryStateSpace::Shared, ScalarType::U32,
           &matches<CvtaToSharedCtaU32>},
      Case{"cvta.shared::cta.u64 %rd0, %rd1;", "SharedCtaU64",
           MemoryStateSpace::Shared, ScalarType::U64,
           &matches<CvtaSharedCtaU64>},
      Case{"cvta.to.shared::cta.u64 %rd0, %rd1;", "ToSharedCtaU64",
           MemoryStateSpace::Shared, ScalarType::U64,
           &matches<CvtaToSharedCtaU64>},
      Case{"cvta.shared::cluster.u32 %r0, %r1;", "SharedClusterU32",
           MemoryStateSpace::Shared, ScalarType::U32,
           &matches<CvtaSharedClusterU32>},
      Case{"cvta.to.shared::cluster.u32 %r0, %r1;", "ToSharedClusterU32",
           MemoryStateSpace::Shared, ScalarType::U32,
           &matches<CvtaToSharedClusterU32>},
      Case{"cvta.shared::cluster.u64 %rd0, %rd1;", "SharedClusterU64",
           MemoryStateSpace::Shared, ScalarType::U64,
           &matches<CvtaSharedClusterU64>},
      Case{"cvta.to.shared::cluster.u64 %rd0, %rd1;", "ToSharedClusterU64",
           MemoryStateSpace::Shared, ScalarType::U64,
           &matches<CvtaToSharedClusterU64>},
      Case{"cvta.param::entry.u32 %r0, %r1;", "ParamEntryU32",
           MemoryStateSpace::Parameter, ScalarType::U32,
           &matches<CvtaParamEntryU32>},
      Case{"cvta.to.param::entry.u32 %r0, %r1;", "ToParamEntryU32",
           MemoryStateSpace::Parameter, ScalarType::U32,
           &matches<CvtaToParamEntryU32>},
      Case{"cvta.param::entry.u64 %rd0, %rd1;", "ParamEntryU64",
           MemoryStateSpace::Parameter, ScalarType::U64,
           &matches<CvtaParamEntryU64>},
      Case{"cvta.to.param::entry.u64 %rd0, %rd1;", "ToParamEntryU64",
           MemoryStateSpace::Parameter, ScalarType::U64,
           &matches<CvtaToParamEntryU64>},
  };

  for (const auto& test : cases) {
    SCOPED_TRACE(test.source);
    const auto selected = select_variant_name(parse_instruction(test.source),
                                              cvta_syntax_descriptor());
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(*selected, test.variant);

    const auto resolved = resolveCvta(parse_instruction(test.source));
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    EXPECT_TRUE(test.matches(**resolved, test.state_space, test.type));
  }
}

TEST(ResolveCvta, RejectsMalformedModifierOrderingAndUnsupportedSpaces) {
  for (const auto source :
       {"cvta.global.to.u64 %rd0, %rd1;", "cvta.u64.global %rd0, %rd1;",
        "cvta.generic.u32 %r0, %r1;"}) {
    const auto selected = select_variant_name(parse_instruction(source),
                                              cvta_syntax_descriptor());
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
  const auto cvta = resolveCvta(*ast);
  ASSERT_TRUE(cvta.has_value()) << cvta.error().message;
  const auto old_ptx = (*cvta)->check(
      Context{.target = {.ptx_version = {1, 9}, .sm_version = 20},
              .instruction_range = ast->range});
  ASSERT_FALSE(old_ptx.has_value());
  EXPECT_EQ(old_ptx.error().front().kind,
            CheckDiagnosticKind::UnsupportedPtxVersion);
  const auto old_sm = (*cvta)->check(
      Context{.target = {.ptx_version = {2, 0}, .sm_version = 19},
              .instruction_range = ast->range});
  ASSERT_FALSE(old_sm.has_value());
  EXPECT_EQ(old_sm.error().front().kind,
            CheckDiagnosticKind::UnsupportedSmVersion);
  EXPECT_TRUE(
      (*cvta)
          ->check(Context{.target = {.ptx_version = {2, 0}, .sm_version = 20},
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
        const auto cvta = resolveCvta(*ast);
        ASSERT_TRUE(cvta.has_value()) << cvta.error().message;
        EXPECT_FALSE((*cvta)->check(rejected_by_ptx).has_value());
        EXPECT_FALSE((*cvta)->check(rejected_by_sm).has_value());
        EXPECT_TRUE((*cvta)->check(supported).has_value());
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
