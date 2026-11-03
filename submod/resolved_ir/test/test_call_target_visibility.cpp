#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/model/control_flow/call.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/mov.gen.hpp>
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
      std::get<syntax_ast::AstIdentifierRef>(target.name).syntax.range.end = {
          std::get<syntax_ast::AstIdentifierRef>(target.name)
                  .syntax.range.start.line +
              1,
          0};
      target.range =
          std::get<syntax_ast::AstIdentifierRef>(target.name).syntax.range;
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

/** Build a real initialized u32 target beside independent floating/narrow uses. */
std::string carrier_source(
    std::string_view type = "u32",
    std::string_view header = ".version 9.3\n.target sm_90\n.address_size 64\n",
    std::string_view extra = {}) {
  std::string source =
      std::string(header) + ".func callee(){ret;}\n.entry kernel(){\n.reg ." +
      std::string(type) +
      " pointer;\n.reg .f32 another;\n.reg .u32 secondp;\n"
      "mov.f32 another, 0f00000000;\nmov.u32 secondp, callee;\n";
  if (type == "u32")
    source += "mov.u32 pointer, callee;\n";
  if (type == "f32")
    source += "mov.f32 pointer, 0f00000000;\n";
  return source + std::string(extra) +
         "meta: .callprototype _ ();\ncall pointer, (), meta;\nret;\n}\n";
}
/** Borrow the actual indirect call after any initialization instructions. */
CallDirect& carrier_call(ResolvedModule& module) {
  for (const auto& instruction : module.functions.back().body)
    if (auto* call = dynamic_cast<CallDirect*>(instruction.get()))
      return *call;
  throw std::runtime_error("Missing indirect-call fixture");
}
/** Find an actual initialized register ref without synthesizing symbol metadata. */
ResolvedRegisterRef carrier_other(const ResolvedModule& module) {
  for (const auto& instruction : module.functions.back().body)
    if (const auto* mov = dynamic_cast<const MovScalar*>(instruction.get());
        mov && mov->dst_register &&
        mov->dst_register->value.spelling == "another")
      return mov->dst_register->value;
  throw std::runtime_error("Missing initialized floating fixture register");
}
/** Require the carrier-specific error at the real callee use, beside other errors. */
void expect_carrier_error(const checker::CheckResult& result,
                          SourceRange range) {
  ASSERT_FALSE(result);
  EXPECT_TRUE(std::ranges::any_of(result.error(), [&](const auto& d) {
    return d.range == range &&
           d.message.find("Indirect function address requires") !=
               std::string::npos;
  }));
}
/** Direct checking uses the instruction's containing range, independent of source. */
checker::Context carrier_context(const ResolvedModule& module) {
  const auto& function = module.functions.back();
  for (size_t i = 0; i < function.body.size(); ++i)
    if (dynamic_cast<const CallDirect*>(function.body[i].get()))
      return {.target = {.ptx_version = {9, 3}, .sm_version = 90},
              .instruction_range = function.instruction_ranges[i]};
  throw std::runtime_error("Missing call range");
}

/** All physical integer/bit widths remain eligible; source floats and b128 reject. */
TEST(IndirectFunctionCarrier, SourceDomainPreservesTwelveCarrierTypes) {
  for (const auto type : {"b8", "b16", "b32", "b64", "s8", "s16", "s32", "s64",
                          "u8", "u16", "u32", "u64"}) {
    SCOPED_TRACE(type);
    auto ast = test_helpers::parseModule(carrier_source(type));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module) << module.error().front().message;
    EXPECT_TRUE(carrier_call(*module).check(carrier_context(*module)));
    EXPECT_TRUE(validateModule(*module));
  }
  for (const auto type : {"f16", "f16x2", "f32", "f64", "b128"}) {
    SCOPED_TRACE(type);
    const auto source = carrier_source(type);
    auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    const auto result = resolveAndValidateModule(*ast);
    ASSERT_FALSE(result);
    ASSERT_EQ(result.error().size(), 1u);
    EXPECT_NE(result.error().front().message.find(
                  "Indirect function address requires"),
              std::string::npos);
    const auto offset = source.rfind("call pointer") + 5;
    const auto line =
        1 + std::count(source.begin(), source.begin() + offset, '\n');
    EXPECT_EQ(result.error().front().range,
              (SourceRange{{static_cast<int32_t>(line), 6},
                           {static_cast<int32_t>(line), 13}}));
  }
}

