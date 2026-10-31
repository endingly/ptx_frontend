#include <gtest/gtest.h>

#include "ptx_source_identity.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ptx_frontend/base/ptx_identifier.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/add.gen.hpp>
#include <ptx_frontend/resolved_ir/model/control_flow/call.gen.hpp>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/fabric.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve complete contextual-name text and release parser/AST before returning. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> contextual_name_module(
    std::string source) {
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  EXPECT_TRUE(ast && ast.diagnostics.empty()) << source;
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  auto resolved = resolveAndValidateModule(*ast);
  if (resolved)
    EXPECT_TRUE(validateModule(*ast, *resolved));
  return resolved;
}

/** Official initializer names are ordinary global symbols, not instruction nodes. */
TEST(DeclarationIdentifiers, PreservesOfficialBarObjectAndGenericReferences) {
  auto module = contextual_name_module(R"ptx(
.version 9.3
.target sm_80
.address_size 32
.global .u32 bar[] = {2, 3, 5};
.global .u32 parr[] = {generic(bar), generic(bar)+4, generic(bar)+8};
)ptx");
  ASSERT_TRUE(module) << (module.error().empty()
                              ? ""
                              : module.error().front().message);
  ASSERT_EQ(module->storage_declarations.size(), 2u);
  const auto& object = module->storage_declarations[0];
  const auto& pointers = module->storage_declarations[1];
  EXPECT_EQ(module->symbols.symbol(object.symbol_id).name, "bar");
  ASSERT_EQ(object.initializer.size(), 3u);
  EXPECT_EQ(object.array_extents, (std::vector<std::optional<uint64_t>>{3}));
  ASSERT_EQ(pointers.initializer.size(), 3u);
  for (std::size_t index = 0; index < pointers.initializer.size(); ++index) {
    const auto* relocation =
        std::get_if<StorageRelocation>(&pointers.initializer[index].value);
    ASSERT_NE(relocation, nullptr);
    EXPECT_EQ(relocation->symbol_id, object.symbol_id);
    EXPECT_EQ(relocation->address_kind, StorageAddressKind::Generic);
    EXPECT_EQ(relocation->addend_bits, index * 4);
  }
  auto copied = *module;
  EXPECT_TRUE(validateModule(copied));
}

/** Official bar function naming preserves ABI, source order and typed owned targets. */
TEST(DeclarationIdentifiers,
     PreservesOfficialBarFunctionAndOwnedKeywordTargets) {
  auto module = contextual_name_module(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.func (.reg .s32 out) bar(.reg .s32 x, .param .align 8 .b8 y[12]) {
  .reg .f64 f1;
  ld.param.f64 f1, [y];
  add.s32 out, x, 1;
  ret;
}
.func (.reg .s32 out) fabric(.reg .s32 x, .param .align 8 .b8 y[12]) {
  add.s32 out, x, 2;
  ret;
}
.entry kernel() {
  .reg .s32 out, x;
  .param .align 8 .b8 py[12];
  call (out), bar, (x, py);
  ret;
}
)ptx");
  ASSERT_TRUE(module) << (module.error().empty()
                              ? ""
                              : module.error().front().message);
  auto copied = *module;
  for (auto& function : copied.functions)
    for (auto& instruction : function.body)
      instruction = instruction->clone();
  auto& call = dynamic_cast<CallDirect&>(*copied.functions.back().body.front());
  ASSERT_TRUE(call.target_direct_call_target);
  auto& target = call.target_direct_call_target->value;
  ASSERT_TRUE(target.symbol_id);
  EXPECT_EQ(copied.symbols.symbol(*target.symbol_id).name, "bar");
  target.spelling = "add";
  EXPECT_TRUE(validateModule(copied));
  const auto fabric =
      copied.symbols.lookup(copied.symbols.moduleScope(), "fabric");
  ASSERT_TRUE(fabric);
  target.symbol_id = fabric->symbol;
  EXPECT_EQ(copied.symbols.symbol(*target.symbol_id).name, "fabric");
  EXPECT_TRUE(validateModule(copied));
  EXPECT_NE(copied.functions.back().body.front().get(),
            module->functions.back().body.front().get());
}

