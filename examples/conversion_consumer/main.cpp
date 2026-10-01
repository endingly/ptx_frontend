#include <array>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/base/base.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace {

/** PTX source exercising public conversion, ownership, and validation APIs. */
constexpr std::string_view kFixture = R"ptx(
.version 9.3
.target sm_121a
.address_size 64
.shared .align 8 .u64 cvta_shared_value;
.global .align 4 .b32 atomic_value;
.global .align 8 .b64 atomic_value_64;
.global .align 16 .b8 atomic_vector_value[16];
.visible .entry conversion_consumer(
    .param .u64 .ptr .const .align 8 constant_pointer) {
  .reg .pred %p<2>;
  .reg .u32 %r<2>;
  .reg .u32 %u0;
  .reg .u64 %rd0;
  .reg .u64 %mbar_addr;
  .reg .s32 %s<3>;
  .reg .b32 %b<5>;
  .reg .u64 %uq<2>;
  .reg .s64 %sq<2>;
  .reg .b64 %bq<3>;
  .reg .f32 %f<4>;
  .reg .f64 %fd<2>;
  .reg .b16 %h<2>;
  .reg .b64 %policy;

  isspacep.shared::cluster %p0, %r0;
  cvta.shared::cluster.u64 %rd0, cvta_shared_value+8;
  cvta.to.const.u64 %rd0, %rd0;
  prmt.b32.rc16 %b0, %b1, %b2, %b3;
  prmt.b32.rc16 %b4, %b1, %b2, 0x1;
  cvt.pack.sat.u2.s32.b32 %u0, %s0, %s1, 0x12345678;
  cvt.rmi.s32.f32 %s2, %f0;
  cvt.rn.satfinite.scaled::n2::ue8m0.s2f6x2.f32 %h0, %f2, %f3, %h1;
  testp.normal.f32 %p0, %f1;
  copysign.f32 %f0, %f1, %f2;
  sin.approx.ftz.f32 %f0, %f1;
  ex2.approx.ftz.bf16 %h0, %h1;
  min.ftz.NaN.xorsign.abs.f32 %f0, %f1, %f2;
  min.ftz.NaN.abs.f32 %f0, %f1, %f2, %f3;
  max.xorsign.abs.f32 %f0, %f1, %f2;
  max.abs.f32 %f0, %f1, %f2, %f3;
  add.f32.f16 %f0, %h1, %f1;
  add.f32.f16 %f0, %h1, 1.0;
  sub.f32.bf16 %f0, %h1, %f1;
  sub.f32.bf16 %f0, %h1, 2.0;
  set.nan.xor.f32.f32 %f0, %f1, %f2, !0;
  selp.s32 %s0, %s1, -1, !%p0;
  selp.u32 %u0, %u0, 0, 2;
  set.num.xor.ftz.f16x2.f16x2 %b3, %b1, %b2, !0;
  set.eq.bf16.f16 %h0, %h1, %h1;
  slct.u32.s32 %u0, %u0, 0, -1;
  slct.ftz.u64.f32 %rd0, %rd0, %rd0, -0.0;
  atom.relaxed.cta.global.add.u32 %u0, [atomic_value], %u0;
  red.relaxed.cta.global.add.u32 [atomic_value], 1;
  atom.global.cas.b32 %b0, [atomic_value], 1, %b1;
  atom.relaxed.cta.global.cas.b32 %b0, [atomic_value], %b1, 2;
  atom.global.inc.u32 %u0, [atomic_value], %u0;
  atom.relaxed.cta.global.exch.b32 %b0, [atomic_value], 1;
  red.global.xor.b32 [atomic_value], %b1;
  atom.global.add.u64 %uq0, [atomic_value_64], %uq1;
  atom.global.relaxed.cta.min.s64 %sq0, [atomic_value_64], 1;
  atom.relaxed.cta.global.cas.b64 %bq0, [atomic_value_64], %bq1, 2;
  red.global.relaxed.cta.xor.b64 [atomic_value_64], %bq1;
  atom.global.add.f32 %f0, [atomic_value], %b0;
  red.global.relaxed.cta.add.f32 [atomic_value], 0f3f800000;
  atom.relaxed.cta.global.add.f64 %fd0, [atomic_value_64], %bq1;
  red.global.add.f64 [atomic_value_64], 1.0;
  atom.global.v2.f16.add.noftz.L2::cache_hint {_, %h1},
      [atomic_vector_value], {%h0, %h1}, %policy;
  red.global.v2.f16.add.noftz.L2::cache_hint
      [atomic_vector_value], {%h0, %h1}, %policy;
  red.async.relaxed.cluster.shared::cluster.mbarrier::complete_tx::bytes.add.u32
      [%rd0], %r0, [%mbar_addr];
  red.async.mmio.release.sys.global.add.u64 [%rd0], %uq0;
  ret;
}
)ptx";

namespace ir = ptx_frontend::resolved_ir;

/** Borrow an exact opcode record from a const owner. */
template <ir::PtxOperator T>
const T* outer_get_if(const ir::OwnedInstruction* instruction) {
  return instruction ? instruction->get_if<T>() : nullptr;
}

/** Borrow an exact opcode record from a mutable owner. */
template <ir::PtxOperator T>
T* outer_get_if(ir::OwnedInstruction* instruction) {
  return instruction ? instruction->get_if<T>() : nullptr;
}

/** Require an exact opcode record and report mismatches as variant access errors. */
template <ir::PtxOperator T>
const T& outer_get(const ir::OwnedInstruction& instruction) {
  if (const auto* value = outer_get_if<T>(&instruction))
    return *value;
  throw std::bad_variant_access();
}

/** Report a failed public-contract check in both Debug and Release builds. */
bool require(bool condition, std::string_view description) {
  if (!condition)
    std::cerr << "consumer contract failed: " << description << '\n';
  return condition;
}

/** Check that owned validation rejects a deliberate public-IR mutation. */
bool rejectsMutation(const ir::ResolvedModule& module,
                     ir::checker::CheckDiagnosticKind expected,
                     std::string_view description) {
  const auto result = ir::validateModule(
      module, ir::ModuleValidationPolicy::RequireCompleteContext);
  const bool matched = !result && !result.error().empty() &&
                       result.error().front().kind == expected;
  if (!matched && !result && !result.error().empty())
    std::cerr << "observed validation diagnostic: "
              << result.error().front().message << '\n';
  return require(matched, description);
}

/** Exercise the installed CTA barrier model after parser and AST destruction. */
bool checkBarrierSyncContract() {
  constexpr std::string_view source = R"ptx(
.version 7.8
.target sm_80
.address_size 64
.entry k() {
  .reg .u32 %r;
  .reg .pred %p<2>;
  barrier.sync.aligned 0;
  barrier.cta.sync 1, 32;
  barrier.arrive 2, 32;
  barrier.cta.arrive.aligned 3, 64;
  barrier.red.popc.aligned.u32 %r, 4, 32, !%p0;
  barrier.cta.red.and.pred %p0, 5, %p1;
  ret;
}
)ptx";
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!require(ast.has_value(), "CTA barrier fixture parses"))
      return false;
    auto resolved = ir::resolveModuleOnly(*ast);
    if (!require(resolved.has_value(), "CTA barrier fixture resolves"))
      return false;
    owned.emplace(std::move(*resolved));
  }

  if (!require(ir::validateModule(
                   *owned, ir::ModuleValidationPolicy::RequireCompleteContext)
                   .has_value(),
               "owned CTA barrier module validates"))
    return false;
  const auto& body = owned->functions.front().body;
  if (!require(body.size() == 7, "CTA barrier instructions retained"))
    return false;
  const auto* ordinary = outer_get_if<ir::Barrier>(&body[0]);
  const auto* qualified = outer_get_if<ir::Barrier>(&body[1]);
  const auto* sync =
      ordinary ? std::get_if<ir::Barrier::Sync>(&ordinary->variant) : nullptr;
  const auto* cta_sync =
      qualified ? std::get_if<ir::Barrier::CtaSync>(&qualified->variant)
                : nullptr;
  const auto* ordinary_arrive = outer_get_if<ir::Barrier>(&body[2]);
  const auto* qualified_arrive = outer_get_if<ir::Barrier>(&body[3]);
  const auto* arrive =
      ordinary_arrive
          ? std::get_if<ir::Barrier::Arrive>(&ordinary_arrive->variant)
          : nullptr;
  const auto* cta_arrive =
      qualified_arrive
          ? std::get_if<ir::Barrier::CtaArrive>(&qualified_arrive->variant)
          : nullptr;
  const auto* popc_instruction = outer_get_if<ir::Barrier>(&body[4]);
  const auto* popc =
      popc_instruction
          ? std::get_if<ir::Barrier::RedPopcU32>(&popc_instruction->variant)
          : nullptr;
  const auto* and_instruction = outer_get_if<ir::Barrier>(&body[5]);
  const auto* and_reduction =
      and_instruction
          ? std::get_if<ir::Barrier::CtaRedAndPred>(&and_instruction->variant)
          : nullptr;
  const auto* popc_operands =
      popc ? std::get_if<ir::Barrier::RedPopcU32::WithThreadCountOperands>(
                 &popc->operands)
           : nullptr;
  return require(
      sync && cta_sync && sync->aligned.value && !sync->aligned.locs.empty() &&
          !cta_sync->aligned.value && cta_sync->aligned.locs.empty() &&
          arrive && cta_arrive && !arrive->aligned.value &&
          arrive->aligned.locs.empty() && cta_arrive->aligned.value &&
          !cta_arrive->aligned.locs.empty() && popc && popc->aligned.value &&
          !popc->aligned.locs.empty() && popc_operands &&
          popc_operands->predicate.value.negated && and_reduction &&
          !and_reduction->aligned.value && and_reduction->aligned.locs.empty(),
      "public CTA barrier variant and aligned metadata");
}

