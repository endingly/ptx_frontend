#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include <gtest/gtest.h>

TEST(ResolvedIrPublicHeaders, CompatibilityAggregateCompilesStandalone) {
  using ptx_frontend::resolved_ir::ModuleValidationPolicy;
  EXPECT_EQ(ModuleValidationPolicy::RequireCompleteContext,
            ModuleValidationPolicy::RequireCompleteContext);
}
