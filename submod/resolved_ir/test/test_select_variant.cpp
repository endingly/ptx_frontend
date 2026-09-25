#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

syntax_ast::AstInstruction parse_instruction(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseInstruction();
  EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  return std::move(*ast);
}

syntax_ast::AstInstruction indirect_metadata_instruction(std::string spelling) {
  const SourceRange range{{1, 1}, {1, 1}};
  syntax_ast::AstInstruction ast{
      .opcode = syntax_ast::AstOpcode{.syntax = {"call", range}},
      .range = range,
  };
  ast.operands.emplace_back(syntax_ast::AstCallTargetSet{
      .name =
          syntax_ast::AstIdentifierRef{.syntax = {std::move(spelling), range}},
      .range = range,
  });
  return ast;
}

TEST(ResolveCvta, ResolvesForwardSymbolAndOffsetSources) {
  PtxSyntaxParser parser(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.global .u32 value;
.entry kernel() {
  .reg .u64 %rd<2>;
  cvta.global.u64 %rd0, value;
  cvta.global.u64 %rd1, value+4;
}
)ptx");
  const auto parsed = parser.parseModule();
  ASSERT_TRUE(parsed.has_value()) << parsed.diagnostics.front().message;
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 2u);

  const Cvta& direct = std::get<Cvta>(body[0]);
  const auto& direct_variant = std::get<Cvta::GlobalU64>(direct.variant);
  EXPECT_TRUE(
      std::holds_alternative<ResolvedSymbolRef>(direct_variant.src.value));

  const Cvta& offset = std::get<Cvta>(body[1]);
  const auto& offset_variant = std::get<Cvta::GlobalU64>(offset.variant);
  const auto* address = std::get_if<ResolvedAddress>(&offset_variant.src.value);
  ASSERT_NE(address, nullptr);
  EXPECT_TRUE(std::holds_alternative<ResolvedSymbolRef>(address->base));
}

