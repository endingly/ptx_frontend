#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Resolve complete text while returning no parser or syntax storage. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> rank_module(
    std::string text) {
  const auto ast = test_helpers::parseModule(text);
  EXPECT_TRUE(ast && ast.diagnostics.empty()) << text;
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveAndValidateModule(*ast);
}
/** Keep all dimension tests within coherent modern declaration gates. */
std::string rank_header() {
  return ".version 9.3\n.target sm_80, texmode_independent\n.address_size 64\n";
}

TEST(HeaderArrayRank, RetainsAllNumericAxesAndNativeVectorElementLayouts) {
  for (const auto& type : base::scalar_type_metadata()) {
    if (!declaration_semantics::parameterScalarType(type.source_spelling))
      continue;
    for (const auto lanes : {1u, 2u, 4u}) {
      if (type.size_bytes * lanes > 16)
        continue;
      const auto element = std::string(lanes == 1   ? ""
                                       : lanes == 2 ? ".v2 "
                                                    : ".v4 ") +
                           std::string(type.source_spelling);
      for (int role = 0; role < 4; ++role) {
        SCOPED_TRACE(element);
        SCOPED_TRACE(role);
        const auto declaration = ".param " + element + " a[2][3]";
        const auto source =
            rank_header() +
            (role == 0   ? ".entry k(" + declaration + ") {}"
             : role == 1 ? ".extern .func f(" + declaration + ");"
             : role == 2 ? ".extern .func (" + declaration + ") f();"
                         : ".entry k() { p: .callprototype (" + declaration +
                               ") _ (" + declaration + "); }");
        auto module = rank_module(source);
        ASSERT_TRUE(module)
            << (module.error().empty() ? "" : module.error().front().message);
        auto copy = *module;
        EXPECT_TRUE(validateModule(copy));
        if (role != 3) {
          const auto& record =
              copy.functions.front().parameter_declarations.front();
          EXPECT_EQ(record.array_extents,
                    (std::vector<std::optional<uint64_t>>{2, 3}));
          EXPECT_EQ(record.byte_extent, 6 * type.size_bytes * lanes);
          EXPECT_EQ(record.alignment, type.size_bytes * lanes);
          EXPECT_EQ(record.vector_width, lanes);
        } else {
          const auto& formal = copy.functions.front()
                                   .call_prototypes.front()
                                   .signature.parameters.front();
          EXPECT_EQ(formal.array_extents.size(), 2u);
          EXPECT_EQ(declaration_semantics::call_argument_properties(formal)
                        .array_size,
                    6u);
        }
      }
    }
  }
  auto rank = rank_module(rank_header() +
                          ".entry k(.param .u32 a[1][1][1][1][1][1][1][2]) {}");
  ASSERT_TRUE(rank);
  EXPECT_EQ(rank->functions.front()
                .parameter_declarations.front()
                .array_extents.size(),
            8u);
}

TEST(HeaderArrayRank, UsesTotalCountAcrossDeclarationsDirectAndMetadataCalls) {
  for (const auto metadata :
       {"", "p: .callprototype _ (.param .v2 .u32 x[3][2]);",
        "p: .calltargets f;"}) {
    for (const auto actual :
         {".param .v2 .u32 a[1][6];", ".param .v2 .u32 a[5];",
          ".param .v4 .u16 a[6];", ".param .align 16 .v2 .u32 a[6];"}) {
      const auto source =
          rank_header() +
          ".func f(.param .v2 .u32 x[2][3]); .entry k() { .reg .b64 target; " +
          actual + metadata +
          (std::string_view(metadata).empty() ? "call f, (a);"
                                              : "call target, (a), p;") +
          " } .func f(.param .v2 .u32 x[6]) { ret; }";
      auto module = rank_module(source);
      ASSERT_EQ(module.has_value(),
                std::string_view(actual) == ".param .v2 .u32 a[1][6];");
      if (module) {
        auto copy = *module;
        copy.functions[1].body[0] = copy.functions[1].body[0]->clone();
        EXPECT_TRUE(validateModule(copy));
        EXPECT_EQ(copy.functions.front().contract.signature,
                  copy.functions.back().contract.signature);
        EXPECT_NE(copy.functions.front().source_identity,
                  copy.functions.back().source_identity);
      }
    }
  }
  EXPECT_FALSE(
      rank_module(rank_header() +
                  ".func f(.param .b8 a[2][3]); .func f(.param .b8 a[7]) {}"));
}

