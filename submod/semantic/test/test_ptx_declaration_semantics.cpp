#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <variant>

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
  EXPECT_TRUE(module.has_value()) << module.diagnostics.front().message;
  EXPECT_TRUE(module.diagnostics.empty());
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

/** Return a normalized constant when the contract field is not structural. */
std::optional<uint64_t> contractConstant(
    const std::optional<NormalizedNumericValue>& value) {
  if (!value)
    return std::nullopt;
  if (const auto* constant = std::get_if<uint64_t>(&*value))
    return *constant;
  return std::nullopt;
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

/** Opaque object handles cannot become initializer addresses through wrappers. */
TEST(PtxDeclarationSemantics, RejectsOpaqueObjectInitializerSymbols) {
  const CheckedModule result = check(R"ptx(
.global .texref texture;
.global .samplerref sampler;
.global .surfref surface;
.global .u32 global_data;
.const .u32 constant_data;
.func helper() { ret; }
.global .u64 direct = texture;
.global .u64 generic_sampler = generic(sampler);
.global .u8 masked_surface = 0xff(surface + 1);
.global .u64 global_address = global_data;
.global .u64 constant_address = generic(constant_data);
.global .u8 function_address = 0xff(helper);
)ptx");

  EXPECT_TRUE(result.binding.diagnostics.empty());
  EXPECT_EQ(diagnosticCount(result,
                            DeclarationDiagnosticKind::InvalidInitializerExpression),
            3u);
  for (const int32_t line : {8, 9, 10}) {
    EXPECT_TRUE(std::ranges::any_of(result.diagnostics,
                                    [line](const auto& diagnostic) {
                                      return diagnostic.kind ==
                                                 DeclarationDiagnosticKind::
                                                     InvalidInitializerExpression &&
                                             diagnostic.range.start.line == line;
                                    }));
  }
}

TEST(PtxDeclarationSemantics, ValidatesM11DirectiveBoundaries) {
  const CheckedModule result = check(R"ptx(
.version 8.0
.global .attribute(.unified(1, 2), .unified(3, 4)) .u32 global;
.func f() .noreturn { ret; }
.entry kernel() .blocksareclusters .language "", 11 {}
)ptx");
  EXPECT_GE(diagnosticCount(result,
                            DeclarationDiagnosticKind::UnsupportedDirectivePtxVersion),
            2u);
  EXPECT_GE(diagnosticCount(result,
                            DeclarationDiagnosticKind::InvalidDeclarationDirective),
            3u);
}

TEST(PtxDeclarationSemantics, RejectsM11AliasAndHeaderSemanticBoundaries) {
  struct Case {
    std::string_view source;
    DeclarationDiagnosticKind kind;
  };
  constexpr std::array cases = {
      Case{".version 9.3\n.alias alias_fn, missing;",
           DeclarationDiagnosticKind::InvalidFunctionAlias},
      Case{".version 9.3\n.func target() {}\n.alias alias_fn, target;",
           DeclarationDiagnosticKind::InvalidFunctionAlias},
      Case{".version 9.3\n.entry target() {}\n.alias alias_fn, target;",
           DeclarationDiagnosticKind::InvalidFunctionAlias},
      Case{".version 9.3\n.weak .func target() {}\n.alias alias_fn, target;",
           DeclarationDiagnosticKind::InvalidFunctionAlias},
      Case{".version 9.3\n.func alias_fn(.param .u32 x);\n.func target(.param .u64 x) {}\n.alias alias_fn, target;",
           DeclarationDiagnosticKind::InvalidFunctionAlias},
      Case{".version 9.3\n.func alias_fn;\n.func target() {}\n.alias alias_fn, target;\n.alias alias_fn, target;",
           DeclarationDiagnosticKind::InvalidFunctionAlias},
      Case{".version 9.3\n.shared .attribute(.managed) .u32 x;",
           DeclarationDiagnosticKind::InvalidDeclarationDirective},
      Case{".version 9.3\n.func (.param .u32 result) f() .noreturn {}",
           DeclarationDiagnosticKind::InvalidDeclarationDirective},
      Case{".version 8.9\n.func f() .abi_preserve 1 {}",
           DeclarationDiagnosticKind::UnsupportedDirectivePtxVersion},
      Case{".version 9.3\n.entry kernel() .blocksareclusters {}",
           DeclarationDiagnosticKind::InvalidDeclarationDirective},
      Case{".version 9.3\n.entry kernel() .language \"bad\", 11 {}",
           DeclarationDiagnosticKind::InvalidDeclarationDirective},
      Case{".version 9.3\n.func f() .abi_preserve 1;\n.func f() .abi_preserve 2 {}",
           DeclarationDiagnosticKind::IncompatibleRedeclaration},
      Case{".version 9.3\n.func f();\n.func f() .abi_preserve 1 {}",
           DeclarationDiagnosticKind::IncompatibleRedeclaration},
      Case{".version 9.3\n.func f() { { .shared .attribute(.managed, .managed) .u32 x; } }",
           DeclarationDiagnosticKind::InvalidDeclarationDirective},
  };
  for (const auto& test : cases) {
    const CheckedModule result = check(test.source);
    EXPECT_GT(diagnosticCount(result, test.kind), 0u) << test.source;
  }
}

TEST(PtxDeclarationSemantics, CanonicalizesEquivalentM11HeaderValues) {
  const CheckedModule result = check(R"ptx(
.version 9.3
.func .attribute(.unified(1, 2)) f() .language "PTX";
.func .attribute(.unified(0x1, 0x2)) f() .language 3 {}
)ptx");
  EXPECT_EQ(diagnosticCount(result, DeclarationDiagnosticKind::IncompatibleRedeclaration),
            0u);
}

/** `.unified` UUID halves use exact unsigned 64-bit source-token decoding. */
TEST(PtxDeclarationSemantics, ValidatesUnifiedAttributeUuidHalves) {
  const CheckedModule accepted = check(R"ptx(
.version 8.0
.target sm_90
.address_size 64
.global .attribute(.unified(0, 0xffffffffffffffffU)) .u32 global_uuid;
.func .attribute(.unified(18446744073709551615U, 0x0)) definition_uuid() {}
.func .attribute(.unified(0x0, 18446744073709551615)) prototype_uuid();
)ptx");
  EXPECT_TRUE(accepted.binding.diagnostics.empty());
  EXPECT_TRUE(accepted.diagnostics.empty());

  PtxSyntaxParser parser(R"ptx(
.version 8.0
.target sm_90
.address_size 64
.func .attribute(.unified(18446744073709551616, 0)) decimal_upper() {}
.func .attribute(.unified(0, 0x10000000000000000)) hexadecimal_lower() {}
.func .attribute(.unified(0x10000000000000000U, 0)) hexadecimal_upper();
.func .attribute(.unified(0, 18446744073709551616U)) decimal_lower();
.global .attribute(.unified(18446744073709551616, 0)) .u32 variable_upper;
)ptx");
  auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;
  ASSERT_TRUE(module.diagnostics.empty());
  const auto binding = binding::bindSymbols(*module);
  ASSERT_TRUE(binding.diagnostics.empty());
  const auto diagnostics = checkDeclarations(*module, binding.table);

  std::vector<SourceRange> expected_ranges;
  for (const auto& item : module->items) {
    if (const auto* function = std::get_if<syntax_ast::AstFunction>(&item)) {
      expected_ranges.push_back(function->attributes.front().values[
          function->name.syntax.text == "decimal_upper" ||
                  function->name.syntax.text == "hexadecimal_upper"
              ? 0
              : 1]
                                    .range);
    } else if (const auto* declaration =
                   std::get_if<syntax_ast::AstVariableDeclaration>(&item)) {
      expected_ranges.push_back(declaration->attributes.front().values.front().range);
    }
  }
  ASSERT_EQ(expected_ranges.size(), 5u);
  for (const SourceRange expected : expected_ranges) {
    EXPECT_TRUE(std::ranges::any_of(
        diagnostics, [expected](const DeclarationDiagnostic& diagnostic) {
          return diagnostic.kind == DeclarationDiagnosticKind::InvalidIntegerLiteral &&
                 diagnostic.range == expected;
        }));
  }
  EXPECT_EQ(std::ranges::count_if(
                diagnostics, [](const DeclarationDiagnostic& diagnostic) {
                  return diagnostic.kind ==
                         DeclarationDiagnosticKind::InvalidIntegerLiteral;
                }),
            expected_ranges.size());
}

/** Redeclaration identity compares integer values across octal and decimal spellings. */
TEST(PtxDeclarationSemantics, CanonicalizesOctalRedeclarationValues) {
  for (const std::string count : {"8", "16"}) {
    SCOPED_TRACE(count);
    const auto result = check(
        ".version 9.3\n"
        ".extern .global .align 010 .u32 aligned;\n"
        ".extern .global .align " + count + " .u32 aligned;\n"
        ".extern .global .u32 slots<010>;\n"
        ".extern .global .u32 slots<" + count + ">;\n"
        ".func f() .abi_preserve 010 .abi_preserve_control 010;\n"
        ".func f() .abi_preserve " + count + " .abi_preserve_control " +
        count + " {}\n");
    EXPECT_TRUE(result.binding.diagnostics.empty());
    EXPECT_EQ(diagnosticCount(result, DeclarationDiagnosticKind::IncompatibleRedeclaration),
              count == "8" ? 0u : 3u);
    if (count == "8")
      EXPECT_TRUE(result.diagnostics.empty());
  }
}

/** External storage redeclarations compare known natural and written alignments. */
TEST(PtxDeclarationSemantics,
     NormalizesNaturalExternalStorageAlignmentInEitherDeclarationOrder) {
  /** One implicit/explicit external declaration pair and its expected default. */
  struct AlignmentCase {
    std::string_view omitted;
    std::string_view explicit_alignment;
  };
  constexpr std::array cases{
      AlignmentCase{".extern .global .u32 scalar;\n",
                    ".extern .global .align 4 .u32 scalar;\n"},
      AlignmentCase{".extern .global .u64 word;\n",
                    ".extern .global .align 8 .u64 word;\n"},
      AlignmentCase{".extern .global .u32 values[8];\n",
                    ".extern .global .align 4 .u32 values[8];\n"},
      AlignmentCase{".extern .global .u8 bytes[32];\n",
                    ".extern .global .align 1 .u8 bytes[32];\n"},
      AlignmentCase{".extern .global .v2 .u32 pair[4];\n",
                    ".extern .global .align 8 .v2 .u32 pair[4];\n"},
      AlignmentCase{".extern .global .v4 .u32 quad[3];\n",
                    ".extern .global .align 16 .v4 .u32 quad[3];\n"},
      AlignmentCase{".extern .global .u32 octal;\n",
                    ".extern .global .align 04 .u32 octal;\n"},
  };

  for (const auto& alignment : cases) {
    for (const bool reverse : std::array{false, true}) {
      const std::string source = reverse
                                     ? std::string{alignment.explicit_alignment} +
                                           std::string{alignment.omitted}
                                     : std::string{alignment.omitted} +
                                           std::string{alignment.explicit_alignment};
      const CheckedModule result = check(source);
      EXPECT_TRUE(result.binding.diagnostics.empty()) << source;
      EXPECT_EQ(diagnosticCount(result,
                                DeclarationDiagnosticKind::IncompatibleRedeclaration),
                0u)
          << source;
      EXPECT_TRUE(result.diagnostics.empty()) << source;
    }
  }
}

/** Existing identical and equivalently explicit external declarations remain valid. */
TEST(PtxDeclarationSemantics, RetainsMatchingExternalStorageAlignmentControls) {
  const CheckedModule result = check(R"ptx(
.extern .global .u32 omitted;
.extern .global .u32 omitted;
.extern .global .align 4 .u32 numeric;
.extern .global .align 04 .u32 numeric;
)ptx");
  EXPECT_TRUE(result.binding.diagnostics.empty());
  EXPECT_TRUE(result.diagnostics.empty());
}

/** Alignment normalization does not weaken invalid or unrelated redeclaration checks. */
TEST(PtxDeclarationSemantics,
     RetainsInvalidAndIncompatibleStorageRedeclarationControls) {
  /** One source fixture and the diagnostic category it must preserve. */
  struct RejectedCase {
    std::string_view source;
    DeclarationDiagnosticKind kind;
  };
  constexpr std::array cases{
      RejectedCase{".extern .global .u32 invalid_alignment;\n"
                   ".extern .global .align 3 .u32 invalid_alignment;\n",
                   DeclarationDiagnosticKind::InvalidAlignment},
      RejectedCase{".extern .global .pred unknown;\n"
                   ".extern .global .align 1 .pred unknown;\n",
                   DeclarationDiagnosticKind::IncompatibleRedeclaration},
      RejectedCase{".extern .global .u32 shaped[4];\n"
                   ".extern .global .align 4 .u32 shaped[5];\n",
                   DeclarationDiagnosticKind::IncompatibleRedeclaration},
      RejectedCase{".extern .global .u32 changed_type;\n"
                   ".extern .global .align 8 .u64 changed_type;\n",
                   DeclarationDiagnosticKind::IncompatibleRedeclaration},
      RejectedCase{".extern .global .u32 linkage;\n"
                   ".visible .global .align 4 .u32 linkage;\n",
                   DeclarationDiagnosticKind::IncompatibleRedeclaration},
      RejectedCase{".global .u32 definition;\n.global .u32 definition;\n",
                   DeclarationDiagnosticKind::MultipleDefinitions},
  };
  for (const auto& rejected : cases) {
    const CheckedModule result = check(rejected.source);
    EXPECT_GT(diagnosticCount(result, rejected.kind), 0u) << rejected.source;
  }
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

TEST(PtxDeclarationSemantics, ChecksDeclarationsAndMetadataInsideNestedBlocks) {
  const CheckedModule result = check(R"ptx(
.func callee();
.func dispatch() {
  {
    .local .u32 invalid_extent[0];
    target: .calltargets missing;
  }
}
)ptx");

  EXPECT_TRUE(result.binding.diagnostics.empty());
  EXPECT_EQ(
      diagnosticCount(result, DeclarationDiagnosticKind::InvalidArrayDimension),
      1u);
  EXPECT_EQ(
      diagnosticCount(result, DeclarationDiagnosticKind::UnresolvedMetadataTarget),
      1u);
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

/**
 * @brief Reports every undecodable integer literal, including conditional
 * branches whose value is not selected.
 */
TEST(PtxDeclarationSemantics,
     PropagatesOverflowingIntegerLiteralsThroughDeclarations) {
  const CheckedModule result = check(R"ptx(.global .u32 decimal[18446744073709551616];
.global .u32 hexadecimal[0x10000000000000000];
.global .u32 octal[02000000000000000000000];
.global .u32 selected[1 ? 18446744073709551616 : 7];
.global .u32 unselected[0 ? 18446744073709551616 : 7];
.global .u32 masked = 0x10000000000000000(7);
)ptx");

  EXPECT_TRUE(result.binding.diagnostics.empty());
  size_t invalid_literal_count = 0;
  for (const DeclarationDiagnostic& diagnostic : result.diagnostics) {
    if (diagnostic.kind != DeclarationDiagnosticKind::InvalidIntegerLiteral)
      continue;
    ++invalid_literal_count;
    EXPECT_GE(diagnostic.range.start.line, 1U);
    EXPECT_LE(diagnostic.range.start.line, 6U);
    EXPECT_FALSE(diagnostic.previous_range.has_value());
  }
  EXPECT_EQ(invalid_literal_count, 6u);
  EXPECT_EQ(diagnosticCount(result,
                            DeclarationDiagnosticKind::InvalidArrayDimension),
            5u);
  EXPECT_EQ(diagnosticCount(
                result, DeclarationDiagnosticKind::InvalidInitializerExpression),
            1u);
}

/**
 * @brief Diagnoses malformed externally constructed integer ASTs at the
 * literal token while value-only helpers remain silent.
 */
TEST(PtxDeclarationSemantics,
     DiagnosesExternallyConstructedInvalidOctalIntegerLiteral) {
  PtxSyntaxParser parser(".global .u32 value[010];");
  auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;
  auto* declaration =
      std::get_if<syntax_ast::AstVariableDeclaration>(&module->items.front());
  ASSERT_NE(declaration, nullptr);
  ASSERT_EQ(declaration->declarators.size(), 1u);
  auto& dimension = declaration->declarators.front().array_dimensions.front();
  ASSERT_TRUE(dimension.size.has_value());
  auto* literal = std::get_if<syntax_ast::AstConstantLiteral>(
      &dimension.size->node);
  ASSERT_NE(literal, nullptr);
  const SourceRange literal_range = literal->value.syntax.range;
  literal->value.syntax.text = "09";

  const auto binding = binding::bindSymbols(*module);
  const auto diagnostics = checkDeclarations(*module, binding.table);

  const auto invalid = std::ranges::find_if(
      diagnostics, [](const DeclarationDiagnostic& diagnostic) {
        return diagnostic.kind == DeclarationDiagnosticKind::InvalidIntegerLiteral;
      });
  ASSERT_NE(invalid, diagnostics.end());
  EXPECT_EQ(invalid->range, literal_range);
  EXPECT_FALSE(constantArrayExtent(*dimension.size).has_value());
  EXPECT_FALSE(constantIntegerValue(*dimension.size).has_value());
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

/** Parameter declarations reject unsupported types, shapes, and ABI contexts. */
TEST(PtxDeclarationSemantics, ValidatesParameterDeclarationBoundaries) {
  /** One source declaration expected to produce the named diagnostic kind. */
  struct Case {
    std::string_view source;
    DeclarationDiagnosticKind kind;
  };
  constexpr std::array cases = {
      Case{R"ptx(.version 9.3
.target sm_80
.entry k(.param .pred predicate) {})ptx",
           DeclarationDiagnosticKind::UnsupportedParameterDeclaration},
      Case{R"ptx(.version 9.3
.target sm_80
.entry k(.param .mystery value) {})ptx",
           DeclarationDiagnosticKind::UnsupportedParameterDeclaration},
      Case{R"ptx(.version 9.3
.target sm_80
.entry k(.param .texref texture) {})ptx",
           DeclarationDiagnosticKind::UnsupportedParameterDeclaration},
      Case{R"ptx(.version 9.3
.target sm_80
.entry k(.param .b8 bytes[]) {})ptx",
           DeclarationDiagnosticKind::UnsizedArrayDimension},
      Case{R"ptx(.version 5.0
.target sm_30
.func f(.param .b8 bytes[]);)ptx",
           DeclarationDiagnosticKind::UnsupportedParameterDeclaration},
      Case{R"ptx(.version 9.3
.target sm_80
.func f(.param .u64 .ptr .global pointer);)ptx",
           DeclarationDiagnosticKind::UnsupportedParameterDeclaration},
      Case{R"ptx(.version 8.1
.target sm_80
.entry k(.param .b8 payload[32765]) {})ptx",
           DeclarationDiagnosticKind::UnsupportedParameterDeclaration},
  };
  for (const auto& test : cases) {
    const CheckedModule result = check(test.source);
    EXPECT_GT(diagnosticCount(result, test.kind), 0u) << test.source;
  }
}

