#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <ptx_frontend/resolved_ir/model/data_movement/cp.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/ld.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/mov.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/st.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Surround a focused address operation with valid binding and target context. */
std::string named_module(std::string_view operation) {
  return ".version 9.3\n.target sm_100\n.address_size 64\n"
         ".global .align 16 .u32 A[8];\n.global .u64 B[8];\n"
         ".global .v2 .u32 V[8];\n.global .u32 M[2][4];\n"
         ".global .u32 scalar;\n.shared .align 16 .u32 S[64];\n.entry kernel() "
         "{\n"
         ".reg .u64 %rd; .reg .u32 %r, idx; .reg .s64 signedidx; .reg .s32 "
         "signed32;\n"
         ".reg .u16 narrow; .reg .b32 bits; .reg .f32 floating;\n" +
         std::string(operation) + "\n}\n";
}

/** Borrow the dedicated MOV's owned address payload. */
ResolvedAddress& mov_address(ResolvedModule& module, size_t index = 0) {
  auto& mov = dynamic_cast<MovScalar&>(*module.functions.front().body[index]);
  return std::get<ResolvedAddress>(mov.src_mov_source->value);
}

/** Collect copied address payloads without retaining any source AST. */
struct NamedRoleObserver final : detail::IReferenceObserver {
  /** Owned snapshots in declaration traversal order. */
  std::vector<ResolvedAddress> values;
  /** Copy generic address references synchronously. */
  void address(const ResolvedAddress& value, std::span<const SourceRange>,
               checker::AddressSymbolResolutionPolicy) override {
    values.push_back(value);
  }
};

/** Supply aligned numeric arrays for distinct memory and object address roles. */
std::string role_module(std::string_view operation) {
  return R"ptx(.version 9.3
.target sm_100a
.address_size 64
.global .align 128 .u32 G[1024];
.global .align 128 .b8 GB[4096];
.shared .align 128 .u32 S[1024];
.shared .align 128 .b8 SB[4096];
.shared .align 16 .b64 Bar[16];
.entry kernel() {
.reg .u32 %u;
.reg .b32 %endpoint;
.reg .u64 %a, %v;
.reg .b64 %policy, %dataoff;
)ptx" + std::string(operation) +
         "\n}\n";
}

