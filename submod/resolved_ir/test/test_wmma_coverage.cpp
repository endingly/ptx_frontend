#include <gtest/gtest.h>

#include <algorithm>
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
  auto* load = dynamic_cast<WmmaLoadAM16n16k16RowGlobalF16*>(body[0].get());
  auto* mma = dynamic_cast<WmmaMmaM16n16k16RowColF32F16F16F32*>(body[3].get());
  auto* store = dynamic_cast<WmmaStoreDM16n16k16ColGlobalF32*>(body[4].get());
  ASSERT_NE(load, nullptr);
  ASSERT_NE(mma, nullptr);
  ASSERT_NE(store, nullptr);
  const auto& descriptor = *load->matrix_descriptor();
  EXPECT_EQ(descriptor.family, MatrixFamily::WMMA_LOAD);
  EXPECT_EQ(descriptor.shape, (MatrixShape{16, 16, 16}));
  EXPECT_EQ(descriptor.a_layout, MatrixLayout::ROW);
  EXPECT_EQ(descriptor.fragments[0].register_count, 8);
  EXPECT_EQ(mma->matrix_descriptor()->family, MatrixFamily::WMMA_MMA);
  EXPECT_EQ(store->matrix_descriptor()->d_layout, MatrixLayout::COL);

  const checker::Context supported{
      .target = {.ptx_version = {9, 3}, .sm_version = 80}};
  for (const auto& instruction : body)
    EXPECT_TRUE(instruction->check(supported).has_value());
  EXPECT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
  ASSERT_NE(load->matrix_descriptor(), nullptr);
  EXPECT_EQ(load->matrix_descriptor(),
            &WmmaLoadAM16n16k16RowGlobalF16::matrix_topology);
  EXPECT_EQ(load->instruction_kind(), WmmaLoadAM16n16k16RowGlobalF16::kind);
  const auto saved_layout = load->operand_layout;
  load->operand_layout.value = 9999;
  EXPECT_FALSE(load->check(supported).has_value());
  load->operand_layout = saved_layout;
  auto& a_identity = load->dst.value.elements.front()->symbol_id;
  const auto saved_identity = a_identity;
  a_identity = binding::SymbolId{.value = 999999u};
  EXPECT_FALSE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
  a_identity = saved_identity;
  const checker::Context too_old{
      .target = {.ptx_version = {9, 3}, .sm_version = 60}};
  EXPECT_FALSE(load->check(too_old).has_value());
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
  auto* mma = dynamic_cast<WmmaMmaM8n8k4RowColF64F64F64F64*>(body[1].get());
  ASSERT_NE(mma, nullptr);
  const auto& descriptor = *mma->matrix_descriptor();
  EXPECT_EQ(descriptor.fragments[0].register_count, 2);
  EXPECT_EQ(descriptor.fragments[3].register_count, 2);
  const checker::Context supported{
      .target = {.ptx_version = {9, 3}, .sm_version = 80}};
  for (std::size_t index = 0; index < 3; ++index)
    EXPECT_TRUE(body[index]->check(supported).has_value());
  auto* load_c = dynamic_cast<WmmaLoadCM8n8k4ColGlobalF64*>(body[0].get());
  ASSERT_NE(load_c, nullptr);
  ASSERT_TRUE(load_c->stride);
  auto& stride = std::get<ResolvedImmediate>(load_c->stride->value);
  stride.bits = 9;
  EXPECT_FALSE(load_c->check(supported).has_value());
  stride.bits = 8;
  stride.integer_source_bits = 9;
  EXPECT_FALSE(load_c->check(supported).has_value());
  stride.integer_source_bits = 8;
  auto* below = dynamic_cast<WmmaLoadCM8n8k4ColGlobalF64*>(body[3].get());
  auto* misaligned_form =
      dynamic_cast<WmmaLoadCM8n8k4ColGlobalF64*>(body[4].get());
  ASSERT_NE(below, nullptr);
  ASSERT_NE(misaligned_form, nullptr);
  ASSERT_TRUE(below->stride);
  ASSERT_TRUE(misaligned_form->stride);
  auto& below_minimum = std::get<ResolvedImmediate>(below->stride->value);
  below_minimum.bits = 7;
  below_minimum.integer_source_bits = 7;
  auto& misaligned =
      std::get<ResolvedImmediate>(misaligned_form->stride->value);
  misaligned.bits = 9;
  misaligned.integer_source_bits = 9;
  EXPECT_FALSE(body[3]->check(supported).has_value());
  EXPECT_FALSE(body[4]->check(supported).has_value());
  const checker::Context too_old{
      .target = {.ptx_version = {9, 3}, .sm_version = 75}};
  EXPECT_FALSE(body[1]->check(too_old).has_value());

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
  const auto* instruction =
      dynamic_cast<const WmmaMmaM8n8k128RowColS32B1B1S32AndPopc*>(
          resolved->functions.front().body.front().get());
  ASSERT_NE(instruction, nullptr);
  EXPECT_EQ(instruction->instruction_kind(),
            WmmaMmaM8n8k128RowColS32B1B1S32AndPopc::kind);
  ASSERT_NE(instruction->matrix_descriptor(), nullptr);
  EXPECT_EQ(instruction->matrix_descriptor(),
            &WmmaMmaM8n8k128RowColS32B1B1S32AndPopc::matrix_topology);
  const checker::Context supported{
      .target = {.ptx_version = {9, 3}, .sm_version = 80}};
  EXPECT_TRUE(instruction->check(supported).has_value());
}

