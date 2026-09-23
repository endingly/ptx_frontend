#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_model.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include <ptx_frontend/resolved_ir/checker/arithmetic.gen.hpp>
#include <ptx_frontend/resolved_ir/checker/comparison_and_selection.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic.gen.hpp>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection.gen.hpp>
#include <ptx_frontend/resolved_ir/resolution/arithmetic.gen.hpp>
#include <ptx_frontend/resolved_ir/resolution/comparison_and_selection.gen.hpp>

#include <gtest/gtest.h>

TEST(ResolvedIrPublicHeaders, ModelCheckerResolutionIncludeOrderCompiles) {
  SUCCEED();
}
