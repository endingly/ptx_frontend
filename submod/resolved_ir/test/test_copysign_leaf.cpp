#include <gtest/gtest.h>

#include <variant>

#include <ptx_frontend/resolved_ir/model/arithmetic/copysign/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/copysign/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/copysign/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker_support.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_support.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using checker::PtxVersion;

TEST(CopysignCompleteness, RetainsSignThenMagnitudeSourceIdentity) {
  const auto parsed =
      test_helpers::parseInstruction("copysign.f64 %fd0, %fd1, %fd2;");
  ASSERT_TRUE(parsed.has_value());
  const auto resolved = resolve<Copysign>(*parsed);
  ASSERT_TRUE(resolved.has_value());
  const auto& variant = std::get<Copysign::F64>(resolved->variant);
  EXPECT_EQ(std::get<ResolvedRegisterRef>(variant.sign_source.value).spelling,
            "%fd1");
  EXPECT_EQ(
      std::get<ResolvedRegisterRef>(variant.magnitude_source.value).spelling,
      "%fd2");
}

TEST(CopysignCompleteness, RejectsWrongTypesAndChecksAvailability) {
  for (const auto source :
       {"copysign.f16 %h0, %h1, %h2;", "copysign.f32 _, %f1, %f2;"}) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseInstruction(source);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_FALSE(resolve<Copysign>(*parsed).has_value());
  }
  const auto parsed =
      test_helpers::parseInstruction("copysign.f32 %f0, 1.0, -2.0;");
  ASSERT_TRUE(parsed.has_value());
  const auto resolved = resolve<Copysign>(*parsed);
  ASSERT_TRUE(resolved.has_value());
  EXPECT_TRUE(checker::check(
                  *resolved,
                  checker::Context{.target = {.ptx_version = PtxVersion{2, 0},
                                              .sm_version = 20}})
                  .has_value());
}


}  // namespace
}  // namespace ptx_frontend::resolved_ir