/** Verify all memory-barrier levels survive loss of the source AST. */
bool checkMembarLevelsContract() {
  constexpr std::string_view source = R"ptx(
.version 2.0
.target sm_20
.entry k() { membar.cta; membar.gl; membar.sys; ret; }
)ptx";
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!require(ast.has_value(), "membar levels fixture parses"))
      return false;
    auto resolved = ir::resolveModuleOnly(*ast);
    if (!require(resolved.has_value(), "membar levels fixture resolves"))
      return false;
    owned.emplace(std::move(*resolved));
  }
  if (!require(ir::validateModule(
                   *owned, ir::ModuleValidationPolicy::RequireCompleteContext)
                   .has_value(),
               "owned membar levels module validates"))
    return false;
  const auto& body = owned->functions.front().body;
  if (!require(body.size() == 4, "all membar levels retained"))
    return false;
  const auto* cta = outer_get_if<ir::Membar>(&body[0]);
  const auto* gl = outer_get_if<ir::Membar>(&body[1]);
  const auto* sys = outer_get_if<ir::Membar>(&body[2]);
  return require(
      cta && gl && sys &&
          std::holds_alternative<ir::Membar::Cta>(cta->variant) &&
          std::holds_alternative<ir::Membar::Gl>(gl->variant) &&
          std::holds_alternative<ir::Membar::Sys>(sys->variant) &&
          std::get<ir::Membar::Gl>(gl->variant).scope == ir::MemoryScope::Gpu,
      "public membar levels and typed GPU scope");
}

/** Check the installed alias-proxy barrier after the syntax AST is released. */
bool checkMembarProxyAliasContract() {
  constexpr std::string_view source = R"ptx(
.version 7.5
.target sm_70
.entry k() { membar.proxy.alias; ret; }
)ptx";
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!require(ast.has_value(), "membar proxy-alias fixture parses"))
      return false;
    auto resolved = ir::resolveModuleOnly(*ast);
    if (!require(resolved.has_value(), "membar proxy-alias fixture resolves"))
      return false;
    owned.emplace(std::move(*resolved));
  }
  if (!require(ir::validateModule(
                   *owned, ir::ModuleValidationPolicy::RequireCompleteContext)
                   .has_value(),
               "owned membar proxy-alias module validates"))
    return false;
  const auto& body = owned->functions.front().body;
  if (!require(body.size() == 2, "membar proxy-alias retained"))
    return false;
  const auto* instruction = outer_get_if<ir::Membar>(&body.front());
  return require(instruction && std::holds_alternative<ir::Membar::ProxyAlias>(
                                    instruction->variant),
                 "public membar proxy-alias variant");
}

/** Check the installed alias proxy fence after the syntax AST is released. */
bool checkFenceProxyAliasContract() {
  constexpr std::string_view source = R"ptx(
.version 7.5
.target sm_70
.entry k() { fence.proxy.alias; ret; }
)ptx";
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!require(ast.has_value(), "fence proxy-alias fixture parses"))
      return false;
    auto resolved = ir::resolveModuleOnly(*ast);
    if (!require(resolved.has_value(), "fence proxy-alias fixture resolves"))
      return false;
    owned.emplace(std::move(*resolved));
  }
  if (!require(ir::validateModule(
                   *owned, ir::ModuleValidationPolicy::RequireCompleteContext)
                   .has_value(),
               "owned fence proxy-alias module validates"))
    return false;
  const auto& body = owned->functions.front().body;
  if (!require(body.size() == 2, "fence proxy-alias retained"))
    return false;
  const auto* instruction = outer_get_if<ir::Fence>(&body.front());
  const auto* alias =
      instruction ? std::get_if<ir::Fence::ProxyAlias>(&instruction->variant)
                  : nullptr;
  return require(alias && alias->proxy && alias->alias,
                 "public fence proxy-alias variant and controls");
}

/** Check installed async-proxy source spaces in an owned resolved module. */
bool checkMembarProxyAsyncContract() {
  constexpr std::string_view source = R"ptx(
.version 8.0
.target sm_90a
.entry k() {
  membar.proxy.async;
  membar.proxy.async.global;
  membar.proxy.async.shared::cta;
  membar.proxy.async.shared::cluster;
  ret;
}
)ptx";
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!require(ast.has_value(), "membar async-proxy fixture parses"))
      return false;
    auto resolved = ir::resolveModuleOnly(*ast);
    if (!require(resolved.has_value(), "membar async-proxy fixture resolves"))
      return false;
    owned.emplace(std::move(*resolved));
  }
  if (!require(ir::validateModule(
                   *owned, ir::ModuleValidationPolicy::RequireCompleteContext)
                   .has_value(),
               "owned membar async-proxy module validates"))
    return false;
  const auto& body = owned->functions.front().body;
  if (!require(body.size() == 5, "membar async-proxy forms retained"))
    return false;
  constexpr std::array expected{ir::AsyncProxyKind::Async,
                                ir::AsyncProxyKind::AsyncGlobal,
                                ir::AsyncProxyKind::AsyncSharedCta};
  for (size_t i = 0; i < expected.size(); ++i) {
    const auto* instruction = outer_get_if<ir::Membar>(&body[i]);
    const auto* async =
        instruction ? std::get_if<ir::Membar::ProxyAsync>(&instruction->variant)
                    : nullptr;
    if (!require(async && async->proxy_kind.value == expected[i] &&
                     !async->proxy_kind.locs.empty(),
                 "public membar async-proxy space"))
      return false;
  }
  const auto* instruction = outer_get_if<ir::Membar>(&body[3]);
  const auto* cluster = instruction
                            ? std::get_if<ir::Membar::ProxyAsyncSharedCluster>(
                                  &instruction->variant)
                            : nullptr;
  return require(
      cluster &&
          cluster->proxy_kind.value == ir::AsyncProxyKind::AsyncSharedCluster &&
          !cluster->proxy_kind.locs.empty(),
      "public membar async-proxy cluster space");
}

/** Check installed ordinary-fence variants after the syntax AST is released. */
bool checkOrdinaryFenceContract() {
  constexpr std::string_view source = R"ptx(
.version 8.6
.target sm_90a
.entry k() {
  fence.cta;
  fence.sc.cta;
  fence.cta.sc;
  fence.acq_rel.cta;
  fence.cta.acq_rel;
  fence.acq_rel.gpu;
  fence.cluster.acq_rel;
  ret;
}
)ptx";
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!require(ast.has_value(), "ordinary fence fixture parses"))
      return false;
    auto resolved = ir::resolveModuleOnly(*ast);
    if (!require(resolved.has_value(), "ordinary fence fixture resolves"))
      return false;
    owned.emplace(std::move(*resolved));
  }
  if (!require(ir::validateModule(
                   *owned, ir::ModuleValidationPolicy::RequireCompleteContext)
                   .has_value(),
               "owned ordinary fence module validates"))
    return false;
  const auto& body = owned->functions.front().body;
  if (!require(body.size() == 8, "ordinary fence forms retained"))
    return false;
  const auto* omitted_instruction = outer_get_if<ir::Fence>(&body[0]);
  const auto* omitted =
      omitted_instruction
          ? std::get_if<ir::Fence::OrdinaryCta>(&omitted_instruction->variant)
          : nullptr;
  if (!require(omitted &&
                   omitted->semantics.value == ir::MemoryConsistency::Omitted &&
                   omitted->semantics.locs.empty(),
               "public omitted ordinary fence semantics"))
    return false;
  for (size_t i : {1U, 2U}) {
    const auto* instruction = outer_get_if<ir::Fence>(&body[i]);
    const auto* sc =
        instruction ? std::get_if<ir::Fence::OrdinaryCta>(&instruction->variant)
                    : nullptr;
    if (!require(sc && sc->semantics.value == ir::MemoryConsistency::Sc &&
                     !sc->semantics.locs.empty(),
                 "public SC ordinary fence orders"))
      return false;
  }
  for (size_t i : {3U, 4U}) {
    const auto* instruction = outer_get_if<ir::Fence>(&body[i]);
    if (!require(instruction && std::holds_alternative<ir::Fence::AcqRelCta>(
                                    instruction->variant),
                 "public legacy acquire-release CTA variant"))
      return false;
  }
  const auto* gpu_instruction = outer_get_if<ir::Fence>(&body[5]);
  const auto* gpu =
      gpu_instruction
          ? std::get_if<ir::Fence::OrdinaryGpuSys>(&gpu_instruction->variant)
          : nullptr;
  const auto* cluster_instruction = outer_get_if<ir::Fence>(&body[6]);
  const auto* cluster = cluster_instruction
                            ? std::get_if<ir::Fence::OrdinaryCluster>(
                                  &cluster_instruction->variant)
                            : nullptr;
  return require(gpu && cluster &&
                     gpu->semantics.value == ir::MemoryConsistency::AcqRel &&
                     gpu->scope.value == ir::MemoryScope::Gpu &&
                     cluster->semantics.value == ir::MemoryConsistency::AcqRel,
                 "public ordinary fence scope and semantics");
}

/** Check the installed restricted mbarrier-init fence after AST release. */
bool checkMbarrierInitFenceContract() {
  constexpr std::string_view source = R"ptx(
.version 8.0
.target sm_90a
.entry k() { fence.mbarrier_init.release.cluster; ret; }
)ptx";
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!require(ast.has_value(), "mbarrier-init fence fixture parses"))
      return false;
    auto resolved = ir::resolveModuleOnly(*ast);
    if (!require(resolved.has_value(), "mbarrier-init fence fixture resolves"))
      return false;
    owned.emplace(std::move(*resolved));
  }
  if (!require(ir::validateModule(
                   *owned, ir::ModuleValidationPolicy::RequireCompleteContext)
                   .has_value(),
               "owned mbarrier-init fence module validates"))
    return false;
  const auto& body = owned->functions.front().body;
  if (!require(body.size() == 2, "mbarrier-init fence retained"))
    return false;
  const auto* instruction = outer_get_if<ir::Fence>(&body.front());
  const auto* restricted =
      instruction ? std::get_if<ir::Fence::MbarrierInitReleaseCluster>(
                        &instruction->variant)
                  : nullptr;
  return require(restricted && restricted->op_restrict &&
                     restricted->semantics == ir::MemoryConsistency::Release &&
                     restricted->scope == ir::MemoryScope::Cluster,
                 "public mbarrier-init fence variant and controls");
}

