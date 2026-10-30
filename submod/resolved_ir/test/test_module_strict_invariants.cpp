#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse source with diagnostics visible to the regression assertion. */
std::optional<syntax_ast::AstModule> parse_strict_fixture(
    std::string_view source) {
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  return std::move(*ast);
}

/** Return only owned IR, destroying both source storage and Syntax AST. */
std::optional<ResolvedModule> resolve_owned_fixture(std::string source) {
  auto ast = parse_strict_fixture(source);
  if (!ast)
    return std::nullopt;
  auto module = resolveModuleOnly(*ast);
  EXPECT_TRUE(module.has_value())
      << (module ? "" : module.error().front().message);
  if (!module)
    return std::nullopt;
  return std::move(*module);
}

/** Require an invariant diagnostic rather than an instruction availability failure. */
void expect_model_rejection(const checker::CheckResult& result) {
  ASSERT_FALSE(result.has_value());
  EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& diagnostic) {
    return diagnostic.kind ==
           checker::CheckDiagnosticKind::ModuleSourceMismatch;
  })) << result.error().front().message;
}

/** Header completeness depends on source traversal, including modules without functions. */
TEST(ModuleStrictInvariants, HeaderOrderIsStrictOnlyAndSurvivesAstDestruction) {
  const syntax_ast::AstModule empty_ast;
  const auto empty = resolveModuleOnly(empty_ast);
  ASSERT_TRUE(empty);
  EXPECT_FALSE(validateModule(empty_ast, *empty));
  EXPECT_FALSE(validateModule(*empty));
  EXPECT_TRUE(validateModule(*empty, ModuleValidationPolicy::AvailableContext));
  constexpr std::string_view invalid[] = {
      ".target sm_80\n.version 9.3\n",
      ".version 9.3\n",
      ".version 9.3\n.address_size 64\n.target sm_80\n",
      ".version 9.3\n.global .u32 x;\n.target sm_80\n",
      ".global .u32 x;\n.version 9.3\n.target sm_80\n",
      ".version 9.3\n.target sm_80\n.global .u32 x;\n.address_size 64\n",
  };
  for (const auto source : invalid) {
    SCOPED_TRACE(source);
    auto ast = parse_strict_fixture(source);
    ASSERT_TRUE(ast);
    auto module = resolveModuleOnly(*ast);
    ASSERT_TRUE(module) << module.error().front().message;
    EXPECT_TRUE(validateModule(*ast, *module,
                               ModuleValidationPolicy::AvailableContext));
    EXPECT_FALSE(validateModule(
        *ast, *module, ModuleValidationPolicy::RequireCompleteContext));
    EXPECT_TRUE(
        validateModule(*module, ModuleValidationPolicy::AvailableContext));
    ast.reset();
    EXPECT_FALSE(validateModule(
        *module, ModuleValidationPolicy::RequireCompleteContext));
  }
  auto valid = resolve_owned_fixture(
      ".version 9.3\n.target sm_80\n.address_size 64\n.global .u32 x;\n.target "
      "sm_90\n");
  ASSERT_TRUE(valid);
  EXPECT_TRUE(validateModule(*valid));
  auto header_only = resolve_owned_fixture(".version 9.3\n.target sm_80\n");
  ASSERT_TRUE(header_only);
  EXPECT_TRUE(validateModule(*header_only));
  header_only->header.source_order.clear();
  EXPECT_FALSE(validateModule(*header_only));
}

/** Retained target events require explicit regions, excluding the implicit initial region. */
TEST(ModuleStrictInvariants, TargetEventsRequireExplicitOwnedArchitecture) {
  auto ast =
      parse_strict_fixture(".version 9.3\n.target sm_80\n.target sm_90\n");
  ASSERT_TRUE(ast);
  const auto baseline = resolveModuleOnly(*ast);
  ASSERT_TRUE(baseline);
  ASSERT_EQ(baseline->header.regions.size(), 3u);
  EXPECT_TRUE(baseline->header.regions.front().target_options.empty());
  EXPECT_EQ(baseline->header.regions.front().target_provenance,
            SourceConfigurationProvenance::Missing);
  EXPECT_TRUE(validateModule(*baseline));
  EXPECT_TRUE(validateModule(*ast, *baseline));
  for (size_t index = 1; index < baseline->header.regions.size(); ++index) {
    auto mutated = *baseline;
    mutated.header.regions[index].target_options.clear();
    mutated.header.regions[index].target_provenance =
        SourceConfigurationProvenance::Missing;
    expect_model_rejection(validateModule(mutated));
    EXPECT_TRUE(
        validateModule(mutated, ModuleValidationPolicy::AvailableContext));
    // Source-taking validation deliberately uses the supplied source configuration.
    EXPECT_TRUE(validateModule(*ast, mutated));
  }
  auto header_only = resolve_owned_fixture(".version 9.3\n.target sm_80\n");
  ASSERT_TRUE(header_only);
  header_only->header.regions.back().target_options.clear();
  header_only->header.regions.back().target_provenance =
      SourceConfigurationProvenance::Missing;
  expect_model_rejection(validateModule(*header_only));
  auto replacement = parse_strict_fixture(".version 9.3\n.target sm_90\n");
  ASSERT_TRUE(replacement);
  EXPECT_TRUE(validateModule(*replacement, *header_only));
}

