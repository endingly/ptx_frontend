#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using test_helpers::parseModule;

/** Require a complete createpolicy module to resolve and validate. */
void expect_valid_createpolicy(std::string_view source) {
  const auto ast = parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  const auto resolved = resolveModule(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto checked = validateModule(*resolved);
  ASSERT_TRUE(checked.has_value()) << checked.error().front().message;
}

/** Require an invalid createpolicy form to fail parsing, resolution, or checking. */
void expect_invalid_createpolicy(std::string_view source) {
  const auto ast = parseModule(source);
  if (!ast)
    return;
  const auto resolved = resolveModule(*ast);
  if (resolved)
    EXPECT_FALSE(validateModule(*resolved));
}

/** Every documented topology retains source-expressed priority and fraction. */
TEST(CreatepolicyCompleteness, ResolvesDocumentedTopologies) {
  const auto ast = parseModule(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.global .align 128 .b8 g[512];
.visible .entry kernel() {
  .reg .b64 %b<9>;
  .reg .f32 %f0;
  .reg .b32 %r<2>;
  .reg .u64 %rd0;
  createpolicy.fractional.L2::evict_last.b64 %b0, 0.25;
  createpolicy.fractional.L2::evict_normal.b64 %b1;
  createpolicy.fractional.L2::evict_first.L2::evict_unchanged.b64 %b2, %f0;
  createpolicy.fractional.L2::evict_unchanged.L2::evict_first.b64 %b3;
  createpolicy.range.L2::evict_last.b64 %b4, [g], 256, 256;
  createpolicy.range.L2::evict_first.L2::evict_unchanged.b64
      %b5, [g], %r0, %r1;
  createpolicy.range.global.L2::evict_normal.b64 %b6, [g], 128, %r0;
  createpolicy.range.global.L2::evict_unchanged.L2::evict_first.b64
      %b7, [g], 128, 256;
  createpolicy.cvt.L2.b64 %b8, %rd0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  const auto resolved = resolveModule(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_TRUE(validateModule(*resolved));
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 9u);
  const auto& first = std::get<Createpolicy>(body[0]);
  const auto& fractional =
      std::get<Createpolicy::FractionalL2B64>(first.variant);
  EXPECT_EQ(fractional.primary_priority.value, EvictionPriority::EvictLast);
  EXPECT_TRUE(std::holds_alternative<
              Createpolicy::FractionalL2B64::WithFractionOperands>(
      fractional.operands));
  EXPECT_TRUE(
      std::holds_alternative<Createpolicy::FractionalL2B64::DefaultOperands>(
          std::get<Createpolicy::FractionalL2B64>(
              std::get<Createpolicy>(body[1]).variant)
              .operands));
  const auto& secondary = std::get<Createpolicy::FractionalL2SecondaryB64>(
      std::get<Createpolicy>(body[2]).variant);
  EXPECT_EQ(secondary.secondary_priority.value,
            EvictionPriority::EvictUnchanged);
  EXPECT_TRUE(std::holds_alternative<
              Createpolicy::FractionalL2SecondaryB64::DefaultOperands>(
      std::get<Createpolicy::FractionalL2SecondaryB64>(
          std::get<Createpolicy>(body[3]).variant)
          .operands));
  EXPECT_TRUE(std::holds_alternative<Createpolicy::RangeGenericL2B64>(
      std::get<Createpolicy>(body[4]).variant));
  EXPECT_TRUE(std::holds_alternative<Createpolicy::RangeGenericL2SecondaryB64>(
      std::get<Createpolicy>(body[5]).variant));
  EXPECT_TRUE(std::holds_alternative<Createpolicy::RangeGlobalL2B64>(
      std::get<Createpolicy>(body[6]).variant));
  EXPECT_TRUE(std::holds_alternative<Createpolicy::RangeGlobalL2SecondaryB64>(
      std::get<Createpolicy>(body[7]).variant));
  EXPECT_TRUE(std::holds_alternative<Createpolicy::CvtL2B64>(
      std::get<Createpolicy>(body[8]).variant));
}

/** All createpolicy topologies start at PTX 7.4 and SM 80. */
TEST(CreatepolicyCompleteness, ChecksTargetFloors) {
  for (const auto instruction : {
           "createpolicy.fractional.L2::evict_last.b64 %b0, 0.5;",
           "createpolicy.range.L2::evict_last.b64 %b0, [%rd0], 128, 256;",
           "createpolicy.cvt.L2.b64 %b0, %b1;",
       }) {
    for (const auto target :
         {".version 7.3\n.target sm_80\n", ".version 7.4\n.target sm_75\n"}) {
      SCOPED_TRACE(instruction);
      const std::string source =
          std::string(target) + ".address_size 64\n" +
          ".visible .entry kernel() {\n.reg .b64 %b<2>; .reg .u64 %rd0;\n" +
          instruction + "\n}";
      expect_invalid_createpolicy(source);
    }
  }
}

/** Immediate fractions use numeric f32 bounds, including nonfinite values. */
TEST(CreatepolicyCompleteness, ChecksFractionBounds) {
  for (const auto fraction :
       {"0.0", "-0.0", "-0.25", "1.25", "0f7f800000", "0f7fc00000"}) {
    SCOPED_TRACE(fraction);
    const std::string source =
        std::string(".version 9.3\n.target sm_80\n.address_size 64\n") +
        ".visible .entry kernel() {\n.reg .b64 %b0;\n" +
        "createpolicy.fractional.L2::evict_last.b64 %b0, " + fraction + ";\n}";
    const auto ast = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    const auto resolved = resolveModuleOnly(*ast);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    const auto checked = checker::check(
        std::get<Createpolicy>(resolved->functions.front().body.front()),
        checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 80}});
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::ImmediateValueMismatch);
  }
  expect_valid_createpolicy(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.visible .entry kernel() {
  .reg .b64 %b0;
  createpolicy.fractional.L2::evict_last.b64 %b0, 1.0;
}
)ptx");
}