/** Whether an instruction exposes a source aligned modifier. */
template <typename T>
concept HasAlignedModifier = requires(T value) { value.aligned; };
static_assert(
    !HasAlignedModifier<WmmaLoadAM16n16k16RowGlobalF16ImplicitAligned>);
static_assert(
    !HasAlignedModifier<WmmaStoreDM16n16k16ColGlobalF32ImplicitAligned>);
static_assert(
    !HasAlignedModifier<WmmaMmaM16n16k16RowColF32F16F16F32ImplicitAligned>);
static_assert(HasAlignedModifier<WmmaLoadAM16n16k16RowGlobalF16>);

/** Build one complete FP16 pipeline with source-selected shape and alignment. */
std::string historicalWmmaModule(std::string_view version, bool aligned,
                                 std::string_view shape = "m16n16k16",
                                 std::string_view target = "sm_70") {
  const std::string qualifier = aligned ? ".aligned." : ".";
  const std::string suffix = qualifier + std::string(shape);
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) + R"ptx(
.address_size 64
.global .align 32 .b32 tile[256];
.entry kernel() {
.reg .f16x2 %a<8>, %b<8>;
.reg .f32 %c<8>, %d<8>;
wmma.load.a.sync)ptx" +
         suffix + R"ptx(.global.row.f16
{%a0,%a1,%a2,%a3,%a4,%a5,%a6,%a7}, [tile], 16;
wmma.mma.sync)ptx" +
         suffix + R"ptx(.row.col.f32.f32
{%d0,%d1,%d2,%d3,%d4,%d5,%d6,%d7},
{%a0,%a1,%a2,%a3,%a4,%a5,%a6,%a7},
{%b0,%b1,%b2,%b3,%b4,%b5,%b6,%b7},
{%c0,%c1,%c2,%c3,%c4,%c5,%c6,%c7};
wmma.store.d.sync)ptx" +
         suffix + R"ptx(.global.col.f32
[tile], {%d0,%d1,%d2,%d3,%d4,%d5,%d6,%d7}, 32;
})ptx";
}