/** Every target item has a position-specific role at parser and owned boundaries. */
TEST(ModuleStrictInvariants, TargetItemsAreClassifiedAtEveryBoundary) {
  for (const auto target :
       {"bogus", "texmode_independent", "sm_80, bogus", "sm_80, sm_90"}) {
    const std::string source =
        std::string{".version 9.3\n.target "} + target + "\n";
    PtxSyntaxParser parser(source);
    const auto parsed = parser.parseModule();
    EXPECT_FALSE(parsed && parsed.diagnostics.empty()) << target;
  }
  auto module = resolve_owned_fixture(
      ".version 9.3\n.target sm_80, texmode_independent, debug, "
      "map_f64_to_f32\n");
  ASSERT_TRUE(module);
  EXPECT_TRUE(validateModule(*module));
  for (const auto option : {"bogus", "sm_90"}) {
    auto mutated = *module;
    mutated.header.regions.back().target_options.emplace_back(option);
    expect_model_rejection(validateModule(mutated));
  }
  for (const auto target : {"sm_999", "compute_999"}) {
    auto unknown = resolve_owned_fixture(std::string{".version 9.3\n.target "} +
                                         target + "\n");
    ASSERT_TRUE(unknown);
    const auto checked = validateModule(*unknown);
    ASSERT_FALSE(checked);
    EXPECT_TRUE(
        std::ranges::any_of(checked.error(), [](const auto& diagnostic) {
          return diagnostic.kind == checker::CheckDiagnosticKind::UnknownTarget;
        }));
  }
  auto ast = parse_strict_fixture(".version 9.3\n.target sm_80\n");
  ASSERT_TRUE(ast);
  auto owned = resolveModuleOnly(*ast);
  ASSERT_TRUE(owned);
  auto& target = std::get<syntax_ast::AstTargetDirective>(ast->items[1]);
  target.targets.push_back(target.targets.front());
  expect_model_rejection(validateModule(*ast, *owned));
}

/** Repeated extern occurrences and lexical identities remain valid owned storage. */
TEST(ModuleStrictInvariants,
     StorageCompletenessIncludesExternsAndNestedScopes) {
  auto module = resolve_owned_fixture(R"ptx(
.version 9.3
.target sm_80
.extern .global .u32 ext[2];
.extern .global .u32 ext[2];
.extern .shared .align 16 .b8 dynamic[];
.global .v2 .u32 group<3>;
.entry kernel() {
  .local .u32 value;
  { .local .u64 value; }
  ret;
}
)ptx");
  ASSERT_TRUE(module);
  const auto valid = validateModule(*module);
  ASSERT_TRUE(valid) << valid.error().front().message;
  ASSERT_EQ(module->storage_declarations.size(), 6u);
  EXPECT_EQ(module->storage_declarations[0].symbol_id,
            module->storage_declarations[1].symbol_id);
  auto missing = *module;
  missing.storage_declarations.erase(missing.storage_declarations.begin());
  expect_model_rejection(validateModule(missing));
  auto duplicate = *module;
  duplicate.storage_declarations.push_back(
      duplicate.storage_declarations.front());
  expect_model_rejection(validateModule(duplicate));
  auto empty = *module;
  empty.storage_declarations.clear();
  expect_model_rejection(validateModule(empty));
}

