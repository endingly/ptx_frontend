#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_storage_declarations.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve a source-owned module while keeping parser diagnostics test-visible. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> resolveSource(
    std::string source) {
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  EXPECT_TRUE(ast.has_value());
  EXPECT_TRUE(ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty()) {
    return std::unexpected(ModuleResolveDiagnostics{{
        .message = ast.diagnostics.empty() ? "PTX source did not parse."
                                           : ast.diagnostics.front().message,
    }});
  }
  return resolveModule(*ast);
}

/** Find the earliest source-projected declaration with a given lexical identifier. */
const ResolvedStorageDeclaration& storageNamed(const ResolvedModule& module,
                                               std::string_view name) {
  const auto match = std::ranges::find_if(
      module.storage_declarations, [&](const auto& declaration) {
        return module.symbols.symbol(declaration.symbol_id).name == name;
      });
  if (match == module.storage_declarations.end()) {
    ADD_FAILURE() << "No storage declaration named '" << name << "'.";
    throw std::runtime_error("Storage declaration lookup failed.");
  }
  return *match;
}

/** Report whether resolution retained a declaration-stage diagnostic category. */
bool hasDeclarationKind(const ModuleResolveDiagnostics& diagnostics,
                        declaration_semantics::DeclarationDiagnosticKind kind) {
  return std::ranges::any_of(diagnostics, [kind](const auto& diagnostic) {
    return diagnostic.declaration_kind == kind;
  });
}

/** Leading-zero integer constants retain their octal value through storage lowering. */
TEST(ResolvedStorageDeclarations, OctalInitializerAndExtent) {
  auto resolved = resolveSource(R"ptx(
.version 8.0
.target sm_80
.address_size 64
.global .u32 octal_value = 010;
.global .u8 octal_extent[010];
)ptx");
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& value = storageNamed(*resolved, "octal_value");
  ASSERT_EQ(value.initializer.size(), 1u);
  EXPECT_EQ(std::get<StorageConstant>(value.initializer[0].value).bits, 8u);
  EXPECT_EQ(storageNamed(*resolved, "octal_extent").array_extents,
            (std::vector<std::optional<uint64_t>>{8u}));
}

/** Octal decoding applies before folding, storage narrowing, and relocation lowering. */
TEST(ResolvedStorageDeclarations, OctalExpressionsAndDeclarationMetadata) {
  auto resolved = resolveSource(R"ptx(
.global .align 010 .u8 values[010] = {0, 0U, 010, 077u, +010, -010, 010 + 02, 0x10};
.global .u32 slots<010>;
.global .u64 pointer = generic(values) + 010;
.global .u64 signedness[] = {(-010 < 0), (-010U < 0), 01777777777777777777777U};
)ptx");
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& values = storageNamed(*resolved, "values");
  EXPECT_EQ(values.alignment, 8u);
  EXPECT_EQ(values.byte_extent, 8u);
  const std::array<uint64_t, 8> expected{0, 0, 8, 63, 8, 248, 10, 16};
  ASSERT_EQ(values.initializer.size(), expected.size());
  for (size_t index = 0; index < expected.size(); ++index) {
    EXPECT_EQ(std::get<StorageConstant>(values.initializer[index].value).bits,
              expected[index]);
  }
  EXPECT_EQ(storageNamed(*resolved, "slots").parameterized_count, 8u);
  const auto& pointer = storageNamed(*resolved, "pointer");
  ASSERT_EQ(pointer.initializer.size(), 1u);
  EXPECT_EQ(std::get<StorageRelocation>(pointer.initializer[0].value).addend_bits,
            8u);
  const auto& signedness = storageNamed(*resolved, "signedness");
  ASSERT_EQ(signedness.initializer.size(), 3u);
  EXPECT_EQ(std::get<StorageConstant>(signedness.initializer[0].value).bits, 1u);
  EXPECT_EQ(std::get<StorageConstant>(signedness.initializer[1].value).bits, 0u);
  EXPECT_EQ(std::get<StorageConstant>(signedness.initializer[2].value).bits,
            std::numeric_limits<uint64_t>::max());
}

/** Invalid octal input reports its literal position, including inside foldable expressions. */
TEST(ResolvedStorageDeclarations, InvalidOctalCannotReachCleanResolution) {
  for (const std::string source : {
           ".global .u32 invalid = 09;",
           ".global .u32 invalid = (09 ? 7 : 7);",
           ".global .u8 invalid[09];",
           ".entry k() { .reg .u32 %r; mov.u32 %r, 09; }",
           ".entry k() { .reg .u32 %r; ld.global.u32 %r, [09]; }"}) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_FALSE(ast.diagnostics.empty());
    EXPECT_TRUE(std::ranges::any_of(ast.diagnostics, [&](const auto& diagnostic) {
      return diagnostic.range.start.column ==
             static_cast<int32_t>(source.find("09") + 1);
    }));
  }
}

/** A failed integer decode must not become a value through equal-branch folding. */
TEST(ResolvedStorageDeclarations, RejectsUndecodableConditionalInteger) {
  const auto resolved = resolveSource(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.global .u32 result = (18446744073709551616 ? 7 : 7);
.global .u8 bytes[18446744073709551616 ? 1 : 1];
.visible .entry k() { ret; }
)ptx");
  EXPECT_FALSE(resolved.has_value());
}

