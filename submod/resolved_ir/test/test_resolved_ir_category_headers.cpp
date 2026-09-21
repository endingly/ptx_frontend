#include "resolved_ir/checker/arithmetic.gen.hpp"
#include "resolved_ir/model/arithmetic.gen.hpp"
#include "resolved_ir/resolution/arithmetic.gen.hpp"

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

}  // namespace ptx_frontend::resolved_ir
