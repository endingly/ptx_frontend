#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_model.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "resolved_ir/checker/arithmetic.gen.hpp"
#include "resolved_ir/model/arithmetic.gen.hpp"
#include "resolved_ir/resolution/arithmetic.gen.hpp"

#include <gtest/gtest.h>

TEST(ResolvedIrPublicHeaders, ModelCheckerResolutionIncludeOrderCompiles) {
  SUCCEED();
}