/** The explicit role, rather than keyword membership, determines name use. */
TEST(DeclarationIdentifiers, AcceptsContextualKeywordsAcrossDeclarationRoles) {
  /** Representative explicit name roles share the same character-grammar check. */
  struct Case {
    /** Expected authoritative bound name. */
    const char* name;
    /** Complete role-specific body following the common modern header. */
    const char* body;
    /** Existing binding category that must remain unchanged. */
    binding::SymbolKind kind;
  };
  const Case cases[] = {
      {"add", ".entry k(.param .u32 add) { ret; }",
       binding::SymbolKind::InputParameter},
      {"ret", ".entry k() { bra ret; ret: ret; }", binding::SymbolKind::Label},
      {"mov",
       ".func mov(.reg .u32 x); .func target(.reg .u32 x) { ret; } .alias mov, "
       "target; .entry k() { .reg .u32 x; call mov, (x); }",
       binding::SymbolKind::Function},
      {"call",
       ".entry k() { .reg .b64 fp; .reg .u32 x; call: .callprototype _ (.reg "
       ".u32 value); call fp, (x), call; }",
       binding::SymbolKind::CallPrototype},
      {"bra",
       ".extern .func target(.reg .u32 x); .entry k() { .reg .b64 fp; .reg "
       ".u32 x; bra: .calltargets target; call fp, (x), bra; }",
       binding::SymbolKind::CallTargetSet},
      {"add",
       ".entry k() { .reg .u32 index; add: .branchtargets done; brx.idx index, "
       "add; done: ret; }",
       binding::SymbolKind::BranchTargetSet},
      {"fabric",
       ".extern .func take(.param .u32 x); .entry k() { .param .u32 fabric; "
       "call take, (fabric); }",
       binding::SymbolKind::CallParameter},
      {"fabric", ".global .u32 fabric; .entry k() { ret; }",
       binding::SymbolKind::Variable},
  };
  for (const auto& item : cases) {
    SCOPED_TRACE(item.name);
    SCOPED_TRACE(item.body);
    auto module = contextual_name_module(
        std::string(".version 9.3\n.target sm_80\n.address_size 64\n") +
        item.body);
    ASSERT_TRUE(module) << (module.error().empty()
                                ? ""
                                : module.error().front().message);
    const auto symbol = std::ranges::find_if(
        module->symbols.symbols(), [&](const auto& symbol) {
          return symbol.name == item.name && symbol.kind == item.kind;
        });
    ASSERT_NE(symbol, module->symbols.symbols().end());
    auto copied = *module;
    for (auto& function : copied.functions)
      for (auto& instruction : function.body)
        instruction = instruction->clone();
    EXPECT_TRUE(validateModule(copied));
  }
}

/** Identifier case, prefixes and suffixes do not change the lowercase add opcode. */
TEST(DeclarationIdentifiers, DistinguishesKeywordNamesFromInstructionPosition) {
  constexpr std::string_view names[] = {"add",  "Add",  "ADD",  "_add", "$add",
                                        "%add", "add0", "add_", "add$"};
  std::string source = ".version 9.3\n.target sm_80\n.entry kernel() { ";
  for (const auto name : names)
    source += ".reg .u32 " + std::string(name) + "; add.u32 " +
              std::string(name) + ", " + std::string(name) + ", 1;";
  source += "ret; }";
  auto module = contextual_name_module(source);
  ASSERT_TRUE(module) << (module.error().empty()
                              ? ""
                              : module.error().front().message);
  auto copied = *module;
  const auto& function = copied.functions.front();
  ASSERT_EQ(function.body.size(), std::size(names) + 1);
  for (std::size_t index = 0; index < std::size(names); ++index) {
    const auto* add =
        dynamic_cast<const AddIntegerNoSat*>(function.body[index].get());
    ASSERT_NE(add, nullptr);
    ASSERT_TRUE(add->dst.value.symbol_id);
    EXPECT_EQ(copied.symbols.symbol(*add->dst.value.symbol_id).name,
              names[index]);
    EXPECT_EQ(add->dst.value.declared_type, ScalarType::U32);
    EXPECT_EQ(std::get<ResolvedRegisterRef>(add->src1.value).symbol_id,
              add->dst.value.symbol_id);
  }
  EXPECT_TRUE(validateModule(copied));
  for (const auto instruction : {"ADD.u32 x, x, 1;", "unknown.u32 x, x, 1;",
                                 "fabric;", "fabric.unknown;"}) {
    SCOPED_TRACE(instruction);
    auto invalid = contextual_name_module(
        std::string(
            ".version 9.3\n.target sm_100\n.entry k() { .reg .u32 x; ") +
        instruction + " }");
    EXPECT_FALSE(invalid);
  }
  auto fabric = contextual_name_module(
      ".version 9.3\n.target sm_100\n.global .u32 fabric; .entry k() { "
      "fabric.submit; ret; }");
  ASSERT_TRUE(fabric) << (fabric.error().empty()
                              ? ""
                              : fabric.error().front().message);
  EXPECT_NE(
      dynamic_cast<FabricSubmit*>(fabric->functions.front().body.front().get()),
      nullptr);
  EXPECT_TRUE(validateModule(*fabric));
  EXPECT_FALSE(
      contextual_name_module(".version 9.3\n.target sm_80\n.global .u32 "
                             "fabric; .entry k() { fabric.submit; }"));
}