/** Static range order is checked while register-dependent order stays dynamic. */
TEST(CreatepolicyCompleteness, ChecksRangeSizesAndAddress) {
  const auto reversed = parseModule(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.global .b8 g[512];
.visible .entry kernel() {
  .reg .b64 %b0;
  createpolicy.range.L2::evict_last.b64 %b0, [g], 512, 256;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(reversed);
  const auto resolved = resolveModuleOnly(*reversed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto checked = checker::check(
      std::get<Createpolicy>(resolved->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 80}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::ImmediateValueMismatch);

  for (const auto instruction : {
           "createpolicy.range.L2::evict_last.b64 %b0, [local_value], 128, "
           "256;",
           "createpolicy.range.global.L2::evict_last.b64 %b0, [local_value], "
           "128, 256;",
       }) {
    const std::string source =
        std::string(".version 9.3\n.target sm_80\n.address_size 64\n") +
        ".visible .entry kernel() {\n.local .b8 local_value[512];\n"
        ".reg .b64 %b0;\n" +
        instruction + "\n}";
    expect_invalid_createpolicy(source);
  }
  expect_valid_createpolicy(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.visible .entry kernel() {
  .reg .b64 %b0;
  .reg .u64 %rd0;
  .reg .u32 %r0;
  createpolicy.range.L2::evict_last.b64 %b0, [%rd0], 128, %r0;
}
)ptx");
}

/** Range literals must fit their 32-bit operand before size comparison. */
TEST(CreatepolicyCompleteness, ChecksRangeLiteralWidth) {
  for (const auto sizes : {
           "4294967296, 4294967296",
           "4294967297, 4294967297",
           "-4294967296, -4294967296",
       }) {
    SCOPED_TRACE(sizes);
    const std::string source =
        std::string(".version 9.3\n.target sm_80\n.address_size 64\n") +
        ".global .b8 g[512];\n.visible .entry kernel() {\n"
        ".reg .b64 %b0;\ncreatepolicy.range.L2::evict_last.b64 "
        "%b0, [g], " +
        sizes + ";\n}";
    const auto ast = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveModuleOnly(*ast));
  }
  expect_valid_createpolicy(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.global .b8 g[512];
.visible .entry kernel() {
  .reg .b64 %b0;
  createpolicy.range.L2::evict_last.b64 %b0, [g], 4294967295, 4294967295;
  createpolicy.range.L2::evict_last.b64 %b0, [g], -1, -1;
}
)ptx");
}

/** Unsupported priority and cache-level spellings stay outside PTX 9.3. */
TEST(CreatepolicyCompleteness, RejectsUnsupportedSyntax) {
  for (const auto instruction : {
           "createpolicy.fractional.L2::evict_last.L2::evict_normal.b64 %b0, "
           "0.5;",
           "createpolicy.fractional.L1::evict_last.b64 %b0, 0.5;",
           "createpolicy.range.fabric.L2::evict_last.b64 %b0, [%rd0], 128, "
           "256;",
           "createpolicy.cvt.L1.b64 %b0, %b1;",
           "createpolicy.fractional.L2::evict_last.u64 %b0, 0.5;",
           "createpolicy.fractional.L2::evict_last.b64 %b0, %r0;",
           "createpolicy.range.L2::evict_last.b64 %b0, [%rd0], %rd0, 256;",
           "createpolicy.cvt.L2.b64 %b0, %r0;",
       }) {
    SCOPED_TRACE(instruction);
    const std::string source =
        std::string(".version 9.3\n.target sm_80\n.address_size 64\n") +
        ".visible .entry kernel() {\n.reg .b64 %b<2>; .reg .u64 %rd0; "
        ".reg .u32 %r0;\n" +
        instruction + "\n}";
    expect_invalid_createpolicy(source);
  }
}

/** Owned fraction and size values remain checked after their AST is destroyed. */
TEST(CreatepolicyCompleteness, RevalidatesOwnedImmediateMutation) {
  std::optional<ResolvedModule> owned;
  {
    const std::string source = R"ptx(
.version 9.3
.target sm_80
.address_size 64
.global .b8 g[512];
.visible .entry kernel() {
  .reg .b64 %b<2>;
  createpolicy.fractional.L2::evict_last.b64 %b0, 0.5;
  createpolicy.range.L2::evict_last.b64 %b1, [g], 128, 256;
}
)ptx";
    const auto ast = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto resolved = resolveModuleOnly(*ast);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned.emplace(std::move(*resolved));
  }
  ASSERT_TRUE(owned.has_value());
  ASSERT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext));
  auto& fractional = std::get<Createpolicy::FractionalL2B64>(
      std::get<Createpolicy>(owned->functions.front().body[0]).variant);
  auto& fraction = std::get<ResolvedImmediate>(
      std::get<Createpolicy::FractionalL2B64::WithFractionOperands>(
          fractional.operands)
          .fraction.value);
  ASSERT_EQ(fraction.bits, 0x3f000000u);
  fraction.bits = 0x7fc00000u;
  const auto invalid_fraction =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid_fraction.has_value());
  fraction.bits = 0x3f000000u;

  auto& range = std::get<Createpolicy::RangeGenericL2B64>(
      std::get<Createpolicy>(owned->functions.front().body[1]).variant);
  auto& primary = std::get<ResolvedImmediate>(range.primary_size.value);
  ASSERT_EQ(primary.bits, 128u);
  primary.bits = 512;
  const auto invalid_range =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  EXPECT_FALSE(invalid_range.has_value());
}

