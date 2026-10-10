#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/model/data_movement/ld.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/mov.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Surround a focused address operation with valid binding and target context. */
std::string named_module(std::string_view operation) {
  return ".version 9.3\n.target sm_90\n.address_size 64\n"
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

TEST(NamedArrayAddress, KeepsUnsupportedFamiliesOffAndOldOffsetsInBytes) {
  constexpr std::string_view operations[] = {
      "ld.global.nc.u32 %r, A[0];",
      "ldu.global.u32 %r, A[0];",
      "atom.global.add.u32 %r, A[0], %r;",
      "st.async.release.gpu.global.u32 A[0], %r;",
      "st.bulk.weak.shared::cta S[0], 64, 0;",
      "cp.async.ca.shared.global [%rd], A[0], 4;"};
  for (auto operation : operations) {
    SCOPED_TRACE(operation);
    const auto parsed = test_helpers::parseModule(named_module(operation));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_FALSE(resolveModuleOnly(*parsed));
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
  const auto parsed = test_helpers::parseModule(
      named_module("mov.u64 %rd, A[idx]; ld.global.nc.u32 %r, [A];"));
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved) << resolved.error().front().message;
  auto& load =
      dynamic_cast<LdGlobalNcScalar&>(*resolved->functions.front().body[1]);
  load.address.value = mov_address(*resolved);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 90},
      .instruction_range = {{1, 1}, {100, 1000}}};
  EXPECT_TRUE(valid_named_array_address(load.address.value,
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