/** Check fixed shared-memory fence restrictions through the installed IR. */
bool checkSharedSyncRestrictedFenceContract() {
  constexpr std::string_view source = R"ptx(
.version 8.6
.target sm_90a
.entry k() {
  fence.acquire.sync_restrict::shared::cluster.cluster;
  fence.release.sync_restrict::shared::cta.cluster;
}
)ptx";
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!require(ast.has_value(), "shared restricted fence fixture parses"))
      return false;
    auto resolved = ir::resolveModuleOnly(*ast);
    if (!require(resolved.has_value(),
                 "shared restricted fence fixture resolves"))
      return false;
    owned.emplace(std::move(*resolved));
  }
  if (!require(ir::validateModule(
                   *owned, ir::ModuleValidationPolicy::RequireCompleteContext)
                   .has_value(),
               "owned shared restricted fence module validates"))
    return false;
  const auto& body = owned->functions.front().body;
  if (!require(body.size() == 2, "shared restricted fences retained"))
    return false;
  const auto* acquire_instruction = outer_get_if<ir::Fence>(&body[0]);
  const auto* release_instruction = outer_get_if<ir::Fence>(&body[1]);
  const auto* acquire =
      acquire_instruction
          ? std::get_if<ir::Fence::AcquireSyncRestrictSharedCluster>(
                &acquire_instruction->variant)
          : nullptr;
  const auto* release =
      release_instruction
          ? std::get_if<ir::Fence::ReleaseSyncRestrictSharedCta>(
                &release_instruction->variant)
          : nullptr;
  return require(acquire && release &&
                     acquire->semantics == ir::MemoryConsistency::Acquire &&
                     acquire->sync_restrict_shared_cluster &&
                     acquire->scope == ir::MemoryScope::Cluster &&
                     release->semantics == ir::MemoryConsistency::Release &&
                     release->sync_restrict_shared_cta &&
                     release->scope == ir::MemoryScope::Cluster,
                 "public shared restricted fence variants and controls");
}

/** Exercise installed paired mbarrier wait qualifiers through the public IR. */
bool checkMbarrierTestWaitContract() {
  constexpr std::string_view source = R"ptx(
.version 9.3
.target sm_90a
.address_size 64
.shared .align 8 .b64 shared_value;
.entry k() {
  .reg .pred %p0;
  .reg .b64 %state;
  .reg .u64 %rd0;
  mbarrier.test_wait.acquire.cta.b64 %p0, [%rd0], %state;
  mbarrier.test_wait.parity.phase_type::conditional.relaxed.cluster.shared::cta.b64
      %p0, [shared_value], 1;
  ret;
}
)ptx";
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!require(ast.has_value(), "paired mbarrier wait fixture parses"))
      return false;
    auto resolved = ir::resolveModuleOnly(*ast);
    if (!require(resolved.has_value(), "paired mbarrier wait fixture resolves"))
      return false;
    owned.emplace(std::move(*resolved));
  }
  if (!require(ir::validateModule(
                   *owned, ir::ModuleValidationPolicy::RequireCompleteContext)
                   .has_value(),
               "owned paired mbarrier wait module validates"))
    return false;
  const auto& body = owned->functions.front().body;
  if (!require(body.size() == 3, "paired mbarrier wait forms retained"))
    return false;
  const auto* first_instruction = outer_get_if<ir::Mbarrier>(&body[0]);
  const auto* second_instruction = outer_get_if<ir::Mbarrier>(&body[1]);
  const auto* first =
      first_instruction
          ? std::get_if<ir::Mbarrier::TestWaitTokenSemanticsGenericOrShared>(
                &first_instruction->variant)
          : nullptr;
  const auto* second =
      second_instruction
          ? std::get_if<
                ir::Mbarrier::TestWaitParityConditionalSemanticsSharedCta>(
                &second_instruction->variant)
          : nullptr;
  return require(
      first && second &&
          first->semantics.value == ir::MemoryConsistency::Acquire &&
          first->scope.value == ir::MemoryScope::Cta &&
          !first->semantics.locs.empty() && !first->scope.locs.empty() &&
          second->semantics.value == ir::MemoryConsistency::Relaxed &&
          second->scope.value == ir::MemoryScope::Cluster &&
          !second->semantics.locs.empty() && !second->scope.locs.empty(),
      "public paired mbarrier wait qualifiers and locations");
}

/** Exercise installed paired try-wait qualifiers and hint ownership. */
bool checkMbarrierTryWaitContract() {
  constexpr std::string_view source = R"ptx(
.version 9.3
.target sm_90a
.address_size 64
.shared .align 8 .b64 shared_value;
.entry k() {
  .reg .pred %p0;
  .reg .b64 %state;
  .reg .u64 %rd0;
  mbarrier.try_wait.acquire.cta.b64 %p0, [%rd0], %state, 16;
  mbarrier.try_wait.parity.phase_type::conditional.relaxed.cluster.shared::cta.b64
      %p0, [shared_value], 1;
  ret;
}
)ptx";
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!require(ast.has_value(), "paired mbarrier try-wait fixture parses"))
      return false;
    auto resolved = ir::resolveModuleOnly(*ast);
    if (!require(resolved.has_value(),
                 "paired mbarrier try-wait fixture resolves"))
      return false;
    owned.emplace(std::move(*resolved));
  }
  if (!require(ir::validateModule(
                   *owned, ir::ModuleValidationPolicy::RequireCompleteContext)
                   .has_value(),
               "owned paired mbarrier try-wait module validates"))
    return false;
  const auto& body = owned->functions.front().body;
  if (!require(body.size() == 3, "paired mbarrier try-wait forms retained"))
    return false;
  const auto* first_instruction = outer_get_if<ir::Mbarrier>(&body[0]);
  const auto* second_instruction = outer_get_if<ir::Mbarrier>(&body[1]);
  const auto* first =
      first_instruction
          ? std::get_if<ir::Mbarrier::TryWaitTokenSemanticsGenericOrShared>(
                &first_instruction->variant)
          : nullptr;
  const auto* second =
      second_instruction
          ? std::get_if<
                ir::Mbarrier::TryWaitParityConditionalSemanticsSharedCta>(
                &second_instruction->variant)
          : nullptr;
  const auto* hint =
      first ? std::get_if<ir::Mbarrier::TryWaitTokenSemanticsGenericOrShared::
                              WithHintOperands>(&first->operands)
            : nullptr;
  return require(
      first && second && hint &&
          first->semantics.value == ir::MemoryConsistency::Acquire &&
          first->scope.value == ir::MemoryScope::Cta &&
          !first->semantics.locs.empty() && !first->scope.locs.empty() &&
          std::get<ir::ResolvedImmediate>(hint->time_hint.value).bits == 16U &&
          second->semantics.value == ir::MemoryConsistency::Relaxed &&
          second->scope.value == ir::MemoryScope::Cluster &&
          !second->semantics.locs.empty() && !second->scope.locs.empty(),
      "public paired mbarrier try-wait qualifiers and hint");
}