/** Owned range sizes are bounded independently before dynamic comparison. */
TEST(CreatepolicyCompleteness, RevalidatesEachOwnedRangeSizeWithoutAst) {
  std::optional<ResolvedModule> owned;
  {
    const auto ast = parseModule(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.global .b8 g[512];
.visible .entry kernel() {
  .reg .b64 %b0;
  .reg .u32 %r0;
  createpolicy.range.L2::evict_last.b64 %b0, [g], 128, 256;
  createpolicy.range.L2::evict_last.b64 %b0, [g], %r0, 256;
  createpolicy.range.L2::evict_last.b64 %b0, [g], 128, %r0;
}
)ptx");
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto resolved = resolveModuleOnly(*ast);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned.emplace(std::move(*resolved));
  }
  ASSERT_TRUE(owned.has_value());
  ASSERT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext));
  auto& body = owned->functions.front().body;
  ASSERT_EQ(body.size(), 3u);
  auto& static_range = std::get<Createpolicy::RangeGenericL2B64>(
      std::get<Createpolicy>(body[0]).variant);
  const auto& dynamic_primary = std::get<Createpolicy::RangeGenericL2B64>(
      std::get<Createpolicy>(body[1]).variant);
  const auto& dynamic_total = std::get<Createpolicy::RangeGenericL2B64>(
      std::get<Createpolicy>(body[2]).variant);
  auto& primary = std::get<ResolvedImmediate>(static_range.primary_size.value);
  auto& total = std::get<ResolvedImmediate>(static_range.total_size.value);
  const auto original_primary = primary;
  const auto original_total = total;
  const auto register_primary = dynamic_primary.primary_size.value;
  const auto register_total = dynamic_total.total_size.value;
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 80}};
  const auto check = [&]() {
    return checker::check(std::get<Createpolicy>(body[0]), context);
  };
  const auto expect_rejected_at = [&](const SourceRange& expected) {
    const auto result = check();
    ASSERT_FALSE(result.has_value());
    ASSERT_FALSE(result.error().empty());
    EXPECT_EQ(result.error().front().range, expected);
  };
  ASSERT_FALSE(static_range.primary_size.locs.empty());
  ASSERT_FALSE(static_range.total_size.locs.empty());
  const auto primary_range = static_range.primary_size.locs.front();
  const auto total_range = static_range.total_size.locs.front();

  primary.bits = static_cast<uint64_t>(UINT32_MAX) + 1;
  expect_rejected_at(primary_range);
  primary = original_primary;
  total.bits = static_cast<uint64_t>(UINT32_MAX) + 1;
  expect_rejected_at(total_range);
  total = original_total;

  primary.bits = static_cast<uint64_t>(UINT32_MAX) + 1;
  total.bits = static_cast<uint64_t>(UINT32_MAX) + 2;
  expect_rejected_at(primary_range);
  primary = original_primary;
  total = original_total;

  static_range.primary_size.value = register_primary;
  std::get<ResolvedImmediate>(static_range.total_size.value).bits =
      static_cast<uint64_t>(UINT32_MAX) + 1;
  expect_rejected_at(total_range);
  static_range.primary_size.value = original_primary;
  static_range.total_size.value = register_total;
  std::get<ResolvedImmediate>(static_range.primary_size.value).bits =
      static_cast<uint64_t>(UINT32_MAX) + 1;
  expect_rejected_at(primary_range);
  static_range.total_size.value = original_total;
  std::get<ResolvedImmediate>(static_range.primary_size.value).type =
      ScalarType::U64;
  expect_rejected_at(primary_range);
  static_range.primary_size.value = original_primary;
  std::get<ResolvedImmediate>(static_range.total_size.value).type =
      ScalarType::U64;
  expect_rejected_at(total_range);
  static_range.total_size.value = original_total;
  EXPECT_TRUE(check());
  EXPECT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext));
}

