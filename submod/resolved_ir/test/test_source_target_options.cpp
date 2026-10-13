#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Synthetic availability gate without changing any canonical instruction form. */
constexpr checker::AvailabilityDescriptor kMapAvailability{
    .any_of = {{{.minimum_ptx_version = {1, 0},
                 .has_exact_target = true,
                 .exact_target_architecture = {10},
                 .requires_map_f64_to_f32 = true},
                {.minimum_ptx_version = {1, 0},
                 .has_exact_target = true,
                 .exact_target_architecture = {11},
                 .requires_map_f64_to_f32 = true},
                {.minimum_ptx_version = {1, 0},
                 .has_exact_target = true,
                 .exact_target_architecture = {12},
                 .requires_map_f64_to_f32 = true}}},
    .any_of_count = 3,
};

/** Forward a real resolved instruction while inspecting the actual module context. */
class MapContextProbe final : public Instruction {
 public:
  /** Own the real payload and the source option expected for this instruction. */
  MapContextProbe(std::unique_ptr<Instruction> instruction, bool expected)
      : instruction_(std::move(instruction)), expected_(expected) {}
  /** Clone the complete real payload and probe expectation independently. */
  std::unique_ptr<Instruction> clone() const override {
    return std::make_unique<MapContextProbe>(instruction_->clone(), expected_);
  }
  /** Preserve the source instruction's exact opcode and form identity. */
  InstructionKind instruction_kind() const noexcept override {
    return instruction_->instruction_kind();
  }
  /** Check that both typed state and common availability see the same option. */
  checker::CheckResult check(const checker::Context& context) const override {
    if (context.target.map_f64_to_f32 != expected_ ||
        checker::is_available(kMapAvailability, context.target) != expected_)
      return std::unexpected(checker::CheckDiagnostics{
          {.kind = checker::CheckDiagnosticKind::RuleViolation,
           .range = context.instruction_range,
           .message = "Synthetic map option context disagrees with source."}});
    return instruction_->check(context);
  }
  /** Retain all owned-reference validation performed for the real instruction. */
  void visit_references(detail::IReferenceObserver& observer) const override {
    instruction_->visit_references(observer);
  }

 private:
  /** Real resolved payload owned by this test probe. */
  std::unique_ptr<Instruction> instruction_;
  /** Effective option required by the fixture's source region. */
  bool expected_;
};

/** Retain ordinary source order and reset the option for every .target. */
TEST(SourceTargetOptions, OwnsExactMapOptionAndResetsAcrossRegions) {
  std::optional<ResolvedModule> owned;
  {
    const std::string source = R"ptx(
.version 1.5
.target sm_10, map_f64_to_f32, texmode_unified
.address_size 64
.func first() { ret; }
.target sm_11
.func second() { ret; }
.target sm_12, map_f64_to_f32_extra, MAP_f64_to_f32, map_f64_to_f32x
.func third() { ret; }
.target sm_12, texmode_unified, map_f64_to_f32
.func fourth() { ret; }
)ptx";
    PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    ASSERT_TRUE(ast.diagnostics.empty());
    auto resolved = resolveAndValidateModule(*ast);
    ASSERT_TRUE(resolved) << resolved.error().front().message;
    owned.emplace(std::move(*resolved));
    ASSERT_EQ(owned->header.regions.size(), 5u);
    const std::array expected{false, true, false, false, true};
    for (size_t i = 0; i < expected.size(); ++i)
      EXPECT_EQ(owned->header.regions[i].map_f64_to_f32, expected[i]);
    EXPECT_EQ(owned->header.regions[1].target_options,
              (std::vector<std::string>{"sm_10", "map_f64_to_f32",
                                        "texmode_unified"}));
    EXPECT_EQ(owned->header.regions[4].target_options,
              (std::vector<std::string>{"sm_12", "texmode_unified",
                                        "map_f64_to_f32"}));
    for (size_t i = 0; i < owned->functions.size(); ++i) {
      auto& function = owned->functions[i];
      ASSERT_EQ(function.source_region, i + 1);
      ASSERT_EQ(function.body.size(), 1u);
      function.body.front() = std::make_unique<MapContextProbe>(
          std::move(function.body.front()), expected[i + 1]);
    }
    const auto checked = validateModule(*ast, *owned);
    ASSERT_TRUE(checked) << checked.error().front().message;
  }
  const auto checked = validateModule(*owned);
  ASSERT_TRUE(checked) << checked.error().front().message;
  EXPECT_FALSE(ResolvedSourceTargetRegion{}.map_f64_to_f32);
  EXPECT_EQ(owned->functions[0].source_target, "sm_10");
  EXPECT_EQ(owned->functions[1].source_target, "sm_11");
}

