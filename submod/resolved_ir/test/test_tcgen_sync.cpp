#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Wrap synchronization instructions in a declaration-bound complete module. */
std::string sync_source(std::string_view body, std::string_view ptx = "9.0",
                        std::string_view target = "sm_110a") {
  return ".version " + std::string(ptx) + "\n.target " + std::string(target) +
         R"ptx(
.address_size 64
.visible .entry kernel() {
  .shared .align 8 .b64 barrier[2];
  .shared .align 4 .b32 misaligned;
  .global .align 8 .b64 global_barrier;
  .reg .b64 %ptr;
  .reg .b32 %t, %r0;
  .reg .b16 %bmask;
  .reg .u16 %umask;
  .reg .s16 %smask;
  .reg .f16 %fmask;
  .reg .b32 %wide;
  .reg .pred %pred;
)ptx" + std::string(body) +
         "\n  ret;\n}\n";
}

/** Parse a full module, reporting the first parser diagnostic if requested. */
std::optional<syntax_ast::AstModule> parse_sync(std::string_view source,
                                                bool report = true) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty()) {
    if (report)
      ADD_FAILURE() << (ast && !ast.diagnostics.empty()
                            ? ast.diagnostics.front().message
                            : "TCGEN synchronization module did not parse.");
    return std::nullopt;
  }
  return std::move(*ast);
}

/** Resolve and validate a full module under its written target. */
bool accepts_sync(std::string_view source) {
  auto ast = parse_sync(source, false);
  return ast && resolveAndValidateModule(*ast).has_value();
}

/** Supply the exact qualified target for an AST-free direct checker call. */
checker::Context sync_context(const ResolvedModule& module) {
  const auto profile = base::find_target_profile("sm_110a");
  EXPECT_TRUE(profile.has_value());
  return checker::Context{
      .target = {.ptx_version = {9, 0},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities},
      .instruction_range = module.functions.front().instruction_ranges[0],
  };
}

/** All eight commit modifier layouts and both fences retain their identities. */
TEST(TcgenSync, AcceptsWrittenLayoutsAndTypedProtocol) {
  for (unsigned group : {1u, 2u}) {
    for (bool shared : {false, true}) {
      for (bool multicast : {false, true}) {
        const std::string commit =
            "tcgen05.commit.cta_group::" + std::to_string(group) +
            ".mbarrier::arrive::one" + (shared ? ".shared::cluster" : "") +
            (multicast ? ".multicast::cluster" : "") + ".b64 [barrier]" +
            (multicast ? ", %bmask" : "") + ";";
        SCOPED_TRACE(commit);
        EXPECT_TRUE(accepts_sync(sync_source(commit)));
      }
    }
  }
  for (std::string_view mask : {"%bmask", "%umask", "%smask"}) {
    EXPECT_TRUE(accepts_sync(
        sync_source("tcgen05.commit.cta_group::1.mbarrier::arrive::one"
                    ".multicast::cluster.b64 [barrier], " +
                    std::string(mask) + ";")));
  }
  std::optional<ResolvedModule> owned;
  {
    auto ast = parse_sync(
        sync_source("tcgen05.fence::before_thread_sync;\n"
                    "tcgen05.commit.cta_group::1.mbarrier::arrive::one"
                    ".multicast::cluster.b64 [barrier], %bmask;\n"
                    "tcgen05.fence::after_thread_sync;"));
    ASSERT_TRUE(ast);
    auto resolved = resolveAndValidateModule(*ast);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned = std::move(*resolved);
  }
  ASSERT_TRUE(owned);
  auto* first = owned->functions.front().body[0].get();
  auto* middle = owned->functions.front().body[1].get();
  auto* last = owned->functions.front().body[2].get();
  ASSERT_NE(first, nullptr);
  ASSERT_NE(middle, nullptr);
  ASSERT_NE(last, nullptr);
  const auto* before = dynamic_cast<const Tcgen05FenceBeforeThreadSync*>(first);
  const auto* after = dynamic_cast<const Tcgen05FenceAfterThreadSync*>(last);
  const auto* commit =
      dynamic_cast<const Tcgen05CommitGroup1GenericMulticast*>(middle);
  ASSERT_NE(before, nullptr);
  ASSERT_NE(after, nullptr);
  ASSERT_NE(commit, nullptr);
  EXPECT_EQ(before->direction, TcgenFenceDirection::BeforeThreadSync);
  EXPECT_EQ(after->direction, TcgenFenceDirection::AfterThreadSync);
  EXPECT_EQ(commit->completion_kind,
            base::AsyncCompletionKind::TcgenMbarrierArriveOne);
  EXPECT_EQ(commit->signal_scope, base::MemoryScope::Cluster);
  EXPECT_EQ(commit->arrive_count, 1);
  EXPECT_TRUE(commit->generic_proxy_access);
  EXPECT_TRUE(commit->multicast);
  EXPECT_EQ(commit->address_spelling, TcgenCommitAddressSpelling::Generic);
  EXPECT_TRUE(validateModule(*owned).has_value());
}