/** Supported parameter forms preserve body-local array coverage and predicates. */
TEST(PtxDeclarationSemantics, AcceptsSupportedParameterDeclarationForms) {
  const CheckedModule result = check(R"ptx(
.version 8.0
.target sm_80
.entry k(.param .u64 .ptr .global pointer) {
  .param .u32 matrix[2][3];
}
.func device(.reg .pred predicate, .param .b8 bytes[]);
)ptx");

  EXPECT_TRUE(result.binding.diagnostics.empty());
  EXPECT_TRUE(result.diagnostics.empty());
}

/** Parameter availability and incomplete-array rules honor PTX and SM bounds. */
TEST(PtxDeclarationSemantics, ValidatesParameterAvailabilityBoundaries) {
  /** One versioned source declaration expected to produce the named diagnostic. */
  struct Case {
    std::string_view source;
    DeclarationDiagnosticKind kind;
  };
  constexpr std::array rejected = {
      Case{".version 1.3\n.entry k(.param .u32 value) {}",
           DeclarationDiagnosticKind::UnsupportedParameterDeclaration},
      Case{".version 1.5\n.target sm_20\n.func f(.param .u32 value);",
           DeclarationDiagnosticKind::UnsupportedParameterDeclaration},
      Case{".version 2.0\n.target sm_13\n.func f(.param .u32 value);",
           DeclarationDiagnosticKind::UnsupportedParameterDeclaration},
      Case{".version 2.1\n.target sm_30\n.entry k(.param .u64 .ptr p) {}",
           DeclarationDiagnosticKind::UnsupportedParameterDeclaration},
      Case{".version 2.0\n.target sm_20\n.entry k() {\n"
           "p: .callprototype _ (.param .u32 value);\n}",
           DeclarationDiagnosticKind::UnsupportedParameterDeclaration},
      Case{".version 5.0\n.target sm_30\n.func f(.param .b8 bytes[]);",
           DeclarationDiagnosticKind::UnsupportedParameterDeclaration},
      Case{".version 6.0\n.target sm_20\n.func f(.param .b8 bytes[]);",
           DeclarationDiagnosticKind::UnsupportedParameterDeclaration},
      Case{".version 6.0\n.target sm_30\n.func f(.param .u32 bytes[]);",
           DeclarationDiagnosticKind::UnsizedArrayDimension},
      Case{".version 6.0\n.target sm_30\n.func f(\n"
           ".param .b8 bytes[], .param .u32 trailing);",
           DeclarationDiagnosticKind::UnsizedArrayDimension},
      Case{".version 8.0\n.target sm_80\n.func (.param .b8 result[]) f();",
           DeclarationDiagnosticKind::UnsizedArrayDimension},
  };
  for (const auto& test : rejected) {
    const CheckedModule result = check(test.source);
    EXPECT_GT(diagnosticCount(result, test.kind), 0u) << test.source;
  }

  constexpr std::array accepted = {
      ".version 1.4\n.target sm_20\n.entry k(.param .u32 value) {}",
      ".version 2.0\n.target sm_20\n.func f(.param .u32 value);",
      ".version 2.2\n.target sm_20\n.entry k(.param .u64 .ptr p) {}",
      ".version 2.1\n.target sm_20\n.entry k() {\n"
      "p: .callprototype _ (.param .u32 value);\n}",
      ".version 6.0\n.target sm_30\n.func f(.param .b8 bytes[]);",
  };
  for (const auto source : accepted) {
    const CheckedModule result = check(source);
    EXPECT_TRUE(result.binding.diagnostics.empty()) << source;
    EXPECT_TRUE(result.diagnostics.empty()) << source;
  }
}

