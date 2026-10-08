#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/model/control_flow/brkpt.gen.hpp>
#include <ptx_frontend/resolved_ir/model/control_flow/nanosleep.gen.hpp>
#include <ptx_frontend/resolved_ir/model/control_flow/pmevent.gen.hpp>
#include <ptx_frontend/resolved_ir/model/control_flow/setmaxnreg.gen.hpp>
#include <ptx_frontend/resolved_ir/model/control_flow/trap.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker_support.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Preserve each miscellaneous operation and source layout in owned IR. */
TEST(MiscellaneousCoverage, ResolvesFamilyForms) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90a
.entry kernel() {
  .reg .u32 %r;
  .reg .pred %p;
  @%p brkpt;
  nanosleep.u32 %r;
  nanosleep.u32 42;
  pmevent 0;
  pmevent 15;
  pmevent.mask 0;
  pmevent.mask 0x8001;
  pmevent.mask 65535;
  @%p trap;
  setmaxnreg.inc.sync.aligned.u32 192;
  setmaxnreg.dec.sync.aligned.u32 64;
}

)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 11u);

  EXPECT_EQ(body[0]->instruction_kind(), InstructionKind::BrkptBare);
  const auto& breakpoint = dynamic_cast<const BrkptBare&>(*body[0]);
  EXPECT_FALSE(breakpoint
                   .check(checker::Context{
                       .target = {.ptx_version = {1, 0}, .sm_version = 10}})
                   .has_value());
  EXPECT_TRUE(breakpoint
                  .check(checker::Context{
                      .target = {.ptx_version = {1, 0}, .sm_version = 11}})
                  .has_value());
  const auto& nanosleep_reg = dynamic_cast<const NanosleepU32&>(*body[1]);
  const auto& nanosleep_imm = dynamic_cast<const NanosleepU32&>(*body[2]);
  EXPECT_TRUE(
      std::holds_alternative<ResolvedRegisterRef>(nanosleep_reg.t.value));
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(nanosleep_imm.t.value));
  EXPECT_EQ(std::get<ResolvedImmediate>(nanosleep_imm.t.value).bits, 42u);
  EXPECT_EQ(dynamic_cast<const PmeventIndex&>(*body[3]).a.value.bits, 0u);
  EXPECT_EQ(dynamic_cast<const PmeventIndex&>(*body[4]).a.value.bits, 15u);
  const auto& zero_mask = dynamic_cast<const PmeventMask&>(*body[5]);
  EXPECT_EQ(zero_mask.a.value.bits, 0u);
  const auto& mask = dynamic_cast<const PmeventMask&>(*body[6]);
  EXPECT_TRUE(PmeventMask::mask);
  EXPECT_EQ(mask.a.value.bits, 0x8001u);
  EXPECT_EQ(dynamic_cast<const PmeventMask&>(*body[7]).a.value.bits, 65535u);
  EXPECT_EQ(body[8]->opcode_kind(), Opcode::Trap);
  const auto& increase =
      dynamic_cast<const SetmaxnregIncSyncAlignedU32&>(*body[9]);
  const auto& decrease =
      dynamic_cast<const SetmaxnregDecSyncAlignedU32&>(*body[10]);
  EXPECT_TRUE(SetmaxnregIncSyncAlignedU32::inc);
  EXPECT_TRUE(SetmaxnregDecSyncAlignedU32::dec);
  EXPECT_EQ(increase.count.value.bits, 192u);
  EXPECT_EQ(decrease.count.value.bits, 64u);
}

/** Integer source constants narrow to the `.u32` duration use without loss of source metadata. */
TEST(MiscellaneousCoverage, NanosleepIntegerSourceNarrowing) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_80
.entry kernel() {
  .reg .s32 %s;
  nanosleep.u32 -1;
  nanosleep.u32 4294967296;
  mov.s32 %s, -1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 3u);
  const auto& negative = std::get<ResolvedImmediate>(
      dynamic_cast<const NanosleepU32&>(*body[0]).t.value);
  EXPECT_EQ(negative.bits, UINT32_MAX);
  EXPECT_EQ(negative.integer_source_bits, UINT64_MAX);
  EXPECT_TRUE(negative.is_negative);
  const auto& wrapped = std::get<ResolvedImmediate>(
      dynamic_cast<const NanosleepU32&>(*body[1]).t.value);
  EXPECT_EQ(wrapped.bits, 0u);
  EXPECT_EQ(wrapped.integer_source_bits, 0x100000000ULL);
  EXPECT_FALSE(wrapped.is_negative);
}