/** Module diagnostics retain the undecodable literal's category and exact source range. */
TEST(ResolvedStorageDeclarations, RetainsInvalidIntegerLiteralDiagnostics) {
  constexpr std::array contexts{
      std::pair{".global .u32 value = ", ";"},
      std::pair{".global .u32 value = (", " ? 7 : 7);"},
      std::pair{".global .u8 bytes[", " ? 1 : 1];"},
      std::pair{".entry k(.param .b8 arg[", " ? 1 : 1]) { ret; }"},
      std::pair{".entry k() { .local .u8 local[", " ? 1 : 1]; }"},
      std::pair{".entry k() { .param .b8 slot[", " ? 1 : 1]; ret; }"},
      std::pair{".global .u32 value = (-", " ? 7 : 7);"},
      std::pair{".global .u32 value = ((.u64)", " ? 7 : 7);"},
      std::pair{".global .u32 value = ((", " + 1) ? 7 : 7);"},
      std::pair{".global .u32 base; .global .u64 value = generic(base + ", ");"},
      std::pair{".global .u32 value = (0xff(", ") ? 7 : 7);"},
  };
  for (const std::string_view literal : {
           "18446744073709551616", "0x10000000000000000",
           "02000000000000000000000"}) {
    for (const auto& [prefix, suffix] : contexts) {
      const std::string source =
          ".version 9.3\n.target sm_80\n.address_size 64\n" +
          std::string{prefix} + std::string{literal} + suffix;
      SCOPED_TRACE(source);
      const auto resolved = resolveSource(source);
      ASSERT_FALSE(resolved.has_value());
      const SourceRange expected{
          {4, static_cast<int32_t>(std::string_view{prefix}.size() + 1)},
          {4, static_cast<int32_t>(std::string_view{prefix}.size() + literal.size() + 1)}};
      EXPECT_EQ(std::ranges::count_if(resolved.error(), [&](const auto& diagnostic) {
        return diagnostic.declaration_kind ==
                   declaration_semantics::DeclarationDiagnosticKind::InvalidIntegerLiteral &&
               diagnostic.range == expected;
      }), 1);
    }
  }
}

/** Valid 64-bit boundaries, deferred equal-branch folding, masks, and relocations survive. */
TEST(ResolvedStorageDeclarations, PreservesValidAndDeferredIntegerConstants) {
  auto resolved = resolveSource(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.global .u32 base;
.global .u64 limits[] = {9223372036854775807, 9223372036854775808,
                         18446744073709551615, 0xffffffffffffffffU};
.global .u32 folded = ((base == base) ? 7 : 7);
.global .u8 bytes[(1.0 < 2.0) ? 1 : 1];
.global .u64 pointer = generic(base) + 8;
.global .u32 masked = 0xff(0xffff);
)ptx");
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& limits = storageNamed(*resolved, "limits");
  constexpr std::array<uint64_t, 4> expected{
      0x7fffffffffffffffULL, 0x8000000000000000ULL,
      0xffffffffffffffffULL, 0xffffffffffffffffULL};
  ASSERT_EQ(limits.initializer.size(), expected.size());
  for (size_t index = 0; index < expected.size(); ++index) {
    EXPECT_EQ(std::get<StorageConstant>(limits.initializer[index].value).bits,
              expected[index]);
  }
  const auto& folded = storageNamed(*resolved, "folded");
  ASSERT_EQ(folded.initializer.size(), 1u);
  EXPECT_EQ(std::get<StorageConstant>(folded.initializer[0].value).bits, 7u);
  EXPECT_EQ(storageNamed(*resolved, "bytes").array_extents,
            (std::vector<std::optional<uint64_t>>{1u}));
  const auto& pointer = storageNamed(*resolved, "pointer");
  ASSERT_EQ(pointer.initializer.size(), 1u);
  EXPECT_EQ(std::get<StorageRelocation>(pointer.initializer[0].value).addend_bits, 8u);
  const auto& masked = storageNamed(*resolved, "masked");
  ASSERT_EQ(masked.initializer.size(), 1u);
  EXPECT_EQ(std::get<StorageConstant>(masked.initializer[0].value).bits, 255u);
}

