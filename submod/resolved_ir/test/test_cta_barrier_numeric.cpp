#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/checker/parallel_synchronization_and_communication.gen.hpp>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker_support.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_support.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#include "test_module_projection.hpp"

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
    const auto resolved = test_support::resolveTypedModule<Bar>(
        *ast, test_support::ModulePipeline::AvailableContext);

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
  const auto resolved = test_support::resolveTypedModule<Bar>(
      *ast, test_support::ModulePipeline::AvailableContext);

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
  const auto resolved = test_support::resolveTypedModule<Bar>(
      *ast, test_support::ModulePipeline::AvailableContext);

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
}

TEST(CtaBarrierNumeric, PreservesImmediateAndCtaAvailabilityBoundaries) {
  const auto legacy_ast = parse_module(R"ptx(
.version 1.0
.address_size 64
.entry k() { bar.sync 0; ret; }
)ptx");
  ASSERT_TRUE(legacy_ast.has_value());
  const auto legacy = test_support::resolveTypedModule<Bar>(
      *legacy_ast, test_support::ModulePipeline::AvailableContext);
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
  const auto cta_before_introduction = test_support::resolveTypedModule<Bar>(
      *cta_before_introduction_ast,
      test_support::ModulePipeline::AvailableContext);
  ASSERT_FALSE(cta_before_introduction.has_value());
  ASSERT_EQ(cta_before_introduction.error().size(), 1U);
  EXPECT_EQ(cta_before_introduction.error().front().checker_kind,
            checker::CheckDiagnosticKind::UnsupportedPtxVersion);
}

/** Keep standalone CTA barrier identity and expressed convergence after AST release. */
TEST(CtaBarrierNumeric, ResolvesStandaloneSyncWithOwnedAlignedMetadata) {
  auto ast = parse_module(R"ptx(
.version 7.8
.target sm_80
.address_size 64
.entry k() {
  .reg .u32 %r<2>;
  barrier.sync 0;
  barrier.sync.aligned 15, 32;
  barrier.sync %r0, %r1;
  barrier.sync.aligned %r0;
  barrier.cta.sync 0;
  barrier.cta.sync.aligned 15, 32;
  barrier.cta.sync %r0, %r1;
  barrier.cta.sync.aligned %r0;
  ret;
}
)ptx");
  ASSERT_TRUE(ast.has_value());
  auto resolved = test_support::resolveTypedModule<Barrier>(
      *ast, test_support::ModulePipeline::AvailableContext);
  ast.reset();
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  auto& function = resolved->functions.front();
  ASSERT_EQ(function.body.size(), 9U);

  for (std::size_t index = 0; index < 8; ++index) {
    const auto& barrier = std::get<Barrier>(function.body[index]);
    const auto context = checker::Context{
        .target = {.ptx_version = {7, 8}, .sm_version = 80},
        .instruction_range = function.instruction_ranges[index],
    };
    EXPECT_TRUE(checker::check(barrier, context).has_value()) << index;
    if (index < 4) {
      const auto& sync = std::get<Barrier::Sync>(barrier.variant);
      EXPECT_EQ(sync.aligned.value, index % 2 == 1);
      EXPECT_EQ(sync.aligned.locs.empty(), index % 2 == 0);
    } else {
      const auto& sync = std::get<Barrier::CtaSync>(barrier.variant);
      EXPECT_EQ(sync.aligned.value, index % 2 == 1);
      EXPECT_EQ(sync.aligned.locs.empty(), index % 2 == 0);
    }
  }

  auto& first =
      std::get<Barrier::Sync>(std::get<Barrier>(function.body.front()).variant);
  const auto original_layout = first.operand_layout;
  first.operand_layout = ResolvedOperandLayoutTag{99};
  const auto corrupted = checker::check(
      std::get<Barrier>(function.body.front()),
      checker::Context{
          .target = {.ptx_version = {7, 8}, .sm_version = 80},
          .instruction_range = function.instruction_ranges.front(),
      });
  ASSERT_FALSE(corrupted.has_value());
  EXPECT_EQ(corrupted.error().front().kind,
            checker::CheckDiagnosticKind::InvalidOperandLayoutTag);
  first.operand_layout = original_layout;
}

/** Accept the first PTX and SM combination that exposes standalone CTA sync. */
TEST(CtaBarrierNumeric, AcceptsStandaloneSyncAtMinimumTarget) {
  const auto ast = parse_module(R"ptx(
.version 6.0
.target sm_30
.address_size 64
.entry k() {
  barrier.sync 0;
  ret;
}
)ptx");
  ASSERT_TRUE(ast.has_value());
  const auto resolved = test_support::resolveTypedModule<Barrier>(
      *ast, test_support::ModulePipeline::AvailableContext);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& barrier =
      std::get<Barrier>(resolved->functions.front().body.front());
  EXPECT_TRUE(std::holds_alternative<Barrier::Sync>(barrier.variant));
}

