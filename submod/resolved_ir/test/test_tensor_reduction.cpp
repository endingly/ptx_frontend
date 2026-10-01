#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include "test_instruction_access.hpp"
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Build a self-contained tensor reduction with reusable declared addresses. */
std::string reduction_module(
    std::string_view instruction,
    std::string_view map_declaration = ".global .align 64 .b8 tensor_map[128];",
    std::string_view source_declaration = ".shared .align 16 .b8 src[1024];",
    std::string_view target = "sm_90", std::string_view version = "9.3") {
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) + "\n.address_size 64\n" +
         std::string(map_declaration) + "\n" + std::string(source_declaration) +
         "\n.entry kernel() {\n.reg .s32 %r<5>;\n" + std::string(instruction) +
         "\n}\n";
}

/** Match direct checker contexts to the target catalogue, including identity. */
checker::Context reduction_context(std::string_view target,
                                   checker::PtxVersion version = {9, 3}) {
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

/** Every rank, operation, and tile spelling owns the same closed form. */
TEST(TensorReduction, AllRankOperationIdentitiesAndTileProvenance) {
  constexpr std::array<std::string_view, 8> operations{
      "add", "min", "max", "inc", "dec", "and", "or", "xor"};
  constexpr std::array<TensorReductionOp, 8> identities{
      TensorReductionOp::Add, TensorReductionOp::Min, TensorReductionOp::Max,
      TensorReductionOp::Inc, TensorReductionOp::Dec, TensorReductionOp::And,
      TensorReductionOp::Or,  TensorReductionOp::Xor};
  for (int rank = 1; rank <= 5; ++rank) {
    std::string coordinates = "%r0";
    for (int index = 1; index < rank; ++index)
      coordinates += ", %r" + std::to_string(index);
    for (size_t operation_index = 0; operation_index < operations.size();
         ++operation_index) {
      const auto operation = operations[operation_index];
      for (const bool explicit_tile : {false, true}) {
        const std::string instruction =
            "cp.reduce.async.bulk.tensor." + std::to_string(rank) +
            "d.global.shared::cta." + std::string(operation) +
            (explicit_tile ? ".tile" : "") + ".bulk_group [tensor_map, {" +
            coordinates + "}], [src];";
        std::optional<ResolvedModule> owned;
        {
          const auto parsed =
              test_helpers::parseModule(reduction_module(instruction));
          ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
          auto resolved = resolveModuleOnly(*parsed);
          ASSERT_TRUE(resolved.has_value())
              << resolved.error().front().message << ": " << instruction;
          owned.emplace(std::move(*resolved));
        }
        ASSERT_EQ(owned->functions.front().body.size(), 1u);
        const auto& copy =
            test_ir_access::get<Cp>(owned->functions.front().body.front());
        EXPECT_TRUE(
            checker::check(copy, reduction_context("sm_90")).has_value())
            << instruction;
        EXPECT_TRUE(validateModule(
                        *owned, ModuleValidationPolicy::RequireCompleteContext)
                        .has_value())
            << instruction;
        std::visit(
            [&](const auto& selected) {
              if constexpr (requires {
                              selected.tensor;
                              selected.tile;
                              selected.tensor_reduction_op;
                            }) {
                EXPECT_EQ(selected.tensor_reduction_op,
                          identities[operation_index]);
                EXPECT_EQ(selected.completion_kind,
                          base::AsyncCompletionKind::BulkGroup);
                EXPECT_EQ(selected.tensor.value.coordinates.elements.size(),
                          static_cast<size_t>(rank));
                EXPECT_EQ(selected.tensor.value.rank,
                          static_cast<TensorRank>(rank));
                EXPECT_EQ(selected.tile.value, explicit_tile);
                EXPECT_EQ(selected.tile.locs.empty(), !explicit_tile);
              } else {
                ADD_FAILURE() << "Unexpected tensor-reduction alternative";
              }
            },
            copy.variant);
      }
    }
  }
}

/** A closed query states conditional descriptor compatibility only. */
TEST(TensorReduction, ConditionalElementTypes) {
  using Type = base::ScalarType;
  for (const auto op : {TensorReductionOp::Add, TensorReductionOp::Min,
                        TensorReductionOp::Max}) {
    EXPECT_TRUE(tensor_reduction_accepts_element_type(op, Type::U32));
    EXPECT_FALSE(tensor_reduction_accepts_element_type(op, Type::B32));
  }
  EXPECT_TRUE(
      tensor_reduction_accepts_element_type(TensorReductionOp::Add, Type::F32));
  EXPECT_FALSE(
      tensor_reduction_accepts_element_type(TensorReductionOp::Min, Type::F32));
  EXPECT_TRUE(
      tensor_reduction_accepts_element_type(TensorReductionOp::Max, Type::S64));
  EXPECT_FALSE(
      tensor_reduction_accepts_element_type(TensorReductionOp::Add, Type::S64));
  for (const auto op : {TensorReductionOp::Inc, TensorReductionOp::Dec}) {
    EXPECT_TRUE(tensor_reduction_accepts_element_type(op, Type::U32));
    EXPECT_FALSE(tensor_reduction_accepts_element_type(op, Type::S32));
  }
  for (const auto op : {TensorReductionOp::And, TensorReductionOp::Or,
                        TensorReductionOp::Xor}) {
    EXPECT_TRUE(tensor_reduction_accepts_element_type(op, Type::B32));
    EXPECT_TRUE(tensor_reduction_accepts_element_type(op, Type::B64));
    EXPECT_FALSE(tensor_reduction_accepts_element_type(op, Type::U32));
  }
  EXPECT_FALSE(tensor_reduction_accepts_element_type(
      static_cast<TensorReductionOp>(255), Type::U32));
  EXPECT_FALSE(tensor_reduction_accepts_element_type(TensorReductionOp::Add,
                                                     static_cast<Type>(255)));
}

/** Illegal directions, qualifiers, and operand shapes must remain unsupported. */
TEST(TensorReduction, RejectsAdjacentForms) {
  constexpr std::array<std::string_view, 9> invalid{
      "cp.reduce.async.bulk.tensor.1d.shared::cluster.global.add.bulk_group "
      "[tensor_map, {0}], [src];",
      "cp.reduce.async.bulk.tensor.1d.global.shared::cta.add.mbarrier::"
      "complete_tx::bytes [tensor_map, {0}], [src];",
      "cp.reduce.async.bulk.tensor.1d.global.shared::cta.add.tile.mbarrier::"
      "complete_tx::bytes [tensor_map, {0}], [src];",
      "cp.reduce.async.bulk.tensor.1d.global.shared::cta.add.noftz.bulk_group "
      "[tensor_map, {0}], [src];",
      "cp.reduce.async.bulk.tensor.1d.global.shared::cta.add.u32.bulk_group "
      "[tensor_map, {0}], [src];",
      "cp.reduce.async.bulk.tensor.1d.global.shared::cta.add.cta_group::2.bulk_"
      "group [tensor_map, {0}], [src];",
      "cp.reduce.async.bulk.tensor.1d.global.shared::cta.add.multicast::"
      "cluster.bulk_group [tensor_map, {0}], [src];",
      "cp.reduce.async.bulk.tensor.1d.global.shared::cta.add.bulk_group "
      "[tensor_map, {0}], [src], [src];",
      "cp.reduce.async.bulk.tensor.1d.global.shared::cta.add.bulk_group "
      "[tensor_map, {0, 1}], [src];",
  };
  for (const auto instruction : invalid) {
    const auto parsed =
        test_helpers::parseModule(reduction_module(instruction));
    if (!parsed)
      continue;
    EXPECT_FALSE(resolveModuleOnly(*parsed).has_value()) << instruction;
  }
}

/** The target catalogue and syntax version gate every reduction operation. */
TEST(TensorReduction, VersionAndTargetBoundaries) {
  const auto parsed = test_helpers::parseModule(reduction_module(
      "cp.reduce.async.bulk.tensor.2d.global.shared::cta.xor.bulk_group "
      "[tensor_map, {%r0, 0}], [src];"));
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveModuleOnly(*parsed);
  ASSERT_TRUE(resolved.has_value());
  const auto& copy =
      test_ir_access::get<Cp>(resolved->functions.front().body.front());
  EXPECT_TRUE(
      checker::check(copy, reduction_context("sm_90", {8, 0})).has_value());
  EXPECT_FALSE(
      checker::check(copy, reduction_context("sm_90", {7, 9})).has_value());
  EXPECT_FALSE(
      checker::check(copy, reduction_context("sm_89", {9, 3})).has_value());
}

/** Reduction write coordinates use the converted signed-32 value. */
TEST(TensorReduction, NarrowedCoordinateSignBoundaries) {
  constexpr std::array<std::string_view, 5> coordinates{
      "4294967296", "-4294967296", "-2147483649", "4294967295",
      "18446744073709551615"};
  for (size_t index = 0; index < coordinates.size(); ++index) {
    const std::string instruction =
        "cp.reduce.async.bulk.tensor.1d.global.shared::cta.add.bulk_group "
        "[tensor_map, {" +
        std::string(coordinates[index]) + "}], [src];";
    const auto parsed =
        test_helpers::parseModule(reduction_module(instruction));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    EXPECT_EQ(resolveModule(*parsed).has_value(), index < 3)
        << coordinates[index];
  }
}

/** Source-backed coordinates and both pointer roles survive AST destruction. */
TEST(TensorReduction, OwnedCoordinatesAndAddressMutation) {
  std::optional<ResolvedModule> owned;
  {
    const auto parsed = test_helpers::parseModule(reduction_module(
        "cp.reduce.async.bulk.tensor.2d.global.shared::cta.add.bulk_group "
        "[tensor_map, {4294967296, %r0}], [src];"));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModuleOnly(*parsed);
    ASSERT_TRUE(resolved.has_value());
    owned.emplace(std::move(*resolved));
  }
  auto& copy = test_ir_access::get<Cp>(owned->functions.front().body.front());
  auto& reduction = std::get<Cp::ReduceAsyncBulkTensor2dAdd>(copy.variant);
  auto& immediate = std::get<ResolvedImmediate>(
      reduction.tensor.value.coordinates.elements[0]);
  auto& coordinate_register = std::get<ResolvedRegisterRef>(
      reduction.tensor.value.coordinates.elements[1]);
  auto& map_symbol = std::get<ResolvedSymbolRef>(
      reduction.tensor.value.tensor_map.address.base);
  auto& source_symbol = std::get<ResolvedSymbolRef>(reduction.src.value.base);
  const auto context = reduction_context("sm_90");
  ASSERT_TRUE(checker::check(copy, context).has_value());
  ASSERT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
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
  EXPECT_FALSE(immediate.is_negative);
  ASSERT_TRUE(map_symbol.symbol_id);
  ASSERT_TRUE(source_symbol.symbol_id);
  ASSERT_TRUE(coordinate_register.symbol_id);

  immediate.bits = 1;
  EXPECT_FALSE(checker::check(copy, context).has_value());
  immediate.bits = 0;
  immediate.integer_source_bits.reset();
  EXPECT_FALSE(checker::check(copy, context).has_value());
  immediate.integer_source_bits = 0x100000000ULL;
  immediate.type = base::ScalarType::U32;
  EXPECT_FALSE(checker::check(copy, context).has_value());
  immediate.type = base::ScalarType::S32;
  immediate.bits = 0xffffffffULL;
  immediate.integer_source_bits = 0xffffffffULL;
  EXPECT_FALSE(checker::check(copy, context).has_value());
  immediate.bits = 0;
  immediate.integer_source_bits = 0x100000000ULL;

  coordinate_register.declared_type = base::ScalarType::U64;
  EXPECT_FALSE(checker::check(copy, context).has_value());
  coordinate_register.declared_type = base::ScalarType::S32;
  const auto original_space = map_symbol.address_state_space;
  map_symbol.address_state_space = base::DeclarationStateSpace::Shared;
  EXPECT_FALSE(checker::check(copy, context).has_value());
  map_symbol.address_state_space = original_space;
  const auto original_alignment = map_symbol.address_alignment;
  map_symbol.address_alignment = 32;
  EXPECT_FALSE(checker::check(copy, context).has_value());
  map_symbol.address_alignment = original_alignment;
  const auto source_alignment = source_symbol.address_alignment;
  source_symbol.address_alignment = 8;
  EXPECT_FALSE(checker::check(copy, context).has_value());
  source_symbol.address_alignment = source_alignment;
  const auto source_space = source_symbol.address_state_space;
  source_symbol.address_state_space = base::DeclarationStateSpace::Global;
  EXPECT_FALSE(checker::check(copy, context).has_value());
  source_symbol.address_state_space = source_space;

  const auto map_id = map_symbol.symbol_id;
  map_symbol.symbol_id = source_symbol.symbol_id;
  EXPECT_FALSE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
  map_symbol.symbol_id = map_id;
  EXPECT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
}

/** Scalar pointer shape and type are rechecked on owned reduction operands. */
TEST(TensorReduction, OwnedPointerRegisterMutation) {
  std::optional<ResolvedModule> owned;
  {
    const auto parsed = test_helpers::parseModule(reduction_module(
        ".reg .b64 %map; .reg .b64 %src; "
        "cp.reduce.async.bulk.tensor.1d.global.shared::cta.inc.bulk_group "
        "[%map, {0}], [%src];",
        "", ""));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModuleOnly(*parsed);
    ASSERT_TRUE(resolved.has_value());
    owned.emplace(std::move(*resolved));
  }
  auto& copy = test_ir_access::get<Cp>(owned->functions.front().body.front());
  auto& reduction = std::get<Cp::ReduceAsyncBulkTensor1dInc>(copy.variant);
  auto& map = std::get<ResolvedRegisterRef>(
      reduction.tensor.value.tensor_map.address.base);
  auto& source = std::get<ResolvedRegisterRef>(reduction.src.value.base);
  const auto context = reduction_context("sm_90");
  ASSERT_TRUE(checker::check(copy, context).has_value());
  for (auto* pointer : {&map, &source}) {
    pointer->vector_width = 2;
    EXPECT_FALSE(checker::check(copy, context).has_value());
    EXPECT_FALSE(
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
            .has_value());
    pointer->vector_width.reset();
    pointer->declared_type = base::ScalarType::B16;
    EXPECT_FALSE(checker::check(copy, context).has_value());
    EXPECT_FALSE(
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
            .has_value());
    pointer->declared_type = base::ScalarType::B64;
  }
  EXPECT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