/** Metadata owns values needed after both the syntax tree and source disappear. */
TEST(ResolvedStorageDeclarations, RetainsAddressableDeclarationsWithoutAst) {
  std::optional<ResolvedModule> resolved_module;
  {
    auto resolved = resolveSource(R"ptx(
.visible .global .u32 initialized[] = {1, 2, 3};
.const .align 8 .u16 constants[3] = {4, 5};
.shared .align 16 .u32 shared_values[2][3];
.global .v4 .u16 packed[2];
.global .u32 slots<3>;
.entry storage_kernel() {
  .local .u16 local_values[4];
  { .local .u32 initialized[2]; }
}
)ptx");
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    resolved_module = std::move(*resolved);
  }

  ASSERT_TRUE(resolved_module.has_value());
  const auto& module = *resolved_module;
  ASSERT_EQ(module.storage_declarations.size(), 7u);

  const auto& initialized = storageNamed(module, "initialized");
  EXPECT_EQ(initialized.space, StorageSpace::Global);
  EXPECT_EQ(initialized.element_type,
            StorageElementType{base::ScalarType::U32});
  EXPECT_EQ(initialized.array_extents,
            (std::vector<std::optional<uint64_t>>{3u}));
  EXPECT_EQ(initialized.byte_extent, 12u);
  EXPECT_EQ(initialized.alignment, 4u);
  EXPECT_FALSE(initialized.explicit_alignment);
  EXPECT_EQ(initialized.linkage, binding::SymbolLinkage::Visible);
  EXPECT_EQ(initialized.declaration_kind, StorageDeclarationKind::Definition);
  EXPECT_EQ(initialized.initialization, StorageInitializationKind::Explicit);
  ASSERT_EQ(initialized.initializer.size(), 3u);
  for (size_t index = 0; index < initialized.initializer.size(); ++index) {
    EXPECT_EQ(initialized.initializer[index].byte_offset, index * 4u);
    const auto* value =
        std::get_if<StorageConstant>(&initialized.initializer[index].value);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->bits, index + 1u);
  }

  const auto& constants = storageNamed(module, "constants");
  EXPECT_EQ(constants.space, StorageSpace::Constant);
  EXPECT_EQ(constants.element_type, StorageElementType{base::ScalarType::U16});
  EXPECT_EQ(constants.byte_extent, 6u);
  EXPECT_EQ(constants.alignment, 8u);
  EXPECT_EQ(constants.explicit_alignment, 8u);
  ASSERT_EQ(constants.initializer.size(), 2u);
  EXPECT_EQ(constants.initializer[0].byte_offset, 0u);
  EXPECT_EQ(constants.initializer[1].byte_offset, 2u);
  EXPECT_EQ(std::get<StorageConstant>(constants.initializer[1].value).bits, 5u);

  const auto& shared = storageNamed(module, "shared_values");
  EXPECT_EQ(shared.space, StorageSpace::Shared);
  EXPECT_EQ(shared.array_extents,
            (std::vector<std::optional<uint64_t>>{2u, 3u}));
  EXPECT_EQ(shared.byte_extent, 24u);
  EXPECT_EQ(shared.alignment, 16u);
  EXPECT_EQ(shared.explicit_alignment, 16u);
  EXPECT_EQ(shared.initialization, StorageInitializationKind::Uninitialized);

  const auto& packed = storageNamed(module, "packed");
  EXPECT_EQ(packed.element_type, StorageElementType{base::ScalarType::U16});
  EXPECT_EQ(packed.vector_width, 4u);
  EXPECT_EQ(packed.array_extents, (std::vector<std::optional<uint64_t>>{2u}));
  EXPECT_EQ(packed.byte_extent, 16u);
  EXPECT_EQ(packed.alignment, 8u);
  EXPECT_FALSE(packed.explicit_alignment);
  EXPECT_EQ(packed.initialization, StorageInitializationKind::Zero);

  const auto& slots = storageNamed(module, "slots");
  EXPECT_EQ(slots.parameterized_count, 3u);
  EXPECT_EQ(slots.byte_extent, 4u);
  EXPECT_EQ(slots.initialization, StorageInitializationKind::Zero);

  const auto& local = storageNamed(module, "local_values");
  EXPECT_EQ(local.space, StorageSpace::Local);
  EXPECT_EQ(local.array_extents, (std::vector<std::optional<uint64_t>>{4u}));
  EXPECT_EQ(local.byte_extent, 8u);
  EXPECT_EQ(local.alignment, 2u);
  EXPECT_FALSE(local.explicit_alignment);
  ASSERT_TRUE(local.owner_function);
  EXPECT_EQ(*local.owner_function, module.functions.front().symbol_id);

  const auto& shadowing_local = module.storage_declarations.back();
  EXPECT_EQ(module.symbols.symbol(shadowing_local.symbol_id).name,
            "initialized");
  EXPECT_EQ(shadowing_local.space, StorageSpace::Local);
  EXPECT_NE(shadowing_local.symbol_id, initialized.symbol_id);
  EXPECT_NE(shadowing_local.scope_id, initialized.scope_id);
}

/** External dynamic shared storage preserves its unknown extent without zeroing it. */
TEST(ResolvedStorageDeclarations, RetainsExternalDynamicSharedAsUnknown) {
  const auto resolved = resolveSource(R"ptx(
.extern .shared .u8 dynamic_shared[];
.extern .global .u32 imported[4];
)ptx");

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->storage_declarations.size(), 2u);
  const auto& dynamic = storageNamed(*resolved, "dynamic_shared");
  EXPECT_EQ(dynamic.space, StorageSpace::Shared);
  EXPECT_EQ(dynamic.declaration_kind, StorageDeclarationKind::External);
  EXPECT_EQ(dynamic.linkage, binding::SymbolLinkage::External);
  EXPECT_TRUE(dynamic.is_dynamic_shared);
  EXPECT_EQ(dynamic.array_extents.size(), 1u);
  EXPECT_FALSE(dynamic.array_extents.front());
  EXPECT_FALSE(dynamic.byte_extent);
  EXPECT_EQ(dynamic.initialization, StorageInitializationKind::External);

  const auto& imported = storageNamed(*resolved, "imported");
  EXPECT_FALSE(imported.is_dynamic_shared);
  EXPECT_EQ(imported.byte_extent, 16u);
  EXPECT_EQ(imported.initialization, StorageInitializationKind::External);
}

