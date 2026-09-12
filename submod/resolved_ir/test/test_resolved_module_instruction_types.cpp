#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using test_helpers::parseModule;

const Add::IntegerNoSat& resolvedIntegerAdd(
    const ResolvedInstruction& instruction) {
  return std::get<Add::IntegerNoSat>(std::get<Add>(instruction).variant);
}

const Mov::Scalar::ScalarOperands& scalarMovOperands(const Mov::Scalar& mov) {
  return std::get<Mov::Scalar::ScalarOperands>(mov.operands);
}

const Mov::Scalar::ScalarOperands& scalarMovOperands(const Mov& mov) {
  return scalarMovOperands(std::get<Mov::Scalar>(mov.variant));
}

const Mov::Scalar::PackOperands& packMovOperands(const Mov& mov) {
  return std::get<Mov::Scalar::PackOperands>(
      std::get<Mov::Scalar>(mov.variant).operands);
}

Mov::Scalar::PackOperands& packMovOperands(Mov& mov) {
  return std::get<Mov::Scalar::PackOperands>(
      std::get<Mov::Scalar>(mov.variant).operands);
}

const Mov::Scalar::UnpackOperands& unpackMovOperands(const Mov& mov) {
  return std::get<Mov::Scalar::UnpackOperands>(
      std::get<Mov::Scalar>(mov.variant).operands);
}

TEST(ResolvedModule, ChecksAndB32RegisterCompatibilityAndWidth) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %u;
  .reg .s32 %s;
  .reg .b32 %b;
  and.b32 %b, %u, %s;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& valid_ast = *parsed_module_1;
  const auto valid = resolveModule(valid_ast);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto valid_check =
      checker::check(std::get<And>(valid->functions.front().body.front()),
                     checker::Context{
                         .target = {.ptx_version = {1, 0}, .sm_version = 0},
                         .instruction_range = valid_ast.range,
                     });
  EXPECT_TRUE(valid_check.has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b;
  .reg .u16 %h;
  and.b32 %b, %h, %b;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto& invalid_ast = *parsed_module_2;
  const auto invalid = resolveModule(invalid_ast);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& invalid_and =
      std::get<And>(invalid->functions.front().body.front());
  const auto& invalid_variant = std::get<And::B32>(invalid_and.variant);
  const auto invalid_check = checker::check(
      invalid_and, checker::Context{
                       .target = {.ptx_version = {1, 0}, .sm_version = 0},
                       .instruction_range = invalid_ast.range,
                   });
  ASSERT_FALSE(invalid_check.has_value());
  ASSERT_EQ(invalid_check.error().size(), 1u);
  EXPECT_EQ(invalid_check.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(invalid_check.error().front().range,
            invalid_variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksOrB32RegisterCompatibilityAndWidth) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %u;
  .reg .s32 %s;
  .reg .b32 %b;
  or.b32 %b, %u, %s;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto& valid_ast = *parsed_module_1;
  const auto valid = resolveModule(valid_ast);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto valid_check =
      checker::check(std::get<Or>(valid->functions.front().body.front()),
                     checker::Context{
                         .target = {.ptx_version = {1, 0}, .sm_version = 0},
                         .instruction_range = valid_ast.range,
                     });
  EXPECT_TRUE(valid_check.has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b;
  .reg .u16 %h;
  or.b32 %b, %h, %b;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto& invalid_ast = *parsed_module_2;
  const auto invalid = resolveModule(invalid_ast);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& invalid_or =
      std::get<Or>(invalid->functions.front().body.front());
  const auto& invalid_variant = std::get<Or::B32>(invalid_or.variant);
  const auto invalid_check = checker::check(
      invalid_or, checker::Context{
                      .target = {.ptx_version = {1, 0}, .sm_version = 0},
                      .instruction_range = invalid_ast.range,
                  });
  ASSERT_FALSE(invalid_check.has_value());
  ASSERT_EQ(invalid_check.error().size(), 1u);
  EXPECT_EQ(invalid_check.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(invalid_check.error().front().range,
            invalid_variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksXorB32RegisterCompatibilityAndWidth) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() { .reg .u32 %u; .reg .s32 %s; .reg .b32 %b; xor.b32 %b, %u, %s; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(
      checker::check(
          std::get<Xor>(valid->functions.front().body.front()),
          checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}})
          .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .b32 %b; .reg .u16 %h; xor.b32 %b, %h, %b; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction =
      std::get<Xor>(invalid->functions.front().body.front());
  const auto& variant = std::get<Xor::B32>(instruction.variant);
  const auto checked = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(checked.has_value());
  ASSERT_EQ(checked.error().size(), 1u);
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksNotB32RegisterCompatibilityAndWidth) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() { .reg .u32 %u; .reg .b32 %b; not.b32 %b, %u; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(
      checker::check(
          std::get<Not>(valid->functions.front().body.front()),
          checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}})
          .has_value());
  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .b32 %b; .reg .u16 %h; not.b32 %b, %h; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction =
      std::get<Not>(invalid->functions.front().body.front());
  const auto& variant = std::get<Not::B32>(instruction.variant);
  const auto checked = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(checked.has_value());
  ASSERT_EQ(checked.error().size(), 1u);
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.src.locs.front());
}

TEST(ResolvedModule, ChecksShlB32DataAndAmountWidths) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() { .reg .u32 %u, %amount; .reg .b32 %b; shl.b32 %b, %u, %amount; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(
      checker::check(
          std::get<Shl>(valid->functions.front().body.front()),
          checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}})
          .has_value());
  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .b32 %b; .reg .u64 %amount; shl.b32 %b, %b, %amount; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction =
      std::get<Shl>(invalid->functions.front().body.front());
  const auto& variant = std::get<Shl::B32>(instruction.variant);
  const auto checked = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.amount.locs.front());
}

TEST(ResolvedModule, ChecksShrU32DataAndAmountWidths) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() { .reg .s32 %s; .reg .b32 %b; shr.u32 %b, %s, %b; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(
      checker::check(
          std::get<Shr>(valid->functions.front().body.front()),
          checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}})
          .has_value());
  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .u32 %u; .reg .u64 %amount; shr.u32 %u, %u, %amount; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction =
      std::get<Shr>(invalid->functions.front().body.front());
  const auto& variant = std::get<Shr::U32>(instruction.variant);
  const auto checked = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.amount.locs.front());
}

TEST(ResolvedModule, ChecksSetpLtU32OperandTypes) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() { .reg .pred %p0, %p1; .reg .u32 %u; setp.lt.and.u32 %p0, %u, 16, !%p1; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(
      checker::check(
          std::get<Setp>(valid->functions.front().body.front()),
          checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}})
          .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .pred %p0; .reg .u64 %wide; setp.lt.u32 %p0, %wide, 16; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction =
      std::get<Setp>(invalid->functions.front().body.front());
  const auto& variant = std::get<Setp::Unsigned>(instruction.variant);
  const auto& operands =
      std::get<Setp::Unsigned::SingleOperands>(variant.operands);
  const auto checked = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, operands.src1.locs.front());
}