TEST(HeaderArrayRank, UsesTotalCountForAliasesTablesAndReturnArrays) {
  auto aliases = rank_module(rank_header() + R"ptx(
.func alias_fn(.param .b8 x[2][3]);
.func target(.param .b8 x[6]) { ret; }
.alias alias_fn, target;
.global .u64 table[2] = {alias_fn, target};
.entry k() {
  .param .b8 a[3][2];
  .reg .b64 address;
  targets: .calltargets alias_fn, target;
  call alias_fn, (a);
  call address, (a), targets;
  call address, (a), table;
}
)ptx");
  ASSERT_TRUE(aliases) << (aliases.error().empty()
                               ? ""
                               : aliases.error().front().message);
  auto copied = *aliases;
  EXPECT_TRUE(validateModule(copied));
  ASSERT_EQ(copied.call_tables.size(), 1u);
  EXPECT_EQ(declaration_semantics::call_argument_properties(
                copied.call_tables[0].signature.parameters[0])
                .array_size,
            6u);
  auto returns = rank_module(rank_header() + R"ptx(
.func (.param .v2 .u32 result[2][3]) f(.param .v2 .u32 input[3][2]);
.entry k() { .param .v2 .u32 result[6], input[1][6]; call (result), f, (input); }
.func (.param .v2 .u32 result[6]) f(.param .v2 .u32 input[6]) { ret; }
)ptx");
  ASSERT_TRUE(returns) << (returns.error().empty()
                               ? ""
                               : returns.error().front().message);
  EXPECT_TRUE(validateModule(*returns));
}

TEST(HeaderArrayRank, PreservesUnknownOuterAndCheckedKnownInnerAxes) {
  for (const auto axes : {"[][2]", "[][2][3]"}) {
    // A five-byte payload is valid even when the known inner product is six.
    auto module =
        rank_module(rank_header() + ".extern .func f(.param .b8 a" + axes +
                    "); .entry k() { .param .b8 a[5]; call f, (a); p: "
                    ".callprototype _ (.param .b8 x" +
                    axes + "); .reg .b64 target; call target, (a), p; }");
    ASSERT_TRUE(module) << (module.error().empty()
                                ? ""
                                : module.error().front().message);
    auto copied = *module;
    const auto& record =
        copied.functions.front().parameter_declarations.front();
    EXPECT_FALSE(record.array_extents.front());
    EXPECT_EQ(record.array_extents[1], 2u);
    EXPECT_FALSE(record.byte_extent);
    EXPECT_FALSE(
        declaration_semantics::call_argument_properties(
            copied.functions.front().contract.signature.parameters.front())
            .array_size);
    EXPECT_TRUE(validateModule(copied));
  }
  for (const auto declaration :
       {".entry k(.param .b8 a[][2]) {}",
        ".extern .func (.param .b8 a[][2]) f();",
        ".extern .func f(.param .b8 a[][2], .param .u32 b);",
        ".extern .func f(.param .u8 a[][2]);",
        ".extern .func f(.param .b8 a[2][]);",
        ".extern .func f(.param .b8 a[][18446744073709551615][2]);",
        ".extern .func f(.reg .u32 a[2][3]);",
        ".entry k(.param .u64 .ptr a[2][3]) {}",
        ".extern .func f(.param .v2 .b8 a[][2]);",
        ".entry k(.param .b8 a[2][0]) {}", ".entry k(.param .b8 a[2][-1]) {}",
        ".entry k(.param .b8 a[2][missing]) {}",
        ".entry k(.param .b8 a[2][18446744073709551615]) {}"}) {
    SCOPED_TRACE(declaration);
    EXPECT_FALSE(rank_module(rank_header() + declaration));
  }
  EXPECT_FALSE(rank_module(
      ".version 5.0\n.target sm_30\n.extern .func f(.param .b8 a[][2]);"));
  EXPECT_FALSE(rank_module(
      ".version 6.0\n.target sm_20\n.extern .func f(.param .b8 a[][2]);"));
  EXPECT_TRUE(rank_module(
      ".version 6.0\n.target sm_30\n.extern .func f(.param .b8 a[][2]);"));
}

TEST(HeaderArrayRank, RejoinsExactOwnedAxesWithoutImposingAnAbiRankVeto) {
  auto module =
      rank_module(rank_header() + ".entry k(.param .v2 .u32 a[2][3]) { ret; }");
  ASSERT_TRUE(module);
  for (int mutation = 0; mutation < 6; ++mutation) {
    auto invalid = *module;
    auto& record = invalid.functions.front().parameter_declarations.front();
    auto& axes = invalid.functions.front()
                     .contract.signature.parameters.front()
                     .array_extents;
    if (mutation == 0)
      std::swap(record.array_extents[0], record.array_extents[1]);
    if (mutation == 1)
      axes[1] = uint64_t{4};
    if (mutation == 2)
      record.byte_extent = 47;
    if (mutation == 3)
      axes[1] = uint64_t{0};
    if (mutation == 4)
      axes[1] = declaration_semantics::InvalidStructuralKey{"bad"};
    if (mutation == 5) {
      record.array_extents = {6};
      record.alignment = 16;
    }
    EXPECT_FALSE(validateModule(invalid));
  }
  auto coherent = *module;
  coherent.functions.front().parameter_declarations.front().array_extents = {6};
  coherent.functions.front()
      .contract.signature.parameters.front()
      .array_extents = {uint64_t{6}};
  EXPECT_TRUE(validateModule(coherent));
  auto prototype =
      rank_module(rank_header() +
                  ".entry k() { p: .callprototype _ (.param .b8 a[2][3]); }");
  ASSERT_TRUE(prototype);
  for (int mutation = 0; mutation < 4; ++mutation) {
    auto invalid = *prototype;
    auto& axes = invalid.functions.front()
                     .call_prototypes.front()
                     .signature.parameters.front()
                     .array_extents;
    if (mutation == 0)
      axes[1] = uint64_t{0};
    if (mutation == 1)
      axes[1] = std::nullopt;
    if (mutation == 2)
      axes = {uint64_t{2}, std::numeric_limits<uint64_t>::max()};
    if (mutation == 3)
      axes[0] = declaration_semantics::InvalidStructuralKey{"bad"};
    EXPECT_FALSE(validateModule(invalid));
  }
}

