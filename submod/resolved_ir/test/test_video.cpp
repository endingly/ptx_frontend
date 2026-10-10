#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/model/video/vadd.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/resolved_ir/ptx_video.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

static_assert(VaddScalar::video_lanes == VideoLanes::Scalar);
static_assert(VaddScalar::video_operation == VideoOperation::Arithmetic);

/** Resolve a standalone instruction and release all parse storage on return. */
std::expected<std::unique_ptr<Instruction>, ResolveDiagnostic> video_resolve(
    std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseInstruction();
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(
        ResolveDiagnostic{.message = "Video syntax failed."});
  return resolveInstruction(*ast);
}

/** Modern generic target used independently of assembler support for old SMs. */
checker::Context video_context() {
  return {.target = {.ptx_version = {9, 3}, .sm_version = 80}};
}

/** All scalar signedness combinations and three layouts survive AST release. */
TEST(Video, ScalarVaddTypesAndLayouts) {
  for (const auto dtype : {"s32", "u32"})
    for (const auto atype : {"s32", "u32"})
      for (const auto btype : {"s32", "u32"}) {
        const std::string suffix =
            std::string("vadd.") + dtype + "." + atype + "." + btype;
        for (const std::string tail :
             {" %r0, %r1, %r2;", " %r0, %r1.b3, %r2.h1;",
              " %r0, -1, 0xFFFFFFFF;", ".sat %r0, %r1, %r2;",
              ".add %r0, %r1, %r2, %r3;", ".min %r0, %r1, %r2, 1;",
              ".sat.add %r0, %r1, %r2, %r3;", ".sat.min %r0, %r1, %r2, %r3;",
              ".sat.max %r0, %r1, %r2, %r3;", ".max %r0, %r1, %r2, -1;",
              " %r0.b0, %r1, %r2, %r3;", ".sat %r0.h1, %r1.h0, 2, %r3;"}) {
          SCOPED_TRACE(suffix + tail);
          auto value = video_resolve(suffix + tail);
          ASSERT_TRUE(value.has_value()) << value.error().message;
          auto checked = (*value)->check(video_context());
          ASSERT_TRUE(checked.has_value()) << checked.error().front().message;
        }
      }
}

/** Every scalar byte/half selector is accepted independently for a, b, and d. */
TEST(Video, ScalarSelectorDomains) {
  for (const auto selector : {"b0", "b1", "b2", "b3", "h0", "h1"}) {
    const std::string selected = std::string("%r1.") + selector;
    for (const std::string source :
         {"vadd.s32.u32.s32 %r0, " + selected + ", %r2;",
          "vadd.s32.u32.s32 %r0, %r2, " + selected + ";",
          "vadd.s32.u32.s32 " + selected + ", %r2, %r3, %r4;"}) {
      auto value = video_resolve(source);
      ASSERT_TRUE(value.has_value()) << source;
      EXPECT_TRUE((*value)->check(video_context())) << source;
    }
  }
}

/** Narrowed scalar constants preserve original 64-bit integer source bits. */
TEST(Video, ScalarImmediateProvenance) {
  auto instruction = video_resolve("vadd.s32.s32.u32 %r0, -1, 0x100000001;");
  ASSERT_TRUE(instruction.has_value());
  const auto& value = dynamic_cast<const VaddScalar&>(**instruction);
  const auto& a = std::get<ResolvedImmediate>(value.a.value.value.value);
  const auto& b = std::get<ResolvedImmediate>(value.b.value.value.value);
  EXPECT_EQ(a.type, ScalarType::S32);
  EXPECT_EQ(a.bits, 0xFFFFFFFFu);
  ASSERT_TRUE(a.integer_source_bits.has_value());
  EXPECT_EQ(*a.integer_source_bits, UINT64_MAX);
  EXPECT_TRUE(a.is_negative);
  EXPECT_FALSE(value.a.value.negated);
  EXPECT_EQ(b.type, ScalarType::U32);
  EXPECT_EQ(b.bits, 1u);
  ASSERT_TRUE(b.integer_source_bits.has_value());
  EXPECT_EQ(*b.integer_source_bits, uint64_t{0x100000001});
  EXPECT_FALSE(b.is_negative);
  EXPECT_TRUE(value.check(video_context()));
}

