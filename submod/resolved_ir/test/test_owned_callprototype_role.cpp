#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Build a legal unused device-call interface with an independently owned body. */
std::string prototype_role_source(std::string_view signature) {
  return ".version 9.3\n.target sm_90\n.entry kernel() { proto: "
         ".callprototype " +
         std::string(signature) + "; ret; }";
}
/** Return strict owned data after original text, parser and AST destruction. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> prototype_role_module(
    std::string_view signature = "(.reg .u32 result) _ ()") {
  const auto ast = test_helpers::parseModule(prototype_role_source(signature));
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  auto result = resolveAndValidateModule(*ast);
  if (result)
    EXPECT_TRUE(validateModule(*ast, *result));
  return result;
}

/** An entry flag alone cannot change the role of an unused return-only prototype. */
TEST(OwnedCallPrototypeRole, RejectsEntryFlagWithoutAWidthOrBindingMismatch) {
  auto module = prototype_role_module();
  ASSERT_TRUE(module) << (module.error().empty()
                              ? ""
                              : module.error().front().message);
  ASSERT_TRUE(validateModule(*module));
  auto copied = *module;
  ResolvedModule assigned;
  assigned = copied;
  auto& function = assigned.functions.front();
  function.body.front() = function.body.front()->clone();
  ASSERT_NE(function.body.front().get(),
            module->functions.front().body.front().get());
  ASSERT_EQ(function.call_prototypes.size(), 1u);
  auto& prototype = function.call_prototypes.front();
  EXPECT_FALSE(prototype.signature.is_entry);
  ASSERT_EQ(prototype.signature.return_parameters.size(), 1u);
  EXPECT_EQ(prototype.signature.return_parameters[0].scalar_type,
            ScalarType::U32);
  EXPECT_TRUE(prototype.signature.parameters.empty());
  prototype.signature.is_entry = true;
  const auto checked = validateModule(assigned);
  ASSERT_FALSE(checked);
  ASSERT_EQ(checked.error().size(), 1u);
  EXPECT_EQ(checked.error().front().kind,
            checker::CheckDiagnosticKind::ModuleSourceMismatch);
  EXPECT_EQ(checked.error().front().range, prototype.range);
  EXPECT_EQ(checked.error().front().message,
            "Resolved .callprototype signature must be a non-entry device-call "
            "interface.");
  auto copied_mutation = assigned;
  EXPECT_FALSE(validateModule(copied_mutation));
}
/** Return/input shapes keep device-call identity independently of their storage types. */
TEST(OwnedCallPrototypeRole, PreservesEligibleShapesCopiesAndCurrentIdentity) {
  for (const auto signature :
       {"(.reg .u32 result) _ ()", "(.reg .v2 .u16 result) _ ()",
        "(.param .u8 result) _ ()", "(.param .v2 .u32 result[2][3]) _ ()",
        "_ (.reg .u32 input)", "_ (.param .u8 input)", "_ ()"}) {
    SCOPED_TRACE(signature);
    auto original = prototype_role_module(signature);
    ASSERT_TRUE(original) << (original.error().empty()
                                  ? ""
                                  : original.error().front().message);
    auto copied = *original;
    ResolvedModule assigned;
    assigned = copied;
    auto& function = assigned.functions.front();
    function.body.front() = function.body.front()->clone();
    const auto& expected = original->functions.front().call_prototypes.front();
    auto& prototype = function.call_prototypes.front();
    EXPECT_EQ(prototype.symbol_id, expected.symbol_id);
    EXPECT_EQ(prototype.scope_id, expected.scope_id);
    EXPECT_EQ(prototype.range, expected.range);
    EXPECT_EQ(prototype.signature, expected.signature);
    EXPECT_EQ(assigned.symbols.symbol(prototype.symbol_id).kind,
              binding::SymbolKind::CallPrototype);
    EXPECT_TRUE(validateModule(assigned));
    prototype.signature.is_entry = true;
    const auto invalid = validateModule(assigned);
    ASSERT_FALSE(invalid);
    EXPECT_TRUE(
        std::ranges::any_of(invalid.error(), [&](const auto& diagnostic) {
          return diagnostic.kind ==
                     checker::CheckDiagnosticKind::ModuleSourceMismatch &&
                 diagnostic.range == prototype.range &&
                 diagnostic.message ==
                     "Resolved .callprototype signature must be a non-entry "
                     "device-call interface.";
        }));
    EXPECT_FALSE(
        std::ranges::any_of(invalid.error(), [](const auto& diagnostic) {
          return diagnostic.message.find("Register formal total width") !=
                 std::string::npos;
        }));
    EXPECT_FALSE(
        validateModule(assigned, ModuleValidationPolicy::AvailableContext));
    prototype.signature.is_entry = false;
    EXPECT_TRUE(validateModule(assigned));
    EXPECT_TRUE(validateModule(*original));
  }
}

/** Source association rejects supplied syntax changes separately from the owned role flag. */
TEST(OwnedCallPrototypeRole,
     KeepsSourceAssociationIndependentOfOwnedFlagChecking) {
  auto module = prototype_role_module();
  ASSERT_TRUE(module);
  const auto replacement = test_helpers::parseModule(
      prototype_role_source("(.reg .s32 result) _ ()"));
  ASSERT_TRUE(replacement && replacement.diagnostics.empty());
  EXPECT_TRUE(resolveAndValidateModule(*replacement));
  const auto stale = validateModule(*replacement, *module);
  ASSERT_FALSE(stale);
  EXPECT_TRUE(std::ranges::any_of(stale.error(), [](const auto& diagnostic) {
    return diagnostic.kind ==
           checker::CheckDiagnosticKind::ModuleSourceMismatch;
  }));
  EXPECT_TRUE(validateModule(*module));
  module->functions.front().call_prototypes.front().signature.is_entry = true;
  EXPECT_FALSE(validateModule(*module));
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
