#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using test_helpers::parseInstruction;
using test_helpers::parseModule;

/** Resolve every ordinary and half/bfloat SETP family at its PTX 9.3 target. */
TEST(SetpCompleteness, ResolvesAndChecksEveryPtx93Family) {
  const auto parsed_module = parseModule(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .pred %p<4>;
  .reg .b16 %b16<3>;
  .reg .b32 %b32<3>;
  .reg .b64 %b64<3>;
  .reg .u16 %u16<3>;
  .reg .u32 %u32<3>;
  .reg .u64 %u64<3>;
  .reg .s16 %s16<3>;
  .reg .s32 %s32<3>;
  .reg .s64 %s64<3>;
  .reg .f32 %f32<3>;
  .reg .f64 %f64<3>;

  setp.ne.b16 %p0, %b160, %b161;
  setp.eq.xor.b32 _|%p1, %b320, %b321, !%p2;
  setp.ge.s16 %p0, %s160, %s161;
  setp.lt.and.s64 %p0|_, %s640, %s641, !%p1;
  setp.hs.u16 %p0|%p1, %u160, %u161;
  setp.lo.or.u64 _, %u640, %u641, %p2;
  setp.nan.ftz.f32 %p0, %f320, %f321;
  setp.geu.xor.f64 %p0|%p1, %f640, %f641, !%p2;
  setp.num.ftz.f16 %p0, %b160, %b161;
  setp.ltu.and.f16 %p0, %b160, %b161, !%p1;
  setp.equ.ftz.f16x2 %p0|%p1, %b320, %b321;
  setp.neu.or.f16x2 %p0|%p1, %b320, %b321, !%p2;
  setp.gt.bf16 %p0, %b160, %b161;
  setp.nan.xor.bf16 %p0, %b160, %b161, !%p1;
  setp.leu.bf16x2 %p0|%p1, %b320, %b321;
  setp.num.and.bf16x2 %p0|%p1, %b320, %b321, !%p2;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module);
  const auto resolved = resolveModule(*parsed_module);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 1u);
  ASSERT_EQ(resolved->functions.front().body.size(), 16u);

  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 100},
      .instruction_range = parsed_module->range,
  };
  for (const auto& instruction : resolved->functions.front().body) {
    const auto& setp = std::get<Setp>(instruction);
    const auto checked = checker::check(setp, context);
    ASSERT_TRUE(checked.has_value()) << checked.error().front().message;
  }
}

/** Target gating is preserved for f64, half, and bfloat SETP values. */
TEST(SetpCompleteness, EnforcesTypeSpecificAvailability) {
  constexpr std::array<std::pair<std::string_view, checker::TargetInfo>, 3>
      unavailable_cases{{
          {"setp.eq.f64 %p0, %fd0, %fd1;",
           {.ptx_version = {1, 0}, .sm_version = 12}},
          {"setp.eq.f16 %p0, %h0, %h1;",
           {.ptx_version = {4, 2}, .sm_version = 52}},
          {"setp.eq.bf16 %p0, %h0, %h1;",
           {.ptx_version = {7, 8}, .sm_version = 89}},
      }};
  for (const auto& [source, target] : unavailable_cases) {
    SCOPED_TRACE(source);
    const auto parsed_instruction = parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed_instruction);
    const auto resolved = resolve<Setp>(*parsed_instruction);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    const auto checked = checker::check(
        *resolved,
        checker::Context{.target = target,
                         .instruction_range = parsed_instruction->range});
    ASSERT_FALSE(checked.has_value());
    EXPECT_TRUE(checked.error().front().kind ==
                    checker::CheckDiagnosticKind::UnsupportedPtxVersion ||
                checked.error().front().kind ==
                    checker::CheckDiagnosticKind::UnsupportedSmVersion);
  }
}

/** Illegal comparison domains, sinks, layouts, and FTZ spellings are rejected. */
TEST(SetpCompleteness, RejectsIllegalModifierAndDestinationForms) {
  constexpr std::array<std::string_view, 6> rejected_sources{{
      "setp.lo.s32 %p0, %r0, %r1;",
      "setp.equ.u32 %p0, %r0, %r1;",
      "setp.eq.ftz.f64 %p0, %fd0, %fd1;",
      "setp.eq.ftz.bf16 %p0, %h0, %h1;",
      "setp.eq.f16x2 %p0, %r0, %r1;",
      "setp.eq.u32 _|_, %r0, %r1;",
  }};
  for (const auto source : rejected_sources) {
    SCOPED_TRACE(source);
    const auto parsed_instruction = parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed_instruction);
    EXPECT_FALSE(resolve<Setp>(*parsed_instruction).has_value());
  }

  const auto parsed_module = parseModule(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .pred %p<2>;
  .reg .f16 %half0, %half1;
  setp.eq.f16x2 %p0|%p1, %half0, %half1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed_module);
  EXPECT_FALSE(resolveModule(*parsed_module).has_value());

  const auto valid_module = parseModule(R"ptx(
.version 9.3
.target sm_100
.entry kernel() {
  .reg .pred %p<2>;
  .reg .b32 %bits0, %bits1;
  setp.eq.f16x2 %p0|%p1, %bits0, %bits1;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(valid_module);
  auto resolved = resolveModule(*valid_module);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  auto& packed = std::get<Setp::F16x2>(
      std::get<Setp>(resolved->functions.front().body.front()).variant);
  packed.src1.value.declared_type = ScalarType::F16;
  const auto checked = checker::check(
      std::get<Setp>(resolved->functions.front().body.front()),
      checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 100},
                       .instruction_range = parsed_module->range});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
}

