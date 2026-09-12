#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve owned input, destroying both source and AST before returning metadata. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> resolveSource(
    std::string source) {
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  EXPECT_TRUE(ast.has_value());
  EXPECT_TRUE(ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(
        ModuleResolveDiagnostics{{.message = ast.diagnostics.front().message}});
  return resolveModule(*ast);
}

/** Entry declaration metadata remains usable after its source and AST are gone. */
TEST(ResolvedParameterDeclarations, RetainsSingleEntryInputWithoutAst) {
  const auto resolved =
      resolveSource(".entry kernel(.param .u32 count) { ret; }");

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& function = resolved->functions.front();
  const auto& declarations = function.parameter_declarations;
  ASSERT_EQ(declarations.size(), 1u);
  const auto& declaration = declarations.front();
  EXPECT_EQ(declaration.role, ParameterDeclarationRole::EntryInput);
  EXPECT_EQ(declaration.scalar_type, base::ScalarType::U32);
  EXPECT_EQ(declaration.alignment, 4u);
  EXPECT_FALSE(declaration.explicit_alignment);
  EXPECT_EQ(declaration.vector_width, 1u);
  EXPECT_TRUE(declaration.array_extents.empty());
  EXPECT_EQ(declaration.byte_extent, 4u);
  EXPECT_FALSE(declaration.pointer);
  const auto& symbol = resolved->symbols.symbol(declaration.symbol_id);
  EXPECT_EQ(symbol.name, "count");
  EXPECT_EQ(symbol.kind, binding::SymbolKind::InputParameter);
  EXPECT_EQ(symbol.state_space, syntax_ast::AstStateSpace::Parameter);
  EXPECT_EQ(declaration.scope_id, symbol.scope);
  EXPECT_EQ(resolved->symbols.scope(symbol.scope).owner,
            resolved->functions.front().symbol_id);
}

/** Representative GEMM inputs retain typed source order and alignments. */
TEST(ResolvedParameterDeclarations, RetainsGemmEntryInputsInSourceOrder) {
  const auto resolved = resolveSource(R"ptx(
.version 8.0
.target sm_80
.address_size 64
.visible .entry gemm(
    .param .align 16 .u64 .ptr .global .align 32 A,
    .param .u64 .ptr .global .align 16 B,
    .param .u64 .ptr .global .align 16 C,
    .param .u32 M,
    .param .u32 N,
    .param .u32 K,
    .param .u32 lda,
    .param .u32 ldb,
    .param .u32 ldc) { ret; }
)ptx");

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& function = resolved->functions.front();
  const auto& declarations = function.parameter_declarations;
  constexpr std::array<std::string_view, 9> names{"A", "B",   "C",   "M",  "N",
                                                  "K", "lda", "ldb", "ldc"};
  ASSERT_EQ(declarations.size(), names.size());
  for (size_t index = 0; index < names.size(); ++index) {
    SCOPED_TRACE(names[index]);
    const auto& declaration = declarations[index];
    const auto& symbol = resolved->symbols.symbol(declaration.symbol_id);
    EXPECT_EQ(symbol.name, names[index]);
    EXPECT_EQ(symbol.kind, binding::SymbolKind::InputParameter);
    EXPECT_EQ(declaration.scope_id, symbol.scope);
    EXPECT_EQ(resolved->symbols.scope(symbol.scope).owner, function.symbol_id);
    EXPECT_EQ(declaration.role, ParameterDeclarationRole::EntryInput);
    EXPECT_EQ(declaration.scalar_type,
              index < 3 ? base::ScalarType::U64 : base::ScalarType::U32);
    EXPECT_EQ(declaration.alignment, index == 0 ? 16u : index < 3 ? 8u : 4u);
    EXPECT_EQ(declaration.byte_extent, index < 3 ? 8u : 4u);
    EXPECT_TRUE(declaration.array_extents.empty());
    if (index < 3) {
      ASSERT_TRUE(declaration.pointer);
      EXPECT_EQ(declaration.pointer->pointed_state_space,
                call_argument_compatibility::PointedStateSpace::Global);
      EXPECT_EQ(declaration.pointer->pointed_alignment, index == 0 ? 32u : 16u);
    } else {
      EXPECT_FALSE(declaration.pointer);
    }
  }
}

/** Pointer defaults and all concrete target spaces survive contract lowering. */
TEST(ResolvedParameterDeclarations,
     RetainsEntryPointerTargetSpacesAndDefaults) {
  const auto resolved = resolveSource(R"ptx(
.entry pointers(
    .param .u64 raw_address,
    .param .u64 .ptr generic_address,
    .param .u64 .ptr .local local_address,
    .param .u64 .ptr .shared .align 8 shared_address,
    .param .u64 .ptr .const .align 16 constant_address) { ret; }
)ptx");

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& function = resolved->functions.front();
  const auto& declarations = function.parameter_declarations;
  ASSERT_EQ(declarations.size(), 5u);
  EXPECT_EQ(declarations[0].role, ParameterDeclarationRole::EntryInput);
  EXPECT_EQ(declarations[0].scalar_type, base::ScalarType::U64);
  EXPECT_EQ(declarations[0].alignment, 8u);
  EXPECT_TRUE(declarations[0].array_extents.empty());
  EXPECT_EQ(declarations[0].byte_extent, 8u);
  EXPECT_FALSE(declarations[0].pointer);
  ASSERT_TRUE(declarations[1].pointer);
  EXPECT_FALSE(declarations[1].pointer->pointed_state_space);
  EXPECT_EQ(declarations[1].pointer->pointed_alignment, 4u);
  using call_argument_compatibility::PointedStateSpace;
  constexpr std::array spaces{PointedStateSpace::Local,
                              PointedStateSpace::Shared,
                              PointedStateSpace::Constant};
  for (size_t index = 0; index < spaces.size(); ++index) {
    ASSERT_TRUE(declarations[index + 2].pointer);
    EXPECT_EQ(declarations[index + 2].pointer->pointed_state_space,
              spaces[index]);
    EXPECT_EQ(declarations[index + 2].pointer->pointed_alignment, 4u << index);
    EXPECT_EQ(declarations[index + 2].alignment, 8u);
  }
  for (size_t index = 1; index < declarations.size(); ++index) {
    EXPECT_EQ(declarations[index].role, ParameterDeclarationRole::EntryInput);
    EXPECT_EQ(declarations[index].scalar_type, base::ScalarType::U64);
    EXPECT_EQ(declarations[index].alignment, 8u);
    EXPECT_TRUE(declarations[index].array_extents.empty());
    EXPECT_EQ(declarations[index].byte_extent, 8u);
    ASSERT_TRUE(declarations[index].pointer);
  }
}

/** Array extents are element counts and byte extents use the element type size. */
TEST(ResolvedParameterDeclarations, RetainsEntryArrayShapesAndByteExtents) {
  const auto resolved = resolveSource(R"ptx(
.entry arrays(.param .align 16 .b8 bytes[2 * 8],
              .param .u32 words[2 + 1],
              .param .align 8 .b8 padded[4],
              .param .b8 scalar) { ret; }
)ptx");

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& declarations = resolved->functions.front().parameter_declarations;
  ASSERT_EQ(declarations.size(), 4u);
  for (const auto& declaration : declarations)
    EXPECT_EQ(declaration.role, ParameterDeclarationRole::EntryInput);
  EXPECT_EQ(declarations[0].scalar_type, base::ScalarType::B8);
  EXPECT_EQ(declarations[0].alignment, 16u);
  EXPECT_EQ(declarations[0].array_extents,
            (std::vector<std::optional<uint64_t>>{16u}));
  EXPECT_EQ(declarations[0].byte_extent, 16u);
  EXPECT_EQ(declarations[1].scalar_type, base::ScalarType::U32);
  EXPECT_EQ(declarations[1].alignment, 4u);
  EXPECT_EQ(declarations[1].array_extents,
            (std::vector<std::optional<uint64_t>>{3u}));
  EXPECT_EQ(declarations[1].byte_extent, 12u);
  EXPECT_EQ(declarations[2].scalar_type, base::ScalarType::B8);
  EXPECT_EQ(declarations[2].alignment, 8u);
  EXPECT_EQ(declarations[2].array_extents,
            (std::vector<std::optional<uint64_t>>{4u}));
  EXPECT_EQ(declarations[2].byte_extent, 4u);
  EXPECT_EQ(declarations[3].scalar_type, base::ScalarType::B8);
  EXPECT_EQ(declarations[3].alignment, 1u);
  EXPECT_TRUE(declarations[3].array_extents.empty());
  EXPECT_EQ(declarations[3].byte_extent, 1u);
}

/** All parameter roles retain typed, scoped metadata after AST destruction. */
TEST(ResolvedParameterDeclarations, RetainsAllRolesAndDeclarationProperties) {
  const auto resolved = resolveSource(R"ptx(
.version 8.0
.target sm_80
.entry kernel(.param .align 8 .u16 kernel_input[2]) {
  .param .align 16 .b8 staging[4];
  { .param .u32 staging; }
  ret;
}
.func (.param .align 8 .b16 result[2]) device(
    .param .u32 input, .param .align 8 .b8 bytes[]) {
  .param .u64 staging;
  { .param .align 4 .u16 staging[2][3]; }
  ret;
}
)ptx");

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  using base::ScalarType;
  using Role = ParameterDeclarationRole;
  const auto& entry = resolved->functions[0];
  const auto& device = resolved->functions[1];
  ASSERT_EQ(entry.parameter_declarations.size(), 3u);
  ASSERT_EQ(device.parameter_declarations.size(), 5u);

  const auto& entry_input = entry.parameter_declarations[0];
  EXPECT_EQ(entry_input.role, Role::EntryInput);
  EXPECT_EQ(entry_input.scalar_type, ScalarType::U16);
  EXPECT_EQ(entry_input.alignment, 8u);
  EXPECT_TRUE(entry_input.explicit_alignment);
  EXPECT_EQ(entry_input.vector_width, 1u);
  EXPECT_EQ(entry_input.array_extents,
            (std::vector<std::optional<uint64_t>>{2u}));
  EXPECT_EQ(entry_input.byte_extent, 4u);
  EXPECT_FALSE(entry_input.pointer);

  const auto& entry_local = entry.parameter_declarations[1];
  const auto& entry_nested = entry.parameter_declarations[2];
  EXPECT_EQ(entry_local.role, Role::BodyLocal);
  EXPECT_EQ(entry_local.scalar_type, ScalarType::B8);
  EXPECT_EQ(entry_local.alignment, 16u);
  EXPECT_TRUE(entry_local.explicit_alignment);
  EXPECT_EQ(entry_local.array_extents,
            (std::vector<std::optional<uint64_t>>{4u}));
  EXPECT_EQ(entry_local.byte_extent, 4u);
  EXPECT_EQ(entry_nested.role, Role::BodyLocal);
  EXPECT_EQ(entry_nested.scalar_type, ScalarType::U32);
  EXPECT_EQ(entry_nested.alignment, 4u);
  EXPECT_FALSE(entry_nested.explicit_alignment);
  EXPECT_TRUE(entry_nested.array_extents.empty());
  EXPECT_EQ(entry_nested.byte_extent, 4u);
  const auto entry_input_count = std::count_if(
      entry.parameter_declarations.begin(), entry.parameter_declarations.end(),
      [](const ResolvedParameterDeclaration& declaration) {
        return declaration.role == Role::EntryInput;
      });
  EXPECT_EQ(entry_input_count, 1);

  const auto& result = device.parameter_declarations[0];
  const auto& input = device.parameter_declarations[1];
  const auto& bytes = device.parameter_declarations[2];
  const auto& staging = device.parameter_declarations[3];
  const auto& nested_staging = device.parameter_declarations[4];
  EXPECT_EQ(result.role, Role::DeviceReturn);
  EXPECT_EQ(result.scalar_type, ScalarType::B16);
  EXPECT_EQ(result.alignment, 8u);
  EXPECT_TRUE(result.explicit_alignment);
  EXPECT_EQ(result.array_extents, (std::vector<std::optional<uint64_t>>{2u}));
  EXPECT_EQ(result.byte_extent, 4u);
  EXPECT_EQ(input.role, Role::DeviceInput);
  EXPECT_EQ(input.scalar_type, ScalarType::U32);
  EXPECT_EQ(input.alignment, 4u);
  EXPECT_FALSE(input.explicit_alignment);
  EXPECT_TRUE(input.array_extents.empty());
  EXPECT_EQ(input.byte_extent, 4u);
  EXPECT_EQ(bytes.role, Role::DeviceInput);
  EXPECT_EQ(bytes.scalar_type, ScalarType::B8);
  EXPECT_EQ(bytes.alignment, 8u);
  EXPECT_TRUE(bytes.explicit_alignment);
  EXPECT_EQ(bytes.array_extents,
            (std::vector<std::optional<uint64_t>>{std::nullopt}));
  EXPECT_FALSE(bytes.byte_extent);
  EXPECT_EQ(staging.role, Role::BodyLocal);
  EXPECT_EQ(staging.scalar_type, ScalarType::U64);
  EXPECT_EQ(staging.alignment, 8u);
  EXPECT_FALSE(staging.explicit_alignment);
  EXPECT_TRUE(staging.array_extents.empty());
  EXPECT_EQ(staging.byte_extent, 8u);
  EXPECT_FALSE(staging.pointer);
  EXPECT_EQ(nested_staging.role, Role::BodyLocal);
  EXPECT_EQ(nested_staging.scalar_type, ScalarType::U16);
  EXPECT_EQ(nested_staging.alignment, 4u);
  EXPECT_TRUE(nested_staging.explicit_alignment);
  EXPECT_EQ(nested_staging.array_extents,
            (std::vector<std::optional<uint64_t>>{2u, 3u}));
  EXPECT_EQ(nested_staging.byte_extent, 12u);

  const auto& symbols = resolved->symbols;
  const auto& outer_symbol = symbols.symbol(staging.symbol_id);
  const auto& nested_symbol = symbols.symbol(nested_staging.symbol_id);
  EXPECT_EQ(outer_symbol.name, "staging");
  EXPECT_EQ(nested_symbol.name, "staging");
  EXPECT_NE(staging.symbol_id, nested_staging.symbol_id);
  EXPECT_NE(staging.scope_id, nested_staging.scope_id);
  EXPECT_EQ(outer_symbol.scope, staging.scope_id);
  EXPECT_EQ(nested_symbol.scope, nested_staging.scope_id);
}

/** Every supported parameter element has a validated enum and natural byte size. */
TEST(ResolvedParameterDeclarations, ClassifiesEverySupportedElementType) {
  const std::pair<std::string_view, base::ScalarType> types[] = {
      {".s8", base::ScalarType::S8},     {".s16", base::ScalarType::S16},
      {".s32", base::ScalarType::S32},   {".s64", base::ScalarType::S64},
      {".u8", base::ScalarType::U8},     {".u16", base::ScalarType::U16},
      {".u32", base::ScalarType::U32},   {".u64", base::ScalarType::U64},
      {".b8", base::ScalarType::B8},     {".b16", base::ScalarType::B16},
      {".b32", base::ScalarType::B32},   {".b64", base::ScalarType::B64},
      {".b128", base::ScalarType::B128}, {".f16", base::ScalarType::F16},
      {".f32", base::ScalarType::F32},   {".f64", base::ScalarType::F64},
  };
  for (const auto& [spelling, type] : types) {
    SCOPED_TRACE(spelling);
    const auto resolved =
        resolveSource(".version 9.3\n.target sm_80\n.entry k(.param " +
                      std::string(spelling) + " value) {}");
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    const auto& declaration = resolved->functions[0].parameter_declarations[0];
    EXPECT_EQ(declaration.scalar_type, type);
    EXPECT_EQ(declaration.alignment, base::scalar_size_of(type));
    EXPECT_EQ(declaration.byte_extent, base::scalar_size_of(type));
    EXPECT_FALSE(declaration.explicit_alignment);
  }
}

/** Natural array alignment is consistent across declarations and all call paths. */
TEST(ResolvedParameterDeclarations, UsesNaturalArrayAlignmentForCalls) {
  const auto resolved = resolveSource(R"ptx(
.version 8.0
.target sm_80
.func target(.param .u32 values[2]);
.func target(.param .align 4 .u32 values[2]) { ret; }
.func alternate(.param .align 4 .u32 values[2]);
.entry caller() {
  .reg .u64 address;
  .param .u32 values[2];
  prototype: .callprototype _ (.param .u32 input[2]);
  targets: .calltargets target, alternate;
  call target, (values);
  call address, (values), prototype;
  call address, (values), targets;
  ret;
}
)ptx");
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& declaration = resolved->functions[0].parameter_declarations[0];
  const auto& definition = resolved->functions[1].parameter_declarations[0];
  EXPECT_EQ(declaration.alignment, definition.alignment);
  EXPECT_FALSE(declaration.explicit_alignment);
  EXPECT_TRUE(definition.explicit_alignment);
}

/** Entry pointer values can be loaded and passed through ordinary device formals. */
TEST(ResolvedParameterDeclarations, PassesEntryPointerValuesToDeviceFunctions) {
  const auto resolved = resolveSource(R"ptx(
.version 8.3
.target sm_80
.func target(.param .u64 address);
.entry caller(.param .u64 .ptr .global .align 16 pointer) {
  .reg .u64 function_address, pointer_value;
  prototype: .callprototype _ (.param .u64 address);
  ld.param.u64 pointer_value, [pointer];
  call target, (pointer_value);
  call function_address, (pointer_value), prototype;
  ret;
}
)ptx");
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_TRUE(resolved->functions[1].parameter_declarations[0].pointer);
  EXPECT_EQ(resolved->functions[1]
                .parameter_declarations[0]
                .pointer->pointed_alignment,
            16u);
}

/** Formal .param storage cannot substitute for a local call-argument object. */
TEST(ResolvedParameterDeclarations, RejectsFormalParameterCallActuals) {
  for (const std::string_view source : {
           ".func target(.param .u64 value); "
           ".entry caller(.param .u64 .ptr .global pointer) { "
           "call target, (pointer); }",
           ".func target(.param .u64 value); "
           ".func caller(.param .u64 input) { call target, (input); }",
           ".func (.param .u64 output) target(); "
           ".func (.param .u64 output) caller() { call (output), target, (); }",
           ".entry caller(.param .u64 input) { .reg .u64 address; "
           "prototype: .callprototype _ (.param .u64 value); "
           "call address, (input), prototype; }",
       }) {
    SCOPED_TRACE(source);
    const auto resolved = resolveSource(std::string(source));
    ASSERT_FALSE(resolved.has_value()) << source;
    ASSERT_EQ(resolved.error().size(), 1u) << source;
    EXPECT_NE(resolved.error()[0].message.find("Formal .param parameter"),
              std::string::npos);
  }
}

/** The final unsized device input may have no corresponding actual payload. */
TEST(ResolvedParameterDeclarations, AllowsOmittedFinalUnsizedInput) {
  const auto resolved = resolveSource(R"ptx(
.version 8.0
.target sm_80
.func only_bytes(.param .b8 bytes[]);
.func with_count(.param .u32 count, .param .b8 bytes[]);
.entry caller() {
  call only_bytes;
  call only_bytes, ();
  call with_count, (0);
  ret;
}
)ptx");
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
}

/** Repeated names retain distinct entry and device declaration identities. */
TEST(ResolvedParameterDeclarations, RetainsParametersPerFunctionScope) {
  const auto resolved = resolveSource(R"ptx(
.entry first(.param .u32 input) { ret; }
.entry second(.param .u32 input) {
  { .reg .u32 input; }
  ret;
}
.entry empty() { ret; }
.func (.param .u32 result) helper(.param .u32 input);
.func (.param .u32 result) helper(.param .u32 input) { ret; }
)ptx");

  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 5u);
  const auto& first_declarations =
      resolved->functions[0].parameter_declarations;
  const auto& second_declarations =
      resolved->functions[1].parameter_declarations;
  const auto& empty_declarations =
      resolved->functions[2].parameter_declarations;
  const auto& helper_declaration =
      resolved->functions[3].parameter_declarations;
  const auto& helper_definition = resolved->functions[4].parameter_declarations;
  ASSERT_EQ(first_declarations.size(), 1u);
  ASSERT_EQ(second_declarations.size(), 1u);
  ASSERT_TRUE(empty_declarations.empty());
  ASSERT_EQ(helper_declaration.size(), 2u);
  ASSERT_EQ(helper_definition.size(), 2u);
  EXPECT_EQ(first_declarations[0].role, ParameterDeclarationRole::EntryInput);
  EXPECT_EQ(second_declarations[0].role, ParameterDeclarationRole::EntryInput);
  EXPECT_EQ(helper_declaration[0].role, ParameterDeclarationRole::DeviceReturn);
  EXPECT_EQ(helper_declaration[1].role, ParameterDeclarationRole::DeviceInput);
  EXPECT_EQ(helper_definition[0].role, ParameterDeclarationRole::DeviceReturn);
  EXPECT_EQ(helper_definition[1].role, ParameterDeclarationRole::DeviceInput);
  const auto& first = resolved->symbols.symbol(first_declarations[0].symbol_id);
  const auto& second =
      resolved->symbols.symbol(second_declarations[0].symbol_id);
  EXPECT_NE(first.id, second.id);
  EXPECT_NE(first.scope, second.scope);
  EXPECT_EQ(first.name, "input");
  EXPECT_EQ(second.name, "input");
  EXPECT_EQ(first.kind, binding::SymbolKind::InputParameter);
  EXPECT_EQ(second.kind, binding::SymbolKind::InputParameter);
  EXPECT_EQ(first_declarations[0].scope_id, first.scope);
  EXPECT_EQ(second_declarations[0].scope_id, second.scope);
  EXPECT_EQ(resolved->symbols.scope(first.scope).owner,
            resolved->functions[0].symbol_id);
  EXPECT_EQ(resolved->symbols.scope(second.scope).owner,
            resolved->functions[1].symbol_id);
  EXPECT_NE(helper_declaration[0].symbol_id, helper_definition[0].symbol_id);
  EXPECT_NE(helper_declaration[1].symbol_id, helper_definition[1].symbol_id);
  EXPECT_NE(helper_declaration[0].scope_id, helper_definition[0].scope_id);
  EXPECT_NE(helper_declaration[1].scope_id, helper_definition[1].scope_id);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
