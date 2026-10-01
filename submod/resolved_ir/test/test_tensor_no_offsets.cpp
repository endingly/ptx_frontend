#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>

#include "test_instruction_access.hpp"
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

/** Every rank and operation selects a distinct fixed-mode owned alternative. */
TEST(TensorNoOffsets, AllTwentySevenIdentities) {
  constexpr std::array<std::string_view, 8> operations{
      "add", "min", "max", "inc", "dec", "and", "or", "xor"};
  constexpr std::array<TensorReductionOp, 8> operation_ids{
      TensorReductionOp::Add, TensorReductionOp::Min, TensorReductionOp::Max,
      TensorReductionOp::Inc, TensorReductionOp::Dec, TensorReductionOp::And,
      TensorReductionOp::Or,  TensorReductionOp::Xor};
  std::set<size_t> alternative_indices;
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
      const auto& copy =
          test_ir_access::get<Cp>(owned->functions.front().body.front());
      alternative_indices.insert(copy.variant.index());
      EXPECT_TRUE(checker::check(copy, no_offset_context()).has_value())
          << instruction;
      EXPECT_TRUE(
          validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
              .has_value())
          << instruction;
      std::visit(
          [&](const auto& selected) {
            if constexpr (requires {
                            selected.tensor;
                            selected.src;
                          }) {
              EXPECT_EQ(selected.tensor.value.mode,
                        TensorAccessMode::Im2colNoOffs);
              EXPECT_EQ(selected.tensor.value.rank,
                        static_cast<TensorRank>(rank));
              EXPECT_EQ(selected.tensor.value.coordinates.elements.size(),
                        static_cast<size_t>(rank));
              EXPECT_EQ(selected.completion_kind,
                        base::AsyncCompletionKind::BulkGroup);
              if constexpr (requires { selected.tensor_reduction_op; }) {
                if (reduction)
                  EXPECT_EQ(selected.tensor_reduction_op,
                            operation_ids[operation_index]);
              } else {
                EXPECT_FALSE(reduction);
              }
            } else {
              ADD_FAILURE() << "Unexpected tensor no-offset alternative";
            }
          },
          copy.variant);
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
  const auto& copy =
      test_ir_access::get<Cp>(resolved->functions.front().body.front());
  EXPECT_TRUE(checker::check(copy, no_offset_context({8, 0})).has_value());
  EXPECT_FALSE(checker::check(copy, no_offset_context({7, 9})).has_value());
  EXPECT_FALSE(
      checker::check(copy, no_offset_context({9, 3}, "sm_89")).has_value());
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
  auto& copy = test_ir_access::get<Cp>(owned->functions.front().body.front());
  auto& selected =
      std::get<Cp::ReduceAsyncBulkTensor3dXorIm2colNoOffs>(copy.variant);
  auto& tensor = selected.tensor.value;
  auto& immediate =
      std::get<ResolvedImmediate>(tensor.coordinates.elements.front());
  const auto context = no_offset_context();
  ASSERT_TRUE(checker::check(copy, context).has_value());
  int reference_count = 0;
  owned->functions.front().body.front().visit_references(
      detail::OwnedReferenceSink{
          .state = &reference_count,
          .accept = [](void* state, detail::OwnedReferenceView) {
            ++*static_cast<int*>(state);
          }});
  EXPECT_GE(reference_count, 2);
  EXPECT_EQ(immediate.bits, 0u);
  EXPECT_EQ(immediate.integer_source_bits, 0x100000000ULL);
  tensor.mode = TensorAccessMode::Tiled;
  EXPECT_FALSE(checker::check(copy, context).has_value());
  EXPECT_FALSE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
  tensor.mode = static_cast<TensorAccessMode>(255);
  EXPECT_FALSE(checker::check(copy, context).has_value());
  tensor.mode = TensorAccessMode::Im2colNoOffs;
  immediate.bits = 1;
  EXPECT_FALSE(checker::check(copy, context).has_value());
  immediate.bits = 0;
  immediate.integer_source_bits.reset();
  EXPECT_FALSE(checker::check(copy, context).has_value());
  immediate.integer_source_bits = 0x100000000ULL;
  tensor.rank = TensorRank::Two;
  EXPECT_FALSE(checker::check(copy, context).has_value());
  tensor.rank = TensorRank::Three;
  EXPECT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
}

