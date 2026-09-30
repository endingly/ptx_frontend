#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Keep WMMA load, compute, and store metadata alive after the syntax tree dies. */
TEST(WmmaCoverage, ResolvesOwnedF16PipelineAndChecksTarget) {
  std::optional<ResolvedModule> owned;
  {
    const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.global .align 32 .b32 tile[256];
.entry kernel() {
  .reg .f16x2 %a<8>;
  .reg .f16x2 %b<8>;
  .reg .f32 %c<8>;
  .reg .f32 %d<8>;
  .reg .u32 %stride;
  wmma.load.a.sync.aligned.m16n16k16.global.row.f16
    {%a0,%a1,%a2,%a3,%a4,%a5,%a6,%a7}, [tile], %stride;
  wmma.load.b.sync.aligned.m16n16k16.global.col.f16
    {%b0,%b1,%b2,%b3,%b4,%b5,%b6,%b7}, [tile], 16;
  wmma.load.c.sync.aligned.m16n16k16.global.row.f32
    {%c0,%c1,%c2,%c3,%c4,%c5,%c6,%c7}, [tile];
  wmma.mma.sync.aligned.m16n16k16.row.col.f32.f32
    {%d0,%d1,%d2,%d3,%d4,%d5,%d6,%d7},
    {%a0,%a1,%a2,%a3,%a4,%a5,%a6,%a7},
    {%b0,%b1,%b2,%b3,%b4,%b5,%b6,%b7},
    {%c0,%c1,%c2,%c3,%c4,%c5,%c6,%c7};
  wmma.store.d.sync.aligned.m16n16k16.global.col.f32
    [tile], {%d0,%d1,%d2,%d3,%d4,%d5,%d6,%d7}, 16;
}
)ptx");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModule(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned = std::move(*resolved);
  }

  ASSERT_EQ(owned->functions.front().body.size(), 5u);
  auto& body = owned->functions.front().body;
  auto& load = std::get<Wmma>(body[0]);
  const auto descriptor = std::visit(
      [](const auto& variant) { return variant.matrix.value; }, load.variant);
  EXPECT_EQ(descriptor.family, MatrixFamily::WMMA_LOAD);
  EXPECT_EQ(descriptor.shape, (MatrixShape{16, 16, 16}));
  EXPECT_EQ(descriptor.a_layout, MatrixLayout::ROW);
  EXPECT_EQ(descriptor.fragments[0].register_count, 8);
  EXPECT_EQ(std::visit(
                [](const auto& variant) { return variant.matrix.value.family; },
                std::get<Wmma>(body[3]).variant),
            MatrixFamily::WMMA_MMA);
  EXPECT_EQ(
      std::visit(
          [](const auto& variant) { return variant.matrix.value.d_layout; },
          std::get<Wmma>(body[4]).variant),
      MatrixLayout::COL);

  const checker::Context supported{
      .target = {.ptx_version = {9, 3}, .sm_version = 80}};
  for (const auto& instruction : body)
    EXPECT_TRUE(
        checker::check(std::get<Wmma>(instruction), supported).has_value());
  EXPECT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
  auto& load_a = std::get<Wmma::LoadAM16n16k16RowGlobalF16>(load.variant);
  ASSERT_TRUE(load.matrix_logical_index().has_value());
  ASSERT_NE(load.matrix_descriptor(), nullptr);
  EXPECT_EQ(load_a.matrix.value, *load.matrix_descriptor());
  const auto original_form = load.semantic_form.value;
  load.semantic_form.value = Wmma::VariantType::LoadAM16n16k16ColGlobalF16;
  ASSERT_TRUE(load.matrix_logical_index().has_value());
  EXPECT_FALSE(checker::check(load, supported).has_value());
  load.semantic_form.value = original_form;
  auto& load_operands =
      std::get<Wmma::LoadAM16n16k16RowGlobalF16::ExplicitStrideOperands>(
          load_a.operands);
  auto& a_identity = load_operands.dst.value.elements.front()->symbol_id;
  const auto saved_identity = a_identity;
  a_identity = binding::SymbolId{.value = 999999u};
  EXPECT_FALSE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
  a_identity = saved_identity;
  const checker::Context too_old{
      .target = {.ptx_version = {9, 3}, .sm_version = 60}};
  EXPECT_FALSE(checker::check(load, too_old).has_value());
}

