#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/model/video/vadd2.gen.hpp>
#include <ptx_frontend/resolved_ir/model/video/vadd4.gen.hpp>
#include <ptx_frontend/resolved_ir/model/video/vmad.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/resolved_ir/ptx_video.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve one source instruction without retaining any AST storage. */
std::expected<std::unique_ptr<Instruction>, ResolveDiagnostic> family_resolve(
    std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseInstruction();
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(
        ResolveDiagnostic{.message = "Video syntax failed."});
  return resolveInstruction(*ast);
}

/** Check source legality at an independently chosen frontend target. */
bool family_accepts(std::string_view source, uint32_t sm = 80,
                    checker::PtxVersion version = {9, 3}) {
  auto result = family_resolve(source);
  if (!result)
    return false;
  return (*result)
      ->check({.target = {.ptx_version = version, .sm_version = sm}})
      .has_value();
}

/** All scalar arithmetic slices retain every typed suffix and legal layout. */
TEST(VideoFamily, ScalarArithmeticSlices) {
  for (const auto opcode : {"vsub", "vabsdiff", "vmin", "vmax"})
    for (const auto dtype : {"s32", "u32"})
      for (const auto atype : {"s32", "u32"})
        for (const auto btype : {"s32", "u32"}) {
          const std::string prefix =
              std::string(opcode) + "." + dtype + "." + atype + "." + btype;
          for (const auto tail :
               {" %r0, %r1.b3, %r2.h1;", ".sat %r0, -1, 0xFFFFFFFF;",
                ".add %r0, %r1, %r2, %r3;", ".sat.min %r0, %r1, %r2, 1;",
                ".sat.max %r0, %r1, %r2, -1;",
                " %r0.h0, %r1.b0, %r2.h1, %r3;"}) {
            SCOPED_TRACE(prefix + tail);
            EXPECT_TRUE(family_accepts(prefix + tail));
          }
          EXPECT_FALSE(family_accepts(prefix + " %r0, %r1, %r2, %r3;"));
          EXPECT_FALSE(family_accepts(prefix + ".add %r0.b0, %r1, %r2, %r3;"));
        }
}

/** Shift mode is mandatory, ordered after saturation, with an unsigned b type. */
TEST(VideoFamily, ScalarShiftSlices) {
  for (const auto opcode : {"vshl", "vshr"})
    for (const auto dtype : {"s32", "u32"})
      for (const auto atype : {"s32", "u32"})
        for (const auto mode : {"clamp", "wrap"}) {
          const std::string prefix =
              std::string(opcode) + "." + dtype + "." + atype + ".u32";
          const std::string suffix = "." + std::string(mode);
          EXPECT_TRUE(family_accepts(prefix + suffix + " %r0, %r1.b1, -1;"));
          EXPECT_TRUE(family_accepts(prefix + ".sat" + suffix +
                                     ".max %r0, %r1, %r2, 1;"));
          EXPECT_TRUE(
              family_accepts(prefix + suffix + " %r0.b3, %r1, %r2, %r3;"));
          EXPECT_FALSE(family_accepts(prefix + " %r0, %r1, %r2;"));
          EXPECT_FALSE(family_accepts(prefix + suffix + ".sat %r0, %r1, %r2;"));
        }
  EXPECT_FALSE(family_accepts("vshl.s32.s32.s32.clamp %r0, %r1, %r2;"));
}

/** Comparison source types and secondary layouts do not invent a dtype. */
TEST(VideoFamily, ScalarComparisonSlice) {
  for (const auto atype : {"s32", "u32"})
    for (const auto btype : {"s32", "u32"})
      for (const auto comparison : {"eq", "ne", "lt", "le", "gt", "ge"}) {
        const std::string prefix =
            std::string("vset.") + atype + "." + btype + "." + comparison;
        EXPECT_TRUE(family_accepts(prefix + " %r0, -1, %r2.b3;"));
        EXPECT_TRUE(family_accepts(prefix + ".add %r0, %r1, %r2, -1;"));
        EXPECT_TRUE(family_accepts(prefix + ".min %r0, %r1, %r2, %r3;"));
        EXPECT_TRUE(family_accepts(prefix + ".max %r0, %r1, %r2, %r3;"));
        EXPECT_TRUE(family_accepts(prefix + " %r0.h1, %r1, %r2, %r3;"));
        EXPECT_FALSE(family_accepts(prefix + ".sat %r0, %r1, %r2;"));
      }
  EXPECT_FALSE(family_accepts("vset.s32.s32.s32.eq %r0, %r1, %r2;"));
}