/** Inspect the typed instruction contract after all syntax owners are gone. */
bool checkExtendedContract(ir::ResolvedModule& module) {
  if (!require(module.functions.size() == 1, "one owned function"))
    return false;
  auto& body = module.functions.front().body;
  if (!require(body.size() == 47, "all conversion and atomic instructions"))
    return false;

  auto* atom_instruction = outer_get_if<ir::Atom>(&body[27]);
  auto* atom =
      atom_instruction
          ? std::get_if<ir::Atom::GlobalAddU32>(&atom_instruction->variant)
          : nullptr;
  auto* red_instruction = outer_get_if<ir::Red>(&body[28]);
  auto* red =
      red_instruction
          ? std::get_if<ir::Red::GlobalAddU32>(&red_instruction->variant)
          : nullptr;
  auto* legacy_cas_instruction = outer_get_if<ir::Atom>(&body[29]);
  auto* legacy_cas = legacy_cas_instruction
                         ? std::get_if<ir::Atom::GlobalCasB32>(
                               &legacy_cas_instruction->variant)
                         : nullptr;
  auto* modern_cas_instruction = outer_get_if<ir::Atom>(&body[30]);
  auto* modern_cas = modern_cas_instruction
                         ? std::get_if<ir::Atom::GlobalCasB32>(
                               &modern_cas_instruction->variant)
                         : nullptr;
  if (!require(
          atom && red && legacy_cas && modern_cas &&
              std::get<0>(atom->operands).dst.value.register_ref.has_value() &&
              legacy_cas->dst.value.register_ref.has_value() &&
              modern_cas->dst.value.register_ref.has_value() &&
              std::holds_alternative<ir::ResolvedRegisterRef>(
                  std::get<0>(atom->operands).src.value) &&
              std::holds_alternative<ir::ResolvedImmediate>(
                  std::get<0>(red->operands).src.value) &&
              std::holds_alternative<ir::ResolvedImmediate>(
                  legacy_cas->compare.value) &&
              std::holds_alternative<ir::ResolvedRegisterRef>(
                  legacy_cas->swap.value) &&
              std::holds_alternative<ir::ResolvedRegisterRef>(
                  modern_cas->compare.value) &&
              std::holds_alternative<ir::ResolvedImmediate>(
                  modern_cas->swap.value),
          "owned atomic register and immediate sources"))
    return false;

  const auto* inc_instruction = outer_get_if<ir::Atom>(&body[31]);
  const auto* inc =
      inc_instruction
          ? std::get_if<ir::Atom::GlobalIncU32>(&inc_instruction->variant)
          : nullptr;
  const auto* exch_instruction = outer_get_if<ir::Atom>(&body[32]);
  const auto* exch =
      exch_instruction
          ? std::get_if<ir::Atom::GlobalExchB32>(&exch_instruction->variant)
          : nullptr;
  const auto* xor_instruction = outer_get_if<ir::Red>(&body[33]);
  const auto* xor_red =
      xor_instruction
          ? std::get_if<ir::Red::GlobalXorB32>(&xor_instruction->variant)
          : nullptr;
  if (!require(inc && exch && xor_red &&
                   std::holds_alternative<ir::ResolvedRegisterRef>(
                       std::get<0>(inc->operands).src.value) &&
                   std::holds_alternative<ir::ResolvedImmediate>(
                       std::get<0>(exch->operands).src.value) &&
                   std::holds_alternative<ir::ResolvedRegisterRef>(
                       std::get<0>(xor_red->operands).src.value),
               "owned expanded atomic and reduction variants"))
    return false;

  const auto* add_64_instruction = outer_get_if<ir::Atom>(&body[34]);
  const auto* add_64 =
      add_64_instruction
          ? std::get_if<ir::Atom::GlobalAddU64>(&add_64_instruction->variant)
          : nullptr;
  const auto* min_64_instruction = outer_get_if<ir::Atom>(&body[35]);
  const auto* min_64 =
      min_64_instruction
          ? std::get_if<ir::Atom::GlobalMinS64>(&min_64_instruction->variant)
          : nullptr;
  const auto* cas_64_instruction = outer_get_if<ir::Atom>(&body[36]);
  const auto* cas_64 =
      cas_64_instruction
          ? std::get_if<ir::Atom::GlobalCasB64>(&cas_64_instruction->variant)
          : nullptr;
  const auto* red_64_instruction = outer_get_if<ir::Red>(&body[37]);
  const auto* red_64 =
      red_64_instruction
          ? std::get_if<ir::Red::GlobalXorB64>(&red_64_instruction->variant)
          : nullptr;
  if (!require(add_64 && min_64 && cas_64 && red_64 &&
                   std::holds_alternative<ir::ResolvedRegisterRef>(
                       std::get<0>(add_64->operands).src.value) &&
                   std::holds_alternative<ir::ResolvedImmediate>(
                       std::get<0>(min_64->operands).src.value) &&
                   std::holds_alternative<ir::ResolvedRegisterRef>(
                       cas_64->compare.value) &&
                   std::holds_alternative<ir::ResolvedImmediate>(
                       cas_64->swap.value) &&
                   std::holds_alternative<ir::ResolvedRegisterRef>(
                       std::get<0>(red_64->operands).src.value),
               "owned 64-bit atomic and reduction variants"))
    return false;

  const auto* float_atom = outer_get_if<ir::Atom>(&body[38]);
  const auto* float_red = outer_get_if<ir::Red>(&body[39]);
  const auto* double_atom = outer_get_if<ir::Atom>(&body[40]);
  const auto* double_red = outer_get_if<ir::Red>(&body[41]);
  if (!require(float_atom && float_red && double_atom && double_red &&
                   std::holds_alternative<ir::Atom::GlobalAddF32>(
                       float_atom->variant) &&
                   std::holds_alternative<ir::Red::GlobalAddF32>(
                       float_red->variant) &&
                   std::holds_alternative<ir::Atom::GlobalAddF64>(
                       double_atom->variant) &&
                   std::holds_alternative<ir::Red::GlobalAddF64>(
                       double_red->variant),
               "owned float atomic and reduction variants"))
    return false;

  const auto* vector_atom_instruction = outer_get_if<ir::Atom>(&body[42]);
  const auto* vector_atom = vector_atom_instruction
                                ? std::get_if<ir::Atom::VectorAddNoftzF16>(
                                      &vector_atom_instruction->variant)
                                : nullptr;
  const auto* vector_red_instruction = outer_get_if<ir::Red>(&body[43]);
  const auto* vector_red = vector_red_instruction
                               ? std::get_if<ir::Red::VectorAddNoftzF16>(
                                     &vector_red_instruction->variant)
                               : nullptr;
  if (!require(vector_atom && vector_red &&
                   vector_atom->vector.value == ir::VectorArity::V2 &&
                   vector_red->vector.value == ir::VectorArity::V2 &&
                   vector_atom->cache_hint.value &&
                   vector_red->cache_hint.value &&
                   !std::get<1>(vector_atom->operands)
                        .dst.value.elements.front()
                        .has_value() &&
                   std::get<1>(vector_atom->operands)
                           .cache_policy.value.declared_type ==
                       ptx_frontend::base::ScalarType::B64,
               "owned vector atomic and reduction policy layout"))
    return false;

  const auto* shared_async = outer_get_if<ir::Red>(&body[44]);
  const auto* release_async = outer_get_if<ir::Red>(&body[45]);
  if (!require(shared_async && release_async &&
                   std::holds_alternative<ir::Red::AsyncSharedAddU32>(
                       shared_async->variant) &&
                   std::holds_alternative<ir::Red::AsyncReleaseAddU64>(
                       release_async->variant) &&
                   shared_async->address_qualifier.value ==
                       ir::AtomicAddressQualifier::SharedCluster &&
                   release_async->address_qualifier.value ==
                       ir::AtomicAddressQualifier::Global &&
                   std::get<ir::Red::AsyncReleaseAddU64>(release_async->variant)
                       .mmio.value,
               "owned asynchronous reduction modes"))
    return false;

  auto* testp = outer_get_if<ir::Testp>(&body[8]);
  auto* property =
      testp ? std::get_if<ir::Testp::F32>(&testp->variant) : nullptr;
  if (!require(property && property->property.value ==
                               ptx_frontend::base::TestProperty::Normal,
               "typed testp.normal.f32 property"))
    return false;
  const auto* copysign = outer_get_if<ir::Copysign>(&body[9]);
  if (!require(copysign &&
                   std::holds_alternative<ir::Copysign::F32>(copysign->variant),
               "typed copysign.f32 variant"))
    return false;
  const auto* sin = outer_get_if<ir::Sin>(&body[10]);
  const auto* sin_f32 =
      sin ? std::get_if<ir::Sin::ApproxF32>(&sin->variant) : nullptr;
  if (!require(sin_f32 && sin_f32->ftz.value,
               "typed transcendental approximation and FTZ"))
    return false;
  const auto* ex2 = outer_get_if<ir::Ex2>(&body[11]);
  if (!require(
          ex2 && std::holds_alternative<ir::Ex2::ApproxFtzBf16>(ex2->variant),
          "typed BF16 transcendental variant"))
    return false;

  auto* min_binary_instruction = outer_get_if<ir::Min>(&body[12]);
  auto* min_ternary_instruction = outer_get_if<ir::Min>(&body[13]);
  auto* max_binary_instruction = outer_get_if<ir::Max>(&body[14]);
  auto* max_ternary_instruction = outer_get_if<ir::Max>(&body[15]);
  auto* min_binary =
      min_binary_instruction
          ? std::get_if<ir::Min::F32>(&min_binary_instruction->variant)
          : nullptr;
  auto* min_ternary =
      min_ternary_instruction
          ? std::get_if<ir::Min::F32>(&min_ternary_instruction->variant)
          : nullptr;
  auto* max_binary =
      max_binary_instruction
          ? std::get_if<ir::Max::F32>(&max_binary_instruction->variant)
          : nullptr;
  auto* max_ternary =
      max_ternary_instruction
          ? std::get_if<ir::Max::F32>(&max_ternary_instruction->variant)
          : nullptr;
  if (!require(
          min_binary && min_ternary && max_binary && max_ternary &&
              min_binary->operand_layout == ir::ResolvedOperandLayoutTag{0} &&
              min_ternary->operand_layout == ir::ResolvedOperandLayoutTag{1} &&
              max_binary->operand_layout == ir::ResolvedOperandLayoutTag{0} &&
              max_ternary->operand_layout == ir::ResolvedOperandLayoutTag{1} &&
              std::holds_alternative<ir::Min::F32::BinaryOperands>(
                  min_binary->operands) &&
              std::holds_alternative<ir::Min::F32::TernaryOperands>(
                  min_ternary->operands) &&
              std::holds_alternative<ir::Max::F32::BinaryOperands>(
                  max_binary->operands) &&
              std::holds_alternative<ir::Max::F32::TernaryOperands>(
                  max_ternary->operands) &&
              min_binary->ftz.value && min_binary->nan.value &&
              min_binary->xorsign_abs.value && !min_binary->abs.value &&
              min_ternary->ftz.value && min_ternary->nan.value &&
              !min_ternary->xorsign_abs.value && min_ternary->abs.value &&
              max_binary->xorsign_abs.value && !max_binary->abs.value &&
              !max_ternary->xorsign_abs.value && max_ternary->abs.value,
          "typed MIN/MAX modifier and operand layouts"))
    return false;

  auto* add_register_instruction = outer_get_if<ir::Add>(&body[16]);
  auto* add_immediate_instruction = outer_get_if<ir::Add>(&body[17]);
  auto* sub_register_instruction = outer_get_if<ir::Sub>(&body[18]);
  auto* sub_immediate_instruction = outer_get_if<ir::Sub>(&body[19]);
  auto* add_register =
      add_register_instruction
          ? std::get_if<ir::Add::MixedF32>(&add_register_instruction->variant)
          : nullptr;
  auto* add_immediate =
      add_immediate_instruction
          ? std::get_if<ir::Add::MixedF32>(&add_immediate_instruction->variant)
          : nullptr;
  auto* sub_register =
      sub_register_instruction
          ? std::get_if<ir::Sub::MixedF32>(&sub_register_instruction->variant)
          : nullptr;
  auto* sub_immediate =
      sub_immediate_instruction
          ? std::get_if<ir::Sub::MixedF32>(&sub_immediate_instruction->variant)
          : nullptr;
  if (!require(add_register && add_immediate && sub_register && sub_immediate &&
                   add_register->input_type.value ==
                       ptx_frontend::base::ScalarType::F16 &&
                   add_immediate->input_type.value ==
                       ptx_frontend::base::ScalarType::F16 &&
                   sub_register->input_type.value ==
                       ptx_frontend::base::ScalarType::BF16 &&
                   sub_immediate->input_type.value ==
                       ptx_frontend::base::ScalarType::BF16,
               "typed mixed ADD/SUB variants"))
    return false;
  const auto* addend_register =
      std::get_if<ir::ResolvedRegisterRef>(&add_register->addend.value);
  const auto* subtrahend_register =
      std::get_if<ir::ResolvedRegisterRef>(&sub_register->subtrahend.value);
  const auto* addend_immediate =
      std::get_if<ir::ResolvedImmediate>(&add_immediate->addend.value);
  const auto* subtrahend_immediate =
      std::get_if<ir::ResolvedImmediate>(&sub_immediate->subtrahend.value);
  if (!require(
          addend_register && addend_register->spelling == "%f1" &&
              addend_register->declared_type ==
                  ptx_frontend::base::ScalarType::F32 &&
              subtrahend_register && subtrahend_register->spelling == "%f1" &&
              subtrahend_register->declared_type ==
                  ptx_frontend::base::ScalarType::F32 &&
              addend_immediate &&
              addend_immediate->type == ptx_frontend::base::ScalarType::F32 &&
              addend_immediate->bits == 0x3F800000u && subtrahend_immediate &&
              subtrahend_immediate->type ==
                  ptx_frontend::base::ScalarType::F32 &&
              subtrahend_immediate->bits == 0x40000000u,
          "mixed register and floating-immediate values"))
    return false;

  auto* set_instruction = outer_get_if<ir::Set>(&body[20]);
  auto* set_float =
      set_instruction
          ? std::get_if<ir::Set::FloatBoolean>(&set_instruction->variant)
          : nullptr;
  auto* selp_scalar_instruction = outer_get_if<ir::Selp>(&body[21]);
  auto* selp_scalar =
      selp_scalar_instruction
          ? std::get_if<ir::Selp::Scalar>(&selp_scalar_instruction->variant)
          : nullptr;
  auto* selp_u32_instruction = outer_get_if<ir::Selp>(&body[22]);
  auto* selp_u32 =
      selp_u32_instruction
          ? std::get_if<ir::Selp::U32>(&selp_u32_instruction->variant)
          : nullptr;
  if (!require(
          set_float &&
              set_float->comparison.value ==
                  ptx_frontend::base::ComparisonOperator::Nan &&
              set_float->boolean.value ==
                  ptx_frontend::base::BooleanOperator::Xor &&
              std::get<ir::ResolvedPredicateConstant>(set_float->combine.value)
                  .value &&
              selp_scalar &&
              selp_scalar->type.value == ptx_frontend::base::ScalarType::S32 &&
              std::get<ir::ResolvedPredicate>(selp_scalar->predicate.value)
                  .negated &&
              selp_u32 &&
              std::get<ir::ResolvedPredicateConstant>(selp_u32->predicate.value)
                  .value,
          "typed SET and both SELP public alternatives"))
    return false;

  auto* set_half_instruction = outer_get_if<ir::Set>(&body[23]);
  auto* set_half = set_half_instruction
                       ? std::get_if<ir::Set::HalfNativeF16x2Boolean>(
                             &set_half_instruction->variant)
                       : nullptr;
  auto* set_bfloat_instruction = outer_get_if<ir::Set>(&body[24]);
  auto* set_bfloat =
      set_bfloat_instruction
          ? std::get_if<ir::Set::HalfBf16F16>(&set_bfloat_instruction->variant)
          : nullptr;
  if (!require(
          set_half && set_half->ftz.value &&
              set_half->comparison.value ==
                  ptx_frontend::base::ComparisonOperator::Num &&
              std::get<ir::ResolvedPredicateConstant>(set_half->combine.value)
                  .value &&
              set_bfloat &&
              set_bfloat->comparison.value ==
                  ptx_frontend::base::ComparisonOperator::Eq &&
              set_bfloat->dst.value.declared_type ==
                  ptx_frontend::base::ScalarType::B16,
          "typed half/bfloat SET alternatives and owned operands"))
    return false;

  auto* slct_integer_instruction = outer_get_if<ir::Slct>(&body[25]);
  auto* slct_integer =
      slct_integer_instruction
          ? std::get_if<ir::Slct::S32>(&slct_integer_instruction->variant)
          : nullptr;
  auto* slct_floating_instruction = outer_get_if<ir::Slct>(&body[26]);
  auto* slct_floating =
      slct_floating_instruction
          ? std::get_if<ir::Slct::F32>(&slct_floating_instruction->variant)
          : nullptr;
  if (!require(slct_integer &&
                   slct_integer->dtype.value ==
                       ptx_frontend::base::ScalarType::U32 &&
                   std::holds_alternative<ir::ResolvedImmediate>(
                       slct_integer->src_false.value) &&
                   std::holds_alternative<ir::ResolvedImmediate>(
                       slct_integer->selector.value) &&
                   slct_floating && slct_floating->ftz.value &&
                   slct_floating->dtype.value ==
                       ptx_frontend::base::ScalarType::U64 &&
                   std::holds_alternative<ir::ResolvedImmediate>(
                       slct_floating->selector.value),
               "typed SLCT selector variants and owned numeric operands"))
    return false;

  min_binary->abs.value = true;
  const bool forbidden_binary_modifier = rejectsMutation(
      module, ir::checker::CheckDiagnosticKind::ModifierNotAllowedForLayout,
      "binary MIN forbids ternary abs modifier");
  min_binary->abs.value = false;
  if (!forbidden_binary_modifier)
    return false;
  max_ternary->xorsign_abs.value = true;
  const bool forbidden_ternary_modifier = rejectsMutation(
      module, ir::checker::CheckDiagnosticKind::ModifierNotAllowedForLayout,
      "ternary MAX forbids binary xorsign modifier");
  max_ternary->xorsign_abs.value = false;
  if (!forbidden_ternary_modifier)
    return false;
  const auto original_abs = min_ternary->abs;
  min_ternary->abs = ptx_frontend::WithLocs<bool>{false};
  min_ternary->operand_layout = ir::ResolvedOperandLayoutTag{0};
  const bool mismatched_layout = rejectsMutation(
      module, ir::checker::CheckDiagnosticKind::OperandLayoutPayloadMismatch,
      "MIN rejects a layout tag/payload mismatch");
  min_ternary->operand_layout = ir::ResolvedOperandLayoutTag{1};
  min_ternary->abs = original_abs;
  if (!mismatched_layout)
    return false;
  property->property.value = ptx_frontend::base::TestProperty::Invalid;
  const bool invalid_property = rejectsMutation(
      module, ir::checker::CheckDiagnosticKind::ModifierValueDomainMismatch,
      "TESTP rejects an invalid typed property");
  property->property.value = ptx_frontend::base::TestProperty::Normal;
  if (!invalid_property)
    return false;
  set_float->comparison.value = ptx_frontend::base::ComparisonOperator::Lo;
  const bool invalid_set_domain = rejectsMutation(
      module, ir::checker::CheckDiagnosticKind::ModifierValueDomainMismatch,
      "SET rejects a comparison outside its floating domain");
  set_float->comparison.value = ptx_frontend::base::ComparisonOperator::Nan;
  if (!invalid_set_domain)
    return false;
  set_half->comparison.value = ptx_frontend::base::ComparisonOperator::Lo;
  const bool invalid_half_domain = rejectsMutation(
      module, ir::checker::CheckDiagnosticKind::ModifierValueDomainMismatch,
      "half SET rejects an integer-only comparison");
  set_half->comparison.value = ptx_frontend::base::ComparisonOperator::Num;
  if (!invalid_half_domain)
    return false;
  slct_floating->dtype.value = ptx_frontend::base::ScalarType::F16;
  const bool invalid_slct_data_type = rejectsMutation(
      module, ir::checker::CheckDiagnosticKind::ModifierValueDomainMismatch,
      "SLCT rejects an unsupported data type");
  slct_floating->dtype.value = ptx_frontend::base::ScalarType::U64;
  if (!invalid_slct_data_type)
    return false;
  selp_scalar->type.value = ptx_frontend::base::ScalarType::F16;
  const bool invalid_selp_domain = rejectsMutation(
      module, ir::checker::CheckDiagnosticKind::ModifierValueDomainMismatch,
      "SELP rejects a type outside its scalar domain");
  selp_scalar->type.value = ptx_frontend::base::ScalarType::S32;
  return invalid_selp_domain &&
         require(ir::validateModule(
                     module, ir::ModuleValidationPolicy::RequireCompleteContext)
                     .has_value(),
                 "restored owned module validates");
}