TEST(NamedArrayAddress, GenericRolePartitionsSurviveSourceDeathCopyAndClone) {
  constexpr std::string_view operations[] = {
      "ld.global.nc.u32 %u, G[4];",
      "ldu.global.u32 %u, G[4];",
      "atom.global.add.u32 %u, G[4], %u;",
      "red.global.add.u32 G[4], %u;",
      "multimem.ld_reduce.relaxed.sys.global.add.u32 %u, G[4];",
      "prefetch.global.L2 GB[128];",
      "applypriority.global.L2::evict_normal GB[128], 128;",
      "discard.global.L2 GB[128], 128;",
      "createpolicy.range.L2::evict_last.b64 %policy, GB[128], 128, 256;",
      "cp.async.ca.shared.global S[4], G[4], 4;",
      "cp.async.bulk.shared::cluster.global.mbarrier::complete_tx::bytes "
      "SB[16], GB[16], 16, Bar[2];",
      "mbarrier.init.shared::cta.b64 Bar[2], 1;",
      "tensormap.replace.tile.rank.b1024.b32 GB[128], 0;",
      "st.async.shared::cluster.mbarrier::complete_tx::bytes.u32 [%a], %u, "
      "Bar[2];",
      "red.async.relaxed.cluster.shared::cluster.mbarrier::complete_tx::bytes."
      "inc.u32 [%a], %u, Bar[2];",
      "tensormap.cp_fenceproxy.global.shared::cta.tensormap::generic.release."
      "gpu.sync.aligned GB[128], SB[128], 128;",
      "clusterlaunchcontrol.try_cancel.async.mbarrier::complete_tx::bytes.b128 "
      "SB[16], Bar[2];",
      "fabric.try_get.async.shared::cta.mbarrier::complete_tx::bytes.mbarrier::"
      "report::fabric.relaxed.sys.b128 SB[16], [%endpoint,%dataoff], 16, "
      "Bar[2];",
      "fabric.try_put.async.shared::cta.mbarrier::complete_tx::16B.mbarrier::"
      "report::fabric.relaxed.sys.b128 [%endpoint,%dataoff], SB[16], 16, "
      "Bar[2];",
      "fabric.try_red.async.shared::cta.mbarrier::complete_tx::16B.mbarrier::"
      "report::fabric.relaxed.sys.add.u32 [%endpoint,%dataoff], SB[16], 16, "
      "Bar[2];",
      "fabric.try_pullred.async.multimem.shared::cta.mbarrier::complete_tx::"
      "bytes.mbarrier::report::fabric.relaxed.sys.add.u32.sync SB[16], "
      "[%endpoint,%dataoff], 16, Bar[2], 0xffffffff;"};
  for (const auto operation : operations) {
    SCOPED_TRACE(operation);
    const auto module = [&] {
      const auto parsed = test_helpers::parseModule(role_module(operation));
      EXPECT_TRUE(parsed.has_value() && parsed.diagnostics.empty());
      return resolveAndValidateModule(*parsed);
    }();
    ASSERT_TRUE(module) << module.error().front().message;
    auto copy = *module;
    copy.functions.front().body[0] = copy.functions.front().body[0]->clone();
    EXPECT_TRUE(validateModule(copy));
    NamedRoleObserver observer;
    copy.functions.front().body[0]->visit_references(observer);
    size_t indexed = 0;
    for (const auto& address : observer.values) {
      if (!address.named_index)
        continue;
      ++indexed;
      const auto& named = *address.named_index;
      const auto& base = std::get<ResolvedSymbolRef>(address.base);
      ASSERT_TRUE(base.symbol_id);
      EXPECT_FALSE(base.parameterized_index);
      const auto stride = base.spelling == "Bar"                         ? 8u
                          : base.spelling == "G" || base.spelling == "S" ? 4u
                                                                         : 1u;
      EXPECT_EQ(named.scalar_stride, stride);
      EXPECT_TRUE(named.byte_displacement == 16 ||
                  named.byte_displacement == 128);
      EXPECT_NE(named.base_range, SourceRange{});
      EXPECT_NE(named.range, SourceRange{});
    }
    EXPECT_GT(indexed, 0u);
  }
}

TEST(NamedArrayAddress,
     RegisterOnlyDataRolesRemainDistinctFromObjectAddresses) {
  constexpr std::string_view patterns[] = {
      "st.async.shared::cluster.mbarrier::complete_tx::bytes.u32 @, %u, "
      "Bar[2];",
      "st.async.release.gpu.global.u32 @, %u;",
      "multimem.st.async.release.gpu.global.u32 @, %u;",
      "red.async.relaxed.cluster.shared::cluster.mbarrier::complete_tx::bytes."
      "inc.u32 @, %u, Bar[2];",
      "red.async.release.gpu.global.add.u32 @, %u;",
      "multimem.red.async.release.sys.add.u32 @, %u;"};
  for (const auto pattern : patterns) {
    for (const auto address : {"[%a]", "[%a+16]", "G[4]", "[G]"}) {
      auto operation = std::string(pattern);
      operation.replace(operation.find('@'), 1, address);
      SCOPED_TRACE(operation);
      const auto parsed = test_helpers::parseModule(role_module(operation));
      ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
      auto module = resolveAndValidateModule(*parsed);
      if (address[1] == '%')
        EXPECT_TRUE(module) << (module ? "" : module.error().front().message);
      else
        EXPECT_FALSE(module);
    }
  }
}