/** Current actual declarations, not retained source history or cache lies, govern. */
TEST(IndirectFunctionCarrier, CurrentFactsSurviveSourceDeathAndCopies) {
  std::optional<ResolvedModule> invalid;
  {
    const auto source = carrier_source();
    auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module);
    auto& call = carrier_call(*module);
    const auto original = call.target_indirect_callee->value;
    const auto identity = module->functions.back().source_identity;
    const auto range = call.target_indirect_callee->locs.front();
    call.target_indirect_callee->value = carrier_other(*module);
    EXPECT_EQ(module->functions.back().source_identity, identity);
    expect_carrier_error(call.check(carrier_context(*module)), range);
    expect_carrier_error(validateModule(*module), range);
    expect_carrier_error(validateModule(*ast, *module), range);
    invalid = *module;
    std::get<ResolvedRegisterRef>(call.target_indirect_callee->value)
        .declared_type = ScalarType::B32;
    expect_carrier_error(validateModule(*module), range);
    expect_carrier_error(validateModule(*ast, *module), range);
    std::get<ResolvedRegisterRef>(call.target_indirect_callee->value)
        .declared_type.reset();
    expect_carrier_error(validateModule(*module), range);
    call.target_indirect_callee->value = original;
    EXPECT_TRUE(validateModule(*module));
    EXPECT_TRUE(validateModule(*ast, *module));
    auto changed_source = source;
    changed_source.replace(changed_source.rfind("call pointer"), 12,
                           "call secondp");
    auto changed = test_helpers::parseModule(changed_source);
    ASSERT_MODULE_PARSE_SUCCEEDS(changed);
    ASSERT_TRUE(resolveAndValidateModule(*changed));
    const auto mismatch = validateModule(*changed, *module);
    ASSERT_FALSE(mismatch);
    EXPECT_TRUE(std::ranges::any_of(mismatch.error(), [](const auto& d) {
      return d.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
    }));
  }
  // Source, parser and AST objects have all died before the invalid owned checks.
  ResolvedModule copied(*invalid), assigned;
  assigned = copied;
  ResolvedFunction function(copied.functions.back()), function_assigned;
  function_assigned = function;
  assigned.functions.back() = function_assigned;
  for (size_t i = 0; i < function.body.size(); ++i)
    if (dynamic_cast<const CallDirect*>(function.body[i].get()))
      assigned.functions.back().body[i] = function.body[i]->clone();
  for (auto* module : {&*invalid, &copied, &assigned}) {
    auto& call = carrier_call(*module);
    const auto range = call.target_indirect_callee->locs.front();
    expect_carrier_error(validateModule(*module), range);
    expect_carrier_error(call.check(carrier_context(*module)), range);
  }
}

