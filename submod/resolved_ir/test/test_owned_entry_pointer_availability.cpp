#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {
/** Omit explicit address size so the pointer introduction is the only feature floor. */
std::string pointerSource(std::string_view version = "2.2",
                          std::string_view type = "u32", bool pointer = true) {
  return ".version " + std::string(version) +
         "\n.target sm_20\n.entry kernel(.param ." + std::string(type) +
         (pointer ? " .ptr .global" : "") + " p) { ret; }\n";
}

/** Resolve while source/parser/AST are alive, then return only the owning module. */
std::optional<ResolvedModule> pointerOwned(std::string source) {
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  auto module = resolveAndValidateModule(*ast);
  EXPECT_TRUE(module);
  if (!module)
    return std::nullopt;
  return std::move(*module);
}

/** Change all retained version facts coherently without touching parameter metadata. */
void retargetPointerVersion(ResolvedModule& module,
                            checker::PtxVersion version) {
  for (auto& region : module.header.regions)
    region.version = version;
  for (auto& function : module.functions)
    function.source_version = version;
}

/** Require this feature-window predicate at its existing function-range fallback. */
void expectPointerAvailability(const checker::CheckResult& result,
                               SourceRange range) {
  ASSERT_FALSE(result);
  EXPECT_TRUE(std::ranges::any_of(
      result.error(), [&](const checker::CheckDiagnostic& error) {
        return error.kind ==
                   checker::CheckDiagnosticKind::UnsupportedAvailability &&
               error.range == range &&
               error.message == ".ptr entry parameter requires PTX ISA >= 2.2.";
      }));
}

/** Missing context is independent of an unknown feature version. */
void expectPointerMissingContext(const checker::CheckResult& result) {
  ASSERT_FALSE(result);
  EXPECT_TRUE(std::ranges::any_of(
      result.error(), [](const checker::CheckDiagnostic& error) {
        return error.kind ==
               checker::CheckDiagnosticKind::MissingValidationContext;
      }));
  EXPECT_FALSE(std::ranges::any_of(
      result.error(), [](const checker::CheckDiagnostic& error) {
        return error.message == ".ptr entry parameter requires PTX ISA >= 2.2.";
      }));
}

/** Legal source controls isolate the pointer feature from later header directives. */
TEST(OwnedEntryPointerAvailability, MatchedSourceControls) {
  auto module = pointerOwned(pointerSource());
  ASSERT_TRUE(module);
  EXPECT_TRUE(validateModule(*module));
  EXPECT_TRUE(pointerOwned(pointerSource("2.1", "u32", false)));
  PtxSyntaxParser parser(pointerSource("2.1"));
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  const auto old = resolveAndValidateModule(*ast);
  ASSERT_FALSE(old);
  EXPECT_TRUE(std::ranges::any_of(old.error(), [](const auto& error) {
    return error.message.find(".ptr entry parameter") != std::string::npos &&
           error.message.find("2.2") != std::string::npos;
  }));
}

/** Actual source-dead coherent retargets must retain the pointer introduction floor. */
TEST(OwnedEntryPointerAvailability, RejectsCoherentOldVersionAfterAstDeath) {
  auto original = pointerOwned(pointerSource());
  ASSERT_TRUE(original);
  ASSERT_TRUE(validateModule(*original));
  auto edited = *original;
  retargetPointerVersion(edited, {2, 1});
  for (const auto policy : {ModuleValidationPolicy::AvailableContext,
                            ModuleValidationPolicy::RequireCompleteContext}) {
    expectPointerAvailability(validateModule(edited, policy),
                              edited.functions[0].range);
    const auto copied = edited;
    expectPointerAvailability(validateModule(copied, policy),
                              copied.functions[0].range);
    ResolvedModule assigned;
    assigned = copied;
    expectPointerAvailability(validateModule(assigned, policy),
                              assigned.functions[0].range);
    auto owner = copied;
    const auto function_copy = copied.functions[0];
    owner.functions[0] = function_copy;
    expectPointerAvailability(validateModule(owner, policy),
                              owner.functions[0].range);
    ResolvedFunction function_assigned;
    function_assigned = function_copy;
    owner.functions[0] = std::move(function_assigned);
    expectPointerAvailability(validateModule(owner, policy),
                              owner.functions[0].range);
    owner.functions[0].body[0] = owner.functions[0].body[0]->clone();
    expectPointerAvailability(validateModule(owner, policy),
                              owner.functions[0].range);
  }
  EXPECT_TRUE(validateModule(*original));
}
/** Pointer declaration width remains independent of an explicit address-size header. */
TEST(OwnedEntryPointerAvailability,
     PreservesAllPointerContainersAndUnusedInputs) {
  for (const auto type : {"b32", "b64", "s32", "s64", "u32", "u64"}) {
    SCOPED_TRACE(type);
    auto module = pointerOwned(pointerSource("2.2", type));
    ASSERT_TRUE(module);
    ASSERT_EQ(module->functions[0].parameter_declarations.size(), 1);
    const auto& parameter = module->functions[0].parameter_declarations[0];
    EXPECT_EQ(parameter.role, ParameterDeclarationRole::EntryInput);
    ASSERT_TRUE(parameter.pointer);
    EXPECT_TRUE(
        module->functions[0].contract.signature.parameters[0].is_pointer);
    EXPECT_TRUE(validateModule(*module));
    retargetPointerVersion(*module, {2, 1});
    expectPointerAvailability(validateModule(*module),
                              module->functions[0].range);
    for (const auto size : {"32", "64"}) {
      auto source = pointerSource("2.3", type);
      source.insert(source.find(".entry"),
                    ".address_size " + std::string(size) + "\n");
      auto explicit_size = pointerOwned(source);
      ASSERT_TRUE(explicit_size);
      EXPECT_TRUE(validateModule(*explicit_size));
    }
  }
  for (const auto space : {".global", ".const", ".local", ".shared", ""}) {
    auto source = pointerSource();
    source.replace(source.find(".global"), 7, space);
    auto module = pointerOwned(source);
    ASSERT_TRUE(module);
    EXPECT_TRUE(validateModule(*module));
  }
}

