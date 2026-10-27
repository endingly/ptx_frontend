#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse one module and retain parser failures as assertions in the caller. */
std::optional<syntax_ast::AstModule> parseModule(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto module = parser.parseModule();
  EXPECT_TRUE(module.has_value());
  EXPECT_TRUE(module.diagnostics.empty());
  if (!module || !module.diagnostics.empty())
    return std::nullopt;
  return std::move(*module);
}

/** Return whether validation emitted one requested structured checker category. */
bool hasDiagnostic(const checker::CheckDiagnostics& diagnostics,
                   checker::CheckDiagnosticKind kind) {
  for (const auto& diagnostic : diagnostics) {
    if (diagnostic.kind == kind)
      return true;
  }
  return false;
}

/** Nested block boundaries are part of the structural source identity. */
TEST(ModuleSourceIdentity, RejectsDifferentNestedBodyShape) {
  const auto original = parseModule(R"ptx(
.func f() { { ret; } ret; }
)ptx");
  const auto changed = parseModule(R"ptx(
.func f() { { ret; ret; } }
)ptx");
  ASSERT_TRUE(original);
  ASSERT_TRUE(changed);

  const auto module = resolveModuleOnly(*original);
  ASSERT_TRUE(module.has_value());
  const auto validation = validateModule(
      *changed, *module, ModuleValidationPolicy::AvailableContext);
  ASSERT_FALSE(validation.has_value());
  EXPECT_TRUE(hasDiagnostic(
      validation.error(), checker::CheckDiagnosticKind::ModuleSourceMismatch));
}

/** Global declarations include their type spelling in module source identity. */
TEST(ModuleSourceIdentity, RejectsChangedGlobalType) {
  const auto original = parseModule(R"ptx(
.global .u32 value;
.func f() { ret; }
)ptx");
  const auto changed = parseModule(R"ptx(
.global .u64 value;
.func f() { ret; }
)ptx");
  ASSERT_TRUE(original);
  ASSERT_TRUE(changed);

  const auto module = resolveModuleOnly(*original);
  ASSERT_TRUE(module.has_value());
  const auto validation = validateModule(
      *changed, *module, ModuleValidationPolicy::AvailableContext);
  ASSERT_FALSE(validation.has_value());
  EXPECT_TRUE(hasDiagnostic(
      validation.error(), checker::CheckDiagnosticKind::ModuleSourceMismatch));
}

/** Retargeting rechecks unsized formals after structure matches the owned model. */
TEST(ModuleSourceIdentity, RechecksUnsizedFormalAfterRetarget) {
  const auto original = parseModule(R"ptx(
.version 6.0
.target sm_30
.func f(.param .b8 bytes[]) { ret; }
)ptx");
  const auto retargeted = parseModule(R"ptx(
.version 6.0
.target sm_20
.func f(.param .b8 bytes[]) { ret; }
)ptx");
  ASSERT_TRUE(original);
  ASSERT_TRUE(retargeted);

  const auto module = resolveModuleOnly(*original);
  ASSERT_TRUE(module.has_value());
  const auto validation = validateModule(
      *retargeted, *module, ModuleValidationPolicy::AvailableContext);
  ASSERT_FALSE(validation.has_value());
  EXPECT_TRUE(
      hasDiagnostic(validation.error(),
                    checker::CheckDiagnosticKind::UnsupportedAvailability));
}

/** Modern opaque storage availability uses the validation entry point's version. */
TEST(ModuleSourceIdentity, OpaqueStorageUsesReplacementVersion) {
  for (const auto kind : {"texref", "samplerref", "surfref"}) {
    SCOPED_TRACE(kind);
    const std::string body = std::string{".target sm_80\n.global ."} + kind +
                             " resource;\n.entry kernel() { ret; }\n";
    const std::string current_source = ".version 9.3\n" + body;
    const std::string old_source = ".version 1.4\n" + body;
    const auto current = parseModule(current_source);
    const auto old = parseModule(old_source);
    ASSERT_TRUE(current);
    ASSERT_TRUE(old);
    for (const auto policy : {ModuleValidationPolicy::RequireCompleteContext,
                              ModuleValidationPolicy::AvailableContext}) {
      auto module = resolveModuleOnly(*current);
      ASSERT_TRUE(module);
      ASSERT_TRUE(validateModule(*current, *module, policy));
      ASSERT_TRUE(validateModule(*module, policy));
      const auto old_replacement = validateModule(*old, *module, policy);
      ASSERT_FALSE(old_replacement);
      EXPECT_TRUE(hasDiagnostic(old_replacement.error(),
                                checker::CheckDiagnosticKind::RuleViolation));
      for (auto& region : module->header.regions)
        region.version = checker::PtxVersion{1, 4};
      for (auto& function : module->functions)
        function.source_version = checker::PtxVersion{1, 4};
      EXPECT_TRUE(validateModule(*current, *module, policy));
      const auto owned = validateModule(*module, policy);
      ASSERT_FALSE(owned);
      EXPECT_TRUE(hasDiagnostic(owned.error(),
                                checker::CheckDiagnosticKind::RuleViolation));
      EXPECT_EQ(module->functions.front().source_version,
                (checker::PtxVersion{1, 4}));
    }
  }
}

/** Missing effective versions remain partial context instead of using retained history. */
TEST(ModuleSourceIdentity, OpaqueStorageMissingVersionUsesValidationPolicy) {
  constexpr std::string_view current_source =
      ".version 9.3\n.target sm_80\n.global .texref resource;\n"
      ".entry kernel() { ret; }\n";
  constexpr std::string_view partial_source =
      ".target sm_80\n.global .texref resource;\n.entry kernel() { ret; }\n";
  const auto current = parseModule(current_source);
  const auto partial = parseModule(partial_source);
  ASSERT_TRUE(current);
  ASSERT_TRUE(partial);
  auto retained_old = resolveModuleOnly(*current);
  ASSERT_TRUE(retained_old);
  for (auto& region : retained_old->header.regions)
    region.version = checker::PtxVersion{1, 4};
  for (auto& function : retained_old->functions)
    function.source_version = checker::PtxVersion{1, 4};
  EXPECT_TRUE(validateModule(*partial, *retained_old,
                             ModuleValidationPolicy::AvailableContext));
  const auto strict_replacement = validateModule(
      *partial, *retained_old, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(strict_replacement);
  EXPECT_TRUE(
      hasDiagnostic(strict_replacement.error(),
                    checker::CheckDiagnosticKind::MissingValidationContext));
  const auto retained_partial = resolveModuleOnly(*partial);
  ASSERT_TRUE(retained_partial);
  EXPECT_TRUE(validateModule(*retained_partial,
                             ModuleValidationPolicy::AvailableContext));
  const auto strict_owned = validateModule(
      *retained_partial, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(strict_owned);
  EXPECT_TRUE(
      hasDiagnostic(strict_owned.error(),
                    checker::CheckDiagnosticKind::MissingValidationContext));
  EXPECT_TRUE(validateModule(*current, *retained_partial,
                             ModuleValidationPolicy::RequireCompleteContext));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