/** Source omission selects a stable class before the half-open version gate. */
TEST(WmmaCoverage, HistoricalVersionMatrixAndExactIdentities) {
  for (const auto version : {"6.0", "6.1", "6.2", "6.3", "9.3"}) {
    for (const bool aligned : {false, true}) {
      for (const auto shape : {"m16n16k16", "m8n32k16", "m32n8k16"}) {
        SCOPED_TRACE(version);
        SCOPED_TRACE(shape);
        SCOPED_TRACE(aligned);
        const auto parsed = test_helpers::parseModule(
            historicalWmmaModule(version, aligned, shape));
        ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
        auto resolved = resolveModuleOnly(*parsed);
        ASSERT_TRUE(resolved) << resolved.error().front().message;
        const bool modern = std::string_view(version) >= "6.3";
        const bool shape_supported = std::string_view(version) != "6.0" ||
                                     std::string_view(shape) == "m16n16k16";
        const bool accepted = shape_supported && aligned == modern;
        for (const auto policy :
             {ModuleValidationPolicy::AvailableContext,
              ModuleValidationPolicy::RequireCompleteContext}) {
          const auto checked = validateModule(*parsed, *resolved, policy);
          EXPECT_EQ(bool(checked), accepted);
          if (!accepted) {
            ASSERT_FALSE(checked);
            EXPECT_EQ(checked.error().front().range,
                      resolved->functions.front().instruction_ranges.front());
          }
        }
        EXPECT_EQ(bool(resolveAndValidateModule(*parsed)), accepted);
        if (std::string_view(shape) == "m16n16k16") {
          const auto& body = resolved->functions.front().body;
          EXPECT_EQ(body[0]->instruction_kind(),
                    aligned
                        ? WmmaLoadAM16n16k16RowGlobalF16::kind
                        : WmmaLoadAM16n16k16RowGlobalF16ImplicitAligned::kind);
          EXPECT_EQ(
              body[1]->instruction_kind(),
              aligned
                  ? WmmaMmaM16n16k16RowColF32F16F16F32::kind
                  : WmmaMmaM16n16k16RowColF32F16F16F32ImplicitAligned::kind);
          EXPECT_EQ(body[2]->instruction_kind(),
                    aligned
                        ? WmmaStoreDM16n16k16ColGlobalF32::kind
                        : WmmaStoreDM16n16k16ColGlobalF32ImplicitAligned::kind);
        }
      }
    }
  }
}

