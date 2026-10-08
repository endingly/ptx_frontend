#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using test_helpers::parseModule;

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
      valid->functions.front().body.front()->check(checker::Context{
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
  const auto& invalid_and = *(invalid->functions.front().body.front());
  const auto& invalid_variant = dynamic_cast<const AndB32&>(invalid_and);
  const auto invalid_check = invalid_and.check(checker::Context{
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
      valid->functions.front().body.front()->check(checker::Context{
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
  const auto& invalid_or = *(invalid->functions.front().body.front());
  const auto& invalid_variant = dynamic_cast<const OrB32&>(invalid_or);
  const auto invalid_check = invalid_or.check(checker::Context{
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
  EXPECT_TRUE(valid->functions.front()
                  .body.front()
                  ->check(checker::Context{
                      .target = {.ptx_version = {1, 0}, .sm_version = 0}})
                  .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .b32 %b; .reg .u16 %h; xor.b32 %b, %h, %b; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction = *(invalid->functions.front().body.front());
  const auto& variant = dynamic_cast<const XorB32&>(instruction);
  const auto checked = instruction.check(
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
  EXPECT_TRUE(valid->functions.front()
                  .body.front()
                  ->check(checker::Context{
                      .target = {.ptx_version = {1, 0}, .sm_version = 0}})
                  .has_value());
  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .b32 %b; .reg .u16 %h; not.b32 %b, %h; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction = *(invalid->functions.front().body.front());
  const auto& variant = dynamic_cast<const NotB32&>(instruction);
  const auto checked = instruction.check(
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
  EXPECT_TRUE(valid->functions.front()
                  .body.front()
                  ->check(checker::Context{
                      .target = {.ptx_version = {1, 0}, .sm_version = 0}})
                  .has_value());
  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .b32 %b; .reg .u64 %amount; shl.b32 %b, %b, %amount; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction = *(invalid->functions.front().body.front());
  const auto& variant = dynamic_cast<const ShlB32&>(instruction);
  const auto checked = instruction.check(
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
  EXPECT_TRUE(valid->functions.front()
                  .body.front()
                  ->check(checker::Context{
                      .target = {.ptx_version = {1, 0}, .sm_version = 0}})
                  .has_value());
  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .u32 %u; .reg .u64 %amount; shr.u32 %u, %u, %amount; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction = *(invalid->functions.front().body.front());
  const auto& variant = dynamic_cast<const ShrU32&>(instruction);
  const auto checked = instruction.check(
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
  EXPECT_TRUE(valid->functions.front()
                  .body.front()
                  ->check(checker::Context{
                      .target = {.ptx_version = {1, 0}, .sm_version = 0}})
                  .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .pred %p0; .reg .u64 %wide; setp.lt.u32 %p0, %wide, 16; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction = *(invalid->functions.front().body.front());
  const auto& variant = dynamic_cast<const SetpUnsigned&>(instruction);
  EXPECT_TRUE(variant.dst_predicate_or_sink.has_value());
  EXPECT_FALSE(variant.dst_predicate_pair_or_sink.has_value());
  const auto checked = instruction.check(
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.src1.locs.front());
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
  const auto& instruction = *(valid->functions.front().body.front());
  EXPECT_TRUE((instruction.instruction_kind() == InstructionKind::SetpSigned));
  EXPECT_TRUE(instruction.check(context).has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .pred %p0; .reg .b64 %rd0; .reg .b32 %r1; setp.ge.s32 %p0, %rd0, %r1; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& invalid_instruction = *(invalid->functions.front().body.front());
  const auto& variant = dynamic_cast<const SetpSigned&>(invalid_instruction);
  EXPECT_TRUE(variant.dst_predicate_or_sink.has_value());
  EXPECT_FALSE(variant.dst_predicate_pair_or_sink.has_value());
  const auto checked = invalid_instruction.check(context);
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range, variant.src1.locs.front());
}

TEST(ResolvedModule, ChecksSetpDualPredicateOperandTypes) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() { .reg .pred %p0, %p1, %p2; .reg .u32 %u0, %u1; .reg .s32 %s0, %s1; setp.eq.u32 %p0|%p1, %u0, %u1; setp.lt.and.s32 %p0|%p1, %s0, %s1, %p2; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  for (const auto& body : valid->functions.front().body) {
    EXPECT_TRUE(body->check(checker::Context{.target = {.ptx_version = {1, 0},
                                                        .sm_version = 0}})
                    .has_value());
  }

  auto& instruction = *valid->functions.front().body.front();
  auto& variant = dynamic_cast<SetpUnsigned&>(instruction);
  ASSERT_TRUE(variant.dst_predicate_pair_or_sink.has_value());
  ASSERT_TRUE(variant.dst_predicate_pair_or_sink->value.second.has_value());
  EXPECT_FALSE(variant.dst_predicate_or_sink.has_value());
  variant.dst_predicate_pair_or_sink->value.second->register_ref.declared_type =
      ScalarType::U32;
  const auto checked = instruction.check(
      checker::Context{.target = {.ptx_version = {1, 0}, .sm_version = 0}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(checked.error().front().range,
            variant.dst_predicate_pair_or_sink->locs[1]);
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
    EXPECT_TRUE(body->check(checker::Context{.target = {.ptx_version = {1, 0},
                                                        .sm_version = 0}})
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
  const auto& instruction = *(invalid->functions.front().body.front());
  const auto& variant = dynamic_cast<const SetSignedBoolean&>(instruction);
  const auto checked = instruction.check(
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
    EXPECT_TRUE(body->check(checker::Context{.target = {.ptx_version = {1, 0},
                                                        .sm_version = 0}})
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
      *(wrong_selector->functions.front().body.front());
  const auto& selector_variant =
      dynamic_cast<const SlctS32&>(selector_instruction);
  const auto bad_selector = selector_instruction.check(
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
      *(wide_selector->functions.front().body.front());
  const auto wide_selector_check = wide_selector_instruction.check(
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
  const auto& data_instruction = *(wrong_data->functions.front().body.front());
  const auto& data_variant = dynamic_cast<const SlctS32&>(data_instruction);
  const auto bad_data = data_instruction.check(
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
      *(integer_float_selector->functions.front().body.front());
  const auto integer_float_check = integer_float_instruction.check(
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
  EXPECT_TRUE(valid->functions.front()
                  .body.front()
                  ->check(checker::Context{
                      .target = {.ptx_version = {1, 0}, .sm_version = 0}})
                  .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .pred %p; .reg .u32 %dst; .reg .u64 %wide; selp.u32 %dst, %wide, 0, %p; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction = *(invalid->functions.front().body.front());
  const auto& variant = dynamic_cast<const SelpU32&>(instruction);
  const auto checked = instruction.check(
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
  EXPECT_TRUE(valid->functions.front()
                  .body.front()
                  ->check(checker::Context{
                      .target = {.ptx_version = {1, 0}, .sm_version = 0}})
                  .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .s32 %dst; .reg .f32 %wrong_src; cvt.s32.u32 %dst, %wrong_src; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction = *(invalid->functions.front().body.front());
  const auto& variant = dynamic_cast<const CvtS32U32&>(instruction);
  const auto checked = instruction.check(
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
  EXPECT_TRUE(valid->functions.front()
                  .body.front()
                  ->check(checker::Context{
                      .target = {.ptx_version = {1, 0}, .sm_version = 13}})
                  .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst, %wrong_src; cvt.rn.f32.f64 %dst, %wrong_src; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction = *(invalid->functions.front().body.front());
  const auto& variant = dynamic_cast<const CvtRnF32F64&>(instruction);
  const auto checked = instruction.check(
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
  EXPECT_TRUE(valid->functions.front().body[0]->check(context).has_value());
  EXPECT_TRUE(valid->functions.front().body[1]->check(context).has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .f32 %wrong_dst, %src; cvt.rzi.u32.f32 %wrong_dst, %src; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction = *(invalid->functions.front().body.front());
  const auto& variant = dynamic_cast<const CvtRziU32F32&>(instruction);
  const auto checked = instruction.check(context);
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
  const auto& scalar = *(valid->functions.front().body[0]);
  const auto& packed = *(valid->functions.front().body[1]);
  EXPECT_TRUE((scalar.instruction_kind() == InstructionKind::CvtRnF32S32));
  EXPECT_TRUE((packed.instruction_kind() == InstructionKind::CvtRnF16x2F32));
  EXPECT_TRUE(scalar
                  .check(checker::Context{
                      .target = {.ptx_version = {1, 0}, .sm_version = 0}})
                  .has_value());
  EXPECT_TRUE(packed
                  .check(checker::Context{
                      .target = {.ptx_version = {7, 0}, .sm_version = 80}})
                  .has_value());

  const auto parsed_module_2 = parseModule(
      ".entry kernel() { .reg .f16x2 %dst; .reg .f32 %a, %b; cvt.rn.f16x2.f32 "
      "%dst, %a, %b; }");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto packed_container = resolveModule(*parsed_module_2);
  ASSERT_TRUE(packed_container.has_value())
      << packed_container.error().front().message;
  EXPECT_TRUE(packed_container->functions.front()
                  .body.front()
                  ->check(checker::Context{
                      .target = {.ptx_version = {7, 0}, .sm_version = 80}})
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
    const auto checked = wrong->functions.front().body.front()->check(
        checker::Context{.target = {.ptx_version = {7, 0}, .sm_version = 80}});
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

/** All `cvt.pack` topologies retain their physical register type contracts. */
TEST(ResolvedModule, ChecksCvtPackOperandTypes) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %dst;
  .reg .s32 %a, %b;
  .reg .b32 %carry;
  cvt.pack.sat.u8.s32.b32 %dst, %a, %b, 0;
  cvt.pack.sat.u16.s32 %dst, %a, %b;
  cvt.pack.sat.s16.s32 %dst, %a, %b;
  cvt.pack.sat.s8.s32.b32 %dst, %a, %b, %carry;
  cvt.pack.sat.u4.s32.b32 %dst, %a, %b, 0;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto context =
      checker::Context{.target = {.ptx_version = {6, 5}, .sm_version = 75}};
  ASSERT_EQ(valid->functions.front().body.size(), 5u);
  EXPECT_TRUE((valid->functions.front().body[0]->instruction_kind() ==
               InstructionKind::CvtPackSatU8S32B32));
  EXPECT_TRUE((valid->functions.front().body[1]->instruction_kind() ==
               InstructionKind::CvtPackSat16S32));
  EXPECT_TRUE((valid->functions.front().body[3]->instruction_kind() ==
               InstructionKind::CvtPackSatSmallS32B32));
  for (const auto& resolved_instruction : valid->functions.front().body) {
    EXPECT_TRUE(resolved_instruction->check(context).has_value());
  }

  for (const auto source : {
           ".entry kernel() { .reg .u16 %dst; .reg .s32 %a, %b; .reg .b32 %c; "
           "cvt.pack.sat.u8.s32.b32 %dst, %a, %b, %c; }",
           ".entry kernel() { .reg .u32 %dst; .reg .s32 %a; .reg .f32 %b; .reg "
           ".b32 %c; "
           "cvt.pack.sat.u8.s32.b32 %dst, %a, %b, %c; }",
           ".entry kernel() { .reg .u32 %dst; .reg .s64 %a; .reg .s32 %b; .reg "
           ".b32 %c; "
           "cvt.pack.sat.u8.s32.b32 %dst, %a, %b, %c; }",
           ".entry kernel() { .reg .u32 %dst; .reg .s32 %a, %b; .reg .u64 %c; "
           "cvt.pack.sat.u8.s32.b32 %dst, %a, %b, %c; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_2 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
    const auto wrong = resolveModule(*parsed_module_2);
    ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
    const auto checked = wrong->functions.front().body.front()->check(context);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

TEST(ResolvedModule, ChecksCvtaNonSubspaceRegisterFormsAndWidths) {
  const auto parsed_module_1 = parseModule(R"ptx(
.entry kernel() {
  .reg .u32 %r<2>;
  .reg .u64 %rd<2>;
  cvta.global.u64 %rd0, %rd1;
  cvta.to.global.u64 %rd0, %rd1;
  cvta.global.u32 %r0, %r1;
  cvta.to.global.u32 %r0, %r1;
  cvta.local.u32 %r0, %r1;
  cvta.to.local.u32 %r0, %r1;
  cvta.local.u64 %rd0, %rd1;
  cvta.to.local.u64 %rd0, %rd1;
  cvta.shared.u32 %r0, %r1;
  cvta.to.shared.u32 %r0, %r1;
  cvta.shared.u64 %rd0, %rd1;
  cvta.to.shared.u64 %rd0, %rd1;
  cvta.const.u32 %r0, %r1;
  cvta.to.const.u32 %r0, %r1;
  cvta.const.u64 %rd0, %rd1;
  cvta.to.const.u64 %rd0, %rd1;
  cvta.param.u32 %r0, %r1;
  cvta.to.param.u32 %r0, %r1;
  cvta.param.u64 %rd0, %rd1;
  cvta.to.param.u64 %rd0, %rd1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const checker::Context context{
      .target = {.ptx_version = {7, 7}, .sm_version = 70}};
  ASSERT_EQ(valid->functions.front().body.size(), 20u);
  for (const auto& instruction : valid->functions.front().body) {
    EXPECT_TRUE(instruction->check(context).has_value());
  }

  /** One invalid spelling and the operand expected to receive its diagnostic. */
  struct InvalidCase {
    std::string_view source;
    bool destination_mismatch;
  };
  constexpr std::array invalid_cases{
      InvalidCase{".entry kernel() { .reg .u32 %r; .reg .u64 %rd; "
                  "cvta.global.u32 %rd, %r; }",
                  true},
      InvalidCase{".entry kernel() { .reg .u32 %r; .reg .u64 %rd; "
                  "cvta.global.u32 %r, %rd; }",
                  false},
      InvalidCase{".entry kernel() { .reg .u32 %r; .reg .u64 %rd; "
                  "cvta.global.u64 %r, %rd; }",
                  true},
      InvalidCase{".entry kernel() { .reg .u32 %r; .reg .u64 %rd; "
                  "cvta.global.u64 %rd, %r; }",
                  false},
  };
  for (const InvalidCase& test : invalid_cases) {
    SCOPED_TRACE(test.source);
    const auto parsed_module_2 = parseModule(test.source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
    const auto invalid = resolveModule(*parsed_module_2);
    ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
    const Instruction& instruction = *invalid->functions.front().body.front();
    const auto checked = instruction.check(context);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
    /** Compare the diagnostic against the selected concrete width's operand. */
    const auto check_range = [&](const auto& form) {
      EXPECT_EQ(checked.error().front().range, test.destination_mismatch
                                                   ? form.dst.locs.front()
                                                   : form.src.locs.front());
    };
    if (instruction.instruction_kind() == InstructionKind::CvtaGlobalU32)
      check_range(dynamic_cast<const CvtaGlobalU32&>(instruction));
    else
      check_range(dynamic_cast<const CvtaGlobalU64&>(instruction));
  }
}

/** `isspacep` accepts the documented 32-bit and 64-bit generic address containers. */
TEST(ResolvedModule, ChecksIsspacepAddressOperandTypes) {
  const auto parsed_module_1 = parseModule(R"ptx(
.version 9.3
.target sm_90
.entry kernel() {
  .reg .pred %p<8>;
  .reg .u32 %r;
  .reg .u64 %rd;
  .reg .b32 %b;
  .reg .b64 %bd;
  isspacep.global %p0, %r;
  isspacep.const %p1, %rd;
  isspacep.local %p2, %b;
  isspacep.shared %p3, %bd;
  isspacep.shared::cta %p4, %r;
  isspacep.shared::cluster %p5, %rd;
  isspacep.param %p6, %b;
  isspacep.param::entry %p7, %bd;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& body = valid->functions.front().body;
  ASSERT_EQ(body.size(), 8u);
  EXPECT_TRUE(
      (body[0]->instruction_kind() == InstructionKind::IsspacepGlobalU64));
  EXPECT_TRUE(
      (body[4]->instruction_kind() == InstructionKind::IsspacepSharedCta));
  EXPECT_TRUE(
      (body[5]->instruction_kind() == InstructionKind::IsspacepSharedCluster));
  EXPECT_TRUE(
      (body[7]->instruction_kind() == InstructionKind::IsspacepParamEntry));
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 90}};
  for (const auto& resolved_instruction : body) {
    EXPECT_TRUE(resolved_instruction->check(context).has_value());
  }

  for (const auto source : {
           ".version 9.3 .target sm_90 .entry kernel() { .reg .pred %p; .reg "
           ".u16 %h; isspacep.global %p, %h; }",
           ".version 9.3 .target sm_90 .entry kernel() { .reg .pred %p; .reg "
           ".f32 %f; isspacep.global %p, %f; }",
           ".version 9.3 .target sm_90 .entry kernel() { .reg .pred %p; .reg "
           ".f64 %fd; isspacep.global %p, %fd; }",
           ".version 9.3 .target sm_90 .entry kernel() { .reg .pred %p; .reg "
           ".b128 %b; isspacep.global %p, %b; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_2 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
    const auto wrong = resolveModuleOnly(*parsed_module_2);
    ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
    const auto checked = wrong->functions.front().body.front()->check(context);
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
  EXPECT_TRUE(
      valid->functions.front().body.front()->check(context).has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .u16 %dst, %src; mul.lo.u32 %dst, %src, 7; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_width = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_width.has_value()) << wrong_width.error().front().message;
  const auto& width_instruction =
      *(wrong_width->functions.front().body.front());
  const auto& width_variant = dynamic_cast<const MulLoU32&>(width_instruction);
  const auto width_checked = width_instruction.check(context);
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
  const auto& type_instruction = *(wrong_type->functions.front().body.front());
  const auto& type_variant = dynamic_cast<const MulLoU32&>(type_instruction);
  const auto type_checked = type_instruction.check(context);
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
  const auto& hi = *(body[0]);
  const auto& wide = *(body[1]);
  EXPECT_TRUE((hi.instruction_kind() == InstructionKind::MulHiU32));
  EXPECT_TRUE((wide.instruction_kind() == InstructionKind::MulWideU32));
  EXPECT_TRUE(hi.check(context).has_value());
  EXPECT_TRUE(wide.check(context).has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .u16 %dst, %src; mul.hi.u32 %dst, %src, %src; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto narrow_hi = resolveModule(*parsed_module_2);
  ASSERT_TRUE(narrow_hi.has_value()) << narrow_hi.error().front().message;
  const auto& narrow_instruction = *(narrow_hi->functions.front().body.front());
  const auto& narrow_variant =
      dynamic_cast<const MulHiU32&>(narrow_instruction);
  const auto narrow_checked = narrow_instruction.check(context);
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
      *(bit_wide_source->functions.front().body.front());
  const auto bit_checked = bit_instruction.check(context);
  EXPECT_TRUE(bit_checked.has_value()) << bit_checked.error().front().message;

  const auto parsed_module_4 = parseModule(R"ptx(
.entry kernel() { .reg .u32 %dst, %src; mul.wide.u32 %dst, %src, %src; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_4);
  const auto narrow_wide_dst = resolveModule(*parsed_module_4);
  ASSERT_TRUE(narrow_wide_dst.has_value())
      << narrow_wide_dst.error().front().message;
  const auto& dst_instruction =
      *(narrow_wide_dst->functions.front().body.front());
  const auto& dst_variant = dynamic_cast<const MulWideU32&>(dst_instruction);
  const auto dst_checked = dst_instruction.check(context);
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
  const auto& instruction = *(valid->functions.front().body.front());
  EXPECT_TRUE((instruction.instruction_kind() == InstructionKind::MulWideS32));
  EXPECT_TRUE(instruction.check(context).has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .b64 %rd0; .reg .b64 %rd1; .reg .b32 %r1; mul.wide.s32 %rd0, %rd1, %r1; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& invalid_instruction = *(invalid->functions.front().body.front());
  const auto& variant = dynamic_cast<const MulWideS32&>(invalid_instruction);
  const auto checked = invalid_instruction.check(context);
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
  EXPECT_TRUE(
      valid->functions.front().body.front()->check(context).has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst, %src2; .reg .f64 %wrong_src; mul.rn.f32 %dst, %wrong_src, %src2; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto invalid = resolveModule(*parsed_module_2);
  ASSERT_TRUE(invalid.has_value()) << invalid.error().front().message;
  const auto& instruction = *(invalid->functions.front().body.front());
  const auto& variant = dynamic_cast<const MulRnF32&>(instruction);
  const auto checked = instruction.check(context);
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
  EXPECT_TRUE(
      valid->functions.front().body.front()->check(context).has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .u16 %dst, %src1, %src3; mad.lo.u32 %dst, %src1, 7, %src3; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_width = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_width.has_value()) << wrong_width.error().front().message;
  const auto& width_instruction =
      *(wrong_width->functions.front().body.front());
  const auto& width_variant = dynamic_cast<const MadLoU32&>(width_instruction);
  const auto width_checked = width_instruction.check(context);
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
  const auto& type_instruction = *(wrong_type->functions.front().body.front());
  const auto& type_variant = dynamic_cast<const MadLoU32&>(type_instruction);
  const auto type_checked = type_instruction.check(context);
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
  const auto& lo = *(body[0]);
  const auto& wide = *(body[1]);
  const auto& rn = *(body[2]);
  EXPECT_TRUE((lo.instruction_kind() == InstructionKind::MadLoS32));
  EXPECT_TRUE((wide.instruction_kind() == InstructionKind::MadWideU32));
  EXPECT_TRUE((rn.instruction_kind() == InstructionKind::MadRnF32));
  EXPECT_TRUE(lo.check(checker::Context{
                           .target = {.ptx_version = {1, 0}, .sm_version = 0}})
                  .has_value());
  EXPECT_TRUE(wide.check(checker::Context{.target = {.ptx_version = {1, 0},
                                                     .sm_version = 0}})
                  .has_value());
  EXPECT_TRUE(rn.check(checker::Context{
                           .target = {.ptx_version = {2, 0}, .sm_version = 20}})
                  .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .u64 %dst, %addend; .reg .b32 %src; mad.wide.u32 %dst, %src, %src, %addend; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto bit_wide_source = resolveModule(*parsed_module_2);
  ASSERT_TRUE(bit_wide_source.has_value())
      << bit_wide_source.error().front().message;
  const auto& bit_instruction =
      *(bit_wide_source->functions.front().body.front());
  const auto bit_checked = bit_instruction.check(
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
      *(narrow_wide_addend->functions.front().body.front());
  const auto& addend_variant =
      dynamic_cast<const MadWideU32&>(addend_instruction);
  const auto addend_checked = addend_instruction.check(
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
      *(narrow_wide_dst->functions.front().body.front());
  const auto& dst_variant = dynamic_cast<const MadWideU32&>(dst_instruction);
  const auto dst_checked = dst_instruction.check(
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
      *(bit_float_source->functions.front().body.front());
  const auto float_checked = float_instruction.check(
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
  EXPECT_TRUE(body[0]
                  ->check(checker::Context{
                      .target = {.ptx_version = {2, 0}, .sm_version = 20}})
                  .has_value());
  EXPECT_TRUE(body[1]
                  ->check(checker::Context{
                      .target = {.ptx_version = {1, 4}, .sm_version = 13}})
                  .has_value());
  EXPECT_TRUE(body[2]
                  ->check(checker::Context{
                      .target = {.ptx_version = {8, 6}, .sm_version = 100}})
                  .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst, %src2, %src3; .reg .u32 %wrong_src; fma.rn.f32 %dst, %wrong_src, %src2, %src3; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_scalar = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_scalar.has_value()) << wrong_scalar.error().front().message;
  const auto& instruction = *(wrong_scalar->functions.front().body.front());
  const auto& variant = dynamic_cast<const FmaRnF32&>(instruction);
  const auto checked = instruction.check(
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
      *(wrong_packed->functions.front().body.front());
  const auto& packed_variant =
      dynamic_cast<const FmaF32x2&>(packed_instruction);
  const auto packed_checked = packed_instruction.check(
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
    EXPECT_TRUE(body[index]->check(contexts[index]).has_value()) << index;
  }

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .b16 %dst, %src2, %src3; .reg .f16 %wrong_src; fma.rn.bf16 %dst, %wrong_src, %src2, %src3; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_bfloat = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_bfloat.has_value()) << wrong_bfloat.error().front().message;
  const auto& bfloat_instruction =
      *(wrong_bfloat->functions.front().body.front());
  const auto& bfloat_variant = dynamic_cast<const FmaBf16&>(bfloat_instruction);
  const auto bfloat_checked = bfloat_instruction.check(
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
      *(wrong_packed_bfloat->functions.front().body.front());
  const auto& packed_bfloat_variant =
      dynamic_cast<const FmaBf16x2&>(packed_bfloat_instruction);
  const auto packed_bfloat_checked = packed_bfloat_instruction.check(
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
      *(wrong_mixed->functions.front().body.front());
  const auto& mixed_variant =
      dynamic_cast<const FmaMixedF32Bf16&>(mixed_instruction);
  const auto mixed_checked = mixed_instruction.check(
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
    const auto checked = body[index]->check(context);
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
  EXPECT_TRUE(
      valid->functions.front().body.front()->check(context).has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .u16 %dst, %src; div.u32 %dst, %src, 0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_width = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_width.has_value()) << wrong_width.error().front().message;
  const auto& width_instruction =
      *(wrong_width->functions.front().body.front());
  const auto& width_variant = dynamic_cast<const DivU32&>(width_instruction);
  const auto width_checked = width_instruction.check(context);
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
  const auto& type_instruction = *(wrong_type->functions.front().body.front());
  const auto& type_variant = dynamic_cast<const DivU32&>(type_instruction);
  const auto type_checked = type_instruction.check(context);
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
  const auto& s32 = *(body[0]);
  const auto& f32 = *(body[1]);
  const auto& f64 = *(body[2]);
  EXPECT_TRUE((s32.instruction_kind() == InstructionKind::DivS32));
  EXPECT_TRUE((f32.instruction_kind() == InstructionKind::DivRnF32));
  EXPECT_TRUE((f64.instruction_kind() == InstructionKind::DivRnF64));
  EXPECT_TRUE(s32.check(checker::Context{
                            .target = {.ptx_version = {1, 0}, .sm_version = 0}})
                  .has_value());
  EXPECT_TRUE(f32.check(checker::Context{.target = {.ptx_version = {1, 4},
                                                    .sm_version = 20}})
                  .has_value());
  EXPECT_TRUE(f64.check(checker::Context{.target = {.ptx_version = {1, 4},
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
      *(bit_f32_source->functions.front().body.front());
  const auto f32_checked = f32_instruction.check(
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
      *(bit_f64_source->functions.front().body.front());
  const auto f64_checked = f64_instruction.check(
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
  const auto& signed_rem = *(body[0]);
  const auto& unsigned_rem = *(body[1]);
  EXPECT_TRUE((signed_rem.instruction_kind() == InstructionKind::RemS32));
  EXPECT_TRUE((unsigned_rem.instruction_kind() == InstructionKind::RemU32));
  const checker::Context context{
      .target = {.ptx_version = {1, 0}, .sm_version = 0}};
  EXPECT_TRUE(signed_rem.check(context).has_value());
  EXPECT_TRUE(unsigned_rem.check(context).has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .u32 %dst; .reg .f32 %src; rem.u32 %dst, %src, 0; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_type = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_type.has_value()) << wrong_type.error().front().message;
  const auto& type_instruction = *(wrong_type->functions.front().body.front());
  const auto& type_variant = dynamic_cast<const RemU32&>(type_instruction);
  const auto type_checked = type_instruction.check(context);
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
      *(wrong_width->functions.front().body.front());
  const auto& width_variant = dynamic_cast<const RemS32&>(width_instruction);
  const auto width_checked = width_instruction.check(context);
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
  const auto& integer_min = *(body[0]);
  const auto& nan_min = *(body[1]);
  EXPECT_TRUE((integer_min.instruction_kind() == InstructionKind::MinS32));
  EXPECT_TRUE((nan_min.instruction_kind() == InstructionKind::MinF32));
  EXPECT_TRUE(integer_min
                  .check(checker::Context{
                      .target = {.ptx_version = {1, 0}, .sm_version = 0}})
                  .has_value());
  EXPECT_TRUE(nan_min
                  .check(checker::Context{
                      .target = {.ptx_version = {7, 0}, .sm_version = 80}})
                  .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .s32 %dst, %src2; .reg .f32 %src1; min.s32 %dst, %src1, %src2; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_integer_type = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_integer_type.has_value())
      << wrong_integer_type.error().front().message;
  const auto& integer_instruction =
      *(wrong_integer_type->functions.front().body.front());
  const auto& integer_variant =
      dynamic_cast<const MinS32&>(integer_instruction);
  const auto integer_checked = integer_instruction.check(
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
      *(bit_nan_source->functions.front().body.front());
  const auto nan_checked = nan_instruction.check(
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
  const auto& integer_max = *(body[0]);
  const auto& nan_max = *(body[1]);
  EXPECT_TRUE((integer_max.instruction_kind() == InstructionKind::MaxS32));
  EXPECT_TRUE((nan_max.instruction_kind() == InstructionKind::MaxF32));
  EXPECT_TRUE(integer_max
                  .check(checker::Context{
                      .target = {.ptx_version = {1, 0}, .sm_version = 0}})
                  .has_value());
  EXPECT_TRUE(nan_max
                  .check(checker::Context{
                      .target = {.ptx_version = {7, 0}, .sm_version = 80}})
                  .has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .s32 %dst, %src2; .reg .f32 %src1; max.s32 %dst, %src1, %src2; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_integer_type = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_integer_type.has_value())
      << wrong_integer_type.error().front().message;
  const auto& integer_instruction =
      *(wrong_integer_type->functions.front().body.front());
  const auto& integer_variant =
      dynamic_cast<const MaxS32&>(integer_instruction);
  const auto integer_checked = integer_instruction.check(
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
      *(bit_nan_source->functions.front().body.front());
  const auto nan_checked = nan_instruction.check(
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
  const auto& integer_abs = *(body[0]);
  const auto& float_abs = *(body[1]);
  EXPECT_TRUE((integer_abs.instruction_kind() == InstructionKind::AbsS32));
  EXPECT_TRUE((float_abs.instruction_kind() == InstructionKind::AbsF32));
  const checker::Context context{
      .target = {.ptx_version = {1, 0}, .sm_version = 0}};
  EXPECT_TRUE(integer_abs.check(context).has_value());
  EXPECT_TRUE(float_abs.check(context).has_value());

  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .f32 %dst; .reg .s32 %src; abs.f32 %dst, %src; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_type = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_type.has_value()) << wrong_type.error().front().message;
  const auto& instruction = *(wrong_type->functions.front().body.front());
  const auto& variant = dynamic_cast<const AbsF32&>(instruction);
  const auto checked = instruction.check(context);
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
  const auto& integer_neg = *(body[0]);
  const auto& float_neg = *(body[1]);
  const auto& packed_neg = *(body[2]);
  EXPECT_TRUE((integer_neg.instruction_kind() == InstructionKind::NegS32));
  EXPECT_TRUE((float_neg.instruction_kind() == InstructionKind::NegF32));
  EXPECT_TRUE((packed_neg.instruction_kind() == InstructionKind::NegF16x2));
  EXPECT_TRUE(integer_neg
                  .check(checker::Context{
                      .target = {.ptx_version = {1, 0}, .sm_version = 0}})
                  .has_value());
  EXPECT_TRUE(float_neg
                  .check(checker::Context{
                      .target = {.ptx_version = {1, 0}, .sm_version = 0}})
                  .has_value());
  EXPECT_TRUE(packed_neg
                  .check(checker::Context{
                      .target = {.ptx_version = {6, 0}, .sm_version = 53}})
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
        *(wrong_container->functions.front().body.front());
    const auto checked = instruction.check(
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
  const auto& instruction = *(valid->functions.front().body.front());
  EXPECT_TRUE((instruction.instruction_kind() == InstructionKind::Lop3B32));
  EXPECT_TRUE(instruction
                  .check(checker::Context{
                      .target = {.ptx_version = {4, 3}, .sm_version = 50}})
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
  EXPECT_TRUE((body[0]->instruction_kind() == InstructionKind::ShfLClampB32));
  EXPECT_TRUE((body[1]->instruction_kind() == InstructionKind::ShfRWrapB32));
  const auto parsed_module_2 = parseModule(R"ptx(
.entry kernel() { .reg .u16 %dst; .reg .u32 %a, %b; shf.l.clamp.b32 %dst, %a, %b, 8; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong_type = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong_type.has_value()) << wrong_type.error().front().message;
  const auto checked = wrong_type->functions.front().body.front()->check(
      checker::Context{.target = {.ptx_version = {3, 1}, .sm_version = 32}});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

/** Generic and specialized `prmt` selectors retain their b32 register contract. */
TEST(ResolvedModule, ChecksPrmtRegisterWidths) {
  const auto parsed_module_1 = parseModule(
      ".entry kernel() { .reg .u32 %r0, %r1, %r2, %r3; prmt.b32 %r0, %r1, %r2, "
      "0x5410; prmt.b32 %r0, %r1, %r2, %r3; prmt.b32.f4e %r0, %r1, %r2, %r3; "
      "prmt.b32.b4e %r0, %r1, %r2, %r3; prmt.b32.rc8 %r0, %r1, %r2, %r3; "
      "prmt.b32.ecl %r0, %r1, %r2, %r3; prmt.b32.ecr %r0, %r1, %r2, %r3; "
      "prmt.b32.rc16 %r0, %r1, %r2, %r3; }");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto context =
      checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}};
  for (const auto& resolved_instruction : valid->functions.front().body) {
    EXPECT_TRUE(resolved_instruction->check(context).has_value());
  }
  const auto parsed_module_2 = parseModule(
      ".entry kernel() { .reg .u16 %r0; .reg .u32 %r1, %r2; prmt.b32 %r0, %r1, "
      "%r2, 0; }");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
  const auto wrong = resolveModule(*parsed_module_2);
  ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
  EXPECT_FALSE(
      wrong->functions.front().body.front()->check(context).has_value());
}

TEST(ResolvedModule, ChecksM12PopcTypes) {
  const auto context =
      checker::Context{.target = {.ptx_version = {2, 0}, .sm_version = 20}};
  const auto parsed_module_1 = parseModule(
      ".entry kernel() { .reg .u32 %dst, %src; popc.b32 %dst, %src; }");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_1);
  const auto valid = resolveModule(*parsed_module_1);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  const auto& instruction = *(valid->functions.front().body.front());
  EXPECT_TRUE((instruction.instruction_kind() == InstructionKind::PopcB32));
  EXPECT_TRUE(instruction.check(context).has_value());
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
    const auto checked = wrong->functions.front().body.front()->check(context);
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
  EXPECT_TRUE(valid->functions.front().body[0]->check(context).has_value());
  EXPECT_TRUE(valid->functions.front().body[1]->check(context).has_value());
  for (const auto source : {
           ".entry kernel() { .reg .u64 %dst, %src; clz.b64 %dst, %src; }",
           ".entry kernel() { .reg .u32 %dst; .reg .u16 %src; clz.b32 %dst, "
           "%src; }",
       }) {
    const auto parsed_module_2 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
    const auto wrong = resolveModule(*parsed_module_2);
    ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
    const auto checked = wrong->functions.front().body.front()->check(context);
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
      valid->functions.front().body.front()->check(context).has_value());
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
    EXPECT_TRUE(
        compatible->functions.front().body.front()->check(context).has_value());
  }
  for (const auto source : {
           ".entry kernel() { .reg .u64 %dst, %src; bfind.shiftamt.u32 %dst, "
           "%src; }",
       }) {
    const auto parsed_module_3 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_3);
    const auto wrong = resolveModule(*parsed_module_3);
    ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
    const auto checked = wrong->functions.front().body.front()->check(context);
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
  EXPECT_TRUE(
      valid->functions.front().body.front()->check(context).has_value());
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
        compatible->functions.front().body.front()->check(context).has_value());
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
    const auto checked = wrong->functions.front().body.front()->check(context);
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
  EXPECT_TRUE(
      valid->functions.front().body.front()->check(context).has_value());
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
    const auto checked = wrong->functions.front().body.front()->check(context);
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
      valid->functions.front().body.front()->check(context).has_value());
  for (const auto source : {
           ".entry kernel() { .reg .u16 %dst, %src; brev.b32 %dst, %src; }",
           ".entry kernel() { .reg .b64 %dst, %src; brev.b32 %dst, %src; }",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module_2 = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module_2);
    const auto wrong = resolveModule(*parsed_module_2);
    ASSERT_TRUE(wrong.has_value()) << wrong.error().front().message;
    const auto checked = wrong->functions.front().body.front()->check(context);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::OperandTypeMismatch);
  }
}

/** Expanded bit operations retain owned operands and validate after AST expiry. */
TEST(ResolvedModule, RetainsExpandedBitOperationsAndRevalidatesMutations) {
  std::optional<ResolvedModule> owned_module;
  {
    const auto parsed_module = parseModule(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .u32 %r<8>;
  .reg .u64 %rd<6>;
  .reg .s64 %srd;
  popc.b64 %r0, 1;
  clz.b64 %r1, 1;
  bfind.shiftamt.s64 %r2, -1;
  bfe.s64 %srd, -1, %r3, %r4;
  bfi.b64 %rd0, 1, 2, %r5, %r6;
  brev.b64 %rd1, 1;
}
)ptx");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module);
    auto resolved = resolveModule(*parsed_module);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned_module.emplace(std::move(*resolved));
  }

  ASSERT_EQ(owned_module->functions.size(), 1u);
  auto& body = owned_module->functions.front().body;
  ASSERT_EQ(body.size(), 6u);
  EXPECT_TRUE(validateModule(*owned_module,
                             ModuleValidationPolicy::RequireCompleteContext)
                  .has_value());

  auto& bfind = dynamic_cast<BfindShiftamtS64&>(*body[2]);
  bfind.dst.value.declared_type = ScalarType::U64;
  const auto invalid_destination = validateModule(
      *owned_module, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid_destination.has_value());
  EXPECT_TRUE(
      std::ranges::any_of(invalid_destination.error(), [](const auto& issue) {
        return issue.kind == checker::CheckDiagnosticKind::OperandTypeMismatch;
      }));
  bfind.dst.value.declared_type = ScalarType::U32;

  auto& bfe = dynamic_cast<BfeS64&>(*body[3]);
  auto& offset_register = std::get<ResolvedRegisterRef>(bfe.offset.value);
  offset_register.declared_type = ScalarType::U64;
  const auto invalid_control = validateModule(
      *owned_module, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid_control.has_value());
  EXPECT_TRUE(
      std::ranges::any_of(invalid_control.error(), [](const auto& issue) {
        return issue.kind == checker::CheckDiagnosticKind::OperandTypeMismatch;
      }));
}

/** Verify every logic-and-shift form survives module resolution independently
 * of the syntax AST that produced it. */
TEST(ResolvedModule, ChecksIssue144LogicAndShiftFormsAfterAstLifetime) {
  std::optional<ResolvedModule> owned_module;
  {
    const auto parsed_module = parseModule(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .pred %p<4>;
  .reg .b16 %h<4>;
  .reg .b32 %r<4>;
  .reg .b64 %rd<4>;
  .reg .u16 %uh<2>;
  .reg .u32 %ur<2>;
  .reg .u64 %urd<2>;
  .reg .s16 %sh<2>;
  .reg .s32 %sr<2>;
  .reg .s64 %srd<2>;
  and.pred %p0, !%p1, 1;
  and.b16 %h0, %h1, 1; and.b32 %r0, %r1, 1; and.b64 %rd0, %rd1, 1;
  or.pred %p0, %p1, 0;
  or.b16 %h0, %h1, 1; or.b32 %r0, %r1, 1; or.b64 %rd0, %rd1, 1;
  xor.pred %p0, %p1, 1;
  xor.b16 %h0, %h1, 1; xor.b32 %r0, %r1, 1; xor.b64 %rd0, %rd1, 1;
  not.pred %p0, !%p1;
  not.b16 %h0, %h1; not.b32 %r0, %r1; not.b64 %rd0, %rd1;
  cnot.b16 %h0, %h1; cnot.b32 %r0, %r1; cnot.b64 %rd0, %rd1;
  lop3.b32 %r0, 1, %r1, 2, 0;
  lop3.and.b32 _ | %p0, 1, %r1, 2, 255, !%p1;
  lop3.or.b32 %r0 | %p0, 1, %r1, 2, 0, %p1;
  shf.l.clamp.b32 %r0, 1, %r1, 32;
  shf.l.wrap.b32 %r0, 1, %r1, 33;
  shf.r.clamp.b32 %r0, 1, %r1, 32;
  shf.r.wrap.b32 %r0, 1, %r1, 33;
  shl.b16 %h0, %h1, 32; shl.b32 %r0, %r1, 32; shl.b64 %rd0, %rd1, 32;
  shr.b16 %h0, %h1, 32; shr.b32 %r0, %r1, 32; shr.b64 %rd0, %rd1, 32;
  shr.u16 %uh0, %uh1, 32; shr.u32 %ur0, %ur1, 32; shr.u64 %urd0, %urd1, 32;
  shr.s16 %sh0, %sh1, 32; shr.s32 %sr0, %sr1, 32; shr.s64 %srd0, %srd1, 32;
}
)ptx");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module);
    auto resolved = resolveModule(*parsed_module);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned_module.emplace(std::move(*resolved));
  }

  ASSERT_EQ(owned_module->functions.size(), 1u);
  const auto& body = owned_module->functions.front().body;
  ASSERT_EQ(body.size(), 38u);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 100}};
  for (const auto& instruction : body) {
    EXPECT_TRUE(instruction->check(context).has_value());
  }
}

/** Verify module checking enforces logic-and-shift operand-width contracts. */
TEST(ResolvedModule, ChecksIssue144LogicAndShiftWidthAndCountContracts) {
  constexpr std::array cases{
      std::pair{std::string_view{R"ptx(.entry kernel() {
  .reg .b16 %h; .reg .b32 %r0, %r1; and.b16 %h, %r0, %r1;
})ptx"},
                false},
      std::pair{std::string_view{R"ptx(.entry kernel() {
  .reg .f32 %f; .reg .u32 %r; shr.s32 %r, %f, 1;
})ptx"},
                false},
      std::pair{std::string_view{R"ptx(.entry kernel() {
  .reg .f32 %f; .reg .u32 %r; shr.b32 %r, %f, 1;
})ptx"},
                true},
      std::pair{std::string_view{R"ptx(.entry kernel() {
  .reg .b32 %r0, %r1; .reg .b64 %rd; shl.b32 %r0, %r1, %rd;
})ptx"},
                false},
      std::pair{std::string_view{R"ptx(.entry kernel() {
  .reg .b32 %r0, %r1; .reg .f32 %f; shl.b32 %r0, %r1, %f;
})ptx"},
                false},
  };
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 100}};
  for (const auto& [source, expected_valid] : cases) {
    SCOPED_TRACE(source);
    const auto parsed_module = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module);
    const auto resolved = resolveModule(*parsed_module);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    ASSERT_EQ(resolved->functions.front().body.size(), 1u);
    EXPECT_EQ(
        resolved->functions.front().body.front()->check(context).has_value(),
        expected_valid);
  }
}

/** Verify resolver rejects layouts reserved for lop3 Boolean-result forms. */
TEST(ResolvedModule, RejectsIssue144InvalidLop3AndPredicateLayouts) {
  for (const std::string_view source : {
           R"ptx(.entry kernel() {
  .reg .pred %p; .reg .b32 %r0, %r1; lop3.and.b32 %r0 | _, %r0, 1, %r1, 0, %p;
})ptx",
           R"ptx(.entry kernel() {
  .reg .b32 %r0, %r1; lop3.b32 _, %r0, 1, %r1, 0;
})ptx",
           R"ptx(.entry kernel() {
  .reg .pred %p; .reg .b32 %r0, %r1; lop3.xor.b32 %r0 | %p, %r0, 1, %r1, 0, %p;
})ptx",
           R"ptx(.entry kernel() {
  .reg .pred %p0, %p1, %p2; and.pred !%p0, %p1, %p2;
})ptx",
       }) {
    SCOPED_TRACE(source);
    const auto parsed_module = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module);
    EXPECT_FALSE(resolveModule(*parsed_module).has_value());
  }
}

/** Verify the lop3 truth-table control is constrained to its u8 range. */
TEST(ResolvedModule, RejectsIssue144OutOfRangeLop3TruthTables) {
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 100}};
  for (const std::string_view lut : {"256", "-1"}) {
    SCOPED_TRACE(lut);
    const std::string source =
        ".entry kernel() { .reg .b32 %r0, %r1, %r2, %r3; lop3.b32 %r0, "
        "%r1, %r2, %r3, " +
        std::string(lut) + "; }";
    const auto parsed_module = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module);
    const auto resolved = resolveModule(*parsed_module);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    const auto checked =
        resolved->functions.front().body.front()->check(context);
    ASSERT_FALSE(checked.has_value());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::ImmediateValueMismatch);
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