/** Operand source kinds, known address roles, and alignment remain bounded. */
TEST(TcgenSync, RejectsWrongSourcesAndMalformedArity) {
  for (std::string_view mask :
       {"0", "1", "65535", "-1", "%fmask", "%wide", "%pred"}) {
    SCOPED_TRACE(mask);
    EXPECT_FALSE(accepts_sync(
        sync_source("tcgen05.commit.cta_group::1.mbarrier::arrive::one"
                    ".multicast::cluster.b64 [barrier], " +
                    std::string(mask) + ";")));
  }
  for (std::string_view instruction : {
           "tcgen05.commit.cta_group::1.mbarrier::arrive::one"
           ".multicast::cluster.b64 [barrier];",
           "tcgen05.commit.cta_group::1.mbarrier::arrive::one.b64 "
           "[barrier], %bmask;",
           "tcgen05.commit.cta_group::1.mbarrier::arrive::one.b64 "
           "[misaligned];",
           "tcgen05.commit.cta_group::1.mbarrier::arrive::one.b64 "
           "[barrier+4];",
           "tcgen05.commit.cta_group::1.mbarrier::arrive::one.b64 "
           "[global_barrier];",
           "tcgen05.commit.cta_group::3.mbarrier::arrive::one.b64 "
           "[barrier];",
           "tcgen05.fence::before_thread_sync.b32 %wide;",
       }) {
    SCOPED_TRACE(instruction);
    EXPECT_FALSE(accepts_sync(sync_source(instruction)));
  }
  EXPECT_TRUE(accepts_sync(
      sync_source("tcgen05.commit.cta_group::1.mbarrier::arrive::one.b64 "
                  "[barrier+8];")));
}

/** Target-qualified floors remain separate from generic target numbers. */
TEST(TcgenSync, ChecksQualifiedTargets) {
  const std::string commit =
      "tcgen05.commit.cta_group::1.mbarrier::arrive::one.b64 [barrier];";
  for (const auto& [ptx, target, accepted] : {
           std::tuple{"8.6", "sm_100a", true},
           std::tuple{"8.8", "sm_100f", true},
           std::tuple{"8.8", "sm_103a", true},
           std::tuple{"8.8", "sm_103f", true},
           std::tuple{"9.0", "sm_110a", true},
           std::tuple{"9.0", "sm_110f", true},
           std::tuple{"9.3", "sm_100", false},
           std::tuple{"9.3", "sm_110", false},
           std::tuple{"9.3", "sm_120a", false},
       }) {
    SCOPED_TRACE(std::string(ptx) + "/" + target);
    EXPECT_EQ(accepts_sync(sync_source(commit, ptx, target)), accepted);
    EXPECT_EQ(accepts_sync(sync_source("tcgen05.fence::before_thread_sync;",
                                       ptx, target)),
              accepted);
    EXPECT_EQ(accepts_sync(sync_source("tcgen05.fence::after_thread_sync;", ptx,
                                       target)),
              accepted);
  }
}

/** Group consistency is local to a function body and fences are group-neutral. */
TEST(TcgenSync, ChecksGroupsPerBody) {
  EXPECT_FALSE(accepts_sync(sync_source(
      "tcgen05.commit.cta_group::1.mbarrier::arrive::one.b64 [barrier];\n"
      "tcgen05.fence::after_thread_sync;\n"
      "tcgen05.commit.cta_group::2.mbarrier::arrive::one.b64 [barrier];")));
  EXPECT_FALSE(accepts_sync(sync_source(
      "tcgen05.commit.cta_group::1.mbarrier::arrive::one.b64 [barrier];\n"
      "tcgen05.alloc.cta_group::2.sync.aligned.shared::cta.b32 "
      "[misaligned], 32;")));
  EXPECT_FALSE(accepts_sync(sync_source(
      "tcgen05.commit.cta_group::1.mbarrier::arrive::one.b64 [barrier];\n"
      "tcgen05.ld.sync.aligned.32x32b.x1.b32 {%r0}, [%t];\n"
      "tcgen05.alloc.cta_group::2.sync.aligned.shared::cta.b32 "
      "[misaligned], 32;")));
  EXPECT_TRUE(accepts_sync(sync_source(
      "tcgen05.commit.cta_group::1.mbarrier::arrive::one.b64 [barrier];\n"
      "tcgen05.ld.sync.aligned.32x32b.x1.b32 {%r0}, [%t];\n"
      "tcgen05.wait::ld.sync.aligned;\n"
      "tcgen05.alloc.cta_group::1.sync.aligned.shared::cta.b32 "
      "[misaligned], 32;")));
  EXPECT_TRUE(accepts_sync(sync_source(
      "tcgen05.commit.cta_group::1.mbarrier::arrive::one.b64 [barrier];\n"
      "tcgen05.wait::ld.sync.aligned;\n"
      "tcgen05.fence::after_thread_sync;")));
  std::string separate = sync_source(
      "tcgen05.commit.cta_group::1.mbarrier::arrive::one.b64 [barrier];");
  separate += R"ptx(
.visible .entry other() {
  .shared .align 8 .b64 second;
  tcgen05.commit.cta_group::2.mbarrier::arrive::one.b64 [second];
  ret;
}
)ptx";
  EXPECT_TRUE(accepts_sync(separate));
}