/** Owned clones retain omission identity, topology and source diagnostic ranges. */
TEST(WmmaCoverage, HistoricalOwnershipReplacementAndTarget) {
  std::optional<ResolvedModule> owned;
  {
    const auto parsed =
        test_helpers::parseModule(historicalWmmaModule("6.2", false));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveAndValidateModule(*parsed);
    ASSERT_TRUE(resolved) << resolved.error().front().message;
    owned.emplace(std::move(*resolved));
  }
  auto copy = *owned;
  const checker::Context supported{
      .target = {.ptx_version = {6, 2}, .sm_version = 70}};
  const checker::Context too_old{
      .target = {.ptx_version = {6, 2}, .sm_version = 60}};
  for (std::size_t index = 0; index < 3; ++index) {
    const auto& instruction = copy.functions.front().body[index];
    auto clone = instruction->clone();
    EXPECT_EQ(clone->instruction_kind(), instruction->instruction_kind());
    EXPECT_TRUE(clone->check(supported));
    EXPECT_FALSE(clone->check(too_old));
  }
  auto* load = dynamic_cast<WmmaLoadAM16n16k16RowGlobalF16ImplicitAligned*>(
      copy.functions.front().body.front().get());
  ASSERT_NE(load, nullptr);
  const auto cloned_load = load->clone();
  const auto* typed_clone =
      dynamic_cast<const WmmaLoadAM16n16k16RowGlobalF16ImplicitAligned*>(
          cloned_load.get());
  ASSERT_NE(typed_clone, nullptr);
  EXPECT_EQ(typed_clone->matrix_descriptor(),
            &WmmaLoadAM16n16k16RowGlobalF16ImplicitAligned::matrix_topology);
  EXPECT_EQ(typed_clone->matrix_descriptor()->family, MatrixFamily::WMMA_LOAD);
  EXPECT_EQ(typed_clone->matrix_descriptor()->shape, (MatrixShape{16, 16, 16}));
  EXPECT_EQ(typed_clone->matrix_descriptor()->fragments[0].register_count, 8);
  ASSERT_TRUE(load->stride);
  auto& stride = std::get<ResolvedImmediate>(load->stride->value);
  stride.bits = 17;
  stride.integer_source_bits = 17;
  EXPECT_FALSE(load->check(supported));
  stride.bits = 16;
  stride.integer_source_bits = 16;
  EXPECT_TRUE(load->check(supported));
  for (const auto policy : {ModuleValidationPolicy::AvailableContext,
                            ModuleValidationPolicy::RequireCompleteContext}) {
    EXPECT_TRUE(validateModule(copy, policy));
    const auto replacement =
        test_helpers::parseModule(historicalWmmaModule("6.3", false));
    ASSERT_MODULE_PARSE_SUCCEEDS(replacement);
    const auto result = validateModule(*replacement, copy, policy);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().front().range,
              copy.functions.front().instruction_ranges.front());
  }
  for (auto& region : copy.header.regions)
    region.version = checker::PtxVersion{6, 3};
  for (auto& function : copy.functions)
    function.source_version = checker::PtxVersion{6, 3};
  EXPECT_FALSE(validateModule(copy, ModuleValidationPolicy::AvailableContext));
  EXPECT_FALSE(
      validateModule(copy, ModuleValidationPolicy::RequireCompleteContext));
  EXPECT_TRUE(validateModule(*owned));
}

/** Standalone selection owns only the fields actually written in the source. */
TEST(WmmaCoverage, HistoricalStandaloneAndTopologyRejection) {
  const std::string source =
      "wmma.load.a.sync.m16n16k16.global.row.f16 "
      "{%a0,%a1,%a2,%a3,%a4,%a5,%a6,%a7}, [tile], 16;";
  std::unique_ptr<Instruction> owned;
  {
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_TRUE(parsed);
    auto resolved = resolveInstruction(*parsed);
    ASSERT_TRUE(resolved) << resolved.error().message;
    owned = std::move(*resolved);
  }
  EXPECT_EQ(owned->instruction_kind(),
            WmmaLoadAM16n16k16RowGlobalF16ImplicitAligned::kind);
  const auto clone = owned->clone();
  for (const auto version :
       {checker::PtxVersion{6, 0}, {6, 1}, {6, 2}, {6, 3}, {9, 3}}) {
    const checker::Context context{
        .target = {.ptx_version = version, .sm_version = 70}};
    EXPECT_EQ(bool(clone->check(context)),
              version < (checker::PtxVersion{6, 3}));
  }
  auto invalid = historicalWmmaModule("6.2", false);
  const auto position = invalid.find("%a6,%a7}");
  ASSERT_NE(position, std::string::npos);
  invalid.replace(position, 8, "%a6}");
  const auto parsed = test_helpers::parseModule(invalid);
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  EXPECT_FALSE(resolveModuleOnly(*parsed));
}

/** Build a source-selected FP16-input MMA with exact packed fragment sizes. */
std::string saturatedWmmaInstruction(std::string_view shape, bool aligned,
                                     std::string_view a_layout,
                                     std::string_view b_layout,
                                     std::string_view d_type,
                                     std::string_view c_type, bool saturated) {
  const auto fragment = [](char prefix, int count) {
    std::string result = "{";
    for (int i = 0; i < count; ++i) {
      if (i)
        result += ',';
      result += '%' + std::string(1, prefix) + std::to_string(i);
    }
    return result + '}';
  };
  return std::string("wmma.mma.sync.") + (aligned ? "aligned." : "") +
         std::string(shape) + '.' + std::string(a_layout) + '.' +
         std::string(b_layout) + '.' + std::string(d_type) + '.' +
         std::string(c_type) + (saturated ? ".satfinite " : " ") +
         fragment('d', d_type == "f16" ? 4 : 8) + ", " + fragment('a', 8) +
         ", " + fragment('b', 8) + ", " +
         fragment('c', c_type == "f16" ? 4 : 8) + ";";
}