/** Source-bound video carriers remain valid after all syntax storage is released. */
TEST(Video, OwnedModuleAndSourceAssociations) {
  constexpr std::string_view source = R"ptx(
.version 9.3
.target sm_80
.address_size 64
.entry k() {
  .reg .b32 %r<4>;
  vadd.s32.u32.s32 %r0.b1, %r1.b3, %r2.h1, %r3;
}
)ptx";
  auto module = [&] {
    PtxSyntaxParser parser(source);
    auto ast = parser.parseModule();
    EXPECT_TRUE(ast.has_value());
    return resolveModuleOnly(*ast);
  }();
  ASSERT_TRUE(module.has_value()) << module.error().front().message;
  EXPECT_TRUE(validateModule(*module));
  PtxSyntaxParser replay(source);
  auto ast = replay.parseModule();
  ASSERT_TRUE(ast.has_value());
  EXPECT_TRUE(validateModule(*ast, *module));
  auto& value = dynamic_cast<VaddScalar&>(*module->functions[0].body[0]);
  auto& a = std::get<ResolvedRegisterRef>(value.a.value.value.value);
  ASSERT_EQ(a.parameterized_index, 1u);
  const auto original = a;
  a.parameterized_index = 4u;
  const auto invalid_member = validateModule(*module);
  ASSERT_FALSE(invalid_member.has_value());
  ASSERT_FALSE(value.a.value.value.locs.empty());
  EXPECT_TRUE(
      std::ranges::any_of(invalid_member.error(), [&](const auto& diagnostic) {
        return diagnostic.kind ==
                   checker::CheckDiagnosticKind::ModuleSourceMismatch &&
               diagnostic.range == value.a.value.value.locs.front();
      }));
  a = original;
  EXPECT_TRUE(validateModule(*module));
  EXPECT_TRUE(validateModule(*ast, *module));

  std::string changed_source(source);
  const auto selection = changed_source.find("%r1.b3");
  ASSERT_NE(selection, std::string::npos);
  changed_source.replace(selection, 6, "%r1.b2");
  PtxSyntaxParser changed_parser(changed_source);
  auto changed_ast = changed_parser.parseModule();
  ASSERT_TRUE(changed_ast.has_value());
  EXPECT_FALSE(validateModule(*changed_ast, *module));
}

/** Composite carrier diagnostics anchor to the identifier inside its selector. */
TEST(Video, BoundCarrierReferenceRanges) {
  constexpr std::string_view source = R"ptx(.version 9.3
.target sm_80
.entry k() { .reg .b32 %r<3>; vadd.s32.s32.s32 %r0, %r1.b3, %r2; })ptx";
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  ASSERT_TRUE(ast.has_value());
  auto module = resolveModuleOnly(*ast);
  ASSERT_TRUE(module.has_value());
  auto& value = dynamic_cast<VaddScalar&>(*module->functions[0].body[0]);
  auto& a = std::get<ResolvedRegisterRef>(value.a.value.value.value);
  ASSERT_FALSE(value.a.value.value.locs.empty());
  a.declared_type = ScalarType::F32;
  const auto checked = validateModule(*module);
  ASSERT_FALSE(checked.has_value());
  EXPECT_TRUE(std::ranges::any_of(checked.error(), [&](const auto& diagnostic) {
    return diagnostic.range == value.a.value.value.locs.front();
  }));
}

/** Scalar availability is exactly PTX 2.0 and SM 20, independently gated. */
TEST(Video, ScalarVaddTargetRequirements) {
  auto value = video_resolve("vadd.s32.s32.s32 %r0, %r1, %r2;");
  ASSERT_TRUE(value.has_value());
  auto context = video_context();
  context.target = {.ptx_version = {2, 0}, .sm_version = 20};
  EXPECT_TRUE((*value)->check(context));
  context.target.sm_version = 13;
  EXPECT_FALSE((*value)->check(context));
  context.target.sm_version = 20;
  context.target.ptx_version = {1, 5};
  EXPECT_FALSE((*value)->check(context));
}