/**
 * Resolve a module while parser state is alive, then validate the owned model
 * after the source, parser, and syntax AST have left scope.
 */
int runOwnedValidation() {
  /// Owns resolved values after parser-owned state has been destroyed.
  std::optional<ptx_frontend::resolved_ir::ResolvedModule> owned;
  {
    std::string source{kFixture};
    ptx_frontend::PtxSyntaxParser parser{source};
    auto parsed = parser.parseModule();
    if (!parsed) {
      std::cerr << "parseModule failed with " << parsed.diagnostics.size()
                << " diagnostic(s)\n";
      return 1;
    }

    auto resolved = ptx_frontend::resolved_ir::resolveModuleOnly(*parsed);
    if (!resolved) {
      std::cerr << "resolveModuleOnly failed with " << resolved.error().size()
                << " diagnostic(s)\n";
      for (const auto& diagnostic : resolved.error())
        std::cerr << diagnostic.message << '\n';
      return 2;
    }

    owned.emplace(std::move(*resolved));
  }

  if (!require(owned.has_value(), "resolved module survived parser lifetime"))
    return 3;
  const auto checked = ptx_frontend::resolved_ir::validateModule(
      *owned, ptx_frontend::resolved_ir::ModuleValidationPolicy::
                  RequireCompleteContext);
  if (!checked) {
    std::cerr << "owned validateModule failed with " << checked.error().size()
              << " diagnostic(s)\n";
    for (const auto& diagnostic : checked.error())
      std::cerr << diagnostic.message << '\n';
    return 4;
  }
  if (!checkExtendedContract(*owned))
    return 5;
  return 0;
}