/** Opaque globals retain identity-only metadata and reject modeled layout forms. */
TEST(ResolvedStorageDeclarations,
     RetainsOpaqueModuleGlobalsAndRejectsUnsupportedOpaqueShapes) {
  const auto opaque = resolveSource(R"ptx(
.global .texref texture;
.global .samplerref sampler;
.global .surfref surface;
)ptx");

  ASSERT_TRUE(opaque.has_value()) << opaque.error().front().message;
  ASSERT_EQ(opaque->storage_declarations.size(), 3u);
  constexpr std::array expected_types{
      StorageOpaqueType::Texture,
      StorageOpaqueType::Sampler,
      StorageOpaqueType::Surface,
  };
  for (size_t index = 0; index < expected_types.size(); ++index) {
    const auto& declaration = opaque->storage_declarations[index];
    EXPECT_EQ(declaration.space, StorageSpace::Global);
    EXPECT_EQ(declaration.element_type,
              StorageElementType{expected_types[index]});
    EXPECT_FALSE(declaration.byte_extent);
    EXPECT_FALSE(declaration.alignment);
    EXPECT_EQ(declaration.initialization,
              StorageInitializationKind::Uninitialized);
  }

  /** Opaque declaration shape outside the normalized storage contract. */
  struct RejectedOpaqueFixture {
    std::string_view name;
    std::string_view source;
  };
  constexpr std::array rejected_fixtures{
      RejectedOpaqueFixture{
          "local",
          ".entry kernel() { .local .texref local_texture; }",
      },
      RejectedOpaqueFixture{
          "vector",
          ".global .v2 .texref vector_texture;",
      },
      RejectedOpaqueFixture{
          "array",
          ".global .texref array_texture[2];",
      },
  };
  for (const auto& fixture : rejected_fixtures) {
    SCOPED_TRACE(fixture.name);
    const auto rejected = resolveSource(std::string{fixture.source});
    EXPECT_FALSE(rejected.has_value());
    if (!rejected) {
      EXPECT_TRUE(hasDeclarationKind(
          rejected.error(), declaration_semantics::DeclarationDiagnosticKind::
                                UnsupportedStorageDeclaration));
    }
  }
}

/** Global attributes remain owned values rather than source syntax references. */
TEST(ResolvedStorageDeclarations, RetainsManagedAndUnifiedAttributes) {
  const auto resolved = resolveSource(R"ptx(
.version 8.0
.global .attribute(.managed, .unified(0x1, 2)) .u32 attributed;
)ptx");

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& attributed = storageNamed(*resolved, "attributed");
  EXPECT_TRUE(attributed.is_managed);
  EXPECT_EQ(attributed.unified_id, (std::array<uint64_t, 2>{1u, 2u}));
}

/** Compatible external declarations retain identity while preserving each range. */
TEST(ResolvedStorageDeclarations, PreservesExternalRedeclarationOccurrences) {
  const auto resolved = resolveSource(R"ptx(
.extern .global .u32 shared_symbol[2];
.extern .global .u32 shared_symbol[2];
.global .u32 independently_defined[2];
)ptx");

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->storage_declarations.size(), 3u);
  const auto& first_external = resolved->storage_declarations[0];
  const auto& second_external = resolved->storage_declarations[1];
  const auto& definition = resolved->storage_declarations[2];
  EXPECT_EQ(first_external.symbol_id, second_external.symbol_id);
  EXPECT_NE(first_external.range, second_external.range);
  EXPECT_EQ(first_external.declaration_kind, StorageDeclarationKind::External);
  EXPECT_EQ(second_external.declaration_kind, StorageDeclarationKind::External);
  EXPECT_EQ(first_external.linkage, binding::SymbolLinkage::External);
  EXPECT_EQ(second_external.linkage, binding::SymbolLinkage::External);
  EXPECT_NE(definition.symbol_id, first_external.symbol_id);
  EXPECT_EQ(definition.declaration_kind, StorageDeclarationKind::Definition);
  EXPECT_EQ(definition.linkage, binding::SymbolLinkage::None);
}

/** Flattened initializer entries retain byte offsets and distinguish implicit fill. */
TEST(ResolvedStorageDeclarations, RetainsSparseInitializersAndTypedConstants) {
  const auto resolved = resolveSource(R"ptx(
.global .u32 sparse[4] = {7, 9};
.const .f32 decimal[2] = {1.0, 0f40000000};
.const .f32 parenthesized[1] = {(1.0)};
)ptx");

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& sparse = storageNamed(*resolved, "sparse");
  EXPECT_EQ(sparse.initialization, StorageInitializationKind::Explicit);
  EXPECT_EQ(sparse.byte_extent, 16u);
  ASSERT_EQ(sparse.initializer.size(), 2u);
  EXPECT_EQ(sparse.initializer[0].byte_offset, 0u);
  EXPECT_EQ(sparse.initializer[1].byte_offset, 4u);
  EXPECT_EQ(std::get<StorageConstant>(sparse.initializer[0].value).bits, 7u);
  EXPECT_EQ(std::get<StorageConstant>(sparse.initializer[1].value).bits, 9u);

  const auto& decimal = storageNamed(*resolved, "decimal");
  EXPECT_EQ(decimal.element_type, StorageElementType{base::ScalarType::F32});
  ASSERT_EQ(decimal.initializer.size(), 2u);
  EXPECT_EQ(std::get<StorageConstant>(decimal.initializer[0].value).bits,
            0x3f800000u);
  EXPECT_EQ(std::get<StorageConstant>(decimal.initializer[1].value).bits,
            0x40000000u);

  const auto& parenthesized = storageNamed(*resolved, "parenthesized");
  ASSERT_EQ(parenthesized.initializer.size(), 1u);
  EXPECT_EQ(
      std::get<StorageConstant>(parenthesized.initializer.front().value).bits,
      0x3f800000u);
}

