#include <gtest/gtest.h>

#include <limits>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_storage_declarations.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse one module and retain parser failures in the test output. */
syntax_ast::AstModule parseModule(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto module = parser.parseModule();
  EXPECT_TRUE(module.has_value());
  EXPECT_TRUE(module.diagnostics.empty());
  return std::move(*module);
}

/** Return the immediate source held by a scalar move instruction. */
const ResolvedImmediate& scalarMovImmediate(const ResolvedInstruction& instruction) {
  const auto& mov = std::get<Mov>(instruction);
  const auto& scalar = std::get<Mov::Scalar>(mov.variant);
  const auto& operands = std::get<Mov::Scalar::ScalarOperands>(scalar.operands);
  return std::get<ResolvedImmediate>(operands.src.value);
}

/** Resolve WARP_SZ through parser, module binding, and typed instruction uses. */
TEST(WarpSizeLiteral, ResolvesSourceConstantInInstructionAndDeclarationUses) {
  const auto module = parseModule(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.global .u32 warp_width = WARP_SZ;
.entry kernel() {
  .reg .u32 %r;
  .reg .s32 %s;
  mov.u32 %r, +WARP_SZ;
  add.u32 %r, %r, WARP_SZ;
  mov.s32 %s, -WARP_SZ;
  ret;
}
)ptx");

  const auto resolved = resolveModule(module);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->storage_declarations.size(), 1u);
  const auto& initializer = resolved->storage_declarations.front().initializer;
  ASSERT_EQ(initializer.size(), 1u);
  EXPECT_EQ(std::get<StorageConstant>(initializer.front().value).bits, 32u);

  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 4u);
  const auto& mov = scalarMovImmediate(body[0]);
  EXPECT_EQ(mov.type, ScalarType::U32);
  EXPECT_EQ(mov.bits, 32u);
  EXPECT_EQ(mov.integer_source_bits, 32u);
  EXPECT_FALSE(mov.is_negative);

  const auto& add = std::get<Add::IntegerNoSat>(std::get<Add>(body[1]).variant);
  const auto& add_immediate = std::get<ResolvedImmediate>(add.src2.value);
  EXPECT_EQ(add_immediate.type, ScalarType::U32);
  EXPECT_EQ(add_immediate.bits, 32u);
  EXPECT_EQ(add_immediate.integer_source_bits, 32u);

  const auto& negative = scalarMovImmediate(body[2]);
  EXPECT_EQ(negative.type, ScalarType::S32);
  EXPECT_EQ(negative.bits, 0xffffffe0U);
  EXPECT_EQ(negative.integer_source_bits,
            std::numeric_limits<uint64_t>::max() - 31u);
  EXPECT_TRUE(negative.is_negative);
}

/** Resolve a directly constructed WARP_SZ AST immediate at signed and unsigned widths. */
TEST(WarpSizeLiteral, PublicLiteralEntryPointUsesSignedSourceConstant) {
  const syntax_ast::AstImmediate immediate{
      .syntax = {.text = "WARP_SZ", .range = {}},
      .kind = syntax_ast::AstImmediateKind::WarpSize,
  };

  const auto signed_value = resolve_immediate_literal(immediate, ScalarType::S16);
  ASSERT_TRUE(signed_value.has_value()) << signed_value.error().message;
  EXPECT_EQ(signed_value->bits, 32u);
  EXPECT_EQ(signed_value->integer_source_bits, 32u);
  EXPECT_FALSE(signed_value->is_negative);

  const auto unsigned_value =
      resolve_immediate_literal(immediate, ScalarType::U8);
  ASSERT_TRUE(unsigned_value.has_value()) << unsigned_value.error().message;
  EXPECT_EQ(unsigned_value->bits, 32u);
  EXPECT_EQ(unsigned_value->integer_source_bits, 32u);

  const syntax_ast::AstImmediate negative_immediate{
      .syntax = {.text = "-WARP_SZ", .range = {}},
      .kind = syntax_ast::AstImmediateKind::WarpSize,
  };
  const auto negative_value =
      resolve_immediate_literal(negative_immediate, ScalarType::S16);
  ASSERT_TRUE(negative_value.has_value()) << negative_value.error().message;
  EXPECT_EQ(negative_value->bits, 0xffe0u);
  EXPECT_EQ(negative_value->integer_source_bits,
            std::numeric_limits<uint64_t>::max() - 31u);
  EXPECT_TRUE(negative_value->is_negative);

  const auto incompatible =
      resolve_immediate_literal(immediate, ScalarType::F32);
  ASSERT_FALSE(incompatible.has_value());
  EXPECT_EQ(incompatible.error().message,
            "Integer literal 'WARP_SZ' is incompatible with scalar type 'F32'.");
}

/** Keep instruction-specific immediate restrictions after WARP_SZ materialization. */
TEST(WarpSizeLiteral, PreservesInstructionImmediateLegality) {
  const auto module = parseModule(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.entry kernel() {
  bar.sync WARP_SZ;
  ret;
}
)ptx");

  const auto resolved = resolveModule(module);

  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().checker_kind,
            checker::CheckDiagnosticKind::ImmediateValueMismatch);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