TEST(ResolvedModule, ChecksSetpGeS32OperandTypes) {
  const checker::Context context{
      .target = {.ptx_version = {1, 0}, .sm_version = 0}};
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() { .reg .pred %p0; .reg .b32 %r0, %r1; setp.ge.s32 %p0, %r0, %r1; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& instruction =
      std::get<Setp>(valid->functions.front().body.front());
  EXPECT_TRUE(std::holds_alternative<Setp::Signed>(instruction.variant));
  EXPECT_TRUE(checker::check(instruction, context).has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .pred %p0; .reg .b64 %rd0; .reg .b32 %r1; setp.ge.s32 %p0, %rd0, %r1; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& invalid_instruction =
      std::get<Setp>(invalid->functions.front().body.front());
  const auto& variant = std::get<Setp::Signed>(invalid_instruction.variant);
  const auto& operands =
      std::get<Setp::Signed::SingleOperands>(variant.operands);
  const auto checked = checker::check(invalid_instruction, context);
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, operands.src1.locs.front());
}

TEST(ResolvedModule, ChecksSetpDualPredicateOperandTypes) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() { .reg .pred %p0, %p1, %p2; .reg .u32 %u0, %u1; .reg .s32 %s0, %s1; setp.eq.u32 %p0|%p1, %u0, %u1; setp.lt.and.s32 %p0|%p1, %s0, %s1, %p2; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  for (const auto& body : valid->functions.front().body) {
    EXPECT_TRUE(
        checker::check(std::get<Setp>(body),
                       checker::Context{
                           .target = {.ptx_version = {1, 0}, .sm_version = 0}})
            .has_value());
  }

  auto instruction = std::get<Setp>(valid->functions.front().body.front());
  auto& variant = std::get<Setp::Unsigned>(instruction.variant);
  auto& operands = std::get<Setp::Unsigned::PairOperands>(variant.operands);
  ASSERT_TRUE(operands.dst.value.second.has_value());
  operands.dst.value.second->register_ref.declared_type = ScalarType::U32;
  const auto checked = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, operands.dst.locs[1]);
}

TEST(ResolvedModule, ChecksSetCommonScalarOperandTypes) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .pred %p;
  .reg .u32 %r0, %r1, %r2;
  .reg .f32 %f;
  .reg .s32 %s0, %s1;
  set.eq.u32.u32 %r0, %r1, %r2;
  set.lt.and.f32.s32 %f, %s0, %s1, !%p;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  for (const auto& body : valid->functions.front().body) {
    EXPECT_TRUE(
        checker::check(std::get<Set>(body),
                       checker::Context{
                           .target = {.ptx_version = {1, 0}, .sm_version = 0}})
            .has_value());
  }

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() {
  .reg .pred %p;
  .reg .f32 %f;
  .reg .s32 %s;
  .reg .u64 %wrong;
  set.lt.and.f32.s32 %f, %wrong, %s, %p;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction =
      std::get<Set>(invalid->functions.front().body.front());
  const auto& variant = std::get<Set::LtAndF32S32>(instruction.variant);
  const auto checked = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksSlctNumericSelectorAndBitSizeDataOperands) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %d32;
  .reg .s32 %a32;
  .reg .u32 %b32;
  .reg .u32 %selector32;
  .reg .b64 %d64;
  .reg .s64 %a64;
  .reg .u64 %b64;
  .reg .f32 %selector64;
  .reg .b32 %selector64_bits;
  slct.u32.s32 %d32, %a32, %b32, %selector32;
  slct.ftz.u64.f32 %d64, %a64, %b64, %selector64;
  slct.ftz.u64.f32 %d64, %a64, %b64, %selector64_bits;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  for (const auto& body : valid->functions.front().body) {
    EXPECT_TRUE(
        checker::check(std::get<Slct>(body),
                       checker::Context{
                           .target = {.ptx_version = {1, 0}, .sm_version = 0}})
            .has_value());
  }

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %d, %a, %b;
  .reg .f32 %selector;
  slct.u32.s32 %d, %a, %b, %selector;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_selector = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_selector.has_value())
      << wrong_selector.error().front().message;
  const auto& selector_instruction =
      std::get<Slct>(wrong_selector->functions.front().body.front());
  const auto& selector_variant =
      std::get<Slct::U32S32>(selector_instruction.variant);
  const auto bad_selector = checker::check(
      selector_instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(bad_selector.has_value());
  EXPECT_EQ(bad_selector.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(bad_selector.error().front().range,
            selector_variant.selector.locs.front());

  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %d, %a, %b;
  .reg .u64 %selector;
  slct.u32.s32 %d, %a, %b, %selector;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto wide_selector = resolveModule(*parsed_module_3);
  ASSERT_TRUE(wide_selector.has_value())
      << wide_selector.error().front().message;
  const auto& wide_selector_instruction =
      std::get<Slct>(wide_selector->functions.front().body.front());
  const auto wide_selector_check = checker::check(
      wide_selector_instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(wide_selector_check.has_value());
  EXPECT_EQ(wide_selector_check.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);

  const auto parsed_module_4 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %d, %b;
  .reg .f32 %a;
  .reg .s32 %selector;
  slct.u32.s32 %d, %a, %b, %selector;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto wrong_data = resolveModule(*parsed_module_4);
  ASSERT_TRUE(wrong_data.has_value()) << wrong_data.error().front().message;
  const auto& data_instruction =
      std::get<Slct>(wrong_data->functions.front().body.front());
  const auto& data_variant = std::get<Slct::U32S32>(data_instruction.variant);
  const auto bad_data = checker::check(
      data_instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(bad_data.has_value());
  EXPECT_EQ(bad_data.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(bad_data.error().front().range, data_variant.src_true.locs.front());

  const auto parsed_module_5 = parseModule(R"ptx(
.entry kernel() {
  .reg .u64 %d, %a, %b;
  .reg .u32 %selector;
  slct.ftz.u64.f32 %d, %a, %b, %selector;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_5);
  const auto integer_float_selector = resolveModule(*parsed_module_5);
  ASSERT_TRUE(integer_float_selector.has_value())
      << integer_float_selector.error().front().message;
  const auto& integer_float_instruction =
      std::get<Slct>(integer_float_selector->functions.front().body.front());
  const auto integer_float_check = checker::check(
      integer_float_instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(integer_float_check.has_value());
  EXPECT_EQ(integer_float_check.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);

  const auto parsed_module_6 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %d, %a, %b;
  .reg .pred %p;
  slct.u32.s32 %d, %a, %b, %p;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_6);
  const auto predicate_selector = resolveModule(*parsed_module_6);
  EXPECT_FALSE(predicate_selector.has_value());
}

TEST(ResolvedModule, ChecksSelpU32OperandTypes) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() { .reg .pred %p; .reg .u32 %dst, %src; selp.u32 %dst, %src, 0, %p; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(
      checker::check(
          std::get<Selp>(valid->functions.front().body.front()),
          checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}})
          .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .pred %p; .reg .u32 %dst; .reg .u64 %wide; selp.u32 %dst, %wide, 0, %p; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction =
      std::get<Selp>(invalid->functions.front().body.front());
  const auto& variant = std::get<Selp::U32>(instruction.variant);
  const auto checked = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.src_true.locs.front());
}

TEST(ResolvedModule, ChecksCvtS32U32OperandTypesAndWidths) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() { .reg .s64 %wide_dst; .reg .u64 %wide_src; cvt.s32.u32 %wide_dst, %wide_src; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(
      checker::check(
          std::get<Cvt>(valid->functions.front().body.front()),
          checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}})
          .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .s32 %dst; .reg .f32 %wrong_src; cvt.s32.u32 %dst, %wrong_src; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction =
      std::get<Cvt>(invalid->functions.front().body.front());
  const auto& variant = std::get<Cvt::S32U32>(instruction.variant);
  const auto checked = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.src.locs.front());
}

TEST(ResolvedModule, ChecksCvtRnF32F64OperandTypes) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst; .reg .f64 %src; cvt.rn.f32.f64 %dst, %src; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(
      checker::check(
          std::get<Cvt>(valid->functions.front().body.front()),
          checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 13}})
          .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst, %wrong_src; cvt.rn.f32.f64 %dst, %wrong_src; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction =
      std::get<Cvt>(invalid->functions.front().body.front());
  const auto& variant = std::get<Cvt::RnF32F64>(instruction.variant);
  const auto checked = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 13}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.src.locs.front());
}

TEST(ResolvedModule, ChecksMixedCvtOperandTypes) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() { .reg .f32 %f; .reg .u32 %u; cvt.rn.f32.u32 %f, %u; cvt.rzi.u32.f32 %u, %f; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const checker::Context context{
      .target = {.ptx_version = {1, 0}, .sm_version = 0}};
  EXPECT_TRUE(
      checker::check(std::get<Cvt>(valid->functions.front().body[0]), context)
          .has_value());
  EXPECT_TRUE(
      checker::check(std::get<Cvt>(valid->functions.front().body[1]), context)
          .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .f32 %wrong_dst, %src; cvt.rzi.u32.f32 %wrong_dst, %src; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction =
      std::get<Cvt>(invalid->functions.front().body.front());
  const auto& variant = std::get<Cvt::RziU32F32>(instruction.variant);
  const auto checked = checker::check(instruction, context);
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.dst.locs.front());
}