/** Register minus controls remain distinct from signed numeric constants. */
TEST(VideoFamily, MadWrittenNegationAndScaleSlice) {
  for (const auto dtype : {"s32", "u32"})
    for (const auto atype : {"s32", "u32"})
      for (const auto btype : {"s32", "u32"})
        for (unsigned flags = 0; flags < 8; ++flags) {
          const bool a = flags & 1, b = flags & 2, c = flags & 4;
          const std::string prefix =
              std::string("vmad.") + dtype + "." + atype + "." + btype;
          const std::string operands = " %r0, " + std::string(a ? "-" : "") +
                                       "%r1.b3, " + (b ? "-" : "") +
                                       "%r2.h1, " + (c ? "-" : "") + "%r3;";
          const bool legal = !((a != b) && c);
          for (const auto scale : {"", ".shr7", ".shr15"}) {
            EXPECT_EQ(family_accepts(prefix + scale + operands), legal)
                << prefix + scale + operands;
            EXPECT_EQ(family_accepts(prefix + ".sat" + scale + operands),
                      legal);
            EXPECT_EQ(family_accepts(prefix + ".po" + scale + operands),
                      flags == 0);
          }
        }
  EXPECT_TRUE(family_accepts("vmad.u32.u32.u32.po.sat.shr15 %r0, 1, 2, -1;"));
  EXPECT_TRUE(family_accepts("vmad.u32.u32.u32 %r0, -%r1, %r2, -1;"));
  for (const auto source : {"vmad.s32.s32.s32.shr7.sat %r0, %r1, %r2, %r3;",
                            "vmad.s32.s32.s32.sat.po %r0, %r1, %r2, %r3;",
                            "vmad.s32.s32.s32.add %r0, %r1, %r2, %r3;",
                            "vmad.s32.s32.s32 %r0.b0, %r1, %r2, %r3;",
                            "vmad.s32.s32.s32 %r0, %r1, %r2, %r3.h0;"})
    EXPECT_FALSE(family_accepts(source)) << source;
  auto value = family_resolve("vmad.u32.s32.u32.shr7 %r0, -%r1.b3, -%r2, -1;");
  ASSERT_TRUE(value.has_value());
  auto& mad = dynamic_cast<VmadScalar&>(**value);
  EXPECT_TRUE(mad.a.value.negated);
  EXPECT_TRUE(mad.b.value.negated);
  EXPECT_FALSE(mad.c.value.negated);
  ASSERT_TRUE(mad.a.value.minus_range);
  EXPECT_TRUE(mad.a.value.selector);
  EXPECT_EQ(std::get<ResolvedImmediate>(mad.c.value.value.value).type,
            ScalarType::B32);
  const auto interpretation = video_mad_interpretation(
      mad.atype.value, mad.btype.value, mad.a.value.negated,
      mad.b.value.negated, mad.c.value.negated);
  ASSERT_TRUE(interpretation);
  EXPECT_TRUE(interpretation->product_signed);
  EXPECT_TRUE(interpretation->c_signed);
  EXPECT_TRUE(interpretation->result_signed);
  mad.po.value = true;
  EXPECT_FALSE(
      mad.check({.target = {.ptx_version = {9, 3}, .sm_version = 80}}));
}

/** Sign interpretation uses typed sources and written register flags alone. */
TEST(VideoFamily, MadInterpretationQuery) {
  for (const auto atype : {VideoType::S32, VideoType::U32})
    for (const auto btype : {VideoType::S32, VideoType::U32})
      for (unsigned flags = 0; flags < 8; ++flags) {
        const bool a = flags & 1, b = flags & 2, c = flags & 4;
        auto value = video_mad_interpretation(atype, btype, a, b, c);
        ASSERT_TRUE(value);
        const bool product =
            atype == VideoType::S32 || btype == VideoType::S32 || (a != b);
        EXPECT_EQ(value->product_signed, product);
        EXPECT_EQ(value->c_signed, product);
        EXPECT_EQ(value->result_signed, product || c);
      }
  EXPECT_FALSE(video_mad_interpretation(static_cast<VideoType>(255),
                                        VideoType::U32, false, false, false));
}