/** Parameter availability follows the target active at each function source range. */
TEST(PtxDeclarationSemantics, UsesEffectiveTargetForParameterAvailability) {
  const CheckedModule upgraded = check(R"ptx(
.version 6.0
.target sm_20
.func early() { ret; }
.target sm_30
.func late(.param .b8 payload[]) { ret; }
)ptx");
  EXPECT_TRUE(upgraded.binding.diagnostics.empty());
  EXPECT_TRUE(upgraded.diagnostics.empty());

  const CheckedModule downgraded = check(R"ptx(
.version 6.0
.target sm_30
.func early() { ret; }
.target sm_20
.func late(.param .b8 payload[]) { ret; }
)ptx");
  EXPECT_TRUE(downgraded.binding.diagnostics.empty());
  EXPECT_EQ(diagnosticCount(
                downgraded,
                DeclarationDiagnosticKind::UnsupportedParameterDeclaration),
            1u);

  const CheckedModule nested_downgraded = check(R"ptx(
.version 6.0
.target sm_30
.func early() { ret; }
.target sm_13
.func late() {
  .param .u32 staging;
  indirect: .callprototype _ (.param .u32 argument);
  ret;
}
)ptx");
  EXPECT_TRUE(nested_downgraded.binding.diagnostics.empty());
  EXPECT_EQ(diagnosticCount(
                nested_downgraded,
                DeclarationDiagnosticKind::UnsupportedParameterDeclaration),
            2u);

  const CheckedModule unknown_target = check(R"ptx(
.version 6.0
.target sm_20
.target sm_123a
.func late(.param .b8 payload[]) { ret; }
)ptx");
  EXPECT_TRUE(unknown_target.binding.diagnostics.empty());
  EXPECT_TRUE(unknown_target.diagnostics.empty());
}

