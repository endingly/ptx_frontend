#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/model/control_flow/call.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/ld.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/st.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Build complete call context without treating compact members as allocated arrays. */
std::string parameterGroupSource(std::string_view declarations,
                                 std::string_view instructions) {
  return ".version 9.3\n.target sm_90\n.address_size 64\n"
         ".func take(.param .u32 input); "
         ".func (.param .u32 result) exchange(.param .u32 input); "
         ".entry kernel() { .reg .u32 %r; " +
         std::string(declarations) + std::string(instructions) + " }";
}

/** Resolve and release source/parser/AST before owned validation in the caller. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> resolveParameterGroup(
    std::string source, bool check_source = true) {
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  EXPECT_TRUE(ast.has_value());
  EXPECT_TRUE(ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return check_source ? resolveAndValidateModule(*ast)
                      : resolveModuleOnly(*ast);
}

/** Inspect the sole call actual as a typed declaration/member identity. */
ResolvedCallParameterRef& groupCallArgument(Instruction& instruction) {
  auto& call = dynamic_cast<CallDirect&>(instruction);
  return std::get<ResolvedCallParameterRef>(
      call.arguments->value.values.front().value);
}

/** Unused nested groups retain one symbol and parameter record per declarator. */
TEST(BodyParameterGroups,
     RetainsCompactCountAndPerMemberExtentWithoutStorageRecords) {
  for (const auto check_source : {true, false}) {
    const auto module = resolveParameterGroup(
        parameterGroupSource(".param .align 8 .u32 value<3>; "
                             "{ .param .u64 nested<2>; }",
                             "ret;"),
        check_source);
    ASSERT_TRUE(module.has_value()) << module.error().front().message;
    EXPECT_TRUE(module->storage_declarations.empty());
    const auto& function = module->functions.back();
    ASSERT_EQ(function.parameter_declarations.size(), 2u);
    const auto& value = function.parameter_declarations[0];
    EXPECT_EQ(value.role, ParameterDeclarationRole::BodyLocal);
    EXPECT_EQ(value.scalar_type, ScalarType::U32);
    EXPECT_EQ(value.byte_extent, 4u);
    EXPECT_EQ(value.alignment, 8u);
    EXPECT_TRUE(value.array_extents.empty());
    EXPECT_EQ(value.vector_width, 1u);
    const auto& symbol = module->symbols.symbol(value.symbol_id);
    EXPECT_EQ(symbol.name, "value");
    EXPECT_EQ(symbol.parameterized_count, 3u);
    EXPECT_EQ(symbol.kind, binding::SymbolKind::CallParameter);
    for (uint32_t member : {0u, 2u}) {
      const auto lookup = module->symbols.lookup(
          value.scope_id, "value" + std::to_string(member));
      ASSERT_TRUE(lookup);
      EXPECT_EQ(lookup->symbol, value.symbol_id);
      EXPECT_EQ(lookup->parameterized_index, member);
    }
    EXPECT_FALSE(module->symbols.lookup(value.scope_id, "value3"));
    const auto& nested = function.parameter_declarations[1];
    EXPECT_NE(nested.scope_id, value.scope_id);
    EXPECT_EQ(nested.byte_extent, 8u);
    EXPECT_EQ(module->symbols.symbol(nested.symbol_id).parameterized_count, 2u);
    EXPECT_TRUE(validateModule(*module));
    const auto copy = *module;
    EXPECT_TRUE(validateModule(copy));
  }
}

