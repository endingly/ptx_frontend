#include "test_module_source_associations_support.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using module_source_test_support::hasCheckerKind;
using module_source_test_support::parseModule;

TEST(ModuleValidationContract, MatchesFunctionsIndependentlyOfIrVectorOrder) {
  const auto ast = parseModule(R"ptx(
.func first() {
  ret;
}
.func second() {
  ret;
}
)ptx");
  ASSERT_TRUE(ast);
  auto resolved = resolveModule(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 2u);
  std::swap(resolved->functions[0], resolved->functions[1]);

  EXPECT_TRUE(checkModuleAvailability(*ast, *resolved));
}

TEST(ModuleValidationContract, KeepsPrototypeAndDefinitionScopesDistinct) {
  const auto ast = parseModule(R"ptx(
.func callee(.param .u32 input);
.func callee(.param .u32 input) {
  ret;
}
)ptx");
  ASSERT_TRUE(ast);
  const auto resolved = resolveModule(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 2u);

  const auto& prototype = resolved->functions[0];
  const auto& definition = resolved->functions[1];
  EXPECT_EQ(prototype.symbol_id, definition.symbol_id);
  EXPECT_NE(prototype.declaration_scope, definition.declaration_scope);
  EXPECT_EQ(resolved->symbols.functionScope(prototype.range),
            prototype.declaration_scope);
  EXPECT_EQ(resolved->symbols.functionScope(definition.range),
            definition.declaration_scope);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