/** Entry parameter byte limits account for alignment and checked arithmetic. */
TEST(PtxDeclarationSemantics, ValidatesEntryParameterByteBoundaries) {
  const auto entry = [](std::string_view version, uint64_t bytes) {
    return ".version " + std::string{version} +
           "\n.target sm_80\n.entry k(.param .b8 payload[" +
           std::to_string(bytes) + "]) {}";
  };
  constexpr std::array<std::pair<std::string_view, uint64_t>, 4> limits{{
      {"1.4", 256},
      {"1.5", 4352},
      {"8.0", 4352},
      {"8.1", 32764},
  }};
  for (const auto& [version, limit] : limits) {
    const CheckedModule exact = check(entry(version, limit));
    EXPECT_TRUE(exact.diagnostics.empty()) << version;
    const CheckedModule excess = check(entry(version, limit + 1));
    EXPECT_GT(
        diagnosticCount(
            excess, DeclarationDiagnosticKind::UnsupportedParameterDeclaration),
        0u)
        << version;
  }

  const CheckedModule aligned_fit = check(
      ".version 1.4\n.target sm_20\n.entry k("
      ".param .b8 first[1], .param .align 16 .b8 second[240]) {}");
  EXPECT_TRUE(aligned_fit.diagnostics.empty());
  const CheckedModule aligned_excess = check(
      ".version 1.4\n.target sm_20\n.entry k("
      ".param .b8 first[1], .param .align 16 .b8 second[241]) {}");
  EXPECT_GT(diagnosticCount(
                aligned_excess,
                DeclarationDiagnosticKind::UnsupportedParameterDeclaration),
            0u);

  const CheckedModule multiply_overflow = check(
      ".version 8.1\n.target sm_80\n.entry k("
      ".param .u64 values[18446744073709551615U]) {}");
  EXPECT_GT(diagnosticCount(multiply_overflow,
                            DeclarationDiagnosticKind::StorageExtentOverflow),
            0u);
  const CheckedModule sum_overflow = check(
      ".version 8.1\n.target sm_80\n.entry k("
      ".param .b8 first[18446744073709551608U], .param .b8 second[16]) {}");
  EXPECT_GT(diagnosticCount(sum_overflow,
                            DeclarationDiagnosticKind::StorageExtentOverflow),
            0u);
}

