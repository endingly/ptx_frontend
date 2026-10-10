#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/fabric.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Wrap a candidate in a complete typed module with shared data and barrier. */
std::string fabric_source(std::string_view body,
                          std::string_view target = "sm_100",
                          std::string_view version = "9.3") {
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) + R"ptx(
.address_size 64
.shared .align 16 .b8 data[512];
.shared .align 16 .b8 bar[16];
.global .align 16 .b8 global_data[64];
.visible .entry kernel() {
  .reg .b32 %endpoint;
  .reg .s32 %signed_endpoint;
  .reg .b64 %dataoff;
  .reg .s64 %counteroff;
  .reg .b64 %wide_endpoint;
  .reg .b32 %narrow_off;
  .reg .b32 %size;
  .reg .b16 %bytemask;
)ptx" + std::string(body) +
         "\n  ret;\n}\n";
}

/** Resolve a complete candidate and validate all owned references. */
bool accepts_fabric(std::string_view source) {
  const auto ast = test_helpers::parseModule(source);
  if (!ast.has_value() || !ast.diagnostics.empty())
    return false;
  return resolveAndValidateModule(*ast).has_value();
}

/** Six families have exact public identities and separate completion contracts. */
TEST(Fabric, SixFamiliesAndCompletion) {
  const auto ast = test_helpers::parseModule(fabric_source(R"ptx(
  fabric.try_get.async.shared::cta.mbarrier::complete_tx::bytes.mbarrier::report::fabric.relaxed.sys.b128 [data], [%endpoint, %dataoff], 16, [bar];
  fabric.try_put.async.shared::cta.mbarrier::complete_tx::16B.mbarrier::report::fabric.relaxed.sys.b128 [%endpoint, %dataoff], [data], 16, [bar];
  fabric.try_red.async.shared::cta.mbarrier::complete_tx::16B.mbarrier::report::fabric.relaxed.sys.add.u32 [%endpoint, %dataoff], [data], 16, [bar];
  fabric.try_pullred.async.multimem.shared::cta.mbarrier::complete_tx::bytes.mbarrier::report::fabric.relaxed.sys.add.u32.sync [data], [%endpoint, %dataoff], 16, [bar], 0xffffffff;
  fabric.submit;
  fabric.wait.sync_restrict::reads;
)ptx"));
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  const auto module = resolveAndValidateModule(*ast);
  ASSERT_TRUE(module.has_value()) << module.error().front().message;
  const auto& body = module->functions.front().body;
  ASSERT_EQ(body.size(), 7u);
  EXPECT_NE(dynamic_cast<FabricTryGet*>(body[0].get()), nullptr);
  EXPECT_NE(dynamic_cast<FabricTryPutUnicastOrdinary*>(body[1].get()), nullptr);
  EXPECT_NE(dynamic_cast<FabricTryRedUnicastOrdinaryAdd*>(body[2].get()),
            nullptr);
  EXPECT_NE(dynamic_cast<FabricTryPullredAddOrdinary*>(body[3].get()), nullptr);
  EXPECT_NE(dynamic_cast<FabricSubmit*>(body[4].get()), nullptr);
  EXPECT_NE(dynamic_cast<FabricWait*>(body[5].get()), nullptr);
  EXPECT_EQ(FabricTryGet::fabric_contract.endpoint,
            FabricEndpointKind::Unicast);
  EXPECT_EQ(FabricTryPutUnicastOrdinary::completion_kind,
            base::AsyncCompletionKind::MbarrierCompleteTx16B);
  EXPECT_EQ(FabricWait::completion_kind,
            base::AsyncCompletionKind::FabricReadWait);
  EXPECT_EQ(FabricWait::fabric_contract.shared_access,
            FabricSharedAccess::None);
}

/** Multicast put/red and counted reductions preserve topology and arity. */
TEST(Fabric, MulticastForms) {
  for (std::string_view candidate : {
           "fabric.try_put.async.multimem.shared::cta."
           "mbarrier::complete_tx::16B.mbarrier::report::fabric.relaxed.sys."
           "b128 "
           "[%endpoint, %dataoff], [data], 16, [bar];",
           "fabric.try_red.async.multimem.shared::cta."
           "mbarrier::complete_tx::16B.mbarrier::report::fabric.relaxed.sys."
           "min.u32 [%endpoint, %dataoff], [data], 16, [bar];",
           "fabric.try_red.async.multimem.shared::cta."
           "mbarrier::complete_tx::16B.mbarrier::report::fabric.counted::bytes."
           "relaxed.sys.add.u32 [%endpoint, %dataoff, %counteroff], [data], "
           "16, "
           "[bar];",
       }) {
    SCOPED_TRACE(candidate);
    EXPECT_TRUE(accepts_fabric(fabric_source(candidate)));
  }
}

/** Source omission, counted arity, masks, widths and static alignments are exact. */
TEST(Fabric, StaticNegativeMatrix) {
  constexpr std::string_view get_prefix =
      "fabric.try_get.async.shared::cta.mbarrier::complete_tx::bytes."
      "mbarrier::report::fabric.relaxed.sys.b128 ";
  for (std::string_view candidate : {
           "fabric.try_put.async.shared::cta.mbarrier::complete_tx::16B."
           "mbarrier::report::fabric.counted::bytes.cp_mask.relaxed.sys.b128 "
           "[%endpoint, %dataoff, %counteroff], [data], 16, [bar], %bytemask;",
           "fabric.try_put.async.shared::cta.mbarrier::complete_tx::16B."
           "mbarrier::report::fabric.counted::bytes.relaxed.sys.b128 "
           "[%endpoint, %dataoff], [data], 16, [bar];",
           "fabric.try_put.async.shared::cta.mbarrier::complete_tx::16B."
           "mbarrier::report::fabric.counted::bytes.relaxed.sys.b128 "
           "[%endpoint, %dataoff, %narrow_off], [data], 16, [bar];",
           "fabric.try_put.async.shared::cta.mbarrier::complete_tx::16B."
           "mbarrier::report::fabric.relaxed.sys.b128 "
           "[%endpoint, %dataoff, %counteroff], [data], 16, [bar];",
           "fabric.try_get.async.shared::cta.mbarrier::complete_tx::bytes."
           "mbarrier::report::fabric.relaxed.sys.b128 "
           "[data], [%wide_endpoint, %dataoff], 16, [bar];",
           "fabric.try_get.async.shared::cta.mbarrier::complete_tx::bytes."
           "mbarrier::report::fabric.relaxed.sys.b128 "
           "[data], [%endpoint, %narrow_off], 16, [bar];",
           "fabric.try_get.async.shared::cta.mbarrier::complete_tx::bytes."
           "mbarrier::report::fabric.relaxed.sys.b128 "
           "[data], [1, %dataoff], 16, [bar];",
           "fabric.try_get.async.shared::cta.mbarrier::complete_tx::bytes."
           "mbarrier::report::fabric.relaxed.sys.b128 "
           "[data], [%endpoint, 16], 16, [bar];",
           "fabric.try_get.async.shared::cta.mbarrier::complete_tx::bytes."
           "mbarrier::report::fabric.relaxed.sys.b128 "
           "[data+4], [%endpoint, %dataoff], 16, [bar];",
           "fabric.try_get.async.shared::cta.mbarrier::complete_tx::bytes."
           "mbarrier::report::fabric.relaxed.sys.b128 "
           "[data], [%endpoint, %dataoff], 15, [bar];",
           "fabric.try_get.async.shared::cta.mbarrier::complete_tx::bytes."
           "mbarrier::report::fabric.relaxed.sys.b128 "
           "[data], [%endpoint, %dataoff], 16, [bar+4];",
           "fabric.try_get.async.shared::cta.mbarrier::complete_tx::bytes."
           "mbarrier::report::fabric.relaxed.sys.b128 "
           "[global_data], [%endpoint, %dataoff], 16, [bar];",
           "fabric.try_put.async.shared::cta.mbarrier::complete_tx::16B."
           "mbarrier::report::fabric.relaxed.sys.b128 "
           "[%endpoint, %dataoff], [data+4], 16, [bar];",
           "fabric.try_put.async.shared::cta.mbarrier::complete_tx::16B."
           "mbarrier::report::fabric.cp_mask.relaxed.sys.b128 "
           "[%endpoint, %dataoff], [data], 16, [bar], %size;",
           "fabric.try_put.async.shared::cta.mbarrier::complete_tx::16B."
           "mbarrier::report::fabric.relaxed.sys.b128 "
           "[%endpoint, %dataoff], [data], %dataoff, [bar];",
           "fabric.try_pullred.async.multimem.shared::cta."
           "mbarrier::complete_tx::bytes.mbarrier::report::fabric.relaxed.sys."
           "add.u32.sync [data], [%endpoint, %dataoff], 16, [bar], 1;",
           "fabric.wait;",
       }) {
    SCOPED_TRACE(candidate);
    EXPECT_FALSE(accepts_fabric(fabric_source(candidate)));
  }
  EXPECT_TRUE(accepts_fabric(
      fabric_source(std::string(get_prefix) +
                    "[data], [%signed_endpoint, %dataoff], 16, [bar];")));
  EXPECT_TRUE(accepts_fabric(fabric_source(
      std::string(get_prefix) + "[data], [%endpoint, %dataoff], 0, [bar];")));
  EXPECT_TRUE(accepts_fabric(fabric_source(
      "fabric.try_put.async.shared::cta.mbarrier::complete_tx::16B."
      "mbarrier::report::fabric.counted::bytes.relaxed.sys.b128 "
      "[%endpoint, %dataoff, %counteroff], [data], 16, [bar];")));
  EXPECT_TRUE(accepts_fabric(fabric_source(
      "fabric.try_put.async.shared::cta.mbarrier::complete_tx::16B."
      "mbarrier::report::fabric.cp_mask.relaxed.sys.b128 "
      "[%endpoint, %dataoff], [data], 16, [bar], %bytemask;")));
}

/** The three ordered proxy pairs each admit acquire and release only at sys. */
TEST(Fabric, SixProxyFences) {
  for (std::string_view pair :
       {"generic::fabric", "fabric::generic", "fabric::fabric"}) {
    for (std::string_view sem : {"acquire", "release"}) {
      const auto instruction = "fence.proxy." + std::string(pair) + ".alias." +
                               std::string(sem) + ".sys;";
      EXPECT_TRUE(accepts_fabric(fabric_source(instruction))) << instruction;
      EXPECT_FALSE(accepts_fabric(fabric_source(instruction, "sm_90")));
      EXPECT_FALSE(accepts_fabric(fabric_source(instruction, "sm_100", "9.2")));
    }
  }
}

/** Feature-gated FP8 pull reduction and written submit controls stay distinct. */
TEST(Fabric, TargetAndSubmitPresence) {
  constexpr std::string_view fp8 =
      "fabric.try_pullred.async.multimem.shared::cta."
      "mbarrier::complete_tx::bytes.mbarrier::report::fabric.relaxed.sys."
      "min.e4m3.sync [data], [%endpoint, %dataoff], 16, [bar], 0xffffffff;";
  EXPECT_FALSE(accepts_fabric(fabric_source(fp8, "sm_100")));
  EXPECT_TRUE(accepts_fabric(fabric_source(fp8, "sm_100a")));
  EXPECT_TRUE(accepts_fabric(fabric_source(fp8, "sm_120a")));
  EXPECT_FALSE(accepts_fabric(fabric_source("fabric.submit;", "sm_90")));
  EXPECT_FALSE(
      accepts_fabric(fabric_source("fabric.submit;", "sm_100", "9.2")));
  std::optional<ResolvedModule> owned;
  {
    auto ast = test_helpers::parseModule(
        fabric_source("fabric.submit; fabric.submit.op_restrict::fetching;"));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module.has_value()) << module.error().front().message;
    owned.emplace(std::move(*module));
  }
  ASSERT_TRUE(owned);
  ASSERT_TRUE(validateModule(*owned));
  const auto& body = owned->functions.front().body;
  auto* omitted = dynamic_cast<FabricSubmit*>(body[0].get());
  auto* written = dynamic_cast<FabricSubmit*>(body[1].get());
  ASSERT_NE(omitted, nullptr);
  ASSERT_NE(written, nullptr);
  EXPECT_FALSE(omitted->fetching.value);
  EXPECT_TRUE(omitted->fetching.locs.empty());
  EXPECT_TRUE(written->fetching.value);
  EXPECT_EQ(written->fetching.locs.size(), 1u);
  omitted->fetching.value = true;
  EXPECT_FALSE(validateModule(*owned));
  omitted->fetching.value = false;
  ASSERT_TRUE(validateModule(*owned));
  written->fetching.locs.clear();
  EXPECT_FALSE(validateModule(*owned));
}

