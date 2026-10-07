#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/bar.gen.hpp>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/barrier.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
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
  const auto& legacy_instruction = *legacy->functions.front().body.front();
  EXPECT_EQ(legacy_instruction.instruction_kind(), InstructionKind::BarSync);
  EXPECT_TRUE(legacy_instruction
                  .check(checker::Context{
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
  auto resolved = resolveModule(*ast);
  ast.reset();
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  auto& function = resolved->functions.front();
  ASSERT_EQ(function.body.size(), 9U);

  for (std::size_t index = 0; index < 8; ++index) {
    const auto& barrier = *function.body[index];
    const auto context = checker::Context{
        .target = {.ptx_version = {7, 8}, .sm_version = 80},
        .instruction_range = function.instruction_ranges[index],
    };
    EXPECT_TRUE(barrier.check(context).has_value()) << index;
    if (index < 4) {
      const auto& sync = dynamic_cast<const BarrierSync&>(barrier);
      EXPECT_EQ(sync.aligned.value, index % 2 == 1);
      EXPECT_EQ(sync.aligned.locs.empty(), index % 2 == 0);
    } else {
      const auto& sync = dynamic_cast<const BarrierCtaSync&>(barrier);
      EXPECT_EQ(sync.aligned.value, index % 2 == 1);
      EXPECT_EQ(sync.aligned.locs.empty(), index % 2 == 0);
    }
  }

  auto& first = dynamic_cast<BarrierSync&>(*function.body.front());
  const auto original_layout = first.operand_layout;
  first.operand_layout = ResolvedOperandLayoutTag{99};
  const auto corrupted = function.body.front()->check(checker::Context{
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
  const auto resolved = resolveModule(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& barrier = *resolved->functions.front().body.front();
  EXPECT_EQ(barrier.instruction_kind(), InstructionKind::BarrierSync);
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
    const auto resolved = resolveModule(*ast);
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
  auto resolved = resolveModule(*ast);
  ast.reset();
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  auto& function = resolved->functions.front();
  ASSERT_EQ(function.body.size(), 9U);

  for (std::size_t index = 0; index < 8; ++index) {
    const auto& barrier = *function.body[index];
    const auto context = checker::Context{
        .target = {.ptx_version = {7, 8}, .sm_version = 80},
        .instruction_range = function.instruction_ranges[index],
    };
    EXPECT_TRUE(barrier.check(context).has_value()) << index;
    if (index < 4) {
      const auto& arrive = dynamic_cast<const BarrierArrive&>(barrier);
      EXPECT_EQ(arrive.aligned.value, index % 2 == 1);
      EXPECT_EQ(arrive.aligned.locs.empty(), index % 2 == 0);
    } else {
      const auto& arrive = dynamic_cast<const BarrierCtaArrive&>(barrier);
      EXPECT_EQ(arrive.aligned.value, index % 2 == 1);
      EXPECT_EQ(arrive.aligned.locs.empty(), index % 2 == 0);
    }
  }

  auto& first = dynamic_cast<BarrierArrive&>(*function.body.front());
  const auto original_layout = first.operand_layout;
  first.operand_layout = ResolvedOperandLayoutTag{99};
  const auto corrupted = function.body.front()->check(checker::Context{
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
    return resolveModule(*ast).has_value();
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
    const auto resolved = resolveModule(*ast);
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
  const auto resolved = resolveModule(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 3U);
  EXPECT_EQ(body[0]->instruction_kind(), InstructionKind::BarArrive);
  EXPECT_EQ(body[1]->instruction_kind(), InstructionKind::BarrierArrive);
}

/** Retain six standalone reduction forms, their layouts, and source metadata. */
TEST(CtaBarrierNumeric, ResolvesStandaloneReductionsAfterAstRelease) {
  auto ast = parse_module(R"ptx(
.version 7.8
.target sm_80
.address_size 64
.entry k() {
  .reg .u32 %r<3>;
  .reg .pred %p<2>;
  barrier.red.popc.u32 %r2, 0, %p1;
  barrier.red.popc.aligned.u32 %r2, 15, 32, !%p1;
  barrier.cta.red.popc.u32 %r2, %r0, %r1, %p1;
  barrier.cta.red.popc.aligned.u32 %r2, 1, !%p1;
  barrier.red.and.pred %p0, 0, %p1;
  barrier.red.and.aligned.pred %p0, 15, 64, !%p1;
  barrier.cta.red.and.pred %p0, %r0, %r1, %p1;
  barrier.cta.red.and.aligned.pred %p0, 1, !%p1;
  barrier.red.or.pred %p0, 0, %p1;
  barrier.red.or.aligned.pred %p0, 15, 64, !%p1;
  barrier.cta.red.or.pred %p0, %r0, %r1, %p1;
  barrier.cta.red.or.aligned.pred %p0, 1, !%p1;
  ret;
}
)ptx");
  ASSERT_TRUE(ast.has_value());
  auto resolved = resolveModule(*ast);
  ast.reset();
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  auto& function = resolved->functions.front();
  ASSERT_EQ(function.body.size(), 13U);
  const std::array<InstructionKind, 12> expected{
      InstructionKind::BarrierRedPopcU32,
      InstructionKind::BarrierRedPopcU32,
      InstructionKind::BarrierCtaRedPopcU32,
      InstructionKind::BarrierCtaRedPopcU32,
      InstructionKind::BarrierRedAndPred,
      InstructionKind::BarrierRedAndPred,
      InstructionKind::BarrierCtaRedAndPred,
      InstructionKind::BarrierCtaRedAndPred,
      InstructionKind::BarrierRedOrPred,
      InstructionKind::BarrierRedOrPred,
      InstructionKind::BarrierCtaRedOrPred,
      InstructionKind::BarrierCtaRedOrPred,
  };
  for (std::size_t index = 0; index < expected.size(); ++index) {
    const auto& barrier = *function.body[index];
    EXPECT_EQ(barrier.instruction_kind(), expected[index]) << index;
    const auto checked = barrier.check(checker::Context{
        .target = {.ptx_version = {7, 8}, .sm_version = 80},
        .instruction_range = function.instruction_ranges[index],
    });
    EXPECT_TRUE(checked.has_value()) << index;
    const auto check_metadata = [index](const auto& reduction) {
      EXPECT_EQ(reduction.aligned.value, index % 2 == 1);
      EXPECT_EQ(reduction.aligned.locs.empty(), index % 2 == 0);
      EXPECT_EQ(reduction.operand_layout.value,
                index % 4 == 1 || index % 4 == 2 ? 1U : 0U);
    };
    switch (expected[index]) {
      case InstructionKind::BarrierRedPopcU32:
        check_metadata(dynamic_cast<const BarrierRedPopcU32&>(barrier));
        break;
      case InstructionKind::BarrierCtaRedPopcU32:
        check_metadata(dynamic_cast<const BarrierCtaRedPopcU32&>(barrier));
        break;
      case InstructionKind::BarrierRedAndPred:
        check_metadata(dynamic_cast<const BarrierRedAndPred&>(barrier));
        break;
      case InstructionKind::BarrierCtaRedAndPred:
        check_metadata(dynamic_cast<const BarrierCtaRedAndPred&>(barrier));
        break;
      case InstructionKind::BarrierRedOrPred:
        check_metadata(dynamic_cast<const BarrierRedOrPred&>(barrier));
        break;
      case InstructionKind::BarrierCtaRedOrPred:
        check_metadata(dynamic_cast<const BarrierCtaRedOrPred&>(barrier));
        break;
      default:
        FAIL() << "Unexpected reduction form";
    }
  }
  const auto& and_reduction =
      dynamic_cast<const BarrierRedAndPred&>(*function.body[5]);
  EXPECT_TRUE(and_reduction.thread_count.has_value());
  EXPECT_EQ(and_reduction.dst.value.register_ref.register_class,
            ResolvedRegisterClass::Predicate);
  EXPECT_FALSE(and_reduction.dst.value.negated);
  EXPECT_TRUE(and_reduction.predicate.value.negated);
  EXPECT_EQ(and_reduction.predicate.value.register_ref.spelling, "%p1");
  EXPECT_FALSE(and_reduction.predicate.locs.empty());
  EXPECT_EQ(std::get<ResolvedImmediate>(and_reduction.barrier.value).bits, 15U);
  EXPECT_EQ(std::get<ResolvedImmediate>(and_reduction.thread_count->value).bits,
            64U);

  auto& first = dynamic_cast<BarrierRedPopcU32&>(*function.body.front());
  first.operand_layout = ResolvedOperandLayoutTag{99};
  const auto corrupted = function.body.front()->check(checker::Context{
      .target = {.ptx_version = {7, 8}, .sm_version = 80},
      .instruction_range = function.instruction_ranges.front(),
  });
  ASSERT_FALSE(corrupted.has_value());
  EXPECT_EQ(corrupted.error().front().kind,
            checker::CheckDiagnosticKind::InvalidOperandLayoutTag);
}

/** Check introduction targets and reject malformed standalone reduction operands. */
TEST(CtaBarrierNumeric, ChecksStandaloneReductionTargetsAndOperands) {
  /** Resolve one reduction with directive-supplied target and typed registers. */
  const auto resolve_one = [](std::string_view version, std::string_view target,
                              std::string_view instruction) {
    const std::string source = ".version " + std::string(version) +
                               "\n.target " + std::string(target) +
                               R"ptx(
.address_size 64
.entry k() {
  .reg .u32 %r<3>;
  .reg .u64 %rd;
  .reg .pred %p<2>;
  )ptx" + std::string(instruction) +
                               R"ptx(
  ret;
}
)ptx";
    PtxSyntaxParser parser(source);
    auto ast = parser.parseModule();
    if (!ast)
      return false;
    return resolveModule(*ast).has_value();
  };

  for (const std::string_view instruction : {
           "barrier.red.popc.u32 %r0, 0, %p0;",
           "barrier.red.and.aligned.pred %p0, 15, 32, !%p1;",
           "barrier.red.or.pred %p0, %r0, %r1, %p1;",
       }) {
    EXPECT_TRUE(resolve_one("6.0", "sm_30", instruction)) << instruction;
    EXPECT_FALSE(resolve_one("5.9", "sm_80", instruction)) << instruction;
    EXPECT_FALSE(resolve_one("8.0", "sm_20", instruction)) << instruction;
  }
  for (const std::string_view instruction : {
           "barrier.cta.red.popc.aligned.u32 %r0, 0, !%p0;",
           "barrier.cta.red.and.pred %p0, 15, 32, %p1;",
           "barrier.cta.red.or.aligned.pred %p0, %r0, %r1, !%p1;",
       }) {
    EXPECT_TRUE(resolve_one("7.8", "sm_30", instruction)) << instruction;
    EXPECT_FALSE(resolve_one("7.7", "sm_80", instruction)) << instruction;
    EXPECT_FALSE(resolve_one("8.0", "sm_20", instruction)) << instruction;
  }
  for (const std::string_view instruction : {
           "barrier.red.popc.u32 %r0, 16, %p0;",
           "barrier.cta.red.popc.u32 %r0, 0, 33, %p0;",
           "barrier.red.and.pred %p0, 16, %p1;",
           "barrier.cta.red.and.pred %p0, 0, 33, %p1;",
           "barrier.red.or.pred %p0, 16, %p1;",
           "barrier.cta.red.or.pred %p0, 0, 33, %p1;",
           "barrier.red.popc.u32 %p0, 0, %p1;",
           "barrier.red.and.pred %r0, 0, %p1;",
           "barrier.red.or.pred %p0, 0, %r0;",
           "barrier.red.and.pred !%p0, 0, %p1;",
           "barrier.red.popc.u32 %r0, %rd, %p0;",
           "barrier.red.or.pred %p0, 0, %rd, %p1;",
           "barrier.red.popc.u32 %r0, 0;",
           "barrier.red.and.pred %p0, 0, 32, 64, %p1;",
           "barrier.red.or.aligned.aligned.pred %p0, 0, %p1;",
           "barrier.red.popc.u32.aligned %r0, 0, %p0;",
           "barrier.cluster.red.or.pred %p0, 0, %p1;",
       }) {
    EXPECT_FALSE(resolve_one("8.0", "sm_80", instruction)) << instruction;
  }
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
