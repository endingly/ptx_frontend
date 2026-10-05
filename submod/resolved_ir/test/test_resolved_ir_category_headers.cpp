#include <ptx_frontend/resolved_ir/model/arithmetic/add.gen.hpp>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection/set.gen.hpp>

#include <gtest/gtest.h>

namespace ptx_frontend::resolved_ir {

/** A narrow arithmetic leaf exposes its final form, checker, and resolver. */
TEST(ResolvedIrCategoryHeaders, ArithmeticCompilesWithoutAggregateModel) {
  static_assert(requires(const AddIntegerNoSat& instruction,
                         const checker::Context& context,
                         const syntax_ast::AstInstruction& ast) {
    instruction.check(context);
    resolveAdd(ast);
  });
}

/** A narrow comparison leaf needs no aggregate generated declarations. */
TEST(ResolvedIrCategoryHeaders, ComparisonCompilesWithoutAggregateModel) {
  static_assert(requires(const SetUnsigned& instruction,
                         const checker::Context& context,
                         const syntax_ast::AstInstruction& ast) {
    instruction.check(context);
    resolveSet(ast);
  });
}

}  // namespace ptx_frontend::resolved_ir
