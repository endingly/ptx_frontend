#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <variant>

#include <ptx_frontend/resolved_ir/model/arithmetic/div.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/mad.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/ld.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/multimem.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/prefetch.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/shfl.gen.hpp>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/vote.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Independent source options and operand omissions survive one owned module. */
TEST(SourceFormIntegration, PreservesIndependentModernSourceStates) {
  for (const bool debug : {false, true}) {
    for (const bool policy : {false, true}) {
      for (const bool weak : {false, true}) {
        for (const bool scoped : {false, true}) {
          SCOPED_TRACE(testing::Message() << debug << policy << weak << scoped);
          std::optional<ResolvedModule> owned;
          {
            const std::string source =
                std::string{".version 9.3\n.target sm_90"} +
                (debug ? ", debug" : "") + R"ptx(
.address_size 64
.global .align 64 .b8 memory[64];
.const .align 64 .b8 constant_map[64];
.entry kernel(.param .align 64 .b8 input_map[64]) {
  .reg .u32 %r;
  .reg .b64 %policy;
  ld.global.L2::cache_hint.u32 %r, [memory])ptx" +
                (policy ? ", %policy;\n" : ";\n") + "  multimem.ld_reduce." +
                (weak ? "weak." : "") + (scoped ? "gpu." : "") +
                R"ptx(add.u32 %r, [memory];
  multimem.st.sys.u32 [memory], %r;
  prefetch.tensormap [input_map];
  prefetch.tensormap [constant_map];
  prefetchu.L1 [memory];
}
)ptx";
            const auto ast = test_helpers::parseModule(source);
            ASSERT_MODULE_PARSE_SUCCEEDS(ast);
            auto resolved = resolveAndValidateModule(*ast);
            ASSERT_TRUE(resolved) << resolved.error().front().message;
            ASSERT_TRUE(validateModule(*ast, *resolved));
            owned.emplace(std::move(*resolved));
          }
          const auto strict = [&] {
            return validateModule(
                *owned, ModuleValidationPolicy::RequireCompleteContext);
          };
          ASSERT_TRUE(strict());
          auto& region = owned->header.regions.back();
          EXPECT_EQ(region.target_options.size(), debug ? 2u : 1u);
          EXPECT_FALSE(region.map_f64_to_f32);
          auto& function = owned->functions.front();
          const auto& load =
              dynamic_cast<const LdGlobalU32L2CacheHint&>(*function.body[0]);
          EXPECT_EQ(load.cache_policy.has_value(), policy);
          const auto& reduction =
              dynamic_cast<const MultimemLdReduceIntegerAddScalarDefaultWeak&>(
                  *function.body[1]);
          EXPECT_EQ(
              reduction.semantics.value,
              weak ? MemoryConsistency::Weak : MemoryConsistency::Omitted);
          EXPECT_EQ(reduction.semantics.locs.empty(), !weak);
          EXPECT_EQ(reduction.scope.value,
                    scoped ? MemoryScope::Gpu : MemoryScope::None);
          EXPECT_EQ(reduction.scope.locs.empty(), !scoped);
          auto& hint =
              dynamic_cast<PrefetchGenericTensormap&>(*function.body[3]);
          auto& parameter =
              std::get<ResolvedSymbolRef>(hint.address.value.base);
          EXPECT_EQ(parameter.address_state_space,
                    base::DeclarationStateSpace::Parameter);
          EXPECT_EQ(parameter.enclosing_function_kind,
                    EnclosingFunctionKind::Entry);
          region.map_f64_to_f32 = true;
          EXPECT_FALSE(strict());
          region.map_f64_to_f32 = false;
          ASSERT_TRUE(strict());
          parameter.enclosing_function_kind = EnclosingFunctionKind::Device;
          hint.address.value.enclosing_function_kind =
              EnclosingFunctionKind::Device;
          EXPECT_FALSE(strict());
          EXPECT_FALSE(region.map_f64_to_f32);
          parameter.enclosing_function_kind = EnclosingFunctionKind::Entry;
          hint.address.value.enclosing_function_kind =
              EnclosingFunctionKind::Entry;
          EXPECT_TRUE(strict());
        }
      }
    }
  }
}

/** Legacy identities and modern address contracts share source-ordered regions. */
TEST(SourceFormIntegration, RevalidatesMixedLegacyAndModernRegions) {
  std::optional<ResolvedModule> owned;
  {
    const std::string source = R"ptx(
.version 9.3
.target sm_12, map_f64_to_f32
.address_size 64
.func mapped() {
  .reg .f64 %d<3>;
  .reg .f32 %f<4>;
  .reg .pred %p<2>;
  div.rn.f64 %d0, %d1, %d2;
  rcp.rn.f64 %d0, %d1;
  sqrt.rn.f64 %d0, %d1;
  mad.f32 %f0, %f1, %f2, %f3;
  vote.all.pred %p0, !%p1;
}
.target sm_30
.func shuffled() {
  .reg .b32 %r<2>;
  .reg .pred %p;
  vote.ballot.b32 %r0, %p;
  shfl.up.b32 %r0|%p, %r1, 1, 31;
}
.target sm_90
.entry modern(.param .u32 input_map) {
  prefetch.tensormap [input_map];
}
)ptx";
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto resolved = resolveAndValidateModule(*ast);
    ASSERT_TRUE(resolved) << resolved.error().front().message;
    ASSERT_TRUE(validateModule(*ast, *resolved));
    owned.emplace(std::move(*resolved));
  }
  const auto strict = [&] {
    return validateModule(*owned,
                          ModuleValidationPolicy::RequireCompleteContext);
  };
  ASSERT_TRUE(strict());
  ASSERT_EQ(owned->header.regions.size(), 4u);
  EXPECT_TRUE(owned->header.regions[1].map_f64_to_f32);
  EXPECT_FALSE(owned->header.regions[2].map_f64_to_f32);
  EXPECT_FALSE(owned->header.regions[3].map_f64_to_f32);
  const auto& mapped = owned->functions[0].body;
  EXPECT_EQ(mapped[0]->instruction_kind(), DivRnF64::kind);
  EXPECT_EQ(mapped[3]->instruction_kind(), MadImplicitF32::kind);
  EXPECT_EQ(mapped[4]->instruction_kind(), VoteAllPred::kind);
  EXPECT_EQ(owned->functions[1].body[0]->instruction_kind(),
            VoteBallotB32::kind);
  EXPECT_EQ(owned->functions[1].body[1]->instruction_kind(), ShflUpB32::kind);
  owned->header.regions[1].map_f64_to_f32 = false;
  EXPECT_FALSE(strict());
  owned->header.regions[1].map_f64_to_f32 = true;
  auto& hint = dynamic_cast<PrefetchGenericTensormap&>(
      *owned->functions[2].body.front());
  auto& parameter = std::get<ResolvedSymbolRef>(hint.address.value.base);
  parameter.address_state_space = base::DeclarationStateSpace::Global;
  EXPECT_FALSE(strict());
  parameter.address_state_space = base::DeclarationStateSpace::Parameter;
  EXPECT_TRUE(strict());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