/** Parameter scalar classification accepts only the modeled storage subset. */
TEST(PtxDeclarationSemantics, ClassifiesSupportedParameterScalarTypes) {
  constexpr std::array supported = {
      ".u8", ".u16", ".u32", ".u64", ".s8",   ".s16", ".s32", ".s64",
      ".b8", ".b16", ".b32", ".b64", ".b128", ".f16", ".f32", ".f64",
  };
  std::string source = ".version 9.3\n.target sm_80\n.entry k(";
  for (size_t index = 0; index < supported.size(); ++index) {
    if (index != 0)
      source += ", ";
    source += ".param ";
    source += supported[index];
    source += " value" + std::to_string(index);
  }
  source += ") {}";
  const CheckedModule accepted = check(source);
  EXPECT_TRUE(accepted.binding.diagnostics.empty());
  EXPECT_TRUE(accepted.diagnostics.empty());
  for (const auto type : supported)
    EXPECT_TRUE(parameterScalarType(type).has_value()) << type;

  constexpr std::array unsupported = {
      ".pred", ".f16x2", ".bf16", ".tf32", ".mystery",
  };
  for (const auto type : unsupported) {
    const CheckedModule result = check(
        ".version 9.3\n.target sm_80\n.entry "
        "k(.param " +
        std::string{type} + " value) {}");
    EXPECT_GT(
        diagnosticCount(
            result, DeclarationDiagnosticKind::UnsupportedParameterDeclaration),
        0u)
        << type;
    EXPECT_FALSE(parameterScalarType(type).has_value()) << type;
  }
  const CheckedModule register_packed_half =
      check(".version 9.3\n.target sm_80\n.func f(.reg .f16x2 value);");
  EXPECT_TRUE(register_packed_half.diagnostics.empty());
}