/** Separate tensor grammar and explicit introduction gates survive generic admission. */
TEST(NamedArrayAddress, RetainsSpecializedGrammarAndAvailabilityChecks) {
  const auto parsed = test_helpers::parseModule(
      role_module("mov.u64 %a, GB[128]; "
                  "cp.async.bulk.prefetch.tensor.1d.L2.global [GB, {%u}];"));
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto module = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(module) << module.error().front().message;
  auto& tensor = dynamic_cast<CpAsyncBulkPrefetchTensor1d&>(
      *module->functions.front().body[1]);
  tensor.tensor.value.tensor_map.address = mov_address(*module);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 100},
      .instruction_range = {{1, 1}, {100, 1000}}};
  EXPECT_FALSE(tensor.check(context));
  for (const auto operation : {"cp.async.ca.shared.global S[4], G[4], 4;",
                               "st.async.shared::cluster.mbarrier::complete_tx:"
                               ":bytes.u32 [%a], %u, Bar[2];",
                               "clusterlaunchcontrol.try_cancel.async.mbarrier:"
                               ":complete_tx::bytes.b128 SB[16], Bar[2];"}) {
    SCOPED_TRACE(operation);
    auto old = role_module(operation);
    old.replace(old.find("9.3"), 3, "7.0");
    old.replace(old.find("sm_100a"), 7, "sm_70");
    const auto old_parsed = test_helpers::parseModule(old);
    ASSERT_MODULE_PARSE_SUCCEEDS(old_parsed);
    EXPECT_FALSE(resolveAndValidateModule(*old_parsed));
  }
}

TEST(NamedArrayAddress, PreservesScalarStrideAndIntegerSourceBits) {
  const auto parsed = test_helpers::parseModule(
      named_module("mov.u64 %rd, A[1+1]; mov.u64 %rd, B[1]; "
                   "mov.u64 %rd, V[1]; mov.u64 %rd, M[1]; "
                   "mov.u64 %rd, A[-1]; mov.u64 %rd, A[8]; "
                   "mov.u64 %rd, A[+1]; mov.u64 %rd, A[1U]; "
                   "mov.u64 %rd, A[WARP_SZ+1]; "
                   "ld.u32 %r, A[1]; st.u32 A[1], %r;"));
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved) << resolved.error().front().message;
  constexpr int64_t bytes[] = {8, 8, 4, 4, -4, 32, 4, 4, 132};
  for (size_t index = 0; index < 9; ++index) {
    const auto& address = mov_address(*resolved, index);
    ASSERT_TRUE(address.named_index);
    EXPECT_FALSE(address.offset);
    EXPECT_EQ(address.named_index->byte_displacement, bytes[index]);
    EXPECT_TRUE(
        valid_named_array_address(address, NamedArrayAddressPolicy::Mov,
                                  {address.named_index->base_range.start,
                                   address.named_index->range.end}));
  }
  EXPECT_EQ(mov_address(*resolved).named_index->scalar_stride, 4u);
  const auto& integer = std::get<declaration_semantics::IntegerConstantValue>(
      mov_address(*resolved).named_index->index);
  EXPECT_EQ(integer.bits, 2u);
  EXPECT_FALSE(integer.is_unsigned);
  EXPECT_TRUE(std::get<declaration_semantics::IntegerConstantValue>(
                  mov_address(*resolved, 7).named_index->index)
                  .is_unsigned);
}

TEST(NamedArrayAddress, KeepsDynamicRegisterTypeAndWrittenOperation) {
  const auto parsed = test_helpers::parseModule(named_module(
      "mov.u64 %rd, A[idx-1]; mov.u64 %rd, A[idx + -1]; "
      "mov.u64 %rd, A[narrow]; mov.u64 %rd, A[signedidx]; "
      "mov.u64 %rd, A[bits]; mov.u64 %rd, A[%rd]; mov.u64 %rd, A[signed32]; "
      "ld.u32 %r, A[%r+2];"));
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved) << resolved.error().front().message;
  auto& minus = *mov_address(*resolved).named_index;
  auto& plus = *mov_address(*resolved, 1).named_index;
  EXPECT_EQ(minus.byte_displacement, -4);
  EXPECT_EQ(plus.byte_displacement, -4);
  EXPECT_EQ(minus.operation, ResolvedAddressOffsetOperator::Subtract);
  EXPECT_EQ(plus.operation, ResolvedAddressOffsetOperator::Add);
  EXPECT_NE(minus.operator_range, SourceRange{});
  constexpr ScalarType types[] = {
      ScalarType::U32, ScalarType::U32, ScalarType::U16, ScalarType::S64,
      ScalarType::B32, ScalarType::U64, ScalarType::S32};
  for (size_t index = 0; index < 7; ++index) {
    const auto& value = *mov_address(*resolved, index).named_index;
    const auto& reg = std::get<WithLocs<ResolvedRegisterRef>>(value.index);
    EXPECT_EQ(reg.value.declared_type, types[index]);
    EXPECT_EQ(resolved_address_alignment(mov_address(*resolved, index)), 4u);
  }
}

