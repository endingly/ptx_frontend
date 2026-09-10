#include <gtest/gtest.h>

#include <optional>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse a complete module and retain a useful failure in the test output. */
std::optional<syntax_ast::AstModule> parseModule(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto parsed = parser.parseModule();
  if (!parsed || !parsed.diagnostics.empty()) {
    ADD_FAILURE() << (parsed.diagnostics.empty()
                          ? "PTX source did not parse."
                          : parsed.diagnostics.front().message);
    return std::nullopt;
  }
  return std::move(*parsed);
}

/** Test whether a resolution diagnostic carries the requested checker kind. */
bool hasCheckerKind(const ModuleResolveDiagnostics& diagnostics,
                    checker::CheckDiagnosticKind kind) {
  for (const auto& diagnostic : diagnostics) {
    if (diagnostic.checker_kind && *diagnostic.checker_kind == kind)
      return true;
  }
  return false;
}

/** Test whether a checker result carries the requested diagnostic kind. */
bool hasCheckerKind(const checker::CheckDiagnostics& diagnostics,
                    checker::CheckDiagnosticKind kind) {
  for (const auto& diagnostic : diagnostics) {
    if (diagnostic.kind == kind)
      return true;
  }
  return false;
}

TEST(ModuleValidationContract, SeparatesResolutionFromCompleteValidation) {
  const auto ast = parseModule(R"ptx(
.version 7.8
.entry kernel() {
  ret;
}
)ptx");
  ASSERT_TRUE(ast);

  const auto only = resolveModuleOnly(*ast);
  ASSERT_TRUE(only.has_value()) << only.error().front().message;

  const auto available = resolveModule(*ast);
  EXPECT_TRUE(available.has_value())
      << (available ? "" : available.error().front().message);

  const auto strict = resolveAndValidateModule(*ast);
  ASSERT_FALSE(strict.has_value());
  EXPECT_TRUE(hasCheckerKind(
      strict.error(), checker::CheckDiagnosticKind::MissingValidationContext));

  const auto default_policy = validateModule(*ast, *only);
  ASSERT_FALSE(default_policy.has_value());
  EXPECT_TRUE(
      hasCheckerKind(default_policy.error(),
                     checker::CheckDiagnosticKind::MissingValidationContext));
}

TEST(ModuleValidationContract,
     ResolvesInvalidInstructionBeforeAvailabilityCheck) {
  const auto ast = parseModule(R"ptx(
.version 7.8
.target sm_80
.entry kernel() .reqnctapercluster 2, 1 {
  .reg .u32 %r;
  mov.u32 %r, %cluster_ctarank;
}
)ptx");
  ASSERT_TRUE(ast);

  const auto only = resolveModuleOnly(*ast);
  ASSERT_TRUE(only.has_value()) << only.error().front().message;

  const auto available = resolveModule(*ast);
  ASSERT_FALSE(available.has_value());
  EXPECT_TRUE(
      hasCheckerKind(available.error(),
                     checker::CheckDiagnosticKind::UnsupportedAvailability));

  const auto strict = resolveAndValidateModule(*ast);
  ASSERT_FALSE(strict.has_value());
  EXPECT_TRUE(hasCheckerKind(
      strict.error(), checker::CheckDiagnosticKind::UnsupportedAvailability));
}

TEST(ModuleValidationContract, UnknownTargetIsRejectedByBothPipelines) {
  const auto ast = parseModule(R"ptx(
.version 7.8
.target sm_123a
.entry kernel() {
  ret;
}
)ptx");
  ASSERT_TRUE(ast);

  const auto only = resolveModuleOnly(*ast);
  ASSERT_TRUE(only.has_value()) << only.error().front().message;

  const auto available = resolveModule(*ast);
  ASSERT_FALSE(available.has_value());
  EXPECT_TRUE(hasCheckerKind(available.error(),
                             checker::CheckDiagnosticKind::UnknownTarget));

  const auto strict = resolveAndValidateModule(*ast);
  ASSERT_FALSE(strict.has_value());
  EXPECT_TRUE(hasCheckerKind(strict.error(),
                             checker::CheckDiagnosticKind::UnknownTarget));
}

TEST(ModuleValidationContract, EnforcesPtxVersionAvailabilityAtBoundary) {
  const auto too_old_ast = parseModule(R"ptx(
.version 1.2
.target sm_80
.entry kernel() {
  .reg .u32 %r;
  mov.u32 %r, %laneid;
}
)ptx");
  const auto supported_ast = parseModule(R"ptx(
.version 1.3
.target sm_80
.entry kernel() {
  .reg .u32 %r;
  mov.u32 %r, %laneid;
}
)ptx");
  ASSERT_TRUE(too_old_ast);
  ASSERT_TRUE(supported_ast);

  const auto too_old_only = resolveModuleOnly(*too_old_ast);
  ASSERT_TRUE(too_old_only.has_value()) << too_old_only.error().front().message;
  const auto supported_only = resolveModuleOnly(*supported_ast);
  ASSERT_TRUE(supported_only.has_value())
      << supported_only.error().front().message;

  const auto rejected = resolveModule(*too_old_ast);
  ASSERT_FALSE(rejected.has_value());
  EXPECT_TRUE(hasCheckerKind(
      rejected.error(), checker::CheckDiagnosticKind::UnsupportedPtxVersion));
  EXPECT_TRUE(resolveModule(*supported_ast).has_value());
}

TEST(ModuleValidationContract, AllowsRetargetedSeparatelyParsedSameBody) {
  const auto source = parseModule(R"ptx(
.version 7.8
.func first() {
  ret;
}
)ptx");
  const auto retargeted = parseModule(R"ptx(
.version 7.8
.target sm_80
.func first() {
  ret;
}
)ptx");
  ASSERT_TRUE(source);
  ASSERT_TRUE(retargeted);
  const auto resolved = resolveModuleOnly(*source);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  EXPECT_TRUE(checkModuleAvailability(*retargeted, *resolved));
  EXPECT_TRUE(resolveAndValidateModule(*retargeted).has_value());
}

TEST(ModuleValidationContract, KeepsTargetRegionsAfterIrFunctionReordering) {
  const auto ast = parseModule(R"ptx(
.version 7.8
.target sm_90
.entry cluster_user() {
  .reg .u32 %r;
  mov.u32 %r, %cluster_ctarank;
}
.target sm_80
.entry ordinary() { ret; }
)ptx");
  ASSERT_TRUE(ast);
  auto resolved = resolveModuleOnly(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 2u);
  EXPECT_EQ(resolved->functions[0].source_target, "sm_90");
  EXPECT_EQ(resolved->functions[1].source_target, "sm_80");
  EXPECT_EQ(resolved->functions[0].source_version, (checker::PtxVersion{7, 8}));
  std::swap(resolved->functions[0], resolved->functions[1]);
  const auto checked = validateModule(*ast, *resolved);
  EXPECT_TRUE(checked.has_value())
      << (checked ? "" : checked.error().front().message);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
