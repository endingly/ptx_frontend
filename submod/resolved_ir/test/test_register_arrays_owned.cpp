#include <gtest/gtest.h>

#include "ptx_source_identity.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Find the explicit array rule among unrelated integrity diagnostics. */
bool hasRegisterArrayRule(const checker::CheckResult& result,
                          SourceRange range) {
  return !result &&
         std::ranges::any_of(result.error(), [range](const auto& diagnostic) {
           return diagnostic.kind ==
                      checker::CheckDiagnosticKind::RuleViolation &&
                  diagnostic.message ==
                      "Register declarations cannot be arrays." &&
                  diagnostic.range == range;
         });
}

/** Source-independent validation checks unused declarations without storage records. */
TEST(RegisterArraysOwned, RejectsReboundUnusedArrayAfterSourceDestruction) {
  std::optional<ResolvedModule> module;
  SourceRange expected;
  {
    const std::string prefix =
        ".version 9.3\n.target sm_80\n.entry kernel() { ";
    PtxSyntaxParser original(prefix + ".reg .u32 value   ; ret; }");
    const auto ast = original.parseModule();
    ASSERT_TRUE(ast);
    auto resolved = resolveAndValidateModule(*ast);
    ASSERT_TRUE(resolved);
    module.emplace(std::move(*resolved));
    ASSERT_TRUE(module->storage_declarations.empty());
    ASSERT_TRUE(validateModule(*module));

    PtxSyntaxParser replacement(prefix + ".reg .u32 value[2]; ret; }");
    const auto replacement_ast = replacement.parseModule();
    ASSERT_TRUE(replacement_ast);
    ASSERT_TRUE(replacement_ast.diagnostics.empty());
    module->symbols = binding::bindSymbols(*replacement_ast).table;
    module->source_identity = detail::module_source_identity(*replacement_ast);
    module->functions.front().source_identity =
        detail::function_source_identity(
            std::get<syntax_ast::AstFunction>(replacement_ast->items.back()));
    const auto symbol = std::ranges::find(module->symbols.symbols(), "value",
                                          &binding::Symbol::name);
    ASSERT_NE(symbol, module->symbols.symbols().end());
    ASSERT_TRUE(symbol->is_array);
    expected = symbol->declaration_range;
    EXPECT_TRUE(hasRegisterArrayRule(validateModule(*replacement_ast, *module),
                                     expected));
  }
  const auto checked = validateModule(*module);
  EXPECT_TRUE(hasRegisterArrayRule(checked, expected));
  ASSERT_FALSE(checked);
  EXPECT_TRUE(std::ranges::none_of(checked.error(), [](const auto& diagnostic) {
    return diagnostic.kind ==
           checker::CheckDiagnosticKind::ModuleSourceMismatch;
  }));
}

/** A later array occurrence remains visible through a shared module identity. */
TEST(RegisterArraysOwned, RetainsArraysAcrossModuleRedeclarations) {
  for (const bool array_first : {false, true}) {
    std::optional<ResolvedModule> module;
    SourceRange expected;
    {
      const std::string prefix = ".version 1.4\n.target sm_10\n";
      PtxSyntaxParser original(prefix +
                               ".reg .u32 value; .entry kernel() { ret; }");
      const auto ast = original.parseModule();
      ASSERT_TRUE(ast);
      auto resolved = resolveAndValidateModule(*ast);
      ASSERT_TRUE(resolved);
      module.emplace(std::move(*resolved));
      const std::string declarations =
          array_first ? ".reg .u32 value[2]; .reg .u32 value;"
                      : ".reg .u32 value; .reg .u32 value[2];";
      PtxSyntaxParser replacement(prefix + declarations +
                                  " .entry kernel() { ret; }");
      const auto changed = replacement.parseModule();
      ASSERT_TRUE(changed);
      module->symbols = binding::bindSymbols(*changed).table;
      const auto symbol = std::ranges::find(module->symbols.symbols(), "value",
                                            &binding::Symbol::name);
      ASSERT_NE(symbol, module->symbols.symbols().end());
      EXPECT_TRUE(symbol->is_array);
      expected = symbol->declaration_range;
    }
    EXPECT_TRUE(module->storage_declarations.empty());
    EXPECT_TRUE(hasRegisterArrayRule(validateModule(*module), expected));
  }
}

/** Array records cannot hide register shape when the rebound symbol clears the bit. */
TEST(RegisterArraysOwned, RejectsStorageAndParameterRecordsBoundToRegisters) {
  for (const bool parameter : {false, true}) {
    const std::string prefix =
        ".version 9.3\n.target sm_80\n.entry kernel() { ";
    const std::string original =
        prefix + (parameter ? ".param " : ".local ") + ".u32 values[2]; ret; }";
    PtxSyntaxParser parser(original);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module) << module.error().front().message;
    std::string replacement = original;
    replacement.replace(replacement.find(parameter ? ".param " : ".local "), 7,
                        ".reg   ");
    replacement.replace(replacement.find("[2]"), 3, "   ");
    PtxSyntaxParser rebound(replacement);
    const auto rebound_ast = rebound.parseModule();
    ASSERT_TRUE(rebound_ast);
    module->symbols = binding::bindSymbols(*rebound_ast).table;
    const auto symbol = std::ranges::find(module->symbols.symbols(), "values",
                                          &binding::Symbol::name);
    ASSERT_NE(symbol, module->symbols.symbols().end());
    ASSERT_FALSE(symbol->is_array);
    const SourceRange expected =
        parameter ? module->functions.front().range
                  : module->storage_declarations.front().range;
    EXPECT_TRUE(hasRegisterArrayRule(validateModule(*module), expected));
  }
}

/** Register formal arrays retain rejection while ordinary register formals survive. */
TEST(RegisterArraysOwned, PreservesFormalArrayRejection) {
  for (const std::string_view source :
       {".version 9.3\n.target sm_80\n.func device(.reg .u32 values[2]) { ret; "
        "}",
        ".version 9.3\n.target sm_80\n.func (.reg .u32 values[2]) device() { "
        "ret; }"}) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    EXPECT_FALSE(resolveModuleOnly(*ast));
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