/** Selected narrow elements share the scalar rule; a real floating element fails. */
TEST(IndirectFunctionCarrier, RealComponentsUseTheSameDomainAfterSourceDeath) {
  for (const auto type : {"u16", "b16", "f32", "u64"}) {
    SCOPED_TRACE(type);
    std::optional<ResolvedModule> owned;
    {
      const std::string extra =
          ".reg .v2 ." + std::string(type) + " V;\n.reg ." + std::string(type) +
          " scratch;\nmov." + std::string(type) + " scratch, V.x;\n";
      auto ast = test_helpers::parseModule(carrier_source(
          "u32", ".version 9.3\n.target sm_90\n.address_size 64\n", extra));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      auto module = resolveAndValidateModule(*ast);
      ASSERT_TRUE(module) << module.error().front().message;
      std::optional<ResolvedRegisterRef> component;
      for (const auto& instruction : module->functions.back().body)
        if (const auto* mov = dynamic_cast<const MovScalar*>(instruction.get());
            mov && mov->src_mov_source)
          if (const auto* ref =
                  std::get_if<ResolvedRegisterRef>(&mov->src_mov_source->value);
              ref && ref->component)
            component = *ref;
      ASSERT_TRUE(component);
      auto& call = carrier_call(*module);
      const auto original = *call.target_indirect_callee;
      const auto start = original.locs.front().start;
      auto& part = *component->component;
      part.base_range = {start, {start.line, start.column + 1}};
      part.selector->range = {{start.line, start.column + 1},
                              {start.line, start.column + 3}};
      part.range = {start, {start.line, start.column + 3}};
      call.target_indirect_callee = WithLocs<ResolvedIndirectCallee>{
          ResolvedIndirectCallee{*component}, part.range};
      ASSERT_TRUE(valid_register_component(*component));
      if (std::string_view(type) == "u64") {
        auto oversized = call;
        std::get<ResolvedRegisterRef>(oversized.target_indirect_callee->value)
            .component->declaration_width = 4;
        // Direct checks can inspect this fact without a declaration lookup.
        expect_carrier_error(oversized.check(carrier_context(*module)),
                             part.range);
      }
      if (std::string_view(type) == "f32") {
        expect_carrier_error(validateModule(*module), part.range);
        expect_carrier_error(validateModule(*ast, *module), part.range);
      } else {
        EXPECT_TRUE(validateModule(*module));
        EXPECT_TRUE(validateModule(*ast, *module));
      }
      owned = *module;
      *call.target_indirect_callee = original;
      EXPECT_TRUE(validateModule(*module));
      EXPECT_TRUE(validateModule(*ast, *module));
    }
    ResolvedModule copied(*owned), assigned;
    assigned = copied;
    ResolvedFunction function(copied.functions.back()), function_assigned;
    function_assigned = function;
    assigned.functions.back() = function_assigned;
    for (size_t index = 0; index < function.body.size(); ++index)
      if (dynamic_cast<const CallDirect*>(function.body[index].get()))
        assigned.functions.back().body[index] = function.body[index]->clone();
    for (auto* module : {&*owned, &copied, &assigned}) {
      auto& call = carrier_call(*module);
      if (std::string_view(type) == "f32") {
        expect_carrier_error(validateModule(*module),
                             call.target_indirect_callee->locs.front());
        expect_carrier_error(call.check(carrier_context(*module)),
                             call.target_indirect_callee->locs.front());
      } else {
        EXPECT_TRUE(validateModule(*module));
        EXPECT_TRUE(call.check(carrier_context(*module)));
      }
    }
    auto component_source =
        carrier_source("u32", ".version 9.3\n.target sm_90\n.address_size 64\n",
                       ".reg .v2 ." + std::string(type) + " V;\n");
    component_source.replace(component_source.rfind("call pointer"), 12,
                             "call V.x");
    const auto selected_ast = test_helpers::parseModule(component_source);
    ASSERT_MODULE_PARSE_SUCCEEDS(selected_ast);
    const auto selected_module = resolveAndValidateModule(*selected_ast);
    if (std::string_view(type) == "f32") {
      ASSERT_FALSE(selected_module);
      EXPECT_TRUE(
          std::ranges::any_of(selected_module.error(), [](const auto& d) {
            return d.message.find("Indirect function address requires") !=
                   std::string::npos;
          }));
    } else {
      ASSERT_TRUE(selected_module) << selected_module.error().front().message;
      EXPECT_TRUE(validateModule(*selected_module));
      EXPECT_TRUE(validateModule(*selected_ast, *selected_module));
    }
  }
}

