#include <gtest/gtest.h>
#include <algorithm>
#include <iostream>
#include <limits>
#include <optional>
#include <ptx_frontend/resolved_ir/model/control_flow/call.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/ld.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/mov.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/st.gen.hpp>
#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/atom.gen.hpp>
#include <ptx_frontend/resolved_ir/model/tensor_memory/tcgen05.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include <string>
#include <utility>
#include "../src/ptx_call_parameter_transfers.hpp"
namespace ptx_frontend::resolved_ir {
namespace {
/** Replay real historical input and independent ABI controls without later header directives. */
TEST(LegacyEntryInput, FreshSourceBeforeAndControls) {
  const std::string sources[] = {
      ".version 1.3\n.target sm_13\n.entry k(){.reg .u32 %r; .param .u32 x; "
      "ld.param.u32 %r,[x]; ret;}",
      ".version 1.3\n.target sm_13\n.entry k(){.reg .u32 %r; mov.u32 %r,0; "
      "ret;}",
      ".version 1.4\n.target sm_13\n.entry k(.param .u32 x){.reg .u32 %r; "
      "ld.param.u32 %r,[x]; ret;}",
      ".version 2.0\n.target sm_20\n.extern .func (.param .b32 y) f(.param "
      ".b32 x);\n.entry k(){.reg .b32 %r; .param .b32 a,b; st.param.b32 "
      "[a],%r; call (b),f,(a); ld.param.b32 %r,[b]; ret;}"};
  for (size_t i = 0; i < 4; ++i) {
    SCOPED_TRACE(i);
    PtxSyntaxParser parser(sources[i]);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    const auto owned = resolveModuleOnly(*ast);
    std::cout << "case " << i << " resolveModuleOnly=" << bool(owned) << '\n';
    if (!owned)
      for (const auto& d : owned.error())
        std::cout << int(d.stage()) << " " << d.message << '\n';
    const auto checked = resolveAndValidateModule(*ast);
    std::cout << "case " << i << " strict=" << bool(checked) << '\n';
    if (!checked)
      for (const auto& d : checked.error())
        std::cout << int(d.stage()) << " " << d.message << '\n';
    EXPECT_TRUE(checked);
    if (owned)
      EXPECT_TRUE(validateModule(*owned));
  }
}

/** Produce a historical body input fixture without later header/qualifier spellings. */
std::string legacyInputSource(
    std::string_view body = ".param .u32 x; ld.param.u32 %r,[x];",
    std::string_view version = "1.3", std::string_view target = "sm_13") {
  std::string s;
  if (!version.empty())
    s += ".version " + std::string(version) + "\n";
  if (!target.empty())
    s += ".target " + std::string(target) + "\n";
  return s + ".entry k(){.reg .u32 %r; " + std::string(body) + " ret;}";
}
/** Return owning output only after parser, source, and syntax have been destroyed. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> legacyInputOwned(
    std::string s, bool strict = true) {
  PtxSyntaxParser p(s);
  auto a = p.parseModule();
  EXPECT_TRUE(a && a.diagnostics.empty());
  if (!a || !a.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return strict ? resolveAndValidateModule(*a) : resolveModuleOnly(*a);
}
/** Join failure messages for a precise causal assertion. */
std::string legacyInputMessages(const checker::CheckResult& result) {
  std::string s;
  if (!result)
    for (const auto& d : result.error())
      s += d.message + "\n";
  return s;
}
/** Verify a located cause in both public validators, independent of complete context. */
void legacyInputRejectBoth(const std::string& source, const ResolvedModule& m,
                           std::string_view cause) {
  PtxSyntaxParser p(source);
  auto a = p.parseModule();
  ASSERT_TRUE(a && a.diagnostics.empty());
  for (auto policy : {ModuleValidationPolicy::AvailableContext,
                      ModuleValidationPolicy::RequireCompleteContext}) {
    const auto current = validateModule(m, policy);
    const auto supplied = validateModule(*a, m, policy);
    for (const auto* r : {&current, &supplied}) {
      EXPECT_FALSE(*r);
      EXPECT_NE(legacyInputMessages(*r).find(cause), std::string::npos)
          << legacyInputMessages(*r);
    }
  }
}
/** Remove current context coherently without changing the current typed role or source payload. */
void legacyInputContext(ResolvedModule& m,
                        std::optional<checker::PtxVersion> version,
                        std::optional<std::string> target) {
  for (auto& r : m.header.regions) {
    r.version = version;
    r.version_provenance = version ? SourceConfigurationProvenance::Explicit
                                   : SourceConfigurationProvenance::Missing;
    r.target_provenance = target ? SourceConfigurationProvenance::Explicit
                                 : SourceConfigurationProvenance::Missing;
    r.target_options.clear();
    if (target)
      r.target_options.push_back(*target);
  }
  for (auto& f : m.functions) {
    f.source_version = version;
    f.source_target = target;
  }
}
/** Borrow the actual current LD input symbol for a bounded payload mutation. */
ResolvedSymbolRef& legacyInputRef(ResolvedModule& m) {
  auto* ld =
      dynamic_cast<LdExplicitScalar*>(m.functions.back().body.front().get());
  EXPECT_NE(ld, nullptr);
  return std::get<ResolvedSymbolRef>(ld->address.value.base);
}
/** Validate a real legacy handoff, separate from the syntactic header signature. */
TEST(LegacyEntryInput, HistoricalWindowsAndNarrowMissingVersionTyping) {
  for (const auto version : {"1.0", "1.1", "1.2", "1.3"}) {
    auto m = legacyInputOwned(legacyInputSource(
        ".param .u32 x;ld.param.u32 %r,[x];", version,
        std::string_view(version) < "1.2" ? "sm_10" : "sm_13"));
    ASSERT_TRUE(m) << (m.error().empty() ? "" : m.error().front().message);
    const auto& f = m->functions.back();
    ASSERT_EQ(f.parameter_declarations.size(), 1);
    EXPECT_EQ(f.parameter_declarations[0].role,
              ParameterDeclarationRole::LegacyEntryInput);
    EXPECT_TRUE(f.contract.signature.parameters.empty());
    EXPECT_TRUE(f.contract.signature.return_parameters.empty());
    EXPECT_EQ(legacyInputRef(*m).parameter_role,
              ParameterDeclarationRole::LegacyEntryInput);
    EXPECT_EQ(legacyInputRef(*m).declaration_kind,
              binding::SymbolKind::CallParameter);
    EXPECT_TRUE(validateModule(*m));
  }
  for (const auto v : {"1.4", "1.5"})
    EXPECT_FALSE(legacyInputOwned(legacyInputSource(".param .u32 x;", v)));
  EXPECT_FALSE(legacyInputOwned(
      ".version 1.3\n.target sm_13\n.entry k(.param .u32 x){}"));
  EXPECT_FALSE(legacyInputOwned(
      ".version 1.3\n.target sm_13\n.func f(){.param .u32 x;ret;}"));
  EXPECT_FALSE(
      legacyInputOwned(legacyInputSource(".param .u32 x;", "2.0", "sm_13")));
  for (const auto t : {"", "sm_13", "sm_20"}) {
    auto m =
        legacyInputOwned(legacyInputSource(".param .u32 x;", "", t), false);
    ASSERT_FALSE(m);
    EXPECT_TRUE(std::ranges::any_of(m.error(), [](const auto& d) {
      return d.checker_kind ==
                 checker::CheckDiagnosticKind::MissingValidationContext &&
             d.message.find("select its semantic role") != std::string::npos;
    }));
  }
  EXPECT_TRUE(legacyInputOwned(
      legacyInputSource(".param .u32 x;ld.param.u32 %r,[x];", "1.3", ""),
      false));
  EXPECT_TRUE(legacyInputOwned(".entry k(){ret;}", false));
  EXPECT_TRUE(legacyInputOwned(".func f(){.param .u32 x;ret;}", false));
  EXPECT_TRUE(legacyInputOwned(".entry k(.param .u32 x){ret;}", false));
  EXPECT_TRUE(
      legacyInputOwned(legacyInputSource(".param .u32 x;", "2.0", ""), false));
  // The established effective module version still permits out-of-order fragments.
  EXPECT_TRUE(
      legacyInputOwned(".entry k(){.param .u32 x;}\n.version 1.3", false));
  for (const auto body : {".param .pred x;", ".param .b8 x[];",
                          ".param .b8 x[18446744073709551615][2];"}) {
    const auto source = legacyInputSource(body, "", "sm_13");
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    const auto bound = binding::bindSymbols(*ast);
    EXPECT_FALSE(
        declaration_semantics::checkDeclarations(*ast, bound.table).empty());
    const auto result = resolveModuleOnly(*ast);
    ASSERT_FALSE(result);
    EXPECT_TRUE(std::ranges::none_of(result.error(), [](const auto& error) {
      return error.checker_kind ==
             checker::CheckDiagnosticKind::MissingValidationContext;
    }));
  }
}
/** Body input scopes, compact identities, axes and vectors are not header formals. */
TEST(LegacyEntryInput, PreservesLexicalShapesAndRejectsInvalidDeclarations) {
  for (const auto decl :
       {".param .u32 x,y;", ".param .align 8 .u32 x<3>;ld.param.u32 %r,[x2];",
        ".param .align 8 .b8 x[24];", ".param .u32 x[2][3];",
        ".param .v2 .u32 x;", "mov.u32 %r,0;.param .u32 x;ld.param.u32 %r,[x];",
        ".param .b8 x[300];"}) {
    auto m = legacyInputOwned(legacyInputSource(decl));
    ASSERT_TRUE(m) << (m.error().empty() ? "" : m.error().front().message);
    EXPECT_TRUE(validateModule(*m));
    for (const auto& r : m->functions.back().parameter_declarations) {
      EXPECT_EQ(r.role, ParameterDeclarationRole::LegacyEntryInput);
      EXPECT_TRUE(r.byte_extent);
    }
  }
  auto m = legacyInputOwned(
      legacyInputSource(".param .u32 x;{.param .u32 x;ld.param.u32 "
                        "%r,[x];}{.param .u32 y;ld.param.u32 %r,[y];}"));
  ASSERT_TRUE(m);
  const auto& r = m->functions.back().parameter_declarations;
  ASSERT_EQ(r.size(), 3);
  EXPECT_NE(r[0].symbol_id, r[1].symbol_id);
  EXPECT_NE(r[0].scope_id, r[1].scope_id);
  EXPECT_NE(r[1].scope_id, r[2].scope_id);
  EXPECT_EQ(m->symbols.symbol(r[0].symbol_id).name, "x");
  EXPECT_EQ(m->symbols.symbol(r[1].symbol_id).name, "x");
  EXPECT_EQ(m->symbols.symbol(r[2].symbol_id).name, "y");
  for (const auto body :
       {".param .u32 x<3>[2];", ".param .u32 x=1;", ".param .b8 x[];",
        ".param .pred x;", ".param .u64 .ptr x;", ".param .texref x;",
        ".param .b8 x[18446744073709551615][2];"}) {
    PtxSyntaxParser p(legacyInputSource(body));
    auto a = p.parseModule();
    if (a && a.diagnostics.empty())
      EXPECT_FALSE(resolveModuleOnly(*a));
    else
      EXPECT_FALSE(a && a.diagnostics.empty());
  }
}
/** Intrinsic cache and ownership failures survive absent version/target in both validators. */
TEST(LegacyEntryInput, JoinsCurrentRoleCachesAndRejectsErasureOrForgery) {
  const auto source = legacyInputSource();
  auto original = legacyInputOwned(source);
  ASSERT_TRUE(original);
  for (int variant = 0; variant < 9; ++variant) {
    auto m = *original;
    auto& ref = legacyInputRef(m);
    auto& f = m.functions.back();
    if (variant == 0)
      ref.parameter_role.reset();
    if (variant == 1)
      ref.parameter_role = ParameterDeclarationRole::BodyLocal;
    if (variant == 2)
      ref.parameter_role = static_cast<ParameterDeclarationRole>(255);
    if (variant == 3)
      ref.declaration_kind = binding::SymbolKind::InputParameter;
    if (variant == 4)
      f.parameter_declarations.clear();
    if (variant == 5)
      f.parameter_declarations[0].scope_id = m.symbols.moduleScope();
    if (variant == 6) {
      f.parameter_declarations[0].role = ParameterDeclarationRole::BodyLocal;
      ref.parameter_role = ParameterDeclarationRole::BodyLocal;
    }
    if (variant == 7) {
      f.is_entry = false;
      f.contract.signature.is_entry = false;
    }
    if (variant == 8) {
      ref.symbol_id = binding::SymbolId{std::numeric_limits<uint32_t>::max()};
      f.parameter_declarations[0].symbol_id = *ref.symbol_id;
    }
    PtxSyntaxParser p(source);
    auto a = p.parseModule();
    ASSERT_TRUE(a);
    EXPECT_FALSE(validateModule(m));
    EXPECT_FALSE(validateModule(*a, m));
    if (variant < 6) {
      legacyInputContext(m, std::nullopt, std::nullopt);
      const auto missing =
          legacyInputSource(".param .u32 x;ld.param.u32 %r,[x];", "", "");
      PtxSyntaxParser q(missing);
      auto b = q.parseModule();
      ASSERT_TRUE(b);
      EXPECT_FALSE(validateModule(m, ModuleValidationPolicy::AvailableContext));
      EXPECT_FALSE(
          validateModule(*b, m, ModuleValidationPolicy::AvailableContext));
    }
  }
  auto m = *original;
  legacyInputContext(m, std::nullopt, std::nullopt);
  EXPECT_TRUE(validateModule(m, ModuleValidationPolicy::AvailableContext));
  const auto strict =
      validateModule(m, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(strict);
  EXPECT_TRUE(std::ranges::any_of(strict.error(), [](const auto& error) {
    return error.kind == checker::CheckDiagnosticKind::MissingValidationContext;
  }));
  const auto missing =
      legacyInputSource(".param .u32 x;ld.param.u32 %r,[x];", "", "");
  PtxSyntaxParser p(missing);
  auto a = p.parseModule();
  ASSERT_TRUE(a);
  EXPECT_TRUE(validateModule(*a, m, ModuleValidationPolicy::AvailableContext));
}
/** A context retarget checks the current role and does not reconstruct semantic history. */
TEST(LegacyEntryInput, ChecksCurrentAndSuppliedRoleWindowsIndependently) {
  auto legacy = legacyInputOwned(legacyInputSource(".param .u32 x;"));
  ASSERT_TRUE(legacy);
  auto modern =
      legacyInputOwned(legacyInputSource(".param .u32 x;", "2.0", "sm_20"));
  ASSERT_TRUE(modern);
  EXPECT_EQ(modern->functions.back().parameter_declarations[0].role,
            ParameterDeclarationRole::BodyLocal);
  legacyInputContext(*legacy, checker::PtxVersion{2, 0}, std::string("sm_20"));
  legacyInputRejectBoth(legacyInputSource(".param .u32 x;", "2.0", "sm_20"),
                        *legacy, "Legacy entry input requires");
  legacyInputContext(*modern, checker::PtxVersion{1, 3}, std::string("sm_13"));
  legacyInputRejectBoth(legacyInputSource(".param .u32 x;"), *modern,
                        "Body-local call .param");
  auto current = legacyInputOwned(legacyInputSource());
  ASSERT_TRUE(current);
  PtxSyntaxParser p(
      legacyInputSource(".param .u32 x;ld.param.u32 %r,[x];", "2.0", "sm_20"));
  auto a = p.parseModule();
  ASSERT_TRUE(a);
  auto supplied = validateModule(*a, *current);
  EXPECT_FALSE(supplied);
  EXPECT_NE(legacyInputMessages(supplied).find("Legacy entry input requires"),
            std::string::npos);
  PtxSyntaxParser changed(
      legacyInputSource(".param .u32 x;ld.param.u32 %r,[x];mov.u32 %r,0;"));
  auto b = changed.parseModule();
  ASSERT_TRUE(b);
  const auto mismatch = validateModule(*b, *current);
  ASSERT_FALSE(mismatch);
  EXPECT_TRUE(std::ranges::any_of(mismatch.error(), [](const auto& d) {
    return d.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
  }));
}
/** The known global ceiling is a checked logical total, without inferred launch packing. */
TEST(LegacyEntryInput, EnforcesLogicalAggregateAcrossGroupsAndNestedScopes) {
  const std::pair<const char*, bool> cases[] = {
      {".param .align 1 .b8 x[32764];", true},
      {".param .align 1 .b8 x[32765];", false},
      {".param .b32 x<8191>;", true},
      {".param .b32 x<8192>;", false},
      {".param .align 1 .b8 x[4],y[18446744073709551615];", false},
      {"{.param .align 1 .b8 x[16000];}{.param .align 1 .b8 y[16764];}", true},
      {"{.param .align 1 .b8 x[16000];}{.param .align 1 .b8 y[16765];}",
       false}};
  for (const auto& [body, ok] : cases) {
    auto m = legacyInputOwned(legacyInputSource(body));
    ASSERT_EQ(bool(m), ok) << (m || m.error().empty()
                                   ? ""
                                   : m.error().front().message);
    if (m)
      EXPECT_TRUE(validateModule(*m));
  }
  const auto source = legacyInputSource(".param .align 1 .b8 x[4];");
  auto m = legacyInputOwned(source);
  ASSERT_TRUE(m);
  auto& record = m->functions.back().parameter_declarations[0];
  record.array_extents = {32765};
  record.byte_extent = 32765;
  legacyInputRejectBoth(source, *m, "32764-byte logical aggregate");
  legacyInputContext(*m, std::nullopt, std::nullopt);
  legacyInputRejectBoth(legacyInputSource(".param .align 1 .b8 x[4];", "", ""),
                        *m, "32764-byte logical aggregate");
  // Isolate checked multiplication without pretending that a forged shape passed the full joins.
  auto group = legacyInputOwned(legacyInputSource(".param .b32 x<3>;"));
  ASSERT_TRUE(group);
  group->functions.back().parameter_declarations[0].byte_extent =
      std::numeric_limits<uint64_t>::max();
  EXPECT_FALSE(detail::check_legacy_entry_input_sizes(*group).empty());
  auto sum =
      legacyInputOwned(legacyInputSource(".param .align 1 .b8 x[4],y[1];"));
  ASSERT_TRUE(sum);
  auto& sum_record = sum->functions.back().parameter_declarations[1];
  sum_record.array_extents = {std::numeric_limits<uint64_t>::max()};
  sum_record.byte_extent = std::numeric_limits<uint64_t>::max();
  EXPECT_FALSE(detail::check_legacy_entry_input_sizes(*sum).empty());
  legacyInputRejectBoth(legacyInputSource(".param .align 1 .b8 x[4],y[1];"),
                        *sum, "32764-byte logical aggregate");
}
/** Copies, moves, assignments and cloned instructions own all roles after AST death. */
TEST(LegacyEntryInput, PreservesSourceDeadValueOperationsAndAddressDirection) {
  auto m = legacyInputOwned(
      legacyInputSource(".param .u32 x;ld.param.u32 %r,[x];mov.u32 %r,x;"));
  ASSERT_TRUE(m) << (m.error().empty() ? "" : m.error().front().message);
  auto copy = *m;
  ResolvedModule assigned;
  assigned = copy;
  ResolvedModule moved = std::move(assigned);
  moved.functions.back() = m->functions.back();
  for (auto& f : moved.functions)
    for (auto& i : f.body)
      i = i->clone();
  for (auto* value : {&*m, &copy, &moved}) {
    EXPECT_TRUE(validateModule(*value));
    auto& ref = legacyInputRef(*value);
    EXPECT_EQ(parameter_direction_from_symbol(&ref), ParameterDirection::Input);
    EXPECT_EQ(ref.address_state_space, base::DeclarationStateSpace::Parameter);
  }
  legacyInputRef(copy).parameter_role.reset();
  EXPECT_FALSE(validateModule(copy));
  EXPECT_TRUE(validateModule(*m));
  EXPECT_TRUE(validateModule(moved));
}
/** Read-only and entry-frame checks use actual records even with corrupt or missing caches. */
TEST(LegacyEntryInput, EnforcesReadOnlyFramesAndFormalCallSeparation) {
  auto bad =
      legacyInputOwned(legacyInputSource(".param .u32 x;st.param.u32 [x],%r;"));
  EXPECT_FALSE(bad);
  const std::string direct =
      ".version 1.3\n.target sm_13\n.extern .func f(.reg .u32 a);.entry "
      "k(){.reg .u32 %r;.param .u32 x;call f,(x);ret;}";
  EXPECT_FALSE(legacyInputOwned(direct));
  const std::string reg =
      ".version 1.3\n.target sm_13\n.extern .func f(.reg .u32 a);.entry "
      "k(){.reg .u32 %r;.param .u32 x;ld.param.u32 %r,[x];call f,(%r);ret;}";
  auto good = legacyInputOwned(reg);
  ASSERT_TRUE(good) << (good.error().empty() ? ""
                                             : good.error().front().message);
  EXPECT_TRUE(validateModule(*good));
  auto legacy = legacyInputOwned(legacyInputSource());
  ASSERT_TRUE(legacy);
  legacyInputRef(*legacy).parameter_qualifier =
      ParameterAddressQualifier::Function;
  auto* ld =
      dynamic_cast<LdExplicitScalar*>(legacy->functions.back().body[0].get());
  ASSERT_NE(ld, nullptr);
  ld->address.value.parameter_qualifier = ParameterAddressQualifier::Function;
  legacyInputRejectBoth(legacyInputSource(), *legacy, "entry parameter frame");
  // Preserve a real, valid modern staging source while current records make its store read-only.
  const std::string modern =
      ".version 2.0\n.target sm_20\n.extern .func f(.param .b32 a);.entry "
      "k(){.reg .b32 %r;.param .b32 x;st.param.b32 [x],%r;call f,(x);ret;}";
  auto edited = legacyInputOwned(modern);
  ASSERT_TRUE(edited);
  edited->functions.back().parameter_declarations[0].role =
      ParameterDeclarationRole::LegacyEntryInput;
  auto* st =
      dynamic_cast<StExplicitScalar*>(edited->functions.back().body[0].get());
  ASSERT_NE(st, nullptr);
  std::get<ResolvedSymbolRef>(st->address.value.base).parameter_role =
      ParameterDeclarationRole::LegacyEntryInput;
  legacyInputContext(*edited, std::nullopt, std::nullopt);
  legacyInputRejectBoth(modern, *edited, "read-only");
}
/** Modern entry/device body slots preserve staging and header-only ABI distinctions. */
TEST(LegacyEntryInput, KeepsModernStagingAndTypedUnknownRoleIntrinsics) {
  for (const auto owner : {".entry", ".func"}) {
    const std::string source =
        ".version 2.0\n.target sm_20\n.extern .func (.param .b32 result) "
        "f(.param .b32 a);" +
        std::string(owner) +
        " k(){.reg .b32 %r;.param .b32 a,b;st.param.b32 [a],%r;call "
        "(b),f,(a);ld.param.b32 %r,[b];ret;}";
    auto m = legacyInputOwned(source);
    ASSERT_TRUE(m) << (m.error().empty() ? "" : m.error().front().message);
    for (const auto& r : m->functions.back().parameter_declarations)
      EXPECT_EQ(r.role, ParameterDeclarationRole::BodyLocal);
    EXPECT_TRUE(validateModule(*m));
    legacyInputContext(*m, std::nullopt, std::nullopt);
    EXPECT_TRUE(validateModule(*m, ModuleValidationPolicy::AvailableContext));
    const auto missing_context = source.substr(source.find(".extern"));
    PtxSyntaxParser parser(missing_context);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    EXPECT_TRUE(
        validateModule(*ast, *m, ModuleValidationPolicy::AvailableContext));
  }
  EXPECT_FALSE(legacyInputOwned(
      legacyInputSource(".param .u32 x;ld.param.u32 %r,[x];", "2.0", "sm_20")));
  EXPECT_FALSE(legacyInputOwned(
      legacyInputSource(".param .u32 x;st.param.u32 [x],%r;", "2.0", "sm_20")));
  const std::string source =
      ".version 1.3\n.target sm_13\n.extern .func (.reg .u32 y) f(.reg .u32 "
      "a);.entry k(){.reg .u32 %r;.param .u32 x;ld.param.u32 %r,[x];call "
      "(%r),f,(%r);ret;}";
  auto original = legacyInputOwned(source);
  ASSERT_TRUE(original);
  for (const bool returning : {false, true}) {
    auto m = *original;
    auto* call = dynamic_cast<CallDirect*>(m.functions.back().body[1].get());
    ASSERT_NE(call, nullptr);
    auto& ref = returning ? call->return_value->value
                          : std::get<ResolvedCallParameterRef>(
                                call->arguments->value.values[0].value);
    ref.symbol_id = m.functions.back().parameter_declarations[0].symbol_id;
    ref.spelling = "x";
    ref.state_space = base::DeclarationStateSpace::Parameter;
    legacyInputRejectBoth(
        source, m, "cannot be used directly as a call argument or return");
  }
}
/** All three projections share semantic roles; standalone known contradictions do not defer. */
TEST(LegacyEntryInput, ProjectsRolesAndChecksKnownStandaloneCacheFacts) {
  WithLocs<ResolvedTensorOperand> tensor;
  ResolvedSymbolRef symbol;
  symbol.declaration_kind = binding::SymbolKind::CallParameter;
  const std::pair<ParameterDeclarationRole, ParameterDirection> cases[] = {
      {ParameterDeclarationRole::EntryInput, ParameterDirection::Input},
      {ParameterDeclarationRole::DeviceInput, ParameterDirection::Input},
      {ParameterDeclarationRole::DeviceReturn, ParameterDirection::Return},
      {ParameterDeclarationRole::BodyLocal, ParameterDirection::CallArgument},
      {ParameterDeclarationRole::LegacyEntryInput, ParameterDirection::Input}};
  for (const auto& [role, direction] : cases) {
    symbol.parameter_role = role;
    tensor.value.tensor_map.address.base = symbol;
    EXPECT_EQ(
        checker::project_tensor_operand("tensor", tensor).parameter_direction,
        direction);
  }
  auto m = legacyInputOwned(legacyInputSource());
  ASSERT_TRUE(m);
  auto* ld = dynamic_cast<LdExplicitScalar*>(m->functions.back().body[0].get());
  ASSERT_NE(ld, nullptr);
  checker::Context context{
      .target = {.ptx_version = {1, 3}, .sm_version = 13},
      .instruction_range = m->functions.back().instruction_ranges[0]};
  auto device = *ld;
  device.address.value.enclosing_function_kind = EnclosingFunctionKind::Device;
  std::get<ResolvedSymbolRef>(device.address.value.base)
      .enclosing_function_kind = EnclosingFunctionKind::Device;
  EXPECT_FALSE(device.check(context));
  auto invalid = *ld;
  std::get<ResolvedSymbolRef>(invalid.address.value.base).parameter_role =
      static_cast<ParameterDeclarationRole>(255);
  EXPECT_FALSE(invalid.check(context));
  auto unknown = *ld;
  unknown.address.value.enclosing_function_kind =
      EnclosingFunctionKind::Unknown;
  std::get<ResolvedSymbolRef>(unknown.address.value.base)
      .enclosing_function_kind = EnclosingFunctionKind::Unknown;
  EXPECT_TRUE(unknown.check(context));
}
/** Full-module identity is required before a standalone absent-cache fallback. */
TEST(LegacyEntryInput, RequiresCurrentIdentityBeforeAbsentCacheFallback) {
  const auto source = legacyInputSource();
  auto original = legacyInputOwned(source);
  ASSERT_TRUE(original);
  const auto missing_source =
      legacyInputSource(".param .u32 x;ld.param.u32 %r,[x];", "", "");
  PtxSyntaxParser parser(missing_source);
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  EXPECT_TRUE(validateModule(*ast, *original,
                             ModuleValidationPolicy::AvailableContext));
  for (const bool current_missing : {false, true}) {
    auto healthy = *original;
    if (current_missing)
      legacyInputContext(healthy, std::nullopt, std::nullopt);
    EXPECT_TRUE(
        validateModule(healthy, ModuleValidationPolicy::AvailableContext))
        << legacyInputMessages(validateModule(
               healthy, ModuleValidationPolicy::AvailableContext));
    for (const bool invalid_id : {false, true}) {
      auto edited = healthy;
      auto& ref = legacyInputRef(edited);
      ref.parameter_role.reset();
      if (invalid_id)
        ref.symbol_id = binding::SymbolId{std::numeric_limits<uint32_t>::max()};
      else
        ref.symbol_id.reset();
      const auto current =
          validateModule(edited, ModuleValidationPolicy::AvailableContext);
      const auto supplied = validateModule(
          *ast, edited, ModuleValidationPolicy::AvailableContext);
      for (const auto* result : {&current, &supplied}) {
        ASSERT_FALSE(*result);
        EXPECT_NE(legacyInputMessages(*result).find("binding identity"),
                  std::string::npos)
            << legacyInputMessages(*result);
      }
    }
  }
}

/** A selected store space cannot erase the actual legacy object's readonly fact. */
TEST(LegacyEntryInput, ReadOnlySurvivesMutableSpaceSelection) {
  const std::string payload =
      ".global .u32 g;\n.entry k(){.reg .u32 %r;.param .u32 x;st.global.u32 "
      "[g],%r;ret;}";
  auto modern = legacyInputOwned(".version 2.0\n.target sm_20\n" + payload);
  ASSERT_TRUE(modern) << (modern.error().empty()
                              ? ""
                              : modern.error().front().message);
  PtxSyntaxParser parser(payload);
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  legacyInputContext(*modern, std::nullopt, std::nullopt);
  EXPECT_TRUE(
      validateModule(*modern, ModuleValidationPolicy::AvailableContext));
  EXPECT_TRUE(
      validateModule(*ast, *modern, ModuleValidationPolicy::AvailableContext));
  for (const auto space :
       {MemoryStateSpace::Parameter, MemoryStateSpace::Global,
        MemoryStateSpace::Generic}) {
    auto edited = *modern;
    auto& record = edited.functions.back().parameter_declarations.front();
    record.role = ParameterDeclarationRole::LegacyEntryInput;
    const auto& symbol = edited.symbols.symbol(record.symbol_id);
    auto* store = dynamic_cast<StExplicitScalar*>(
        edited.functions.back().body.front().get());
    ASSERT_NE(store, nullptr);
    store->state_space.value = space;
    auto& ref = std::get<ResolvedSymbolRef>(store->address.value.base);
    ref.symbol_id = symbol.id;
    ref.spelling = symbol.name;
    ref.declaration_kind = symbol.kind;
    ref.declaration_state_space = symbol.state_space;
    ref.address_state_space = symbol.state_space;
    ref.address_alignment = symbol.address_alignment;
    ref.parameter_role = record.role;
    const auto current =
        validateModule(edited, ModuleValidationPolicy::AvailableContext);
    const auto supplied =
        validateModule(*ast, edited, ModuleValidationPolicy::AvailableContext);
    for (const auto* result : {&current, &supplied}) {
      ASSERT_FALSE(*result);
      ASSERT_EQ(result->error().size(), 1u) << legacyInputMessages(*result);
      EXPECT_EQ(result->error().front().message,
                "Legacy entry input parameters are read-only.");
    }
  }
  // Fresh source now rejects through the same actual-record guard before handoff;
  // it is not falsely counted as an owned negative with a missing module.
  const auto fresh = legacyInputOwned(
      legacyInputSource(".param .u32 x;st.global.u32 [x],%r;", "1.3", ""),
      false);
  ASSERT_FALSE(fresh);
  EXPECT_TRUE(std::ranges::any_of(fresh.error(), [](const auto& error) {
    return error.message == "Legacy entry input parameters are read-only.";
  }));
}
/** Copy the current record's complete scalar address facts without changing its structural kind. */
void legacyInputRedirect(ResolvedSymbolRef& ref, const ResolvedModule& module,
                         const ResolvedParameterDeclaration& record) {
  const auto& symbol = module.symbols.symbol(record.symbol_id);
  ref.symbol_id = symbol.id;
  ref.spelling = symbol.name;
  ref.parameterized_index.reset();
  ref.declaration_kind = symbol.kind;
  ref.declaration_state_space = symbol.state_space;
  ref.address_state_space = symbol.state_space;
  ref.declared_type = record.scalar_type;
  ref.address_alignment = symbol.address_alignment;
  ref.address_availability.reset();
  ref.declaration_is_unified = false;
  ref.enclosing_function_kind = EnclosingFunctionKind::Entry;
  ref.parameter_qualifier = ParameterAddressQualifier::Default;
  ref.parameter_role = record.role;
}
/** Current domain proof covers a real legal historical global atomic producer and its input redirect. */
TEST(LegacyEntryInput, SharedDomainRejectsAtomicInputAfterContextRemoval) {
  const std::string payload =
      ".global .u32 g;\n.entry k(){.reg .u32 %r,%old;.param .u32 "
      "x;atom.global.add.u32 %old,[g],%r;ret;}";
  // The canonical ordinary global-u32 atomic form starts at PTX1.1/SM11;
  // the source writes g, never a parameter, and uses no later qualifiers.
  auto module = legacyInputOwned(".version 1.3\n.target sm_13\n" + payload);
  ASSERT_TRUE(module) << (module.error().empty()
                              ? ""
                              : module.error().front().message);
  auto& record = module->functions.back().parameter_declarations.front();
  ASSERT_EQ(record.role, ParameterDeclarationRole::LegacyEntryInput);
  PtxSyntaxParser parser(payload);
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  legacyInputContext(*module, std::nullopt, std::nullopt);
  EXPECT_TRUE(
      validateModule(*module, ModuleValidationPolicy::AvailableContext));
  EXPECT_TRUE(
      validateModule(*ast, *module, ModuleValidationPolicy::AvailableContext));
  auto* atom = dynamic_cast<AtomGlobalAddU32*>(
      module->functions.back().body.front().get());
  ASSERT_NE(atom, nullptr);
  legacyInputRedirect(std::get<ResolvedSymbolRef>(atom->address.value.base),
                      *module, record);
  for (const auto& result :
       {validateModule(*module, ModuleValidationPolicy::AvailableContext),
        validateModule(*ast, *module,
                       ModuleValidationPolicy::AvailableContext)}) {
    ASSERT_FALSE(result);
    EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& error) {
      return error.kind ==
             checker::CheckDiagnosticKind::AddressStateSpaceMismatch;
    })) << legacyInputMessages(result);
  }
}
/** A known writable operand rejects actual legacy input without inventing an empty-domain ban. */
TEST(LegacyEntryInput, SharedWritableRequirementRejectsLegacyAllocDestination) {
  const std::string payload =
      ".entry k(){.shared .align 4 .b32 slot;.reg .b32 %t;.reg .u32 %r;.param "
      ".u32 x;tcgen05.alloc.cta_group::1.sync.aligned.b32 "
      "[slot],32;tcgen05.dealloc.cta_group::1.sync.aligned.b32 %t,32;ret;}";
  // Allocation has no historical1.x source permission: the real producer is
  // modern, then current records/addresses are coherently edited with context unknown.
  auto module = legacyInputOwned(".version 9.3\n.target sm_100a\n" + payload);
  ASSERT_TRUE(module) << (module.error().empty()
                              ? ""
                              : module.error().front().message);
  PtxSyntaxParser parser(payload);
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  legacyInputContext(*module, std::nullopt, std::nullopt);
  EXPECT_TRUE(
      validateModule(*module, ModuleValidationPolicy::AvailableContext));
  EXPECT_TRUE(
      validateModule(*ast, *module, ModuleValidationPolicy::AvailableContext));
  auto& record = module->functions.back().parameter_declarations.front();
  record.role = ParameterDeclarationRole::LegacyEntryInput;
  auto* alloc = dynamic_cast<Tcgen05AllocGeneric*>(
      module->functions.back().body.front().get());
  ASSERT_NE(alloc, nullptr);
  legacyInputRedirect(std::get<ResolvedSymbolRef>(alloc->dst.value.base),
                      *module, record);
  for (const auto& result :
       {validateModule(*module, ModuleValidationPolicy::AvailableContext),
        validateModule(*ast, *module,
                       ModuleValidationPolicy::AvailableContext)}) {
    ASSERT_FALSE(result);
    ASSERT_EQ(result.error().size(), 1u) << legacyInputMessages(result);
    EXPECT_EQ(result.error().front().kind,
              checker::CheckDiagnosticKind::ModuleSourceMismatch);
    EXPECT_EQ(result.error().front().range,
              module->functions.back().instruction_ranges.front());
    EXPECT_EQ(result.error().front().message,
              "Legacy entry input parameters are read-only.");
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
