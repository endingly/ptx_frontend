#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Supply known read-map and CTA source storage around one tensor write. */
std::string no_offset_module(
    std::string_view instruction,
    std::string_view map = ".global .align 64 .b8 tensor_map[128];",
    std::string_view source = ".shared .align 16 .b8 src[1024];") {
  return ".version 9.3\n.target sm_90\n.address_size 64\n" + std::string(map) +
         "\n" + std::string(source) +
         "\n.entry kernel() {\n.reg .s32 %r<5>;\n" + std::string(instruction) +
         "\n}\n";
}

/** Match direct checks to the same catalog profile used by module checks. */
checker::Context no_offset_context(checker::PtxVersion version = {9, 3},
                                   std::string_view target = "sm_90") {
  const auto profile = base::find_target_profile(target);
  if (!profile)
    return {};
  return {
      .target = {.ptx_version = version,
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
}

/** Mutable fields shared by the exact tensor-write identities tested here. */
struct MutableTensorWrite {
  /** Owned tensor operand within the instruction. */
  ResolvedTensorOperand* tensor;
  /** Owned shared CTA source address within the instruction. */
  ResolvedAddress* src;
};

/** Borrow write fields from the exact store and reduction forms in this suite. */
std::optional<MutableTensorWrite> mutable_tensor_write(
    Instruction& instruction) {
#define PTX_TRY_TENSOR_WRITE(CLASS)                        \
  if (auto* selected = dynamic_cast<CLASS*>(&instruction)) \
  return MutableTensorWrite{&selected->tensor.value, &selected->src.value}
  PTX_TRY_TENSOR_WRITE(CpAsyncBulkTensor3dGlobalSharedCtaIm2colNoOffs);
  PTX_TRY_TENSOR_WRITE(CpReduceAsyncBulkTensor3dAddIm2colNoOffs);
  PTX_TRY_TENSOR_WRITE(CpReduceAsyncBulkTensor3dXorIm2colNoOffs);
  PTX_TRY_TENSOR_WRITE(CpAsyncBulkTensor1dGlobalSharedCta);
  PTX_TRY_TENSOR_WRITE(CpAsyncBulkTensor2dGlobalSharedCta);
  PTX_TRY_TENSOR_WRITE(CpAsyncBulkTensor3dGlobalSharedCta);
  PTX_TRY_TENSOR_WRITE(CpAsyncBulkTensor4dGlobalSharedCta);
  PTX_TRY_TENSOR_WRITE(CpAsyncBulkTensor5dGlobalSharedCta);
#undef PTX_TRY_TENSOR_WRITE
  return std::nullopt;
}

/** Count owned tensor and address references during synchronous traversal. */
class TensorReferenceCounter final : public detail::IReferenceObserver {
 public:
  /** Count a tensor operand reference. */
  void tensor_operand(const ResolvedTensorOperand&,
                      std::span<const SourceRange>,
                      checker::AddressSymbolResolutionPolicy) override {
    ++count;
  }
  /** Count an address reference. */
  void address(const ResolvedAddress&, std::span<const SourceRange>,
               checker::AddressSymbolResolutionPolicy) override {
    ++count;
  }
  /** Number of references seen. */
  int count = 0;
};

/** Every rank and operation selects a distinct fixed-mode owned alternative. */
TEST(TensorNoOffsets, AllTwentySevenIdentities) {
  constexpr std::array<std::string_view, 8> operations{
      "add", "min", "max", "inc", "dec", "and", "or", "xor"};
  std::set<InstructionKind> alternative_indices;
  for (int rank = 3; rank <= 5; ++rank) {
    std::string coordinates = "%r0";
    for (int i = 1; i < rank; ++i)
      coordinates += ", %r" + std::to_string(i);
    for (int operation_index = -1;
         operation_index < static_cast<int>(operations.size());
         ++operation_index) {
      const bool reduction = operation_index >= 0;
      const std::string instruction =
          "cp." + std::string(reduction ? "reduce." : "") +
          "async.bulk.tensor." + std::to_string(rank) +
          "d.global.shared::cta." +
          (reduction ? std::string(operations[operation_index]) + "." : "") +
          "im2col_no_offs.bulk_group [tensor_map, {" + coordinates +
          "}], [src];";
      std::optional<ResolvedModule> owned;
      {
        const auto parsed =
            test_helpers::parseModule(no_offset_module(instruction));
        ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
        auto resolved = resolveModuleOnly(*parsed);
        ASSERT_TRUE(resolved.has_value())
            << resolved.error().front().message << ": " << instruction;
        owned.emplace(std::move(*resolved));
      }
      ASSERT_EQ(owned->functions.front().body.size(), 1u);
      const auto& copy = owned->functions.front().body.front();
      alternative_indices.insert(copy->instruction_kind());
      EXPECT_TRUE(copy->check(no_offset_context()).has_value()) << instruction;
      EXPECT_TRUE(
          validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
              .has_value())
          << instruction;
      class TensorObserver final : public detail::IReferenceObserver {
       public:
        /** Check the borrowed tensor during synchronous reference traversal. */
        void tensor_operand(const ResolvedTensorOperand& value,
                            std::span<const SourceRange>,
                            checker::AddressSymbolResolutionPolicy) override {
          seen = true;
          EXPECT_EQ(value.mode, TensorAccessMode::Im2colNoOffs);
          EXPECT_EQ(value.rank, expected_rank);
          EXPECT_EQ(value.coordinates.elements.size(), expected_coordinates);
        }
        /** Expected semantic rank and coordinate arity. */
        TensorRank expected_rank = TensorRank::Three;
        size_t expected_coordinates = 3;
        /** Whether a tensor operand was reached. */
        bool seen = false;
      } observer;
      observer.expected_rank = static_cast<TensorRank>(rank);
      observer.expected_coordinates = static_cast<size_t>(rank);
      copy->visit_references(observer);
      EXPECT_TRUE(observer.seen);
      const auto kind = copy->instruction_kind();
      if (!reduction) {
        EXPECT_TRUE(
            kind == InstructionKind::
                        CpAsyncBulkTensor3dGlobalSharedCtaIm2colNoOffs ||
            kind == InstructionKind::
                        CpAsyncBulkTensor4dGlobalSharedCtaIm2colNoOffs ||
            kind == InstructionKind::
                        CpAsyncBulkTensor5dGlobalSharedCtaIm2colNoOffs);
      } else {
#define PTX_EXPECT_NO_OFFSET_OP(OP)                                           \
  EXPECT_TRUE(                                                                \
      kind == InstructionKind::CpReduceAsyncBulkTensor3d##OP##Im2colNoOffs || \
      kind == InstructionKind::CpReduceAsyncBulkTensor4d##OP##Im2colNoOffs || \
      kind == InstructionKind::CpReduceAsyncBulkTensor5d##OP##Im2colNoOffs)
        switch (operation_index) {
          case 0:
            PTX_EXPECT_NO_OFFSET_OP(Add);
            break;
          case 1:
            PTX_EXPECT_NO_OFFSET_OP(Min);
            break;
          case 2:
            PTX_EXPECT_NO_OFFSET_OP(Max);
            break;
          case 3:
            PTX_EXPECT_NO_OFFSET_OP(Inc);
            break;
          case 4:
            PTX_EXPECT_NO_OFFSET_OP(Dec);
            break;
          case 5:
            PTX_EXPECT_NO_OFFSET_OP(And);
            break;
          case 6:
            PTX_EXPECT_NO_OFFSET_OP(Or);
            break;
          case 7:
            PTX_EXPECT_NO_OFFSET_OP(Xor);
            break;
        }
#undef PTX_EXPECT_NO_OFFSET_OP
      }
    }
  }
  EXPECT_EQ(alternative_indices.size(), 27u);
}

/** Selection rejects adjacent shapes rather than treating the fixed mode as tile. */
TEST(TensorNoOffsets, RejectsAdjacentSpellingsAndTargetFloor) {
  constexpr std::array<std::string_view, 11> invalid{
      "cp.async.bulk.tensor.1d.global.shared::cta.im2col_no_offs.bulk_group "
      "[tensor_map, {0}], [src];",
      "cp.async.bulk.tensor.2d.global.shared::cta.im2col_no_offs.bulk_group "
      "[tensor_map, {0, 1}], [src];",
      "cp.async.bulk.tensor.3d.global.shared::cta.im2col.bulk_group "
      "[tensor_map, {0, 1, 2}], [src];",
      "cp.async.bulk.tensor.3d.global.shared::cta.tile.im2col_no_offs.bulk_"
      "group "
      "[tensor_map, {0, 1, 2}], [src];",
      "cp.async.bulk.tensor.3d.global.shared::cta.im2col_no_offs."
      "mbarrier::complete_tx::bytes [tensor_map, {0, 1, 2}], [src];",
      "cp.async.bulk.tensor.3d.global.shared::cta.im2col_no_offs.bulk_group "
      "[tensor_map, {0, 1, 2}], [src], 0;",
      "cp.async.bulk.tensor.3d.global.shared::cta.im2col_no_offs."
      "cta_group::2.bulk_group [tensor_map, {0, 1, 2}], [src];",
      "cp.async.bulk.tensor.3d.global.shared::cta.im2col_no_offs."
      "L2::cache_hint.bulk_group [tensor_map, {0, 1, 2}], [src], %r0;",
      "cp.async.bulk.tensor.3d.shared::cluster.global.im2col_no_offs."
      "bulk_group [tensor_map, {0, 1, 2}], [src];",
      "cp.async.bulk.tensor.3d.global.shared::cta.im2col_no_offs."
      "u32.bulk_group [tensor_map, {0, 1, 2}], [src];",
      "cp.reduce.async.bulk.tensor.3d.global.shared::cta.xor.im2col_no_offs."
      "multicast::cluster.bulk_group [tensor_map, {0, 1, 2}], [src];",
  };
  for (const auto instruction : invalid) {
    const auto parsed =
        test_helpers::parseModule(no_offset_module(instruction));
    if (parsed)
      EXPECT_FALSE(resolveModuleOnly(*parsed).has_value()) << instruction;
  }
  const auto parsed = test_helpers::parseModule(no_offset_module(
      "cp.async.bulk.tensor.3d.global.shared::cta.im2col_no_offs.bulk_group "
      "[tensor_map, {0, 1, 2}], [src];"));
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveModuleOnly(*parsed);
  ASSERT_TRUE(resolved.has_value());
  const auto& copy = resolved->functions.front().body.front();
  EXPECT_TRUE(copy->check(no_offset_context({8, 0})).has_value());
  EXPECT_FALSE(copy->check(no_offset_context({7, 9})).has_value());
  EXPECT_FALSE(copy->check(no_offset_context({9, 3}, "sm_89")).has_value());
}

/** The owned mode and converted coordinate are rechecked after AST lifetime. */
TEST(TensorNoOffsets, OwnedModeCoordinatesAndReferences) {
  std::optional<ResolvedModule> owned;
  {
    const auto parsed = test_helpers::parseModule(no_offset_module(
        "cp.reduce.async.bulk.tensor.3d.global.shared::cta.xor."
        "im2col_no_offs.bulk_group [tensor_map, {4294967296, 1, 2}], [src];"));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModuleOnly(*parsed);
    ASSERT_TRUE(resolved.has_value());
    owned.emplace(std::move(*resolved));
  }
  auto& copy = owned->functions.front().body.front();
  auto selected = mutable_tensor_write(*copy);
  ASSERT_TRUE(selected);
  auto& tensor = *selected->tensor;
  auto& immediate =
      std::get<ResolvedImmediate>(tensor.coordinates.elements.front());
  const auto context = no_offset_context();
  ASSERT_TRUE(copy->check(context).has_value());
  TensorReferenceCounter references;
  copy->visit_references(references);
  EXPECT_GE(references.count, 2);
  EXPECT_EQ(immediate.bits, 0u);
  EXPECT_EQ(immediate.integer_source_bits, 0x100000000ULL);
  tensor.mode = TensorAccessMode::Tiled;
  EXPECT_FALSE(copy->check(context).has_value());
  EXPECT_FALSE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
  tensor.mode = static_cast<TensorAccessMode>(255);
  EXPECT_FALSE(copy->check(context).has_value());
  tensor.mode = TensorAccessMode::Im2colNoOffs;
  immediate.bits = 1;
  EXPECT_FALSE(copy->check(context).has_value());
  immediate.bits = 0;
  immediate.integer_source_bits.reset();
  EXPECT_FALSE(copy->check(context).has_value());
  immediate.integer_source_bits = 0x100000000ULL;
  tensor.rank = TensorRank::Two;
  EXPECT_FALSE(copy->check(context).has_value());
  tensor.rank = TensorRank::Three;
  EXPECT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
}

/** Store and reduction coordinate register shape remains checked after AST death. */
TEST(TensorNoOffsets, OwnedWriteCoordinateRegisterShapes) {
  for (const bool reduction : {false, true}) {
    const std::string instruction =
        "cp." + std::string(reduction ? "reduce." : "") +
        "async.bulk.tensor.3d.global.shared::cta." + (reduction ? "add." : "") +
        "im2col_no_offs.bulk_group [tensor_map, {%r0, 1, 2}], [src];";
    std::optional<ResolvedModule> owned;
    {
      const auto parsed =
          test_helpers::parseModule(no_offset_module(instruction));
      ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
      auto resolved = resolveModuleOnly(*parsed);
      ASSERT_TRUE(resolved.has_value()) << instruction;
      owned.emplace(std::move(*resolved));
    }
    auto& copy = owned->functions.front().body.front();
    const auto context = no_offset_context();
    auto check_selected = [&](ResolvedTensorOperand& tensor) {
      auto& coordinate =
          std::get<ResolvedRegisterRef>(tensor.coordinates.elements.front());
      ASSERT_TRUE(coordinate.symbol_id);
      ASSERT_EQ(coordinate.declared_type, base::ScalarType::S32);
      ASSERT_TRUE(copy->check(context).has_value());
      ASSERT_TRUE(
          validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
              .has_value());
      const auto saved = coordinate;
      auto check_damage = [&] {
        EXPECT_FALSE(copy->check(context).has_value());
        EXPECT_FALSE(validateModule(
                         *owned, ModuleValidationPolicy::RequireCompleteContext)
                         .has_value());
      };
      coordinate.vector_width = 2;
      check_damage();
      coordinate = saved;
      coordinate.register_class = ResolvedRegisterClass::Predicate;
      check_damage();
      coordinate = saved;
      coordinate.declared_type = base::ScalarType::B16;
      check_damage();
      coordinate = saved;
      coordinate.declared_type.reset();
      check_damage();
      coordinate = saved;
      EXPECT_TRUE(copy->check(context).has_value());
      EXPECT_TRUE(
          validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
              .has_value());
      coordinate.symbol_id.reset();
      coordinate.declared_type.reset();
      EXPECT_TRUE(copy->check(context).has_value());
      coordinate = saved;
    };
    auto selected = mutable_tensor_write(*copy);
    ASSERT_TRUE(selected);
    check_selected(*selected->tensor);
  }
}

/** Resolver reports a malformed generated binding before constructing IR. */
TEST(TensorNoOffsets, MissingOrInvalidBindingModeDiagnosed) {
  const auto parsed = test_helpers::parseInstruction(
      "cp.async.bulk.tensor.3d.global.shared::cta.im2col_no_offs."
      "bulk_group [tensor_map, {0, 1, 2}], [src];");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
  const auto& canonical = cp_resolved_descriptor();
  std::vector<check_end::ResolvedVariantDescriptor> variants(
      canonical.variants.begin(), canonical.variants.end());
  auto selected =
      std::find_if(variants.begin(), variants.end(), [](const auto& candidate) {
        return candidate.variant_name ==
               "AsyncBulkTensor3dGlobalSharedCtaIm2colNoOffs";
      });
  ASSERT_NE(selected, variants.end());
  ASSERT_EQ(selected->operand_layouts.size(), 1u);
  std::vector<check_end::ResolvedOperandLayoutDescriptor> layouts(
      selected->operand_layouts.begin(), selected->operand_layouts.end());
  std::vector<check_end::ResolvedOperandBindingDescriptor> bindings(
      layouts.front().bindings.begin(), layouts.front().bindings.end());
  ASSERT_EQ(bindings.size(), 2u);
  ASSERT_EQ(bindings.front().expected_tensor_mode,
            TensorAccessMode::Im2colNoOffs);
  layouts.front().bindings = bindings;
  selected->operand_layouts = layouts;
  auto descriptor = canonical;
  descriptor.variants = variants;
  for (const auto mode :
       {std::optional<TensorAccessMode>{},
        std::optional<TensorAccessMode>{static_cast<TensorAccessMode>(255)}}) {
    bindings.front().expected_tensor_mode = mode;
    layouts.front().bindings = bindings;
    selected->operand_layouts = layouts;
    descriptor.variants = variants;
    const auto fields =
        resolve_fields(*parsed, cp_syntax_descriptor(), descriptor,
                       "AsyncBulkTensor3dGlobalSharedCtaIm2colNoOffs");
    ASSERT_FALSE(fields.has_value());
    EXPECT_NE(fields.error().message.find("no valid access mode"),
              std::string::npos);
  }
}

/** No-offset writes reject the sign after S32 conversion, not source sign. */
TEST(TensorNoOffsets, ConvertedCoordinateSign) {
  constexpr std::array<std::string_view, 5> values{"4294967296", "-4294967296",
                                                   "-2147483649", "4294967295",
                                                   "18446744073709551615"};
  for (const bool reduction : {false, true}) {
    for (size_t index = 0; index < values.size(); ++index) {
      const std::string instruction =
          "cp." + std::string(reduction ? "reduce." : "") +
          "async.bulk.tensor.3d.global.shared::cta." +
          (reduction ? "add." : "") +
          "im2col_no_offs.bulk_group [tensor_map, {" +
          std::string(values[index]) + ", 1, 2}], [src];";
      const auto parsed =
          test_helpers::parseModule(no_offset_module(instruction));
      ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
      EXPECT_EQ(resolveModule(*parsed).has_value(), index < 3) << instruction;
    }
  }
}

/** Both address roles retain space, alignment, and identity after AST death. */
TEST(TensorNoOffsets, OwnedStoreAndReductionAddressMutation) {
  for (const bool reduction : {false, true}) {
    const std::string instruction =
        "cp." + std::string(reduction ? "reduce." : "") +
        "async.bulk.tensor.3d.global.shared::cta." + (reduction ? "add." : "") +
        "im2col_no_offs.bulk_group [tensor_map, {0, 1, 2}], [src];";
    std::optional<ResolvedModule> owned;
    {
      const auto parsed =
          test_helpers::parseModule(no_offset_module(instruction));
      ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
      auto resolved = resolveModuleOnly(*parsed);
      ASSERT_TRUE(resolved.has_value());
      owned.emplace(std::move(*resolved));
    }
    auto& copy = owned->functions.front().body.front();
    const auto context = no_offset_context();
    auto selected = mutable_tensor_write(*copy);
    ASSERT_TRUE(selected);
    auto& map =
        std::get<ResolvedSymbolRef>(selected->tensor->tensor_map.address.base);
    auto& source = std::get<ResolvedSymbolRef>(selected->src->base);
    ASSERT_TRUE(copy->check(context).has_value());
    const auto map_alignment = map.address_alignment;
    map.address_alignment = 32;
    EXPECT_FALSE(copy->check(context).has_value());
    EXPECT_FALSE(
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
            .has_value());
    map.address_alignment = map_alignment;
    const auto source_alignment = source.address_alignment;
    source.address_alignment = 8;
    EXPECT_FALSE(copy->check(context).has_value());
    source.address_alignment = source_alignment;
    const auto map_space = map.address_state_space;
    map.address_state_space = base::DeclarationStateSpace::Shared;
    EXPECT_FALSE(copy->check(context).has_value());
    map.address_state_space = map_space;
    const auto source_space = source.address_state_space;
    source.address_state_space = base::DeclarationStateSpace::Global;
    EXPECT_FALSE(copy->check(context).has_value());
    source.address_state_space = source_space;
    const auto map_id = map.symbol_id;
    map.symbol_id = source.symbol_id;
    EXPECT_FALSE(
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
            .has_value());
    map.symbol_id = map_id;
    EXPECT_TRUE(
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
            .has_value());
  }
}

/** New write modes independently recheck both scalar pointer registers. */
TEST(TensorNoOffsets, OwnedStoreAndReductionPointerShape) {
  for (const bool reduction : {false, true}) {
    const std::string instruction =
        "cp." + std::string(reduction ? "reduce." : "") +
        "async.bulk.tensor.3d.global.shared::cta." + (reduction ? "add." : "") +
        "im2col_no_offs.bulk_group [%map, {0, 1, 2}], [%src];";
    std::optional<ResolvedModule> owned;
    {
      const auto parsed = test_helpers::parseModule(
          no_offset_module(instruction, ".reg .b64 %map;", ".reg .b64 %src;"));
      ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
      auto resolved = resolveModuleOnly(*parsed);
      ASSERT_TRUE(resolved.has_value());
      owned.emplace(std::move(*resolved));
    }
    auto& copy = owned->functions.front().body.front();
    const auto context = no_offset_context();
    auto selected = mutable_tensor_write(*copy);
    ASSERT_TRUE(selected);
    auto& map = std::get<ResolvedRegisterRef>(
        selected->tensor->tensor_map.address.base);
    auto& source = std::get<ResolvedRegisterRef>(selected->src->base);
    for (auto* pointer : {&map, &source}) {
      pointer->vector_width = 2;
      EXPECT_FALSE(copy->check(context).has_value());
      EXPECT_FALSE(
          validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
              .has_value());
      pointer->vector_width.reset();
      pointer->register_class = ResolvedRegisterClass::Predicate;
      EXPECT_FALSE(copy->check(context).has_value());
      pointer->register_class = ResolvedRegisterClass::General;
      pointer->declared_type = base::ScalarType::B16;
      EXPECT_FALSE(copy->check(context).has_value());
      pointer->declared_type = base::ScalarType::B64;
    }
    EXPECT_TRUE(copy->check(context).has_value());
  }
}

/** Both pointer roles on every tiled store now share reduction scalar checks. */
TEST(TensorNoOffsets, FiveExistingTiledStoresRecheckOwnedPointers) {
  for (int rank = 1; rank <= 5; ++rank) {
    std::string coordinates = "0";
    for (int i = 1; i < rank; ++i)
      coordinates += ", 0";
    std::optional<ResolvedModule> owned;
    {
      const auto parsed = test_helpers::parseModule(
          no_offset_module("cp.async.bulk.tensor." + std::to_string(rank) +
                               "d.global.shared::cta.bulk_group [%map, {" +
                               coordinates + "}], [%src];",
                           ".reg .b64 %map;", ".reg .b64 %src;"));
      ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
      auto resolved = resolveModuleOnly(*parsed);
      ASSERT_TRUE(resolved.has_value());
      owned.emplace(std::move(*resolved));
    }
    auto& copy = owned->functions.front().body.front();
    const auto context = no_offset_context();
    auto selected = mutable_tensor_write(*copy);
    ASSERT_TRUE(selected);
    auto& map = std::get<ResolvedRegisterRef>(
        selected->tensor->tensor_map.address.base);
    auto& source = std::get<ResolvedRegisterRef>(selected->src->base);
    for (auto* pointer : {&map, &source}) {
      pointer->vector_width = 2;
      EXPECT_FALSE(copy->check(context).has_value()) << rank;
      EXPECT_FALSE(
          validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
              .has_value());
      pointer->vector_width.reset();
      pointer->declared_type = base::ScalarType::B16;
      EXPECT_FALSE(copy->check(context).has_value()) << rank;
      EXPECT_FALSE(
          validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
              .has_value());
      pointer->declared_type = base::ScalarType::B64;
    }
    selected->tensor->mode = TensorAccessMode::Im2colNoOffs;
    EXPECT_FALSE(copy->check(context).has_value()) << rank;
    selected->tensor->mode = TensorAccessMode::Tiled;
    EXPECT_TRUE(copy->check(context).has_value()) << rank;
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