/** Each packed opcode accepts shared-bank swizzles and optional destination masks. */
TEST(VideoFamily, PackedArithmeticSlices) {
  for (const auto count : {2, 4})
    for (const auto stem :
         {"vadd", "vsub", "vavrg", "vabsdiff", "vmin", "vmax"})
      for (const auto dtype : {"s32", "u32"})
        for (const auto atype : {"s32", "u32"})
          for (const auto btype : {"s32", "u32"}) {
            const std::string prefix = std::string(stem) +
                                       std::to_string(count) + "." + dtype +
                                       "." + atype + "." + btype;
            const std::string selected =
                count == 2 ? " %r0.h0, %r1.h33, %r2.h00, %r3;"
                           : " %r0.b31, %r1.b7777, %r2.b0000, %r3;";
            EXPECT_TRUE(family_accepts(prefix + " %r0, %r1, %r2, %r3;"));
            EXPECT_TRUE(family_accepts(prefix + selected));
            EXPECT_TRUE(family_accepts(prefix + ".sat" + selected));
            EXPECT_TRUE(family_accepts(prefix + ".add" + selected));
            EXPECT_FALSE(family_accepts(prefix + ".sat.add" + selected));
            EXPECT_FALSE(family_accepts(prefix + " %r0, 1, %r2, %r3;"));
            EXPECT_FALSE(family_accepts(prefix + " %r0, %r1, -1, %r3;"));
            EXPECT_FALSE(family_accepts(prefix + " %r0, %r1, %r2, 1;"));
          }
}

/** Packed comparisons preserve supplied c and accept masked accumulation. */
TEST(VideoFamily, PackedComparisonSlices) {
  for (const auto count : {2, 4})
    for (const auto atype : {"s32", "u32"})
      for (const auto btype : {"s32", "u32"})
        for (const auto comparison : {"eq", "ne", "lt", "le", "gt", "ge"}) {
          const std::string prefix = std::string("vset") +
                                     std::to_string(count) + "." + atype + "." +
                                     btype + "." + comparison;
          const std::string selected =
              count == 2 ? " %r0.h1, %r1.h30, %r2.h22, %r3;"
                         : " %r0.b320, %r1.b7654, %r2.b3333, %r3;";
          EXPECT_TRUE(family_accepts(prefix + selected));
          EXPECT_TRUE(family_accepts(prefix + ".add" + selected));
          EXPECT_FALSE(family_accepts(prefix + ".max" + selected));
          EXPECT_FALSE(family_accepts(prefix + ".sat" + selected));
          EXPECT_FALSE(family_accepts(prefix + " %r0, %r1, %r2;"));
        }
}

/** Every canonical destination subset is accepted; repetition is source-only. */
TEST(VideoFamily, PackedMaskDomainsAndAvailability) {
  for (unsigned mask = 1; mask < 16; ++mask) {
    std::string spelling;
    for (int lane = 3; lane >= 0; --lane)
      if (mask & (1u << lane))
        spelling += char('0' + lane);
    for (const auto control : {"", ".sat", ".add"})
      EXPECT_TRUE(family_accepts(std::string("vadd4.s32.u32.s32") + control +
                                 " %r0.b" + spelling +
                                 ", %r1.b7654, %r2.b3210, %r3;"));
  }
  for (const auto selector : {"h0", "h1", "h10"})
    EXPECT_TRUE(family_accepts(std::string("vadd2.s32.u32.s32.add %r0.") +
                               selector + ", %r1.h32, %r2.h10, %r3;"));
  for (const auto source : {"vadd2.s32.s32.s32 %r0.h00, %r1, %r2, %r3;",
                            "vadd2.s32.s32.s32 %r0.h01, %r1, %r2, %r3;",
                            "vadd2.s32.s32.s32 %r0, %r1.h40, %r2, %r3;",
                            "vadd4.s32.s32.s32 %r0.b00, %r1, %r2, %r3;",
                            "vadd4.s32.s32.s32 %r0.b01, %r1, %r2, %r3;",
                            "vadd4.s32.s32.s32 %r0, %r1.b8765, %r2, %r3;",
                            "vavrg.s32.s32.s32 %r0, %r1, %r2;",
                            "varvg2.s32.s32.s32 %r0, %r1, %r2, %r3;"})
    EXPECT_FALSE(family_accepts(source)) << source;
  for (const auto source : {"vadd2.s32.s32.s32 %r0, %r1, %r2, %r3;",
                            "vset4.s32.u32.eq.add %r0.b0, %r1, %r2, %r3;"}) {
    EXPECT_TRUE(family_accepts(source, 30, {3, 0}));
    EXPECT_FALSE(family_accepts(source, 20, {3, 0}));
    EXPECT_FALSE(family_accepts(source, 30, {2, 0}));
    EXPECT_TRUE(family_accepts(source, 100));
  }
  auto value = family_resolve("vadd4.s32.u32.s32 %r0, %r1, %r2, %r3;");
  ASSERT_TRUE(value);
  auto& packed = dynamic_cast<Vadd4Packed&>(**value);
  EXPECT_FALSE(packed.b.value.selector);
  const auto effective = video_effective_selector(
      packed.b.value, Vadd4Packed::video_lanes, VideoOperandPosition::B);
  ASSERT_TRUE(effective);
  EXPECT_EQ(std::get<VideoByteSwizzle>(*effective).indices,
            (std::array<uint8_t, 4>{7, 6, 5, 4}));
  packed.a.value.selector =
      WithLocs<VideoSelector>{VideoByteSwizzle{{8, 0, 0, 0}}};
  EXPECT_FALSE(
      packed.check({.target = {.ptx_version = {9, 3}, .sm_version = 80}}));
}