/** Spacing preserves equivalent displacement without erasing written operators. */
TEST(NamedArrayAddress, PreservesAllThreeWrittenMinusForms) {
  const auto parsed = test_helpers::parseModule(
      named_module("mov.u64 %rd, A[%r-1]; mov.u64 %rd, A[%r - 1]; "
                   "mov.u64 %rd, A[%r + -1];"));
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto module = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(module) << module.error().front().message;
  const auto& first = mov_address(*module);
  ASSERT_TRUE(first.named_index);
  const auto& first_reg =
      std::get<WithLocs<ResolvedRegisterRef>>(first.named_index->index);
  for (size_t index = 0; index < 3; ++index) {
    const auto& address = mov_address(*module, index);
    ASSERT_TRUE(address.named_index);
    const auto& named = *address.named_index;
    const auto& reg = std::get<WithLocs<ResolvedRegisterRef>>(named.index);
    EXPECT_EQ(std::get<ResolvedSymbolRef>(address.base).symbol_id,
              std::get<ResolvedSymbolRef>(first.base).symbol_id);
    EXPECT_EQ(reg.value.symbol_id, first_reg.value.symbol_id);
    EXPECT_EQ(reg.value.declared_type, ScalarType::U32);
    EXPECT_EQ(named.scalar_stride, 4u);
    EXPECT_EQ(named.byte_displacement, -4);
    EXPECT_EQ(resolved_address_alignment(address), 4u);
    EXPECT_EQ(named.operation, index == 2
                                   ? ResolvedAddressOffsetOperator::Add
                                   : ResolvedAddressOffsetOperator::Subtract);
    EXPECT_NE(named.operator_range, SourceRange{});
    EXPECT_EQ(named.operator_range.start.line, named.operator_range.end.line);
    EXPECT_EQ(
        named.operator_range.end.column - named.operator_range.start.column, 1);
  }
  EXPECT_TRUE(validateModule(*module));
}

TEST(NamedArrayAddress, RejectsNonArrayBadIndexAndScaledOverflow) {
  constexpr std::string_view operations[] = {
      "mov.u64 %rd, scalar[0];", "mov.u64 %rd, A[floating];",
      "mov.u64 %rd, A[scalar];", "mov.u64 %rd, A[idx+idx];",
      "mov.u64 %rd, A[1.5];",    "mov.u64 %rd, A[0xffffffffffffffffU];",
      "ld.u32 %r, A[536870912];"};
  for (auto operation : operations) {
    SCOPED_TRACE(operation);
    const auto parsed = test_helpers::parseModule(named_module(operation));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveModuleOnly(*parsed));
  }
}

TEST(NamedArrayAddress, AdmitsGenericRolesAndKeepsOldOffsetsInBytes) {
  constexpr std::string_view operations[] = {
      "ld.global.nc.u32 %r, A[0];", "ldu.global.u32 %r, A[0];",
      "atom.global.add.u32 %r, A[0], %r;",
      "st.bulk.weak.shared::cta S[0], 64, 0;",
      "cp.async.ca.shared.global [%rd], A[0], 4;"};
  for (auto operation : operations) {
    SCOPED_TRACE(operation);
    const auto parsed = test_helpers::parseModule(named_module(operation));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_TRUE(resolveModule(*parsed));
  }
  const auto parsed =
      test_helpers::parseModule(named_module("mov.u64 %rd, A+1;"));
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved) << resolved.error().front().message;
  const auto& address = mov_address(*resolved);
  EXPECT_FALSE(address.named_index);
  ASSERT_TRUE(address.offset);
  EXPECT_EQ(address.offset->value.bits, 1u);
}

