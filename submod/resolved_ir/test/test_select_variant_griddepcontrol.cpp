#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/griddepcontrol/model.gen.hpp>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/griddepcontrol/resolution.gen.hpp>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/griddepcontrol/checker.gen.hpp>
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

TEST(SelectVariantGriddepcontrol, SelectsActionsAndRejectsInvalidForms) {
  const auto expect_variant = [](std::string_view source,
                                 Griddepcontrol::VariantType expected) {
    const auto selected =
        selectVariant<Griddepcontrol>(parse_instruction(source));
    ASSERT_TRUE(selected.has_value()) << selected.error().message;
    EXPECT_EQ(*selected, expected);
  };

  expect_variant("griddepcontrol.launch_dependents;",
                 Griddepcontrol::VariantType::LaunchDependents);
  expect_variant("griddepcontrol.wait;", Griddepcontrol::VariantType::Wait);

  for (const std::string_view source : {
           "griddepcontrol;",
           "griddepcontrol.launch_dependents.wait;",
           "griddepcontrol.wait.sync;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(
        selectVariant<Griddepcontrol>(parse_instruction(source)).has_value());
  }
  EXPECT_FALSE(
      resolve<Griddepcontrol>(parse_instruction("griddepcontrol.wait %r0;"))
          .has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