/** An empty explicit list and absent initialization retain different contracts. */
TEST(ResolvedStorageDeclarations, DistinguishesExplicitEmptyAndImplicitZero) {
  const auto resolved = resolveSource(R"ptx(
.global .u32 implicit_zero[2];
.const .u16 constant_zero;
.global .u32 explicit_empty[2] = {};
.shared .u32 shared_uninitialized;
)ptx");

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  EXPECT_EQ(storageNamed(*resolved, "implicit_zero").initialization,
            StorageInitializationKind::Zero);
  EXPECT_EQ(storageNamed(*resolved, "constant_zero").initialization,
            StorageInitializationKind::Zero);
  const auto& explicit_empty = storageNamed(*resolved, "explicit_empty");
  EXPECT_EQ(explicit_empty.initialization, StorageInitializationKind::Explicit);
  EXPECT_TRUE(explicit_empty.initializer.empty());
  EXPECT_EQ(storageNamed(*resolved, "shared_uninitialized").initialization,
            StorageInitializationKind::Uninitialized);
}

/** Symbol-address initialization preserves relocation identity and generic addends. */
TEST(ResolvedStorageDeclarations, RetainsGenericSymbolRelocation) {
  const auto resolved = resolveSource(R"ptx(
.version 7.3
.global .u32 base[4];
.global .u64 addresses[3] = {base, generic(base) + 8, 0xff(base + 1)};
)ptx");

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& base = storageNamed(*resolved, "base");
  const auto& addresses = storageNamed(*resolved, "addresses");
  ASSERT_EQ(addresses.initializer.size(), 3u);
  const auto* direct =
      std::get_if<StorageRelocation>(&addresses.initializer[0].value);
  const auto* generic =
      std::get_if<StorageRelocation>(&addresses.initializer[1].value);
  const auto* masked =
      std::get_if<StorageRelocation>(&addresses.initializer[2].value);
  ASSERT_NE(direct, nullptr);
  ASSERT_NE(generic, nullptr);
  ASSERT_NE(masked, nullptr);
  EXPECT_EQ(direct->symbol_id, base.symbol_id);
  EXPECT_EQ(direct->address_kind, StorageAddressKind::StateSpace);
  EXPECT_EQ(direct->addend_bits, 0u);
  EXPECT_FALSE(direct->parameterized_index);
  EXPECT_FALSE(direct->byte_mask);
  EXPECT_EQ(generic->symbol_id, base.symbol_id);
  EXPECT_EQ(generic->address_kind, StorageAddressKind::Generic);
  EXPECT_EQ(generic->addend_bits, 8u);
  EXPECT_FALSE(generic->byte_mask);
  EXPECT_EQ(addresses.initializer[1].byte_offset, 8u);
  EXPECT_EQ(masked->symbol_id, base.symbol_id);
  EXPECT_EQ(masked->address_kind, StorageAddressKind::StateSpace);
  EXPECT_EQ(masked->addend_bits, 1u);
  EXPECT_EQ(masked->byte_mask, 0xffu);
  EXPECT_EQ(addresses.initializer[2].byte_offset, 16u);
}

/** Version gates preserve state-space and byte-mask relocation interpretation. */
TEST(ResolvedStorageDeclarations, AppliesRelocationAndMaskVersionBoundaries) {
  const auto pre31 = resolveSource(R"ptx(
.version 3.0
.global .u32 target;
.global .u64 address = target;
)ptx");
  const auto at31 = resolveSource(R"ptx(
.version 3.1
.global .u32 target;
.global .u64 address = target;
)ptx");
  ASSERT_TRUE(pre31.has_value()) << pre31.error().front().message;
  ASSERT_TRUE(at31.has_value()) << at31.error().front().message;
  const auto* pre31_relocation = std::get_if<StorageRelocation>(
      &storageNamed(*pre31, "address").initializer.front().value);
  const auto* at31_relocation = std::get_if<StorageRelocation>(
      &storageNamed(*at31, "address").initializer.front().value);
  ASSERT_NE(pre31_relocation, nullptr);
  ASSERT_NE(at31_relocation, nullptr);
  EXPECT_EQ(pre31_relocation->address_kind, StorageAddressKind::Generic);
  EXPECT_EQ(at31_relocation->address_kind, StorageAddressKind::StateSpace);

  /** Source fragment expected to fail initializer normalization. */
  struct RejectedMaskFixture {
    std::string_view name;
    std::string_view source;
  };
  constexpr std::array rejected_fixtures{
      RejectedMaskFixture{
          "kernel address before PTX 3.1",
          ".version 3.0\n.entry kernel() {}\n"
          ".global .u64 address = kernel;",
      },
      RejectedMaskFixture{
          "address before PTX 7.1",
          ".version 7.0\n.global .u32 target;\n"
          ".global .u64 address = 0xff(target);",
      },
      RejectedMaskFixture{
          "integer before PTX 7.3",
          ".version 7.2\n.global .u32 masked = 0xff00(0x1234);",
      },
      RejectedMaskFixture{
          "non-byte selector",
          ".version 7.3\n.global .u32 target;\n"
          ".global .u64 address = 0xf(target);",
      },
      RejectedMaskFixture{
          "post-mask addend",
          ".version 7.3\n.global .u32 target;\n"
          ".global .u64 address = 0xff(target) + 4;",
      },
  };
  for (const auto& fixture : rejected_fixtures) {
    SCOPED_TRACE(fixture.name);
    const auto rejected = resolveSource(std::string{fixture.source});
    EXPECT_FALSE(rejected.has_value());
    if (!rejected) {
      EXPECT_TRUE(hasDeclarationKind(
          rejected.error(), declaration_semantics::DeclarationDiagnosticKind::
                                UnsupportedStorageInitializer));
    }
  }

  const auto accepted_integer_mask = resolveSource(R"ptx(
.version 7.3
.global .u32 masked = 0xff00(0x1234);
)ptx");
  ASSERT_TRUE(accepted_integer_mask.has_value())
      << accepted_integer_mask.error().front().message;
  const auto& masked = storageNamed(*accepted_integer_mask, "masked");
  ASSERT_EQ(masked.initializer.size(), 1u);
  EXPECT_EQ(std::get<StorageConstant>(masked.initializer.front().value).bits,
            0x12u);

  const auto kernel_address = resolveSource(
      ".version 3.1\n.entry kernel() {}\n"
      ".global .u64 address = kernel;");
  ASSERT_TRUE(kernel_address.has_value()) << kernel_address.error().front().message;
  const auto& kernel_pointer = storageNamed(*kernel_address, "address");
  ASSERT_EQ(kernel_pointer.initializer.size(), 1u);
  EXPECT_EQ(std::get<StorageRelocation>(kernel_pointer.initializer.front().value)
                .address_kind,
            StorageAddressKind::Function);

  const auto device_address = resolveSource(
      ".version 3.0\n.func helper() {}\n"
      ".global .u64 address = helper;");
  ASSERT_TRUE(device_address.has_value()) << device_address.error().front().message;
}

