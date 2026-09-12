#include <gtest/gtest.h>

#include <optional>
#include <string_view>

#include <ptx_frontend/base/ptx_target.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse source and retain any parser failure as a test assertion. */
std::optional<syntax_ast::AstModule> parseModule(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto module = parser.parseModule();
  EXPECT_TRUE(module.has_value());
  EXPECT_TRUE(module.diagnostics.empty());
  if (!module || !module.diagnostics.empty())
    return std::nullopt;
  return std::move(*module);
}

/** Count declaration-semantics diagnostics of one requested kind. */
size_t declarationDiagnosticCount(
    const ModuleResolveDiagnostics& diagnostics,
    declaration_semantics::DeclarationDiagnosticKind kind) {
  size_t count = 0;
  for (const ResolveDiagnostic& diagnostic : diagnostics) {
    if (diagnostic.declaration_kind == kind)
      ++count;
  }
  return count;
}

/** The legacy architecture remains a known low-feature target context. */
TEST(SourceTargetRegions, RecognizesSm20WithoutModernCapabilities) {
  const auto profile = base::find_target_profile("sm_20");
  ASSERT_TRUE(profile.has_value());
  EXPECT_EQ(profile->identity.architecture.number, 20u);
  EXPECT_TRUE(profile->enabled_family_features.empty());
  EXPECT_TRUE(profile->capabilities.empty());
}

/** A later supported target governs the formal declaration in its region. */
TEST(SourceTargetRegions, AcceptsFormalAfterUpgradeToSm30) {
  const auto ast = parseModule(R"ptx(
.version 6.0
.target sm_20
.address_size 64
.func early() { ret; }
.target sm_30
.func late(.param .b8 payload[]) { ret; }
)ptx");
  ASSERT_TRUE(ast);

  const auto resolved = resolveModule(*ast);
  ASSERT_TRUE(resolved.has_value())
      << (resolved.error().empty() ? "resolution failed"
                                   : resolved.error().front().message);
}

/** A later older target rejects an SM-30-only final unsized formal. */
TEST(SourceTargetRegions, RejectsFormalAfterDowngradeToSm20) {
  const auto ast = parseModule(R"ptx(
.version 6.0
.target sm_30
.address_size 64
.func early() { ret; }
.target sm_20
.func late(.param .b8 payload[]) { ret; }
)ptx");
  ASSERT_TRUE(ast);

  const auto resolved = resolveModule(*ast);
  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(
      declarationDiagnosticCount(
          resolved.error(), declaration_semantics::DeclarationDiagnosticKind::
                                UnsupportedParameterDeclaration),
      1u);
}

/** A single target still supplies the parameter availability context. */
TEST(SourceTargetRegions, AppliesSingleTargetToFormalAvailability) {
  const auto ast = parseModule(R"ptx(
.version 6.0
.target sm_20
.func late(.param .b8 payload[]) { ret; }
)ptx");
  ASSERT_TRUE(ast);

  const auto resolved = resolveModule(*ast);
  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(
      declarationDiagnosticCount(
          resolved.error(), declaration_semantics::DeclarationDiagnosticKind::
                                UnsupportedParameterDeclaration),
      1u);
}

/** Function-local declarations use their containing function's target region. */
TEST(SourceTargetRegions, AppliesDowngradeToNestedParametersAndCallPrototypes) {
  const auto ast = parseModule(R"ptx(
.version 6.0
.target sm_30
.address_size 64
.func early() { ret; }
.target sm_13
.func late() {
  .param .u32 staging;
  indirect: .callprototype _ (.param .u32 argument);
  ret;
}
)ptx");
  ASSERT_TRUE(ast);

  const auto resolved = resolveModule(*ast);
  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(
      declarationDiagnosticCount(
          resolved.error(), declaration_semantics::DeclarationDiagnosticKind::
                                UnsupportedParameterDeclaration),
      2u);
}

/** Targetless fragments retain their permissive target-dependent validation policy. */
TEST(SourceTargetRegions, RetainsTargetlessFragmentSupport) {
  const auto ast = parseModule(R"ptx(
.version 6.0
.func late(.param .b8 payload[]) { ret; }
)ptx");
  ASSERT_TRUE(ast);

  EXPECT_TRUE(resolveModule(*ast).has_value());
}

/** A supported target continues to apply until another target directive appears. */
TEST(SourceTargetRegions, RetainsUnchangedTargetRegion) {
  const auto ast = parseModule(R"ptx(
.version 6.0
.target sm_30
.func first(.param .b8 payload[]) { ret; }
.func second(.param .b8 payload[]) { ret; }
)ptx");
  ASSERT_TRUE(ast);

  EXPECT_TRUE(resolveModule(*ast).has_value());
}

/** An unrecognized later target clears an earlier target instead of inheriting it. */
TEST(SourceTargetRegions, UnknownTargetClearsEarlierDeclarationContext) {
  const auto ast = parseModule(R"ptx(
.version 6.0
.target sm_20
.target sm_123a
.func late(.param .b8 payload[]) { ret; }
)ptx");
  ASSERT_TRUE(ast);

  const auto resolved = resolveModule(*ast);
  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().checker_kind,
            checker::CheckDiagnosticKind::UnknownTarget);
  EXPECT_EQ(
      declarationDiagnosticCount(
          resolved.error(), declaration_semantics::DeclarationDiagnosticKind::
                                UnsupportedParameterDeclaration),
      0u);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