/** Actual declaration and every selected use must retain the physical owner cap. */
TEST(IndirectFunctionCarrier, CoherentActualOwnerCapPrecedesAvailableContext) {
  for (const auto header :
       {".version 9.3\n.target sm_90\n.address_size 64\n", ""}) {
    std::optional<ResolvedModule> oversized_owned;
    {
      const std::string extra =
          ".reg .v2 .u64 V;\n.reg .u64 scratch;\nmov.u64 scratch, V.x;\n";
      const auto source = carrier_source("u32", header, extra);
      auto ast = test_helpers::parseModule(source);
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      auto module = resolveModuleOnly(*ast);
      ASSERT_TRUE(module);
      EXPECT_TRUE(
          validateModule(*module, ModuleValidationPolicy::AvailableContext));
      EXPECT_TRUE(validateModule(*ast, *module,
                                 ModuleValidationPolicy::AvailableContext));
      std::optional<ResolvedRegisterRef> selected;
      for (const auto& instruction : module->functions.back().body)
        if (const auto* mov = dynamic_cast<const MovScalar*>(instruction.get());
            mov && mov->src_mov_source)
          if (const auto* ref =
                  std::get_if<ResolvedRegisterRef>(&mov->src_mov_source->value);
              ref && ref->component)
            selected = *ref;
      ASSERT_TRUE(selected && selected->symbol_id && selected->component);
      auto& call = carrier_call(*module);
      const auto start = call.target_indirect_callee->locs.front().start;
      auto& part = *selected->component;
      part.base_range = {start, {start.line, start.column + 1}};
      part.selector->range = {{start.line, start.column + 1},
                              {start.line, start.column + 3}};
      part.range = {start, {start.line, start.column + 3}};
      call.target_indirect_callee = WithLocs<ResolvedIndirectCallee>{
          ResolvedIndirectCallee{*selected}, part.range};
      const auto use_range = part.range;
      const auto owner_id = *selected->symbol_id;
      auto& declaration =
          const_cast<binding::Symbol&>(module->symbols.symbol(owner_id));
      ASSERT_EQ(declaration.type, std::optional<std::string>{".u64"});
      ASSERT_EQ(declaration.vector_width, 2u);
      EXPECT_TRUE(
          validateModule(*module, ModuleValidationPolicy::AvailableContext));
      EXPECT_TRUE(validateModule(*ast, *module,
                                 ModuleValidationPolicy::AvailableContext));
      EXPECT_TRUE(call.check(carrier_context(*module)));
      const auto identity = module->functions.back().source_identity;
      // Keep every current selected use coherent with the real owned declaration.
      const auto set_owner_width = [&](uint8_t width) {
        declaration.vector_width = width;
        size_t updated = 0;
        for (const auto& instruction : module->functions.back().body) {
          if (auto* mov = dynamic_cast<MovScalar*>(instruction.get());
              mov && mov->src_mov_source)
            if (auto* ref = std::get_if<ResolvedRegisterRef>(
                    &mov->src_mov_source->value);
                ref && ref->symbol_id == owner_id && ref->component) {
              ref->component->declaration_width = width;
              ++updated;
            }
          if (auto* current_call = dynamic_cast<CallDirect*>(instruction.get());
              current_call && current_call->target_indirect_callee)
            if (auto* ref = std::get_if<ResolvedRegisterRef>(
                    &current_call->target_indirect_callee->value);
                ref && ref->symbol_id == owner_id && ref->component) {
              ref->component->declaration_width = width;
              ++updated;
            }
        }
        return updated;
      };
      EXPECT_EQ(set_owner_width(4), 2u);
      EXPECT_EQ(declaration.type, std::optional<std::string>{".u64"});
      EXPECT_EQ(declaration.vector_width, 4u);
      const auto& actual =
          std::get<ResolvedRegisterRef>(call.target_indirect_callee->value);
      EXPECT_EQ(actual.symbol_id, owner_id);
      EXPECT_EQ(actual.declared_type, ScalarType::U64);
      EXPECT_EQ(actual.component->declaration_width, 4u);
      EXPECT_TRUE(valid_register_component(actual));
      EXPECT_EQ(module->functions.back().source_identity, identity);
      const auto current =
          validateModule(*module, ModuleValidationPolicy::AvailableContext);
      const auto supplied = validateModule(
          *ast, *module, ModuleValidationPolicy::AvailableContext);
      expect_carrier_error(current, use_range);
      expect_carrier_error(supplied, use_range);
      for (const auto* result : {&current, &supplied})
        EXPECT_FALSE(
            std::ranges::any_of(result->error(), [](const auto& diagnostic) {
              return diagnostic.message.find("metadata disagrees") !=
                         std::string::npos ||
                     diagnostic.message.find("does not match") !=
                         std::string::npos;
            }));
      expect_carrier_error(call.check(carrier_context(*module)), use_range);
      auto missing = carrier_context(*module);
      missing.target = {};
      expect_carrier_error(call.check(missing), use_range);
      oversized_owned = *module;
      EXPECT_EQ(set_owner_width(2), 2u);
      EXPECT_TRUE(
          validateModule(*module, ModuleValidationPolicy::AvailableContext));
      EXPECT_TRUE(validateModule(*ast, *module,
                                 ModuleValidationPolicy::AvailableContext));
      EXPECT_TRUE(call.check(carrier_context(*module)));
    }
    // The coherent oversized declaration and references outlive source and AST.
    ResolvedModule copied(*oversized_owned), assigned;
    assigned = copied;
    ResolvedFunction function(copied.functions.back()), function_assigned;
    function_assigned = function;
    assigned.functions.back() = function_assigned;
    for (size_t index = 0; index < function.body.size(); ++index)
      if (dynamic_cast<const CallDirect*>(function.body[index].get()))
        assigned.functions.back().body[index] = function.body[index]->clone();
    for (auto* module : {&*oversized_owned, &copied, &assigned}) {
      auto& call = carrier_call(*module);
      expect_carrier_error(
          validateModule(*module, ModuleValidationPolicy::AvailableContext),
          call.target_indirect_callee->locs.front());
      expect_carrier_error(call.check(carrier_context(*module)),
                           call.target_indirect_callee->locs.front());
    }
  }
}