/** One bound extern identity must retain the same ordered shape at every occurrence. */
TEST(ModuleStrictInvariants,
     ExternStorageOccurrencesRequireMatchingArrayShape) {
  for (const auto shape : {"[2][3]", "[][2]"}) {
    SCOPED_TRACE(shape);
    const std::string declaration =
        std::string{".extern .global .u32 data"} + shape + ";\n";
    const std::string source =
        ".version 9.3\n.target sm_80\n" + declaration + declaration;
    const auto ast = parse_strict_fixture(source);
    ASSERT_TRUE(ast);
    const auto baseline = resolveModuleOnly(*ast);
    ASSERT_TRUE(baseline);
    ASSERT_EQ(baseline->storage_declarations.size(), 2u);
    ASSERT_EQ(baseline->storage_declarations[0].symbol_id,
              baseline->storage_declarations[1].symbol_id);
    ASSERT_TRUE(validateModule(*ast, *baseline));
    ASSERT_TRUE(validateModule(*baseline));
    const std::function<void(ResolvedStorageDeclaration&)> mutations[] = {
        [](auto& declaration) {
          declaration.array_extents =
              declaration.array_extents.front()
                  ? std::vector<std::optional<uint64_t>>{3, 2}
                  : std::vector<std::optional<uint64_t>>{std::nullopt, 3};
        },
        [](auto& declaration) {
          declaration.array_extents =
              declaration.array_extents.front()
                  ? std::vector<std::optional<uint64_t>>{6}
                  : std::vector<std::optional<uint64_t>>{std::nullopt, 1, 2};
        },
        [](auto& declaration) {
          if (declaration.array_extents.front()) {
            declaration.array_extents.front().reset();
            declaration.byte_extent.reset();
          } else {
            declaration.array_extents.front() = 3;
            declaration.byte_extent = 24;
          }
        },
    };
    for (const auto& mutate : mutations) {
      for (size_t index = 0; index < baseline->storage_declarations.size();
           ++index) {
        SCOPED_TRACE(index);
        auto module = *baseline;
        mutate(module.storage_declarations[index]);
        for (const auto policy :
             {ModuleValidationPolicy::RequireCompleteContext,
              ModuleValidationPolicy::AvailableContext}) {
          expect_model_rejection(validateModule(*ast, module, policy));
          expect_model_rejection(validateModule(module, policy));
        }
      }
    }
  }
  const auto incompatible = parse_strict_fixture(
      ".version 9.3\n.target sm_80\n.extern .global .u32 data[2][3];\n"
      ".extern .global .u32 data[3][2];\n");
  ASSERT_TRUE(incompatible);
  EXPECT_FALSE(resolveModuleOnly(*incompatible));
}

/** Compatible extern occurrences retain alignment spelling, unsizedness, and scope. */
TEST(ModuleStrictInvariants, CompatibleExternStorageShapesRemainValid) {
  const auto ast = parse_strict_fixture(R"ptx(
.version 9.3
.target sm_80
.extern .global .u32 data[2][3];
.extern .global .align 4 .u32 data[2][3];
.extern .global .u32 unsized[][2];
.extern .global .u32 unsized[][2];
.extern .shared .b8 dynamic[];
.extern .shared .b8 dynamic[];
.entry kernel() {
  .local .u32 data[3][2];
  { .local .u32 data[6]; }
  ret;
}
)ptx");
  ASSERT_TRUE(ast);
  const auto module = resolveModuleOnly(*ast);
  ASSERT_TRUE(module);
  ASSERT_EQ(module->storage_declarations.size(), 8u);
  EXPECT_EQ(module->storage_declarations[0].symbol_id,
            module->storage_declarations[1].symbol_id);
  EXPECT_NE(module->storage_declarations[0].explicit_alignment,
            module->storage_declarations[1].explicit_alignment);
  EXPECT_NE(module->storage_declarations[0].symbol_id,
            module->storage_declarations[6].symbol_id);
  EXPECT_NE(module->storage_declarations[6].symbol_id,
            module->storage_declarations[7].symbol_id);
  for (const auto policy : {ModuleValidationPolicy::RequireCompleteContext,
                            ModuleValidationPolicy::AvailableContext}) {
    EXPECT_TRUE(validateModule(*ast, *module, policy));
    EXPECT_TRUE(validateModule(*module, policy));
  }
}

