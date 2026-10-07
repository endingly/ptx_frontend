#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include <gtest/gtest.h>

TEST(ResolvedIrPublicHeaders, ResolutionCompilesStandalone) {
  using ptx_frontend::resolved_ir::ModuleValidationPolicy;
  EXPECT_EQ(ModuleValidationPolicy::AvailableContext,
            ModuleValidationPolicy::AvailableContext);
}