/** Wrap an FP16-input MMA in a module whose version can change independently. */
std::string saturatedWmmaModule(std::string_view instruction,
                                std::string_view version) {
  return ".version " + std::string(version) + R"ptx(
.target sm_70
.entry kernel() {
.reg .f16x2 %a<8>, %b<8>, %h<4>;
.reg .b32 %d<8>, %c<8>;
)ptx" + std::string(instruction) +
         "\n}";
}

/** Exhaust all existing FP16 forms without changing source-driven identity. */
TEST(WmmaCoverage, FloatingSaturationVersionAndTopologyMatrix) {
  for (const auto shape : {"m16n16k16", "m8n32k16", "m32n8k16"})
    for (const bool aligned : {false, true})
      for (const auto a : {"row", "col"})
        for (const auto b : {"row", "col"})
          for (const auto d : {"f16", "f32"})
            for (const auto c : {"f16", "f32"})
              for (const bool saturated : {false, true}) {
                const auto source = saturatedWmmaInstruction(
                    shape, aligned, a, b, d, c, saturated);
                SCOPED_TRACE(source);
                const auto parsed = test_helpers::parseInstruction(source);
                ASSERT_TRUE(parsed);
                auto resolved = resolveInstruction(*parsed);
                ASSERT_TRUE(resolved) << resolved.error().message;
                const auto kind = (*resolved)->instruction_kind();
                for (const auto version : {checker::PtxVersion{6, 0},
                                           {6, 1},
                                           {6, 2},
                                           {6, 3},
                                           {6, 4},
                                           {6, 5},
                                           {9, 3}}) {
                  const bool accepted =
                      (std::string_view(shape) == "m16n16k16" ||
                       version >= (checker::PtxVersion{6, 1})) &&
                      aligned == (version >= (checker::PtxVersion{6, 3})) &&
                      (!saturated || version < (checker::PtxVersion{6, 5}));
                  const checker::Context context{
                      .target = {.ptx_version = version, .sm_version = 70}};
                  EXPECT_EQ(bool((*resolved)->check(context)), accepted);
                  EXPECT_EQ((*resolved)->instruction_kind(), kind);
                  const auto module =
                      test_helpers::parseModule(saturatedWmmaModule(
                          source, std::to_string(version.major) + "." +
                                      std::to_string(version.minor)));
                  ASSERT_MODULE_PARSE_SUCCEEDS(module);
                  auto owned = resolveModuleOnly(*module);
                  ASSERT_TRUE(owned) << owned.error().front().message;
                  for (const auto policy :
                       {ModuleValidationPolicy::AvailableContext,
                        ModuleValidationPolicy::RequireCompleteContext})
                    EXPECT_EQ(bool(validateModule(*module, *owned, policy)),
                              accepted);
                }
                const checker::Context below_target{
                    .target = {.ptx_version = aligned
                                                  ? checker::PtxVersion{6, 4}
                                                  : checker::PtxVersion{6, 2},
                               .sm_version = 60}};
                EXPECT_FALSE((*resolved)->check(below_target));
              }
}

