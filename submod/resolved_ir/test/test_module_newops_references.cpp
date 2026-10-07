#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve a complete module while syntax remains local to this call. */
std::optional<ResolvedModule> owned_module(std::string_view source) {
  const auto parsed = test_helpers::parseModule(source);
  if (!parsed)
    return std::nullopt;
  auto resolved = resolveModuleOnly(*parsed);
  if (!resolved)
    return std::nullopt;
  return std::move(*resolved);
}

/** Detect a module-owned identity or cached-type mismatch in diagnostics. */
bool has_module_mismatch(const checker::CheckResult& result) {
  if (result)
    return false;
  for (const auto& diagnostic : result.error())
    if (diagnostic.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch)
      return true;
  return false;
}

/** TMA group spellings do not participate in TCGEN function uniformity. */
TEST(ModuleNewOpsReferences, MixedTmaGroupsRemainFunctionLocalLegal) {
  auto owned = owned_module(R"ptx(
.version 9.3
.target sm_110a
.address_size 64
.global .align 64 .b8 tensor_map[128];
.shared .align 16 .b8 dst[1024];
.shared .align 8 .b64 mbar;
.entry kernel() {
  .reg .s32 %r;
  cp.async.bulk.tensor.1d.shared::cta.global.tile.mbarrier::complete_tx::bytes
    [dst], [tensor_map, {%r}], [mbar];
  cp.async.bulk.tensor.1d.shared::cta.global.tile.mbarrier::complete_tx::bytes.cta_group::1
    [dst], [tensor_map, {%r}], [mbar];
  cp.async.bulk.tensor.1d.shared::cta.global.tile.mbarrier::complete_tx::bytes.cta_group::2
    [dst], [tensor_map, {%r}], [mbar];
  tcgen05.commit.cta_group::1.mbarrier::arrive::one.b64 [mbar];
}
)ptx");
  ASSERT_TRUE(owned);
  EXPECT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext));

  auto conflicting_tcgen = owned_module(R"ptx(
.version 9.3
.target sm_110a
.address_size 64
.shared .align 8 .b64 mbar;
.entry kernel() {
  tcgen05.commit.cta_group::1.mbarrier::arrive::one.b64 [mbar];
  tcgen05.commit.cta_group::2.mbarrier::arrive::one.b64 [mbar];
}
)ptx");
  ASSERT_TRUE(conflicting_tcgen);
  const auto result = validateModule(
      *conflicting_tcgen, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(result);
  bool group_conflict = false;
  for (const auto& diagnostic : result.error())
    group_conflict |=
        diagnostic.kind == checker::CheckDiagnosticKind::RuleViolation &&
        diagnostic.message.find("CTA group conflicts") != std::string::npos;
  EXPECT_TRUE(group_conflict);
}

/** TCGEN address bindings and cached types remain checked without syntax. */
TEST(ModuleNewOpsReferences, TcgenAddressIdentityAndTypeAreOwned) {
  auto owned = owned_module(R"ptx(
.version 9.3
.target sm_110a
.address_size 64
.entry kernel() {
  .reg .b32 %r, %t;
  tcgen05.ld.sync.aligned.32x32b.x1.b32 {%r}, [%t];
}
)ptx");
  ASSERT_TRUE(owned);
  ASSERT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext));
  auto& load = dynamic_cast<Tcgen05Ld&>(*owned->functions.front().body.front());
  auto& address = std::get<ResolvedRegisterRef>(load.taddr.value.value);
  const auto identity = address.symbol_id;
  address.symbol_id.reset();
  EXPECT_TRUE(has_module_mismatch(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)));
  address.symbol_id = identity;
  address.declared_type = ScalarType::B64;
  EXPECT_TRUE(has_module_mismatch(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)));
}

/** Im2col pack references are checked after the source AST is released. */
TEST(ModuleNewOpsReferences, Im2colInfoRegisterIdentityIsOwned) {
  auto owned = owned_module(R"ptx(
.version 9.3
.target sm_110a
.address_size 64
.global .align 64 .b8 tensor_map[128];
.entry kernel() {
  .reg .u16 %h;
  cp.async.bulk.prefetch.tensor.3d.L2.global.im2col
    [tensor_map, {0, 0, 0}], {%h};
}
)ptx");
  ASSERT_TRUE(owned);
  ASSERT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext));
  auto& copy = dynamic_cast<CpAsyncBulkPrefetchTensor3dIm2col&>(
      *owned->functions.front().body.front());
  ASSERT_TRUE(copy.im2col_info);
  auto& register_ref =
      std::get<ResolvedRegisterRef>(copy.im2col_info->value.elements.front());
  register_ref.symbol_id.reset();
  EXPECT_TRUE(has_module_mismatch(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)));
}

/** Shared matrix descriptors retain declaration identity and cached type. */
TEST(ModuleNewOpsReferences, WgmmaDescriptorBindingIsOwned) {
  auto owned = owned_module(R"ptx(
.version 9.3
.target sm_90a
.address_size 64
.entry kernel() {
  .reg .b32 %d<2>;
  .reg .b64 %adesc, %bdesc;
  wgmma.mma_async.sync.aligned.m64n8k16.f16.f16.f16
    {%d0,%d1}, %adesc, %bdesc, 1, -1, 1, 0, 1;
}
)ptx");
  ASSERT_TRUE(owned);
  ASSERT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext));
  auto& mma = dynamic_cast<WgmmaMmaAsyncDenseM64n8k16F16F16F16SharedPlain&>(
      *owned->functions.front().body.front());
  mma.a_desc.value.register_ref.symbol_id.reset();
  EXPECT_TRUE(has_module_mismatch(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