/** Exercise the installed non-bulk copy variants after AST destruction. */
bool checkCpAsyncContract() {
  constexpr std::string_view source = R"ptx(
.version 9.3
.target sm_80
.address_size 64
.global .align 16 .b8 global_value[32];
.shared .align 16 .b8 shared_value[32];
.entry k() {
  .reg .u64 %policy;
  cp.async.ca.shared.global [shared_value], [global_value], 4;
  cp.async.ca.shared.global.L2::cache_hint [shared_value], [global_value], 4, %policy;
  cp.async.ca.shared.global.L2::128B [shared_value], [global_value], 4, 0;
  cp.async.wait_all;
  ret;
}
)ptx";
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!require(ast.has_value(), "non-bulk copy fixture parses"))
      return false;
    auto resolved = ir::resolveModuleOnly(*ast);
    if (!require(resolved.has_value(), "non-bulk copy fixture resolves"))
      return false;
    owned.emplace(std::move(*resolved));
  }
  if (!require(ir::validateModule(
                   *owned, ir::ModuleValidationPolicy::RequireCompleteContext)
                   .has_value(),
               "owned non-bulk copies validate"))
    return false;
  const auto& body = owned->functions.front().body;
  if (!require(body.size() >= 3, "non-bulk copy forms retained"))
    return false;
  const auto* original_instruction = outer_get_if<ir::Cp>(&body[0]);
  const auto* original = original_instruction
                             ? std::get_if<ir::Cp::AsyncCaSharedGlobal>(
                                   &original_instruction->variant)
                             : nullptr;
  if (!require(original && !original->dst.locs.empty() &&
                   !original->src.locs.empty() &&
                   original->cp_size.value.bits == 4,
               "original three-operand public copy fields remain available"))
    return false;
  const auto* policy_instruction = outer_get_if<ir::Cp>(&body[1]);
  const auto* policy =
      policy_instruction
          ? std::get_if<ir::Cp::AsyncCaSharedGlobalCacheHintControl>(
                &policy_instruction->variant)
          : nullptr;
  return require(
      policy &&
          std::holds_alternative<ir::ResolvedCpAsyncCachePolicy>(
              policy->source_control.value) &&
          std::get<ir::ResolvedCpAsyncCachePolicy>(policy->source_control.value)
                  .register_ref.declared_type ==
              ptx_frontend::base::ScalarType::U64,
      "typed cache-policy fourth operand survives AST destruction");
}

/** Check installed bulk-copy completion identity after syntax storage expires. */
bool checkBulkAsyncContract() {
  constexpr std::string_view source = R"ptx(
.version 9.3
.target sm_100f
.address_size 64
.global .align 16 .b8 g[64];
.shared .align 16 .b8 s[64];
.shared .align 8 .b64 bar;
.entry k() {
  cp.async.bulk.shared::cta.global.mbarrier::complete_tx::bytes [s], [g], 16, [bar];
  cp.async.bulk.global.shared::cta.bulk_group [g], [s], 16;
  cp.async.bulk.commit_group;
  cp.async.bulk.wait_group.read 0;
  st.bulk.shared::cta [s], 16, 0;
}

)ptx";
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!require(ast.has_value(), "bulk async fixture parses"))
      return false;
    auto resolved = ir::resolveModuleOnly(*ast);
    if (!require(resolved.has_value(), "bulk async fixture resolves"))
      return false;
    owned.emplace(std::move(*resolved));
  }
  if (!require(ir::validateModule(
                   *owned, ir::ModuleValidationPolicy::RequireCompleteContext)
                   .has_value(),
               "owned bulk async module validates"))
    return false;
  const auto& body = owned->functions.front().body;
  if (!require(body.size() == 5, "bulk async forms retained"))
    return false;
  const auto* copy = outer_get_if<ir::Cp>(&body[0]);
  const auto* mbar =
      copy ? std::get_if<ir::Cp::AsyncBulkGlobalSharedCta>(&copy->variant)
           : nullptr;
  const auto* group = outer_get_if<ir::Cp>(&body[1]);
  const auto* bulk_group =
      group ? std::get_if<ir::Cp::AsyncBulkSharedCtaGlobal>(&group->variant)
            : nullptr;
  return require(mbar && bulk_group &&
                     ir::Cp::AsyncBulkGlobalSharedCta::completion_kind ==
                         ptx_frontend::base::AsyncCompletionKind::
                             MbarrierCompleteTxBytes &&
                     ir::Cp::AsyncBulkSharedCtaGlobal::completion_kind ==
                         ptx_frontend::base::AsyncCompletionKind::BulkGroup &&
                     std::holds_alternative<ir::Cp::AsyncBulkCommitGroup>(
                         outer_get<ir::Cp>(body[2]).variant) &&
                     std::get<ir::Cp::AsyncBulkWaitGroup>(
                         outer_get<ir::Cp>(body[3]).variant)
                         .read.value,
                 "public bulk async completion and group controls");
}

/** Exercise installed tiled tensor variants after syntax ownership expires. */
bool checkTensorAsyncContract() {
  constexpr std::string_view source = R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .align 64 .b8 tensor_map[128];
.shared .align 128 .b8 tile_data[1024];
.shared .align 8 .b64 barrier;
.entry kernel() {
  .reg .s32 %coord<2>;
  cp.async.bulk.prefetch.tensor.2d.L2.global.tile [tensor_map, {%coord0, 4294967296}];
  cp.async.bulk.tensor.2d.shared::cluster.global.mbarrier::complete_tx::bytes [tile_data], [tensor_map, {18446744073709551615, %coord1}], [barrier];
  cp.async.bulk.tensor.1d.global.shared::cta.tile.bulk_group [tensor_map, {-4294967296}], [tile_data];
  cp.async.bulk.commit_group;
  cp.async.bulk.wait_group 0;
}

)ptx";
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!require(ast.has_value(), "tensor fixture parses"))
      return false;
    auto resolved = ir::resolveModuleOnly(*ast);
    if (!require(resolved.has_value(), "tensor fixture resolves"))
      return false;
    owned.emplace(std::move(*resolved));
  }
  if (!require(ir::validateModule(
                   *owned, ir::ModuleValidationPolicy::RequireCompleteContext)
                   .has_value(),
               "owned tensor module validates"))
    return false;
  const auto& body = owned->functions.front().body;
  if (!require(body.size() == 5, "tensor forms retained"))
    return false;
  const auto* prefetch = std::get_if<ir::Cp::AsyncBulkPrefetchTensor2d>(
      &outer_get<ir::Cp>(body[0]).variant);
  const auto* load = std::get_if<ir::Cp::AsyncBulkTensor2dSharedCluster>(
      &outer_get<ir::Cp>(body[1]).variant);
  const auto* store = std::get_if<ir::Cp::AsyncBulkTensor1dGlobalSharedCta>(
      &outer_get<ir::Cp>(body[2]).variant);
  const auto* prefetch_coordinate =
      prefetch && prefetch->tensor.value.coordinates.elements.size() == 2
          ? std::get_if<ir::ResolvedImmediate>(
                &prefetch->tensor.value.coordinates.elements[1])
          : nullptr;
  const auto* load_coordinate =
      load && load->tensor.value.coordinates.elements.size() == 2
          ? std::get_if<ir::ResolvedImmediate>(
                &load->tensor.value.coordinates.elements[0])
          : nullptr;
  const auto* store_coordinate =
      store && store->tensor.value.coordinates.elements.size() == 1
          ? std::get_if<ir::ResolvedImmediate>(
                &store->tensor.value.coordinates.elements[0])
          : nullptr;
  return require(
      prefetch && load && store && prefetch->tile.value &&
          prefetch->tensor.value.rank == ir::TensorRank::Two &&
          load->tensor.value.coordinates.elements.size() == 2 &&
          store->tile.value && prefetch_coordinate &&
          prefetch_coordinate->bits == 0 &&
          prefetch_coordinate->integer_source_bits == 0x100000000ULL &&
          !prefetch_coordinate->is_negative && load_coordinate &&
          load_coordinate->bits == 0xffffffffULL &&
          load_coordinate->integer_source_bits == 0xffffffffffffffffULL &&
          !load_coordinate->is_negative && store_coordinate &&
          store_coordinate->bits == 0 &&
          store_coordinate->integer_source_bits == 0xffffffff00000000ULL &&
          store_coordinate->is_negative &&
          ir::Cp::AsyncBulkTensor2dSharedCluster::completion_kind ==
              ptx_frontend::base::AsyncCompletionKind::
                  MbarrierCompleteTxBytes &&
          ir::Cp::AsyncBulkTensor1dGlobalSharedCta::completion_kind ==
              ptx_frontend::base::AsyncCompletionKind::BulkGroup,
      "installed tensor map, rank, tile, and completion identities");
}