/** Known bad types reject before context readiness; unknown standalone facts stay unknown. */
TEST(IndirectFunctionCarrier,
     IntrinsicTypePrecedesAvailabilityWithoutChangingOtherRoles) {
  for (const auto header : {".target sm_90\n", ".version 9.3\n", ""}) {
    auto ast = test_helpers::parseModule(carrier_source("u32", header));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto module = resolveModuleOnly(*ast);
    ASSERT_TRUE(module);
    EXPECT_TRUE(
        validateModule(*module, ModuleValidationPolicy::AvailableContext));
    auto& call = carrier_call(*module);
    const auto original = call.target_indirect_callee->value;
    call.target_indirect_callee->value = carrier_other(*module);
    const auto range = call.target_indirect_callee->locs.front();
    expect_carrier_error(
        validateModule(*module, ModuleValidationPolicy::AvailableContext),
        range);
    expect_carrier_error(
        validateModule(*ast, *module, ModuleValidationPolicy::AvailableContext),
        range);
    const auto strict = validateModule(*module);
    expect_carrier_error(strict, range);
    EXPECT_TRUE(std::ranges::any_of(strict.error(), [](const auto& d) {
      return d.kind == checker::CheckDiagnosticKind::MissingValidationContext;
    }));
    auto context = carrier_context(*module);
    context.target = {};
    expect_carrier_error(call.check(context), range);
    call.target_indirect_callee->value = original;
    auto& unknown =
        std::get<ResolvedRegisterRef>(call.target_indirect_callee->value);
    unknown.symbol_id.reset();
    unknown.declared_type.reset();
    EXPECT_TRUE(call.check(carrier_context(*module)));
    const auto unavailable = call.check(context);
    ASSERT_FALSE(unavailable);
    EXPECT_FALSE(std::ranges::any_of(unavailable.error(), [](const auto& d) {
      return d.message.find("Indirect function address requires") !=
             std::string::npos;
    }));
  }
  auto low = test_helpers::parseModule(
      carrier_source("u32", ".version 2.1\n.target sm_20\n"));
  ASSERT_MODULE_PARSE_SUCCEEDS(low);
  EXPECT_TRUE(resolveAndValidateModule(*low));
}

/** Factory-produced typed calls expose their actual register to check and visit. */
TEST(IndirectFunctionCarrier,
     FactoryAndVisitorRetainBorrowedActualAlternative) {
  auto parsed = test_helpers::parseInstruction("call %r0, (), meta;");
  ASSERT_TRUE(parsed);
  auto instruction = resolveInstruction(*parsed);
  ASSERT_TRUE(instruction) << instruction.error().message;
  auto* call = dynamic_cast<CallDirect*>(instruction->get());
  ASSERT_NE(call, nullptr);
  auto& ref =
      std::get<ResolvedRegisterRef>(call->target_indirect_callee->value);
  ref.declared_type = ScalarType::F32;
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 90},
      .instruction_range = parsed->range};
  expect_carrier_error(call->check(context),
                       call->target_indirect_callee->locs.front());
  /** Observe active alternatives synchronously, without retaining a borrowed pointer. */
  struct Observer final : detail::IReferenceObserver {
    /** Number of actual register alternatives, excluding metadata. */
    size_t registers{};
    /** Count only actual active RegRef alternatives. */
    void indirect_callee(const ResolvedIndirectCallee& value,
                         std::span<const SourceRange>,
                         checker::AddressSymbolResolutionPolicy) override {
      if (std::holds_alternative<ResolvedRegisterRef>(value))
        ++registers;
    }
  } observer;
  auto clone = call->clone();
  clone->visit_references(observer);
  EXPECT_EQ(observer.registers, 1u);
  expect_carrier_error(clone->check(context),
                       call->target_indirect_callee->locs.front());
  const auto bindings =
      call_resolved_descriptor().variants[0].operand_layouts[4].bindings;
  const auto descriptor = std::span{bindings.data(), size_t{1}};
  ref.declared_type = ScalarType::B32;
  const std::array views{checker::OperandView{
      .field_id = bindings[0].target_field_id,
      .actual_shape = check_end::OperandShape::IndirectCallee,
      .register_type = ScalarType::F32,
      .register_ref = &ref,
      .locations = call->target_indirect_callee->locs,
  }};
  EXPECT_TRUE(checker::check_operands(descriptor, {}, views, {}, context));
  ref.declared_type = ScalarType::F32;
  auto lying = views;
  lying[0].register_type = ScalarType::B32;
  expect_carrier_error(
      checker::check_operands(descriptor, {}, lying, {}, context),
      call->target_indirect_callee->locs.front());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