/** Scalar storage mutations cannot produce a false complete-context certificate. */
TEST(ModuleStrictInvariants, RejectsScalarStorageMutationsInBothValidators) {
  auto ast = parse_strict_fixture(
      ".version 9.3\n.target sm_80\n.global .align 8 .u32 data[2];\n");
  ASSERT_TRUE(ast);
  const auto baseline = resolveModuleOnly(*ast);
  ASSERT_TRUE(baseline);
  const std::function<void(ResolvedStorageDeclaration&)> mutations[] = {
      [](auto& d) { d.byte_extent = 0; },
      [](auto& d) { d.alignment = 3; },
      [](auto& d) { d.symbol_id.value = std::numeric_limits<uint32_t>::max(); },
      [](auto& d) { d.element_type = ScalarType::F64; },
      [](auto& d) { d.vector_width = 4; },
      [](auto& d) { d.array_extents[0] = 0; },
      [](auto& d) { d.array_extents[0] = std::nullopt; },
      [](auto& d) { d.declaration_kind = StorageDeclarationKind::External; },
      [](auto& d) { d.is_dynamic_shared = true; },
      [](auto& d) { d.owner_function = binding::SymbolId{0}; },
      [](auto& d) {
        d.array_extents[0] = std::numeric_limits<uint64_t>::max();
      },
  };
  for (const auto& mutate : mutations) {
    auto module = *baseline;
    mutate(module.storage_declarations.front());
    expect_model_rejection(validateModule(*ast, module));
    expect_model_rejection(validateModule(module));
  }
  auto destroyed = resolve_owned_fixture(
      ".version 9.3\n.target sm_80\n.global .u32 data[2];\n");
  ASSERT_TRUE(destroyed);
  EXPECT_TRUE(validateModule(*destroyed));
  destroyed->storage_declarations.front().byte_extent = 0;
  expect_model_rejection(validateModule(*destroyed));
}

/** Mixed opaque/scalar inputs and matching prototype/definition scopes stay independent. */
TEST(ModuleStrictInvariants, FormalScopesAndMixedKindsRetainTheirAbi) {
  auto module = resolve_owned_fixture(R"ptx(
.version 9.3
.target sm_80
.func (.param .u32 result) device(.param .b8 data[8], .reg .u32 value);
.func (.param .u32 result) device(.param .b8 bytes[8], .reg .u32 regvalue) {
  .param .u32 body_value;
  ret;
}
.entry kernel(.param .u32 scalar, .param .texref texture,
              .param .u64 .ptr .global .align 16 pointer, .param .samplerref sampler) {
  ret;
}
)ptx");
  ASSERT_TRUE(module);
  const auto valid = validateModule(*module);
  ASSERT_TRUE(valid) << valid.error().front().message;
  ASSERT_EQ(module->functions.size(), 3u);
  EXPECT_EQ(module->functions[0].symbol_id, module->functions[1].symbol_id);
  EXPECT_NE(module->functions[0].declaration_scope,
            module->functions[1].declaration_scope);
  auto mutated = *module;
  mutated.functions[2].contract.signature.parameters[0].scalar_type =
      ScalarType::F64;
  expect_model_rejection(validateModule(mutated));
  mutated = *module;
  mutated.functions[1].contract.signature.return_parameters[0].scalar_type =
      ScalarType::F64;
  expect_model_rejection(validateModule(mutated));
  mutated = *module;
  mutated.functions[2].contract.signature.parameters[2].pointer_alignment =
      uint64_t{8};
  expect_model_rejection(validateModule(mutated));
  mutated = *module;
  mutated.functions[1].parameter_declarations.back().role =
      ParameterDeclarationRole::DeviceInput;
  expect_model_rejection(validateModule(mutated));
  mutated = *module;
  mutated.functions[2].parameter_declarations.clear();
  expect_model_rejection(validateModule(mutated));
  mutated = *module;
  mutated.functions[1].contract.signature.parameters[1].scalar_type =
      ScalarType::F64;
  expect_model_rejection(validateModule(mutated));
  mutated = *module;
  mutated.functions[2].contract.signature.parameters[2].pointer_alignment =
      uint64_t{3};
  mutated.functions[2].parameter_declarations[1].pointer->pointed_alignment = 3;
  expect_model_rejection(validateModule(mutated));
}

/** Invalid numeric alternatives must not collapse to absent opaque ABI fields. */
TEST(ModuleStrictInvariants, OpaqueNormalizedFieldsFailBothValidators) {
  for (const auto parameter :
       {".param .texref texture", ".param .align 8 .texref texture[2]"}) {
    SCOPED_TRACE(parameter);
    const std::string source =
        std::string{".version 9.3\n.target sm_80\n.entry kernel("} + parameter +
        ") { ret; }\n";
    const auto ast = parse_strict_fixture(source);
    ASSERT_TRUE(ast);
    const auto baseline = resolveModuleOnly(*ast);
    ASSERT_TRUE(baseline);
    EXPECT_TRUE(validateModule(*ast, *baseline));
    EXPECT_TRUE(validateModule(*baseline));
    const std::function<void(declaration_semantics::FunctionParameterContract&)>
        mutations[] = {
            [](auto& formal) {
              formal.alignment =
                  declaration_semantics::InvalidStructuralKey{"bad"};
            },
            [](auto& formal) {
              formal.array_extent =
                  declaration_semantics::InvalidStructuralKey{"bad"};
            },
            [](auto& formal) {
              formal.is_array = false;
              formal.array_extent = uint64_t{2};
            },
            [](auto& formal) {
              formal.pointer_alignment =
                  declaration_semantics::InvalidStructuralKey{"bad"};
            },
        };
    for (const auto& mutate : mutations) {
      auto module = resolveModuleOnly(*ast);
      ASSERT_TRUE(module);
      ASSERT_TRUE(validateModule(*module));
      mutate(module->functions.front().contract.signature.parameters.front());
      expect_model_rejection(validateModule(*ast, *module));
      expect_model_rejection(validateModule(*module));
    }
  }
}

