#include <ptx_frontend/resolved_ir/checker/arithmetic.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic.gen.hpp>
#include <ptx_frontend/resolved_ir/resolution/arithmetic.gen.hpp>

#include <ptx_frontend/resolved_ir/checker/comparison_and_selection.gen.hpp>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection.gen.hpp>
#include <ptx_frontend/resolved_ir/resolution/comparison_and_selection.gen.hpp>

#include <gtest/gtest.h>

namespace ptx_frontend::resolved_ir {

/** Verify category headers expose model and specialization declarations alone. */
TEST(ResolvedIrCategoryHeaders, ArithmeticCompilesWithoutAggregateModel) {
  static_assert(requires(const Add& instruction,
                         const checker::Context& context,
                         const syntax_ast::AstInstruction& ast) {
    checker::check(instruction, context);
    resolve<Add>(ast);
  });
}

/** Comparison instructions can use their category declarations directly. */
TEST(ResolvedIrCategoryHeaders, ComparisonCompilesWithoutAggregateModel) {
  static_assert(requires(const Set& instruction,
                         const checker::Context& context,
                         const syntax_ast::AstInstruction& ast) {
    checker::check(instruction, context);
    resolve<Set>(ast);
  });
}

}  // namespace ptx_frontend::resolved_ir
