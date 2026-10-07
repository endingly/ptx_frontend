#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/model/arithmetic/add.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/and.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/not.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/or.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/sub.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/xor.gen.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse one standalone instruction for resolver support tests. */
syntax_ast::AstInstruction parse_instruction(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseInstruction();
  EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  return std::move(*ast);
}

/**
 * Fixed b32 operand type provenance must not impose a source-range conversion.
 * These generated data uses preserve the full decoded source for later
 * diagnostics while retaining their low 32 bits as the operand value.
 */
TEST(ResolveLogic, NarrowsFixedB32ImmediateDataOperands) {
  const auto and_boundary =
      resolveAnd(parse_instruction("and.b32 %r0, %r1, 4294967295;"));
  ASSERT_TRUE(and_boundary.has_value()) << and_boundary.error().message;
  const auto* and_b32 = dynamic_cast<AndB32*>(and_boundary->get());
  ASSERT_NE(and_b32, nullptr);
  const auto* and_immediate =
      std::get_if<ResolvedImmediate>(&and_b32->src2.value);
  ASSERT_NE(and_immediate, nullptr);
  EXPECT_EQ(and_immediate->bits, 0xffffffffU);
  EXPECT_EQ(and_immediate->integer_source_bits, 0xffffffffU);

  const auto and_resolved =
      resolveAnd(parse_instruction("and.b32 %r0, %r1, 4294967296;"));
  ASSERT_TRUE(and_resolved.has_value()) << and_resolved.error().message;
  and_b32 = dynamic_cast<AndB32*>(and_resolved->get());
  ASSERT_NE(and_b32, nullptr);
  and_immediate = std::get_if<ResolvedImmediate>(&and_b32->src2.value);
  ASSERT_NE(and_immediate, nullptr);
  EXPECT_EQ(and_immediate->bits, 0U);
  EXPECT_EQ(and_immediate->integer_source_bits, 0x100000000ULL);

  const auto or_resolved =
      resolveOr(parse_instruction("or.b32 %r0, %r1, 0x100000000;"));
  ASSERT_TRUE(or_resolved.has_value()) << or_resolved.error().message;
  const auto* or_b32 = dynamic_cast<OrB32*>(or_resolved->get());
  ASSERT_NE(or_b32, nullptr);
  const auto* or_immediate =
      std::get_if<ResolvedImmediate>(&or_b32->src2.value);
  ASSERT_NE(or_immediate, nullptr);
  EXPECT_EQ(or_immediate->bits, 0U);
  EXPECT_EQ(or_immediate->integer_source_bits, 0x100000000ULL);

  const auto xor_resolved =
      resolveXor(parse_instruction("xor.b32 %r0, %r1, 4294967296;"));
  ASSERT_TRUE(xor_resolved.has_value()) << xor_resolved.error().message;
  const auto* xor_b32 = dynamic_cast<XorB32*>(xor_resolved->get());
  ASSERT_NE(xor_b32, nullptr);
  const auto* xor_immediate =
      std::get_if<ResolvedImmediate>(&xor_b32->src2.value);
  ASSERT_NE(xor_immediate, nullptr);
  EXPECT_EQ(xor_immediate->bits, 0U);
  EXPECT_EQ(xor_immediate->integer_source_bits, 0x100000000ULL);

  const auto not_resolved =
      resolveNot(parse_instruction("not.b32 %r0, 0xffffffffffffffff;"));
  ASSERT_TRUE(not_resolved.has_value()) << not_resolved.error().message;
  const auto* not_b32 = dynamic_cast<NotB32*>(not_resolved->get());
  ASSERT_NE(not_b32, nullptr);
  const auto* not_immediate =
      std::get_if<ResolvedImmediate>(&not_b32->src.value);
  ASSERT_NE(not_immediate, nullptr);
  EXPECT_EQ(not_immediate->bits, 0xffffffffU);
  EXPECT_EQ(not_immediate->integer_source_bits,
            std::numeric_limits<uint64_t>::max());
}

TEST(SelectVariantMixedPrecision, RejectsUnsupportedReorderingAndDuplicates) {
  for (const std::string_view opcode : {"add", "sub"}) {
    for (const std::string_view suffix : {
             ".rz.f32.sat.bf16",
             ".rz.sat.bf16.f32",
             ".sat.sat.f32.bf16",
             ".rn.rz.f32.bf16",
             ".sat.f32.f16.sat",
         }) {
      const std::string source =
          std::string(opcode) + std::string(suffix) + " %f0, %h1, %f2;";
      SCOPED_TRACE(source);
      const auto ast = parse_instruction(source);
      if (opcode == "add")
        EXPECT_FALSE(
            select_variant_name(ast, add_syntax_descriptor()).has_value());
      else
        EXPECT_FALSE(
            select_variant_name(ast, sub_syntax_descriptor()).has_value());
    }
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
