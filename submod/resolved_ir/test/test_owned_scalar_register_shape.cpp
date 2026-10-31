#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/model/arithmetic/add.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Supply same-element-type scalar, compact and whole-vector declarations. */
std::string scalar_shape_module(std::string_view operation) {
  return ".version 9.3\n.target sm_90\n.address_size 64\n"
         ".entry kernel() { .reg .u32 a, b, output, group<2>; "
         ".reg .v2 .u32 V; .reg .v4 .u32 Q; " +
         std::string(operation) + " }";
}

/** Both effective scalar slots reject a coherent real whole-vector declaration. */
TEST(OwnedScalarRegisterShape,
     RejectsArithmeticSourceAndDestinationWholeVectors) {
  auto owned = [] {
    const auto parsed =
        test_helpers::parseModule(scalar_shape_module("add.u32 output, a, b;"));
    EXPECT_TRUE(parsed && parsed.diagnostics.empty());
    return resolveAndValidateModule(*parsed);
  }();
  ASSERT_TRUE(owned) << owned.error().front().message;
  for (bool destination : {false, true}) {
    for (const auto name : {"V", "Q"}) {
      SCOPED_TRACE(destination ? "destination" : "source");
      SCOPED_TRACE(name);
      const auto operation =
          destination ? "add.u32 " + std::string(name) + ", a, b;"
                      : "add.u32 output, " + std::string(name) + ", b;";
      const auto parsed =
          test_helpers::parseModule(scalar_shape_module(operation));
      ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
      EXPECT_FALSE(resolveAndValidateModule(*parsed));
      auto copy = *owned;
      auto& function = copy.functions.front();
      function.body[0] = function.body[0]->clone();
      auto& arithmetic = dynamic_cast<AddIntegerNoSat&>(*function.body[0]);
      auto& reference =
          destination ? arithmetic.dst.value
                      : std::get<ResolvedRegisterRef>(arithmetic.src1.value);
      const auto scope = copy.symbols.symbol(function.symbol_id).owned_scope;
      ASSERT_TRUE(scope);
      const auto lookup = copy.symbols.lookup(*scope, name);
      ASSERT_TRUE(lookup);
      const auto width = name == std::string_view("V") ? 2u : 4u;
      reference = {.spelling = name,
                   .register_class = ResolvedRegisterClass::General,
                   .symbol_id = lookup->symbol,
                   .declared_type = ScalarType::U32,
                   .vector_width = static_cast<uint8_t>(width)};
      const checker::Context context{
          .target = {.ptx_version = {9, 3}, .sm_version = 90},
          .instruction_range = function.instruction_ranges[0]};
      const auto checked = arithmetic.check(context);
      ASSERT_FALSE(checked);
      EXPECT_EQ(checked.error().front().kind,
                checker::CheckDiagnosticKind::UnsupportedOperandShape);
      EXPECT_FALSE(validateModule(copy));
    }
  }
}

/** Compact members and explicitly selected components retain effective scalar shape. */
TEST(OwnedScalarRegisterShape,
     PreservesScalarMembersAndSelectedComponentsAfterAstDeath) {
  for (const auto operation :
       {"add.u32 output, a, b;", "add.u32 group1, group0, a;",
        "add.u32 V.x, Q.w, group1;", "add.u32 Q.y, V.g, Q.z;"}) {
    SCOPED_TRACE(operation);
    auto owned = [&] {
      const auto parsed =
          test_helpers::parseModule(scalar_shape_module(operation));
      EXPECT_TRUE(parsed && parsed.diagnostics.empty());
      return resolveAndValidateModule(*parsed);
    }();
    ASSERT_TRUE(owned) << owned.error().front().message;
    auto copy = *owned;
    auto& function = copy.functions.front();
    function.body[0] = function.body[0]->clone();
    const auto& arithmetic =
        dynamic_cast<const AddIntegerNoSat&>(*function.body[0]);
    EXPECT_FALSE(arithmetic.dst.value.vector_width);
    EXPECT_FALSE(
        std::get<ResolvedRegisterRef>(arithmetic.src1.value).vector_width);
    EXPECT_TRUE(validateModule(copy));
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
