#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Resolve source-local declarations and return only independently owned data. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> abi_module(
    std::string source) {
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveAndValidateModule(*ast);
}
/** Identify the formal-width diagnostic rather than unrelated context failures. */
bool has_formal_width(const checker::CheckResult& result) {
  return !result &&
         std::ranges::any_of(result.error(), [](const auto& diagnostic) {
           return diagnostic.message.find("Register formal total width") !=
                  std::string::npos;
         });
}
/** Retarget all owned region/function caches coherently without changing declarations. */
void retarget_formal_module(ResolvedModule& module, checker::PtxVersion version,
                            std::string target) {
  for (auto& region : module.header.regions) {
    region.version = version;
    if (!region.target_options.empty())
      region.target_options.front() = target;
  }
  for (auto& function : module.functions) {
    function.source_version = version;
    function.source_target = target;
  }
}

TEST(RegisterFormalAbi, ChecksWholeValueAcrossAllFormalPositions) {
  /** One otherwise eligible scalar/vector formal and its whole-width result. */
  struct Case {
    /** Written scalar identity and optional explicit vector modifier. */
    const char* type;
    /** Whether total width meets the active ABI minimum. */
    bool valid;
  };
  constexpr Case cases[] = {
      {".u8", false},    {".s8", false},       {".b8", false},
      {".u16", false},   {".s16", false},      {".b16", false},
      {".f16", false},   {".pred", false},     {".v2 .u8", false},
      {".v4 .u8", true}, {".v2 .u16", true},   {".v2 .f16", true},
      {".f16x2", true},  {".v2 .f16x2", true}, {".u32", true},
      {".f64", true},    {".b128", true},      {".v4 .u32", true},
  };
  for (const auto& item : cases) {
    for (int role = 0; role < 6; ++role) {
      SCOPED_TRACE(item.type);
      SCOPED_TRACE(role);
      const auto parameter = std::string(".reg ") + item.type + " x";
      const auto declaration =
          role == 0   ? ".func f(" + parameter + ") { ret; }"
          : role == 1 ? ".func f(" + parameter + ");"
          : role == 2 ? ".extern .func f(" + parameter + ");"
          : role == 3 ? ".func (" + parameter + ") f() { ret; }"
          : role == 4
              ? ".entry k() { p: .callprototype _ (" + parameter + "); }"
              : ".entry k() { p: .callprototype (" + parameter + ") _ (); }";
      const auto ast = test_helpers::parseModule(
          ".version 9.3\n.target sm_90\n" + declaration);
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      const auto resolved = resolveModuleOnly(*ast);
      ASSERT_EQ(resolved.has_value(), item.valid);
      if (!resolved) {
        EXPECT_TRUE(
            std::ranges::any_of(resolved.error(), [](const auto& diagnostic) {
              return diagnostic.message.find("Register formal total width") !=
                     std::string::npos;
            }));
      } else {
        auto copied = *resolved;
        EXPECT_TRUE(validateModule(copied));
        for (auto& function : copied.functions)
          for (auto& instruction : function.body)
            instruction = instruction->clone();
        EXPECT_TRUE(validateModule(copied));
      }
    }
  }
  EXPECT_FALSE(
      abi_module(".version 9.3\n.target sm_90\n.func f(.reg .v4 .b64 x);"));
  EXPECT_FALSE(
      abi_module(".version 9.3\n.target sm_90\n.func f(.reg .u32 x[2]);"));
}