/** Function relocations retain function identity and reject address-space transforms. */
TEST(ResolvedStorageDeclarations, RetainsDirectAndMaskedFunctionRelocations) {
  const auto resolved = resolveSource(R"ptx(
.version 9.3
.func helper() { ret; }
.global .u64 direct = helper;
.global .u8 byte = 0xff(helper);
)ptx");

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto helper =
      resolved->symbols.lookup(resolved->symbols.moduleScope(), "helper");
  ASSERT_TRUE(helper);
  const auto& direct = storageNamed(*resolved, "direct");
  const auto& byte = storageNamed(*resolved, "byte");
  ASSERT_EQ(direct.initializer.size(), 1u);
  ASSERT_EQ(byte.initializer.size(), 1u);
  const auto* direct_relocation =
      std::get_if<StorageRelocation>(&direct.initializer.front().value);
  const auto* byte_relocation =
      std::get_if<StorageRelocation>(&byte.initializer.front().value);
  ASSERT_NE(direct_relocation, nullptr);
  ASSERT_NE(byte_relocation, nullptr);
  EXPECT_EQ(direct_relocation->symbol_id, helper->symbol);
  EXPECT_EQ(direct_relocation->address_kind, StorageAddressKind::Function);
  EXPECT_FALSE(direct_relocation->byte_mask);
  EXPECT_EQ(byte_relocation->symbol_id, helper->symbol);
  EXPECT_EQ(byte_relocation->address_kind, StorageAddressKind::Function);
  EXPECT_EQ(byte_relocation->byte_mask, 0xffu);

  constexpr std::array rejected_sources{
      R"ptx(.version 9.3
.func helper() { ret; }
.global .u64 direct = helper + 4;
)ptx",
      R"ptx(.version 9.3
.func helper() { ret; }
.global .u64 direct = generic(helper);
)ptx",
  };
  for (const auto source : rejected_sources) {
    const auto rejected = resolveSource(source);
    EXPECT_FALSE(rejected.has_value());
    if (!rejected) {
      EXPECT_TRUE(hasDeclarationKind(
          rejected.error(), declaration_semantics::DeclarationDiagnosticKind::
                                UnsupportedStorageInitializer));
    }
  }
}

/** Relocations retain the initializer's lexical binding rather than its spelling. */
TEST(ResolvedStorageDeclarations, RetainsScopedInitializerSymbolIdentity) {
  const auto scoped = resolveSource(R"ptx(
.global .u32 value;
.entry kernel() {
  .global .u32 value;
  .global .u64 pointer = value;
  {
    .global .u32 block_value;
    .global .u64 block_pointer = block_value;
  }
  ret;
}
)ptx");

  ASSERT_TRUE(scoped.has_value()) << scoped.error().front().message;
  ASSERT_EQ(scoped->storage_declarations.size(), 5u);
  const auto& module_value = scoped->storage_declarations[0];
  const auto& scoped_value = scoped->storage_declarations[1];
  const auto& pointer = scoped->storage_declarations[2];
  const auto& block_value = scoped->storage_declarations[3];
  const auto& block_pointer = scoped->storage_declarations[4];
  EXPECT_EQ(scoped->symbols.symbol(module_value.symbol_id).name, "value");
  EXPECT_EQ(scoped->symbols.symbol(scoped_value.symbol_id).name, "value");
  EXPECT_NE(module_value.symbol_id, scoped_value.symbol_id);
  EXPECT_NE(module_value.scope_id, scoped_value.scope_id);
  ASSERT_EQ(pointer.initializer.size(), 1u);
  const auto* relocation =
      std::get_if<StorageRelocation>(&pointer.initializer.front().value);
  ASSERT_NE(relocation, nullptr);
  EXPECT_EQ(relocation->symbol_id, scoped_value.symbol_id);
  ASSERT_EQ(block_pointer.initializer.size(), 1u);
  const auto* block_relocation =
      std::get_if<StorageRelocation>(&block_pointer.initializer.front().value);
  ASSERT_NE(block_relocation, nullptr);
  EXPECT_EQ(block_relocation->symbol_id, block_value.symbol_id);

  constexpr std::array rejected_spaces{
      ".local",
      ".shared",
  };
  for (const auto space : rejected_spaces) {
    SCOPED_TRACE(space);
    const std::string source =
        ".global .u32 value;\n.entry kernel() {\n" + std::string{space} +
        " .u32 value;\n.global .u64 pointer = value;\nret;\n}\n";
    const auto rejected = resolveSource(source);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_TRUE(
        hasDeclarationKind(rejected.error(),
                           declaration_semantics::DeclarationDiagnosticKind::
                               InvalidInitializerExpression) ||
        hasDeclarationKind(rejected.error(),
                           declaration_semantics::DeclarationDiagnosticKind::
                               UnsupportedStorageInitializer));
  }
}

