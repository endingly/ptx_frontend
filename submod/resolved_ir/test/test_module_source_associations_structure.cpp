#include "test_module_source_associations_support.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using module_source_test_support::hasCheckerKind;
using module_source_test_support::parseModule;

TEST(ModuleValidationContract, ReportsMissingAndExtraFunctions) {
  const auto two_functions = parseModule(R"ptx(
.func first() { ret; }
.func second() { ret; }
)ptx");
  ASSERT_TRUE(two_functions);
  auto missing = resolveModule(*two_functions);
  ASSERT_TRUE(missing.has_value()) << missing.error().front().message;
  ASSERT_EQ(missing->functions.size(), 2u);
  missing->functions.pop_back();
  const auto missing_result = checkModuleAvailability(*two_functions, *missing);
  ASSERT_FALSE(missing_result.has_value());
  EXPECT_TRUE(
      hasCheckerKind(missing_result.error(),
                     checker::CheckDiagnosticKind::ModuleSourceMismatch));

  const auto one_function = parseModule(R"ptx(
.func first() { ret; }
)ptx");
  ASSERT_TRUE(one_function);
  auto extra = resolveModule(*one_function);
  ASSERT_TRUE(extra.has_value()) << extra.error().front().message;
  auto duplicate = resolveModule(*one_function);
  ASSERT_TRUE(duplicate.has_value()) << duplicate.error().front().message;
  extra->functions.push_back(std::move(duplicate->functions.front()));
  const auto extra_result = checkModuleAvailability(*one_function, *extra);
  ASSERT_FALSE(extra_result.has_value());
  EXPECT_TRUE(
      hasCheckerKind(extra_result.error(),
                     checker::CheckDiagnosticKind::ModuleSourceMismatch));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