/** Signature-only scalar mutations fail with and without source AST ownership. */
TEST(ModuleStrictInvariants, SignatureMutationFailsBothValidators) {
  auto ast = parse_strict_fixture(
      ".version 9.3\n.target sm_80\n.func f(.param .u32 x, .param .u64 y) { "
      "ret; }\n");
  ASSERT_TRUE(ast);
  const auto baseline = resolveModuleOnly(*ast);
  ASSERT_TRUE(baseline);
  const std::function<void(ResolvedFunction&)> mutations[] = {
      [](auto& f) {
        f.contract.signature.parameters[0].scalar_type = ScalarType::F64;
      },
      [](auto& f) {
        std::swap(f.contract.signature.parameters[0],
                  f.contract.signature.parameters[1]);
      },
      [](auto& f) {
        f.contract.signature.parameters[0].alignment = uint64_t{3};
      },
      [](auto& f) { f.parameter_declarations[0].byte_extent = 0; },
      [](auto& f) {
        f.parameter_declarations[0].role =
            ParameterDeclarationRole::DeviceReturn;
      },
      [](auto& f) { f.parameter_declarations[0].array_extents.push_back(0); },
      [](auto& f) {
        std::swap(f.parameter_declarations[0], f.parameter_declarations[1]);
      },
  };
  for (const auto& mutate : mutations) {
    auto module = *baseline;
    mutate(module.functions[0]);
    expect_model_rejection(validateModule(*ast, module));
    expect_model_rejection(validateModule(module));
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir {
namespace {
/** Vector signature ownership survives AST release and detects ABI corruption. */
TEST(ModuleStrictInvariants, VectorRegisterFormalsRejoinOwnedBinding) {
  using call_argument_compatibility::CallArgumentVectorShape;
  const std::string source =
      ".version 9.3\n.target sm_80\n.func (.reg .v2 .u32 r) f(.reg .v4 .u32 p) "
      "{ ret; }\n";
  auto owned = resolve_owned_fixture(source);
  ASSERT_TRUE(owned);
  EXPECT_TRUE(validateModule(*owned));
  EXPECT_EQ(
      owned->functions[0].contract.signature.return_parameters[0].vector_shape,
      CallArgumentVectorShape::V2);
  for (const auto shape :
       {CallArgumentVectorShape::Scalar, CallArgumentVectorShape::V2,
        CallArgumentVectorShape::Invalid,
        static_cast<CallArgumentVectorShape>(255)}) {
    auto module = resolve_owned_fixture(source);
    ASSERT_TRUE(module);
    module->functions[0].contract.signature.parameters[0].vector_shape = shape;
    expect_model_rejection(validateModule(*module));
  }
  for (const auto width : {1, 2, 3, 8}) {
    auto module = resolve_owned_fixture(source);
    ASSERT_TRUE(module);
    auto& symbols =
        const_cast<std::vector<binding::Symbol>&>(module->symbols.symbols());
    auto found = std::ranges::find_if(symbols, [](const auto& symbol) {
      return symbol.kind == binding::SymbolKind::InputParameter;
    });
    ASSERT_NE(found, symbols.end());
    found->vector_width = width;
    expect_model_rejection(validateModule(*module));
  }
  for (const auto type :
       {ScalarType::Pred, ScalarType::U64, ScalarType::BF16}) {
    auto module = resolve_owned_fixture(source);
    ASSERT_TRUE(module);
    module->functions[0].contract.signature.parameters[0].scalar_type = type;
    expect_model_rejection(validateModule(*module));
  }
  auto entry = resolve_owned_fixture(source);
  ASSERT_TRUE(entry);
  entry->functions[0].is_entry = true;
  entry->functions[0].contract.signature.is_entry = true;
  expect_model_rejection(validateModule(*entry));
  auto alignment = resolve_owned_fixture(source);
  ASSERT_TRUE(alignment);
  alignment->functions[0].contract.signature.parameters[0].alignment =
      uint64_t{4};
  expect_model_rejection(validateModule(*alignment));
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