/** Resolver reports a malformed generated binding before constructing IR. */
TEST(TensorNoOffsets, MissingOrInvalidBindingModeDiagnosed) {
  const auto parsed = test_helpers::parseInstruction(
      "cp.async.bulk.tensor.3d.global.shared::cta.im2col_no_offs."
      "bulk_group [tensor_map, {0, 1, 2}], [src];");
  ASSERT_INSTRUCTION_PARSE_SUCCEEDS(parsed);
  const auto& canonical = Cp::get_resolved_descriptor();
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
    const auto fields =
        resolve_fields(*parsed, Cp::get_syntax_descriptor(), descriptor,
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
    auto& copy = test_ir_access::get<Cp>(owned->functions.front().body.front());
    const auto context = no_offset_context();
    std::visit(
        [&](auto& selected) {
          if constexpr (requires {
                          selected.tensor;
                          selected.src;
                        }) {
            auto& map = std::get<ResolvedSymbolRef>(
                selected.tensor.value.tensor_map.address.base);
            auto& source = std::get<ResolvedSymbolRef>(selected.src.value.base);
            ASSERT_TRUE(checker::check(copy, context).has_value());
            const auto map_alignment = map.address_alignment;
            map.address_alignment = 32;
            EXPECT_FALSE(checker::check(copy, context).has_value());
            EXPECT_FALSE(
                validateModule(*owned,
                               ModuleValidationPolicy::RequireCompleteContext)
                    .has_value());
            map.address_alignment = map_alignment;
            const auto source_alignment = source.address_alignment;
            source.address_alignment = 8;
            EXPECT_FALSE(checker::check(copy, context).has_value());
            source.address_alignment = source_alignment;
            const auto map_space = map.address_state_space;
            map.address_state_space = base::DeclarationStateSpace::Shared;
            EXPECT_FALSE(checker::check(copy, context).has_value());
            map.address_state_space = map_space;
            const auto source_space = source.address_state_space;
            source.address_state_space = base::DeclarationStateSpace::Global;
            EXPECT_FALSE(checker::check(copy, context).has_value());
            source.address_state_space = source_space;
            const auto map_id = map.symbol_id;
            map.symbol_id = source.symbol_id;
            EXPECT_FALSE(
                validateModule(*owned,
                               ModuleValidationPolicy::RequireCompleteContext)
                    .has_value());
            map.symbol_id = map_id;
            EXPECT_TRUE(
                validateModule(*owned,
                               ModuleValidationPolicy::RequireCompleteContext)
                    .has_value());
          } else {
            ADD_FAILURE() << "Unexpected no-offset tensor alternative";
          }
        },
        copy.variant);
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
    auto& copy = test_ir_access::get<Cp>(owned->functions.front().body.front());
    const auto context = no_offset_context();
    std::visit(
        [&](auto& selected) {
          if constexpr (requires {
                          selected.tensor;
                          selected.src;
                        }) {
            auto& map = std::get<ResolvedRegisterRef>(
                selected.tensor.value.tensor_map.address.base);
            auto& source =
                std::get<ResolvedRegisterRef>(selected.src.value.base);
            for (auto* pointer : {&map, &source}) {
              pointer->vector_width = 2;
              EXPECT_FALSE(checker::check(copy, context).has_value());
              EXPECT_FALSE(
                  validateModule(*owned,
                                 ModuleValidationPolicy::RequireCompleteContext)
                      .has_value());
              pointer->vector_width.reset();
              pointer->register_class = ResolvedRegisterClass::Predicate;
              EXPECT_FALSE(checker::check(copy, context).has_value());
              pointer->register_class = ResolvedRegisterClass::General;
              pointer->declared_type = base::ScalarType::B16;
              EXPECT_FALSE(checker::check(copy, context).has_value());
              pointer->declared_type = base::ScalarType::B64;
            }
            EXPECT_TRUE(checker::check(copy, context).has_value());
          } else {
            ADD_FAILURE() << "Unexpected no-offset pointer alternative";
          }
        },
        copy.variant);
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
    auto& copy = test_ir_access::get<Cp>(owned->functions.front().body.front());
    const auto context = no_offset_context();
    std::visit(
        [&](auto& selected) {
          if constexpr (requires {
                          selected.tensor;
                          selected.src;
                        }) {
            auto& map = std::get<ResolvedRegisterRef>(
                selected.tensor.value.tensor_map.address.base);
            auto& source =
                std::get<ResolvedRegisterRef>(selected.src.value.base);
            for (auto* pointer : {&map, &source}) {
              pointer->vector_width = 2;
              EXPECT_FALSE(checker::check(copy, context).has_value()) << rank;
              EXPECT_FALSE(
                  validateModule(*owned,
                                 ModuleValidationPolicy::RequireCompleteContext)
                      .has_value());
              pointer->vector_width.reset();
              pointer->declared_type = base::ScalarType::B16;
              EXPECT_FALSE(checker::check(copy, context).has_value()) << rank;
              EXPECT_FALSE(
                  validateModule(*owned,
                                 ModuleValidationPolicy::RequireCompleteContext)
                      .has_value());
              pointer->declared_type = base::ScalarType::B64;
            }
            selected.tensor.value.mode = TensorAccessMode::Im2colNoOffs;
            EXPECT_FALSE(checker::check(copy, context).has_value()) << rank;
            selected.tensor.value.mode = TensorAccessMode::Tiled;
            EXPECT_TRUE(checker::check(copy, context).has_value()) << rank;
          } else {
            ADD_FAILURE() << "Unexpected tiled store alternative";
          }
        },
        copy.variant);
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