/** Enforce WMMA register topology, integer pairing, and removed syntax. */
TEST(WmmaCoverage, RejectsInvalidFragmentsAndUnsupportedModifiers) {
  for (const std::string_view source : {
           R"ptx(.version 9.3 .target sm_80
             .global .align 32 .b32 tile[256]; .entry kernel() {
             .reg .f16x2 %a<7>;
             wmma.load.a.sync.aligned.m16n16k16.global.row.f16
               {%a0,%a1,%a2,%a3,%a4,%a5,%a6}, [tile]; })ptx",
           R"ptx(.version 9.3 .target sm_80 .entry kernel() {
             .reg .s32 %d<8>, %c<8>; .reg .b32 %a<2>, %b<2>;
             wmma.mma.sync.aligned.m16n16k16.row.col.s32.s8.u8.s32
               {%d0,%d1,%d2,%d3,%d4,%d5,%d6,%d7}, {%a0,%a1}, {%b0,%b1},
               {%c0,%c1,%c2,%c3,%c4,%c5,%c6,%c7}; })ptx",
           R"ptx(.version 9.3 .target sm_80 .entry kernel() {
             .reg .f16x2 %a<8>, %b<8>; .reg .f32 %d<8>, %c<8>;
             wmma.mma.sync.aligned.m16n16k16.row.col.f32.f32.satfinite
               {%d0,%d1,%d2,%d3,%d4,%d5,%d6,%d7},
               {%a0,%a1,%a2,%a3,%a4,%a5,%a6,%a7},
               {%b0,%b1,%b2,%b3,%b4,%b5,%b6,%b7},
               {%c0,%c1,%c2,%c3,%c4,%c5,%c6,%c7}; })ptx",
           R"ptx(.version 9.3 .target sm_80
             .global .align 32 .b32 tile[256]; .entry kernel() {
             .reg .f16x2 %a<8>;
             wmma.load.a.sync.m16n16k16.global.row.f16
               {%a0,%a1,%a2,%a3,%a4,%a5,%a6,%a7}, [tile]; })ptx",
           R"ptx(.version 9.3 .target sm_80
             .global .align 4 .b32 tile[256]; .entry kernel() {
             .reg .b32 %a;
             wmma.load.a.sync.aligned.m8n8k32.global.col.s4 {%a}, [tile]; })ptx",
       }) {
    const auto parsed = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveModule(*parsed).has_value()) << source;
  }
}