/** Shape selection and semantic control coupling reject malformed source forms. */
TEST(Video, RejectsScalarVaddInvalidForms) {
  for (const auto source :
       {"vadd.s32.s32.s32 %r0, %r1, %r2, %r3;",
        "vadd.s32.s32.s32.add %r0, %r1, %r2;",
        "vadd.s32.s32.s32.add %r0.b0, %r1, %r2, %r3;",
        "vadd.s32.s32.s32 %r0.b0, %r1, %r2;",
        "vadd.s32.s32.s32 %r0, %r1.b4, %r2;",
        "vadd.s32.s32.s32 %r0, %r1.h2, %r2;",
        "vadd.s32.s32.s32 %r0, %r1.h10, %r2;",
        "vadd.s32.s32.s32 %r0, -%r1, %r2;", "vadd.s32.s32.s32 %r0, 1.0, %r2;",
        "vadd.s32.s32.s32 _, %r1, %r2;", "vadd.s32.s32.s32 %r0, %tid.x, %r2;",
        "vadd.u8.u8.u8 %r0, %r1, %r2;"}) {
    SCOPED_TRACE(source);
    auto value = video_resolve(source);
    if (value)
      EXPECT_FALSE((*value)->check(video_context()));
  }
}

/** AST-free checks revalidate mutable selector tags, integer bits and controls. */
TEST(Video, RechecksMutatedScalarVaddPayloads) {
  auto instruction = video_resolve("vadd.s32.s32.s32 %r0.b0, %r1, %r2, -1;");
  ASSERT_TRUE(instruction.has_value());
  auto& value = dynamic_cast<VaddScalar&>(**instruction);
  const auto original = value.dst.value.selector;
  value.dst.value.selector->value = VideoByteSwizzle{{3, 2, 1, 0}};
  EXPECT_FALSE(value.check(video_context()));
  value.dst.value.selector = original;
  value.a.value.negated = true;
  EXPECT_FALSE(value.check(video_context()));
  value.a.value.negated = false;
  ASSERT_TRUE(value.c.has_value());
  auto& immediate = std::get<ResolvedImmediate>(value.c->value.value.value);
  immediate.bits ^= 1;
  EXPECT_FALSE(value.check(video_context()));
  immediate.bits ^= 1;
  value.secondary.value = VideoSecondaryOp::Add;
  EXPECT_FALSE(value.check(video_context()));
  value.secondary.value = VideoSecondaryOp::None;
  EXPECT_TRUE(value.check(video_context()));
}

/** Typed defaults expose pair-wide source indices without fabricating syntax. */
TEST(Video, TypedSelectorDefaultsAndMasks) {
  const auto a =
      video_default_selector(VideoLanes::Two, VideoOperandPosition::A);
  const auto b =
      video_default_selector(VideoLanes::Four, VideoOperandPosition::B);
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(std::get<VideoHalfSwizzle>(*a).indices,
            (std::array<uint8_t, 2>{1, 0}));
  EXPECT_EQ(std::get<VideoByteSwizzle>(*b).indices,
            (std::array<uint8_t, 4>{7, 6, 5, 4}));
  EXPECT_TRUE(video_selector_is_well_formed(VideoByteSwizzle{{7, 7, 0, 0}}));
  EXPECT_FALSE(video_selector_is_well_formed(VideoByteSwizzle{{8, 0, 0, 0}}));
  EXPECT_FALSE(video_selector_is_well_formed(VideoHalfMask{{0, 1}, 2}));
  EXPECT_FALSE(video_selector_is_well_formed(VideoByteMask{{3, 3, 0, 0}, 2}));
  EXPECT_FALSE(video_selector_is_well_formed(VideoByteMask{{0, 0, 0, 0}, 0}));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