/** Owned packed masks and accumulation controls are rechecked after mutation. */
TEST(VideoFamily, MutatedPackedAccumulationMasks) {
  for (const auto count : {2, 4}) {
    auto value = family_resolve(
        count == 2 ? "vadd2.s32.u32.s32.add %r0.h1, %r1, %r2, %r3;"
                   : "vadd4.s32.u32.s32.add %r0.b31, %r1, %r2, %r3;");
    ASSERT_TRUE(value);
    auto& destination = count == 2
                            ? dynamic_cast<Vadd2Packed&>(**value).dst.value
                            : dynamic_cast<Vadd4Packed&>(**value).dst.value;
    auto& saturate = count == 2
                         ? dynamic_cast<Vadd2Packed&>(**value).saturate.value
                         : dynamic_cast<Vadd4Packed&>(**value).saturate.value;
    const checker::Context context{
        .target = {.ptx_version = {9, 3}, .sm_version = 80}};
    ASSERT_TRUE((*value)->check(context));
    saturate = true;
    EXPECT_FALSE((*value)->check(context));
    saturate = false;
    EXPECT_TRUE((*value)->check(context));
    const auto original = destination.selector;
    destination.selector->value =
        count == 2 ? VideoSelector{VideoHalfMask{{1, 0}, 0}}
                   : VideoSelector{VideoByteMask{{3, 1, 0, 0}, 0}};
    EXPECT_FALSE((*value)->check(context));
    destination.selector->value =
        count == 2 ? VideoSelector{VideoHalfMask{{0, 1}, 2}}
                   : VideoSelector{VideoByteMask{{1, 3, 0, 0}, 2}};
    EXPECT_FALSE((*value)->check(context));
    destination.selector->value =
        count == 2 ? VideoSelector{VideoHalfSwizzle{{1, 0}}}
                   : VideoSelector{VideoByteSwizzle{{3, 2, 1, 0}}};
    EXPECT_FALSE((*value)->check(context));
    destination.selector = original;
    EXPECT_TRUE((*value)->check(context));
  }
}

/** Minus, selector and carrier token ranges remain independently owned/bound. */
TEST(VideoFamily, NegatedModuleCarrierReferenceRanges) {
  constexpr std::string_view source = R"ptx(.version 9.3
.target sm_80
.entry k() {
  .reg .b32 %r<4>;
  vmad.u32.s32.u32 %r0, -%r1.b3, %r2, %r3;
})ptx";
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  ASSERT_TRUE(ast);
  auto module = resolveModuleOnly(*ast);
  ASSERT_TRUE(module);
  ASSERT_TRUE(validateModule(*module));
  ASSERT_TRUE(validateModule(*ast, *module));
  auto& mad = dynamic_cast<VmadScalar&>(*module->functions[0].body[0]);
  auto& operand = mad.a.value;
  ASSERT_TRUE(operand.negated);
  ASSERT_TRUE(operand.minus_range);
  ASSERT_TRUE(operand.selector);
  ASSERT_FALSE(operand.value.locs.empty());
  ASSERT_FALSE(operand.selector->locs.empty());
  const auto identifier = operand.value.locs.front();
  const auto minus = *operand.minus_range;
  const auto selector = operand.selector->locs.front();
  EXPECT_EQ(minus.end.column, minus.start.column + 1);
  EXPECT_EQ(identifier.start.column, minus.end.column);
  EXPECT_EQ(identifier.end.column, identifier.start.column + 3);
  EXPECT_LT(identifier.end.column, selector.end.column);
  EXPECT_EQ(mad.a.locs.front().start, minus.start);
  EXPECT_EQ(mad.a.locs.front().end, selector.end);
  auto& carrier = std::get<ResolvedRegisterRef>(operand.value.value);
  carrier.declared_type = ScalarType::F32;
  const auto checked = validateModule(*module);
  ASSERT_FALSE(checked);
  EXPECT_TRUE(std::ranges::any_of(checked.error(), [&](const auto& diagnostic) {
    return diagnostic.range == identifier;
  }));
  EXPECT_EQ(operand.minus_range, minus);
  EXPECT_EQ(operand.selector->locs.front(), selector);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