/** Check FP64 C/D cardinality and constant stride and architecture limits. */
TEST(WmmaCoverage, F64FragmentsAndStrideLimits) {
  const std::string source = R"ptx(
.version 9.3
.target sm_80
.address_size 64
.global .align 32 .b32 tile[256];
.entry kernel() {
  .reg .f64 %a, %b;
  .reg .f64 %c<2>;
  .reg .f64 %d<2>;
  wmma.load.c.sync.aligned.m8n8k4.global.col.f64 {%c0,%c1}, [tile], 8;
  wmma.mma.sync.aligned.m8n8k4.row.col.rz.f64.f64.f64.f64
    {%d0,%d1}, {%a}, {%b}, {%c0,%c1};
  wmma.store.d.sync.aligned.m8n8k4.global.row.f64 [tile], {%d0,%d1}, 8;
  wmma.load.c.sync.aligned.m8n8k4.global.col.f64 {%c0,%c1}, [tile], 8;
  wmma.load.c.sync.aligned.m8n8k4.global.col.f64 {%c0,%c1}, [tile], 8;
}
)ptx";
  const auto parsed = test_helpers::parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 5u);
  const auto descriptor =
      std::visit([](const auto& variant) { return variant.matrix.value; },
                 std::get<Wmma>(body[1]).variant);
  EXPECT_EQ(descriptor.fragments[0].register_count, 2);
  EXPECT_EQ(descriptor.fragments[3].register_count, 2);
  const checker::Context supported{
      .target = {.ptx_version = {9, 3}, .sm_version = 80}};
  for (std::size_t index = 0; index < 3; ++index)
    EXPECT_TRUE(
        checker::check(std::get<Wmma>(body[index]), supported).has_value());
  auto& load_c = std::get<Wmma>(body[0]);
  auto& stride = std::get<ResolvedImmediate>(
      std::get<Wmma::LoadCM8n8k4ColGlobalF64::ExplicitStrideOperands>(
          std::get<Wmma::LoadCM8n8k4ColGlobalF64>(load_c.variant).operands)
          .stride.value);
  stride.bits = 9;
  EXPECT_FALSE(checker::check(load_c, supported).has_value());
  stride.bits = 8;
  stride.integer_source_bits = 9;
  EXPECT_FALSE(checker::check(load_c, supported).has_value());
  stride.integer_source_bits = 8;
  auto& below_minimum = std::get<ResolvedImmediate>(
      std::get<Wmma::LoadCM8n8k4ColGlobalF64::ExplicitStrideOperands>(
          std::get<Wmma::LoadCM8n8k4ColGlobalF64>(
              std::get<Wmma>(body[3]).variant)
              .operands)
          .stride.value);
  below_minimum.bits = 7;
  below_minimum.integer_source_bits = 7;
  auto& misaligned = std::get<ResolvedImmediate>(
      std::get<Wmma::LoadCM8n8k4ColGlobalF64::ExplicitStrideOperands>(
          std::get<Wmma::LoadCM8n8k4ColGlobalF64>(
              std::get<Wmma>(body[4]).variant)
              .operands)
          .stride.value);
  misaligned.bits = 9;
  misaligned.integer_source_bits = 9;
  EXPECT_FALSE(checker::check(std::get<Wmma>(body[3]), supported).has_value());
  EXPECT_FALSE(checker::check(std::get<Wmma>(body[4]), supported).has_value());
  const checker::Context too_old{
      .target = {.ptx_version = {9, 3}, .sm_version = 75}};
  EXPECT_FALSE(checker::check(std::get<Wmma>(body[1]), too_old).has_value());

  std::string invalid_source = source;
  const auto stride_position = invalid_source.find("[tile], 8;");
  ASSERT_NE(stride_position, std::string::npos);
  invalid_source.replace(stride_position, 10, "[tile], 7;");
  const auto invalid_parsed = test_helpers::parseModule(invalid_source);
  ASSERT_MODULE_PARSE_SUCCEEDS(invalid_parsed);
  EXPECT_FALSE(resolveModule(*invalid_parsed).has_value());
}

/** Resolve a logical form above the legacy enum reflection scan range. */
TEST(WmmaCoverage, ResolvesHighestLogicalForm) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_80
.entry kernel() {
  .reg .b32 %a, %b;
  .reg .s32 %d<2>, %c<2>;
  wmma.mma.and.popc.sync.aligned.m8n8k128.row.col.s32.b1.b1.s32
    {%d0,%d1}, {%a}, {%b}, {%c0,%c1};
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& instruction =
      std::get<Wmma>(resolved->functions.front().body.front());
  EXPECT_EQ(static_cast<std::size_t>(instruction.semantic_form.value), 551u);
  ASSERT_TRUE(instruction.matrix_logical_index().has_value());
  EXPECT_EQ(*instruction.matrix_logical_index(), 551u);
  const checker::Context supported{
      .target = {.ptx_version = {9, 3}, .sm_version = 80}};
  EXPECT_TRUE(checker::check(instruction, supported).has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
