#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <ptx_frontend/base/base.hpp>
#include <ptx_frontend/binding/ptx_symbol_table.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse one complete module and surface parser failures in the current test. */
std::optional<syntax_ast::AstModule> parseModule(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto module = parser.parseModule();
  if (!module || !module.diagnostics.empty()) {
    ADD_FAILURE() << (module.diagnostics.empty()
                          ? "PTX source did not parse."
                          : module.diagnostics.front().message);
    return std::nullopt;
  }
  return std::move(*module);
}

/** Count declaration diagnostics of one precise semantic category. */
size_t declarationCount(
    const std::vector<declaration_semantics::DeclarationDiagnostic>& diagnostics,
    declaration_semantics::DeclarationDiagnosticKind kind) {
  return std::ranges::count_if(
      diagnostics,
      [kind](const auto& diagnostic) { return diagnostic.kind == kind; });
}

/** Unknown `.reg` types fail before either used or unused operands are resolved. */
TEST(RegisterDeclarations, RejectsUnknownTypesAtTheirDeclarationTokens) {
  const auto ast = parseModule(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.entry invalid_register_types() {
  .reg .not_a_type %unused<2>;
  {
    .reg .also_not_a_type %used;
    mov.u32 %used, 1;
  }
  ret;
}
)ptx");
  ASSERT_TRUE(ast);
  const auto binding = binding::bindSymbols(*ast);
  const auto diagnostics =
      declaration_semantics::checkDeclarations(*ast, binding.table);

  EXPECT_EQ(declarationCount(
                diagnostics, declaration_semantics::DeclarationDiagnosticKind::
                                 UnknownRegisterDeclarationType),
            2u);
  EXPECT_EQ(declarationCount(
                diagnostics, declaration_semantics::DeclarationDiagnosticKind::
                                 UnsupportedRegisterDeclarationType),
            0u);
  std::vector<uint32_t> type_columns;
  for (const auto& diagnostic : diagnostics) {
    if (diagnostic.kind != declaration_semantics::DeclarationDiagnosticKind::
                               UnknownRegisterDeclarationType)
      continue;
    type_columns.push_back(diagnostic.range.start.column);
    EXPECT_NE(diagnostic.message.find("Unknown register declaration type"),
              std::string::npos);
  }
  std::ranges::sort(type_columns);
  EXPECT_EQ(type_columns, (std::vector<uint32_t>{8u, 10u}));

  const auto resolved = resolveModule(*ast);
  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(std::ranges::count_if(
                resolved.error(), [](const auto& diagnostic) {
                  return diagnostic.declaration_kind ==
                         declaration_semantics::DeclarationDiagnosticKind::
                             UnknownRegisterDeclarationType;
                }),
            2u);
}

/** Recognized packed and alternate instruction formats have a distinct diagnostic. */
TEST(RegisterDeclarations, RejectsInstructionOnlyFormatsAtDeclarationTime) {
  const auto ast = parseModule(R"ptx(
.entry invalid_register_formats() {
  .reg .bf16 %bfloat;
  .reg .tf32 %tensor;
  .reg .u8x4 %packed_integer;
  .reg .f32x2 %packed_float;
  .reg .e4m3x2 %packed_fp8;
  ret;
}
)ptx");
  ASSERT_TRUE(ast);
  const auto binding = binding::bindSymbols(*ast);
  const auto diagnostics =
      declaration_semantics::checkDeclarations(*ast, binding.table);

  EXPECT_EQ(declarationCount(
                diagnostics, declaration_semantics::DeclarationDiagnosticKind::
                                 UnsupportedRegisterDeclarationType),
            5u);
  EXPECT_EQ(declarationCount(
                diagnostics, declaration_semantics::DeclarationDiagnosticKind::
                                 UnknownRegisterDeclarationType),
            0u);
  for (const auto& diagnostic : diagnostics) {
    if (diagnostic.kind != declaration_semantics::DeclarationDiagnosticKind::
                               UnsupportedRegisterDeclarationType)
      continue;
    EXPECT_EQ(diagnostic.range.start.column, 8u);
    EXPECT_NE(diagnostic.message.find("instruction-only"), std::string::npos);
  }
}

/** Every modeled scalar spelling has the declaration classification in base metadata. */
TEST(RegisterDeclarations, ClassifiesEveryModeledScalarFromBaseMetadata) {
  const auto metadata = base::scalar_type_metadata();
  const auto scalar_types = magic_enum::enum_values<base::ScalarType>();
  ASSERT_EQ(metadata.size(), scalar_types.size() - 1);

  for (const auto scalar_type : scalar_types) {
    if (scalar_type == base::ScalarType::Invalid)
      continue;
    const auto* scalar_metadata = base::find_scalar_type_metadata(scalar_type);
    ASSERT_NE(scalar_metadata, nullptr)
        << magic_enum::enum_name(scalar_type);

    std::string source = ".entry scalar_declaration() { .reg ";
    source += scalar_metadata->source_spelling;
    source += " %value; ret; }";
    const auto ast = parseModule(source);
    ASSERT_TRUE(ast) << scalar_metadata->source_spelling;
    const auto* function =
        std::get_if<syntax_ast::AstFunction>(&ast->items.front());
    ASSERT_NE(function, nullptr);
    const auto* declaration = std::get_if<syntax_ast::AstVariableDeclaration>(
        &function->body.front());
    ASSERT_NE(declaration, nullptr);

    const auto binding = binding::bindSymbols(*ast);
    const auto diagnostics =
        declaration_semantics::checkDeclarations(*ast, binding.table);
    EXPECT_EQ(declarationCount(
                  diagnostics,
                  declaration_semantics::DeclarationDiagnosticKind::
                      UnknownRegisterDeclarationType),
              0u)
        << scalar_metadata->source_spelling;
    if (scalar_metadata->register_declaration_usage ==
        base::ScalarDeclarationUsage::Fundamental) {
      EXPECT_TRUE(diagnostics.empty()) << scalar_metadata->source_spelling;
      continue;
    }
    ASSERT_EQ(diagnostics.size(), 1u) << scalar_metadata->source_spelling;
    EXPECT_EQ(diagnostics.front().kind,
              declaration_semantics::DeclarationDiagnosticKind::
                  UnsupportedRegisterDeclarationType)
        << scalar_metadata->source_spelling;
    EXPECT_EQ(diagnostics.front().range, declaration->type.range)
        << scalar_metadata->source_spelling;
  }
}