/** Check an installed tiled reduction's owned operation and descriptor query. */
bool checkTensorReductionContract() {
  constexpr std::string_view source = R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .align 64 .b8 tensor_map[128];
.shared .align 16 .b8 src[1024];
.entry kernel() {
  cp.reduce.async.bulk.tensor.2d.global.shared::cta.add.tile.bulk_group
      [tensor_map, {4294967296, 1}], [src];
}
)ptx";
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!require(ast.has_value(), "tensor reduction fixture parses"))
      return false;
    auto resolved = ir::resolveModuleOnly(*ast);
    if (!require(resolved.has_value(), "tensor reduction fixture resolves"))
      return false;
    owned.emplace(std::move(*resolved));
  }
  if (!require(ir::validateModule(
                   *owned, ir::ModuleValidationPolicy::RequireCompleteContext)
                   .has_value(),
               "owned tensor reduction validates"))
    return false;
  const auto& body = owned->functions.front().body;
  if (!require(body.size() == 1, "tensor reduction retained"))
    return false;
  const auto* reduction = std::get_if<ir::Cp::ReduceAsyncBulkTensor2dAdd>(
      &outer_get<ir::Cp>(body[0]).variant);
  const auto* coordinate =
      reduction && reduction->tensor.value.coordinates.elements.size() == 2
          ? std::get_if<ir::ResolvedImmediate>(
                &reduction->tensor.value.coordinates.elements[0])
          : nullptr;
  return require(
      reduction &&
          reduction->tensor_reduction_op == ir::TensorReductionOp::Add &&
          reduction->completion_kind ==
              ptx_frontend::base::AsyncCompletionKind::BulkGroup &&
          reduction->tile.value && !reduction->tile.locs.empty() &&
          coordinate && coordinate->bits == 0 &&
          coordinate->integer_source_bits == 0x100000000ULL &&
          ir::tensor_reduction_accepts_element_type(
              ir::TensorReductionOp::Add,
              ptx_frontend::base::ScalarType::F32) &&
          !ir::tensor_reduction_accepts_element_type(
              ir::TensorReductionOp::Add, ptx_frontend::base::ScalarType::B32),
      "installed typed reduction, completion, and narrowed coordinate");
}

/** Exercise installed fixed no-offset store and reduction mode ownership. */
bool checkTensorNoOffsetsContract() {
  constexpr std::string_view source = R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .align 64 .b8 tensor_map[128];
.shared .align 16 .b8 src[1024];
.entry kernel() {
  cp.async.bulk.tensor.3d.global.shared::cta.im2col_no_offs.bulk_group
      [tensor_map, {4294967296, 1, 2}], [src];
  cp.reduce.async.bulk.tensor.5d.global.shared::cta.xor.im2col_no_offs.bulk_group
      [tensor_map, {0, 1, 2, 3, 4}], [src];
}
)ptx";
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!require(ast.has_value(), "tensor no-offset fixture parses"))
      return false;
    auto resolved = ir::resolveModuleOnly(*ast);
    if (!require(resolved.has_value(), "tensor no-offset fixture resolves"))
      return false;
    owned.emplace(std::move(*resolved));
  }
  if (!require(ir::validateModule(
                   *owned, ir::ModuleValidationPolicy::RequireCompleteContext)
                   .has_value(),
               "owned tensor no-offset module validates"))
    return false;
  const auto& body = owned->functions.front().body;
  if (!require(body.size() == 2, "tensor no-offset forms retained"))
    return false;
  const auto* store =
      std::get_if<ir::Cp::AsyncBulkTensor3dGlobalSharedCtaIm2colNoOffs>(
          &outer_get<ir::Cp>(body[0]).variant);
  const auto* reduction =
      std::get_if<ir::Cp::ReduceAsyncBulkTensor5dXorIm2colNoOffs>(
          &outer_get<ir::Cp>(body[1]).variant);
  const auto* first = store ? std::get_if<ir::ResolvedImmediate>(
                                  &store->tensor.value.coordinates.elements[0])
                            : nullptr;
  return require(
      store && reduction &&
          store->tensor.value.mode == ir::TensorAccessMode::Im2colNoOffs &&
          reduction->tensor.value.mode == ir::TensorAccessMode::Im2colNoOffs &&
          store->tensor.value.rank == ir::TensorRank::Three &&
          reduction->tensor.value.rank == ir::TensorRank::Five &&
          reduction->tensor_reduction_op == ir::TensorReductionOp::Xor &&
          store->completion_kind ==
              ptx_frontend::base::AsyncCompletionKind::BulkGroup &&
          first && first->bits == 0 &&
          first->integer_source_bits == 0x100000000ULL,
      "installed fixed tensor mode, operation, and coordinate provenance");
}

/** Exercise installed im2col mode, optional info, and semantic roles. */
bool checkTensorIm2colInfoContract() {
  constexpr std::string_view source = R"ptx(
.version 9.3
.target sm_100a
.address_size 64
.global .align 64 .b8 tensor_map[128];
.shared .align 16 .b8 dst[1024];
.shared .align 8 .b64 mbar;
.entry kernel() {
  .reg .s32 %r<5>;
  .reg .u16 %u<3>;
  cp.async.bulk.tensor.4d.shared::cluster.global.im2col.mbarrier::complete_tx::bytes
      [dst], [tensor_map, {%r0, %r1, %r2, %r3}], [mbar], {65537, %u0};
  cp.async.bulk.prefetch.tensor.3d.L2.global.im2col::w::128
      [tensor_map, {%r0, %r1, %r2}];
}
)ptx";
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!require(ast.has_value(), "tensor im2col fixture parses"))
      return false;
    auto resolved = ir::resolveModuleOnly(*ast);
    if (!require(resolved.has_value(), "tensor im2col fixture resolves"))
      return false;
    owned.emplace(std::move(*resolved));
  }
  if (!require(ir::validateModule(
                   *owned, ir::ModuleValidationPolicy::RequireCompleteContext)
                   .has_value(),
               "owned tensor im2col module validates"))
    return false;
  const auto& body = owned->functions.front().body;
  if (!require(body.size() == 2, "tensor im2col forms retained"))
    return false;
  const auto* first = outer_get_if<ir::Cp>(&body[0]);
  const auto* second = outer_get_if<ir::Cp>(&body[1]);
  if (!require(first && second, "tensor im2col Cp identities retained"))
    return false;
  const auto* load = std::get_if<ir::Cp::AsyncBulkTensor4dSharedClusterIm2col>(
      &first->variant);
  const auto* prefetch =
      std::get_if<ir::Cp::AsyncBulkPrefetchTensor3dIm2colW128>(
          &second->variant);
  if (!require(load && prefetch, "typed im2col alternatives retained"))
    return false;
  bool load_info_valid = false;
  std::visit(
      [&](const auto& payload) {
        if constexpr (requires {
                        payload.im2col_info;
                        payload.tensor;
                      }) {
          const auto& info = payload.im2col_info.value;
          const auto* first_value =
              std::get_if<ir::ResolvedImmediate>(&info.elements.front());
          load_info_valid =
              info.elements.size() == 2 && first_value &&
              first_value->type == ptx_frontend::base::ScalarType::U16 &&
              first_value->bits == 1 &&
              first_value->integer_source_bits == 65537 &&
              ir::tensor_im2col_info_role(payload.tensor.value, info, 0) ==
                  ir::TensorIm2colInfoRole::OffsetW &&
              ir::tensor_im2col_info_role(payload.tensor.value, info, 1) ==
                  ir::TensorIm2colInfoRole::OffsetH;
        }
      },
      load->operands);
  bool prefetch_absent = false;
  std::visit(
      [&](const auto& payload) {
        if constexpr (!requires { payload.im2col_info; })
          prefetch_absent =
              payload.tensor.value.mode == ir::TensorAccessMode::Im2colW128;
      },
      prefetch->operands);
  return require(load_info_valid && prefetch_absent,
                 "installed im2col U16, roles, and explicit absence retained");
}

/** Check rank and five ordered roles through installed, AST-released IR. */
bool checkTensorGatherScatterContract() {
  constexpr std::string_view source = R"ptx(
.version 9.3
.target sm_110a
.address_size 64
.global .align 64 .b8 tensor_map[128];
.shared .align 16 .b8 dst[1024];
.shared .align 16 .b8 src[1024];
.shared .align 8 .b64 mbar;
.entry kernel() {
  .reg .s32 %r<5>;
  cp.async.bulk.tensor.2d.shared::cluster.global.tile::gather4.mbarrier::complete_tx::bytes
      [dst], [tensor_map, {%r0, %r1, %r2, %r3, %r4}], [mbar];
  cp.async.bulk.prefetch.tensor.2d.L2.global.tile::gather4
      [tensor_map, {0, 1, 2, 3, 4}];
  cp.async.bulk.tensor.2d.global.shared::cta.tile::scatter4.bulk_group
      [tensor_map, {0, 1, 2, 3, 4}], [src];
}
)ptx";
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!require(ast.has_value(), "gather/scatter fixture parses"))
      return false;
    auto resolved = ir::resolveModuleOnly(*ast);
    if (!require(resolved.has_value(), "gather/scatter fixture resolves"))
      return false;
    owned.emplace(std::move(*resolved));
  }
  if (!require(ir::validateModule(
                   *owned, ir::ModuleValidationPolicy::RequireCompleteContext)
                   .has_value(),
               "owned gather/scatter module validates"))
    return false;
  const auto& body = owned->functions.front().body;
  if (!require(body.size() == 3, "gather/scatter forms retained"))
    return false;
  const auto* first = outer_get_if<ir::Cp>(&body[0]);
  const auto* second = outer_get_if<ir::Cp>(&body[1]);
  const auto* third = outer_get_if<ir::Cp>(&body[2]);
  if (!require(first && second && third, "gather/scatter Cp retained"))
    return false;
  const auto* load =
      std::get_if<ir::Cp::AsyncBulkTensor2dSharedClusterTileGather4>(
          &first->variant);
  const auto* prefetch =
      std::get_if<ir::Cp::AsyncBulkPrefetchTensor2dTileGather4>(
          &second->variant);
  const auto* scatter =
      std::get_if<ir::Cp::AsyncBulkTensor2dGlobalSharedCtaTileScatter4>(
          &third->variant);
  if (!require(load && prefetch && scatter,
               "typed gather/scatter forms retained"))
    return false;
  constexpr std::array roles{ir::TensorGatherScatterCoordinateRole::Column,
                             ir::TensorGatherScatterCoordinateRole::Row0,
                             ir::TensorGatherScatterCoordinateRole::Row1,
                             ir::TensorGatherScatterCoordinateRole::Row2,
                             ir::TensorGatherScatterCoordinateRole::Row3};
  const auto matches = [&](const ir::ResolvedTensorOperand& tensor,
                           ir::TensorAccessMode mode) {
    if (tensor.mode != mode || tensor.rank != ir::TensorRank::Two ||
        tensor.coordinates.elements.size() != 5 ||
        tensor.coordinate_ranges.size() != 5)
      return false;
    for (size_t index = 0; index < roles.size(); ++index)
      if (ir::tensor_gather_scatter_coordinate_role(tensor, index) !=
          roles[index])
        return false;
    return !ir::tensor_gather_scatter_coordinate_role(tensor, roles.size());
  };
  return require(
      matches(load->tensor.value, ir::TensorAccessMode::TileGather4) &&
          matches(prefetch->tensor.value, ir::TensorAccessMode::TileGather4) &&
          matches(scatter->tensor.value, ir::TensorAccessMode::TileScatter4),
      "installed rank-two five-coordinate roles retained");
}