/** An owned CFT handle survives AST death and rejects mutable shape corruption. */
TEST(Fabric, OwnedHandleValidation) {
  std::optional<ResolvedModule> owned;
  {
    auto ast = test_helpers::parseModule(fabric_source(
        "fabric.try_get.async.shared::cta.mbarrier::complete_tx::bytes."
        "mbarrier::report::fabric.relaxed.sys.b128 "
        "[data], [%endpoint, %dataoff], 16, [bar];"));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module.has_value()) << module.error().front().message;
    owned.emplace(std::move(*module));
  }
  ASSERT_TRUE(owned);
  ASSERT_TRUE(validateModule(*owned));
  auto* form =
      dynamic_cast<FabricTryGet*>(owned->functions.front().body.front().get());
  ASSERT_NE(form, nullptr);
  const auto original = form->src;
  form->src.value.comma_ranges.clear();
  EXPECT_FALSE(validateModule(*owned));
  form->src = original;
  ASSERT_TRUE(validateModule(*owned));
  form->src.value.data_offset.value.declared_type = base::ScalarType::B32;
  EXPECT_FALSE(validateModule(*owned));
  form->src = original;
  ASSERT_TRUE(validateModule(*owned));
  form->src.value.endpoint.locs.clear();
  EXPECT_FALSE(validateModule(*owned));
}

