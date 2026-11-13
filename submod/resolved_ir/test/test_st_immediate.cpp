#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <optional>
#include <string>
#include <variant>

#include <ptx_frontend/resolved_ir/model/data_movement/st.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Capabilities supplied by the recognized SM100 profile for shared cluster ST. */
constexpr std::array<std::string_view, 1> cluster_capabilities{"cluster"};

/** Complete target context for standalone scalar-store compatibility checks. */
const checker::Context target{.target = {.ptx_version = {9, 3},
                                         .sm_version = 100,
                                         .capabilities = cluster_capabilities}};

/** Resolve a scalar shared store while its parser-owned source remains live. */
auto scalar_store(std::string_view type, std::string_view value) {
  const auto ast = test_helpers::parseInstruction(
      "st.shared." + std::string(type) + " [%r0], " + std::string(value) + ";");
  return resolveSt(*ast);
}

/** Integer values retain evaluated source bits and the selected use-width bits. */
TEST(StImmediate, PreservesIntegerSourceAndNarrowedBits) {
  for (const auto type : {"b8", "b16", "b32", "b64", "u8", "u16", "u32", "u64",
                          "s8", "s16", "s32", "s64"}) {
    SCOPED_TRACE(type);
    for (const auto value : {"0", "17", "-1", "4294967296", "(1+2)"}) {
      SCOPED_TRACE(value);
      auto result = scalar_store(type, value);
      ASSERT_TRUE(result) << result.error().message;
      const auto& store = dynamic_cast<const StExplicitScalar&>(**result);
      const auto* immediate = std::get_if<ResolvedImmediate>(&store.src.value);
      ASSERT_NE(immediate, nullptr);
      EXPECT_EQ(immediate->type, store.type.value);
      ASSERT_TRUE(immediate->integer_source_bits);
      const auto width = base::scalar_size_of(immediate->type) * 8;
      const uint64_t mask =
          width == 64 ? UINT64_MAX : (uint64_t{1} << width) - 1;
      EXPECT_EQ(immediate->bits, *immediate->integer_source_bits & mask);
      EXPECT_EQ(immediate->is_negative, std::string_view(value) == "-1");
      ASSERT_EQ(store.src.locs.size(), 1u);
      EXPECT_TRUE((*result)->check(target));
    }
  }
  EXPECT_FALSE(scalar_store("b64", "18446744073709551616"));
  EXPECT_FALSE(scalar_store("b128", "0"));
  auto reg = scalar_store("b128", "%q0");
  ASSERT_TRUE(reg);
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(
      dynamic_cast<const StExplicitScalar&>(**reg).src.value));
  EXPECT_TRUE((*reg)->check(target));
}

/** Numeric floats convert precision; matching exact words preserve payloads. */
TEST(StImmediate, PreservesFloatingWordsAndRejectsOtherOrigins) {
  /** One source precision and its expected stored scalar word. */
  struct Case {
    std::string_view type;
    std::string_view value;
    uint64_t bits;
  };
  constexpr Case cases[] = {{"f32", "1.5", 0x3fc00000},
                            {"f32", "-1.5", 0xbfc00000},
                            {"f32", "0d3ff8000000000000", 0x3fc00000},
                            {"f64", "0f3fc00000", 0x3ff8000000000000},
                            {"f64", "1.5", 0x3ff8000000000000},
                            {"b32", "0f7fc01234", 0x7fc01234},
                            {"b64", "0d7ff8000000001234", 0x7ff8000000001234},
                            {"b64", "1.5", 0x3ff8000000000000}};
  for (const auto& row : cases) {
    SCOPED_TRACE(row.type);
    SCOPED_TRACE(row.value);
    auto result = scalar_store(row.type, row.value);
    ASSERT_TRUE(result) << result.error().message;
    const auto& store = dynamic_cast<const StExplicitScalar&>(**result);
    const auto& immediate = std::get<ResolvedImmediate>(store.src.value);
    EXPECT_EQ(immediate.bits, row.bits);
    EXPECT_FALSE(immediate.integer_source_bits);
    EXPECT_FALSE(immediate.is_negative);
    EXPECT_TRUE((*result)->check(target));
  }
  for (const auto type : {"f32", "f64"})
    EXPECT_FALSE(scalar_store(type, "1"));
  for (const auto type : {"b8", "b16", "u32", "s64"})
    EXPECT_FALSE(scalar_store(type, "1.5"));
  EXPECT_FALSE(scalar_store("b32", "1.5"));
  EXPECT_FALSE(scalar_store("b32", "0d3ff8000000000000"));
  EXPECT_FALSE(scalar_store("b64", "0f3fc00000"));
}