/** Body-local parameters reject unmodeled forms but retain sized multidimensional arrays. */
TEST(PtxDeclarationSemantics, ValidatesBodyLocalParameterForms) {
  /** One body-local declaration expected to produce the named diagnostic. */
  struct Case {
    std::string_view declaration;
    DeclarationDiagnosticKind kind;
  };
  constexpr std::array rejected = {
      Case{".param .b8 bytes[];",
           DeclarationDiagnosticKind::UnsizedArrayDimension},
      Case{".param .v2 .u32 vector;",
           DeclarationDiagnosticKind::UnsupportedParameterDeclaration},
      Case{".param .texref texture;",
           DeclarationDiagnosticKind::UnsupportedParameterDeclaration},
      Case{".param .u32 values<2>;",
           DeclarationDiagnosticKind::UnsupportedParameterDeclaration},
  };
  for (const auto& test : rejected) {
    const CheckedModule result =
        check(".version 8.0\n.target sm_80\n.entry k() {\n" +
              std::string{test.declaration} + "\n}");
    EXPECT_GT(diagnosticCount(result, test.kind), 0u) << test.declaration;
  }
  const CheckedModule accepted = check(
      ".version 8.0\n.target sm_80\n.entry k() {\n"
      ".param .u32 matrix[2][3];\n}");
  EXPECT_TRUE(accepted.binding.diagnostics.empty());
  EXPECT_TRUE(accepted.diagnostics.empty());
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

TEST(PtxDeclarationSemantics, BuildsReusableCanonicalFunctionSignatures) {
  PtxSyntaxParser parser(R"ptx(
.func (.param .align 16 .u32 result) helper(
    .param .u64 address,
    .param .u32 values[2 * 2]);
.func (.param .align 16 .u32 output) helper(
    .param .u64 address,
    .param .u32 data[4]) { ret; }
.entry kernel() { }
.func noreturn_function() .noreturn { }
.func indirect() {
  prototype: .callprototype (.param .align 16 .u32 output) _
      (.param .u64 address, .param .u32 data[4]);
  noreturn_prototype: .callprototype _ .noreturn;
}
)ptx");
  const auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;
  const auto& prototype = std::get<syntax_ast::AstFunction>(module->items[0]);
  const auto& definition = std::get<syntax_ast::AstFunction>(module->items[1]);

  const FunctionSignature prototype_signature = functionSignature(prototype);
  EXPECT_EQ(prototype_signature, functionSignature(definition));
  ASSERT_EQ(prototype_signature.return_parameters.size(), 1u);
  const auto& result = prototype_signature.return_parameters[0];
  EXPECT_EQ(result.state_space,
            call_argument_compatibility::CallArgumentStateSpace::Parameter);
  EXPECT_EQ(contractConstant(result.alignment), 16u);
  EXPECT_EQ(result.scalar_type, base::ScalarType::U32);
  EXPECT_EQ(result.type_spelling, ".u32");
  const auto& address = prototype_signature.parameters[0];
  EXPECT_FALSE(address.is_pointer);
  EXPECT_EQ(address.scalar_type, base::ScalarType::U64);
  EXPECT_EQ(contractConstant(address.alignment), 8u);
  ASSERT_EQ(prototype_signature.parameters.size(), 2u);
  const auto& values = prototype_signature.parameters[1];
  EXPECT_TRUE(values.is_array);
  EXPECT_EQ(contractConstant(values.array_extent), 4u);
  const FunctionSignature kernel_signature =
      functionSignature(std::get<syntax_ast::AstFunction>(module->items[2]));
  EXPECT_TRUE(kernel_signature.is_entry);
  EXPECT_FALSE(kernel_signature.is_noreturn);
  const FunctionSignature noreturn_signature =
      functionSignature(std::get<syntax_ast::AstFunction>(module->items[3]));
  EXPECT_FALSE(noreturn_signature.is_entry);
  EXPECT_TRUE(noreturn_signature.is_noreturn);
  const auto& indirect = std::get<syntax_ast::AstFunction>(module->items[4]);
  const auto& indirect_prototype =
      std::get<syntax_ast::AstCallPrototype>(indirect.body[0]);
  EXPECT_EQ(prototype_signature, functionSignature(indirect_prototype));
  const auto& noreturn_prototype =
      std::get<syntax_ast::AstCallPrototype>(indirect.body[1]);
  EXPECT_TRUE(functionSignature(noreturn_prototype).is_noreturn);
  EXPECT_TRUE(
      checkDeclarations(*module, binding::bindSymbols(*module).table).empty());
}

/** Invalid numeric fields retain distinct structural keys for redeclaration checks. */
TEST(PtxDeclarationSemantics, RetainsInvalidParameterContractStructure) {
  PtxSyntaxParser parser(R"ptx(
.func f(.param .align 3 .u32 value);
.func f(.param .align 5 .u32 value) { ret; }
)ptx");
  const auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value()) << module.diagnostics.front().message;
  const auto& declaration =
      std::get<syntax_ast::AstFunction>(module->items[0]);
  const auto& definition = std::get<syntax_ast::AstFunction>(module->items[1]);
  const auto declaration_signature = functionSignature(declaration);
  const auto definition_signature = functionSignature(definition);
  ASSERT_EQ(declaration_signature.parameters.size(), 1u);
  ASSERT_EQ(definition_signature.parameters.size(), 1u);
  const auto* first_key = std::get_if<InvalidStructuralKey>(
      &*declaration_signature.parameters.front().alignment);
  const auto* second_key = std::get_if<InvalidStructuralKey>(
      &*definition_signature.parameters.front().alignment);
  ASSERT_NE(first_key, nullptr);
  ASSERT_NE(second_key, nullptr);
  EXPECT_EQ(first_key->value, "3");
  EXPECT_EQ(second_key->value, "5");
  EXPECT_NE(declaration_signature, definition_signature);

  const auto diagnostics =
      checkDeclarations(*module, binding::bindSymbols(*module).table);
  EXPECT_EQ(std::ranges::count_if(diagnostics, [](const auto& diagnostic) {
              return diagnostic.kind == DeclarationDiagnosticKind::InvalidAlignment;
            }),
            2u);
  EXPECT_EQ(std::ranges::count_if(diagnostics, [](const auto& diagnostic) {
              return diagnostic.kind ==
                     DeclarationDiagnosticKind::IncompatibleRedeclaration;
            }),
            1u);
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

TEST(PtxDeclarationSemantics, AcceptsCompatibleControlFlowMetadata) {
  const CheckedModule result = check(R"ptx(
.func (.reg .u32 output) first(.reg .u32 input);
.func (.reg .u32 output) second(.reg .u32 value) { ret; }
.func dispatch() {
L0:
L1:
N0:
N1:
  prototype: .callprototype _ (.param .b8 payload[12]) .noreturn;
  targets: .calltargets first, second;
  branches: .branchtargets L0, N<2U>;
}
)ptx");

  EXPECT_TRUE(result.binding.diagnostics.empty());
  EXPECT_TRUE(result.diagnostics.empty());
}

TEST(PtxDeclarationSemantics, ValidatesControlFlowMetadataDeclarations) {
  const CheckedModule result = check(R"ptx(
.func (.reg .u32 output) first(.reg .u32 input);
.func (.reg .u32 output) mismatch(.reg .u64 input);
.entry kernel() { }
.func dispatch() {
local:
N0:
N1:
  targets: .calltargets first, first, later, kernel, mismatch;
  branches: .branchtargets local, local, Other, N<3>, N<2>, Z<0>;
  returning: .callprototype (.param .u32 output) _ .noreturn;
  register_array: .callprototype _ (.reg .b8 values[4]);
  zero_array: .callprototype _ (.param .b8 values[0]);
}
.func later();
.func other() { Other: ret; }
)ptx");

  EXPECT_TRUE(result.binding.diagnostics.empty());
  EXPECT_EQ(diagnosticCount(result,
                            DeclarationDiagnosticKind::DuplicateMetadataTarget),
            1u);
  EXPECT_EQ(diagnosticCount(result,
                            DeclarationDiagnosticKind::UnresolvedMetadataTarget),
            3u);
  EXPECT_EQ(diagnosticCount(result,
                            DeclarationDiagnosticKind::InvalidMetadataTarget),
            2u);
  EXPECT_EQ(
      diagnosticCount(result,
                      DeclarationDiagnosticKind::IncompatibleCallTargetSignature),
      1u);
  EXPECT_EQ(diagnosticCount(result,
                            DeclarationDiagnosticKind::InvalidCallPrototype),
            2u);
  EXPECT_EQ(diagnosticCount(result,
                            DeclarationDiagnosticKind::InvalidArrayDimension),
            1u);

  const auto duplicate_call_target = std::ranges::find_if(
      result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.kind ==
                   DeclarationDiagnosticKind::DuplicateMetadataTarget &&
               diagnostic.message.find(".calltargets") != std::string::npos;
      });
  ASSERT_NE(duplicate_call_target, result.diagnostics.end());
  ASSERT_TRUE(duplicate_call_target->previous_range.has_value());
  EXPECT_EQ(duplicate_call_target->previous_range->start.line, 9);
  EXPECT_EQ(duplicate_call_target->range.start.line, 9);

  const auto incompatible = std::ranges::find_if(
      result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.kind ==
               DeclarationDiagnosticKind::IncompatibleCallTargetSignature;
      });
  ASSERT_NE(incompatible, result.diagnostics.end());
  ASSERT_TRUE(incompatible->previous_range.has_value());
  EXPECT_EQ(incompatible->previous_range->start.line, 9);
  EXPECT_EQ(incompatible->range.start.line, 9);
}

TEST(PtxDeclarationSemantics,
     AllowsRepeatedSymbolicBranchTargetsWhileCheckingEachDestination) {
  const CheckedModule result = check(R"ptx(
.func dispatch() {
  branches: .branchtargets Missing, Missing, N<3>, N<2>;
}
)ptx");

  EXPECT_TRUE(result.binding.diagnostics.empty());
  EXPECT_EQ(diagnosticCount(result,
                            DeclarationDiagnosticKind::DuplicateMetadataTarget),
            0u);
  EXPECT_EQ(diagnosticCount(result,
                            DeclarationDiagnosticKind::UnresolvedMetadataTarget),
            4u);
}

TEST(PtxDeclarationSemantics, AllowsOverlappingBranchTargetDestinations) {
  const CheckedModule result = check(R"ptx(
.func dispatch() {
N0:
N1:
N2:
N3:
N10:
N11:
  branches: .branchtargets N0, N0, N<4>, N<2>, N1<2>;
}
)ptx");

  EXPECT_TRUE(result.binding.diagnostics.empty());
  EXPECT_TRUE(result.diagnostics.empty());
}

TEST(PtxDeclarationSemantics,
     ReportsMissingLabelsForDistinctCompactBranchTargetPrefixes) {
  const CheckedModule result = check(R"ptx(
.func dispatch() {
  branches: .branchtargets N<20>, N1<2>;
}
)ptx");

  EXPECT_TRUE(result.binding.diagnostics.empty());
  EXPECT_EQ(diagnosticCount(result,
                            DeclarationDiagnosticKind::DuplicateMetadataTarget),
            0u);
  EXPECT_EQ(diagnosticCount(result,
                            DeclarationDiagnosticKind::UnresolvedMetadataTarget),
            2u);
}