/** A replacement AST supplies its own effective option rather than the old header. */
TEST(SourceTargetOptions, AstValidationUsesReplacementSourceOption) {
  PtxSyntaxParser original{R"ptx(.version 1.5
.target sm_10, map_f64_to_f32
.func f() { ret; }
)ptx"};
  PtxSyntaxParser replacement{R"ptx(.version 1.5
.target sm_10, map_f64_to_f32x
.func f() { ret; }
)ptx"};
  auto ast = original.parseModule();
  auto retargeted = replacement.parseModule();
  ASSERT_TRUE(ast);
  ASSERT_TRUE(retargeted);
  auto resolved = resolveAndValidateModule(*ast);
  ASSERT_TRUE(resolved);
  auto& instruction = resolved->functions.front().body.front();
  instruction =
      std::make_unique<MapContextProbe>(std::move(instruction), false);
  EXPECT_TRUE(validateModule(*retargeted, *resolved));
  EXPECT_FALSE(validateModule(*ast, *resolved));
  EXPECT_FALSE(validateModule(*resolved));
  EXPECT_TRUE(resolved->header.regions[1].map_f64_to_f32);
}

/** Owned validation rejects typed/raw contradictions even without an instruction gate. */
TEST(SourceTargetOptions, RejectsForgedMapOptionStateAndSpelling) {
  PtxSyntaxParser parser{R"ptx(.version 1.5
.target sm_10, map_f64_to_f32
.func first() { ret; }
.target sm_11
.func second() { ret; }
)ptx"};
  auto ast = parser.parseModule();
  ASSERT_TRUE(ast);
  auto resolved = resolveAndValidateModule(*ast);
  ASSERT_TRUE(resolved);
  const auto expect_mismatch = [&] {
    const auto checked = validateModule(*resolved);
    ASSERT_FALSE(checked);
    EXPECT_TRUE(
        std::ranges::any_of(checked.error(), [](const auto& diagnostic) {
          return diagnostic.kind ==
                     checker::CheckDiagnosticKind::ModuleSourceMismatch &&
                 diagnostic.message.find("map_f64_to_f32") != std::string::npos;
        }));
  };
  auto& with_option = resolved->header.regions[1];
  auto& without_option = resolved->header.regions[2];
  with_option.map_f64_to_f32 = false;
  expect_mismatch();
  with_option.map_f64_to_f32 = true;
  with_option.target_options.back() = "map_f64_to_f32x";
  expect_mismatch();
  with_option.target_options.pop_back();
  expect_mismatch();
  with_option.target_options.push_back("map_f64_to_f32");
  without_option.map_f64_to_f32 = true;
  expect_mismatch();
  without_option.map_f64_to_f32 = false;
  without_option.target_options.push_back("map_f64_to_f32");
  expect_mismatch();
  without_option.target_options.pop_back();
  resolved->header.regions.front().map_f64_to_f32 = true;
  expect_mismatch();
  resolved->header.regions.front().map_f64_to_f32 = false;
  EXPECT_TRUE(validateModule(*resolved));
}

/** Lexically valid profiles outside the explicit catalog remain unknown. */
TEST(SourceTargetOptions, UnknownHistoricalTargetDoesNotInheritMapContext) {
  PtxSyntaxParser parser{R"ptx(.version 1.5
.target sm_10, map_f64_to_f32
.func first() { ret; }
.target sm_14
.func second() { ret; }
)ptx"};
  auto ast = parser.parseModule();
  ASSERT_TRUE(ast);
  auto resolved = resolveModuleOnly(*ast);
  ASSERT_TRUE(resolved);
  EXPECT_FALSE(resolved->header.regions[2].map_f64_to_f32);
  for (const auto& result :
       {validateModule(*ast, *resolved), validateModule(*resolved)}) {
    ASSERT_FALSE(result);
    EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& diagnostic) {
      return diagnostic.kind == checker::CheckDiagnosticKind::UnknownTarget;
    }));
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
