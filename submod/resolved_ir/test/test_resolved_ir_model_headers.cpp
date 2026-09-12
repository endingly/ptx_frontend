#include <ptx_frontend/resolved_ir/ptx_resolved_ir_foundation.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_model.hpp>

#include <gtest/gtest.h>
#include <concepts>

template <typename T>
concept CompleteType = requires { sizeof(T); };

static_assert(!CompleteType<ptx_frontend::syntax_ast::AstModule>);

namespace ptx_frontend::resolved_ir {

/** Verify model-only headers provide owned records without parser declarations. */
TEST(ResolvedIrModelHeaders, ExposeOwnedModelRecords) {
  ResolvedRegisterRef register_ref{
      .spelling = "%r0", .register_class = ResolvedRegisterClass::General};
  ResolvedCallLiteral literal{
      .spelling = "1", .kind = syntax_ast::AstImmediateKind::DecimalInteger};
  EXPECT_EQ(register_ref.spelling, "%r0");
  EXPECT_EQ(literal.spelling, "1");
}

/** Verify the model aggregate retains handwritten module containers. */
TEST(ResolvedIrModelHeaders, ExposeHandwrittenModuleContainers) {
  ResolvedModule module{};
  module.functions.emplace_back();
  EXPECT_TRUE(module.functions.front().body.empty());
}

}  // namespace ptx_frontend::resolved_ir
