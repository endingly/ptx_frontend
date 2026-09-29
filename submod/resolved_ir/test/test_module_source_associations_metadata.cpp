#include "test_module_source_associations_support.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using module_source_test_support::hasCheckerKind;
using module_source_test_support::parseModule;

TEST(ModuleValidationContract, ReportsMalformedSourceMapAndOpcodeMismatch) {
  const auto ast = parseModule(R"ptx(
.func first() {
  ret;
}
)ptx");
  ASSERT_TRUE(ast);

  auto malformed_map = resolveModule(*ast);
  ASSERT_TRUE(malformed_map.has_value())
      << malformed_map.error().front().message;
  malformed_map->functions.front().instruction_ranges.clear();
  const auto map_result = checkModuleAvailability(*ast, *malformed_map);
  ASSERT_FALSE(map_result.has_value());
  EXPECT_TRUE(hasCheckerKind(
      map_result.error(), checker::CheckDiagnosticKind::ModuleSourceMismatch));

  auto wrong_opcode = resolveModule(*ast);
  ASSERT_TRUE(wrong_opcode.has_value()) << wrong_opcode.error().front().message;
  wrong_opcode->functions.front().instruction_opcodes.front() = "add";
  const auto opcode_result = checkModuleAvailability(*ast, *wrong_opcode);
  ASSERT_FALSE(opcode_result.has_value());
  EXPECT_TRUE(
      hasCheckerKind(opcode_result.error(),
                     checker::CheckDiagnosticKind::ModuleSourceMismatch));
}

TEST(ModuleValidationContract, RejectsChangedSameShapeSource) {
  const auto original = parseModule(R"ptx(
.func first() {
  .reg .u32 %r<2>;
  mov.u32 %r0, %r0;
}
)ptx");
  const auto changed = parseModule(R"ptx(
.func first() {
  .reg .u32 %r<2>;
  mov.u32 %r1, %r0;
}
)ptx");
  ASSERT_TRUE(original);
  ASSERT_TRUE(changed);
  const auto resolved = resolveModule(*original);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  const auto result = checkModuleAvailability(*changed, *resolved);
  ASSERT_FALSE(result.has_value());
  EXPECT_TRUE(hasCheckerKind(
      result.error(), checker::CheckDiagnosticKind::ModuleSourceMismatch));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