TEST(PtxDeclarationSemantics, UsesEachFunctionBodyScopeForBranchTargets) {
  const CheckedModule result = check(R"ptx(
.func duplicate() {
first_label:
  first_targets: .branchtargets first_label;
}
.func duplicate() {
second_label:
  second_targets: .branchtargets second_label;
}
)ptx");

  EXPECT_TRUE(result.binding.diagnostics.empty());
  EXPECT_EQ(diagnosticCount(result,
                            DeclarationDiagnosticKind::UnresolvedMetadataTarget),
            0u);
  EXPECT_EQ(diagnosticCount(result,
                            DeclarationDiagnosticKind::MultipleDefinitions),
            1u);
}

TEST(PtxDeclarationSemantics, RequiresFunctionDeclarationsBeforeAddressInitializers) {
  const CheckedModule result = check(R"ptx(
.global .u64 late_table[1] = { late };
.func late() { ret; }
.func declared();
.func local_table() {
  .global .u64 targets[1] = { declared };
}
.func declared() { ret; }
)ptx");

  EXPECT_TRUE(result.binding.diagnostics.empty());
  EXPECT_EQ(
      diagnosticCount(
          result, DeclarationDiagnosticKind::FunctionAddressBeforeDeclaration),
      1u);
  const auto diagnostic =
      std::ranges::find_if(result.diagnostics, [](const auto& candidate) {
        return candidate.kind ==
               DeclarationDiagnosticKind::FunctionAddressBeforeDeclaration;
      });
  ASSERT_NE(diagnostic, result.diagnostics.end());
  EXPECT_EQ(diagnostic->range.start.line, 2);
  ASSERT_TRUE(diagnostic->previous_range.has_value());
  EXPECT_EQ(diagnostic->previous_range->start.line, 3);
}

TEST(PtxDeclarationSemantics,
     PreservesAliasIdentityWhileCheckingAliasDeclarationVisibility) {
  const CheckedModule accepted = check(R"ptx(
.func alias_fn();
.global .u64 targets[1] = { alias_fn };
.func target() { ret; }
.alias alias_fn, target;
)ptx");
  EXPECT_TRUE(accepted.binding.diagnostics.empty());
  EXPECT_EQ(diagnosticCount(
                accepted,
                DeclarationDiagnosticKind::FunctionAddressBeforeDeclaration),
            0u);

  const CheckedModule rejected = check(R"ptx(
.func target();
.global .u64 targets[1] = { alias_fn };
.func alias_fn();
.func target() { ret; }
.alias alias_fn, target;
)ptx");
  EXPECT_TRUE(rejected.binding.diagnostics.empty());
  EXPECT_EQ(diagnosticCount(
                rejected,
                DeclarationDiagnosticKind::FunctionAddressBeforeDeclaration),
            1u);
}

TEST(PtxDeclarationSemantics,
     RejectsFunctionAddressWhenSourceOrderCannotBeEstablished) {
  PtxSyntaxParser parser(R"ptx(
.global .u64 targets[1] = { later };
.func later() { ret; }
)ptx");
  auto module = parser.parseModule();
  ASSERT_TRUE(module.has_value());
  ASSERT_TRUE(module.diagnostics.empty());
  auto& declaration =
      std::get<syntax_ast::AstVariableDeclaration>(module->items.front());
  auto& initializer_list = std::get<syntax_ast::AstInitializerList>(
      declaration.declarators.front().initializer->value);
  auto& expression = std::get<syntax_ast::AstConstantExpression>(
      initializer_list.elements.front().value);
  auto& symbol = std::get<syntax_ast::AstConstantSymbol>(expression.node);
  auto& function = std::get<syntax_ast::AstFunction>(module->items.back());
  symbol.name.syntax.range = SourceRange{};
  function.name.syntax.range = SourceRange{};

  auto binding = binding::bindSymbols(*module);
  const auto diagnostics = checkDeclarations(*module, binding.table);
  const auto diagnostic =
      std::ranges::find_if(diagnostics, [](const auto& candidate) {
        return candidate.kind ==
               DeclarationDiagnosticKind::FunctionAddressBeforeDeclaration;
      });
  ASSERT_NE(diagnostic, diagnostics.end());
  EXPECT_FALSE(diagnostic->previous_range.has_value());
  EXPECT_NE(diagnostic->message.find("Cannot establish"), std::string::npos);
}

TEST(PtxDeclarationSemantics,
     KeepsDeclarationOccurrencesAlignedAfterDebugMetadataSymbols) {
  const CheckedModule result = check(R"ptx(
.file 1 "source.ptx"
.func declared();
.global .u64 targets[1] = { declared };
.func declared() { ret; }
)ptx");
  EXPECT_TRUE(result.binding.diagnostics.empty());
  EXPECT_TRUE(result.diagnostics.empty());
}

TEST(PtxDeclarationSemantics,
     RequiresBranchTargetMetadataBeforeIndexedBranchUse) {
  const CheckedModule late = check(R"ptx(
.entry kernel() {
  .reg .u32 %index;
  brx.idx %index, targets;
  targets: .branchtargets L0;
L0:
  ret;
}
)ptx");
  EXPECT_TRUE(late.binding.diagnostics.empty());
  EXPECT_EQ(diagnosticCount(
                late, DeclarationDiagnosticKind::UnresolvedMetadataTarget),
            1u);

  const CheckedModule prior = check(R"ptx(
.entry kernel() {
  .reg .u32 %index;
  { targets: .branchtargets L0; }
  brx.idx %index, targets;
  bra done;
L0:
done:
  ret;
}
)ptx");
  EXPECT_TRUE(prior.binding.diagnostics.empty());
  EXPECT_TRUE(prior.diagnostics.empty());
}

