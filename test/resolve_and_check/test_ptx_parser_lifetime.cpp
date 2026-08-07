#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "ptx_parser.hpp"

namespace ptx_frontend {
namespace {

const Ident& registerName(const WithLoc<ParsedOp>& operand) {
  const auto& reg_or_immediate =
      std::get<RegOrImmediate<Ident>>(operand.value.value);
  return std::get<Ident>(reg_or_immediate.value);
}

TEST(PtxParserLifetime, ParsedInstructionOwnsIdentifierText) {
  std::optional<ParsedInstruction> parsed;

  {
    std::string source = "add.u32 %r1, %r2, %r3;";
    PtxParser parser(source);

    auto result = parser.parseInstruction();
    ASSERT_TRUE(result.has_value()) << result.error().message;
    parsed.emplace(std::move(*result));

    source.assign(source.size(), 'x');
  }

  const auto& add =
      std::get<generated::InstrIntegerAdd<ParsedOp>>(*parsed);

  EXPECT_EQ(registerName(add.dst), "%r1");
  EXPECT_EQ(registerName(add.src1), "%r2");
  EXPECT_EQ(registerName(add.src2), "%r3");
}

}  // namespace
}  // namespace ptx_frontend
