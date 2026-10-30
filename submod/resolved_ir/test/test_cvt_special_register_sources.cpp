#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/model/arithmetic/add.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/cvt.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve a conversion independently of transient parser storage. */
std::unique_ptr<Instruction> conversion(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseInstruction();
  if (!ast)
    return nullptr;
  auto value = resolveCvt(*ast);
  if (!value)
    return nullptr;
  return std::move(*value);
}

/** Current target used to isolate operand checking from instruction gates. */
checker::Context conversion_context() {
  return {.target = {.ptx_version = {9, 3}, .sm_version = 100}};
}

/** Integer, floating and bit carriers retain their intrinsic compatibility. */
TEST(CvtSpecialSources, ChecksSourceTypesAndWidths) {
  for (const auto source :
       {"cvt.u64.u32 %rd0, %laneid;", "cvt.u64.u32 %rd0, %tid.x;",
        "cvt.u64.u32 %rd0, %tid.y;", "cvt.u64.u32 %rd0, %tid.z;",
        "cvt.rn.f32.u32 %f0, %laneid;", "cvt.u64.s32 %rd0, %laneid;",
        "cvt.u32.u8 %r0, %laneid;", "cvt.u32.u16 %r0, %laneid;",
        "cvt.u64.u64 %rd0, %clock64;", "cvt.u32.u32 %r0, %clock64;",
        "cvt.rni.u32.f32 %r0, %envreg31;", "cvt.u32.u32 %r0, 7;",
        "cvt.u32.u32 %r0, %r1;"}) {
    SCOPED_TRACE(source);
    auto value = conversion(source);
    ASSERT_NE(value, nullptr);
    auto result = value->check(conversion_context());
    EXPECT_TRUE(result.has_value()) << result.error().front().message;
  }
  for (const auto source :
       {"cvt.rni.u32.f32 %r0, %laneid;", "cvt.u64.u64 %rd0, %laneid;",
        "cvt.u32.u32 %r0, %is_explicit_cluster;"}) {
    SCOPED_TRACE(source);
    auto value = conversion(source);
    ASSERT_NE(value, nullptr);
    EXPECT_FALSE(value->check(conversion_context()).has_value());
  }
}

/** Illegal topology and unknown intrinsics fail before becoming scalar values. */
TEST(CvtSpecialSources, RejectsNonScalarAndAddressSyntax) {
  for (const auto source :
       {"cvt.u32.u32 %r0, %tid;", "cvt.u32.u32 %r0, %tid.w;",
        "cvt.u32.u32 %r0, %laneid.x;", "cvt.u32.u32 %r0, %unknown;",
        "cvt.u32.u32 %laneid, %r0;", "cvt.u32.u32 %r0, [value];",
        "cvt.u32.u32 %r0, value;"}) {
    SCOPED_TRACE(source);
    EXPECT_EQ(conversion(source), nullptr);
  }
  PtxSyntaxParser parser("add.u32 %r0, %r1, %laneid;");
  auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast);
  EXPECT_FALSE(resolveAdd(*ast).has_value());
  EXPECT_EQ(conversion("cvt.pack.sat.u8.s32.b32 %r0, %laneid, %r1, %r2;"),
            nullptr);
}

/** Packed scalar and auxiliary sources retain their original immediate domain. */
TEST(CvtSpecialSources, ChecksPackedAndAuxiliarySources) {
  auto context = conversion_context();
  context.target.identity = base::parse_target_identity("sm_100a");
  const std::array<std::string_view, 1> capabilities{"cluster"};
  context.target.capabilities = capabilities;
  for (const auto source : {"cvt.rn.f16x2.e2m1x2 %r0, %envreg0;",
                            "cvt.rs.f16x2.f32 %r0, %f0, %f1, %laneid;"}) {
    SCOPED_TRACE(source);
    auto value = conversion(source);
    ASSERT_NE(value, nullptr);
    auto checked = value->check(context);
    EXPECT_TRUE(checked.has_value()) << checked.error().front().message;
  }
  auto predicate = conversion("cvt.rn.f16x2.e2m1x2 %r0, %is_explicit_cluster;");
  ASSERT_NE(predicate, nullptr);
  EXPECT_FALSE(predicate->check(context).has_value());
  EXPECT_EQ(conversion("cvt.rn.f16x2.e2m1x2 %r0, 1;"), nullptr);
  EXPECT_EQ(conversion("cvt.rs.f16x2.f32 %r0, %f0, %f1, 1;"), nullptr);
}

/** Historical use types override checking while intrinsic identity remains owned. */
TEST(CvtSpecialSources, PreservesHistoricalIntrinsicIdentity) {
  for (const auto source :
       {"%tid.x", "%ntid.y", "%ctaid.z", "%nctaid.x", "%gridid"}) {
    auto value = conversion(std::string("cvt.u32.u16 %r0, ") + source + ";");
    ASSERT_NE(value, nullptr);
    auto context = conversion_context();
    context.target.ptx_version = {1, 0};
    context.target.sm_version = 10;
    EXPECT_TRUE(value->check(context).has_value()) << source;
    const auto& typed = dynamic_cast<const CvtUnroundedBase&>(*value);
    const auto& intrinsic =
        std::get<ResolvedSpecialRegisterRef>(typed.src.value);
    EXPECT_EQ(base::metadata(intrinsic.id).element_type,
              intrinsic.id.kind == base::SpecialRegisterKind::GridId
                  ? ScalarType::U64
                  : ScalarType::U32);
  }
  auto value = conversion("cvt.u64.u32 %rd0, %gridid;");
  ASSERT_NE(value, nullptr);
  auto context = conversion_context();
  context.target.ptx_version = {1, 3};
  context.target.sm_version = 10;
  EXPECT_TRUE(value->check(context).has_value());
  context.target.ptx_version = {1, 2};
  EXPECT_FALSE(value->check(context).has_value());
}

