#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/model/control_flow/call.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Provide modern complete context independently of call declaration order. */
std::string visibility_module(std::string_view body) {
  return ".version 9.3\n.target sm_80\n.address_size 64\n" + std::string(body);
}
/** Resolve without retaining parser/source/AST storage. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> owned_visibility(
    std::string_view body) {
  const auto parsed = test_helpers::parseModule(visibility_module(body));
  EXPECT_TRUE(parsed && parsed.diagnostics.empty());
  if (!parsed || !parsed.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveAndValidateModule(*parsed);
}
/** Borrow the named caller rather than depend on prototype/definition list positions. */
ResolvedFunction& visibility_caller(ResolvedModule& module) {
  return *std::ranges::find(module.functions, std::string{"kernel"},
                            &ResolvedFunction::name);
}
/** Borrow the actual direct target payload for semantic owned mutations. */
WithLocs<ResolvedFunctionRef>& direct_target(ResolvedModule& module) {
  return *dynamic_cast<CallDirect&>(*visibility_caller(module).body.front())
              .target_direct_call_target;
}

/** Prototype occurrences, definitions, nested bodies and modern recursion establish visibility. */
TEST(CallTargetVisibility,
     AcceptsPriorDeclarationsDefinitionsAndModernRecursion) {
  for (const auto source :
       {".visible .func f(); .entry kernel() { call f; } .visible .func f() { "
        "ret; }",
        ".func f(); .entry kernel() { call f; } .func f() { ret; }",
        ".extern .func f(); .entry kernel() { call f; }",
        ".func f() { ret; } .entry kernel() { { call f; } }",
        ".func f(); .func f(); .entry kernel() { call f; }",
        ".func self() { call self; ret; }",
        ".func b(); .func a() { call b; } .func b() { call a; }",
        ".func alias_fn(); .func target() { ret; } .alias alias_fn,target; "
        ".entry kernel() { call alias_fn; }"}) {
    SCOPED_TRACE(source);
    auto module = owned_visibility(source);
    ASSERT_TRUE(module) << (module.error().empty()
                                ? ""
                                : module.error().front().message);
    auto copy = *module;
    for (auto& function : copy.functions)
      for (auto& instruction : function.body)
        instruction = instruction->clone();
    EXPECT_TRUE(validateModule(copy));
  }
}

/** Whole-module lookup cannot turn later-only functions or alias prototypes into visible callees. */
TEST(CallTargetVisibility,
     RejectsLateTargetsAcrossSourceResolutionEntrypoints) {
  for (const auto source :
       {".entry kernel() { call f; } .extern .func f();",
        ".entry kernel() { call f; } .func f() { ret; }",
        ".func a() { call b; } .func b() { call a; }",
        ".func target() { ret; } .entry kernel() { call alias_fn; } .func "
        "alias_fn(); .alias alias_fn,target;"}) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseModule(visibility_module(source));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto source_only = resolveModuleOnly(*parsed);
    ASSERT_FALSE(source_only);
    ASSERT_FALSE(source_only.error().empty());
    EXPECT_NE(
        source_only.error().front().message.find("before its direct call"),
        std::string::npos);
    EXPECT_FALSE(resolveModule(*parsed));
    EXPECT_FALSE(resolveAndValidateModule(*parsed));
  }
}

/** Current target identity is checked at the retained use event, independently of spelling/history. */
TEST(CallTargetVisibility, RevalidatesCopiedCallsAndCoherentOwnedRetargets) {
  auto module = owned_visibility(
      ".func before() { ret; } .func earlier() { ret; } .entry other() {} "
      ".global .u32 data; .entry kernel() { call before; } .func later() { "
      "ret; }");
  ASSERT_TRUE(module) << module.error().front().message;
  for (const auto name : {"earlier", "later", "other", "data"}) {
    auto copy = *module;
    visibility_caller(copy).body[0] = visibility_caller(copy).body[0]->clone();
    auto& target = direct_target(copy).value;
    const auto symbol = copy.symbols.lookup(copy.symbols.moduleScope(), name);
    ASSERT_TRUE(symbol);
    target.symbol_id = symbol->symbol;
    target.spelling = "diagnostic_spelling_need_not_rejoin";
    const auto valid = validateModule(copy);
    EXPECT_EQ(valid.has_value(), name == std::string_view("earlier"));
    if (name == std::string_view("later")) {
      ASSERT_FALSE(valid);
      EXPECT_TRUE(
          std::ranges::any_of(valid.error(), [](const auto& diagnostic) {
            return diagnostic.message.find("before its direct call") !=
                   std::string::npos;
          }));
    }
  }
  auto reordered = *module;
  std::ranges::reverse(reordered.functions);
  EXPECT_TRUE(validateModule(reordered));
  auto spelling = *module;
  direct_target(spelling).value.spelling = "unrelated_diagnostic_text";
  EXPECT_TRUE(validateModule(spelling));
}

