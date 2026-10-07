#include <ptx_frontend/resolved_ir/ptx_resolved_ir_descriptors.hpp>

#include <gtest/gtest.h>

TEST(ResolvedIrPublicHeaders, DescriptorsCompileStandalone) {
  using ptx_frontend::resolved_ir::check_end::OperandPresence;
  EXPECT_EQ(OperandPresence::Required, OperandPresence::Required);
}
