#include <gtest/gtest.h>
#include <algorithm>
#include <ptx_frontend/resolved_ir/model/data_movement/cp.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/ld.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/mov.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/st.gen.hpp>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/atom.gen.hpp>
#include <ptx_frontend/resolved_ir/model/tensor_memory/tcgen05.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include <string>
#include <vector>
namespace ptx_frontend::resolved_ir {
namespace {
constexpr std::string_view atom_payload =
    ".global .u32 g;\n.entry k(.param .u32 x){.reg .u32 "
    "%r,%old;atom.global.add.u32 %old,[g],%r;ld.param.u32 %r,[x];ret;}";
/** Parse owned syntax for one exact source fixture. */
syntax_ast::AstModule parse_intrinsic(std::string_view text) {
  PtxSyntaxParser parser(text);
  auto ast = parser.parseModule();
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  return ast ? std::move(*ast) : syntax_ast::AstModule{};
}
/** Construct legal current data and return it after parser/input-buffer destruction. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> intrinsic_module(
    std::string text) {
  auto ast = parse_intrinsic(text);
  return resolveAndValidateModule(ast);
}
/** Erase only retained version/target facts; binding/source identity remains unchanged. */
void remove_context(ResolvedModule& module) {
  for (auto& region : module.header.regions) {
    region.version.reset();
    region.version_provenance = SourceConfigurationProvenance::Missing;
    region.target_options.clear();
    region.target_provenance = SourceConfigurationProvenance::Missing;
  }
  for (auto& function : module.functions) {
    function.source_version.reset();
    function.source_target.reset();
  }
}
/** Require both current-owned and supplied-missing-context APIs to report a cause. */
void expect_intrinsic(const ResolvedModule& module,
                      const syntax_ast::AstModule& ast,
                      std::optional<checker::CheckDiagnosticKind> kind,
                      std::string_view message = {}) {
  for (const auto& result :
       {validateModule(module, ModuleValidationPolicy::AvailableContext),
        validateModule(ast, module,
                       ModuleValidationPolicy::AvailableContext)}) {
    if (!kind) {
      EXPECT_TRUE(result) << (result ? "" : result.error().front().message);
      continue;
    }
    ASSERT_FALSE(result);
    EXPECT_TRUE(std::ranges::any_of(result.error(), [&](const auto& d) {
      return d.kind == *kind && (message.empty() || d.message == message);
    })) << result.error().front().message;
  }
}
/** Existing Header input redirect rejects before either missing-context skip. */
TEST(ParameterAddressIntrinsics, HeaderAtomCurrentAndProvidedMissingContext) {
  auto module = intrinsic_module(".version 9.3\n.target sm_80\n" +
                                 std::string(atom_payload));
  ASSERT_TRUE(module);
  auto missing = parse_intrinsic(atom_payload);
  remove_context(*module);
  expect_intrinsic(*module, missing, std::nullopt);
  auto& atom =
      dynamic_cast<AtomGlobalAddU32&>(*module->functions.front().body[0]);
  const auto& load =
      dynamic_cast<const LdExplicitScalar&>(*module->functions.front().body[1]);
  atom.address.value.base =
      std::get<ResolvedSymbolRef>(load.address.value.base);
  expect_intrinsic(*module, missing,
                   checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  auto copy = *module;
  ResolvedModule assigned;
  assigned = copy;
  assigned.functions.front().body[0] =
      assigned.functions.front().body[0]->clone();
  expect_intrinsic(assigned, missing,
                   checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
}
/** Missing/out-of-range/current-scope facts cannot become standalone unknown addresses. */
TEST(ParameterAddressIntrinsics, RejoinsCurrentIdentityBeforeUnknownFallback) {
  const auto good = intrinsic_module(".version 9.3\n.target sm_80\n" +
                                     std::string(atom_payload));
  ASSERT_TRUE(good);
  auto missing = parse_intrinsic(atom_payload);
  for (const bool absent : {false, true}) {
    auto module = *good;
    remove_context(module);
    auto& address =
        dynamic_cast<AtomGlobalAddU32&>(*module.functions.front().body[0])
            .address.value;
    auto& symbol = std::get<ResolvedSymbolRef>(address.base);
    symbol.symbol_id =
        absent ? std::nullopt : std::optional{binding::SymbolId{0xffffffff}};
    expect_intrinsic(module, missing,
                     checker::CheckDiagnosticKind::ModuleSourceMismatch);
  }
}
/** Exact current associated modifier equality applies without target facts. */
TEST(ParameterAddressIntrinsics, CurrentSelectedSpaceAndReturnConstraint) {
  const std::string payload =
      ".entry k(.param .u32 x){.reg .u32 %r;ld.param.u32 %r,[x];ret;}";
  auto module = intrinsic_module(".version 9.3\n.target sm_80\n" + payload);
  ASSERT_TRUE(module);
  auto missing = parse_intrinsic(payload);
  remove_context(*module);
  expect_intrinsic(*module, missing, std::nullopt);
  auto& load =
      dynamic_cast<LdExplicitScalar&>(*module->functions.front().body[0]);
  load.state_space.value = MemoryStateSpace::Global;
  expect_intrinsic(*module, missing,
                   checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  const std::string staging =
      ".extern .func (.param .b32 result) callee(.param .b32 arg);"
      ".entry k(.param .b32 x){.reg .b32 %r;.param .b32 a,b;"
      "st.param.b32 [a],%r;call (b),callee,(a);ld.param.b32 %r,[b];"
      "ld.param.b32 %r,[x];ret;}";
  auto stored = intrinsic_module(".version 9.3\n.target sm_80\n" + staging);
  ASSERT_TRUE(stored);
  auto original = parse_intrinsic(staging);
  remove_context(*stored);
  expect_intrinsic(*stored, original, std::nullopt);
  auto& store =
      dynamic_cast<StExplicitScalar&>(*stored->functions.back().body[0]);
  const auto& input =
      dynamic_cast<const LdExplicitScalar&>(*stored->functions.back().body[3]);
  store.address.value.base =
      std::get<ResolvedSymbolRef>(input.address.value.base);
  expect_intrinsic(*stored, original,
                   checker::CheckDiagnosticKind::RuleViolation,
                   "Entry input .param storage is read-only for this operand.");
}
/** Copy/bulk domains use their existing metadata, independently of LD/ST. */
TEST(ParameterAddressIntrinsics, BulkDomainAndWritableAllocException) {
  const std::string bulk =
      ".global .align 16 .b8 g[16];.entry k(.param .u32 x){.shared .align 16 "
      ".b8 s[16];.reg .u32 %r;cp.async.bulk.global.shared::cta.bulk_group "
      "[g],[s],16;ld.param.u32 %r,[x];ret;}";
  auto module = intrinsic_module(".version 9.3\n.target sm_90\n" + bulk);
  ASSERT_TRUE(module);
  auto missing = parse_intrinsic(bulk);
  remove_context(*module);
  expect_intrinsic(*module, missing, std::nullopt);
  auto& cp = dynamic_cast<CpAsyncBulkSharedCtaGlobal&>(
      *module->functions.front().body[0]);
  const auto& load =
      dynamic_cast<const LdExplicitScalar&>(*module->functions.front().body[1]);
  cp.src.value.base = std::get<ResolvedSymbolRef>(load.address.value.base);
  expect_intrinsic(*module, missing,
                   checker::CheckDiagnosticKind::AddressStateSpaceMismatch);
  const std::string alloc =
      ".entry k(.param .u32 x){.shared .align 4 .b32 slot;.reg .b32 %t;.reg "
      ".u32 %r;tcgen05.alloc.cta_group::1.sync.aligned.b32 "
      "[slot],32;tcgen05.dealloc.cta_group::1.sync.aligned.b32 "
      "%t,32;ld.param.u32 %r,[x];ret;}";
  auto allocated = intrinsic_module(".version 9.3\n.target sm_100a\n" + alloc);
  ASSERT_TRUE(allocated);
  auto no_target = parse_intrinsic(alloc);
  remove_context(*allocated);
  expect_intrinsic(*allocated, no_target, std::nullopt);
  auto& target =
      dynamic_cast<Tcgen05AllocGeneric&>(*allocated->functions.front().body[0]);
  const auto& parameter = dynamic_cast<const LdExplicitScalar&>(
      *allocated->functions.front().body[2]);
  target.dst.value.base =
      std::get<ResolvedSymbolRef>(parameter.address.value.base);
  expect_intrinsic(*allocated, no_target,
                   checker::CheckDiagnosticKind::RuleViolation);
}
/** Existing payload-only observers remain source compatible and receive the same callbacks. */
struct OldAddressObserver : detail::IReferenceObserver {
  /** Ordered address callbacks; copied count owns no payload reference. */
  unsigned addresses{};
  /** Observe the old signature without implementing the new optional hook. */
  void address(const ResolvedAddress&, std::span<const SourceRange>,
               checker::AddressSymbolResolutionPolicy) override {
    ++addresses;
  }
};
/** Copy required scalar/domain facts during each synchronous metadata bracket. */
struct AddressContextObserver final : OldAddressObserver {
  /** Owned observed domain snapshots, safe after visits and instruction destruction. */
  std::vector<std::vector<MemoryStateSpace>> domains;
  /** Active bracket state, never a retained descriptor pointer. */
  bool active{};
  /** Copied value-access facts for the observed top-level operands. */
  std::vector<checker::OperandAccess> accesses;
  /** Current copied association. */
  std::optional<MemoryStateSpace> selected;
  /** Copy values synchronously and enforce balanced nonnull/reset calls. */
  void operand_context(const checker::OperandDescriptor* descriptor,
                       std::optional<MemoryStateSpace> space) override {
    if (!descriptor) {
      active = false;
      selected.reset();
      return;
    }
    EXPECT_FALSE(active);
    active = true;
    selected = space;
    accesses.push_back(descriptor->access);
    domains.emplace_back();
    for (const auto& domain : descriptor->allowed_address_state_spaces)
      domains.back().push_back(domain.state_space);
  }
  /** Address callbacks must see the exact active metadata bracket. */
  void address(const ResolvedAddress& value,
               std::span<const SourceRange> locations,
               checker::AddressSymbolResolutionPolicy policy) override {
    EXPECT_TRUE(active);
    OldAddressObserver::address(value, locations, policy);
  }
  /** A tensor-map descriptor remains a borrowed read within its outer tensor operand. */
  void tensor_operand(const ResolvedTensorOperand&,
                      std::span<const SourceRange>,
                      checker::AddressSymbolResolutionPolicy) override {
    EXPECT_TRUE(active);
    ASSERT_FALSE(accesses.empty());
    EXPECT_EQ(accesses.back(), checker::OperandAccess::Read);
  }
  /** Pure register children and unrelated operands must not inherit context. */
  void reg(const ResolvedRegisterRef&, std::span<const SourceRange>,
           checker::AddressSymbolResolutionPolicy) override {
    EXPECT_FALSE(active);
  }
  /** Execution predicates are outside all operand brackets. */
  void predicate(const ResolvedPredicate&, std::span<const SourceRange>,
                 checker::AddressSymbolResolutionPolicy) override {
    EXPECT_FALSE(active);
  }
};
/** Current selected atomic layouts forward different existing closed domains. */
TEST(ParameterAddressIntrinsics, ObserverCompatibilityLayoutAndReset) {
  auto module = intrinsic_module(".version 9.3\n.target sm_80\n" +
                                 std::string(atom_payload));
  ASSERT_TRUE(module);
  auto& atom =
      dynamic_cast<AtomGlobalAddU32&>(*module->functions.front().body[0]);
  atom.execution_predicate = WithLocs<ResolvedPredicate>{};
  OldAddressObserver old;
  atom.visit_references(old);
  EXPECT_EQ(old.addresses, 1u);
  AddressContextObserver observer;
  atom.visit_references(observer);
  ASSERT_EQ(observer.domains.size(), 1u);
  EXPECT_EQ(observer.domains[0],
            (std::vector{MemoryStateSpace::Global, MemoryStateSpace::Shared}));
  EXPECT_FALSE(observer.active);
  atom.operand_layout.value = 1;
  atom.cache_policy = WithLocs<ResolvedRegisterRef>{};
  atom.visit_references(observer);
  ASSERT_EQ(observer.domains.size(), 2u);
  EXPECT_EQ(observer.domains[1], (std::vector{MemoryStateSpace::Global}));
  EXPECT_FALSE(observer.active);
}
/** Header tensor-map descriptors remain legal reads even for a tensor store. */
TEST(ParameterAddressIntrinsics, HeaderTensorMapStoreDescriptorRead) {
  const std::string payload =
      ".entry k(.param .align 64 .b8 tensor_map[128]){.shared .align 64 .b8 "
      "tile_data[1024];cp.async.bulk.tensor.1d.global.shared::cta.bulk_group "
      "[tensor_map,{0}],[tile_data];ret;}";
  auto module = intrinsic_module(".version 9.3\n.target sm_90\n" + payload);
  ASSERT_TRUE(module) << module.error().front().message;
  AddressContextObserver observer;
  module->functions.front().body[0]->visit_references(observer);
  ASSERT_EQ(observer.domains.size(), 2u);
  EXPECT_EQ(observer.accesses[0], checker::OperandAccess::Read);
  EXPECT_EQ(observer.domains[0], (std::vector{MemoryStateSpace::Parameter,
                                              MemoryStateSpace::Constant,
                                              MemoryStateSpace::Global}));
  EXPECT_FALSE(observer.active);
  auto missing = parse_intrinsic(payload);
  remove_context(*module);
  expect_intrinsic(*module, missing, std::nullopt);
}
/** Ordinary equal-key errors retain multiplicity when no intrinsic produced that key. */
TEST(ParameterAddressIntrinsics, PreservesOrdinaryDiagnosticMultiplicity) {
  const std::string payload =
      ".global .u32 g;.entry k(){.reg .u32 %r;ld.global.u32 "
      "%r,[g];ld.global.u32 %r,[g];ret;}";
  const std::string source = ".version 9.3\n.target sm_80\n" + payload;
  auto module = intrinsic_module(source);
  ASSERT_TRUE(module);
  auto& first =
      dynamic_cast<LdExplicitScalar&>(*module->functions.front().body[0]);
  auto& second =
      dynamic_cast<LdExplicitScalar&>(*module->functions.front().body[1]);
  first.state_space.value = MemoryStateSpace::Shared;
  second.state_space.value = MemoryStateSpace::Shared;
  second.address.locs = first.address.locs;
  auto ast = parse_intrinsic(source);
  for (const auto& result :
       {validateModule(*module), validateModule(ast, *module)}) {
    ASSERT_FALSE(result);
    EXPECT_EQ(
        std::ranges::count_if(
            result.error(),
            [](const auto& diagnostic) {
              return diagnostic.kind ==
                     checker::CheckDiagnosticKind::AddressStateSpaceMismatch;
            }),
        2);
  }
}
/** MOV acquisition preserves Header inputs and materialized Device Local addresses. */
TEST(ParameterAddressIntrinsics, MovReadAcquisitionAndDeviceMaterialization) {
  const std::string payload =
      ".entry k(.param .u32 x){.reg .u64 %rd;mov.u64 %rd,x;ret;}.func f(.param "
      ".u32 x){.reg .u64 %rd;mov.u64 %rd,x;ret;}";
  auto module = intrinsic_module(".version 9.3\n.target sm_80\n" + payload);
  ASSERT_TRUE(module);
  auto& mov = dynamic_cast<MovScalar&>(*module->functions[1].body[0]);
  ASSERT_TRUE(mov.src_mov_source);
  EXPECT_EQ(std::get<ResolvedSymbolRef>(mov.src_mov_source->value)
                .address_state_space,
            base::DeclarationStateSpace::Local);
  auto missing = parse_intrinsic(payload);
  remove_context(*module);
  expect_intrinsic(*module, missing, std::nullopt);
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
