#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** One scalar register-formal arithmetic case and its operand type. */
struct RegisterFormalArithmeticCase {
  /** PTX type spelling used by the formal declarations. */
  std::string_view type_spelling;
  /** Arithmetic instruction suffix, including any required rounding mode. */
  std::string_view instruction_suffix;
  /** Literal spelling that is valid for the selected instruction type. */
  std::string_view immediate;
  /** Expected resolved scalar type retained on both register references. */
  ScalarType scalar_type;
};

/** Assert the bound type, class, shape, and identity of two scalar formals. */
void expectBoundFormalMetadata(
    const ResolvedRegisterRef& result, const ResolvedRegisterRef& input,
    const RegisterFormalArithmeticCase& test_case,
    const binding::SymbolLookup& result_symbol,
    const binding::SymbolLookup& input_symbol) {
  EXPECT_EQ(result.register_class, ResolvedRegisterClass::General);
  EXPECT_EQ(input.register_class, ResolvedRegisterClass::General);
  EXPECT_EQ(result.declared_type, test_case.scalar_type);
  EXPECT_EQ(input.declared_type, test_case.scalar_type);
  EXPECT_FALSE(result.vector_width.has_value());
  EXPECT_FALSE(input.vector_width.has_value());
  EXPECT_EQ(result.symbol_id, result_symbol.symbol);
  EXPECT_EQ(input.symbol_id, input_symbol.symbol);
  EXPECT_NE(result.symbol_id, input.symbol_id);
}

/** Read and write register formals in a function body for each supported width. */
TEST(RegisterFormals, ResolveArithmeticReadsAndWritesWithBoundIdentity) {
  constexpr std::array cases = {
      RegisterFormalArithmeticCase{"u32", "u32", "1", ScalarType::U32},
      RegisterFormalArithmeticCase{"b32", "u32", "1", ScalarType::B32},
      RegisterFormalArithmeticCase{"f32", "rn.f32", "0f3f800000",
                                  ScalarType::F32},
      RegisterFormalArithmeticCase{"u64", "u64", "1", ScalarType::U64},
  };
  const checker::Context context{.target = {.ptx_version = {8, 0},
                                             .sm_version = 80}};

  for (const RegisterFormalArithmeticCase& test_case : cases) {
    const std::string source =
        ".version 8.0\n.target sm_80\n.address_size 64\n.func (.reg ." +
        std::string(test_case.type_spelling) + " %result) formal(.reg ." +
        std::string(test_case.type_spelling) + " %input) {\n  add." +
        std::string(test_case.instruction_suffix) + " %result, %input, " +
        std::string(test_case.immediate) + ";\n  ret;\n}\n";
    SCOPED_TRACE(source);

    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
    ASSERT_TRUE(ast.diagnostics.empty());
    const auto bound = binding::bindSymbols(*ast);
    ASSERT_TRUE(bound.diagnostics.empty());

    const auto resolved = resolveModule(*ast);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    ASSERT_EQ(resolved->functions.size(), 1u);
    ASSERT_EQ(resolved->functions.front().body.size(), 2u);
    const Add& add = std::get<Add>(resolved->functions.front().body.front());
    EXPECT_TRUE(checker::check(add, context).has_value());

    const auto& syntax_function =
        std::get<syntax_ast::AstFunction>(ast->items.back());
    const auto function_scope = bound.table.functionScope(syntax_function.range);
    ASSERT_TRUE(function_scope.has_value());
    const auto result_symbol = bound.table.lookup(*function_scope, "%result");
    const auto input_symbol = bound.table.lookup(*function_scope, "%input");
    ASSERT_TRUE(result_symbol.has_value());
    ASSERT_TRUE(input_symbol.has_value());
    EXPECT_EQ(bound.table.symbol(result_symbol->symbol).kind,
              binding::SymbolKind::ReturnParameter);
    EXPECT_EQ(bound.table.symbol(input_symbol->symbol).kind,
              binding::SymbolKind::InputParameter);

    if (test_case.scalar_type == ScalarType::F32) {
      const auto* float_add = std::get_if<Add::FloatF32>(&add.variant);
      ASSERT_NE(float_add, nullptr);
      expectBoundFormalMetadata(
          float_add->dst.value,
          std::get<ResolvedRegisterRef>(float_add->src1.value), test_case,
          *result_symbol, *input_symbol);
    } else {
      const auto* integer_add = std::get_if<Add::IntegerNoSat>(&add.variant);
      ASSERT_NE(integer_add, nullptr);
      expectBoundFormalMetadata(
          integer_add->dst.value,
          std::get<ResolvedRegisterRef>(integer_add->src1.value), test_case,
          *result_symbol, *input_symbol);
    }
  }
}

