#include <ptx_frontend/resolved_ir/ptx_resolved_ir_foundation.hpp>

#include <gtest/gtest.h>

TEST(ResolvedIrPublicHeaders, FoundationCompilesStandalone) {
  using ptx_frontend::resolved_ir::checker::PtxVersion;
  EXPECT_EQ((PtxVersion{.major = 9, .minor = 3}.major), 9);
}