/** Missing, ambiguous and misplaced locations cannot manufacture declaration-order evidence. */
TEST(CallTargetVisibility, RejectsMalformedOwnedTargetContext) {
  auto module = owned_visibility(".func f(); .entry kernel() { call f; }");
  ASSERT_TRUE(module) << module.error().front().message;
  for (int mutation = 0; mutation < 8; ++mutation) {
    auto copy = *module;
    auto& target = direct_target(copy);
    auto& caller = visibility_caller(copy);
    if (mutation == 0)
      target.locs.clear();
    if (mutation == 1)
      target.locs.push_back(target.locs.front());
    if (mutation == 2)
      target.locs.front() = {};
    if (mutation == 3)
      target.locs.front() = copy.range;
    if (mutation == 4) {
      const auto start = caller.instruction_ranges.front().start;
      target.locs.front() = {start, {start.line, start.column + 1}};
    }
    if (mutation == 5)
      target.value.symbol_id.reset();
    if (mutation == 6)
      target.value.symbol_id = binding::SymbolId{
          static_cast<uint32_t>(copy.symbols.symbols().size() + 1)};
    if (mutation == 7)
      caller.instruction_ranges.clear();
    const auto invalid = validateModule(copy);
    EXPECT_FALSE(invalid);
    if (mutation == 4) {
      ASSERT_FALSE(invalid);
      EXPECT_TRUE(
          std::ranges::any_of(invalid.error(), [](const auto& diagnostic) {
            return diagnostic.message.find("no unambiguous lexical context") !=
                   std::string::npos;
          }));
    }
  }
}

/** Indexed malformed endpoints fail the positive-range guard before successful order lookup. */
TEST(CallTargetVisibility, RejectsNonpositiveRangeEndpointsInIndexedContext) {
  for (bool use_end : {false, true}) {
    auto parsed = test_helpers::parseModule(
        visibility_module(".func f(); .entry kernel() { call f; }"));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto& function = std::get<syntax_ast::AstFunction>(parsed->items.back());
    auto& instruction =
        std::get<syntax_ast::AstInstruction>(function.body.front());
    auto& target =
        std::get<syntax_ast::AstCallTarget>(instruction.operands.front());
    if (use_end) {
      target.name.syntax.range.end = {target.name.syntax.range.start.line + 1,
                                      0};
      target.range = target.name.syntax.range;
    }
    instruction.range.end = {instruction.range.end.line + 2, use_end ? 1 : 0};
    const auto bound = binding::bindSymbols(*parsed);
    const auto symbol = bound.table.lookup(bound.table.moduleScope(), "f");
    ASSERT_TRUE(symbol);
    ASSERT_EQ(bound.table.hasPriorDeclaration(symbol->symbol, target.range),
              true);
    const auto invalid = resolveModuleOnly(*parsed);
    ASSERT_FALSE(invalid);
    EXPECT_TRUE(
        std::ranges::any_of(invalid.error(), [](const auto& diagnostic) {
          return diagnostic.message.find(
                     "one nonempty location inside its instruction") !=
                 std::string::npos;
        }));
  }
}

/** Partial-context instruction tools and real indirect layouts retain their existing contracts. */
TEST(CallTargetVisibility, PreservesFragmentsAndIndirectCalls) {
  const auto fragment = test_helpers::parseInstruction("call f;");
  ASSERT_TRUE(fragment);
  EXPECT_TRUE(resolveInstruction(*fragment));
  const auto declarations = test_helpers::parseModule(
      visibility_module(".extern .func f(); .entry kernel() {}"));
  ASSERT_MODULE_PARSE_SUCCEEDS(declarations);
  const auto bound = binding::bindSymbols(*declarations);
  ASSERT_TRUE(bound.diagnostics.empty());
  ResolveContext context{.symbols = bound.table,
                         .scope = bound.table.moduleScope()};
  const auto function = bound.table.lookup(bound.table.moduleScope(), "f");
  ASSERT_TRUE(function);
  const auto& fragment_target =
      std::get<syntax_ast::AstCallTarget>(fragment->operands.front());
  EXPECT_FALSE(
      bound.table.hasPriorDeclaration(function->symbol, fragment_target.range)
          .has_value());
  EXPECT_TRUE(resolveInstruction(*fragment, context));
  for (const auto metadata :
       {"proto: .callprototype _ ();", "proto: .calltargets f;"}) {
    auto module = owned_visibility(
        std::string(".func f(); .entry kernel() { .reg .u64 pointer; ") +
        metadata + " call pointer, (), proto; }");
    ASSERT_TRUE(module) << module.error().front().message;
    EXPECT_TRUE(validateModule(*module));
  }
}
/** Existing function-address MOV admission is independent of the direct-call check. */
TEST(CallTargetVisibility, DoesNotApplyCallVisibilityToFunctionAddressMov) {
  auto module = owned_visibility(
      ".entry kernel() { .reg .u64 pointer; mov.u64 pointer, f; } .func f();");
  ASSERT_TRUE(module) << module.error().front().message;
  EXPECT_TRUE(validateModule(*module));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
