#include <ptx_frontend/resolved_ir/ptx_resolved_unified_id.hpp>

#include <gtest/gtest.h>

namespace ptx_frontend::resolved_ir {

/** Verify the shared `.unified` value type is usable without model headers. */
TEST(ResolvedIrPublicHeaders, UnifiedIdHeaderCompilesStandalone) {
  constexpr ResolvedUnifiedId unified_id{.upper = 1u, .lower = 2u};
  EXPECT_EQ(unified_id.upper, 1u);
  EXPECT_EQ(unified_id.lower, 2u);
}

}  // namespace ptx_frontend::resolved_ir