/** Predicate formals retain predicate class while serving as body operands. */
TEST(RegisterFormals, ResolvePredicateReadAndWrite) {
  constexpr std::string_view source = R"ptx(.version 8.0
.target sm_80
.address_size 64
.func (.reg .pred %result) formal(.reg .pred %input) {
  bar.red.and.pred %result, 0, %input;
  ret;
}
)ptx";
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  ASSERT_TRUE(ast.diagnostics.empty());

  const auto resolved = resolveModule(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.front().body.size(), 2u);
  const auto& bar = std::get<Bar>(resolved->functions.front().body.front());
  const auto& reduction = std::get<Bar::RedAndPred>(bar.variant);
  const auto& operands =
      std::get<Bar::RedAndPred::WithoutThreadCountOperands>(reduction.operands);
  EXPECT_EQ(operands.dst.value.register_ref.register_class,
            ResolvedRegisterClass::Predicate);
  EXPECT_EQ(operands.predicate.value.register_ref.register_class,
            ResolvedRegisterClass::Predicate);
  EXPECT_EQ(operands.dst.value.register_ref.declared_type, ScalarType::Pred);
  EXPECT_EQ(operands.predicate.value.register_ref.declared_type,
            ScalarType::Pred);
}

/** Parameter-space formals remain invalid in arithmetic register operands. */
TEST(RegisterFormals, RejectParameterSpaceFormalAsArithmeticRegister) {
  constexpr std::string_view source = R"ptx(.version 8.0
.target sm_80
.address_size 64
.func (.reg .u32 %result) formal(.param .u32 input) {
  add.u32 %result, input, 1;
  ret;
}
)ptx";
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  ASSERT_TRUE(ast.diagnostics.empty());

  const auto resolved = resolveModule(*ast);
  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().stage(),
            ResolveDiagnosticStage::Resolution);
  EXPECT_EQ(resolved.error().front().message,
            "Symbol 'input' is not a .reg variable.");
}

/** Module resolution projects checker rejection of a mismatched register formal. */
TEST(RegisterFormals, ReportsProjectedTypeMismatch) {
  constexpr std::string_view source = R"ptx(.version 8.0
.target sm_80
.address_size 64
.func (.reg .u64 %result) formal(.reg .u32 %input) {
  add.u32 %result, %input, 1;
  ret;
}
)ptx";
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast.has_value()) << ast.diagnostics.front().message;
  ASSERT_TRUE(ast.diagnostics.empty());

  const auto resolved = resolveModule(*ast);
  ASSERT_FALSE(resolved.has_value());
  ASSERT_EQ(resolved.error().size(), 1u);
  EXPECT_EQ(resolved.error().front().stage(), ResolveDiagnosticStage::Checking);
  EXPECT_EQ(resolved.error().front().checker_kind,
            checker::CheckDiagnosticKind::OperandTypeMismatch);
  EXPECT_EQ(resolved.error().front().message,
            "Register operand 'dst' has declared type 'U64' but instruction "
            "type source 'type' is 'U32'.");
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