/** Reject wrong operand kinds, missing qualifiers, and literal bounds. */
TEST(MiscellaneousCoverage, RejectsInvalidForms) {
  constexpr std::string_view prefix = R"ptx(
.version 9.3
.target sm_90a
.entry kernel() {
  .reg .u32 %r;
  .reg .pred %p;
)ptx";
  for (const std::string_view invalid : {
           "brkpt 1;",
           "brkpt.u32;",
           "nanosleep.s32 %r;",
           "nanosleep.u32 %p;",
           "pmevent %r;",
           "pmevent 16;",
           "pmevent.mask %r;",
           "pmevent.mask -1;",
           "pmevent.mask 65536;",
           "setmaxnreg.dec.sync.aligned.u32 %r;",
           "setmaxnreg.dec.sync.aligned.u32 16;",
           "setmaxnreg.dec.sync.aligned.u32 25;",
           "setmaxnreg.dec.sync.aligned.u32 264;",
           "setmaxnreg.dec.aligned.u32 64;",
           "setmaxnreg.dec.sync.u32 64;",
           "setmaxnreg.dec.sync.aligned.s32 64;",
       }) {
    SCOPED_TRACE(invalid);
    const auto parsed = test_helpers::parseModule(
        std::string{prefix} + std::string{invalid} + "\n}\n");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveAndValidateModule(*parsed).has_value());
  }
}

/** Apply PTX and target floors from the module rather than source spelling. */
TEST(MiscellaneousCoverage, ModuleAvailability) {
  for (const std::string_view module : {
           ".version 1.4 .target sm_13 .entry k() { pmevent 15; }",
           ".version 3.0 .target sm_20 .entry k() { pmevent.mask 1; }",
           ".version 6.3 .target sm_70 .entry k() { nanosleep.u32 0; }",
           ".version 8.0 .target sm_90a .entry k() { "
           "setmaxnreg.dec.sync.aligned.u32 24; }",
           ".version 8.6 .target sm_100a .entry k() { "
           "setmaxnreg.dec.sync.aligned.u32 64; }",
           ".version 8.7 .target sm_120a .entry k() { "
           "setmaxnreg.inc.sync.aligned.u32 256; }",
           ".version 8.8 .target sm_100f .entry k() { "
           "setmaxnreg.dec.sync.aligned.u32 64; }",
           ".version 8.8 .target sm_120f .entry k() { "
           "setmaxnreg.inc.sync.aligned.u32 64; }",
           ".version 8.8 .target sm_103a .entry k() { "
           "setmaxnreg.dec.sync.aligned.u32 64; }",
           ".version 8.8 .target sm_103f .entry k() { "
           "setmaxnreg.inc.sync.aligned.u32 64; }",
           ".version 9.0 .target sm_110a .entry k() { "
           "setmaxnreg.inc.sync.aligned.u32 64; }",
           ".version 9.0 .target sm_110f .entry k() { "
           "setmaxnreg.dec.sync.aligned.u32 64; }",
           ".version 9.3 .target sm_121a .entry k() { "
           "setmaxnreg.dec.sync.aligned.u32 64; }",
           ".version 9.3 .target sm_121f .entry k() { "
           "setmaxnreg.inc.sync.aligned.u32 64; }",
       }) {
    SCOPED_TRACE(module);
    const auto parsed = test_helpers::parseModule(module);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_TRUE(resolveAndValidateModule(*parsed).has_value());
  }
  for (const std::string_view module : {
           ".version 2.9 .target sm_20 .entry k() { pmevent.mask 1; }",
           ".version 3.0 .target sm_13 .entry k() { pmevent.mask 1; }",
           ".version 6.2 .target sm_70 .entry k() { nanosleep.u32 0; }",
           ".version 6.3 .target sm_30 .entry k() { nanosleep.u32 0; }",
           ".version 7.9 .target sm_90a .entry k() { "
           "setmaxnreg.dec.sync.aligned.u32 64; }",
           ".version 8.5 .target sm_100a .entry k() { "
           "setmaxnreg.dec.sync.aligned.u32 64; }",
           ".version 8.6 .target sm_120a .entry k() { "
           "setmaxnreg.inc.sync.aligned.u32 64; }",
           ".version 8.7 .target sm_100f .entry k() { "
           "setmaxnreg.dec.sync.aligned.u32 64; }",
           ".version 8.7 .target sm_120f .entry k() { "
           "setmaxnreg.inc.sync.aligned.u32 64; }",
           ".version 8.9 .target sm_110a .entry k() { "
           "setmaxnreg.inc.sync.aligned.u32 64; }",
           ".version 8.9 .target sm_110f .entry k() { "
           "setmaxnreg.dec.sync.aligned.u32 64; }",
           ".version 9.3 .target sm_90 .entry k() { "
           "setmaxnreg.inc.sync.aligned.u32 64; }",
           ".version 9.3 .target sm_100 .entry k() { "
           "setmaxnreg.dec.sync.aligned.u32 64; }",
           ".version 9.3 .target sm_103 .entry k() { "
           "setmaxnreg.inc.sync.aligned.u32 64; }",
           ".version 9.3 .target sm_110 .entry k() { "
           "setmaxnreg.dec.sync.aligned.u32 64; }",
           ".version 9.3 .target sm_120 .entry k() { "
           "setmaxnreg.inc.sync.aligned.u32 64; }",
           ".version 9.3 .target sm_121 .entry k() { "
           "setmaxnreg.dec.sync.aligned.u32 64; }",
       }) {
    SCOPED_TRACE(module);
    const auto parsed = test_helpers::parseModule(module);
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveAndValidateModule(*parsed).has_value());
  }
}