/** Stores, call actuals/returns, and loads retain distinct member identities after cloning. */
TEST(BodyParameterGroups, CallsUseDistinctMembersAndPreservePerMemberAbi) {
  const std::string body =
      "st.param.u32 [value0], %r; "
      "call (value1), exchange, (value0); "
      "ld.param.u32 %r, [value1];";
  for (const auto declaration :
       {".param .u32 value<2>; ", ".param .u32 value0, value1; "}) {
    auto module =
        resolveParameterGroup(parameterGroupSource(declaration, body));
    ASSERT_TRUE(module.has_value()) << module.error().front().message;
    auto& function = module->functions.back();
    ASSERT_EQ(function.body.size(), 3u);
    const auto& store =
        dynamic_cast<const StExplicitScalar&>(*function.body[0]);
    const auto& stored = std::get<ResolvedSymbolRef>(store.address.value.base);
    const auto& call = dynamic_cast<const CallDirect&>(*function.body[1]);
    const auto& argument = std::get<ResolvedCallParameterRef>(
        call.arguments->value.values.front().value);
    ASSERT_TRUE(call.return_value);
    const auto& result = call.return_value->value;
    EXPECT_EQ(stored.symbol_id, argument.symbol_id);
    EXPECT_EQ(argument.declared_type, ScalarType::U32);
    EXPECT_EQ(result.declared_type, ScalarType::U32);
    const auto& load = dynamic_cast<const LdExplicitScalar&>(*function.body[2]);
    const auto& loaded = std::get<ResolvedSymbolRef>(load.address.value.base);
    EXPECT_EQ(loaded.symbol_id, result.symbol_id);
    const bool compact =
        std::string_view(declaration).find('<') != std::string_view::npos;
    EXPECT_EQ(function.parameter_declarations.size(), compact ? 1u : 2u);
    if (compact) {
      EXPECT_EQ(argument.symbol_id, result.symbol_id);
      EXPECT_EQ(stored.parameterized_index, 0u);
      EXPECT_EQ(argument.parameterized_index, 0u);
      EXPECT_EQ(result.parameterized_index, 1u);
      EXPECT_EQ(loaded.parameterized_index, 1u);
    } else {
      EXPECT_NE(argument.symbol_id, result.symbol_id);
      EXPECT_FALSE(argument.parameterized_index);
      EXPECT_FALSE(result.parameterized_index);
    }
    for (const auto& parameter : function.parameter_declarations)
      EXPECT_EQ(parameter.byte_extent, 4u);
    EXPECT_TRUE(validateModule(*module));
    auto copy = *module;
    copy.functions.back().body[1] = function.body[1]->clone();
    EXPECT_TRUE(validateModule(copy));
  }
}

/** Lexical shadowing picks the local group while staging compares exact members. */
TEST(BodyParameterGroups, ResolvesShadowingAndRejectsMismatchedStaging) {
  auto module = resolveParameterGroup(parameterGroupSource(
      ".param .u32 value<2>; ",
      "call take, (value0); { .param .u32 value<3>; call take, (value2); } "
      "call take, (value1);"));
  ASSERT_TRUE(module.has_value()) << module.error().front().message;
  auto& function = module->functions.back();
  ASSERT_EQ(function.parameter_declarations.size(), 2u);
  ASSERT_EQ(function.body.size(), 3u);
  const auto& first = groupCallArgument(*function.body[0]);
  const auto& nested = groupCallArgument(*function.body[1]);
  const auto& last = groupCallArgument(*function.body[2]);
  EXPECT_EQ(first.symbol_id, function.parameter_declarations[0].symbol_id);
  EXPECT_EQ(last.symbol_id, first.symbol_id);
  EXPECT_EQ(nested.symbol_id, function.parameter_declarations[1].symbol_id);
  EXPECT_NE(nested.symbol_id, first.symbol_id);
  EXPECT_EQ(first.parameterized_index, 0u);
  EXPECT_EQ(nested.parameterized_index, 2u);
  EXPECT_EQ(last.parameterized_index, 1u);
  EXPECT_TRUE(validateModule(*module));
  EXPECT_FALSE(resolveParameterGroup(
      parameterGroupSource(".param .u32 value<2>; ",
                           "st.param.u32 [value0], %r; call take, (value1);")));
}

/** Owned call members use typed identity; missing/extra/out-of-range indices remain invalid. */
TEST(BodyParameterGroups, ValidatesOwnedMemberDomainAndCoherentSemanticEdits) {
  const auto baseline = resolveParameterGroup(
      parameterGroupSource(".param .u32 value<2>; ", "call take, (value0);"));
  ASSERT_TRUE(baseline.has_value()) << baseline.error().front().message;
  for (const int mutation : {0, 1, 2, 3, 4}) {
    auto changed = *baseline;
    auto& function = changed.functions.back();
    auto& actual = groupCallArgument(*function.body.front());
    if (mutation == 0)
      actual.parameterized_index = 2;
    else if (mutation == 1)
      actual.parameterized_index.reset();
    else if (mutation == 2)
      function.parameter_declarations.front().role =
          ParameterDeclarationRole::EntryInput;
    else if (mutation == 3)
      function.parameter_declarations.front().scalar_type = ScalarType::U64;
    else
      function.parameter_declarations.front().vector_width = 2;
    EXPECT_FALSE(validateModule(changed));
  }
  auto edited = *baseline;
  groupCallArgument(*edited.functions.back().body.front()).parameterized_index =
      1;
  EXPECT_TRUE(validateModule(edited));

  auto ordinary = resolveParameterGroup(
      parameterGroupSource(".param .u32 value; ", "call take, (value);"));
  ASSERT_TRUE(ordinary.has_value());
  groupCallArgument(*ordinary->functions.back().body.front())
      .parameterized_index = 0;
  EXPECT_FALSE(validateModule(*ordinary));

  auto address = resolveParameterGroup(
      parameterGroupSource(".param .u32 value<2>; ",
                           "st.param.u32 [value0], %r; call take, (value0);"));
  ASSERT_TRUE(address.has_value());
  auto& store =
      dynamic_cast<StExplicitScalar&>(*address->functions.back().body.front());
  std::get<ResolvedSymbolRef>(store.address.value.base)
      .parameterized_index.reset();
  EXPECT_FALSE(validateModule(*address));
}

