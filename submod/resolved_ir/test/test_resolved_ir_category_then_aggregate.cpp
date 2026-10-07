#include <ptx_frontend/resolved_ir/model/arithmetic/add.gen.hpp>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection/set.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include <gtest/gtest.h>

/** Narrow leaves remain valid when followed by the generated aggregate. */
TEST(ResolvedIrPublicHeaders, CategoryThenAggregateIncludeOrderCompiles) {
  SUCCEED();
}
