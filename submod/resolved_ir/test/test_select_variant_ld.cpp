#include <gtest/gtest.h>

#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/model/data_movement/ld.gen.hpp>
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

TEST(ResolveLoadStore, ChecksMemoryConsistencyCrossRules) {
  const checker::Context context{
      .target = {.ptx_version = {9, 2}, .sm_version = 90},
      .instruction_range = SourceRange{},
  };
  const auto missing_scope =
      resolve<Ld>(parse_instruction("ld.relaxed.u32 %r0, [%rd0];"));
  ASSERT_TRUE(missing_scope.has_value()) << missing_scope.error().message;
  const auto missing_scope_check = checker::check(*missing_scope, context);
  ASSERT_FALSE(missing_scope_check.has_value());
  EXPECT_EQ(missing_scope_check.error().back().kind,
            checker::CheckDiagnosticKind::MemoryConsistencyViolation);

  const auto conflicting_cache =
      resolve<Ld>(parse_instruction("ld.volatile.ca.u32 %r0, [%rd0];"));
  ASSERT_TRUE(conflicting_cache.has_value())
      << conflicting_cache.error().message;
  const auto cache_check = checker::check(*conflicting_cache, context);
  ASSERT_FALSE(cache_check.has_value());
  EXPECT_EQ(cache_check.error().back().kind,
            checker::CheckDiagnosticKind::MemoryConsistencyViolation);

  const auto relaxed_local =
      resolve<Ld>(parse_instruction("ld.local.relaxed.cta.u32 %r0, [%rd0];"));
  ASSERT_TRUE(relaxed_local.has_value()) << relaxed_local.error().message;
  const auto relaxed_local_check = checker::check(*relaxed_local, context);
  ASSERT_FALSE(relaxed_local_check.has_value());
  EXPECT_EQ(relaxed_local_check.error().back().kind,
            checker::CheckDiagnosticKind::MemoryConsistencyViolation);

  const auto canonical_relaxed_local =
      resolve<Ld>(parse_instruction("ld.relaxed.cta.local.u32 %r0, [%rd0];"));
  ASSERT_TRUE(canonical_relaxed_local.has_value())
      << canonical_relaxed_local.error().message;
  const auto canonical_relaxed_local_check =
      checker::check(*canonical_relaxed_local, context);
  ASSERT_FALSE(canonical_relaxed_local_check.has_value());
  EXPECT_EQ(canonical_relaxed_local_check.error().back().kind,
            checker::CheckDiagnosticKind::MemoryConsistencyViolation);

  const auto canonical_cache = resolve<Ld>(
      parse_instruction("ld.relaxed.cta.global.ca.u32 %r0, [%rd0];"));
  ASSERT_TRUE(canonical_cache.has_value()) << canonical_cache.error().message;
  const auto canonical_cache_check = checker::check(*canonical_cache, context);
  ASSERT_FALSE(canonical_cache_check.has_value());
  EXPECT_EQ(canonical_cache_check.error().back().kind,
            checker::CheckDiagnosticKind::MemoryConsistencyViolation);

  const auto canonical_mmio = resolve<Ld>(
      parse_instruction("ld.mmio.relaxed.cta.global.u32 %r0, [%rd0];"));
  ASSERT_TRUE(canonical_mmio.has_value()) << canonical_mmio.error().message;
  const auto canonical_mmio_check = checker::check(*canonical_mmio, context);
  ASSERT_FALSE(canonical_mmio_check.has_value());
  EXPECT_EQ(canonical_mmio_check.error().back().kind,
            checker::CheckDiagnosticKind::MemoryConsistencyViolation);

  const auto unknown_generic =
      resolve<Ld>(parse_instruction("ld.acquire.gpu.u32 %r0, [%rd0];"));
  ASSERT_TRUE(unknown_generic.has_value()) << unknown_generic.error().message;
  EXPECT_TRUE(checker::check(*unknown_generic, context).has_value());
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

}  // namespace
}  // namespace ptx_frontend::resolved_ir
