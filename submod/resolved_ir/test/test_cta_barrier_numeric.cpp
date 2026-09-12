#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse a module fixture and surface parse failures in its calling test. */
std::optional<syntax_ast::AstModule> parse_module(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto module = parser.parseModule();
  if (!module) {
    ADD_FAILURE() << (module.diagnostics.empty()
                          ? "PTX source did not parse."
                          : module.diagnostics.front().message);
    return std::nullopt;
  }
  return std::move(*module);
}

TEST(CtaBarrierNumeric, RejectsInvalidKnownImmediateValuesAtTheirOperands) {
  /** Resolve one numeric-negative fixture and require an operand diagnostic. */
  const auto reject = [](std::string_view instruction,
                         std::size_t invalid_operand_index,
                         std::size_t expected_diagnostic_count = 1) {
    const std::string source = R"ptx(
.version 8.0
.target sm_80
.address_size 64
.entry k() {
  .reg .u32 %r;
  .reg .pred %p<2>;
  )ptx" + std::string(instruction) +
                               R"ptx(
  ret;
}
)ptx";
    const auto ast = parse_module(source);
    ASSERT_TRUE(ast.has_value());
    const auto& body =
        std::get<syntax_ast::AstFunction>(ast->items.back()).body;
    const auto bar =
        std::find_if(body.begin(), body.end(),
                     [](const syntax_ast::AstFunctionBodyItem& item) {
                       const auto* instruction =
                           std::get_if<syntax_ast::AstInstruction>(&item);
                       return instruction != nullptr &&
                              instruction->opcode.syntax.text == "bar";
                     });
    ASSERT_NE(bar, body.end());
    const auto& syntax_instruction = std::get<syntax_ast::AstInstruction>(*bar);
    const auto resolved = resolveModule(*ast);

    ASSERT_FALSE(resolved.has_value());
    ASSERT_EQ(resolved.error().size(), expected_diagnostic_count);
    for (const auto& diagnostic : resolved.error()) {
      EXPECT_EQ(diagnostic.checker_kind,
                checker::CheckDiagnosticKind::ImmediateValueMismatch);
      EXPECT_EQ(diagnostic.range,
                syntax_ast::sourceRange(
                    syntax_instruction.operands[invalid_operand_index]));
    }
  };

  reject("bar.sync 16;", 0);
  reject("bar.sync 0, 33;", 1);
  reject("bar.cta.sync 16;", 0);
  reject("bar.cta.sync 0, 33;", 1);
  reject("bar.arrive 16, 32;", 0);
  reject("bar.arrive 0, 33;", 1);
  reject("bar.arrive 0, 0;", 1);
  reject("bar.cta.arrive 16, 32;", 0);
  reject("bar.cta.arrive 0, 33;", 1);
  reject("bar.cta.arrive 0, 0;", 1);
  reject("bar.red.popc.u32 %r, 16, %p0;", 1);
  reject("bar.red.popc.u32 %r, 0, 33, %p0;", 2);
  reject("bar.cta.red.popc.u32 %r, 16, %p0;", 1);
  reject("bar.cta.red.popc.u32 %r, 0, 33, %p0;", 2);
  reject("bar.red.and.pred %p0, 16, %p1;", 1);
  reject("bar.red.and.pred %p0, 0, 33, %p1;", 2);
  reject("bar.cta.red.and.pred %p0, 16, %p1;", 1);
  reject("bar.cta.red.and.pred %p0, 0, 33, %p1;", 2);
  reject("bar.red.or.pred %p0, 16, %p1;", 1);
  reject("bar.red.or.pred %p0, 0, 33, %p1;", 2);
  reject("bar.cta.red.or.pred %p0, 16, %p1;", 1);
  reject("bar.cta.red.or.pred %p0, 0, 33, %p1;", 2);
  reject("bar.sync -1;", 0);
  reject("bar.sync 0, -1;", 1);
}

