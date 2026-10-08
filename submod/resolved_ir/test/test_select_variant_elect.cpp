#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/elect.gen.hpp>
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

TEST(SelectVariantElect, SelectsAndResolvesOptionalDataDestination) {
  for (const std::string_view source : {
           "elect.sync %lane|%p, 0xffffffff;",
           "elect.sync _|%p, 0xffffffff;",
       }) {
    SCOPED_TRACE(source);
    const auto selected = select_variant_name(parse_instruction(source),
                                              elect_syntax_descriptor());
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, "Sync");
  }
  for (const std::string_view source : {
           "elect %lane|%p, 0xffffffff;",
           "elect.sync.abs %lane|%p, 0xffffffff;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(select_variant_name(parse_instruction(source),
                                     elect_syntax_descriptor())
                     .has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