TEST(ResolvedModule, ChecksM12CvtScalarAndPackedTypes) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .f32 %f0, %f1;
  .reg .u32 %u0;
  .reg .b32 %r0;
  cvt.rn.f32.s32 %f0, %u0;
  cvt.rn.f16x2.f32 %r0, %f0, %f1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& scalar = std::get<Cvt>(valid->functions.front().body[0]);
  const auto& packed = std::get<Cvt>(valid->functions.front().body[1]);
  EXPECT_TRUE(std::holds_alternative<Cvt::RnF32S32>(scalar.variant));
  EXPECT_TRUE(std::holds_alternative<Cvt::RnF16x2F32>(packed.variant));
  EXPECT_TRUE(
      checker::check(scalar, checker::Context{.target = {.ptx_version = {1, 0},
                                                         .sm_version = 0}})
          .has_value());
  EXPECT_TRUE(
      checker::check(packed, checker::Context{.target = {.ptx_version = {7, 0},
                                                         .sm_version = 80}})
          .has_value());

  const auto parsed_module_2 = parseModule(
      ".entry kernel() { .reg .f16x2 %dst; .reg .f32 %a, %b; cvt.rn.f16x2.f32 "
      "%dst, %a, %b; }");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto packed_container = resolveModule(*parsed_module_2);
  ASSERT_TRUE(packed_container.has_value())
      << packed_container.error().front().message;
  EXPECT_TRUE(
      checker::check(
          std::get<Cvt>(packed_container->functions.front().body.front()),
          checker::Context{.target = {.ptx_version = {7, 0}, .sm_version = 80}})
          .has_value());

  for (const auto source : {
           ".entry kernel() { .reg .f32 %dst, %a, %b; cvt.rn.f16x2.f32 %dst, "
           "%a, %b; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_3 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
    const auto wrong = resolveModule(*parsed_module_3);
    ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
    const auto checked = checker::check(
        std::get<Cvt>(wrong->functions.front().body.front()),
        checker::Context{.target = {.ptx_version = {7, 0}, .sm_version = 80}});
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ChecksM12CvtPackOperandTypes) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r0, %r1, %r2, %r3;
  cvt.pack.sat.u8.s32.b32 %r0, %r1, %r2, %r3;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& instruction =
      std::get<Cvt>(valid->functions.front().body.front());
  EXPECT_TRUE(
      std::holds_alternative<Cvt::PackSatU8S32B32>(instruction.variant));
  EXPECT_TRUE(checker::check(instruction,
                             checker::Context{.target = {.ptx_version = {6, 5},
                                                         .sm_version = 72}})
                  .has_value());

  for (const auto source : {
           ".entry kernel() { .reg .u16 %dst; .reg .u32 %a, %b, %c; "
           "cvt.pack.sat.u8.s32.b32 %dst, %a, %b, %c; }",
           ".entry kernel() { .reg .u32 %dst, %a, %c; .reg .f32 %b; "
           "cvt.pack.sat.u8.s32.b32 %dst, %a, %b, %c; }",
           ".entry kernel() { .reg .u32 %dst, %a, %b; .reg .u64 %c; "
           "cvt.pack.sat.u8.s32.b32 %dst, %a, %b, %c; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_2 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
    const auto wrong = resolveModule(*parsed_module_2);
    ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
    const auto checked = checker::check(
        std::get<Cvt>(wrong->functions.front().body.front()),
        checker::Context{.target = {.ptx_version = {6, 5}, .sm_version = 72}});
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ChecksCvtaU64OperandWidths) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() { .reg .u64 %dst, %src; cvta.global.u64 %dst, %src; cvta.to.global.u64 %dst, %src; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const checker::Context context{
      .target = {.ptx_version = {2, 0}, .sm_version = 20}};
  EXPECT_TRUE(
      checker::check(std::get<Cvta>(valid->functions.front().body[0]), context)
          .has_value());
  EXPECT_TRUE(
      checker::check(std::get<Cvta>(valid->functions.front().body[1]), context)
          .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .u32 %dst, %src; cvta.global.u64 %dst, %src; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction =
      std::get<Cvta>(invalid->functions.front().body.front());
  const auto& variant = std::get<Cvta::GlobalU64>(instruction.variant);
  const auto checked = checker::check(instruction, context);
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.dst.locs.front());
}

TEST(ResolvedModule, ChecksIsspacepGlobalU64OperandTypes) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .pred %p;
  .reg .u64 %rd;
  isspacep.global %p, %rd;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& instruction =
      std::get<Isspacep>(valid->functions.front().body.front());
  EXPECT_TRUE(std::holds_alternative<Isspacep::GlobalU64>(instruction.variant));
  EXPECT_TRUE(checker::check(instruction,
                             checker::Context{.target = {.ptx_version = {2, 0},
                                                         .sm_version = 20}})
                  .has_value());

  for (const auto source : {
           ".entry kernel() { .reg .pred %p; .reg .u32 %r; isspacep.global %p, "
           "%r; }",
           ".entry kernel() { .reg .pred %p; .reg .s32 %s; isspacep.global %p, "
           "%s; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_2 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
    const auto wrong = resolveModule(*parsed_module_2);
    ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
    const auto checked = checker::check(
        std::get<Isspacep>(wrong->functions.front().body.front()),
        checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}});
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }

  const auto parsed_module_3 = parseModule(
      ".entry kernel() { .reg .u32 %r; .reg .u64 %rd; isspacep.global %r, %rd; "
      "}");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  EXPECT_FALSE(resolveModule(*parsed_module_3).has_value());
}

