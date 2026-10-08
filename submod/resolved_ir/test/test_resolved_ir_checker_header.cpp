#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker_support.hpp>

#include <gtest/gtest.h>

/** The public checker support header compiles without generated forms. */
TEST(ResolvedIrPublicHeaders, CheckerCompilesStandalone) {
  using ptx_frontend::resolved_ir::checker::CheckDiagnosticKind;
  EXPECT_EQ(CheckDiagnosticKind::RuleViolation,
            CheckDiagnosticKind::RuleViolation);
}