/** Direct and module checking survive AST destruction and catch owned damage. */
TEST(TcgenSync, RejectsOwnedPointerMaskAndTagMutation) {
  std::optional<ResolvedModule> owned;
  {
    auto ast = parse_sync(
        sync_source("tcgen05.commit.cta_group::1.mbarrier::arrive::one"
                    ".multicast::cluster.b64 [%ptr], %bmask;"));
    ASSERT_TRUE(ast);
    auto resolved = resolveAndValidateModule(*ast);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned = std::move(*resolved);
  }
  ASSERT_TRUE(owned);
  auto& instruction = *owned->functions.front().body[0].get();
  auto& form = dynamic_cast<Tcgen05CommitGroup1GenericMulticast&>(instruction);
  auto& pointer = std::get<ResolvedRegisterRef>(form.mbar.value.base);
  auto& mask = form.cta_mask.value;
  const auto context = sync_context(*owned);
  const auto reject = [&]() {
    EXPECT_FALSE(instruction.check(context).has_value());
    EXPECT_FALSE(validateModule(*owned).has_value());
  };
  ASSERT_TRUE(instruction.check(context).has_value());
  ASSERT_TRUE(validateModule(*owned).has_value());
  pointer.vector_width = 2;
  reject();
  pointer.vector_width.reset();
  pointer.register_class = ResolvedRegisterClass::Predicate;
  reject();
  pointer.register_class = ResolvedRegisterClass::General;
  const auto pointer_type = pointer.declared_type;
  for (const auto type : {base::ScalarType::F32, base::ScalarType::B16}) {
    pointer.declared_type = type;
    reject();
  }
  pointer.declared_type = pointer_type;
  const auto pointer_binding = pointer.symbol_id;
  pointer.declared_type.reset();
  reject();
  pointer.symbol_id.reset();
  EXPECT_TRUE(instruction.check(context).has_value());
  pointer.symbol_id = pointer_binding;
  pointer.declared_type = pointer_type;
  pointer.symbol_id = binding::SymbolId{.value = 999998u};
  EXPECT_FALSE(validateModule(*owned).has_value());
  pointer.symbol_id = pointer_binding;
  mask.vector_width = 2;
  reject();
  mask.vector_width.reset();
  mask.register_class = ResolvedRegisterClass::Predicate;
  reject();
  mask.register_class = ResolvedRegisterClass::General;
  const auto mask_type = mask.declared_type;
  for (const auto type :
       {base::ScalarType::F16, base::ScalarType::B32, base::ScalarType::B64}) {
    mask.declared_type = type;
    reject();
  }
  mask.declared_type.reset();
  reject();
  mask.declared_type = mask_type;
  const auto mask_binding = mask.symbol_id;
  mask.declared_type.reset();
  mask.symbol_id.reset();
  EXPECT_TRUE(instruction.check(context).has_value());
  mask.symbol_id = mask_binding;
  mask.declared_type = mask_type;
  mask.symbol_id = binding::SymbolId{.value = 999999u};
  EXPECT_FALSE(validateModule(*owned).has_value());
  mask.symbol_id = mask_binding;
  form.multicast_cluster.value = false;
  reject();
  form.multicast_cluster.value = true;
  form.cta_group.value = TcgenCtaGroup::Two;
  reject();
  form.cta_group.value = TcgenCtaGroup::One;
  ++form.operand_layout.value;
  reject();
  --form.operand_layout.value;
  EXPECT_TRUE(instruction.check(context).has_value());
  EXPECT_TRUE(validateModule(*owned).has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