TEST(ResolvedModule, ChecksMulLoU32OperandTypes) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() { .reg .u32 %dst, %src; mul.lo.u32 %dst, %src, 7; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const checker::Context context{
      .target = {.ptx_version = {1, 0}, .sm_version = 0}};
  EXPECT_TRUE(checker::check(
                  std::get<Mul>(valid->functions.front().body.front()), context)
                  .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .u16 %dst, %src; mul.lo.u32 %dst, %src, 7; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_width = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_width.has_value()) << wrong_width.error().front().message;
  const auto& width_instruction =
      std::get<Mul>(wrong_width->functions.front().body.front());
  const auto& width_variant = std::get<Mul::LoU32>(width_instruction.variant);
  const auto width_checked = checker::check(width_instruction, context);
  ASSERT_FALSE(width_checked.has_value());
  EXPECT_EQ(width_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(width_checked.error().front().range,
            width_variant.dst.locs.front());

  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() { .reg .u32 %dst; .reg .f32 %src; mul.lo.u32 %dst, %src, 7; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto wrong_type = resolveModule(*parsed_module_3);
  ASSERT_TRUE(wrong_type.has_value()) << wrong_type.error().front().message;
  const auto& type_instruction =
      std::get<Mul>(wrong_type->functions.front().body.front());
  const auto& type_variant = std::get<Mul::LoU32>(type_instruction.variant);
  const auto type_checked = checker::check(type_instruction, context);
  ASSERT_FALSE(type_checked.has_value());
  EXPECT_EQ(type_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(type_checked.error().front().range, type_variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksMulHiAndWideU32OperandTypes) {
  const checker::Context context{
      .target = {.ptx_version = {1, 0}, .sm_version = 0}};
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r0, %r1, %r2;
  .reg .u64 %rd0;
  mul.hi.u32 %r0, %r1, %r2;
  mul.wide.u32 %rd0, %r1, %r2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& body = valid->functions.front().body;
  const auto& hi = std::get<Mul>(body[0]);
  const auto& wide = std::get<Mul>(body[1]);
  EXPECT_TRUE(std::holds_alternative<Mul::HiU32>(hi.variant));
  EXPECT_TRUE(std::holds_alternative<Mul::WideU32>(wide.variant));
  EXPECT_TRUE(checker::check(hi, context).has_value());
  EXPECT_TRUE(checker::check(wide, context).has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .u16 %dst, %src; mul.hi.u32 %dst, %src, %src; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto narrow_hi = resolveModule(*parsed_module_2);
  ASSERT_TRUE(narrow_hi.has_value()) << narrow_hi.error().front().message;
  const auto& narrow_instruction =
      std::get<Mul>(narrow_hi->functions.front().body.front());
  const auto& narrow_variant = std::get<Mul::HiU32>(narrow_instruction.variant);
  const auto narrow_checked = checker::check(narrow_instruction, context);
  ASSERT_FALSE(narrow_checked.has_value());
  EXPECT_EQ(narrow_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(narrow_checked.error().front().range,
            narrow_variant.dst.locs.front());

  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() { .reg .u64 %dst; .reg .b32 %src; mul.wide.u32 %dst, %src, %src; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto bit_wide_source = resolveModule(*parsed_module_3);
  ASSERT_TRUE(bit_wide_source.has_value())
      << bit_wide_source.error().front().message;
  const auto& bit_instruction =
      std::get<Mul>(bit_wide_source->functions.front().body.front());
  const auto bit_checked = checker::check(bit_instruction, context);
  EXPECT_TRUE(bit_checked.has_value()) << bit_checked.error().front().message;

  const auto parsed_module_4 = parseModule(R"ptx(
.entry kernel() { .reg .u32 %dst, %src; mul.wide.u32 %dst, %src, %src; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto narrow_wide_dst = resolveModule(*parsed_module_4);
  ASSERT_TRUE(narrow_wide_dst.has_value())
      << narrow_wide_dst.error().front().message;
  const auto& dst_instruction =
      std::get<Mul>(narrow_wide_dst->functions.front().body.front());
  const auto& dst_variant = std::get<Mul::WideU32>(dst_instruction.variant);
  const auto dst_checked = checker::check(dst_instruction, context);
  ASSERT_FALSE(dst_checked.has_value());
  EXPECT_EQ(dst_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(dst_checked.error().front().range, dst_variant.dst.locs.front());
}

TEST(ResolvedModule, ChecksMulWideS32OperandTypes) {
  const checker::Context context{
      .target = {.ptx_version = {1, 0}, .sm_version = 0}};
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() { .reg .b64 %rd0; .reg .b32 %r0, %r1; mul.wide.s32 %rd0, %r0, %r1; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& instruction =
      std::get<Mul>(valid->functions.front().body.front());
  EXPECT_TRUE(std::holds_alternative<Mul::WideS32>(instruction.variant));
  EXPECT_TRUE(checker::check(instruction, context).has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .b64 %rd0; .reg .b64 %rd1; .reg .b32 %r1; mul.wide.s32 %rd0, %rd1, %r1; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& invalid_instruction =
      std::get<Mul>(invalid->functions.front().body.front());
  const auto& variant = std::get<Mul::WideS32>(invalid_instruction.variant);
  const auto checked = checker::check(invalid_instruction, context);
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksMulRnF32OperandTypes) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst, %src1, %src2; mul.rn.f32 %dst, %src1, %src2; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const checker::Context context{
      .target = {.ptx_version = {1, 0}, .sm_version = 0}};
  EXPECT_TRUE(checker::check(
                  std::get<Mul>(valid->functions.front().body.front()), context)
                  .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst, %src2; .reg .f64 %wrong_src; mul.rn.f32 %dst, %wrong_src, %src2; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction =
      std::get<Mul>(invalid->functions.front().body.front());
  const auto& variant = std::get<Mul::RnF32>(instruction.variant);
  const auto checked = checker::check(instruction, context);
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksMadLoU32OperandTypes) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() { .reg .u32 %dst, %src1, %src3; mad.lo.u32 %dst, %src1, 7, %src3; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const checker::Context context{
      .target = {.ptx_version = {1, 0}, .sm_version = 0}};
  EXPECT_TRUE(checker::check(
                  std::get<Mad>(valid->functions.front().body.front()), context)
                  .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .u16 %dst, %src1, %src3; mad.lo.u32 %dst, %src1, 7, %src3; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_width = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_width.has_value()) << wrong_width.error().front().message;
  const auto& width_instruction =
      std::get<Mad>(wrong_width->functions.front().body.front());
  const auto& width_variant = std::get<Mad::LoU32>(width_instruction.variant);
  const auto width_checked = checker::check(width_instruction, context);
  ASSERT_FALSE(width_checked.has_value());
  EXPECT_EQ(width_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(width_checked.error().front().range,
            width_variant.dst.locs.front());

  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() { .reg .u32 %dst, %src2, %src3; .reg .f32 %wrong_src; mad.lo.u32 %dst, %wrong_src, %src2, %src3; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto wrong_type = resolveModule(*parsed_module_3);
  ASSERT_TRUE(wrong_type.has_value()) << wrong_type.error().front().message;
  const auto& type_instruction =
      std::get<Mad>(wrong_type->functions.front().body.front());
  const auto& type_variant = std::get<Mad::LoU32>(type_instruction.variant);
  const auto type_checked = checker::check(type_instruction, context);
  ASSERT_FALSE(type_checked.has_value());
  EXPECT_EQ(type_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(type_checked.error().front().range, type_variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksM12MadWideAndRnOperandTypes) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r0, %r1, %r2, %r3;
  .reg .u64 %rd0, %rd3;
  .reg .f32 %f0, %f1, %f2, %f3;
  mad.lo.s32 %r0, %r1, %r2, %r3;
  mad.wide.u32 %rd0, %r1, %r2, %rd3;
  mad.rn.f32 %f0, %f1, %f2, %f3;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& body = valid->functions.front().body;
  const auto& lo = std::get<Mad>(body[0]);
  const auto& wide = std::get<Mad>(body[1]);
  const auto& rn = std::get<Mad>(body[2]);
  EXPECT_TRUE(std::holds_alternative<Mad::LoS32>(lo.variant));
  EXPECT_TRUE(std::holds_alternative<Mad::WideU32>(wide.variant));
  EXPECT_TRUE(std::holds_alternative<Mad::RnF32>(rn.variant));
  EXPECT_TRUE(
      checker::check(lo, checker::Context{.target = {.ptx_version = {1, 0},
                                                     .sm_version = 0}})
          .has_value());
  EXPECT_TRUE(
      checker::check(wide, checker::Context{.target = {.ptx_version = {1, 0},
                                                       .sm_version = 0}})
          .has_value());
  EXPECT_TRUE(
      checker::check(rn, checker::Context{.target = {.ptx_version = {2, 0},
                                                     .sm_version = 20}})
          .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .u64 %dst, %addend; .reg .b32 %src; mad.wide.u32 %dst, %src, %src, %addend; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto bit_wide_source = resolveModule(*parsed_module_2);
  ASSERT_TRUE(bit_wide_source.has_value())
      << bit_wide_source.error().front().message;
  const auto& bit_instruction =
      std::get<Mad>(bit_wide_source->functions.front().body.front());
  const auto bit_checked = checker::check(
      bit_instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  EXPECT_TRUE(bit_checked.has_value()) << bit_checked.error().front().message;

  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() { .reg .u64 %dst; .reg .u32 %src, %addend; mad.wide.u32 %dst, %src, %src, %addend; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto narrow_wide_addend = resolveModule(*parsed_module_3);
  ASSERT_TRUE(narrow_wide_addend.has_value())
      << narrow_wide_addend.error().front().message;
  const auto& addend_instruction =
      std::get<Mad>(narrow_wide_addend->functions.front().body.front());
  const auto& addend_variant =
      std::get<Mad::WideU32>(addend_instruction.variant);
  const auto addend_checked = checker::check(
      addend_instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(addend_checked.has_value());
  EXPECT_EQ(addend_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(addend_checked.error().front().range,
            addend_variant.src3.locs.front());

  const auto parsed_module_4 = parseModule(R"ptx(
.entry kernel() { .reg .u32 %dst, %src; .reg .u64 %addend; mad.wide.u32 %dst, %src, %src, %addend; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto narrow_wide_dst = resolveModule(*parsed_module_4);
  ASSERT_TRUE(narrow_wide_dst.has_value())
      << narrow_wide_dst.error().front().message;
  const auto& dst_instruction =
      std::get<Mad>(narrow_wide_dst->functions.front().body.front());
  const auto& dst_variant = std::get<Mad::WideU32>(dst_instruction.variant);
  const auto dst_checked = checker::check(
      dst_instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(dst_checked.has_value());
  EXPECT_EQ(dst_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(dst_checked.error().front().range, dst_variant.dst.locs.front());

  const auto parsed_module_5 = parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst, %src2, %src3; .reg .b32 %src1; mad.rn.f32 %dst, %src1, %src2, %src3; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_5);
  const auto bit_float_source = resolveModule(*parsed_module_5);
  ASSERT_TRUE(bit_float_source.has_value())
      << bit_float_source.error().front().message;
  const auto& float_instruction =
      std::get<Mad>(bit_float_source->functions.front().body.front());
  const auto float_checked = checker::check(
      float_instruction,
      checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}});
  EXPECT_TRUE(float_checked.has_value())
      << float_checked.error().front().message;
}

TEST(ResolvedModule, ChecksFmaFloatingAndPackedOperandTypes) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .b32 %b32dst, %b32src1, %b32src2, %b32src3;
  .reg .b64 %b64dst, %b64src1, %b64src2, %b64src3;
  fma.rz.ftz.sat.f32 %b32dst, %b32src1, %b32src2, %b32src3;
  fma.rp.f64 %b64dst, %b64src1, %b64src2, %b64src3;
  fma.rm.ftz.f32x2 %b64dst, %b64src1, %b64src2, %b64src3;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& body = valid->functions.front().body;
  EXPECT_TRUE(checker::check(std::get<Fma>(body[0]),
                             checker::Context{.target = {.ptx_version = {2, 0},
                                                         .sm_version = 20}})
                  .has_value());
  EXPECT_TRUE(checker::check(std::get<Fma>(body[1]),
                             checker::Context{.target = {.ptx_version = {1, 4},
                                                         .sm_version = 13}})
                  .has_value());
  EXPECT_TRUE(checker::check(std::get<Fma>(body[2]),
                             checker::Context{.target = {.ptx_version = {8, 6},
                                                         .sm_version = 100}})
                  .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst, %src2, %src3; .reg .u32 %wrong_src; fma.rn.f32 %dst, %wrong_src, %src2, %src3; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_scalar = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_scalar.has_value()) << wrong_scalar.error().front().message;
  const auto& instruction =
      std::get<Fma>(wrong_scalar->functions.front().body.front());
  const auto& variant = std::get<Fma::RnF32>(instruction.variant);
  const auto checked = checker::check(
      instruction,
      checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.src1.locs.front());

  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() { .reg .b64 %dst, %src1, %src3; .reg .b32 %wrong_src; fma.rn.f32x2 %dst, %src1, %wrong_src, %src3; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto wrong_packed = resolveModule(*parsed_module_3);
  ASSERT_TRUE(wrong_packed.has_value()) << wrong_packed.error().front().message;
  const auto& packed_instruction =
      std::get<Fma>(wrong_packed->functions.front().body.front());
  const auto& packed_variant = std::get<Fma::F32x2>(packed_instruction.variant);
  const auto packed_checked = checker::check(
      packed_instruction,
      checker::Context{.target = {.ptx_version = {8, 6}, .sm_version = 100}});
  ASSERT_FALSE(packed_checked.has_value());
  EXPECT_EQ(packed_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(packed_checked.error().front().range,
            packed_variant.src2.locs.front());
}

TEST(ResolvedModule, ChecksFmaHalfBfloatAndMixedOperandTypes) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .b16 %b16dst, %b16src1, %b16src2, %b16src3;
  .reg .b32 %b32dst, %b32src1, %b32src2, %b32src3;
  .reg .f16x2 %f16x2dst, %f16x2src1, %f16x2src2, %f16x2src3;
  .reg .f32 %f32dst, %f32src3;
  fma.rn.ftz.sat.f16 %b16dst, %b16src1, %b16src2, %b16src3;
  fma.rn.oob.relu.f16x2 %b32dst, %b32src1, %b32src2, %b32src3;
  fma.rn.f16x2 %f16x2dst, %f16x2src1, %f16x2src2, %f16x2src3;
  fma.rn.relu.bf16 %b16dst, %b16src1, %b16src2, %b16src3;
  fma.rn.oob.relu.bf16x2 %b32dst, %b32src1, %b32src2, %b32src3;
  fma.rp.sat.f32.f16 %f32dst, %b16src1, %b16src2, %f32src3;
  fma.rm.f32.bf16 %f32dst, %b16src1, %b16src2, %f32src3;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& body = valid->functions.front().body;
  const std::array contexts{
      checker::Context{.target = {.ptx_version = {4, 2}, .sm_version = 53}},
      checker::Context{.target = {.ptx_version = {8, 1}, .sm_version = 90}},
      checker::Context{.target = {.ptx_version = {4, 2}, .sm_version = 53}},
      checker::Context{.target = {.ptx_version = {7, 0}, .sm_version = 80}},
      checker::Context{.target = {.ptx_version = {8, 1}, .sm_version = 90}},
      checker::Context{.target = {.ptx_version = {8, 6}, .sm_version = 100}},
      checker::Context{.target = {.ptx_version = {8, 6}, .sm_version = 100}},
  };
  ASSERT_EQ(body.size(), contexts.size());
  for (size_t index = 0; index != body.size(); ++index) {
    EXPECT_TRUE(
        checker::check(std::get<Fma>(body[index]), contexts[index]).has_value())
        << index;
  }

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .b16 %dst, %src2, %src3; .reg .f16 %wrong_src; fma.rn.bf16 %dst, %wrong_src, %src2, %src3; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_bfloat = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_bfloat.has_value()) << wrong_bfloat.error().front().message;
  const auto& bfloat_instruction =
      std::get<Fma>(wrong_bfloat->functions.front().body.front());
  const auto& bfloat_variant = std::get<Fma::Bf16>(bfloat_instruction.variant);
  const auto bfloat_checked = checker::check(
      bfloat_instruction,
      checker::Context{.target = {.ptx_version = {7, 0}, .sm_version = 80}});
  ASSERT_FALSE(bfloat_checked.has_value());
  EXPECT_EQ(bfloat_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(bfloat_checked.error().front().range,
            bfloat_variant.src1.locs.front());

  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() { .reg .b32 %dst, %src2, %src3; .reg .f16x2 %wrong_src; fma.rn.bf16x2 %dst, %wrong_src, %src2, %src3; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto wrong_packed_bfloat = resolveModule(*parsed_module_3);
  ASSERT_TRUE(wrong_packed_bfloat.has_value())
      << wrong_packed_bfloat.error().front().message;
  const auto& packed_bfloat_instruction =
      std::get<Fma>(wrong_packed_bfloat->functions.front().body.front());
  const auto& packed_bfloat_variant =
      std::get<Fma::Bf16x2>(packed_bfloat_instruction.variant);
  const auto packed_bfloat_checked = checker::check(
      packed_bfloat_instruction,
      checker::Context{.target = {.ptx_version = {7, 0}, .sm_version = 80}});
  ASSERT_FALSE(packed_bfloat_checked.has_value());
  EXPECT_EQ(packed_bfloat_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(packed_bfloat_checked.error().front().range,
            packed_bfloat_variant.src1.locs.front());

  const auto parsed_module_4 = parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst; .reg .b16 %src1; .reg .f16 %src2; .reg .b32 %src3; fma.rn.f32.bf16 %dst, %src1, %src2, %src3; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto wrong_mixed = resolveModule(*parsed_module_4);
  ASSERT_TRUE(wrong_mixed.has_value()) << wrong_mixed.error().front().message;
  const auto& mixed_instruction =
      std::get<Fma>(wrong_mixed->functions.front().body.front());
  const auto& mixed_variant =
      std::get<Fma::MixedF32Bf16>(mixed_instruction.variant);
  const auto mixed_checked = checker::check(
      mixed_instruction,
      checker::Context{.target = {.ptx_version = {8, 6}, .sm_version = 100}});
  ASSERT_FALSE(mixed_checked.has_value());
  EXPECT_EQ(mixed_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(mixed_checked.error().front().range,
            mixed_variant.src2.locs.front());
}

TEST(ResolvedModule, RejectsFmaMismatchedTypesInEveryOperandPosition) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .f32 %f;
  .reg .f64 %d;
  .reg .u32 %u;
  .reg .u64 %u64;
  .reg .b64 %b64;
  .reg .b32 %b32;
  .reg .b16 %b16;
  .reg .f16 %h;

  fma.rn.f32 %u, %f, %f, %f;
  fma.rn.f32 %f, %d, %f, %f;
  fma.rn.f32 %f, %f, %u, %f;
  fma.rn.f32 %f, %f, %f, %u;

  fma.rn.f32x2 %u64, %b64, %b64, %b64;
  fma.rn.f32x2 %b64, %d, %b64, %b64;
  fma.rn.f32x2 %b64, %b64, %b32, %b64;
  fma.rn.f32x2 %b64, %b64, %b64, %b32;

  fma.rn.bf16 %h, %b16, %b16, %b16;
  fma.rn.bf16 %b16, %h, %b16, %b16;
  fma.rn.bf16 %b16, %b16, %h, %b16;
  fma.rn.bf16 %b16, %b16, %b16, %h;

  fma.rn.f32.bf16 %d, %b16, %b16, %f;
  fma.rn.f32.bf16 %f, %h, %b16, %f;
  fma.rn.f32.bf16 %f, %b16, %h, %f;
  fma.rn.f32.bf16 %f, %b16, %b16, %d;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto resolved = resolveModule(*parsed_module_1);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 16U);

  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 100}};
  for (size_t index = 0; index != body.size(); ++index) {
    const auto checked = checker::check(std::get<Fma>(body[index]), context);
    SCOPED_TRACE(index);
    ASSERT_FALSE(checked.has_value());
    ASSERT_EQ(checked.error().size(), 1U);
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ChecksDivU32OperandTypes) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() { .reg .u32 %dst, %src; div.u32 %dst, %src, 0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const checker::Context context{
      .target = {.ptx_version = {1, 0}, .sm_version = 0}};
  EXPECT_TRUE(checker::check(
                  std::get<Div>(valid->functions.front().body.front()), context)
                  .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .u16 %dst, %src; div.u32 %dst, %src, 0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_width = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_width.has_value()) << wrong_width.error().front().message;
  const auto& width_instruction =
      std::get<Div>(wrong_width->functions.front().body.front());
  const auto& width_variant = std::get<Div::U32>(width_instruction.variant);
  const auto width_checked = checker::check(width_instruction, context);
  ASSERT_FALSE(width_checked.has_value());
  EXPECT_EQ(width_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(width_checked.error().front().range,
            width_variant.dst.locs.front());

  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() { .reg .u32 %dst; .reg .f32 %wrong_src; div.u32 %dst, %wrong_src, 0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto wrong_type = resolveModule(*parsed_module_3);
  ASSERT_TRUE(wrong_type.has_value()) << wrong_type.error().front().message;
  const auto& type_instruction =
      std::get<Div>(wrong_type->functions.front().body.front());
  const auto& type_variant = std::get<Div::U32>(type_instruction.variant);
  const auto type_checked = checker::check(type_instruction, context);
  ASSERT_FALSE(type_checked.has_value());
  EXPECT_EQ(type_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(type_checked.error().front().range, type_variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksM12DivS32AndRnFloatingOperandTypes) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r0, %r1, %r2;
  .reg .f32 %f0, %f1, %f2;
  .reg .f64 %d0, %d1, %d2;
  div.s32 %r0, %r1, %r2;
  div.rn.f32 %f0, %f1, %f2;
  div.rn.f64 %d0, %d1, %d2;
}

)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& body = valid->functions.front().body;
  const auto& s32 = std::get<Div>(body[0]);
  const auto& f32 = std::get<Div>(body[1]);
  const auto& f64 = std::get<Div>(body[2]);
  EXPECT_TRUE(std::holds_alternative<Div::S32>(s32.variant));
  EXPECT_TRUE(std::holds_alternative<Div::RnF32>(f32.variant));
  EXPECT_TRUE(std::holds_alternative<Div::RnF64>(f64.variant));
  EXPECT_TRUE(
      checker::check(s32, checker::Context{.target = {.ptx_version = {1, 0},
                                                      .sm_version = 0}})
          .has_value());
  EXPECT_TRUE(
      checker::check(f32, checker::Context{.target = {.ptx_version = {1, 4},
                                                      .sm_version = 20}})
          .has_value());
  EXPECT_TRUE(
      checker::check(f64, checker::Context{.target = {.ptx_version = {1, 4},
                                                      .sm_version = 13}})
          .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst, %src2; .reg .b32 %src1; div.rn.f32 %dst, %src1, %src2; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto bit_f32_source = resolveModule(*parsed_module_2);
  ASSERT_TRUE(bit_f32_source.has_value())
      << bit_f32_source.error().front().message;
  const auto& f32_instruction =
      std::get<Div>(bit_f32_source->functions.front().body.front());
  const auto f32_checked = checker::check(
      f32_instruction,
      checker::Context{.target = {.ptx_version = {1, 4}, .sm_version = 20}});
  EXPECT_TRUE(f32_checked.has_value()) << f32_checked.error().front().message;

  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() { .reg .f64 %dst, %src2; .reg .b64 %src1; div.rn.f64 %dst, %src1, %src2; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto bit_f64_source = resolveModule(*parsed_module_3);
  ASSERT_TRUE(bit_f64_source.has_value())
      << bit_f64_source.error().front().message;
  const auto& f64_instruction =
      std::get<Div>(bit_f64_source->functions.front().body.front());
  const auto f64_checked = checker::check(
      f64_instruction,
      checker::Context{.target = {.ptx_version = {1, 4}, .sm_version = 13}});
  EXPECT_TRUE(f64_checked.has_value()) << f64_checked.error().front().message;
}

TEST(ResolvedModule, ChecksM12RemTypesAndZeroDivisor) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .s32 %s0, %s1;
  .reg .u32 %u0, %u1, %u2;
  rem.s32 %s0, %s1, 0;
  rem.u32 %u0, %u1, %u2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& body = valid->functions.front().body;
  const auto& signed_rem = std::get<Rem>(body[0]);
  const auto& unsigned_rem = std::get<Rem>(body[1]);
  EXPECT_TRUE(std::holds_alternative<Rem::S32>(signed_rem.variant));
  EXPECT_TRUE(std::holds_alternative<Rem::U32>(unsigned_rem.variant));
  const checker::Context context{
      .target = {.ptx_version = {1, 0}, .sm_version = 0}};
  EXPECT_TRUE(checker::check(signed_rem, context).has_value());
  EXPECT_TRUE(checker::check(unsigned_rem, context).has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .u32 %dst; .reg .f32 %src; rem.u32 %dst, %src, 0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_type = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_type.has_value()) << wrong_type.error().front().message;
  const auto& type_instruction =
      std::get<Rem>(wrong_type->functions.front().body.front());
  const auto& type_variant = std::get<Rem::U32>(type_instruction.variant);
  const auto type_checked = checker::check(type_instruction, context);
  ASSERT_FALSE(type_checked.has_value());
  EXPECT_EQ(type_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(type_checked.error().front().range, type_variant.src1.locs.front());

  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() { .reg .u64 %dst, %src; rem.s32 %dst, %src, 0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto wrong_width = resolveModule(*parsed_module_3);
  ASSERT_TRUE(wrong_width.has_value()) << wrong_width.error().front().message;
  const auto& width_instruction =
      std::get<Rem>(wrong_width->functions.front().body.front());
  const auto& width_variant = std::get<Rem::S32>(width_instruction.variant);
  const auto width_checked = checker::check(width_instruction, context);
  ASSERT_FALSE(width_checked.has_value());
  EXPECT_EQ(width_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(width_checked.error().front().range,
            width_variant.dst.locs.front());
}

TEST(ResolvedModule, ChecksM12MinTypes) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .s32 %s0, %s1, %s2;
  .reg .f32 %f0, %f1, %f2;
  min.s32 %s0, %s1, %s2;
  min.NaN.f32 %f0, %f1, %f2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& body = valid->functions.front().body;
  const auto& integer_min = std::get<Min>(body[0]);
  const auto& nan_min = std::get<Min>(body[1]);
  EXPECT_TRUE(std::holds_alternative<Min::S32>(integer_min.variant));
  EXPECT_TRUE(std::holds_alternative<Min::NanF32>(nan_min.variant));
  EXPECT_TRUE(checker::check(integer_min,
                             checker::Context{.target = {.ptx_version = {1, 0},
                                                         .sm_version = 0}})
                  .has_value());
  EXPECT_TRUE(
      checker::check(nan_min, checker::Context{.target = {.ptx_version = {7, 0},
                                                          .sm_version = 80}})
          .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .s32 %dst, %src2; .reg .f32 %src1; min.s32 %dst, %src1, %src2; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_integer_type = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_integer_type.has_value())
      << wrong_integer_type.error().front().message;
  const auto& integer_instruction =
      std::get<Min>(wrong_integer_type->functions.front().body.front());
  const auto& integer_variant = std::get<Min::S32>(integer_instruction.variant);
  const auto integer_checked = checker::check(
      integer_instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(integer_checked.has_value());
  EXPECT_EQ(integer_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(integer_checked.error().front().range,
            integer_variant.src1.locs.front());

  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst, %src2; .reg .b32 %src1; min.NaN.f32 %dst, %src1, %src2; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto bit_nan_source = resolveModule(*parsed_module_3);
  ASSERT_TRUE(bit_nan_source.has_value())
      << bit_nan_source.error().front().message;
  const auto& nan_instruction =
      std::get<Min>(bit_nan_source->functions.front().body.front());
  const auto nan_checked = checker::check(
      nan_instruction,
      checker::Context{.target = {.ptx_version = {7, 0}, .sm_version = 80}});
  EXPECT_TRUE(nan_checked.has_value()) << nan_checked.error().front().message;
}

TEST(ResolvedModule, ChecksM12MaxTypes) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .s32 %s0, %s1, %s2;
  .reg .f32 %f0, %f1, %f2;
  max.s32 %s0, %s1, %s2;
  max.NaN.f32 %f0, %f1, %f2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& body = valid->functions.front().body;
  const auto& integer_max = std::get<Max>(body[0]);
  const auto& nan_max = std::get<Max>(body[1]);
  EXPECT_TRUE(std::holds_alternative<Max::S32>(integer_max.variant));
  EXPECT_TRUE(std::holds_alternative<Max::NanF32>(nan_max.variant));
  EXPECT_TRUE(checker::check(integer_max,
                             checker::Context{.target = {.ptx_version = {1, 0},
                                                         .sm_version = 0}})
                  .has_value());
  EXPECT_TRUE(
      checker::check(nan_max, checker::Context{.target = {.ptx_version = {7, 0},
                                                          .sm_version = 80}})
          .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .s32 %dst, %src2; .reg .f32 %src1; max.s32 %dst, %src1, %src2; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_integer_type = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_integer_type.has_value())
      << wrong_integer_type.error().front().message;
  const auto& integer_instruction =
      std::get<Max>(wrong_integer_type->functions.front().body.front());
  const auto& integer_variant = std::get<Max::S32>(integer_instruction.variant);
  const auto integer_checked = checker::check(
      integer_instruction,
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(integer_checked.has_value());
  EXPECT_EQ(integer_checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(integer_checked.error().front().range,
            integer_variant.src1.locs.front());

  const auto parsed_module_3 = parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst, %src2; .reg .b32 %src1; max.NaN.f32 %dst, %src1, %src2; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
  const auto bit_nan_source = resolveModule(*parsed_module_3);
  ASSERT_TRUE(bit_nan_source.has_value())
      << bit_nan_source.error().front().message;
  const auto& nan_instruction =
      std::get<Max>(bit_nan_source->functions.front().body.front());
  const auto nan_checked = checker::check(
      nan_instruction,
      checker::Context{.target = {.ptx_version = {7, 0}, .sm_version = 80}});
  EXPECT_TRUE(nan_checked.has_value()) << nan_checked.error().front().message;
}

TEST(ResolvedModule, ChecksM12AbsTypes) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .s32 %s0, %s1;
  .reg .f32 %f0, %f1;
  abs.s32 %s0, %s1;
  abs.f32 %f0, %f1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& body = valid->functions.front().body;
  const auto& integer_abs = std::get<Abs>(body[0]);
  const auto& float_abs = std::get<Abs>(body[1]);
  EXPECT_TRUE(std::holds_alternative<Abs::S32>(integer_abs.variant));
  EXPECT_TRUE(std::holds_alternative<Abs::F32>(float_abs.variant));
  const checker::Context context{
      .target = {.ptx_version = {1, 0}, .sm_version = 0}};
  EXPECT_TRUE(checker::check(integer_abs, context).has_value());
  EXPECT_TRUE(checker::check(float_abs, context).has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst; .reg .s32 %src; abs.f32 %dst, %src; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_type = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_type.has_value()) << wrong_type.error().front().message;
  const auto& instruction =
      std::get<Abs>(wrong_type->functions.front().body.front());
  const auto& variant = std::get<Abs::F32>(instruction.variant);
  const auto checked = checker::check(instruction, context);
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.src.locs.front());
}

TEST(ResolvedModule, ChecksM12NegTypesAndPackedContainers) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .s32 %s0, %s1;
  .reg .f32 %f0, %f1;
  .reg .b32 %r0, %r1;
  neg.s32 %s0, %s1;
  neg.f32 %f0, %f1;
  neg.f16x2 %r0, %r1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& body = valid->functions.front().body;
  const auto& integer_neg = std::get<Neg>(body[0]);
  const auto& float_neg = std::get<Neg>(body[1]);
  const auto& packed_neg = std::get<Neg>(body[2]);
  EXPECT_TRUE(std::holds_alternative<Neg::S32>(integer_neg.variant));
  EXPECT_TRUE(std::holds_alternative<Neg::F32>(float_neg.variant));
  EXPECT_TRUE(std::holds_alternative<Neg::F16x2>(packed_neg.variant));
  EXPECT_TRUE(checker::check(integer_neg,
                             checker::Context{.target = {.ptx_version = {1, 0},
                                                         .sm_version = 0}})
                  .has_value());
  EXPECT_TRUE(checker::check(float_neg,
                             checker::Context{.target = {.ptx_version = {1, 0},
                                                         .sm_version = 0}})
                  .has_value());
  EXPECT_TRUE(checker::check(packed_neg,
                             checker::Context{.target = {.ptx_version = {6, 0},
                                                         .sm_version = 53}})
                  .has_value());

  for (const auto source : {
           ".entry kernel() { .reg .f16 %dst, %src; neg.f16x2 %dst, %src; }",
           ".entry kernel() { .reg .f32 %dst, %src; neg.f16x2 %dst, %src; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_2 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
    const auto wrong_container = resolveModule(*parsed_module_2);
    ASSERT_TRUE(wrong_container.has_value())
        << wrong_container.error().front().message;
    const auto& instruction =
        std::get<Neg>(wrong_container->functions.front().body.front());
    const auto checked = checker::check(
        instruction,
        checker::Context{.target = {.ptx_version = {6, 0}, .sm_version = 53}});
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ChecksM12Lop3B32WidthCompatibility) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r0, %r1, %r2, %r3;
  lop3.b32 %r0, %r1, %r2, %r3, 0x1a;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& instruction =
      std::get<Lop3>(valid->functions.front().body.front());
  EXPECT_TRUE(std::holds_alternative<Lop3::B32>(instruction.variant));
  EXPECT_TRUE(checker::check(instruction,
                             checker::Context{.target = {.ptx_version = {4, 3},
                                                         .sm_version = 50}})
                  .has_value());
}

TEST(ResolvedModule, ChecksM12ShfTypesAndCounts) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r0, %r1, %r2, %r3;
  shf.l.clamp.b32 %r0, %r1, %r2, 8;
  shf.r.wrap.b32 %r0, %r1, %r2, %r3;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& body = valid->functions.front().body;
  EXPECT_TRUE(
      std::holds_alternative<Shf::LClampB32>(std::get<Shf>(body[0]).variant));
  EXPECT_TRUE(
      std::holds_alternative<Shf::RWrapB32>(std::get<Shf>(body[1]).variant));
  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .u16 %dst; .reg .u32 %a, %b; shf.l.clamp.b32 %dst, %a, %b, 8; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_type = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_type.has_value()) << wrong_type.error().front().message;
  const auto checked = checker::check(
      std::get<Shf>(wrong_type->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {3, 1}, .sm_version = 32}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

TEST(ResolvedModule, ChecksM12PrmtRegisterWidths) {
  const auto parsed_module_1 = parseModule(
      ".entry kernel() { .reg .u32 %r0, %r1, %r2, %r3; prmt.b32 %r0, %r1, %r2, "
      "0x5410; prmt.b32.f4e %r0, %r1, %r2, %r3; }");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto context =
      checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}};
  EXPECT_TRUE(
      checker::check(std::get<Prmt>(valid->functions.front().body[0]), context)
          .has_value());
  EXPECT_TRUE(
      checker::check(std::get<Prmt>(valid->functions.front().body[1]), context)
          .has_value());
  const auto parsed_module_2 = parseModule(
      ".entry kernel() { .reg .u16 %r0; .reg .u32 %r1, %r2; prmt.b32 %r0, %r1, "
      "%r2, 0; }");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
  EXPECT_FALSE(
      checker::check(std::get<Prmt>(wrong->functions.front().body.front()),
                     context)
          .has_value());
}

TEST(ResolvedModule, ChecksM12PopcTypes) {
  const auto context =
      checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}};
  const auto parsed_module_1 = parseModule(
      ".entry kernel() { .reg .u32 %dst, %src; popc.b32 %dst, %src; }");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& instruction =
      std::get<Popc>(valid->functions.front().body.front());
  EXPECT_TRUE(std::holds_alternative<Popc::B32>(instruction.variant));
  EXPECT_TRUE(checker::check(instruction, context).has_value());
  for (const auto source : {
           ".entry kernel() { .reg .u16 %dst; .reg .u32 %src; popc.b32 %dst, "
           "%src; }",
           ".entry kernel() { .reg .u32 %dst; .reg .u16 %src; popc.b32 %dst, "
           "%src; }",
       }) {
    const auto parsed_module_2 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
    const auto wrong = resolveModule(*parsed_module_2);
    ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
    const auto checked = checker::check(
        std::get<Popc>(wrong->functions.front().body.front()), context);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ChecksM12ClzTypes) {
  const auto context =
      checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}};
  const auto parsed_module_1 = parseModule(
      ".entry kernel() { .reg .u32 %dst, %src32; .reg .u64 %src64; clz.b32 "
      "%dst, %src32; clz.b64 %dst, %src64; }");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(
      checker::check(std::get<Clz>(valid->functions.front().body[0]), context)
          .has_value());
  EXPECT_TRUE(
      checker::check(std::get<Clz>(valid->functions.front().body[1]), context)
          .has_value());
  for (const auto source : {
           ".entry kernel() { .reg .u64 %dst, %src; clz.b64 %dst, %src; }",
           ".entry kernel() { .reg .u32 %dst; .reg .u16 %src; clz.b32 %dst, "
           "%src; }",
       }) {
    const auto parsed_module_2 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
    const auto wrong = resolveModule(*parsed_module_2);
    ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
    const auto checked = checker::check(
        std::get<Clz>(wrong->functions.front().body.front()), context);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ChecksM12BfindTypes) {
  const auto context =
      checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}};
  const auto parsed_module_1 = parseModule(
      ".entry kernel() { .reg .u32 %dst, %src; bfind.shiftamt.u32 %dst, %src; "
      "}");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(
      checker::check(std::get<Bfind>(valid->functions.front().body.front()),
                     context)
          .has_value());
  for (const auto source : {
           ".entry kernel() { .reg .s32 %dst, %src; bfind.shiftamt.u32 %dst, "
           "%src; }",
           ".entry kernel() { .reg .u32 %dst; .reg .s32 %src; "
           "bfind.shiftamt.u32 %dst, %src; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_2 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
    const auto compatible = resolveModule(*parsed_module_2);
    ASSERT_TRUE(compatible.has_value()) << compatible.error().front().message;
    EXPECT_TRUE(checker::check(
                    std::get<Bfind>(compatible->functions.front().body.front()),
                    context)
                    .has_value());
  }
  for (const auto source : {
           ".entry kernel() { .reg .u64 %dst, %src; bfind.shiftamt.u32 %dst, "
           "%src; }",
       }) {
    const auto parsed_module_3 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
    const auto wrong = resolveModule(*parsed_module_3);
    ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
    const auto checked = checker::check(
        std::get<Bfind>(wrong->functions.front().body.front()), context);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ChecksM12BfeTypes) {
  const auto context =
      checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}};
  const auto parsed_module_1 = parseModule(
      ".entry kernel() { .reg .u32 %dst, %src; bfe.u32 %dst, %src, 0, 8; }");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(checker::check(
                  std::get<Bfe>(valid->functions.front().body.front()), context)
                  .has_value());
  for (const auto source : {
           ".entry kernel() { .reg .s32 %dst, %src; bfe.u32 %dst, %src, 0, 8; "
           "}",
           ".entry kernel() { .reg .u32 %dst; .reg .s32 %src; bfe.u32 %dst, "
           "%src, 0, 8; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_2 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
    const auto compatible = resolveModule(*parsed_module_2);
    ASSERT_TRUE(compatible.has_value()) << compatible.error().front().message;
    EXPECT_TRUE(
        checker::check(
            std::get<Bfe>(compatible->functions.front().body.front()), context)
            .has_value());
  }
  for (const auto source : {
           ".entry kernel() { .reg .u64 %dst, %src; bfe.u32 %dst, %src, 0, 8; "
           "}",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_3 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
    const auto wrong = resolveModule(*parsed_module_3);
    ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
    const auto checked = checker::check(
        std::get<Bfe>(wrong->functions.front().body.front()), context);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ChecksM12BfiTypes) {
  const auto context =
      checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}};
  const auto parsed_module_1 = parseModule(
      ".entry kernel() { .reg .u32 %dst, %insert, %base; bfi.b32 %dst, "
      "%insert, %base, 0, 8; }");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(checker::check(
                  std::get<Bfi>(valid->functions.front().body.front()), context)
                  .has_value());
  for (const auto source : {
           ".entry kernel() { .reg .b64 %dst, %insert, %base; bfi.b32 %dst, "
           "%insert, %base, 0, 8; }",
           ".entry kernel() { .reg .u16 %dst, %insert, %base; bfi.b32 %dst, "
           "%insert, %base, 0, 8; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_2 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
    const auto wrong = resolveModule(*parsed_module_2);
    ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
    const auto checked = checker::check(
        std::get<Bfi>(wrong->functions.front().body.front()), context);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ChecksM12BrevTypes) {
  const auto context =
      checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}};
  const auto parsed_module_1 = parseModule(
      ".entry kernel() { .reg .u32 %dst, %src; brev.b32 %dst, %src; }");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(
      checker::check(std::get<Brev>(valid->functions.front().body.front()),
                     context)
          .has_value());
  for (const auto source : {
           ".entry kernel() { .reg .u16 %dst, %src; brev.b32 %dst, %src; }",
           ".entry kernel() { .reg .b64 %dst, %src; brev.b32 %dst, %src; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_2 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
    const auto wrong = resolveModule(*parsed_module_2);
    ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
    const auto checked = checker::check(
        std::get<Brev>(wrong->functions.front().body.front()), context);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