TEST(NamedArrayAddress,
     DynamicAlignmentUsesScalarStrideRatherThanArrayAlignment) {
  const auto parsed =
      test_helpers::parseModule(named_module("ld.v2.u32 {%r, idx}, A[%r];"));
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveModuleOnly(*parsed);
  ASSERT_TRUE(resolved) << resolved.error().front().message;
  EXPECT_FALSE(validateModule(*resolved));
  const auto constant =
      test_helpers::parseModule(named_module("ld.v2.u32 {%r, idx}, A[2];"));
  ASSERT_MODULE_PARSE_SUCCEEDS(constant);
  EXPECT_TRUE(resolveModule(*constant));
}

TEST(NamedArrayAddress,
     OwnedValidationSurvivesSourceReleaseAndRejectsTampering) {
  auto owned = [] {
    const auto parsed =
        test_helpers::parseModule(named_module("mov.u64 %rd, A[idx+1];"));
    return resolveModule(*parsed);
  }();
  ASSERT_TRUE(owned) << owned.error().front().message;
  EXPECT_TRUE(validateModule(*owned));
  ResolvedModule copied = *owned;
  ResolvedModule moved = std::move(copied);
  EXPECT_TRUE(validateModule(moved));
  mov_address(moved).named_index->scalar_stride = 8;
  EXPECT_FALSE(validateModule(moved));
  moved = *owned;
  mov_address(moved).named_index->byte_displacement = 8;
  EXPECT_FALSE(validateModule(moved));
  moved = *owned;
  moved.storage_declarations.front().array_extents.clear();
  EXPECT_FALSE(validateModule(moved));
  moved = *owned;
  auto& reg = std::get<WithLocs<ResolvedRegisterRef>>(
                  mov_address(moved).named_index->index)
                  .value;
  reg.declared_type = ScalarType::S64;
  EXPECT_FALSE(validateModule(moved));
  moved = *owned;
  mov_address(moved).named_index->operator_range = {};
  EXPECT_FALSE(validateModule(moved));
  moved = *owned;
  mov_address(moved).unified = true;
  mov_address(moved).unified_range =
      mov_address(moved).named_index->right_bracket_range;
  EXPECT_FALSE(validateModule(moved));
  moved = *owned;
  mov_address(moved).unified_range =
      mov_address(moved).named_index->right_bracket_range;
  EXPECT_FALSE(validateModule(moved));
}

TEST(NamedArrayAddress,
     PreservesUnifiedStorageAddressTakingAndBracketLoadPolicy) {
  const auto source = [](std::string_view operation) {
    return ".version 9.3\n.target sm_90\n.address_size 64\n"
           ".global .attribute(.unified(1, 2)) .u32 U[8];\n"
           ".entry kernel() { .reg .u64 %rd; .reg .u32 %r; " +
           std::string(operation) + " }";
  };
  const auto valid = test_helpers::parseModule(
      source("mov.u64 %rd, U; mov.u64 %rd, U+4; mov.u64 %rd, U[1]; "
             "ld.global.u32 %r, [U+4].unified;"));
  ASSERT_MODULE_PARSE_SUCCEEDS(valid);
  const auto owned = resolveModule(*valid);
  ASSERT_TRUE(owned) << owned.error().front().message;
  for (const auto operation :
       {"ld.global.u32 %r, U[1];", "st.global.u32 U[1], %r;"}) {
    const auto invalid = test_helpers::parseModule(source(operation));
    ASSERT_MODULE_PARSE_SUCCEEDS(invalid);
    EXPECT_FALSE(resolveModule(*invalid));
  }
  const auto suffix =
      test_helpers::parseInstruction("ld.u32 %r, U[1].unified;");
  EXPECT_TRUE(!suffix || !suffix.diagnostics.empty());
}

