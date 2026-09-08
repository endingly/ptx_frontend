#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve owned input, destroying both source and AST before returning metadata. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> resolveSource(
    std::string source) {
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  EXPECT_TRUE(ast.has_value());
  EXPECT_TRUE(ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(
        ModuleResolveDiagnostics{{.message = ast.diagnostics.front().message}});
  return resolveModule(*ast);
}

/** Entry metadata remains usable after the source and syntax tree are gone. */
TEST(ResolvedEntryParameters, RetainsSingleParameterWithoutAst) {
  const auto resolved =
      resolveSource(".entry kernel(.param .u32 count) { ret; }");

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& parameters = resolved->functions.front().entry_parameters;
  ASSERT_EQ(parameters.size(), 1u);
  const auto& parameter = parameters.front();
  EXPECT_EQ(parameter.type, ".u32");
  EXPECT_EQ(parameter.alignment, 4u);
  EXPECT_FALSE(parameter.pointer);
  EXPECT_FALSE(parameter.is_array);
  EXPECT_FALSE(parameter.array_extent);
  const auto& symbol = resolved->symbols.symbol(parameter.symbol_id);
  EXPECT_EQ(symbol.name, "count");
  EXPECT_EQ(symbol.kind, binding::SymbolKind::InputParameter);
  EXPECT_EQ(symbol.state_space, syntax_ast::AstStateSpace::Parameter);
  EXPECT_EQ(resolved->symbols.scope(symbol.scope).owner,
            resolved->functions.front().symbol_id);
}

/** Representative GEMM inputs retain declaration order and separate alignments. */
TEST(ResolvedEntryParameters, RetainsGemmMetadataInSourceOrder) {
  const auto resolved = resolveSource(R"ptx(
.version 8.0
.target sm_80
.address_size 64
.visible .entry gemm(
    .param .align 16 .u64 .ptr .global .align 32 A,
    .param .u64 .ptr .global .align 16 B,
    .param .u64 .ptr .global .align 16 C,
    .param .u32 M,
    .param .u32 N,
    .param .u32 K,
    .param .u32 lda,
    .param .u32 ldb,
    .param .u32 ldc) { ret; }
)ptx");

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& function = resolved->functions.front();
  const auto& parameters = function.entry_parameters;
  constexpr std::array<std::string_view, 9> names{"A", "B",   "C",   "M",  "N",
                                                  "K", "lda", "ldb", "ldc"};
  ASSERT_EQ(parameters.size(), names.size());
  for (size_t index = 0; index < names.size(); ++index) {
    SCOPED_TRACE(names[index]);
    const auto& parameter = parameters[index];
    const auto& symbol = resolved->symbols.symbol(parameter.symbol_id);
    EXPECT_EQ(symbol.name, names[index]);
    EXPECT_EQ(symbol.kind, binding::SymbolKind::InputParameter);
    EXPECT_EQ(resolved->symbols.scope(symbol.scope).owner, function.symbol_id);
    EXPECT_EQ(parameter.type, index < 3 ? ".u64" : ".u32");
    EXPECT_EQ(parameter.alignment, index == 0 ? 16u : index < 3 ? 8u : 4u);
    EXPECT_FALSE(parameter.is_array);
    EXPECT_FALSE(parameter.array_extent);
    if (index < 3) {
      ASSERT_TRUE(parameter.pointer);
      EXPECT_EQ(parameter.pointer->pointed_state_space,
                call_argument_compatibility::PointedStateSpace::Global);
      EXPECT_EQ(parameter.pointer->pointed_alignment, index == 0 ? 32u : 16u);
    } else {
      EXPECT_FALSE(parameter.pointer);
    }
  }
}

/** Pointer defaults and all concrete target spaces survive contract lowering. */
TEST(ResolvedEntryParameters, RetainsPointerTargetSpacesAndDefaults) {
  const auto resolved = resolveSource(R"ptx(
.entry pointers(
    .param .u64 raw_address,
    .param .u64 .ptr generic_address,
    .param .u64 .ptr .local local_address,
    .param .u64 .ptr .shared .align 8 shared_address,
    .param .u64 .ptr .const .align 16 constant_address) { ret; }
)ptx");

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& parameters = resolved->functions.front().entry_parameters;
  ASSERT_EQ(parameters.size(), 5u);
  EXPECT_FALSE(parameters[0].pointer);
  ASSERT_TRUE(parameters[1].pointer);
  EXPECT_FALSE(parameters[1].pointer->pointed_state_space);
  EXPECT_EQ(parameters[1].pointer->pointed_alignment, 4u);
  using call_argument_compatibility::PointedStateSpace;
  constexpr std::array spaces{PointedStateSpace::Local,
                              PointedStateSpace::Shared,
                              PointedStateSpace::Constant};
  for (size_t index = 0; index < spaces.size(); ++index) {
    ASSERT_TRUE(parameters[index + 2].pointer);
    EXPECT_EQ(parameters[index + 2].pointer->pointed_state_space,
              spaces[index]);
    EXPECT_EQ(parameters[index + 2].pointer->pointed_alignment, 4u << index);
    EXPECT_EQ(parameters[index + 2].alignment, 8u);
  }
}

/** Array extents are element counts; unsized arrays remain distinguishable. */
TEST(ResolvedEntryParameters, RetainsArrayShapeAndNormalizedExtent) {
  const auto resolved = resolveSource(R"ptx(
.entry arrays(.param .align 16 .b8 bytes[2 * 8],
              .param .u32 words[2 + 1],
              .param .align 8 .b8 unsized[],
              .param .b8 scalar) { ret; }
)ptx");

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& parameters = resolved->functions.front().entry_parameters;
  ASSERT_EQ(parameters.size(), 4u);
  EXPECT_EQ(parameters[0].type, ".b8");
  EXPECT_EQ(parameters[0].alignment, 16u);
  EXPECT_TRUE(parameters[0].is_array);
  EXPECT_EQ(parameters[0].array_extent, 16u);
  EXPECT_EQ(parameters[1].type, ".u32");
  EXPECT_EQ(parameters[1].alignment, 4u);
  EXPECT_TRUE(parameters[1].is_array);
  EXPECT_EQ(parameters[1].array_extent, 3u);
  EXPECT_EQ(parameters[2].alignment, 8u);
  EXPECT_TRUE(parameters[2].is_array);
  EXPECT_FALSE(parameters[2].array_extent);
  EXPECT_EQ(parameters[3].alignment, 1u);
  EXPECT_FALSE(parameters[3].is_array);
  EXPECT_FALSE(parameters[3].array_extent);
}

/** Repeated parameter names identify each entry's own inputs, not nested locals. */
TEST(ResolvedEntryParameters, RetainsParametersPerEntryScope) {
  const auto resolved = resolveSource(R"ptx(
.entry first(.param .u32 input) { ret; }
.entry second(.param .u32 input) {
  { .reg .u32 input; }
  ret;
}
.entry empty() { ret; }
.func (.param .u32 result) helper(.param .u32 input);
.func (.param .u32 result) helper(.param .u32 input) { ret; }
)ptx");

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 5u);
  ASSERT_EQ(resolved->functions[0].entry_parameters.size(), 1u);
  ASSERT_EQ(resolved->functions[1].entry_parameters.size(), 1u);
  const auto& first = resolved->symbols.symbol(
      resolved->functions[0].entry_parameters[0].symbol_id);
  const auto& second = resolved->symbols.symbol(
      resolved->functions[1].entry_parameters[0].symbol_id);
  EXPECT_NE(first.id, second.id);
  EXPECT_NE(first.scope, second.scope);
  EXPECT_EQ(first.name, "input");
  EXPECT_EQ(second.name, "input");
  EXPECT_EQ(first.kind, binding::SymbolKind::InputParameter);
  EXPECT_EQ(second.kind, binding::SymbolKind::InputParameter);
  EXPECT_EQ(resolved->symbols.scope(first.scope).owner,
            resolved->functions[0].symbol_id);
  EXPECT_EQ(resolved->symbols.scope(second.scope).owner,
            resolved->functions[1].symbol_id);
  EXPECT_TRUE(resolved->functions[2].entry_parameters.empty());
  EXPECT_TRUE(resolved->functions[3].entry_parameters.empty());
  EXPECT_TRUE(resolved->functions[4].entry_parameters.empty());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