/** Scalar width and constant ownership remain explicit at integer boundaries. */
TEST(ResolvedStorageDeclarations, RetainsIntegerBitsAndImplicitWideZero) {
  const auto resolved = resolveSource(R"ptx(
.global .u32 negative = -1;
.global .b128 wide;
)ptx");

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& negative = storageNamed(*resolved, "negative");
  ASSERT_EQ(negative.initializer.size(), 1u);
  EXPECT_EQ(std::get<StorageConstant>(negative.initializer.front().value).bits,
            0xffffffffu);
  EXPECT_EQ(std::get<StorageConstant>(negative.initializer.front().value).high_bits,
            0u);
  const auto& wide = storageNamed(*resolved, "wide");
  EXPECT_EQ(wide.element_type, StorageElementType{base::ScalarType::B128});
  EXPECT_EQ(wide.byte_extent, 16u);
  EXPECT_EQ(wide.alignment, 16u);
  EXPECT_EQ(wide.initialization, StorageInitializationKind::Zero);

  const auto empty_wide = resolveSource(".global .b128 wide_empty = {};");
  EXPECT_FALSE(empty_wide.has_value());
}

/** Widen only the typed 64-bit result, preserving signedness and wraparound. */
TEST(ResolvedStorageDeclarations, WidensB128IntegerResults) {
  constexpr uint64_t all_bits = std::numeric_limits<uint64_t>::max();
  /** Expected low/high words for a scalar initializer expression. */
  struct Fixture {
    std::string_view expression;
    uint64_t low;
    uint64_t high;
  };
  constexpr std::array fixtures{
      Fixture{"1", 1, 0},
      Fixture{"0", 0, 0},
      Fixture{"-1", all_bits, all_bits},
      Fixture{"-1U", all_bits, 0},
      Fixture{"~0", all_bits, 0},
      Fixture{"0x8000000000000000", uint64_t{1} << 63, 0},
      Fixture{"0xffffffffffffffff", all_bits, 0},
      Fixture{"(.s64)0x8000000000000000", uint64_t{1} << 63, all_bits},
      Fixture{"(.u64)-1", all_bits, 0},
      Fixture{"-2 + 1", all_bits, all_bits},
      Fixture{"1 ? -1 : 0U", all_bits, 0},
      Fixture{"1 << 63", uint64_t{1} << 63, all_bits},
      Fixture{"0xffffffffffffffffU + 1U", 0, 0},
      Fixture{"0xff00(0x1234)", 0x12, 0},
  };
  for (const auto& fixture : fixtures) {
    SCOPED_TRACE(fixture.expression);
    const auto resolved = resolveSource(
        ".version 9.3\n.global .b128 wide = " +
        std::string{fixture.expression} + ";");
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    const auto& wide = storageNamed(*resolved, "wide");
    EXPECT_EQ(wide.byte_extent, 16u);
    EXPECT_EQ(wide.alignment, 16u);
    EXPECT_EQ(wide.initialization, StorageInitializationKind::Explicit);
    ASSERT_EQ(wide.initializer.size(), 1u);
    EXPECT_EQ(wide.initializer[0].byte_offset, 0u);
    const auto* value = std::get_if<StorageConstant>(&wide.initializer[0].value);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->bits, fixture.low);
    EXPECT_EQ(value->high_bits, fixture.high);
  }
}

/** Wide aggregates retain sparse entries and 16-byte element strides. */
TEST(ResolvedStorageDeclarations, RetainsSparseB128Aggregates) {
  const auto resolved = resolveSource(R"ptx(
.global .b128 values[][3] = {{1, -1}, {-1U}};
.const .b128 empty[2] = {};
)ptx");
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& values = storageNamed(*resolved, "values");
  EXPECT_EQ(values.array_extents,
            (std::vector<std::optional<uint64_t>>{2, 3}));
  EXPECT_EQ(values.byte_extent, 96u);
  ASSERT_EQ(values.initializer.size(), 3u);
  EXPECT_EQ(values.initializer[0].byte_offset, 0u);
  EXPECT_EQ(values.initializer[1].byte_offset, 16u);
  EXPECT_EQ(values.initializer[2].byte_offset, 48u);
  EXPECT_EQ(std::get<StorageConstant>(values.initializer[1].value).high_bits,
            std::numeric_limits<uint64_t>::max());
  EXPECT_EQ(std::get<StorageConstant>(values.initializer[2].value).high_bits, 0u);
  const auto& empty = storageNamed(*resolved, "empty");
  EXPECT_EQ(empty.byte_extent, 32u);
  EXPECT_EQ(empty.initialization, StorageInitializationKind::Explicit);
  EXPECT_TRUE(empty.initializer.empty());
}