/** Owned literal metadata remains checked after the source AST is destroyed. */
TEST(MiscellaneousCoverage, OwnedLiteralTamperIsRejected) {
  std::optional<ResolvedModule> owned;
  {
    const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90a
.entry k() {
  pmevent 15;
  pmevent.mask 0;
  setmaxnreg.dec.sync.aligned.u32 64;
}
)ptx");
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveAndValidateModule(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned.emplace(std::move(*resolved));
  }
  /** Revalidate owned metadata without consulting the discarded source AST. */
  const auto accepted = [&owned] {
    return validateModule(*owned,
                          ModuleValidationPolicy::RequireCompleteContext)
        .has_value();
  };
  EXPECT_TRUE(accepted());
  auto& event = dynamic_cast<PmeventIndex&>(*owned->functions.front().body[0]);
  event.a.value.bits = 16;
  EXPECT_FALSE(accepted());
  event.a.value.bits = 15;
  event.a.value.integer_source_bits = 16;
  EXPECT_FALSE(accepted());
  event.a.value.bits = 16;
  EXPECT_FALSE(accepted());
  event.a.value.bits = 15;
  event.a.value.integer_source_bits = 15;
  EXPECT_TRUE(accepted());

  auto& mask = dynamic_cast<PmeventMask&>(*owned->functions.front().body[1]);
  mask.a.value.bits = 65536;
  EXPECT_FALSE(accepted());
  mask.a.value.bits = 0;
  mask.a.value.integer_source_bits = 65536;
  EXPECT_FALSE(accepted());
  mask.a.value.bits = 65536;
  EXPECT_FALSE(accepted());
  mask.a.value.bits = 0;
  mask.a.value.integer_source_bits = 0;
  EXPECT_TRUE(accepted());

  auto& decrease = dynamic_cast<SetmaxnregDecSyncAlignedU32&>(
      *owned->functions.front().body[2]);
  decrease.count.value.bits = 65;
  EXPECT_FALSE(accepted());
  decrease.count.value.bits = 64;
  decrease.count.value.integer_source_bits = 65;
  EXPECT_FALSE(accepted());
  decrease.count.value.bits = 65;
  EXPECT_FALSE(accepted());
}

/** Fixed-value checks reject stale integer bits without changing other conversions. */
TEST(MiscellaneousCoverage, FixedValueChecksImmediateConsistency) {
  static constexpr std::array<uint64_t, 3> allowed_values{15, UINT64_MAX, 1};
  const checker::VariantDescriptor::ImmediateValueDescriptor descriptor{
      .operand_field_id = "event",
      .allowed_values = allowed_values,
  };
  checker::OperandView operand{
      .field_id = "event",
      .actual_shape = checker::OperandShape::Immediate,
      .immediate_type = ScalarType::U32,
      .immediate_bits = 16,
      .immediate_is_negative = false,
      .integer_source_bits = 15,
  };
  const auto check = [&] {
    return checker::check_immediate_value(
        descriptor, std::span<const checker::OperandView>{&operand, 1},
        checker::Context{});
  };
  const auto mismatched = check();
  ASSERT_FALSE(mismatched.has_value());
  EXPECT_EQ(mismatched.error().front().kind,
            checker::CheckDiagnosticKind::ImmediateValueMismatch);
  operand.immediate_bits = 15;
  EXPECT_TRUE(check().has_value());
  operand.integer_source_bits.reset();
  EXPECT_TRUE(check().has_value());
  operand.immediate_type = ScalarType::U64;
  operand.immediate_bits = UINT64_MAX;
  operand.integer_source_bits = UINT64_MAX;
  EXPECT_TRUE(check().has_value());
  operand.immediate_type = ScalarType::B32;
  operand.immediate_bits = 15;
  operand.integer_source_bits = 15;
  EXPECT_TRUE(check().has_value());
  operand.immediate_bits = 16;
  EXPECT_FALSE(check().has_value());
  operand.immediate_type = ScalarType::S32;
  operand.immediate_bits = UINT32_MAX;
  operand.integer_source_bits = UINT64_MAX;
  operand.immediate_is_negative = true;
  const auto negative = check();
  ASSERT_FALSE(negative.has_value());
  EXPECT_NE(negative.error().front().message.find("unsupported value"),
            std::string::npos);
  operand.immediate_type = ScalarType::F32;
  operand.immediate_bits = 0x3f800000;
  operand.immediate_is_negative = false;
  operand.integer_source_bits = 1;
  EXPECT_TRUE(check().has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
