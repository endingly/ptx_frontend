#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/match.gen.hpp>
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

TEST(SelectVariantMatch, SelectsMatchSyncFormsAndRejectsInvalidOnes) {
  const auto expect_variant = [](std::string_view source,
                                 std::string_view expected) {
    const auto selected = select_variant_name(parse_instruction(source),
                                              match_syntax_descriptor());
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("match.any.sync.b32 %b0, %b1, 0xffffffff;", "AnySync");
  expect_variant("match.any.sync.b64 %b0, %d0, %r0;", "AnySync");
  expect_variant("match.all.sync.b32 %b0, %b1, 0xffffffff;", "AllSync");
  expect_variant("match.all.sync.b64 %b0|%p0, %d0, %r0;", "AllSync");

  for (const std::string_view source : {
           "match.any.b32 %b0, %b1, 0xffffffff;",
           "match.sync.any.b32 %b0, %b1, 0xffffffff;",
           "match.all.sync.u32 %b0, %b1, 0xffffffff;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(select_variant_name(parse_instruction(source),
                                     match_syntax_descriptor())
                     .has_value());
  }
  EXPECT_FALSE(
      resolveMatch(parse_instruction("match.any.sync.b32 _, %b1, 0xffffffff;"))
          .has_value());
  EXPECT_FALSE(resolveMatch(parse_instruction(
                                "match.any.sync.b32 _|%p0, %b1, 0xffffffff;"))
                   .has_value());
  EXPECT_TRUE(
      resolveMatch(parse_instruction("match.all.sync.b32 _, %b1, 0xffffffff;"))
          .has_value());
  EXPECT_TRUE(resolveMatch(parse_instruction(
                               "match.all.sync.b32 _|%p0, %b1, 0xffffffff;"))
                  .has_value());
  EXPECT_TRUE(resolveMatch(parse_instruction(
                               "match.all.sync.b32 %b0|_, %b1, 0xffffffff;"))
                  .has_value());
  EXPECT_FALSE(resolveMatch(parse_instruction(
                                "match.all.sync.b32 _|_, %b1, 0xffffffff;"))
                   .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