/** Scalar source widening preserves the surrounding qualifiers and exclusions. */
TEST(StImmediate, CoversSpacesQualifiersAndRetainsRegisterOnlyRoles) {
  for (const auto source :
       {"st.b32 [%r0], 17;", "st.global.b32 [%r0], 17;",
        "st.local.b32 [%r0], 17;", "st.shared::cta.b32 [%r0], 17;",
        "st.shared::cluster.b32 [%r0], 17;",
        "st.volatile.shared.b32 [%r0], 17;",
        "st.release.gpu.global.b32 [%r0], 17;",
        "st.relaxed.cta.shared.b32 [%r0], 17;", "st.global.cg.b32 [%r0], 17;",
        "st.global.L1::evict_last.b32 [%r0], 17;",
        "st.L1::evict_last.b32 [%r0], 17;",
        "st.global.L2::cache_hint.b32 [%r0], 17, %policy0;",
        "st.L2::cache_hint.b32 [%r0], 17, %policy0;"}) {
    SCOPED_TRACE(source);
    const auto ast = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
    auto result = resolveSt(*ast);
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_TRUE((*result)->check(target));
  }
  for (const auto source : {"st.global.v2.b32 [%r0], {0, 1};",
                            "st.global.L2::cache_hint.b32 [%r0], 17, 0;",
                            "st.shared.b32 [%r0], %tid.x;"}) {
    SCOPED_TRACE(source);
    const auto ast = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
    auto result = resolveSt(*ast);
    EXPECT_TRUE(!result || !(*result)->check(target));
  }
}

/** Owned validation rejects payloads that the source converter cannot produce. */
TEST(StImmediate, RejectsForgedWidthAndFloatingOrigin) {
  auto result = scalar_store("b32", "0");
  ASSERT_TRUE(result);
  auto& store = dynamic_cast<StExplicitScalar&>(**result);
  const auto replace = [&](ScalarType type, ResolvedImmediate value) {
    store.type.value = type;
    store.src.value = value;
    EXPECT_FALSE(store.check(target));
  };
  store.type.value = ScalarType::U8;
  store.src.value = ResolvedImmediate{.bits = 255, .type = ScalarType::U8};
  EXPECT_TRUE(store.check(target));
  replace(ScalarType::U8, {.bits = 256, .type = ScalarType::U8});
  replace(ScalarType::B128, {.bits = 0, .type = ScalarType::B128});
  replace(ScalarType::B32,
          {.bits = uint64_t{1} << 32, .type = ScalarType::B32});
  replace(ScalarType::F32,
          {.bits = 0, .type = ScalarType::F32, .integer_source_bits = 0});
  replace(ScalarType::F64,
          {.bits = 0, .type = ScalarType::F64, .is_negative = true});
  replace(ScalarType::F32,
          {.bits = uint64_t{1} << 32, .type = ScalarType::F32});
  replace(ScalarType::U8,
          {.bits = 0, .type = ScalarType::U8, .integer_source_bits = 1});
}