/** Revalidation rejects a public-IR pair mutated to discard both results. */
TEST(SetpCompleteness, RevalidationRejectsAllSinkPair) {
  const auto parsed_instruction =
      parseInstruction("setp.eq.u32 %p0|%p1, %r0, %r1;");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed_instruction);
  auto resolved = resolve<Setp>(*parsed_instruction);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  auto& unsigned_variant = std::get<Setp::Unsigned>(resolved->variant);
  auto& operands =
      std::get<Setp::Unsigned::PairOperands>(unsigned_variant.operands);
  operands.dst.value.first.reset();
  operands.dst.value.second.reset();

  const auto checked = checker::check(
      *resolved,
      checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 100},
                       .instruction_range = parsed_instruction->range});
  ASSERT_FALSE(checked.has_value());
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedOperandShape);
}

/** Boolean combination precedes FTZ in each normative SETP source form. */
TEST(SetpCompleteness, EnforcesBooleanBeforeFtz) {
  for (const auto type : {"f32", "f16", "f16x2"}) {
    const std::string operands = std::string(type) == "f16x2"
                                     ? " %p0|%p1, %r0, %r1, !%p2;"
                                     : " %p0, %r0, %r1, !%p2;";
    for (const auto combine : {"and", "or", "xor"}) {
      const std::string prefix = std::string("setp.lt.") + combine;
      const auto valid = parseInstruction(prefix + ".ftz." + type + operands);
      ASSERT_INSTRUCTION_PARSE_SUCCEEDS(valid);
      EXPECT_TRUE(resolve<Setp>(*valid));
      const auto invalid = parseInstruction(std::string("setp.lt.ftz.") +
                                            combine + "." + type + operands);
      ASSERT_INSTRUCTION_PARSE_SUCCEEDS(invalid);
      EXPECT_FALSE(resolve<Setp>(*invalid));
    }
  }
}

/** Boolean SETP inputs canonicalize integer constants before checking. */
TEST(SetpCompleteness, CanonicalizesBooleanPredicateConstants) {
  constexpr std::pair<std::string_view, bool> constants[] = {
      {"0", false}, {"2", true},   {"-1", true},
      {"!0", true}, {"!1", false}, {"!-1", false},
  };
  for (const auto& [source_constant, expected] : constants) {
    const std::string source =
        "setp.eq.and.u32 %p0, %r0, %r1, " + std::string(source_constant) + ";";
    SCOPED_TRACE(source);
    const auto parsed = parseInstruction(source);
    ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolve<Setp>(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    const auto& variant = std::get<Setp::UnsignedBoolean>(resolved->variant);
    const auto& operands =
        std::get<Setp::UnsignedBoolean::SingleOperands>(variant.operands);
    const auto* constant =
        std::get_if<ResolvedPredicateConstant>(&operands.combine.value);
    ASSERT_NE(constant, nullptr);
    EXPECT_EQ(constant->value, expected);
    EXPECT_TRUE(checker::check(
        *resolved, checker::Context{
                       .target = {.ptx_version = {9, 3}, .sm_version = 100}}));
  }

  const auto half = parseInstruction("setp.eq.and.f16 %p0, %h0, %h1, !0;");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(half);
  auto resolved_half = resolve<Setp>(*half);
  ASSERT_TRUE(resolved_half.has_value()) << resolved_half.error().message;
  auto& half_variant = std::get<Setp::F16Boolean>(resolved_half->variant);
  EXPECT_TRUE(
      std::get<ResolvedPredicateConstant>(half_variant.combine.value).value);

  const auto special_info = base::lookup("%is_explicit_cluster");
  ASSERT_TRUE(special_info.has_value());
  half_variant.combine.value = ResolvedPredicateSpecialRegister{
      .register_ref = {.spelling = "%is_explicit_cluster",
                       .id = special_info->id},
  };
  const auto rechecked = checker::check(
      *resolved_half,
      checker::Context{.target = {.ptx_version = {9, 3}, .sm_version = 100},
                       .instruction_range = half->range});
  ASSERT_FALSE(rechecked.has_value());
  EXPECT_EQ(rechecked.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedOperandShape);
}

/** SETP Boolean sources exclude floating literals and predicate sregs. */
TEST(SetpCompleteness, RejectsFloatingAndSpecialRegisterCombineSources) {
  const auto floating = parseInstruction("setp.eq.and.u32 %p0, %r0, %r1, 1.0;");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(floating);
  EXPECT_FALSE(resolve<Setp>(*floating).has_value());

  const auto special = parseModule(R"ptx(
.version 9.3
.target sm_90
.entry kernel() {
  .reg .pred %p0;
  .reg .u32 %r0, %r1;
  setp.eq.and.u32 %p0, %r0, %r1, %is_explicit_cluster;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(special);
  EXPECT_FALSE(resolveModule(*special).has_value());
}

/** Half/bfloat SETP cannot receive an immediate in either data-source slot. */
TEST(SetpCompleteness, RejectsHalfAndBfloatImmediates) {
  for (const auto type : {"f16", "f16x2", "bf16", "bf16x2"}) {
    const bool packed = std::string_view(type).ends_with("x2");
    for (const auto immediate : {"0x3c00", "1.0"}) {
      for (const bool first : {false, true}) {
        const auto source = std::string("setp.eq.") + type +
                            (packed ? " %p0|%p1, " : " %p0, ") +
                            (first ? std::string(immediate) + ", %r0;"
                                   : std::string("%r0, ") + immediate + ";");
        SCOPED_TRACE(source);
        const auto ast = parseInstruction(source);
        ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
        EXPECT_FALSE(resolve<Setp>(*ast));
      }
    }
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