/** Supplied source supplies the current version without becoming raw metadata history. */
TEST(OwnedEntryPointerAvailability, UsesSuppliedVersionWithoutHistoricalVeto) {
  auto module = pointerOwned(pointerSource());
  ASSERT_TRUE(module);
  PtxSyntaxParser old_parser(pointerSource("2.1"));
  const auto old_ast = old_parser.parseModule();
  ASSERT_TRUE(old_ast && old_ast.diagnostics.empty());
  expectPointerAvailability(validateModule(*old_ast, *module),
                            module->functions[0].range);
  retargetPointerVersion(*module, {2, 1});
  expectPointerAvailability(validateModule(*module),
                            module->functions[0].range);
  PtxSyntaxParser legal_parser(pointerSource());
  const auto legal_ast = legal_parser.parseModule();
  ASSERT_TRUE(legal_ast && legal_ast.diagnostics.empty());
  EXPECT_TRUE(validateModule(*legal_ast, *module));
  retargetPointerVersion(*module, {2, 2});
  EXPECT_TRUE(validateModule(*module));
  auto changed_source = pointerSource();
  changed_source.erase(changed_source.find(" .ptr .global"), 13);
  PtxSyntaxParser changed_parser(changed_source);
  const auto changed_ast = changed_parser.parseModule();
  ASSERT_TRUE(changed_ast && changed_ast.diagnostics.empty());
  ASSERT_TRUE(resolveAndValidateModule(*changed_ast));
  const auto changed = validateModule(*changed_ast, *module);
  ASSERT_FALSE(changed);
  EXPECT_TRUE(std::ranges::any_of(
      changed.error(), [](const checker::CheckDiagnostic& error) {
        return error.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
      }));
}

/** Known old versions reject without target context, while unknown versions defer. */
TEST(OwnedEntryPointerAvailability,
     SeparatesKnownVersionFromTargetCompleteness) {
  for (const bool missing_version : {false, true}) {
    auto source = pointerSource();
    const auto start = source.find(missing_version ? ".version" : ".target");
    source.erase(start, source.find('\n', start) - start + 1);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto resolved = resolveModuleOnly(*ast);
    ASSERT_TRUE(resolved);
    auto module = std::move(*resolved);
    EXPECT_TRUE(
        validateModule(module, ModuleValidationPolicy::AvailableContext));
    EXPECT_TRUE(
        validateModule(*ast, module, ModuleValidationPolicy::AvailableContext));
    expectPointerMissingContext(validateModule(module));
    expectPointerMissingContext(validateModule(*ast, module));
    if (!missing_version) {
      retargetPointerVersion(module, {2, 1});
      for (const auto policy : {ModuleValidationPolicy::AvailableContext,
                                ModuleValidationPolicy::RequireCompleteContext})
        expectPointerAvailability(validateModule(module, policy),
                                  module.functions[0].range);
      auto old_source = source;
      old_source.replace(old_source.find("2.2"), 3, "2.1");
      PtxSyntaxParser old_parser(old_source);
      const auto old_ast = old_parser.parseModule();
      ASSERT_TRUE(old_ast && old_ast.diagnostics.empty());
      expectPointerAvailability(
          validateModule(*old_ast, module,
                         ModuleValidationPolicy::AvailableContext),
          module.functions[0].range);
      EXPECT_TRUE(validateModule(*ast, module,
                                 ModuleValidationPolicy::AvailableContext));
    }
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