/** Owned parameter references cannot cross their owning function's scope. */
TEST(BodyParameterGroups, RejectsOwnedMemberReferenceToAnotherFunction) {
  auto module = resolveParameterGroup(
      ".version 9.3\n.target sm_90\n"
      ".func take(.param .u32 input); "
      ".func other() { .param .u32 hidden<2>; ret; } "
      ".entry kernel() { .param .u32 value<2>; call take, (value0); }");
  ASSERT_TRUE(module.has_value()) << module.error().front().message;
  groupCallArgument(*module->functions.back().body.front()).symbol_id =
      module->functions[1].parameter_declarations.front().symbol_id;
  EXPECT_FALSE(validateModule(*module));
}

/** Compact spelling does not bypass body-local parameter availability requirements. */
TEST(BodyParameterGroups, PreservesVersionAndTargetMinima) {
  for (const auto& [version, target] :
       {std::pair{"2.0", "sm_20"}, std::pair{"1.5", "sm_13"},
        std::pair{"2.0", "sm_13"}}) {
    const auto module = resolveParameterGroup(
        ".version " + std::string(version) + "\n.target " + target +
        "\n.entry kernel() { .param .u32 value<2>; ret; }");
    EXPECT_EQ(module.has_value(), std::string_view(target) == "sm_20");
    if (module)
      EXPECT_TRUE(validateModule(*module));
  }
}

/** Source associations retain the compact count and exact written member reference. */
TEST(BodyParameterGroups, RejectsChangedSourceCountAndMemberAssociations) {
  const auto original =
      parameterGroupSource(".param .u32 value<2>; ", "call take, (value0);");
  const auto module = resolveParameterGroup(original);
  ASSERT_TRUE(module.has_value());
  for (const auto& changed :
       {parameterGroupSource(".param .u32 value<3>; ", "call take, (value0);"),
        parameterGroupSource(".param .u32 value<2>; ",
                             "call take, (value1);")}) {
    PtxSyntaxParser parser(changed);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast.has_value());
    ASSERT_TRUE(ast.diagnostics.empty());
    EXPECT_FALSE(validateModule(*ast, *module));
  }
}

/** Bounds, count syntax, overlap, and shape constraints remain independent of admission. */
TEST(BodyParameterGroups, PreservesSourceBoundsCountsAndShapeRestrictions) {
  for (const auto member : {"value", "value2"}) {
    EXPECT_FALSE(resolveParameterGroup(
        parameterGroupSource(".param .u32 value<2>; ",
                             "call take, (" + std::string(member) + ");")));
  }
  for (const auto declaration :
       {".param .u32 value<0>; ", ".param .u32 value<4294967296>; ",
        ".param .u32 value<2>[2]; ", ".param .u32 value<2> = 1; ",
        ".param .u32 value<2>; .param .u32 value1; "}) {
    PtxSyntaxParser parser(parameterGroupSource(declaration, "ret;"));
    const auto ast = parser.parseModule();
    if (ast && ast.diagnostics.empty())
      EXPECT_FALSE(resolveModuleOnly(*ast));
    else
      EXPECT_FALSE(ast.diagnostics.empty());
  }
  const auto array = resolveParameterGroup(
      parameterGroupSource(".param .u32 matrix[2][3]; ", "ret;"));
  ASSERT_TRUE(array.has_value()) << array.error().front().message;
  EXPECT_EQ(array->functions.back().parameter_declarations.front().byte_extent,
            24u);
  EXPECT_TRUE(validateModule(*array));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
