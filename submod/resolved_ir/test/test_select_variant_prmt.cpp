#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/data_movement/prmt/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/prmt/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/prmt/resolution.gen.hpp>
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

/** Generic and specialized `prmt` forms select their documented selector layouts. */
TEST(ResolvePrmt, SelectsGenericAndSpecializedVariants) {
  for (const auto source :
       {"prmt.b32 %r0, %r1, %r2, 0x5410;",
        "prmt.b32 %r0, 0x10000, -1, 0x12345410;",
        "prmt.b32.f4e %r0, 1, %r2, 0x10000;",
        "prmt.b32.b4e %r0, %r1, 2, 0xffff;", "prmt.b32.rc8 %r0, %r1, %r2, %r3;",
        "prmt.b32.ecl %r0, 1, 2, 4;", "prmt.b32.ecr %r0, %r1, %r2, 0xffff;",
        "prmt.b32.rc16 %r0, 1, %r2, 4;"})
    EXPECT_TRUE(resolve<Prmt>(parse_instruction(source)).has_value()) << source;
}

/** `prmt` accepts only its documented selector mode tokens. */
TEST(ResolvePrmt, RejectsUnknownSelectorMode) {
  EXPECT_FALSE(
      selectVariant<Prmt>(parse_instruction("prmt.b32.b4x %r0, %r1, %r2, %r3;"))
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir::checker {
namespace {

/** Every documented `prmt` selector mode shares the PTX 2.0 / SM 20 boundary. */
TEST(ResolvedIrChecker, ChecksGeneratedPrmtAvailability) {
  for (const auto source :
       {"prmt.b32 %r0, 0x10000, -1, 0x12345410;",
        "prmt.b32.f4e %r0, 1, %r2, 0x10000;",
        "prmt.b32.b4e %r0, %r1, 2, 0xffff;", "prmt.b32.rc8 %r0, %r1, %r2, %r3;",
        "prmt.b32.ecl %r0, 1, 2, 4;", "prmt.b32.ecr %r0, %r1, %r2, 0xffff;",
        "prmt.b32.rc16 %r0, 1, %r2, 4;"}) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value());
    const auto prmt = resolve<Prmt>(*ast);
    ASSERT_TRUE(prmt.has_value());
    EXPECT_TRUE(check(*prmt, Context{.target = {.ptx_version = {2, 0},
                                                .sm_version = 20}})
                    .has_value());
    EXPECT_FALSE(check(*prmt, Context{.target = {.ptx_version = {1, 9},
                                                 .sm_version = 20}})
                     .has_value());
    EXPECT_FALSE(check(*prmt, Context{.target = {.ptx_version = {2, 0},
                                                 .sm_version = 19}})
                     .has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir::checker