/** Reject unsupported standalone CTA barriers and statically known bad operands. */
TEST(CtaBarrierNumeric, RejectsStandaloneSyncInvalidForms) {
  /** Resolve one complete module so version and target checks use directive context. */
  const auto reject = [](std::string_view version, std::string_view target,
                         std::string_view instruction) {
    const std::string source = ".version " + std::string(version) +
                               "\n.target " + std::string(target) +
                               R"ptx(
.address_size 64
.entry k() {
  .reg .u32 %r<2>;
  )ptx" + std::string(instruction) +
                               R"ptx(
  ret;
}
)ptx";
    const auto ast = parse_module(source);
    ASSERT_TRUE(ast.has_value());
    const auto resolved = test_support::resolveTypedModule<Barrier>(
        *ast, test_support::ModulePipeline::AvailableContext);
    EXPECT_FALSE(resolved.has_value()) << instruction;
  };

  for (const std::string_view instruction : {
           "barrier.sync 16;",
           "barrier.sync -1;",
           "barrier.sync 0, 33;",
           "barrier.cta.sync 0, -1;",
           "barrier.sync;",
           "barrier.sync 0, 32, 64;",
           "barrier.sync.aligned.sync 0;",
           "barrier.sync.aligned.aligned 0;",
           "barrier.cta.sync.sync 0;",
       }) {
    reject("8.0", "sm_80", instruction);
  }
  reject("5.9", "sm_80", "barrier.sync 0;");
  reject("7.7", "sm_80", "barrier.cta.sync 0;");
  reject("8.0", "sm_20", "barrier.sync 0;");
  reject("8.0", "sm_20", "barrier.cta.sync.aligned 0;");
}

/** Keep CTA arrival operands and expressed alignment after AST release. */
TEST(CtaBarrierNumeric, ResolvesStandaloneArriveWithOwnedAlignedMetadata) {
  auto ast = parse_module(R"ptx(
.version 7.8
.target sm_80
.address_size 64
.entry k() {
  .reg .u32 %r<2>;
  barrier.arrive 0, 32;
  barrier.arrive.aligned 15, 64;
  barrier.arrive %r0, %r1;
  barrier.arrive.aligned %r0, %r1;
  barrier.cta.arrive 0, 32;
  barrier.cta.arrive.aligned 15, 64;
  barrier.cta.arrive %r0, %r1;
  barrier.cta.arrive.aligned %r0, %r1;
  ret;
}
)ptx");
  ASSERT_TRUE(ast.has_value());
  auto resolved = test_support::resolveTypedModule<Barrier>(
      *ast, test_support::ModulePipeline::AvailableContext);
  ast.reset();
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  auto& function = resolved->functions.front();
  ASSERT_EQ(function.body.size(), 9U);

  for (std::size_t index = 0; index < 8; ++index) {
    const auto& barrier = std::get<Barrier>(function.body[index]);
    const auto context = checker::Context{
        .target = {.ptx_version = {7, 8}, .sm_version = 80},
        .instruction_range = function.instruction_ranges[index],
    };
    EXPECT_TRUE(checker::check(barrier, context).has_value()) << index;
    if (index < 4) {
      const auto& arrive = std::get<Barrier::Arrive>(barrier.variant);
      EXPECT_EQ(arrive.aligned.value, index % 2 == 1);
      EXPECT_EQ(arrive.aligned.locs.empty(), index % 2 == 0);
    } else {
      const auto& arrive = std::get<Barrier::CtaArrive>(barrier.variant);
      EXPECT_EQ(arrive.aligned.value, index % 2 == 1);
      EXPECT_EQ(arrive.aligned.locs.empty(), index % 2 == 0);
    }
  }

  auto& first = std::get<Barrier::Arrive>(
      std::get<Barrier>(function.body.front()).variant);
  const auto original_layout = first.operand_layout;
  first.operand_layout = ResolvedOperandLayoutTag{99};
  const auto corrupted = checker::check(
      std::get<Barrier>(function.body.front()),
      checker::Context{
          .target = {.ptx_version = {7, 8}, .sm_version = 80},
          .instruction_range = function.instruction_ranges.front(),
      });
  ASSERT_FALSE(corrupted.has_value());
  EXPECT_EQ(corrupted.error().front().kind,
            checker::CheckDiagnosticKind::InvalidOperandLayoutTag);
  first.operand_layout = original_layout;
}

