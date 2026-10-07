#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>

static_assert(ptx_frontend::resolved_ir::ResolvedFieldType<
              ptx_frontend::resolved_ir::ScalarType>);
static_assert(!ptx_frontend::resolved_ir::ResolvedFieldType<double>);

#include <gtest/gtest.h>

TEST(ResolvedIrPublicHeaders, ResolutionDetailCompilesStandalone) {
  using ptx_frontend::resolved_ir::ResolvedInstructionFields;
  EXPECT_TRUE(ResolvedInstructionFields{}.modifiers.empty());
}
