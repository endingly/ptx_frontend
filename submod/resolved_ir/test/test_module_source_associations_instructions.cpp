#include "test_module_source_associations_support.hpp"

#include <memory>
#include <vector>

#include <ptx_frontend/resolved_ir/model/control_flow/ret.gen.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

using module_source_test_support::hasCheckerKind;
using module_source_test_support::parseModule;

/** Source association rejects a removed instruction or an extra exact clone. */
TEST(ModuleValidationContract, ReportsMissingAndExtraInstructions) {
  const auto ast = parseModule(R"ptx(
.func first() {
  ret;
}
)ptx");
  ASSERT_TRUE(ast);

  auto missing = resolveModule(*ast);
  ASSERT_TRUE(missing.has_value()) << missing.error().front().message;
  ASSERT_EQ(missing->functions.front().body.size(), 1u);
  missing->functions.front().body.clear();
  const auto missing_result = checkModuleAvailability(*ast, *missing);
  ASSERT_FALSE(missing_result.has_value());
  EXPECT_TRUE(
      hasCheckerKind(missing_result.error(),
                     checker::CheckDiagnosticKind::ModuleSourceMismatch));

  auto extra = resolveModule(*ast);
  ASSERT_TRUE(extra.has_value()) << extra.error().front().message;
  auto& function = extra->functions.front();
  const auto& original_return =
      dynamic_cast<const RetBare&>(*function.body.front());
  /** Independent exact clones make body cardinality the only mismatch. */
  std::vector<std::unique_ptr<Instruction>> duplicated_body;
  duplicated_body.push_back(original_return.clone());
  duplicated_body.push_back(original_return.clone());
  function.body.swap(duplicated_body);
  function.instruction_ranges.push_back(function.instruction_ranges.front());
  function.instruction_opcodes.push_back(function.instruction_opcodes.front());
  const auto extra_result = checkModuleAvailability(*ast, *extra);
  ASSERT_FALSE(extra_result.has_value());
  EXPECT_TRUE(
      hasCheckerKind(extra_result.error(),
                     checker::CheckDiagnosticKind::ModuleSourceMismatch));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
