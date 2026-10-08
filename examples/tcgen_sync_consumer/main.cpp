#include <iostream>
#include <optional>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace {

/** Report a failed installed-package contract even in optimized builds. */
bool require(bool result, std::string_view contract) {
  if (!result)
    std::cerr << "TCGEN synchronization consumer: " << contract << '\n';
  return result;
}

}  // namespace

/** Validate stable generated descriptors and owned data after AST release. */
int main() {
  namespace ir = ptx_frontend::resolved_ir;
  constexpr std::string_view source = R"ptx(
.version 9.0
.target sm_110a
.address_size 64
.visible .entry kernel() {
  .shared .align 8 .b64 barrier;
  .reg .b16 %mask;
  tcgen05.fence::before_thread_sync;
  tcgen05.commit.cta_group::1.mbarrier::arrive::one.multicast::cluster.b64 [barrier], %mask;
  tcgen05.fence::after_thread_sync;
  ret;
}
)ptx";
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!require(ast.has_value() && ast.diagnostics.empty(), "source parses"))
      return 1;
    auto resolved = ir::resolveAndValidateModule(*ast);
    if (!require(resolved.has_value(), "source resolves"))
      return 1;
    owned.emplace(std::move(*resolved));
  }
  if (!require(owned->functions.size() == 1 &&
                   owned->functions.front().body.size() == 4,
               "all instructions survive AST release"))
    return 1;
  auto* commit = owned->functions.front().body[1].get();
  if (!require(commit != nullptr, "commit has the shared TCGEN root"))
    return 1;
  const auto* form =
      dynamic_cast<ir::Tcgen05CommitGroup1GenericMulticast*>(commit);
  if (!require(
          form != nullptr && form->multicast && form->arrive_count == 1 &&
              form->completion_kind == ptx_frontend::base::AsyncCompletionKind::
                                           TcgenMbarrierArriveOne &&
              form->signal_scope == ptx_frontend::base::MemoryScope::Cluster &&
              form->address_spelling == ir::TcgenCommitAddressSpelling::Generic,
          "typed completion protocol is stable"))
    return 1;
  const auto& descriptor = ir::tcgen05_resolved_descriptor();
  if (!require(descriptor.variants.size() >= 10,
               "installed descriptor exposes all synchronization forms"))
    return 1;
  return require(ir::validateModule(*owned).has_value(),
                 "AST-independent module validation succeeds")
             ? 0
             : 1;
}
