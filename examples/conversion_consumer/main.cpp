#include <algorithm>
#include <array>
#include <iostream>
#include <optional>
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
  // Source consistency may be diagnosed before the rule targeted by a mutation.
  const bool matched =
      !result &&
      std::ranges::any_of(result.error(), [expected](const auto& diagnostic) {
        return diagnostic.kind == expected;
      });
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
  const auto* ordinary = body[0].get();
  const auto* qualified = body[1].get();
  const auto* sync =
      ordinary ? dynamic_cast<const ir::BarrierSync*>(ordinary) : nullptr;
  const auto* cta_sync =
      qualified ? dynamic_cast<const ir::BarrierCtaSync*>(qualified) : nullptr;
  const auto* ordinary_arrive = body[2].get();
  const auto* qualified_arrive = body[3].get();
  const auto* arrive =
      ordinary_arrive ? dynamic_cast<const ir::BarrierArrive*>(ordinary_arrive)
                      : nullptr;
  const auto* cta_arrive =
      qualified_arrive
          ? dynamic_cast<const ir::BarrierCtaArrive*>(qualified_arrive)
          : nullptr;
  const auto* popc_instruction = body[4].get();
  const auto* popc =
      popc_instruction
          ? dynamic_cast<const ir::BarrierRedPopcU32*>(popc_instruction)
          : nullptr;
  const auto* and_instruction = body[5].get();
  const auto* and_reduction =
      and_instruction
          ? dynamic_cast<const ir::BarrierCtaRedAndPred*>(and_instruction)
          : nullptr;
  return require(
      sync && cta_sync && sync->aligned.value && !sync->aligned.locs.empty() &&
          !cta_sync->aligned.value && cta_sync->aligned.locs.empty() &&
          arrive && cta_arrive && !arrive->aligned.value &&
          arrive->aligned.locs.empty() && cta_arrive->aligned.value &&
          !cta_arrive->aligned.locs.empty() && popc && popc->aligned.value &&
          !popc->aligned.locs.empty() && popc->thread_count.has_value() &&
          popc->predicate.value.negated && and_reduction &&
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
  const auto* cta = body[0].get();
  const auto* gl = body[1].get();
  const auto* sys = body[2].get();
  return require(cta && gl && sys &&
                     dynamic_cast<const ir::MembarCta*>(cta) != nullptr &&
                     dynamic_cast<const ir::MembarGl*>(gl) != nullptr &&
                     dynamic_cast<const ir::MembarSys*>(sys) != nullptr &&
                     ir::MembarGl::scope == ir::MemoryScope::Gpu,
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
  const auto* instruction = body.front().get();
  return require(instruction && dynamic_cast<const ir::MembarProxyAlias*>(
                                    instruction) != nullptr,
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
  const auto* instruction = body.front().get();
  const auto* alias =
      instruction ? dynamic_cast<const ir::FenceProxyAlias*>(instruction)
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
    const auto* instruction = body[i].get();
    const auto* async =
        instruction ? dynamic_cast<const ir::MembarProxyAsync*>(instruction)
                    : nullptr;
    if (!require(async && async->proxy_kind.value == expected[i] &&
                     !async->proxy_kind.locs.empty(),
                 "public membar async-proxy space"))
      return false;
  }
  const auto* instruction = body[3].get();
  const auto* cluster =
      instruction
          ? dynamic_cast<const ir::MembarProxyAsyncSharedCluster*>(instruction)
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
  const auto* omitted_instruction = body[0].get();
  const auto* omitted =
      omitted_instruction
          ? dynamic_cast<const ir::FenceOrdinaryCta*>(omitted_instruction)
          : nullptr;
  if (!require(omitted &&
                   omitted->semantics.value == ir::MemoryConsistency::Omitted &&
                   omitted->semantics.locs.empty(),
               "public omitted ordinary fence semantics"))
    return false;
  for (size_t i : {1U, 2U}) {
    const auto* instruction = body[i].get();
    const auto* sc =
        instruction ? dynamic_cast<const ir::FenceOrdinaryCta*>(instruction)
                    : nullptr;
    if (!require(sc && sc->semantics.value == ir::MemoryConsistency::Sc &&
                     !sc->semantics.locs.empty(),
                 "public SC ordinary fence orders"))
      return false;
  }
  for (size_t i : {3U, 4U}) {
    const auto* instruction = body[i].get();
    if (!require(instruction && dynamic_cast<const ir::FenceAcqRelCta*>(
                                    instruction) != nullptr,
                 "public legacy acquire-release CTA variant"))
      return false;
  }
  const auto* gpu_instruction = body[5].get();
  const auto* gpu =
      gpu_instruction
          ? dynamic_cast<const ir::FenceOrdinaryGpuSys*>(gpu_instruction)
          : nullptr;
  const auto* cluster_instruction = body[6].get();
  const auto* cluster =
      cluster_instruction
          ? dynamic_cast<const ir::FenceOrdinaryCluster*>(cluster_instruction)
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
  const auto* instruction = body.front().get();
  const auto* restricted =
      instruction ? dynamic_cast<const ir::FenceMbarrierInitReleaseCluster*>(
                        instruction)
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
  const auto* acquire_instruction = body[0].get();
  const auto* release_instruction = body[1].get();
  const auto* acquire =
      acquire_instruction
          ? dynamic_cast<const ir::FenceAcquireSyncRestrictSharedCluster*>(
                acquire_instruction)
          : nullptr;
  const auto* release =
      release_instruction
          ? dynamic_cast<const ir::FenceReleaseSyncRestrictSharedCta*>(
                release_instruction)
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
  const auto* first_instruction = body[0].get();
  const auto* second_instruction = body[1].get();
  const auto* first =
      first_instruction
          ? dynamic_cast<
                const ir::MbarrierTestWaitTokenSemanticsGenericOrShared*>(
                first_instruction)
          : nullptr;
  const auto* second =
      second_instruction
          ? dynamic_cast<
                const ir::MbarrierTestWaitParityConditionalSemanticsSharedCta*>(
                second_instruction)
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
  const auto* first_instruction = body[0].get();
  const auto* second_instruction = body[1].get();
  const auto* first =
      first_instruction
          ? dynamic_cast<
                const ir::MbarrierTryWaitTokenSemanticsGenericOrShared*>(
                first_instruction)
          : nullptr;
  const auto* second =
      second_instruction
          ? dynamic_cast<
                const ir::MbarrierTryWaitParityConditionalSemanticsSharedCta*>(
                second_instruction)
          : nullptr;
  return require(
      first && second && first->time_hint.has_value() &&
          first->semantics.value == ir::MemoryConsistency::Acquire &&
          first->scope.value == ir::MemoryScope::Cta &&
          !first->semantics.locs.empty() && !first->scope.locs.empty() &&
          std::get<ir::ResolvedImmediate>(first->time_hint->value).bits ==
              16U &&
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

  auto* atom_instruction = body[27].get();
  auto* atom = atom_instruction
                   ? dynamic_cast<ir::AtomGlobalAddU32*>(atom_instruction)
                   : nullptr;
  auto* red_instruction = body[28].get();
  auto* red = red_instruction
                  ? dynamic_cast<ir::RedGlobalAddU32*>(red_instruction)
                  : nullptr;
  auto* legacy_cas_instruction = body[29].get();
  auto* legacy_cas =
      legacy_cas_instruction
          ? dynamic_cast<ir::AtomGlobalCasB32*>(legacy_cas_instruction)
          : nullptr;
  auto* modern_cas_instruction = body[30].get();
  auto* modern_cas =
      modern_cas_instruction
          ? dynamic_cast<ir::AtomGlobalCasB32*>(modern_cas_instruction)
          : nullptr;
  if (!require(
          atom && red && legacy_cas && modern_cas &&
              atom->dst.value.register_ref.has_value() &&
              legacy_cas->dst.value.register_ref.has_value() &&
              modern_cas->dst.value.register_ref.has_value() &&
              std::holds_alternative<ir::ResolvedRegisterRef>(
                  atom->src.value) &&
              std::holds_alternative<ir::ResolvedImmediate>(red->src.value) &&
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

  auto* inc_instruction = body[31].get();
  const auto* inc = inc_instruction
                        ? dynamic_cast<ir::AtomGlobalIncU32*>(inc_instruction)
                        : nullptr;
  auto* exch_instruction = body[32].get();
  const auto* exch =
      exch_instruction ? dynamic_cast<ir::AtomGlobalExchB32*>(exch_instruction)
                       : nullptr;
  auto* xor_instruction = body[33].get();
  const auto* xor_red =
      xor_instruction ? dynamic_cast<ir::RedGlobalXorB32*>(xor_instruction)
                      : nullptr;
  if (!require(
          inc && exch && xor_red &&
              std::holds_alternative<ir::ResolvedRegisterRef>(inc->src.value) &&
              std::holds_alternative<ir::ResolvedImmediate>(exch->src.value) &&
              std::holds_alternative<ir::ResolvedRegisterRef>(
                  xor_red->src.value),
          "owned expanded atomic and reduction variants"))
    return false;

  auto* add_64_instruction = body[34].get();
  const auto* add_64 =
      add_64_instruction
          ? dynamic_cast<ir::AtomGlobalAddU64*>(add_64_instruction)
          : nullptr;
  auto* min_64_instruction = body[35].get();
  const auto* min_64 =
      min_64_instruction
          ? dynamic_cast<ir::AtomGlobalMinS64*>(min_64_instruction)
          : nullptr;
  auto* cas_64_instruction = body[36].get();
  const auto* cas_64 =
      cas_64_instruction
          ? dynamic_cast<ir::AtomGlobalCasB64*>(cas_64_instruction)
          : nullptr;
  auto* red_64_instruction = body[37].get();
  const auto* red_64 =
      red_64_instruction
          ? dynamic_cast<ir::RedGlobalXorB64*>(red_64_instruction)
          : nullptr;
  if (!require(add_64 && min_64 && cas_64 && red_64 &&
                   std::holds_alternative<ir::ResolvedRegisterRef>(
                       add_64->src.value) &&
                   std::holds_alternative<ir::ResolvedImmediate>(
                       min_64->src.value) &&
                   std::holds_alternative<ir::ResolvedRegisterRef>(
                       cas_64->compare.value) &&
                   std::holds_alternative<ir::ResolvedImmediate>(
                       cas_64->swap.value) &&
                   std::holds_alternative<ir::ResolvedRegisterRef>(
                       red_64->src.value),
               "owned 64-bit atomic and reduction variants"))
    return false;

  const auto* float_atom = body[38].get();
  const auto* float_red = body[39].get();
  const auto* double_atom = body[40].get();
  const auto* double_red = body[41].get();
  if (!require(
          float_atom && float_red && double_atom && double_red &&
              dynamic_cast<const ir::AtomGlobalAddF32*>(float_atom) !=
                  nullptr &&
              dynamic_cast<const ir::RedGlobalAddF32*>(float_red) != nullptr &&
              dynamic_cast<const ir::AtomGlobalAddF64*>(double_atom) !=
                  nullptr &&
              dynamic_cast<const ir::RedGlobalAddF64*>(double_red) != nullptr,
          "owned float atomic and reduction variants"))
    return false;

  auto* vector_atom_instruction = body[42].get();
  const auto* vector_atom =
      vector_atom_instruction
          ? dynamic_cast<ir::AtomVectorAddNoftzF16*>(vector_atom_instruction)
          : nullptr;
  auto* vector_red_instruction = body[43].get();
  const auto* vector_red =
      vector_red_instruction
          ? dynamic_cast<ir::RedVectorAddNoftzF16*>(vector_red_instruction)
          : nullptr;
  if (!require(vector_atom && vector_red &&
                   vector_atom->vector.value == ir::VectorArity::V2 &&
                   vector_red->vector.value == ir::VectorArity::V2 &&
                   vector_atom->cache_hint.value &&
                   vector_red->cache_hint.value &&
                   !vector_atom->dst.value.elements.front().has_value() &&
                   vector_atom->cache_policy.has_value() &&
                   vector_atom->cache_policy->value.declared_type ==
                       ptx_frontend::base::ScalarType::B64,
               "owned vector atomic and reduction policy layout"))
    return false;

  const auto* shared_async =
      dynamic_cast<const ir::RedAsyncSharedAddU32*>(body[44].get());
  const auto* release_async =
      dynamic_cast<const ir::RedAsyncReleaseAddU64*>(body[45].get());
  if (!require(shared_async && release_async &&
                   shared_async->address_qualifier.value ==
                       ir::AtomicAddressQualifier::SharedCluster &&
                   release_async->address_qualifier.value ==
                       ir::AtomicAddressQualifier::Global &&
                   release_async->mmio.value,
               "owned asynchronous reduction modes"))
    return false;

  auto* testp = body[8].get();
  auto* property = testp ? dynamic_cast<ir::TestpF32*>(testp) : nullptr;
  if (!require(property && property->property.value ==
                               ptx_frontend::base::TestProperty::Normal,
               "typed testp.normal.f32 property"))
    return false;
  const auto* copysign = body[9].get();
  if (!require(
          copysign && dynamic_cast<const ir::CopysignF32*>(copysign) != nullptr,
          "typed copysign.f32 variant"))
    return false;
  const auto* sin = body[10].get();
  const auto* sin_f32 =
      sin ? dynamic_cast<const ir::SinApproxF32*>(sin) : nullptr;
  if (!require(sin_f32 && sin_f32->ftz.value,
               "typed transcendental approximation and FTZ"))
    return false;
  const auto* ex2 = body[11].get();
  if (!require(ex2 && dynamic_cast<const ir::Ex2ApproxFtzBf16*>(ex2) != nullptr,
               "typed BF16 transcendental variant"))
    return false;

  auto* min_binary_instruction = body[12].get();
  auto* min_ternary_instruction = body[13].get();
  auto* max_binary_instruction = body[14].get();
  auto* max_ternary_instruction = body[15].get();
  auto* min_binary = min_binary_instruction
                         ? dynamic_cast<ir::MinF32*>(min_binary_instruction)
                         : nullptr;
  auto* min_ternary = min_ternary_instruction
                          ? dynamic_cast<ir::MinF32*>(min_ternary_instruction)
                          : nullptr;
  auto* max_binary = max_binary_instruction
                         ? dynamic_cast<ir::MaxF32*>(max_binary_instruction)
                         : nullptr;
  auto* max_ternary = max_ternary_instruction
                          ? dynamic_cast<ir::MaxF32*>(max_ternary_instruction)
                          : nullptr;
  if (!require(
          min_binary && min_ternary && max_binary && max_ternary &&
              min_binary->operand_layout == ir::ResolvedOperandLayoutTag{0} &&
              min_ternary->operand_layout == ir::ResolvedOperandLayoutTag{1} &&
              max_binary->operand_layout == ir::ResolvedOperandLayoutTag{0} &&
              max_ternary->operand_layout == ir::ResolvedOperandLayoutTag{1} &&
              !min_binary->src3.has_value() && min_ternary->src3.has_value() &&
              !max_binary->src3.has_value() && max_ternary->src3.has_value() &&
              min_binary->ftz.value && min_binary->nan.value &&
              min_binary->xorsign_abs.value && !min_binary->abs.value &&
              min_ternary->ftz.value && min_ternary->nan.value &&
              !min_ternary->xorsign_abs.value && min_ternary->abs.value &&
              max_binary->xorsign_abs.value && !max_binary->abs.value &&
              !max_ternary->xorsign_abs.value && max_ternary->abs.value,
          "typed MIN/MAX modifier and operand layouts"))
    return false;

  auto* add_register_instruction = body[16].get();
  auto* add_immediate_instruction = body[17].get();
  auto* sub_register_instruction = body[18].get();
  auto* sub_immediate_instruction = body[19].get();
  auto* add_register =
      add_register_instruction
          ? dynamic_cast<ir::AddMixedF32*>(add_register_instruction)
          : nullptr;
  auto* add_immediate =
      add_immediate_instruction
          ? dynamic_cast<ir::AddMixedF32*>(add_immediate_instruction)
          : nullptr;
  auto* sub_register =
      sub_register_instruction
          ? dynamic_cast<ir::SubMixedF32*>(sub_register_instruction)
          : nullptr;
  auto* sub_immediate =
      sub_immediate_instruction
          ? dynamic_cast<ir::SubMixedF32*>(sub_immediate_instruction)
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

  auto* set_instruction = body[20].get();
  auto* set_float = set_instruction
                        ? dynamic_cast<ir::SetFloatBoolean*>(set_instruction)
                        : nullptr;
  auto* selp_scalar_instruction = body[21].get();
  auto* selp_scalar =
      selp_scalar_instruction
          ? dynamic_cast<ir::SelpScalar*>(selp_scalar_instruction)
          : nullptr;
  auto* selp_u32_instruction = body[22].get();
  auto* selp_u32 = selp_u32_instruction
                       ? dynamic_cast<ir::SelpU32*>(selp_u32_instruction)
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

  auto* set_half_instruction = body[23].get();
  auto* set_half =
      set_half_instruction
          ? dynamic_cast<ir::SetHalfNativeF16x2Boolean*>(set_half_instruction)
          : nullptr;
  auto* set_bfloat_instruction = body[24].get();
  auto* set_bfloat =
      set_bfloat_instruction
          ? dynamic_cast<ir::SetHalfBf16F16*>(set_bfloat_instruction)
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

  auto* slct_integer_instruction = body[25].get();
  auto* slct_integer =
      slct_integer_instruction
          ? dynamic_cast<ir::SlctS32*>(slct_integer_instruction)
          : nullptr;
  auto* slct_floating_instruction = body[26].get();
  auto* slct_floating =
      slct_floating_instruction
          ? dynamic_cast<ir::SlctF32*>(slct_floating_instruction)
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

  const auto original_binary_abs = min_binary->abs;
  min_binary->abs.value = true;
  const bool inconsistent_binary_modifier = rejectsMutation(
      module, ir::checker::CheckDiagnosticKind::ModuleSourceMismatch,
      "binary MIN rejects an optional modifier without a source location");
  min_binary->abs = original_binary_abs;
  if (!inconsistent_binary_modifier)
    return false;

  min_binary->abs.value = true;
  min_binary->abs.locs = min_binary->ftz.locs;
  const bool forbidden_binary_modifier = rejectsMutation(
      module, ir::checker::CheckDiagnosticKind::ModifierNotAllowedForLayout,
      "binary MIN forbids ternary abs modifier");
  min_binary->abs = original_binary_abs;
  if (!forbidden_binary_modifier)
    return false;

  const auto original_ternary_xorsign_abs = max_ternary->xorsign_abs;
  max_ternary->xorsign_abs.value = true;
  max_ternary->xorsign_abs.locs = max_ternary->abs.locs;
  const bool forbidden_ternary_modifier = rejectsMutation(
      module, ir::checker::CheckDiagnosticKind::ModifierNotAllowedForLayout,
      "ternary MAX forbids binary xorsign modifier");
  max_ternary->xorsign_abs = original_ternary_xorsign_abs;
  if (!forbidden_ternary_modifier)
    return false;
  const auto original_src3 = min_ternary->src3;
  min_ternary->src3.reset();
  const bool mismatched_layout = rejectsMutation(
      module, ir::checker::CheckDiagnosticKind::OperandLayoutPayloadMismatch,
      "MIN rejects a layout tag/payload mismatch");
  min_ternary->src3 = original_src3;
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
  const auto* original_instruction = body[0].get();
  const auto* original =
      original_instruction
          ? dynamic_cast<const ir::CpAsyncCaSharedGlobal*>(original_instruction)
          : nullptr;
  if (!require(original && !original->dst.locs.empty() &&
                   !original->src.locs.empty() &&
                   original->cp_size.value.bits == 4,
               "original three-operand public copy fields remain available"))
    return false;
  const auto* policy_instruction = body[1].get();
  const auto* policy =
      policy_instruction
          ? dynamic_cast<const ir::CpAsyncCaSharedGlobalCacheHintControl*>(
                policy_instruction)
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
  const auto* copy = body[0].get();
  const auto* mbar =
      copy ? dynamic_cast<const ir::CpAsyncBulkGlobalSharedCta*>(copy)
           : nullptr;
  const auto* group = body[1].get();
  const auto* bulk_group =
      group ? dynamic_cast<const ir::CpAsyncBulkSharedCtaGlobal*>(group)
            : nullptr;
  const auto* commit =
      dynamic_cast<const ir::CpAsyncBulkCommitGroup*>(body[2].get());
  const auto* wait =
      dynamic_cast<const ir::CpAsyncBulkWaitGroup*>(body[3].get());
  const auto* zero_fill = dynamic_cast<const ir::StBulkZero*>(body[4].get());
  return require(mbar && bulk_group && commit && wait && zero_fill &&
                     ir::CpAsyncBulkGlobalSharedCta::completion_kind ==
                         ptx_frontend::base::AsyncCompletionKind::
                             MbarrierCompleteTxBytes &&
                     ir::CpAsyncBulkSharedCtaGlobal::completion_kind ==
                         ptx_frontend::base::AsyncCompletionKind::BulkGroup &&
                     wait->read.value,
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
  const auto* prefetch =
      dynamic_cast<ir::CpAsyncBulkPrefetchTensor2d*>(body[0].get());
  const auto* load =
      dynamic_cast<ir::CpAsyncBulkTensor2dSharedCluster*>(body[1].get());
  const auto* store =
      dynamic_cast<ir::CpAsyncBulkTensor1dGlobalSharedCta*>(body[2].get());
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
          ir::CpAsyncBulkTensor2dSharedCluster::completion_kind ==
              ptx_frontend::base::AsyncCompletionKind::
                  MbarrierCompleteTxBytes &&
          ir::CpAsyncBulkTensor1dGlobalSharedCta::completion_kind ==
              ptx_frontend::base::AsyncCompletionKind::BulkGroup,
      "installed tensor map, rank, tile, and completion identities");
}
}  // namespace

/** Check the installed public conversion, comparison, and resolved-IR contract. */
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
  std::cout << "conversion consumer passed\n";
  return 0;
}