/** A malformed projected immediate cannot bypass the range-size rule. */
TEST(CreatepolicyCompleteness, RejectsMissingProjectedRangeBits) {
  const SourceRange primary_range{{1, 1}, {1, 4}};
  const SourceRange total_range{{1, 6}, {1, 9}};
  const std::array primary_locations{primary_range};
  const std::array total_locations{total_range};
  std::array<checker::OperandView, 2> sizes{{
      {.field_id = "primary_size",
       .actual_shape = checker::OperandShape::Immediate,
       .immediate_type = ScalarType::U32,
       .locations = primary_locations},
      {.field_id = "total_size",
       .actual_shape = checker::OperandShape::Register,
       .locations = total_locations},
  }};
  auto checked = checker::check_createpolicy_rule(sizes, {});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().range, primary_range);
  sizes[0].actual_shape = checker::OperandShape::Register;
  sizes[1].actual_shape = checker::OperandShape::Immediate;
  sizes[1].immediate_type = ScalarType::U32;
  checked = checker::check_createpolicy_rule(sizes, {});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().range, total_range);
  sizes[1].immediate_bits = 256;
  EXPECT_TRUE(checker::check_createpolicy_rule(sizes, {}));
  sizes[1].immediate_bits = static_cast<uint64_t>(UINT32_MAX) + 1;
  checked = checker::check_createpolicy_rule(sizes, {});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().range, total_range);
  sizes[1].immediate_bits = 256;
  sizes[1].immediate_type = ScalarType::U64;
  checked = checker::check_createpolicy_rule(sizes, {});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().range, total_range);
  sizes[1].immediate_type = ScalarType::U32;
  sizes[1].actual_shape = checker::OperandShape::Address;
  checked = checker::check_createpolicy_rule(sizes, {});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().range, total_range);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