/** A wider destination does not permit wider literals, addresses or float values. */
TEST(ResolvedStorageDeclarations, RejectsInvalidB128Initializers) {
  constexpr std::array sources{
      ".global .b128 wide = 18446744073709551616;",
      ".global .b128 wide = 0x10000000000000000;",
      ".global .b128 wide = 1.0;",
      ".global .u32 target; .global .b128 wide = target;",
      ".global .b128 wide[1] = {1, 2};",
  };
  for (const auto source : sources) {
    SCOPED_TRACE(source);
    const auto rejected = resolveSource(source);
    ASSERT_FALSE(rejected.has_value());
    ASSERT_FALSE(rejected.error().empty());
    EXPECT_TRUE(rejected.error().front().declaration_kind.has_value());
  }
}

/** Storage resolution reports overflow and invalid alignment with declaration kinds. */
TEST(ResolvedStorageDeclarations, ReportsStructuredStorageDeclarationErrors) {
  const auto overflow =
      resolveSource(".global .u64 too_large[18446744073709551615U];");
  ASSERT_FALSE(overflow.has_value());
  ASSERT_FALSE(overflow.error().empty());
  EXPECT_EQ(
      overflow.error().front().declaration_kind,
      declaration_semantics::DeclarationDiagnosticKind::StorageExtentOverflow);

  const auto invalid_alignment =
      resolveSource(".global .align 3 .u32 badly_aligned;");
  ASSERT_FALSE(invalid_alignment.has_value());
  ASSERT_FALSE(invalid_alignment.error().empty());
  EXPECT_EQ(invalid_alignment.error().front().declaration_kind,
            declaration_semantics::DeclarationDiagnosticKind::InvalidAlignment);
  EXPECT_FALSE(invalid_alignment.error().front().previous_range);

  const auto invalid_extent = resolveSource(".global .u32 no_elements[0];");
  ASSERT_FALSE(invalid_extent.has_value());
  ASSERT_FALSE(invalid_extent.error().empty());
  EXPECT_EQ(
      invalid_extent.error().front().declaration_kind,
      declaration_semantics::DeclarationDiagnosticKind::InvalidArrayDimension);

  const auto trailing_overflow = resolveSource(
      ".extern .global .u64 trailing_overflow[][18446744073709551615U];");
  ASSERT_FALSE(trailing_overflow.has_value());
  ASSERT_FALSE(trailing_overflow.error().empty());
  EXPECT_EQ(
      trailing_overflow.error().front().declaration_kind,
      declaration_semantics::DeclarationDiagnosticKind::StorageExtentOverflow);
}

/** Cross-declaration failures retain their semantic category and related range. */
TEST(ResolvedStorageDeclarations, RetainsRedeclarationDiagnosticContext) {
  const auto incompatible = resolveSource(R"ptx(
.extern .global .u32 conflicting[2];
.extern .global .u64 conflicting[2];
)ptx");

  ASSERT_FALSE(incompatible.has_value());
  ASSERT_FALSE(incompatible.error().empty());
  const auto& diagnostic = incompatible.error().front();
  EXPECT_EQ(diagnostic.declaration_kind,
            declaration_semantics::DeclarationDiagnosticKind::
                IncompatibleRedeclaration);
  ASSERT_TRUE(diagnostic.previous_range);
  EXPECT_LT(diagnostic.previous_range->start.line, diagnostic.range.start.line);
}

/** An external declaration cannot be resolved by a static definition. */
TEST(ResolvedStorageDeclarations, RejectsExternalStaticStorageResolution) {
  const auto incompatible = resolveSource(R"ptx(
.extern .global .u32 conflicting[2];
.global .u32 conflicting[2];
)ptx");

  ASSERT_FALSE(incompatible.has_value());
  ASSERT_FALSE(incompatible.error().empty());
  EXPECT_EQ(incompatible.error().front().declaration_kind,
            declaration_semantics::DeclarationDiagnosticKind::
                IncompatibleRedeclaration);
  EXPECT_TRUE(incompatible.error().front().previous_range);
}

/** Unsupported storage constructs diagnose instead of acquiring fabricated metadata. */
TEST(ResolvedStorageDeclarations,
     RejectsUnsupportedStorageWithoutZeroMetadata) {
  const auto unsupported = resolveSource(R"ptx(
.global .bf16 bfloat_value;
.global .tf32 tensor_value;
.global .e4m3 fp8_value;
.global .u8x4 packed_value;
)ptx");

  ASSERT_FALSE(unsupported.has_value());
  const size_t unsupported_count =
      std::ranges::count_if(unsupported.error(), [](const auto& diagnostic) {
        return diagnostic.declaration_kind ==
               declaration_semantics::DeclarationDiagnosticKind::
                   UnsupportedStorageDeclaration;
      });
  EXPECT_EQ(unsupported_count, 4u);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