TEST(RegisterFormalAbi, RetainsHistoricalAndActiveTargetRegionBoundaries) {
  for (const auto header :
       {".version 1.4\n.target sm_13\n", ".version 1.5\n.target sm_13\n",
        ".version 2.0\n.target sm_13\n"}) {
    auto module = abi_module(std::string(header) +
                             ".func (.reg .u8 r) f(.reg .u16 x) { ret; }");
    ASSERT_TRUE(module);
    EXPECT_TRUE(validateModule(*module));
  }
  EXPECT_FALSE(
      abi_module(".version 2.0\n.target sm_20\n.func f(.reg .u16 x);"));
  EXPECT_TRUE(abi_module(".version 2.0\n.target sm_20\n.func f(.reg .u32 x);"));
  const auto ast = test_helpers::parseModule(R"ptx(
.version 2.0
.target sm_13
.func old(.reg .u8 x);
.target sm_20
.func modern(.reg .u8 x);
)ptx");
  ASSERT_TRUE(ast);
  const auto rejected = resolveModuleOnly(*ast);
  ASSERT_FALSE(rejected);
  ASSERT_EQ(rejected.error().size(), 1u);
  EXPECT_NE(
      rejected.error().front().message.find("Register formal total width"),
      std::string::npos);
  EXPECT_EQ(rejected.error().front().range.start.line, 6u);
  EXPECT_TRUE(
      abi_module(".version 2.0\n.target sm_20\n.func wide(.reg .u32 "
                 "x);\n.target sm_13\n.func small(.reg .u8 x);"));
}

TEST(RegisterFormalAbi, DefersOnlyMissingAbiContext) {
  for (const auto source :
       {".func f(.reg .u8 x);", ".version 9.3\n.func f(.reg .u8 x);",
        ".target sm_80\n.func f(.reg .u8 x);"}) {
    const auto ast = test_helpers::parseModule(source);
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto module = resolveModuleOnly(*ast);
    ASSERT_TRUE(module);
    EXPECT_TRUE(validateModule(*ast, *module,
                               ModuleValidationPolicy::AvailableContext));
    EXPECT_TRUE(
        validateModule(*module, ModuleValidationPolicy::AvailableContext));
    const auto strict = validateModule(*module);
    ASSERT_FALSE(strict);
    EXPECT_TRUE(std::ranges::any_of(strict.error(), [](const auto& diagnostic) {
      return diagnostic.kind ==
             checker::CheckDiagnosticKind::MissingValidationContext;
    }));
    EXPECT_FALSE(has_formal_width(strict));
    const auto source_strict = validateModule(*ast, *module);
    ASSERT_FALSE(source_strict);
    EXPECT_TRUE(
        std::ranges::any_of(source_strict.error(), [](const auto& diagnostic) {
          return diagnostic.kind ==
                 checker::CheckDiagnosticKind::MissingValidationContext;
        }));
    EXPECT_FALSE(has_formal_width(source_strict));
  }
  const auto unknown = test_helpers::parseModule(
      ".version 9.3\n.target sm_80\n.func wide(.reg .u32 x);\n.target "
      "sm_999\n.func small(.reg .u8 x);");
  ASSERT_TRUE(unknown && unknown.diagnostics.empty());
  ASSERT_EQ(unknown->items.size(), 5u);
  const auto* cleared_target =
      std::get_if<syntax_ast::AstTargetDirective>(&unknown->items[3]);
  ASSERT_NE(cleared_target, nullptr);
  ASSERT_EQ(cleared_target->targets.size(), 1u);
  EXPECT_EQ(cleared_target->targets.front().text, "sm_999");
  const auto bound = binding::bindSymbols(*unknown);
  const auto declarations =
      declaration_semantics::checkDeclarations(*unknown, bound.table);
  EXPECT_FALSE(std::ranges::any_of(declarations, [](const auto& diagnostic) {
    return diagnostic.message.find("Register formal total width") !=
           std::string::npos;
  }));
  auto owned = abi_module(".version 1.4\n.target sm_13\n.func f(.reg .u8 x);");
  ASSERT_TRUE(owned);
  retarget_formal_module(*owned, {9, 3}, "sm_999");
  const auto checked =
      validateModule(*owned, ModuleValidationPolicy::AvailableContext);
  ASSERT_FALSE(checked);
  EXPECT_FALSE(has_formal_width(checked));
  EXPECT_TRUE(std::ranges::any_of(checked.error(), [](const auto& diagnostic) {
    return diagnostic.kind == checker::CheckDiagnosticKind::UnknownTarget;
  }));
}

