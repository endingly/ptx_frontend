#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/redux/checker.gen.hpp>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/redux/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/redux/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse one standalone instruction for a generated-opcode test. */
syntax_ast::AstInstruction parse_instruction(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseInstruction();
  EXPECT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  return std::move(*ast);
}

TEST(SelectVariantRedux, SelectsReduxSyncFormsAndRejectsInvalidOnes) {
  const auto expect_variant = [](std::string_view source,
                                 Redux::VariantType expected) {
    const auto selected = selectVariant<Redux>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("redux.sync.add.u32 %r0, %r1, 0xffffffff;",
                 Redux::VariantType::SyncAdd);
  expect_variant("redux.sync.min.s32 %r0, %r1, %r2;",
                 Redux::VariantType::SyncMin);
  expect_variant("redux.sync.max.u32 %r0, %r1, %r2;",
                 Redux::VariantType::SyncMax);
  for (const std::string_view source : {
           "redux.sync.and.b32 %r0, %r1, 0xffffffff;",
           "redux.sync.or.b32 %r0, %r1, 0xffffffff;",
           "redux.sync.xor.b32 %r0, %r1, 0xffffffff;",
       }) {
    expect_variant(source, Redux::VariantType::SyncBoolean);
  }
  for (const std::string_view source : {
           "redux.sync.min.f32 %f0, %f1, 0xffffffff;",
           "redux.sync.min.abs.f32 %f0, %f1, 0xffffffff;",
           "redux.sync.min.NaN.f32 %f0, %f1, 0xffffffff;",
           "redux.sync.min.abs.NaN.f32 %f0, %f1, 0xffffffff;",
       }) {
    expect_variant(source, Redux::VariantType::SyncMinF32);
  }
  expect_variant("redux.sync.max.abs.NaN.f32 %f0, %f1, 0xffffffff;",
                 Redux::VariantType::SyncMaxF32);

  for (const std::string_view source : {
           "redux.sync.add.b32 %r0, %r1, 0xffffffff;",
           "redux.sync.and.u32 %r0, %r1, 0xffffffff;",
           "redux.sync.min.NaN.abs.f32 %f0, %f1, 0xffffffff;",
           "redux.sync.add.abs.u32 %r0, %r1, 0xffffffff;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(selectVariant<Redux>(parse_instruction(source)).has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