TEST(HeaderArrayRank, RetainsOpaqueAxesAcrossSourceDeathAndValueCopies) {
  auto module =
      rank_module(rank_header() +
                  ".entry k(.param .align 8 .texref tex[2][3][4], .param "
                  ".surfref surf[3][2], .param .samplerref sampler) { ret; }");
  ASSERT_TRUE(module);
  auto copied = *module;
  const auto& records = copied.functions.front().opaque_entry_parameters;
  ASSERT_EQ(records.size(), 3u);
  EXPECT_EQ(records[0].array_extents,
            (std::vector<std::optional<uint64_t>>{2, 3, 4}));
  EXPECT_EQ(records[1].array_extents,
            (std::vector<std::optional<uint64_t>>{3, 2}));
  EXPECT_TRUE(records[2].array_extents.empty());
  EXPECT_EQ(records[0].explicit_alignment, 8u);
  EXPECT_TRUE(copied.functions.front().parameter_declarations.empty());
  EXPECT_TRUE(validateModule(copied));
  ResolvedFunction function_copy(copied.functions.front());
  copied.functions.front() = function_copy;
  copied.functions.front().body[0] = copied.functions.front().body[0]->clone();
  EXPECT_TRUE(validateModule(copied));
  copied.functions.front().opaque_entry_parameters[0].array_extents = {24};
  EXPECT_FALSE(validateModule(copied));
  EXPECT_TRUE(validateModule(*module));
  for (const auto declaration :
       {".entry k(.param .texref tex[][2]) {}",
        ".entry k(.param .texref tex[2][]) {}",
        ".extern .func f(.param .texref tex[2][3]);",
        ".entry k(.param .texref tex[2][18446744073709551615]) {}"})
    EXPECT_FALSE(rank_module(rank_header() + declaration));
}

TEST(HeaderArrayRank, PreservesSourceIdentityBeyondAbiEquivalentCounts) {
  auto original = test_helpers::parseModule(rank_header() +
                                            ".entry k(.param .u32 a[2][3]) {}");
  ASSERT_TRUE(original);
  auto module = resolveAndValidateModule(*original);
  ASSERT_TRUE(module);
  auto& function = std::get<syntax_ast::AstFunction>(original->items.back());
  const auto original_signature =
      declaration_semantics::functionSignature(function);
  auto& dimensions = function.parameters.front().array_dimensions;
  dimensions.resize(1);
  std::get<syntax_ast::AstConstantLiteral>(dimensions.front().size->node)
      .value.syntax.text = "6";
  EXPECT_EQ(original_signature,
            declaration_semantics::functionSignature(function));
  const auto stale = validateModule(*original, *module);
  ASSERT_FALSE(stale);
  EXPECT_TRUE(std::ranges::any_of(stale.error(), [](const auto& diagnostic) {
    return diagnostic.kind ==
           checker::CheckDiagnosticKind::ModuleSourceMismatch;
  }));
  EXPECT_TRUE(validateModule(*module));
}

TEST(HeaderArrayRank, EntryLimitsAndByteOverflowUseEveryAxis) {
  for (const auto extra : {"", ", .param .b8 extra"}) {
    auto module = rank_module(
        std::string(
            ".version 8.0\n.target sm_70\n.entry k(.param .b8 a[2][2176]") +
        extra + ") {}");
    EXPECT_EQ(module.has_value(), std::string_view(extra).empty());
  }
  auto padded = rank_module(
      ".version 1.4\n.target sm_13\n.entry k(.param .b8 a[3][83], .param .v2 "
      ".u32 b) {}");
  EXPECT_FALSE(padded);
  EXPECT_FALSE(rank_module(
      rank_header() +
      ".extern .func f(.param .v4 .u32 a[1152921504606846976][2]);"));
  auto unknown =
      rank_module(rank_header() + ".extern .func f(.param .b8 a[][2][3]);");
  ASSERT_TRUE(unknown);
  auto invalid = *unknown;
  auto& function = invalid.functions.front();
  function.parameter_declarations.front().array_extents = {
      std::nullopt, std::numeric_limits<uint64_t>::max(), 2};
  function.contract.signature.parameters.front().array_extents = {
      std::nullopt, std::numeric_limits<uint64_t>::max(), uint64_t{2}};
  EXPECT_FALSE(validateModule(invalid));
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