TEST(RegisterFormalAbi, RechecksCoherentOwnedAndReplacementSourceContexts) {
  const std::string legacy =
      ".version 1.4\n.target sm_13\n.func f(.reg .u8 x) { ret; }";
  auto module = abi_module(legacy);
  ASSERT_TRUE(module);
  auto copy = *module;
  copy.functions.front().body.front() =
      copy.functions.front().body.front()->clone();
  retarget_formal_module(copy, {2, 0}, "sm_20");
  const auto modern = validateModule(copy);
  ASSERT_TRUE(has_formal_width(modern));
  ASSERT_EQ(modern.error().size(), 1u);
  const auto legacy_ast = test_helpers::parseModule(legacy);
  ASSERT_TRUE(legacy_ast);
  EXPECT_TRUE(validateModule(*legacy_ast, copy));
  const auto modern_ast = test_helpers::parseModule(
      ".version 2.0\n.target sm_20\n.func f(.reg .u8 x) { ret; }");
  ASSERT_TRUE(modern_ast);
  EXPECT_TRUE(has_formal_width(validateModule(*modern_ast, *module)));
  retarget_formal_module(copy, {1, 4}, "sm_13");
  EXPECT_TRUE(validateModule(copy));
  auto wide =
      abi_module(".version 1.4\n.target sm_13\n.func f(.reg .u32 x) { ret; }");
  ASSERT_TRUE(wide);
  retarget_formal_module(*wide, {2, 0}, "sm_20");
  EXPECT_TRUE(validateModule(*wide));
}

TEST(RegisterFormalAbi,
     RechecksUnusedOwnedPrototypeWithoutBindingTypeConfusion) {
  auto module = abi_module(
      ".version 9.3\n.target sm_90\n.entry k() { p: .callprototype (.reg .u32 "
      "r) _ (.reg .u32 x); }");
  ASSERT_TRUE(module);
  for (const auto input : {false, true}) {
    auto invalid = *module;
    auto& signature =
        invalid.functions.front().call_prototypes.front().signature;
    auto& formal = input ? signature.parameters.front()
                         : signature.return_parameters.front();
    formal.scalar_type = ScalarType::U8;
    formal.type_spelling = ".u8";
    formal.vector_shape =
        call_argument_compatibility::CallArgumentVectorShape::V2;
    formal.alignment = uint64_t{2};
    const auto checked = validateModule(invalid);
    ASSERT_TRUE(has_formal_width(checked));
    ASSERT_EQ(checked.error().size(), 1u);
    formal.vector_shape =
        call_argument_compatibility::CallArgumentVectorShape::V4;
    formal.alignment = uint64_t{4};
    EXPECT_TRUE(validateModule(invalid));
  }
}

TEST(RegisterFormalAbi, ReturnOnlyPrototypeCannotHideBehindAnEntryFlag) {
  const auto ast = test_helpers::parseModule(
      ".version 9.3\n.target sm_90\n.entry k() { p: .callprototype (.reg .u32 "
      "r) _ (); }");
  ASSERT_TRUE(ast);
  auto module = resolveAndValidateModule(*ast);
  ASSERT_TRUE(module);
  auto copied = *module;
  auto& signature = copied.functions.front().call_prototypes.front().signature;
  signature.is_entry = true;
  auto& result = signature.return_parameters.front();
  result.scalar_type = ScalarType::U8;
  result.type_spelling = ".u8";
  result.alignment = uint64_t{1};
  EXPECT_TRUE(has_formal_width(validateModule(copied)));
  EXPECT_TRUE(has_formal_width(validateModule(*ast, copied)));
  signature.is_entry = false;
  result.scalar_type = ScalarType::U32;
  result.type_spelling = ".u32";
  result.alignment = uint64_t{4};
  EXPECT_TRUE(validateModule(copied));
}

TEST(RegisterFormalAbi, PreservesSmallOrdinaryRegistersAndParameterFormals) {
  auto module = abi_module(R"ptx(
.version 9.3
.target sm_90
.extern .func (.param .u8 r) take(.param .u8 x, .param .f16 h);
.entry k(.param .u8 input, .param .f16 half) {
  .reg .u8 small;
  .reg .u16 narrow;
  .reg .f16 h;
  .reg .pred p;
  .param .u8 result;
  call (result), take, (small, h);
}
)ptx");
  ASSERT_TRUE(module) << (module.error().empty()
                              ? ""
                              : module.error().front().message);
  auto copy = *module;
  EXPECT_TRUE(validateModule(copy));
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