/** Exercise installed tensor-map update projections after syntax ownership ends. */
bool checkTensorMapReplacementContract() {
  constexpr std::string_view source = R"ptx(
.version 9.3
.target sm_100a
.address_size 64
.global .align 128 .b8 tensor_map[128];
.shared .align 128 .b8 shared_map[128];
.entry kernel() {
  tensormap.replace.tile.rank.global.b1024.b32 [tensor_map], 4294967297;
  tensormap.replace.tile.elemtype.shared::cta.b1024.b32 [shared_map], 15;
  tensormap.cp_fenceproxy.global.shared::cta.tensormap::generic.release.gpu.sync.aligned [tensor_map], [shared_map], 128;
}
)ptx";
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!require(ast.has_value(), "tensor-map update fixture parses"))
      return false;
    auto resolved = ir::resolveModuleOnly(*ast);
    if (!require(resolved.has_value(), "tensor-map update fixture resolves"))
      return false;
    owned.emplace(std::move(*resolved));
  }
  if (!require(ir::validateModule(
                   *owned, ir::ModuleValidationPolicy::RequireCompleteContext)
                   .has_value(),
               "owned tensor-map update module validates"))
    return false;
  const auto& body = owned->functions.front().body;
  if (!require(body.size() == 3, "tensor-map updates retained"))
    return false;
  const auto* rank = std::get_if<ir::Tensormap::ReplaceTileRank>(
      &outer_get<ir::Tensormap>(body[0]).variant);
  const auto* element = std::get_if<ir::Tensormap::ReplaceTileElemtype>(
      &outer_get<ir::Tensormap>(body[1]).variant);
  const auto* fence = std::get_if<ir::Tensormap::CpFenceproxyOrdinary>(
      &outer_get<ir::Tensormap>(body[2]).variant);
  const auto rank_ref = rank ? rank->tensor_map_ref() : std::nullopt;
  const auto element_ref = element ? element->tensor_map_ref() : std::nullopt;
  const auto* rank_symbol =
      rank_ref ? std::get_if<ir::ResolvedSymbolRef>(&rank_ref->address.base)
               : nullptr;
  const auto* element_symbol =
      element_ref
          ? std::get_if<ir::ResolvedSymbolRef>(&element_ref->address.base)
          : nullptr;
  return require(
      rank && element && fence && rank_symbol && element_symbol &&
          rank_symbol->spelling == "tensor_map" &&
          element_symbol->spelling == "shared_map" && rank_symbol->symbol_id &&
          element_symbol->symbol_id &&
          std::get<ir::ResolvedImmediate>(rank->new_val.value).bits == 1 &&
          std::get<ir::ResolvedImmediate>(rank->new_val.value)
                  .integer_source_bits == 0x100000001ULL &&
          element->encoded_value() ==
              ir::TensorMapElementType::B6x16P32OrB6p2x16 &&
          fence->scope.value == ir::MemoryScope::Gpu &&
          fence->proxy_pair == ir::ProxyKindPair::TensormapToGeneric &&
          fence->size.value.bits == 128 &&
          ir::Tensormap::ReplaceTileRank::replacement_field ==
              ir::TensorMapReplaceField::Rank,
      "installed typed update, source code, and reference identity");
}
}  // namespace

/** Check the installed public conversion, comparison, and resolved-IR contract. */
/** Exercise installed multicast mask ownership after parsed syntax is gone. */
bool checkTensorMulticastContract() {
  constexpr std::string_view source = R"ptx(
.version 9.3
.target sm_110a
.address_size 64
.global .align 64 .b8 tensor_map[128];
.shared .align 16 .b8 dst[1024];
.shared .align 8 .b64 mbar;
.entry kernel() {
  .reg .s32 %r<3>;
  .reg .u16 %info;
  .reg .b16 %mask;
  cp.async.bulk.tensor.1d.shared::cluster.global.tile.mbarrier::complete_tx::bytes.multicast::cluster
      [dst], [tensor_map, {%r0}], [mbar], 65536;
  cp.async.bulk.tensor.3d.shared::cluster.global.im2col.mbarrier::complete_tx::bytes.multicast::cluster
      [dst], [tensor_map, {%r0, %r1, %r2}], [mbar], {%info}, %mask;
}
)ptx";
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!require(ast.has_value(), "multicast fixture parses"))
      return false;
    auto resolved = ir::resolveModuleOnly(*ast);
    if (!require(resolved.has_value(), "multicast fixture resolves"))
      return false;
    owned.emplace(std::move(*resolved));
  }
  if (!require(ir::validateModule(
                   *owned, ir::ModuleValidationPolicy::RequireCompleteContext)
                   .has_value(),
               "owned multicast module validates"))
    return false;
  const auto& body = owned->functions.front().body;
  if (!require(body.size() == 2, "both multicast layouts retained"))
    return false;
  const auto* first = outer_get_if<ir::Cp>(&body[0]);
  const auto* second = outer_get_if<ir::Cp>(&body[1]);
  if (!require(first && second, "multicast Cp instructions retained"))
    return false;
  const auto* tile =
      std::get_if<ir::Cp::AsyncBulkTensor1dSharedClusterMulticast>(
          &first->variant);
  const auto* im2col =
      std::get_if<ir::Cp::AsyncBulkTensor3dSharedClusterIm2colMulticast>(
          &second->variant);
  if (!require(
          tile && im2col && tile->tensor_multicast && im2col->tensor_multicast,
          "typed multicast variants retained"))
    return false;
  const auto* literal =
      std::get_if<ir::ResolvedImmediate>(&tile->cta_mask.value);
  const bool literal_ok = literal && literal->bits == 0 &&
                          literal->integer_source_bits == 65536 &&
                          literal->type == ptx_frontend::base::ScalarType::U16;
  const bool info_ok = std::visit(
      [](const auto& payload) {
        if constexpr (requires {
                        payload.im2col_info;
                        payload.cta_mask;
                      })
          return payload.im2col_info.value.elements.size() == 1 &&
                 std::holds_alternative<ir::ResolvedRegisterRef>(
                     payload.cta_mask.value);
        return false;
      },
      im2col->operands);
  return require(literal_ok && info_ok,
                 "installed multicast U16 source and info ordering retained");
}

int main() {
  using ptx_frontend::base::RoundingMode;
  using ptx_frontend::base::ScalarType;

  static_assert(ScalarType::F16x2 != ScalarType::Invalid);
  static_assert(ScalarType::BF16x2 != ScalarType::Invalid);
  static_assert(ScalarType::U2 != ScalarType::Invalid);
  static_assert(ScalarType::S2 != ScalarType::Invalid);
  static_assert(ScalarType::U4 != ScalarType::Invalid);
  static_assert(ScalarType::S4 != ScalarType::Invalid);
  static_assert(ScalarType::E2m1x2 != ScalarType::Invalid);
  static_assert(ScalarType::E2m3x2 != ScalarType::Invalid);
  static_assert(ScalarType::E3m2x2 != ScalarType::Invalid);
  static_assert(ScalarType::E4m3x4 != ScalarType::Invalid);
  static_assert(ScalarType::E5m2x4 != ScalarType::Invalid);
  static_assert(ScalarType::E2m1x4 != ScalarType::Invalid);
  static_assert(ScalarType::E2m3x4 != ScalarType::Invalid);
  static_assert(ScalarType::E3m2x4 != ScalarType::Invalid);
  static_assert(ScalarType::UE8M0x2 != ScalarType::Invalid);
  static_assert(ScalarType::S2f6x2 != ScalarType::Invalid);
  static_assert(RoundingMode::Rni != RoundingMode::Invalid);
  static_assert(RoundingMode::Rmi != RoundingMode::Invalid);
  static_assert(RoundingMode::Rpi != RoundingMode::Invalid);
  static_assert(RoundingMode::Rna != RoundingMode::Invalid);
  static_assert(RoundingMode::Rs != RoundingMode::Invalid);

  if (const int result = runOwnedValidation(); result != 0)
    return result;
  if (!checkBarrierSyncContract())
    return 6;
  if (!checkMbarrierTestWaitContract())
    return 7;
  if (!checkMbarrierTryWaitContract())
    return 8;
  if (!checkMembarLevelsContract())
    return 9;
  if (!checkMembarProxyAliasContract())
    return 10;
  if (!checkMembarProxyAsyncContract())
    return 11;
  if (!checkOrdinaryFenceContract())
    return 12;
  if (!checkMbarrierInitFenceContract())
    return 13;
  if (!checkSharedSyncRestrictedFenceContract())
    return 14;
  if (!checkFenceProxyAliasContract())
    return 15;
  if (!checkCpAsyncContract())
    return 16;
  if (!checkBulkAsyncContract())
    return 17;
  if (!checkTensorAsyncContract())
    return 18;
  if (!checkTensorMapReplacementContract())
    return 19;
  if (!checkTensorReductionContract())
    return 20;
  if (!checkTensorNoOffsetsContract())
    return 21;
  if (!checkTensorIm2colInfoContract())
    return 22;
  if (!checkTensorGatherScatterContract())
    return 23;
  if (!checkTensorMulticastContract())
    return 24;
  std::cout << "conversion consumer passed\n";
  return 0;
}