TEST(PtxDeclarationSemantics,
     KeepsNestedBranchTargetSetShadowingAtItsBoundIdentity) {
  const CheckedModule result = check(R"ptx(
.entry kernel() {
  .reg .u32 %index;
  targets: .branchtargets done;
  {
    .reg .u32 targets;
    brx.idx %index, targets;
  }
done:
  ret;
}
)ptx");
  EXPECT_EQ(std::ranges::count_if(
                result.binding.diagnostics,
                [](const auto& diagnostic) {
                  return diagnostic.kind ==
                         binding::BindDiagnosticKind::InvalidReferenceTarget;
                }),
            1u);
  EXPECT_EQ(diagnosticCount(
                result, DeclarationDiagnosticKind::UnresolvedMetadataTarget),
            0u);
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

TEST(PtxDeclarationSemantics, ChecksKernelResourcePtxAvailability) {
  const CheckedModule old_max = check(R"ptx(
.version 1.2
.entry kernel() .maxnreg 32 .maxntid 32 { }
)ptx");
  EXPECT_EQ(diagnosticCount(
                old_max,
                DeclarationDiagnosticKind::UnsupportedKernelResourcePtxVersion),
            2u);

  const CheckedModule valid_max = check(R"ptx(
.version 1.3
.entry kernel() .maxnreg 32 .maxntid 32, 2 { }
)ptx");
  EXPECT_TRUE(valid_max.diagnostics.empty());

  const CheckedModule old_min = check(R"ptx(
.version 1.9
.entry kernel() .minnctapersm 2 { }
)ptx");
  EXPECT_EQ(diagnosticCount(
                old_min,
                DeclarationDiagnosticKind::UnsupportedKernelResourcePtxVersion),
            1u);

  const CheckedModule valid_min = check(R"ptx(
.version 2.0
.entry kernel() .minnctapersm 2 { }
)ptx");
  EXPECT_TRUE(valid_min.diagnostics.empty());

  const CheckedModule old_req = check(R"ptx(
.version 2.0
.entry kernel() .reqntid 32 { }
)ptx");
  EXPECT_EQ(diagnosticCount(
                old_req,
                DeclarationDiagnosticKind::UnsupportedKernelResourcePtxVersion),
            1u);

  const CheckedModule valid_req = check(R"ptx(
.version 2.1
.entry kernel() .reqntid 32 .minnctapersm 2 { }
)ptx");
  EXPECT_TRUE(valid_req.diagnostics.empty());
}

TEST(PtxDeclarationSemantics, ChecksClusterDimensionDirectivePtxAvailability) {
  const CheckedModule old_req = check(R"ptx(
.version 7.7
.entry required() .reqnctapercluster 2, 1 { }
)ptx");
  const CheckedModule old_explicit = check(R"ptx(
.version 7.7
.entry explicit_cluster() .explicitcluster { }
)ptx");
  const CheckedModule old_maximum = check(R"ptx(
.version 7.7
.entry maximum() .maxclusterrank 8 { }
)ptx");
  EXPECT_EQ(diagnosticCount(
                old_req,
                DeclarationDiagnosticKind::UnsupportedKernelResourcePtxVersion),
            1u);
  EXPECT_EQ(diagnosticCount(
                old_explicit,
                DeclarationDiagnosticKind::UnsupportedKernelResourcePtxVersion),
            1u);
  EXPECT_EQ(diagnosticCount(
                old_maximum,
                DeclarationDiagnosticKind::UnsupportedKernelResourcePtxVersion),
            1u);

  const CheckedModule supported = check(R"ptx(
.version 7.8
.entry required() .reqnctapercluster 2, 1 { }
.entry explicit_cluster() .explicitcluster { }
.entry maximum() .maxclusterrank 8 { }
)ptx");
  EXPECT_TRUE(supported.diagnostics.empty());
}

TEST(PtxDeclarationSemantics,
     RejectsConflictingClusterDimensionDirectivesInOrder) {
  const CheckedModule req_then_max = check(R"ptx(
.version 7.8
.entry required() .reqnctapercluster 2 .maxclusterrank 8 { }
)ptx");
  const CheckedModule max_then_req = check(R"ptx(
.version 7.8
.entry maximum() .maxclusterrank 8 .reqnctapercluster 2 { }
)ptx");
  for (const CheckedModule* result : {&req_then_max, &max_then_req}) {
    const auto diagnostic =
        std::ranges::find_if(result->diagnostics, [](const auto& entry) {
          return entry.kind ==
                 DeclarationDiagnosticKind::IncompatibleKernelResourceDirective;
        });
    ASSERT_NE(diagnostic, result->diagnostics.end());
    ASSERT_TRUE(diagnostic->previous_range.has_value());
    EXPECT_TRUE(diagnostic->previous_range->start.column <
                diagnostic->range.start.column);
  }

  const CheckedModule explicit_with_req = check(R"ptx(
.version 7.8
.entry required() .explicitcluster .reqnctapercluster 2 { }
)ptx");
  const CheckedModule explicit_with_max = check(R"ptx(
.version 7.8
.entry maximum() .explicitcluster .maxclusterrank 8 { }
)ptx");
  EXPECT_TRUE(explicit_with_req.diagnostics.empty());
  EXPECT_TRUE(explicit_with_max.diagnostics.empty());
}

TEST(PtxDeclarationSemantics, RejectsConflictingKernelThreadCountsInOrder) {
  const CheckedModule max_then_req = check(R"ptx(
.version 2.1
.entry first() .maxntid 32 .reqntid 32 { }
)ptx");
  const auto max_then_req_diagnostic = std::ranges::find_if(
      max_then_req.diagnostics, [](const auto& diagnostic) {
        return diagnostic.kind ==
               DeclarationDiagnosticKind::IncompatibleKernelResourceDirective;
      });
  ASSERT_NE(max_then_req_diagnostic, max_then_req.diagnostics.end());
  ASSERT_TRUE(max_then_req_diagnostic->previous_range.has_value());
  EXPECT_EQ(max_then_req_diagnostic->range.start.line, 3u);
  EXPECT_TRUE(max_then_req_diagnostic->previous_range->start.column <
              max_then_req_diagnostic->range.start.column);

  const CheckedModule req_then_max = check(R"ptx(
.version 2.1
.entry second() .reqntid 32 .maxntid 32 { }
)ptx");
  const auto req_then_max_diagnostic = std::ranges::find_if(
      req_then_max.diagnostics, [](const auto& diagnostic) {
        return diagnostic.kind ==
               DeclarationDiagnosticKind::IncompatibleKernelResourceDirective;
      });
  ASSERT_NE(req_then_max_diagnostic, req_then_max.diagnostics.end());
  ASSERT_TRUE(req_then_max_diagnostic->previous_range.has_value());
  EXPECT_TRUE(req_then_max_diagnostic->previous_range->start.column <
              req_then_max_diagnostic->range.start.column);

  const CheckedModule separate_entries = check(R"ptx(
.version 2.1
.entry maximum() .maxntid 32 { }
.entry required() .reqntid 32 { }
)ptx");
  EXPECT_EQ(diagnosticCount(
                separate_entries,
                DeclarationDiagnosticKind::IncompatibleKernelResourceDirective),
            0u);
}

TEST(PtxDeclarationSemantics, RejectsModuleScopeParameterVariables) {
  const CheckedModule result = check(".param .u32 staging;");

  EXPECT_EQ(diagnosticCount(result,
                            DeclarationDiagnosticKind::ModuleScopeParameter),
            1u);
}

}  // namespace
}  // namespace ptx_frontend::declaration_semantics
