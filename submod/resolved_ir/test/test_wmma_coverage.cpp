#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
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
  const auto& body = owned->functions.front().body;
  const auto& load = std::get<Wmma>(body[0]);
  const auto descriptor = std::visit([](const auto& variant) {
    return variant.matrix.value;
  }, load.variant);
  EXPECT_EQ(descriptor.family, MatrixFamily::WMMA_LOAD);
  EXPECT_EQ(descriptor.shape, (MatrixShape{16, 16, 16}));
  EXPECT_EQ(descriptor.a_layout, MatrixLayout::ROW);
  EXPECT_EQ(descriptor.fragments[0].register_count, 8);
  EXPECT_EQ(std::visit([](const auto& variant) {
    return variant.matrix.value.family;
  }, std::get<Wmma>(body[3]).variant), MatrixFamily::WMMA_MMA);
  EXPECT_EQ(std::visit([](const auto& variant) {
    return variant.matrix.value.d_layout;
  }, std::get<Wmma>(body[4]).variant), MatrixLayout::COL);

  const checker::Context supported{
      .target = {.ptx_version = {9, 3}, .sm_version = 80}};
  for (const auto& instruction : body)
    EXPECT_TRUE(checker::check(std::get<Wmma>(instruction), supported).has_value());
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
  const auto parsed = test_helpers::parseModule(R"ptx(
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
  wmma.load.c.sync.aligned.m8n8k4.global.col.f64 {%c0,%c1}, [tile], 7;
  wmma.load.c.sync.aligned.m8n8k4.global.col.f64 {%c0,%c1}, [tile], 9;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 5u);
  const auto descriptor = std::visit([](const auto& variant) {
    return variant.matrix.value;
  }, std::get<Wmma>(body[1]).variant);
  EXPECT_EQ(descriptor.fragments[0].register_count, 2);
  EXPECT_EQ(descriptor.fragments[3].register_count, 2);
  const checker::Context supported{
      .target = {.ptx_version = {9, 3}, .sm_version = 80}};
  for (std::size_t index = 0; index < 3; ++index)
    EXPECT_TRUE(checker::check(std::get<Wmma>(body[index]), supported).has_value());
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
  EXPECT_FALSE(checker::check(std::get<Wmma>(body[3]), supported).has_value());
  EXPECT_FALSE(checker::check(std::get<Wmma>(body[4]), supported).has_value());
  const checker::Context too_old{
      .target = {.ptx_version = {9, 3}, .sm_version = 75}};
  EXPECT_FALSE(checker::check(std::get<Wmma>(body[1]), too_old).has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