TEST(CtaBarrierNumeric, AcceptsStaticBoundariesAndOptionalLayouts) {
  const auto ast = parse_module(R"ptx(
.version 8.0
.target sm_80
.address_size 64
.entry k() {
  .reg .u32 %r;
  .reg .pred %p<2>;
  bar.sync 0;
  bar.sync 15;
  bar.sync 0, 0;
  bar.sync 0, 32;
  bar.arrive 0, 32;
  bar.cta.sync 15, 32;
  bar.cta.arrive 0, 32;
  bar.red.popc.u32 %r, 0, %p0;
  bar.cta.red.popc.u32 %r, 15, 32, %p0;
  bar.red.and.pred %p0, 0, 32, %p1;
  bar.cta.red.and.pred %p0, 15, !%p1;
  bar.red.or.pred %p0, 0, %p1;
  bar.cta.red.or.pred %p0, 15, 0, !%p1;
  ret;
}
)ptx");
  ASSERT_TRUE(ast.has_value());
  const auto resolved = resolveModule(*ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
}

TEST(CtaBarrierNumeric, LeavesRegisterValueContractsDynamic) {
  const auto ast = parse_module(R"ptx(
.version 8.0
.target sm_80
.address_size 64
.entry k() {
  .reg .u32 %r<3>;
  .reg .pred %p<2>;
  bar.sync %r0, %r1;
  bar.cta.sync %r0, %r1;
  bar.arrive %r0, %r1;
  bar.cta.arrive %r0, %r1;
  bar.red.popc.u32 %r2, %r0, %r1, %p0;
  bar.cta.red.popc.u32 %r2, %r0, %r1, %p0;
  bar.red.and.pred %p0, %r0, %r1, %p1;
  bar.cta.red.and.pred %p0, %r0, %r1, %p1;
  bar.red.or.pred %p0, %r0, %r1, %p1;
  bar.cta.red.or.pred %p0, %r0, %r1, %p1;
  ret;
}
)ptx");
  ASSERT_TRUE(ast.has_value());
  const auto resolved = resolveModule(*ast);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
}

TEST(CtaBarrierNumeric, PreservesImmediateAndCtaAvailabilityBoundaries) {
  const auto legacy_ast = parse_module(R"ptx(
.version 1.0
.address_size 64
.entry k() { bar.sync 0; ret; }
)ptx");
  ASSERT_TRUE(legacy_ast.has_value());
  const auto legacy = resolveModule(*legacy_ast);
  ASSERT_TRUE(legacy.has_value()) << legacy.error().front().message;
  const auto& legacy_instruction =
      std::get<Bar>(legacy->functions.front().body.front());
  EXPECT_TRUE(checker::check(
                  legacy_instruction,
                  checker::Context{
                      .target = {.ptx_version = {1, 0}, .sm_version = 10},
                      .instruction_range =
                          legacy->functions.front().instruction_ranges.front(),
                  })
                  .has_value());

  const auto cta_before_introduction_ast = parse_module(R"ptx(
.version 7.7
.target sm_80
.address_size 64
.entry k() { bar.cta.sync 0; ret; }
)ptx");
  ASSERT_TRUE(cta_before_introduction_ast.has_value());
  const auto cta_before_introduction =
      resolveModule(*cta_before_introduction_ast);
  ASSERT_FALSE(cta_before_introduction.has_value());
  ASSERT_EQ(cta_before_introduction.error().size(), 1U);
  EXPECT_EQ(cta_before_introduction.error().front().checker_kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
}

TEST(CtaBarrierNumeric, RejectsMalformedDivisibilityDescriptorForRegister) {
  constexpr checker::VariantDescriptor::ImmediateMultipleOfDescriptor
      descriptor{
          .operand_field_id = "thread_count",
          .divisor = 0,
      };
  const checker::OperandView thread_count{
      .field_id = "thread_count",
      .actual_shape = checker::OperandShape::Register,
  };
  const auto checked = checker::check_immediate_multiple_of(
      descriptor, std::span<const checker::OperandView>{&thread_count, 1},
      checker::Context{.instruction_range = SourceRange{{4, 3}, {4, 17}}});

  ASSERT_FALSE(checked.has_value());
  ASSERT_EQ(checked.error().size(), 1U);
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::RuleViolation);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
