#include <ptx_frontend/resolved_ir/ptx_resolved_ir_module.hpp>

#include <gtest/gtest.h>

namespace ptx_frontend::resolved_ir {

/** Verify the module layer is usable without the model aggregate. */
TEST(ResolvedIrPublicHeaders, ModuleHeaderCompilesStandalone) {
  ResolvedFunction function{};
  EXPECT_TRUE(function.body.empty());
}

}  // namespace ptx_frontend::resolved_ir
