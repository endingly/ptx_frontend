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

/** Resolve one declared tensor form before returning its AST-independent IR. */
std::optional<ResolvedModule> owned_tensor_coordinate(
    std::string_view instruction, std::string_view coordinate_type = "s32") {
  const std::string source =
      ".version 9.3\n.target sm_110a\n.address_size 64\n"
      ".global .align 64 .b8 tensor_map[128];\n"
      ".shared .align 16 .b8 dst[1024];\n"
      ".shared .align 16 .b8 src[1024];\n"
      ".shared .align 8 .b64 mbar;\n"
      ".entry kernel() {\n.reg ." +
      std::string(coordinate_type) +
      " %r<5>;\n.reg .u16 %h<3>;\n.reg .b16 %mask;\n" +
      std::string(instruction) + "\n}\n";
  const auto parsed = test_helpers::parseModule(source);
  if (!parsed)
    return std::nullopt;
  auto resolved = resolveModuleOnly(*parsed);
  if (!resolved)
    return std::nullopt;
  return std::move(*resolved);
}

/** Use catalog identity and floor for direct checks on released IR. */
checker::Context tensor_coordinate_context() {
  const auto profile = base::find_target_profile("sm_110a");
  if (!profile)
    return {};
  return {
      .target = {.ptx_version = {9, 3},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
}

/** Find the first owned coordinate regardless of a form's optional layout. */
ResolvedRegisterRef* first_coordinate(Cp& copy) {
  ResolvedRegisterRef* result = nullptr;
  std::visit(
      [&](auto& selected) {
        auto find = [&](auto& payload) {
          if constexpr (requires { payload.tensor; }) {
            if (!payload.tensor.value.coordinates.elements.empty())
              result = std::get_if<ResolvedRegisterRef>(
                  &payload.tensor.value.coordinates.elements.front());
          }
        };
        if constexpr (requires { selected.operands; })
          std::visit(find, selected.operands);
        else
          find(selected);
      },
      copy.variant);
  return result;
}

/** All current tensor paths recheck scalar register metadata after AST death. */
TEST(TensorCoordinateOwned, ExistingReadWriteAndMulticastFormsRejectDamage) {
  constexpr std::array<std::string_view, 11> instructions{
      "cp.async.bulk.tensor.1d.shared::cta.global.tile.mbarrier::complete_tx::"
      "bytes [dst], [tensor_map, {%r0}], [mbar];",
      "cp.async.bulk.prefetch.tensor.1d.L2.global.tile [tensor_map, {%r0}];",
      "cp.async.bulk.tensor.1d.global.shared::cta.tile.bulk_group [tensor_map, "
      "{%r0}], [src];",
      "cp.reduce.async.bulk.tensor.1d.global.shared::cta.add.tile.bulk_group "
      "[tensor_map, {%r0}], [src];",
      "cp.async.bulk.tensor.3d.global.shared::cta.im2col_no_offs.bulk_group "
      "[tensor_map, {%r0, %r1, %r2}], [src];",
      "cp.reduce.async.bulk.tensor.3d.global.shared::cta.add.im2col_no_offs."
      "bulk_group [tensor_map, {%r0, %r1, %r2}], [src];",
      "cp.async.bulk.tensor.3d.shared::cluster.global.im2col.mbarrier::"
      "complete_tx::bytes [dst], [tensor_map, {%r0, %r1, %r2}], [mbar];",
      "cp.async.bulk.tensor.3d.shared::cluster.global.im2col.mbarrier::"
      "complete_tx::bytes [dst], [tensor_map, {%r0, %r1, %r2}], [mbar], {%h0};",
      "cp.async.bulk.tensor.1d.shared::cluster.global.tile.mbarrier::complete_"
      "tx::bytes.multicast::cluster [dst], [tensor_map, {%r0}], [mbar], %mask;",
      "cp.async.bulk.tensor.3d.shared::cluster.global.im2col.mbarrier::"
      "complete_tx::bytes.multicast::cluster [dst], [tensor_map, {%r0, %r1, "
      "%r2}], [mbar], {%h0}, %mask;",
      "cp.async.bulk.prefetch.tensor.3d.L2.global.im2col [tensor_map, {%r0, "
      "%r1, %r2}], {%h0};",
  };
  for (const auto instruction : instructions) {
    SCOPED_TRACE(std::string(instruction));
    auto owned = owned_tensor_coordinate(instruction);
    ASSERT_TRUE(owned);
    auto& copy = test_ir_access::get<Cp>(owned->functions.front().body.front());
    auto direct = [&] {
      return checker::check(copy, tensor_coordinate_context());
    };
    auto module = [&] {
      return validateModule(*owned,
                            ModuleValidationPolicy::RequireCompleteContext);
    };
    auto* coordinate = first_coordinate(copy);
    ASSERT_NE(coordinate, nullptr);
    ASSERT_TRUE(coordinate->symbol_id);
    ASSERT_TRUE(direct());
    ASSERT_TRUE(module());
    const auto saved = *coordinate;
    coordinate->vector_width = 2;
    EXPECT_FALSE(direct());
    EXPECT_FALSE(module());
    *coordinate = saved;
    coordinate->register_class = ResolvedRegisterClass::Predicate;
    EXPECT_FALSE(direct());
    EXPECT_FALSE(module());
    *coordinate = saved;
    coordinate->declared_type = ScalarType::B16;
    EXPECT_FALSE(direct());
    EXPECT_FALSE(module());
    *coordinate = saved;
    coordinate->declared_type.reset();
    EXPECT_FALSE(direct());
    EXPECT_FALSE(module());
    *coordinate = saved;
    EXPECT_TRUE(direct());
    EXPECT_TRUE(module());
    coordinate->symbol_id.reset();
    coordinate->declared_type.reset();
    EXPECT_TRUE(direct());
    *coordinate = saved;
    EXPECT_TRUE(direct());
    EXPECT_TRUE(module());
  }
}

/** Source-declared bit, unsigned, and signed scalar carriers remain legal. */
TEST(TensorCoordinateOwned, DeclaredScalar32FamiliesRemainAccepted) {
  constexpr std::string_view instruction =
      "cp.async.bulk.tensor.1d.shared::cta.global.tile.mbarrier::complete_tx::"
      "bytes [dst], [tensor_map, {%r0}], [mbar];";
  for (const auto type : {"b32", "u32", "s32"}) {
    auto owned = owned_tensor_coordinate(instruction, type);
    ASSERT_TRUE(owned) << type;
    auto& copy = test_ir_access::get<Cp>(owned->functions.front().body.front());
    EXPECT_TRUE(checker::check(copy, tensor_coordinate_context())) << type;
    EXPECT_TRUE(
        validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext))
        << type;
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