/** Check introduction targets and static numeric constraints for CTA arrival. */
TEST(CtaBarrierNumeric, ChecksStandaloneArriveTargetsAndOperands) {
  /** Resolve one arrival with directive-supplied PTX and SM context. */
  const auto resolve_one = [](std::string_view version, std::string_view target,
                              std::string_view instruction) {
    const std::string source = ".version " + std::string(version) +
                               "\n.target " + std::string(target) +
                               R"ptx(
.address_size 64
.entry k() {
  .reg .u32 %r<2>;
  .reg .u64 %rd;
  )ptx" + std::string(instruction) +
                               R"ptx(
  ret;
}
)ptx";
    const auto ast = parse_module(source);
    if (!ast)
      return false;
    return test_support::resolveTypedModule<Barrier>(
               *ast, test_support::ModulePipeline::AvailableContext)
        .has_value();
  };

  EXPECT_TRUE(resolve_one("6.0", "sm_30", "barrier.arrive 0, 32;"));
  EXPECT_TRUE(resolve_one("7.8", "sm_30", "barrier.cta.arrive 15, 32;"));
  for (const std::string_view instruction : {
           "barrier.arrive 16, 32;",
           "barrier.arrive -1, 32;",
           "barrier.arrive 0, 0;",
           "barrier.arrive 0, -1;",
           "barrier.arrive 0, 33;",
           "barrier.arrive %rd, 32;",
           "barrier.arrive 0, %rd;",
           "barrier.cta.arrive 16, 32;",
           "barrier.cta.arrive 0, 0;",
           "barrier.cta.arrive 0, 33;",
           "barrier.arrive 0;",
           "barrier.arrive 0, 32, 64;",
           "barrier.arrive.aligned.arrive 0, 32;",
           "barrier.arrive.aligned.aligned 0, 32;",
           "barrier.cta.arrive.arrive 0, 32;",
       }) {
    EXPECT_FALSE(resolve_one("8.0", "sm_80", instruction)) << instruction;
  }
  EXPECT_FALSE(resolve_one("5.9", "sm_80", "barrier.arrive 0, 32;"));
  EXPECT_FALSE(resolve_one("7.7", "sm_80", "barrier.cta.arrive 0, 32;"));
  EXPECT_FALSE(resolve_one("8.0", "sm_20", "barrier.arrive 0, 32;"));
  EXPECT_FALSE(resolve_one("8.0", "sm_20", "barrier.cta.arrive 0, 32;"));
}

/** Attribute known arrival-value failures to the offending source operand. */
TEST(CtaBarrierNumeric, RejectsStandaloneArriveImmediateAtOperand) {
  /** Compare the checker diagnostic with the parsed immediate source range. */
  const auto reject = [](std::string_view instruction,
                         std::size_t operand_index) {
    const std::string source = R"ptx(
.version 8.0
.target sm_80
.address_size 64
.entry k() {
  )ptx" + std::string(instruction) +
                               R"ptx(
  ret;
}
)ptx";
    const auto ast = parse_module(source);
    ASSERT_TRUE(ast.has_value());
    const auto& body =
        std::get<syntax_ast::AstFunction>(ast->items.back()).body;
    const auto& syntax_instruction =
        std::get<syntax_ast::AstInstruction>(body.front());
    const auto resolved = test_support::resolveTypedModule<Barrier>(
        *ast, test_support::ModulePipeline::AvailableContext);
    ASSERT_FALSE(resolved.has_value());
    ASSERT_EQ(resolved.error().size(), 1U);
    EXPECT_EQ(resolved.error().front().checker_kind,
              checker::CheckDiagnosticKind::ImmediateValueMismatch);
    EXPECT_EQ(
        resolved.error().front().range,
        syntax_ast::sourceRange(syntax_instruction.operands[operand_index]));
  };

  reject("barrier.arrive 16, 32;", 0);
  reject("barrier.arrive 0, 0;", 1);
  reject("barrier.arrive 0, 33;", 1);
  reject("barrier.cta.arrive 16, 32;", 0);
  reject("barrier.cta.arrive 0, 0;", 1);
  reject("barrier.cta.arrive 0, 33;", 1);
}

/** Keep legacy bar arrival and standalone barrier arrival in separate types. */
TEST(CtaBarrierNumeric, DistinguishesBarAndBarrierArriveOpcodes) {
  const auto ast = parse_module(R"ptx(
.version 7.8
.target sm_80
.address_size 64
.entry k() {
  bar.arrive 0, 32;
  barrier.arrive.aligned 0, 32;
  ret;
}
)ptx");
  ASSERT_TRUE(ast.has_value());
  const auto legacy = test_support::resolveTypedModule<Bar>(
      *ast, test_support::ModulePipeline::AvailableContext);
  const auto standalone = test_support::resolveTypedModule<Barrier>(
      *ast, test_support::ModulePipeline::AvailableContext);
  ASSERT_TRUE(legacy.has_value()) << legacy.error().front().message;
  ASSERT_TRUE(standalone.has_value()) << standalone.error().front().message;
  EXPECT_TRUE(std::holds_alternative<Bar>(legacy->functions.front().body[0]));
  EXPECT_TRUE(std::holds_alternative<std::monostate>(
      legacy->functions.front().body[1]));
  EXPECT_TRUE(std::holds_alternative<std::monostate>(
      standalone->functions.front().body[0]));
  EXPECT_TRUE(
      std::holds_alternative<Barrier>(standalone->functions.front().body[1]));
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
