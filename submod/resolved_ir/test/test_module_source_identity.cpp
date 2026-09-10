#include <gtest/gtest.h>

#include <optional>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
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

  const auto module = resolveModule(*original);
  ASSERT_TRUE(module.has_value());
  const auto validation = checkModuleAvailability(
      *changed, *module);
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

  const auto module = resolveModule(*original);
  ASSERT_TRUE(module.has_value());
  const auto validation = checkModuleAvailability(
      *changed, *module);
  ASSERT_FALSE(validation.has_value());
  EXPECT_TRUE(hasDiagnostic(
      validation.error(), checker::CheckDiagnosticKind::ModuleSourceMismatch));
}


}  // namespace
}  // namespace ptx_frontend::resolved_ir
