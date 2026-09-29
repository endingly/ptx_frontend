#include "test_module_source_associations_support.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using module_source_test_support::hasCheckerKind;
using module_source_test_support::parseModule;

TEST(ModuleValidationContract, OwnsInstructionSourcesAfterAstDestruction) {
  /** Return only the IR, destroying both source storage and syntax nodes. */
  const auto resolve_local = [] {
    const auto ast = parseModule(R"ptx(
.version 7.8
.target sm_80
.func local() { { ret; } }
)ptx");
    return resolveModule(*ast);
  };
  const auto resolved = resolve_local();
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& function = resolved->functions.front();
  ASSERT_EQ(function.instruction_ranges.size(), 1u);
  EXPECT_EQ(function.instruction_ranges.front().start.line, 4);
  EXPECT_EQ(function.instruction_opcodes.front(), "ret");
  EXPECT_EQ(function.source_target, "sm_80");
  EXPECT_FALSE(function.source_identity.empty());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