/** Selected flag values, locations and owned context survive copies and clones. */
TEST(WmmaCoverage, FloatingSaturationOwnershipMutationAndDiagnostics) {
  for (const bool aligned : {false, true}) {
    const auto source = saturatedWmmaInstruction("m16n16k16", aligned, "row",
                                                 "col", "f32", "f32", true);
    std::optional<ResolvedModule> owned;
    {
      const auto parsed = test_helpers::parseModule(
          saturatedWmmaModule(source, aligned ? "6.4" : "6.2"));
      ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
      auto result = resolveAndValidateModule(*parsed);
      ASSERT_TRUE(result) << result.error().front().message;
      owned.emplace(std::move(*result));
    }
    auto copy = *owned;
    auto& instruction = copy.functions.front().body.front();
    const auto check_flag = [&]<typename T>(T& typed)
      requires requires(T value) {
        value.satfinite.value;
        value.satfinite.locs;
      }
    {
      EXPECT_TRUE(typed.satfinite.value);
      ASSERT_EQ(typed.satfinite.locs.size(), 1u);
      const auto range = typed.satfinite.locs.front();
      const auto column = static_cast<int32_t>(source.find(".satfinite") + 1);
      EXPECT_EQ(range, (SourceRange{{6, column}, {6, column + 10}}));
      const checker::Context removed{
          .target = {.ptx_version = {6, 5}, .sm_version = 70}};
      auto result = typed.check(removed);
      ASSERT_FALSE(result);
      // Explicit alignment remains valid, so its rejection must locate the flag.
      if (aligned)
        EXPECT_EQ(result.error().front().range, range);
      auto clone = typed.clone();
      const auto* cloned = dynamic_cast<const T*>(clone.get());
      ASSERT_NE(cloned, nullptr);
      EXPECT_TRUE(cloned->satfinite.value);
      EXPECT_EQ(cloned->satfinite.locs, typed.satfinite.locs);
      EXPECT_EQ(cloned->instruction_kind(), typed.instruction_kind());
      EXPECT_TRUE(
          validateModule(copy, ModuleValidationPolicy::RequireCompleteContext));
      const auto replacement =
          test_helpers::parseModule(saturatedWmmaModule(source, "6.5"));
      ASSERT_MODULE_PARSE_SUCCEEDS(replacement);
      for (const auto policy : {ModuleValidationPolicy::AvailableContext,
                                ModuleValidationPolicy::RequireCompleteContext})
        EXPECT_FALSE(validateModule(*replacement, copy, policy));
      for (auto& region : copy.header.regions)
        region.version = checker::PtxVersion{6, 5};
      for (auto& function : copy.functions)
        function.source_version = checker::PtxVersion{6, 5};
      EXPECT_FALSE(validateModule(copy));
      typed.satfinite.value = false;
      const auto mismatched = typed.check(removed);
      ASSERT_FALSE(mismatched);
      EXPECT_TRUE(
          std::ranges::any_of(mismatched.error(), [](const auto & diagnostic) {
            return diagnostic.kind ==
                   checker::CheckDiagnosticKind::ModuleSourceMismatch;
          }));
      typed.satfinite.value = true;
    };
    if (aligned) {
      auto* typed =
          dynamic_cast<WmmaMmaM16n16k16RowColF32F16F16F32*>(instruction.get());
      ASSERT_NE(typed, nullptr);
      check_flag(*typed);
    } else {
      auto* typed =
          dynamic_cast<WmmaMmaM16n16k16RowColF32F16F16F32ImplicitAligned*>(
              instruction.get());
      ASSERT_NE(typed, nullptr);
      check_flag(*typed);
    }
    EXPECT_TRUE(validateModule(*owned));
  }
  const auto source = saturatedWmmaInstruction("m16n16k16", true, "row", "col",
                                               "f32", "f32", false);
  const auto parsed = test_helpers::parseInstruction(source);
  ASSERT_TRUE(parsed);
  auto resolved = resolveInstruction(*parsed);
  ASSERT_TRUE(resolved);
  auto* typed =
      dynamic_cast<WmmaMmaM16n16k16RowColF32F16F16F32*>(resolved->get());
  ASSERT_NE(typed, nullptr);
  EXPECT_FALSE(typed->satfinite.value);
  EXPECT_TRUE(typed->satfinite.locs.empty());
  const checker::Context modern{
      .target = {.ptx_version = {9, 3}, .sm_version = 70}};
  EXPECT_TRUE(typed->check(modern));
  typed->satfinite.value = true;
  const auto removed = typed->check(modern);
  ASSERT_FALSE(removed);
  EXPECT_TRUE(std::ranges::any_of(removed.error(), [](const auto& diagnostic) {
    return diagnostic.kind ==
           checker::CheckDiagnosticKind::UnsupportedPtxVersion;
  }));
}