TEST(NamedArrayAddress, OwnedFamilyFenceRejectsTransplantedIndex) {
  const auto parsed = test_helpers::parseModule(named_module(
      "mov.u64 %rd, A[idx]; st.async.release.gpu.global.u32 [%rd], %r;"));
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved) << resolved.error().front().message;
  auto& load =
      dynamic_cast<StAsyncGlobalRelease&>(*resolved->functions.front().body[1]);
  load.dst.value = mov_address(*resolved);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 100},
      .instruction_range = {{1, 1}, {100, 1000}}};
  EXPECT_TRUE(valid_named_array_address(load.dst.value,
                                        NamedArrayAddressPolicy::Memory,
                                        context.instruction_range));
  EXPECT_FALSE(load.check(context));
}

TEST(NamedArrayAddress,
     SourceIdentityKeepsEquivalentWrittenOperationsDistinct) {
  const auto minus =
      test_helpers::parseModule(named_module("mov.u64 %rd, A[idx-1];"));
  const auto plus =
      test_helpers::parseModule(named_module("mov.u64 %rd, A[idx+-1];"));
  ASSERT_MODULE_PARSE_SUCCEEDS(minus);
  ASSERT_MODULE_PARSE_SUCCEEDS(plus);
  const auto owned = resolveModule(*minus);
  ASSERT_TRUE(owned) << owned.error().front().message;
  EXPECT_TRUE(validateModule(*minus, *owned));
  EXPECT_FALSE(validateModule(*plus, *owned));
}

TEST(NamedArrayAddress,
     StandaloneRequiresOwnedShapeAndSyntaxRejectsComplexForms) {
  const auto parsed = test_helpers::parseInstruction("mov.u64 %rd0, A[1];");
  ASSERT_TRUE(parsed);
  const auto resolved = resolveMov(*parsed);
  ASSERT_FALSE(resolved);
  EXPECT_NE(resolved.error().message.find("array-shape context"),
            std::string::npos);
  constexpr std::string_view invalid[] = {"A[idx+idx]", "A[idx*2]", "A[1][2]",
                                          "[A[1]]"};
  for (auto source : invalid) {
    SCOPED_TRACE(source);
    const auto instruction = test_helpers::parseInstruction(
        "mov.u64 %rd0, " + std::string(source) + ";");
    if (instruction && instruction.diagnostics.empty())
      EXPECT_FALSE(resolveMov(*instruction));
    else
      EXPECT_TRUE(!instruction || !instruction.diagnostics.empty());
  }
}

TEST(NamedArrayAddress,
     ParameterShapeCollectionPreservesOrderAndNestedShadowing) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.func helper(.param .u32 input[4]) {
  .reg .u64 %rd;
  .reg .u32 %r;
  mov.u64 %rd, input[1];
  .param .u32 staging[2];
  ld.param.u32 %r, staging[1];
  { .param .u64 staging[2]; ld.param.u64 %rd, staging[1]; }
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved) << resolved.error().front().message;
  const auto& parameters = resolved->functions.front().parameter_declarations;
  ASSERT_EQ(parameters.size(), 3u);
  EXPECT_EQ(parameters[0].scalar_type, ScalarType::U32);
  EXPECT_EQ(parameters[1].scalar_type, ScalarType::U32);
  EXPECT_EQ(parameters[2].scalar_type, ScalarType::U64);
  EXPECT_EQ(mov_address(*resolved, 0).named_index->scalar_stride, 4u);
  EXPECT_EQ(
      dynamic_cast<LdExplicitScalar&>(*resolved->functions.front().body[1])
          .address.value.named_index->scalar_stride,
      4u);
  EXPECT_EQ(
      dynamic_cast<LdExplicitScalar&>(*resolved->functions.front().body[2])
          .address.value.named_index->scalar_stride,
      8u);
  EXPECT_TRUE(validateModule(*resolved));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