/** All counted components retain indexed bindings and reject mutable tampering. */
TEST(Fabric, CountedIndexedHandleIntegrity) {
  std::optional<ResolvedModule> owned;
  {
    auto ast = test_helpers::parseModule(fabric_source(R"ptx(
  .reg .b32 %end<2>;
  .reg .b64 %off<2>;
  fabric.try_put.async.shared::cta.mbarrier::complete_tx::16B.mbarrier::report::fabric.counted::bytes.relaxed.sys.b128 [%end0, %off0, %off1], [data], 16, [bar];
)ptx"));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module.has_value()) << module.error().front().message;
    owned.emplace(std::move(*module));
  }
  ASSERT_TRUE(owned);
  ASSERT_TRUE(validateModule(*owned));
  auto* form = dynamic_cast<FabricTryPutUnicastCounted*>(
      owned->functions.front().body.front().get());
  ASSERT_NE(form, nullptr);
  const auto original = form->dst;
  ASSERT_TRUE(form->dst.value.counter_offset);
  EXPECT_EQ(form->dst.value.endpoint.value.parameterized_index, 0u);
  EXPECT_EQ(form->dst.value.counter_offset->value.parameterized_index, 1u);
  form->dst.value.counter_offset->value.parameterized_index = 5;
  EXPECT_FALSE(validateModule(*owned));
  form->dst = original;
  form->dst.value.counter_offset->value.symbol_id.reset();
  EXPECT_FALSE(validateModule(*owned));
  form->dst = original;
  form->dst.value.counter_offset.reset();
  EXPECT_FALSE(validateModule(*owned));
  form->dst = original;
  form->dst.value.counter_offset->locs.front().end =
      form->dst.value.counter_offset->locs.front().start;
  EXPECT_FALSE(validateModule(*owned));
  form->dst = original;
  form->dst.value.comma_ranges.front().start =
      form->dst.value.right_bracket_range.start;
  EXPECT_FALSE(validateModule(*owned));
}

/** Both documented counted spellings own the same contract and true source ranges. */
TEST(Fabric, CountedSourceOrders) {
  std::optional<ResolvedModule> owned;
  {
    auto ast = test_helpers::parseModule(fabric_source(R"ptx(
  fabric.try_put.async.shared::cta.mbarrier::complete_tx::16B.mbarrier::report::fabric.counted::bytes.relaxed.sys.b128 [%endpoint, %dataoff, %counteroff], [data], 16, [bar];
  fabric.try_put.async.counted::bytes.shared::cta.mbarrier::complete_tx::16B.mbarrier::report::fabric.relaxed.sys.b128 [%endpoint, %dataoff, %counteroff], [data], 16, [bar];
)ptx"));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module.has_value()) << module.error().front().message;
    owned.emplace(std::move(*module));
  }
  ASSERT_TRUE(owned);
  ASSERT_TRUE(validateModule(*owned));
  const auto& body = owned->functions.front().body;
  auto* canonical = dynamic_cast<FabricTryPutUnicastCounted*>(body[0].get());
  auto* early = dynamic_cast<FabricTryPutUnicastCountedEarly*>(body[1].get());
  ASSERT_NE(canonical, nullptr);
  ASSERT_NE(early, nullptr);
  EXPECT_EQ(canonical->fabric_contract.operation,
            early->fabric_contract.operation);
  EXPECT_EQ(canonical->fabric_contract.completion,
            early->fabric_contract.completion);
  EXPECT_EQ(canonical->fabric_contract.counted, early->fabric_contract.counted);
  ASSERT_TRUE(canonical->dst.value.counter_offset);
  ASSERT_TRUE(early->dst.value.counter_offset);
  EXPECT_EQ(canonical->dst.value.comma_ranges.size(), 2u);
  EXPECT_EQ(early->dst.value.comma_ranges.size(), 2u);
  EXPECT_LT(canonical->dst.locs.front().start.line,
            early->dst.locs.front().start.line);
  EXPECT_LT(early->dst.value.left_bracket_range.start.column,
            early->dst.value.endpoint.locs.front().start.column);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
