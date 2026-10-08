#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include <gtest/gtest.h>

TEST(ResolvedIrPublicHeaders, CompatibilityAggregateCompilesStandalone) {
  using namespace ptx_frontend::resolved_ir;
  EXPECT_EQ(AddIntegerNoSat::kind, InstructionKind::AddIntegerNoSat);
}