/** Owned mutation checks reject forged intrinsic identities and selectors. */
TEST(CvtSpecialSources, RechecksOwnedIntrinsicMutations) {
  auto value = conversion("cvt.u64.u32 %rd0, %tid.x;");
  ASSERT_NE(value, nullptr);
  auto& typed = dynamic_cast<CvtUnroundedBase&>(*value);
  auto& special = std::get<ResolvedSpecialRegisterRef>(typed.src.value);
  const auto original = special;
  special.spelling = "presentation-only";
  EXPECT_TRUE(value->check(conversion_context()).has_value());
  for (const auto kind : {base::SpecialRegisterKind::Invalid,
                          static_cast<base::SpecialRegisterKind>(255)}) {
    special = original;
    special.id.kind = kind;
    EXPECT_FALSE(value->check(conversion_context()).has_value());
  }
  special = original;
  special.component.reset();
  EXPECT_FALSE(value->check(conversion_context()).has_value());
  special = original;
  special.component = static_cast<base::VectorComponent>(255);
  EXPECT_FALSE(value->check(conversion_context()).has_value());
  special = original;
  special.id = {base::SpecialRegisterKind::LaneId, 0};
  EXPECT_FALSE(value->check(conversion_context()).has_value());
  for (const auto id :
       {base::SpecialRegisterId{base::SpecialRegisterKind::Environment, 32},
        base::SpecialRegisterId{base::SpecialRegisterKind::PerformanceMonitor,
                                8},
        base::SpecialRegisterId{base::SpecialRegisterKind::PerformanceMonitor64,
                                8},
        base::SpecialRegisterId{base::SpecialRegisterKind::ReservedSmemOffset,
                                2},
        base::SpecialRegisterId{base::SpecialRegisterKind::LaneId, 1}}) {
    special = original;
    special.component.reset();
    special.id = id;
    EXPECT_FALSE(value->check(conversion_context()).has_value());
  }
  EXPECT_FALSE(typed.src.locs.empty());
}

/** Intrinsic metadata supplies PTX, architecture and capability gates. */
TEST(CvtSpecialSources, ChecksIntrinsicAvailability) {
  for (const auto source :
       {"%laneid", "%pm7", "%envreg31", "%clock_hi", "%cluster_ctarank"}) {
    auto value = conversion(std::string("cvt.u64.u32 %rd0, ") + source + ";");
    ASSERT_NE(value, nullptr);
    auto context = conversion_context();
    context.target.ptx_version = {1, 0};
    context.target.sm_version = 10;
    EXPECT_FALSE(value->check(context).has_value());
  }
  auto value = conversion("cvt.u64.u32 %rd0, %cluster_ctarank;");
  ASSERT_NE(value, nullptr);
  auto context = conversion_context();
  EXPECT_FALSE(value->check(context).has_value());
  const std::array<std::string_view, 1> capabilities{"cluster"};
  context.target.capabilities = capabilities;
  EXPECT_TRUE(value->check(context).has_value());
  context.target.sm_version = 80;
  EXPECT_FALSE(value->check(context).has_value());
}

/** Module validation and observers retain source-independent conversion inputs. */
TEST(CvtSpecialSources, OwnsModuleRegisterReferences) {
  std::optional<ResolvedModule> owned;
  {
    auto ast = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_80
.entry k() {
 .reg .u32 %r<2>;
 .reg .u64 %rd0;
 cvt.u64.u32 %rd0, %tid.x;
 cvt.u32.u32 %r0, %r1;
 cvt.u32.u32 %r1, 7;
})ptx");
    ASSERT_TRUE(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result) << result.error().front().message;
    owned = std::move(*result);
  }
  EXPECT_TRUE(validateModule(*owned).has_value());
  auto& ordinary =
      dynamic_cast<CvtUnroundedBase&>(*owned->functions.front().body[1]);
  auto& reg = std::get<ResolvedRegisterRef>(ordinary.src.value);
  const auto original = reg;
  reg.symbol_id = binding::SymbolId{999999};
  EXPECT_FALSE(validateModule(*owned).has_value());
  reg = original;
  reg.register_class = ResolvedRegisterClass::Predicate;
  EXPECT_FALSE(ordinary.check(conversion_context()).has_value());
  reg = original;
  reg.vector_width = 2;
  EXPECT_FALSE(ordinary.check(conversion_context()).has_value());
  reg = original;
  reg.declared_type.reset();
  EXPECT_FALSE(ordinary.check(conversion_context()).has_value());
  reg = original;
  reg.declared_type = ScalarType::F32;
  EXPECT_FALSE(validateModule(*owned).has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