/** Single-character punctuation identifiers must not become declarations. */
TEST(DeclarationIdentifiers, RejectsSingletonGlobalNames) {
  for (const std::string_view name : {"_", "$"}) {
    const std::string source = ".version 9.3\n.target sm_80\n.global .u32 " +
                               std::string(name) + ";\n";
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    ASSERT_TRUE(ast.diagnostics.empty());
    EXPECT_FALSE(resolveAndValidateModule(*ast)) << name;
  }
}

/** Every concrete declaration context reports its precise invalid name leaf. */
TEST(DeclarationIdentifiers, RejectsNamesAcrossDeclarationContexts) {
  const std::vector<std::string> declarations{
      ".global .u32 #;",
      ".global .u32 #<2>;",
      ".entry #() { ret; }",
      ".func #();",
      ".entry k(.param .u32 #) { ret; }",
      ".func k(.reg .u32 #);",
      ".func (.reg .u32 #) k();",
      ".entry k() { .reg .u32 #; ret; }",
      ".entry k() { { .local .u32 #; } ret; }",
      ".entry k() { .param .u32 #; ret; }",
      ".entry k() { #: ret; }",
      ".entry k() { #: .callprototype _ (); ret; }",
      ".func target();\n.entry k() { #: .calltargets target; ret; }",
      ".entry k() { #: .branchtargets done; done: ret; }",
      ".func target();\n.func #();\n.alias #, target;",
      ".section .debug_str { #: .b8 0; };"};
  for (const auto& pattern : declarations) {
    for (const std::string_view name : {"_", "$"}) {
      std::string source = ".version 9.3\n.target sm_80\n" + pattern;
      std::ranges::replace(source, '#', name.front());
      SCOPED_TRACE(source);
      PtxSyntaxParser parser(source);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast);
      ASSERT_TRUE(ast.diagnostics.empty());
      const auto binding = binding::bindSymbols(*ast);
      const auto diagnostics =
          declaration_semantics::checkDeclarations(*ast, binding.table);
      const auto diagnostic = std::ranges::find(
          diagnostics,
          declaration_semantics::DeclarationDiagnosticKind::InvalidIdentifier,
          &declaration_semantics::DeclarationDiagnostic::kind);
      ASSERT_NE(diagnostic, diagnostics.end());
      size_t expected_count = 0;
      for (size_t offset = pattern.find('#'); offset != std::string::npos;
           offset = pattern.find('#', offset + 1)) {
        ++expected_count;
        const auto preceding = std::string_view(pattern).substr(0, offset);
        const int32_t line =
            3 + static_cast<int32_t>(std::ranges::count(preceding, '\n'));
        const auto newline = preceding.rfind('\n');
        const int32_t column = static_cast<int32_t>(
            newline == std::string_view::npos ? offset + 1 : offset - newline);
        const SourceRange range{{line, column}, {line, column + 1}};
        EXPECT_TRUE(std::ranges::any_of(diagnostics, [&](const auto& value) {
          return value.kind ==
                     declaration_semantics::DeclarationDiagnosticKind::
                         InvalidIdentifier &&
                 value.range == range;
        }));
      }
      EXPECT_EQ(std::ranges::count(
                    diagnostics,
                    declaration_semantics::DeclarationDiagnosticKind::
                        InvalidIdentifier,
                    &declaration_semantics::DeclarationDiagnostic::kind),
                expected_count);
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
  }
}

/** The public declaration checker uses AST spellings even with a stale table. */
TEST(DeclarationIdentifiers, ChecksAstNamesIndependentlyOfBinding) {
  PtxSyntaxParser parser(".global .u32 a;\n.global .u32 a;");
  auto ast = parser.parseModule();
  ASSERT_TRUE(ast);
  const auto bound = binding::bindSymbols(*ast);
  for (auto& item : ast->items)
    std::get<syntax_ast::AstVariableDeclaration>(item)
        .declarators[0]
        .name.syntax.text = "$";
  const auto diagnostics =
      declaration_semantics::checkDeclarations(*ast, bound.table);
  std::vector<SourceRange> ranges;
  for (const auto& diagnostic : diagnostics)
    if (diagnostic.kind ==
        declaration_semantics::DeclarationDiagnosticKind::InvalidIdentifier)
      ranges.push_back(diagnostic.range);
  ASSERT_EQ(ranges.size(), 2u);
  EXPECT_EQ(ranges[0], (SourceRange{{1, 14}, {1, 15}}));
  EXPECT_EQ(ranges[1], (SourceRange{{2, 14}, {2, 15}}));
}

