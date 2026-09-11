#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker.hpp>

#include <gtest/gtest.h>

TEST(ResolvedIrPublicHeaders, CheckerCompilesStandalone) {
  using ptx_frontend::resolved_ir::checker::CheckDiagnosticKind;
  EXPECT_EQ(CheckDiagnosticKind::RuleViolation,
            CheckDiagnosticKind::RuleViolation);
}