/** Predicate and wide vector declarations reject only their invalid shapes. */
TEST(RegisterDeclarations, ValidatesPredicateAndVectorShapes) {
  const auto ast = parseModule(R"ptx(
.entry invalid_register_shapes() {
  .reg .v2 .pred %predicate_vector;
  .reg .v4 .f64 %wide_float_vector;
  .reg .v2 .b128 %wide_bit_vector;
  ret;
}
)ptx");
  ASSERT_TRUE(ast);
  const auto binding = binding::bindSymbols(*ast);
  const auto diagnostics =
      declaration_semantics::checkDeclarations(*ast, binding.table);

  EXPECT_EQ(declarationCount(
                diagnostics, declaration_semantics::DeclarationDiagnosticKind::
                                 InvalidRegisterDeclarationShape),
            3u);
  for (const auto& diagnostic : diagnostics) {
    if (diagnostic.kind != declaration_semantics::DeclarationDiagnosticKind::
                               InvalidRegisterDeclarationShape)
      continue;
    EXPECT_EQ(diagnostic.range.start.column, 8u);
  }
}

/** Externally constructed declarations cannot turn an unknown vector token into `.v4`. */
TEST(RegisterDeclarations, RejectsExternallyConstructedVectorWidths) {
  auto ast = parseModule(R"ptx(
.entry externally_constructed_vector() {
  .reg .v2 .u32 %vector;
  ret;
}
)ptx");
  ASSERT_TRUE(ast);
  auto* function = std::get_if<syntax_ast::AstFunction>(&ast->items.front());
  ASSERT_NE(function, nullptr);
  auto* declaration =
      std::get_if<syntax_ast::AstVariableDeclaration>(&function->body.front());
  ASSERT_NE(declaration, nullptr);
  ASSERT_TRUE(declaration->vector_type.has_value());
  declaration->vector_type->text = ".v8";

  const auto binding = binding::bindSymbols(*ast);
  const auto diagnostics =
      declaration_semantics::checkDeclarations(*ast, binding.table);
  EXPECT_EQ(declarationCount(
                diagnostics, declaration_semantics::DeclarationDiagnosticKind::
                                 InvalidRegisterDeclarationShape),
            1u);
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics.front().range, declaration->vector_type->range);
  EXPECT_NE(diagnostics.front().message.find(".v2 or .v4"),
            std::string::npos);
}

/** Parser grammar keeps compact register groups distinct from arrays and initializers. */
TEST(RegisterDeclarations, RejectsArraysAndInitializersOnParameterizedGroups) {
  for (const std::string_view source : {
           ".entry invalid_group_array() { .reg .b32 %r<2>[2]; }",
           ".entry invalid_group_initializer() { .reg .b32 %r<2> = 1; }",
       }) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast.has_value());
    ASSERT_FALSE(ast.diagnostics.empty());
    EXPECT_TRUE(ast.diagnostics.front().message.find("parameterized variable "
                                                     "names cannot") !=
                std::string::npos)
        << source;
  }
}

/** Fundamental register forms remain valid in headers, groups, and nested blocks. */
TEST(RegisterDeclarations, AcceptsFundamentalFormsAndRegisterFormals) {
  const auto ast = parseModule(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.func register_forms(.reg .pred %formal_pred, .reg .f16x2 %formal_half) {
  .reg .u8 %unsigned8;
  .reg .u16 %unsigned16;
  .reg .u32 %used, %unused;
  .reg .u64 %unsigned64;
  .reg .s8 %signed8;
  .reg .s16 %signed16;
  .reg .s32 %signed32;
  .reg .s64 %signed64;
  .reg .b8 %bytes<2>;
  .reg .b16 %bits16;
  .reg .b32 %bits32;
  .reg .b64 %bits64;
  .reg .b128 %bits;
  .reg .f16 %half;
  .reg .f16x2 %half_pairs<2>;
  .reg .v4 .f32 %float_vector;
  .reg .v2 .f64 %double_vector;
  .reg .pred %predicate;
  mov.u32 %used, 1;
  {
    .reg .b64 %nested<2>;
    .reg .s8 %nested_signed;
  }
  ret;
}
)ptx");
  ASSERT_TRUE(ast);
  const auto binding = binding::bindSymbols(*ast);
  const auto diagnostics =
      declaration_semantics::checkDeclarations(*ast, binding.table);
  EXPECT_TRUE(binding.diagnostics.empty());
  EXPECT_TRUE(diagnostics.empty());

  const auto resolved = resolveModule(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
