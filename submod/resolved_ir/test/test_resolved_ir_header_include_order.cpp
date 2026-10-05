#include <ptx_frontend/resolved_ir/ptx_instruction_base.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_module.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/add.gen.hpp>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection/set.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include <gtest/gtest.h>

/** Base, module, resolution, leaves, and aggregate coexist in that order. */
TEST(ResolvedIrPublicHeaders, ModelCheckerResolutionIncludeOrderCompiles) {
  SUCCEED();
}