/** Prefixes remain legal in longer names and dummy/sink contexts stay distinct. */
TEST(DeclarationIdentifiers, PreservesLegalPrefixesAndContextualSinks) {
  std::optional<ResolvedModule> owned;
  {
    const std::string source = R"ptx(.version 9.3
.target sm_80
.global .u32 _x, $x, a, _g<2>, $g<2>;
.section .debug_str { debug_name: .b8 0; };
.file 1 "file.ptx"
.entry k() {
  .reg .b32 %r<2>;
  .reg .pred %p;
proto: .callprototype (.reg .u32 _) _ (.reg .u32 _);
  setp.eq.b32 _|%p, %r0, %r1;
  setp.eq.b32 %p|_, %r0, %r1;
  mov.v2.b32 {_,%r1}, {%r0,%r1};
  ret;
}
)ptx";
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    ASSERT_TRUE(ast.diagnostics.empty());
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result) << result.error().front().message;
    owned.emplace(std::move(*result));
  }
  EXPECT_TRUE(validateModule(*owned));
  for (const std::string source :
       {".entry k() { p: .callprototype _ (.reg .u32 $); ret; }",
        ".entry k() { p: .callprototype (.reg .u32 $) _ (); ret; }"}) {
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    ASSERT_TRUE(ast.diagnostics.empty());
    const auto bound = binding::bindSymbols(*ast);
    const auto diagnostics =
        declaration_semantics::checkDeclarations(*ast, bound.table);
    EXPECT_TRUE(std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
      return diagnostic.kind ==
             declaration_semantics::DeclarationDiagnosticKind::
                 InvalidIdentifier;
    }));
  }
}

/** Coherent owned names fail legality after both source and AST have gone away. */
TEST(DeclarationIdentifiers, RevalidatesOwnedAndReplacementNames) {
  for (const std::string_view name : {"_", "$"}) {
    for (const std::string_view pattern :
         {".global .u32 #;", ".global .u32 #<2>;", ".entry #() { ret; }",
          ".section .debug_str { #: .b8 0; };"}) {
      const bool function = pattern.starts_with(".entry");
      std::optional<ResolvedModule> module;
      SourceRange expected;
      {
        const std::string prefix = ".version 9.3\n.target sm_80\n";
        std::string valid(pattern);
        valid[valid.find('#')] = 'a';
        PtxSyntaxParser parser(prefix + valid);
        const auto ast = parser.parseModule();
        ASSERT_TRUE(ast);
        auto result = resolveAndValidateModule(*ast);
        ASSERT_TRUE(result);
        module.emplace(std::move(*result));
        std::string invalid = valid;
        invalid[pattern.find('#')] = name.front();
        PtxSyntaxParser replacement(prefix + invalid);
        const auto replacement_ast = replacement.parseModule();
        ASSERT_TRUE(replacement_ast);
        ASSERT_TRUE(replacement_ast.diagnostics.empty());
        module->symbols = binding::bindSymbols(*replacement_ast).table;
        module->source_identity =
            detail::module_source_identity(*replacement_ast);
        if (function) {
          module->functions.front().name = name;
          module->functions.front().source_identity =
              detail::function_source_identity(
                  std::get<syntax_ast::AstFunction>(
                      replacement_ast->items.back()));
        }
        const auto symbol = std::ranges::find(module->symbols.symbols(), name,
                                              &binding::Symbol::name);
        ASSERT_NE(symbol, module->symbols.symbols().end());
        expected = symbol->declaration_range;
        const auto validation = validateModule(*replacement_ast, *module);
        ASSERT_FALSE(validation);
        EXPECT_EQ(validation.error().front().kind,
                  checker::CheckDiagnosticKind::RuleViolation);
        EXPECT_EQ(validation.error().front().range, expected);
      }
      const auto validation = validateModule(*module);
      ASSERT_FALSE(validation);
      EXPECT_EQ(validation.error().front().kind,
                checker::CheckDiagnosticKind::RuleViolation);
      EXPECT_EQ(validation.error().front().range, expected);
    }
  }
}

/** The shared grammar rejects malformed constructed spellings without normalization. */
TEST(DeclarationIdentifiers, RecognizesExactAsciiGrammar) {
  for (const std::string_view name :
       {"a", "Z", "_a", "$a", "%r", "__", "$$", "%_", "a0_$"})
    EXPECT_TRUE(base::isIdentifier(name)) << name;
  for (const std::string_view name :
       {"", "_", "$", "%", "0a", "a%b", "a.b", "a b", "é"})
    EXPECT_FALSE(base::isIdentifier(name)) << name;
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
