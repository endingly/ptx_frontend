#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/data_movement/isspacep/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/isspacep/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/isspacep/resolution.gen.hpp>
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

/** All documented `isspacep` state-space spellings select their fixed variants. */
TEST(ResolveIsspacep, SelectsStateSpaceVariantsAndRejectsOtherForms) {
  const auto ast = parse_instruction("isspacep.global %p0, %rd0;");
  const auto resolved = resolve<Isspacep>(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* global = std::get_if<Isspacep::GlobalU64>(&resolved->variant);
  ASSERT_NE(global, nullptr);
  EXPECT_EQ(Isspacep::GlobalU64::state_space, MemoryStateSpace::Global);
  EXPECT_EQ(global->src.value.register_class, ResolvedRegisterClass::General);

  for (const auto source : {
           "isspacep.global %p0, %r0;",
           "isspacep.const %p0, %rd0;",
           "isspacep.local %p0, %rd0;",
           "isspacep.shared %p0, %rd0;",
           "isspacep.shared::cta %p0, %rd0;",
           "isspacep.shared::cluster %p0, %rd0;",
           "isspacep.param %p0, %rd0;",
           "isspacep.param::entry %p0, %rd0;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_TRUE(resolve<Isspacep>(parse_instruction(source)).has_value());
  }

  const auto shared_cta =
      resolve<Isspacep>(parse_instruction("isspacep.shared::cta %p0, %rd0;"));
  ASSERT_TRUE(shared_cta.has_value()) << shared_cta.error().message;
  EXPECT_NE(std::get_if<Isspacep::SharedCta>(&shared_cta->variant), nullptr);
  const auto parameter_entry =
      resolve<Isspacep>(parse_instruction("isspacep.param::entry %p0, %rd0;"));
  ASSERT_TRUE(parameter_entry.has_value()) << parameter_entry.error().message;
  EXPECT_NE(std::get_if<Isspacep::ParamEntry>(&parameter_entry->variant),
            nullptr);

  for (const auto source :
       {"isspacep %p0, %rd0;", "isspacep.param::func %p0, %rd0;",
        "isspacep.global %r0, %rd0;", "isspacep.global %p0, [%rd0];"}) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(resolve<Isspacep>(parse_instruction(source)).has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

/** Minimum target profile for one fixed `isspacep` state-space spelling. */
struct IsspacepAvailabilityCase {
  /** Standalone PTX instruction using the spelling under test. */
  std::string_view source;
  /** First PTX version that admits the form. */
  PtxVersion minimum_ptx;
  /** Immediately preceding rejected PTX version. */
  PtxVersion rejected_ptx;
  /** First SM version that admits the form. */
  uint16_t minimum_sm;
};

/** Generated `isspacep` variants enforce every documented PTX and SM boundary. */
TEST(ResolvedIrChecker, ChecksGeneratedIsspacepAvailabilityBoundaries) {
  constexpr std::array cases{
      IsspacepAvailabilityCase{"isspacep.global %p0, %r0;", {2, 0}, {1, 9}, 20},
      IsspacepAvailabilityCase{"isspacep.const %p0, %rd0;", {3, 1}, {3, 0}, 20},
      IsspacepAvailabilityCase{"isspacep.local %p0, %rd0;", {2, 0}, {1, 9}, 20},
      IsspacepAvailabilityCase{
          "isspacep.shared %p0, %rd0;", {2, 0}, {1, 9}, 20},
      IsspacepAvailabilityCase{
          "isspacep.shared::cta %p0, %rd0;", {7, 8}, {7, 7}, 30},
      IsspacepAvailabilityCase{
          "isspacep.shared::cluster %p0, %rd0;", {7, 8}, {7, 7}, 90},
      IsspacepAvailabilityCase{"isspacep.param %p0, %rd0;", {7, 7}, {7, 6}, 70},
      IsspacepAvailabilityCase{
          "isspacep.param::entry %p0, %rd0;", {8, 3}, {8, 2}, 70},
  };
  for (const IsspacepAvailabilityCase& test : cases) {
    SCOPED_TRACE(test.source);
    PtxSyntaxParser parser(test.source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    const auto isspacep = resolve<Isspacep>(*ast);
    ASSERT_TRUE(isspacep.has_value()) << isspacep.error().message;
    EXPECT_FALSE(
        check(*isspacep, Context{.target = {.ptx_version = test.rejected_ptx,
                                            .sm_version = test.minimum_sm},
                                 .instruction_range = ast->range})
            .has_value());
    EXPECT_FALSE(
        check(*isspacep, Context{.target = {.ptx_version = test.minimum_ptx,
                                            .sm_version = static_cast<uint16_t>(
                                                test.minimum_sm - 1)},
                                 .instruction_range = ast->range})
            .has_value());
    EXPECT_TRUE(
        check(*isspacep, Context{.target = {.ptx_version = test.minimum_ptx,
                                            .sm_version = test.minimum_sm},
                                 .instruction_range = ast->range})
            .has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