/** Canonical modifier order retains the selected flag and original identity. */
TEST(WmmaCoverage, FloatingSaturationCanonicalOrder) {
  for (const bool aligned : {false, true}) {
    auto alias = saturatedWmmaInstruction("m16n16k16", aligned, "row", "col",
                                          "f32", "f32", true);
    auto canonical = alias;
    const auto position = canonical.find("m16n16k16.row.col");
    ASSERT_NE(position, std::string::npos);
    canonical.replace(position, 17, "row.col.m16n16k16");
    const auto alias_parsed = test_helpers::parseInstruction(alias);
    const auto canonical_parsed = test_helpers::parseInstruction(canonical);
    ASSERT_TRUE(alias_parsed);
    ASSERT_TRUE(canonical_parsed);
    const auto alias_resolved = resolveInstruction(*alias_parsed);
    const auto canonical_resolved = resolveInstruction(*canonical_parsed);
    ASSERT_TRUE(alias_resolved);
    ASSERT_TRUE(canonical_resolved);
    EXPECT_EQ((*alias_resolved)->instruction_kind(),
              (*canonical_resolved)->instruction_kind());
    const checker::Context supported{
        .target = {.ptx_version = aligned ? checker::PtxVersion{6, 4}
                                          : checker::PtxVersion{6, 2},
                   .sm_version = 70}};
    EXPECT_TRUE((*canonical_resolved)->check(supported));
  }
}

/** Floating compatibility does not admit saturation on later floating types. */
TEST(WmmaCoverage, FloatingSaturationLeavesOtherTypesUnchanged) {
  const auto fragment = [](int count) {
    std::string value = "{";
    for (int i = 0; i < count; ++i) {
      if (i)
        value += ',';
      value += "%r" + std::to_string(i);
    }
    return value + '}';
  };
  const checker::Context modern{
      .target = {.ptx_version = {9, 3}, .sm_version = 80}};
  for (const auto opcode :
       {"wmma.mma.sync.aligned.m16n16k16.row.col.f32.bf16.bf16.f32",
        "wmma.mma.sync.aligned.m16n16k8.row.col.f32.tf32.tf32.f32",
        "wmma.mma.sync.aligned.m8n8k4.row.col.f64.f64.f64.f64",
        "wmma.mma.sync.aligned.m16n16k16.row.col.s32.s8.s8.s32"}) {
    const std::string spelling = opcode;
    const bool integer = spelling.find("s8") != std::string::npos;
    const bool fp64 = spelling.find("f64") != std::string::npos;
    const auto operands = " " + fragment(fp64 ? 2 : 8) + ", " +
                          fragment(fp64      ? 1
                                   : integer ? 2
                                             : 4) +
                          ", " +
                          fragment(fp64      ? 1
                                   : integer ? 2
                                             : 4) +
                          ", " + fragment(fp64 ? 2 : 8) + ";";
    for (const bool saturated : {false, true}) {
      const auto parsed = test_helpers::parseInstruction(
          spelling + (saturated ? ".satfinite" : "") + operands);
      ASSERT_TRUE(parsed);
      const auto resolved = resolveInstruction(*parsed);
      EXPECT_EQ(bool(resolved), !saturated || integer) << spelling;
      if (resolved)
        EXPECT_TRUE((*resolved)->check(modern)) << spelling;
    }
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