/** Source identity and owned scalar immediates survive independent copies. */
TEST(StImmediate, OwnsSourcesAfterReleaseAndPreservesAstCorrespondence) {
  std::optional<ResolvedModule> owned;
  {
    std::string source = R"ptx(.version 9.3
.target sm_100
.address_size 64
.visible .entry kernel() {
.shared .align 4 .b32 slot;
.reg .b32 %v;
st.shared.b32 [slot], 0;
st.shared.b32 [slot], %v;
})ptx";
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result) << result.error().front().message;
    EXPECT_TRUE(validateModule(*ast, *result));
    auto& store =
        dynamic_cast<StExplicitScalar&>(*result->functions[0].body[0]);
    const auto old = store.src.value;
    store.src.value = ResolvedImmediate{
        .bits = 1, .type = ScalarType::B32, .integer_source_bits = 1};
    EXPECT_TRUE(validateModule(*ast, *result));
    EXPECT_TRUE(validateModule(*result));
    std::string changed_source = source;
    changed_source.replace(changed_source.find("[slot], 0"), 9, "[slot], 2");
    const auto changed_ast = test_helpers::parseModule(changed_source);
    ASSERT_MODULE_PARSE_SUCCEEDS(changed_ast);
    EXPECT_FALSE(validateModule(*changed_ast, *result));
    store.src.value = old;
    owned.emplace(std::move(*result));
  }
  ASSERT_TRUE(validateModule(*owned));
  ResolvedModule copy = *owned;
  ResolvedModule assigned;
  assigned = copy;
  EXPECT_TRUE(validateModule(copy));
  EXPECT_TRUE(validateModule(assigned));
  const auto& store = *assigned.functions[0].body[0];
  EXPECT_TRUE(store.clone()->check(target));
  auto& original =
      dynamic_cast<StExplicitScalar&>(*owned->functions[0].body[0]);
  std::get<ResolvedImmediate>(original.src.value).bits = 1;
  EXPECT_FALSE(validateModule(*owned));
  EXPECT_TRUE(validateModule(copy));
  auto& register_store =
      dynamic_cast<StExplicitScalar&>(*assigned.functions[0].body[1]);
  auto& reference = std::get<ResolvedRegisterRef>(register_store.src.value);
  ASSERT_TRUE(reference.symbol_id);
  EXPECT_EQ(reference.declared_type, ScalarType::B32);
  reference.declared_type = ScalarType::F32;
  EXPECT_FALSE(validateModule(assigned));
}

/** Existing non-store integer and floating immediate producers remain valid. */
TEST(StImmediate, KeepsOtherNumericImmediateRoles) {
  for (const auto source :
       {"add.u32 %r0, %r1, 4294967296;", "add.f32 %f0, %f1, 1.5;",
        "mov.b32 %r0, 0f7fc01234;", "mov.b64 %r0, 0d7ff8000000001234;"}) {
    SCOPED_TRACE(source);
    const auto ast = test_helpers::parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
    auto result = resolveInstruction(*ast);
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_TRUE((*result)->check(target));
  }
}

/** Parameter return and adjacent call staging accept immediate data unchanged. */
TEST(StImmediate, PreservesParameterAndCallStagingRules) {
  for (const auto space : {"param", "param::func"}) {
    const std::string source =
        ".version 9.3\n.target sm_100\n.address_size 64\n"
        ".func (.param .b32 result) give(.param .b32 input) { st." +
        std::string(space) +
        ".b32 [result], 17; ret; }\n"
        ".entry kernel() { .param .b32 arg; .param .b32 result; st." +
        space + ".b32 [arg], 0; call (result), give, (arg); ret; }";
    SCOPED_TRACE(space);
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    const auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result) << result.error().front().message;
    EXPECT_TRUE(validateModule(*result));
  }
  const auto ast = test_helpers::parseModule(
      ".version 9.3\n.target sm_100\n.entry kernel(.param .b32 input) { "
      "st.param.b32 [input], 0; }");
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  EXPECT_FALSE(resolveAndValidateModule(*ast));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