TEST(ResolveLogicAndShift, SelectsExpandedWidths) {
  for (const auto source : {
           "and.pred %p0, !%p1, 1;",
           "or.b16 %h0, %h1, 1;",
           "xor.b64 %rd0, %rd1, 1;",
           "not.b16 %h0, %h1;",
           "cnot.b64 %rd0, 0;",
           "shl.b64 %rd0, %rd1, 64;",
           "shr.b16 %h0, %h1, 16;",
           "shr.u64 %rd0, %rd1, 1;",
           "shr.s32 %r0, %r1, 1;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_TRUE(resolveInstruction(parse_instruction(source)).has_value());
  }
}

/** All `cvt.pack` layouts select their topology-specific generated variants. */
TEST(ResolveCvt, SelectsPackedSatVariantsAndRejectsInvalidTopologies) {
  const auto ast =
      parse_instruction("cvt.pack.sat.u8.s32.b32 %r0, %r1, %r2, %r3;");
  const auto resolved = resolve<Cvt>(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  ASSERT_NE(std::get_if<Cvt::PackSatU8S32B32>(&resolved->variant), nullptr);
  EXPECT_TRUE(Cvt::PackSatU8S32B32::pack);
  EXPECT_TRUE(Cvt::PackSatU8S32B32::saturate);
  EXPECT_EQ(Cvt::PackSatU8S32B32::dst_type, ScalarType::U8);
  EXPECT_EQ(Cvt::PackSatU8S32B32::src_type, ScalarType::S32);
  EXPECT_EQ(Cvt::PackSatU8S32B32::carry_type, ScalarType::B32);

  const auto packed_16 =
      resolve<Cvt>(parse_instruction("cvt.pack.sat.s16.s32 %r0, %r1, %r2;"));
  ASSERT_TRUE(packed_16.has_value()) << packed_16.error().message;
  const auto* s16 = std::get_if<Cvt::PackSat16S32>(&packed_16->variant);
  ASSERT_NE(s16, nullptr);
  EXPECT_EQ(s16->dst_type.value, ScalarType::S16);

  const auto packed_small = resolve<Cvt>(
      parse_instruction("cvt.pack.sat.u4.s32.b32 %r0, %r1, %r2, 0;"));
  ASSERT_TRUE(packed_small.has_value()) << packed_small.error().message;
  const auto* u4 = std::get_if<Cvt::PackSatSmallS32B32>(&packed_small->variant);
  ASSERT_NE(u4, nullptr);
  EXPECT_EQ(u4->dst_type.value, ScalarType::U4);

  const auto dispatched = resolveInstruction(ast);
  ASSERT_TRUE(dispatched.has_value()) << dispatched.error().message;
  EXPECT_TRUE(std::holds_alternative<Cvt>(*dispatched));

  for (const auto source : {"cvt.sat.u8.s32.b32 %r0, %r1, %r2, %r3;",
                            "cvt.pack.u8.s32.b32 %r0, %r1, %r2, %r3;",
                            "cvt.pack.sat.u16.s32.b32 %r0, %r1, %r2, %r3;",
                            "cvt.pack.sat.u4.s32.b32 %r0, %r1, %r2;"}) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(resolve<Cvt>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(
      resolve<Cvt>(parse_instruction("cvt.pack.sat.u8.s32.b32 %r0, %r1, %r2;"))
          .has_value());
}

TEST(ResolveLd, SelectsM12GlobalNcL1NoAllocateAndRejectsUnfrozenForms) {
  const auto ast =
      parse_instruction("ld.global.nc.L1::no_allocate.u32 %r0, [%rd0];");
  const auto resolved = resolve<Ld>(ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  const auto* no_allocate =
      std::get_if<Ld::GlobalNcL1NoAllocateU32>(&resolved->variant);
  ASSERT_NE(no_allocate, nullptr);
  EXPECT_EQ(Ld::GlobalNcL1NoAllocateU32::state_space, MemoryStateSpace::Global);
  EXPECT_TRUE(Ld::GlobalNcL1NoAllocateU32::nc);
  EXPECT_EQ(no_allocate->eviction_priority.value, EvictionPriority::NoAllocate);
  EXPECT_EQ(no_allocate->type.value, ScalarType::U32);

  const auto dispatched = resolveInstruction(ast);
  ASSERT_TRUE(dispatched.has_value()) << dispatched.error().message;
  EXPECT_TRUE(std::holds_alternative<Ld>(*dispatched));

  for (const auto source : {
           "ld.global.L1::no_allocate.u32 %r0, [%rd0];",
           "ld.global.nc.L1::evict_first.u32 %r0, [%rd0];",
           "ld.global.nc.L1::no_allocate.b32 %r0, [%rd0];",
       }) {
    SCOPED_TRACE(source);
    EXPECT_TRUE(selectVariant<Ld>(parse_instruction(source)).has_value());
  }
  for (const auto source : {
           "ld.global.nc.L2::evict_first.u32 %r0, [%rd0];",
           "ld.global.ca.nc.L1::no_allocate.u32 %r0, [%rd0];",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Ld>(parse_instruction(source)).has_value());
  }
}

TEST(ResolveInstruction, DispatchesByOpcodeIntoGeneratedVariant) {
  const ResolvedModule empty_module{};
  EXPECT_TRUE(empty_module.functions.empty());

  const auto add_ast = parse_instruction("add.u32 %r0, %r1, %r2;");
  const auto add = resolveInstruction(add_ast);
  ASSERT_TRUE(add.has_value()) << add.error().message;
  EXPECT_TRUE(std::holds_alternative<Add>(*add));

  const auto sub_ast = parse_instruction("sub.u32 %r0, %r1, %r2;");
  const auto sub = resolveInstruction(sub_ast);
  ASSERT_TRUE(sub.has_value()) << sub.error().message;
  EXPECT_TRUE(std::holds_alternative<Sub>(*sub));

  const auto ret_ast = parse_instruction("ret;");
  const auto ret = resolveInstruction(ret_ast);
  ASSERT_TRUE(ret.has_value()) << ret.error().message;
  EXPECT_TRUE(std::holds_alternative<Ret>(*ret));

  const auto exit_ast = parse_instruction("exit;");
  const auto exit_instruction = resolveInstruction(exit_ast);
  ASSERT_TRUE(exit_instruction.has_value()) << exit_instruction.error().message;
  EXPECT_TRUE(std::holds_alternative<Exit>(*exit_instruction));

  const auto trap_ast = parse_instruction("trap;");
  const auto trap = resolveInstruction(trap_ast);
  ASSERT_TRUE(trap.has_value()) << trap.error().message;
  EXPECT_TRUE(std::holds_alternative<Trap>(*trap));

  const auto and_ast = parse_instruction("and.b32 %r0, %r1, %r2;");
  const auto and_instruction = resolveInstruction(and_ast);
  ASSERT_TRUE(and_instruction.has_value()) << and_instruction.error().message;
  EXPECT_TRUE(std::holds_alternative<And>(*and_instruction));

  const auto or_ast = parse_instruction("or.b32 %r0, %r1, %r2;");
  const auto or_instruction = resolveInstruction(or_ast);
  ASSERT_TRUE(or_instruction.has_value()) << or_instruction.error().message;
  EXPECT_TRUE(std::holds_alternative<Or>(*or_instruction));

  const auto xor_ast = parse_instruction("xor.b32 %r0, %r1, %r2;");
  const auto xor_instruction = resolveInstruction(xor_ast);
  ASSERT_TRUE(xor_instruction.has_value()) << xor_instruction.error().message;
  EXPECT_TRUE(std::holds_alternative<Xor>(*xor_instruction));

  const auto not_ast = parse_instruction("not.b32 %r0, %r1;");
  const auto not_instruction = resolveInstruction(not_ast);
  ASSERT_TRUE(not_instruction.has_value()) << not_instruction.error().message;
  EXPECT_TRUE(std::holds_alternative<Not>(*not_instruction));

  const auto shl_ast = parse_instruction("shl.b32 %r0, %r1, %r2;");
  const auto shl_instruction = resolveInstruction(shl_ast);
  ASSERT_TRUE(shl_instruction.has_value()) << shl_instruction.error().message;
  EXPECT_TRUE(std::holds_alternative<Shl>(*shl_instruction));

  const auto shr_ast = parse_instruction("shr.u32 %r0, %r1, %r2;");
  const auto shr_instruction = resolveInstruction(shr_ast);
  ASSERT_TRUE(shr_instruction.has_value()) << shr_instruction.error().message;
  EXPECT_TRUE(std::holds_alternative<Shr>(*shr_instruction));

  const auto setp_ast = parse_instruction("setp.lt.u32 %p0, %r0, %r1;");
  const auto setp_instruction = resolveInstruction(setp_ast);
  ASSERT_TRUE(setp_instruction.has_value()) << setp_instruction.error().message;
  EXPECT_TRUE(std::holds_alternative<Setp>(*setp_instruction));

  const auto selp_ast = parse_instruction("selp.u32 %r0, %r1, %r2, %p0;");
  const auto selp_instruction = resolveInstruction(selp_ast);
  ASSERT_TRUE(selp_instruction.has_value()) << selp_instruction.error().message;
  EXPECT_TRUE(std::holds_alternative<Selp>(*selp_instruction));

  const auto cvt_ast = parse_instruction("cvt.s32.u32 %s0, %r0;");
  const auto cvt_instruction = resolveInstruction(cvt_ast);
  ASSERT_TRUE(cvt_instruction.has_value()) << cvt_instruction.error().message;
  EXPECT_TRUE(std::holds_alternative<Cvt>(*cvt_instruction));
}

TEST(ResolveInstruction, RejectsUnknownOpcode) {
  const auto ast = parse_instruction("unknown.u32 %r0, %r1, %r2;");

  const auto resolved = resolveInstruction(ast);

  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(resolved.error().range, ast.opcode.syntax.range);
  EXPECT_EQ(resolved.error().message, "Unknown PTX opcode 'unknown'.");
}

TEST(ResolveInstruction, RejectsMalformedMetadataCallWithGenericLayoutError) {
  const auto resolved =
      resolveInstruction(indirect_metadata_instruction("metadata"));

  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(resolved.error().message,
            "Operands do not match any layout of instruction variant "
            "'Direct'.");
}

/**
 * @brief Resolves leading-zero integer spellings as octal without changing
 * their AST kind.
 */

/** @brief Retains evaluated 64-bit values while rejecting decoder overflow. */

/** @brief Normalizes integer minus zero without changing unsigned wraparound. */

/**
 * @brief Rejects malformed and overflowing octal text supplied directly to
 * immediate resolution.
 */

/**
 * @brief Keeps decimal, hexadecimal, and floating leading-zero literal forms
 * distinct from octal.
 */

/** @brief Keeps floating negative zero independent of integer normalization. */

}  // namespace
}  // namespace ptx_frontend::resolved_ir
