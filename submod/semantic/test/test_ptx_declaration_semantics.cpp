#include <gtest/gtest.h>

#include <algorithm>
#include <string_view>

#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::declaration_semantics {
namespace {

struct CheckedModule {
  binding::SymbolBinding binding;
  std::vector<DeclarationDiagnostic> diagnostics;
};

CheckedModule check(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto module = parser.parseModule();
  EXPECT_TRUE(module.has_value()) << module.error().message;
  auto binding = binding::bindSymbols(*module);
  auto diagnostics = checkDeclarations(*module, binding.table);
  return {std::move(binding), std::move(diagnostics)};
}

size_t diagnosticCount(const CheckedModule& result,
                       DeclarationDiagnosticKind kind) {
  return std::ranges::count_if(
      result.diagnostics,
      [kind](const auto& diagnostic) { return diagnostic.kind == kind; });
}

TEST(PtxDeclarationSemantics, AcceptsIncompleteAndInferredAggregates) {
  const CheckedModule result = check(R"ptx(
.global .u32 inferred[] = {1, 2, 3};
.global .s32 matrix[3][2] = {{1, 2}, {3}};
.global .v4 .f32 vector = {1.0, 2.0, 3.0};
.global .u64 pointer = generic(inferred) + 4;
.global .u32 masked = 0xff(inferred) + 1;
.global .u32 compact<2>;
.global .u32 compact;
)ptx");

  EXPECT_TRUE(result.binding.diagnostics.empty());
  EXPECT_TRUE(result.diagnostics.empty());
}

TEST(PtxDeclarationSemantics, ValidatesArrayDimensionsAndInitializerShape) {
  const CheckedModule result = check(R"ptx(
.global .u32 too_many[2] = {1, 2, 3};
.global .u32 wrong_shape[2][2] = {1, 2};
.global .u32 missing_size[];
.global .u32 inner_unsized[2][] = {{1}};
.global .u32 zero[0];
.global .u32 symbolic[too_many];
)ptx");

  EXPECT_EQ(diagnosticCount(
                result, DeclarationDiagnosticKind::ExcessInitializerElements),
            1u);
  EXPECT_EQ(diagnosticCount(
                result, DeclarationDiagnosticKind::InitializerShapeMismatch),
            2u);
  EXPECT_EQ(
      diagnosticCount(result, DeclarationDiagnosticKind::UnsizedArrayDimension),
      2u);
  EXPECT_EQ(
      diagnosticCount(result, DeclarationDiagnosticKind::InvalidArrayDimension),
      2u);
}

TEST(PtxDeclarationSemantics,
     EvaluatesTypedSignedAndUnsignedIntegerDimensions) {
  const CheckedModule result = check(R"ptx(
.global .u32 signed_sum[-1 + 2];
.global .u32 double_negative[-(-2)];
.global .u32 signed_comparison[-1 < 0];
.global .u32 unsigned_comparison[((.u64)-1 > 0) ? 2 : 0];
.global .u32 implicit_unsigned[0xffffffffffffffff > 0];
.global .u32 usual_conversion[-1 + 2U];
.global .u32 unsigned_remainder[((-1 % 3) == 0) ? 2 : 0];
.global .u32 signed_division[-6 / -3];
.global .u32 unsigned_complement[~0 > 0];
.global .u32 signed_shift[(-4 >> 1) + 3];
)ptx");

  EXPECT_TRUE(result.binding.diagnostics.empty());
  EXPECT_TRUE(result.diagnostics.empty());
}

TEST(PtxDeclarationSemantics, ValidatesInitializerExpressionTypes) {
  const CheckedModule result = check(R"ptx(
.global .u32 integer_from_float = 1.0;
.global .f32 float_from_integer = 1;
.global .s64 signed_pointer = generic(integer_from_float);
.global .u64 unsigned_pointer = generic(integer_from_float);
.global .f16 unsupported = 1.0;
.global .u32 invalid = generic(1);
.shared .u32 shared_value;
.global .u64 invalid_space = generic(shared_value);
)ptx");

  EXPECT_EQ(diagnosticCount(result,
                            DeclarationDiagnosticKind::InitializerTypeMismatch),
            4u);
  EXPECT_EQ(
      diagnosticCount(result,
                      DeclarationDiagnosticKind::InvalidInitializerExpression),
      2u);
}

TEST(PtxDeclarationSemantics,
     AcceptsCompatibleExternalAndFunctionDeclarations) {
  const CheckedModule result = check(R"ptx(
.extern .global .u32 external_values[4];
.extern .global .u32 external_values[2 * 2];
.func (.reg .u32 result) helper(.reg .u32 input);
.func (.reg .u32 output) helper(.reg .u32 value) { ret; }
)ptx");

  EXPECT_TRUE(result.binding.diagnostics.empty());
  EXPECT_TRUE(result.diagnostics.empty());
  const auto helper =
      result.binding.table.lookup(result.binding.table.moduleScope(), "helper");
  ASSERT_TRUE(helper.has_value());
  ASSERT_TRUE(result.binding.table.symbol(helper->symbol).owned_scope);
  EXPECT_EQ(result.binding.table
                .scope(*result.binding.table.symbol(helper->symbol).owned_scope)
                .owner,
            helper->symbol);
}

TEST(PtxDeclarationSemantics, RejectsIncompatibleRedeclarationsAndDefinitions) {
  const CheckedModule result = check(R"ptx(
.extern .global .u32 value[4];
.extern .global .u64 value[4];
.global .u32 duplicate;
.global .u32 duplicate;
.func helper(.reg .u32 input);
.func helper(.reg .u64 input) { ret; }
.global .u32 collision;
.func collision();
.extern .func external_body() { ret; }
)ptx");

  EXPECT_TRUE(result.binding.diagnostics.empty());
  EXPECT_EQ(diagnosticCount(
                result, DeclarationDiagnosticKind::IncompatibleRedeclaration),
            3u);
  EXPECT_EQ(
      diagnosticCount(result, DeclarationDiagnosticKind::MultipleDefinitions),
      1u);
  EXPECT_EQ(diagnosticCount(result, DeclarationDiagnosticKind::InvalidLinkage),
            1u);
  for (const auto& diagnostic : result.diagnostics) {
    if (diagnostic.kind ==
            DeclarationDiagnosticKind::IncompatibleRedeclaration ||
        diagnostic.kind == DeclarationDiagnosticKind::MultipleDefinitions) {
      EXPECT_TRUE(diagnostic.previous_range.has_value());
    }
  }
}

TEST(PtxDeclarationSemantics, RequiresPositivePowerOfTwoAlignment) {
  const CheckedModule result = check(R"ptx(
.global .align 0 .u32 zero;
.global .align 3 .u32 non_power;
.global .align 16 .u32 valid;
.entry kernel(.param .align 6 .u32 input,
              .param .u64 .ptr .global .align 6 bad_pointer,
              .param .u64 .ptr .global .align 16 valid_pointer) { }
)ptx");

  EXPECT_EQ(diagnosticCount(result, DeclarationDiagnosticKind::InvalidAlignment),
            4u);
}

}  // namespace
}  // namespace ptx_frontend::declaration_semantics
