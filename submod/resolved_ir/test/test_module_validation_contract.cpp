#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse a complete module and retain a useful failure in the test output. */
std::optional<syntax_ast::AstModule> parseModule(std::string_view source) {
  PtxSyntaxParser parser(source);
  auto parsed = parser.parseModule();
  if (!parsed || !parsed.diagnostics.empty()) {
    ADD_FAILURE() << (parsed.diagnostics.empty()
                          ? "PTX source did not parse."
                          : parsed.diagnostics.front().message);
    return std::nullopt;
  }
  return std::move(*parsed);
}

/** Test whether a resolution diagnostic carries the requested checker kind. */
bool hasCheckerKind(const ModuleResolveDiagnostics& diagnostics,
                    checker::CheckDiagnosticKind kind) {
  for (const auto& diagnostic : diagnostics) {
    if (diagnostic.checker_kind && *diagnostic.checker_kind == kind)
      return true;
  }
  return false;
}

/** Test whether a checker result carries the requested diagnostic kind. */
bool hasCheckerKind(const checker::CheckDiagnostics& diagnostics,
                    checker::CheckDiagnosticKind kind) {
  for (const auto& diagnostic : diagnostics) {
    if (diagnostic.kind == kind)
      return true;
  }
  return false;
}

/** Resolve a complete source and release its parser, AST, and source storage. */
std::expected<ResolvedModule, ModuleResolveDiagnostics>
resolveOwnedPointerSource(std::string source) {
  const auto ast = parseModule(source);
  if (!ast)
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveModuleOnly(*ast);
}

/** Owned pointer validation preserves all six types under both address headers. */
TEST(ModuleValidationContract, ValidatesOwnedIntegerAndBitEntryPointers) {
  const std::pair<std::string_view, base::ScalarType> types[] = {
      {".b32", base::ScalarType::B32}, {".b64", base::ScalarType::B64},
      {".s32", base::ScalarType::S32}, {".s64", base::ScalarType::S64},
      {".u32", base::ScalarType::U32}, {".u64", base::ScalarType::U64}};
  constexpr std::array<std::string_view, 6> qualifiers{
      "", ".global", ".local", ".shared", ".const", ".global .align 32"};
  for (const auto address_size : {"32", "64"}) {
    for (size_t index = 0; index < std::size(types); ++index) {
      const auto& [spelling, type] = types[index];
      SCOPED_TRACE(std::string(address_size) + " " + std::string(spelling));
      auto resolved = resolveOwnedPointerSource(
          ".version 9.3\n.target sm_80\n.address_size " +
          std::string(address_size) + "\n.entry k(.param " +
          (index == 5 ? ".align 16 " : "") + std::string(spelling) + " .ptr " +
          std::string(qualifiers[index]) + " p) { ret; }");
      ASSERT_TRUE(resolved.has_value())
          << (resolved.error().empty() ? "No diagnostic"
                                       : resolved.error().front().message);
      const auto& function = resolved->functions.front();
      const auto& record = function.parameter_declarations.front();
      const auto& formal = function.contract.signature.parameters.front();
      const auto& symbol = resolved->symbols.symbol(record.symbol_id);
      const auto bytes = base::scalar_size_of(type);
      EXPECT_EQ(record.scalar_type, type);
      EXPECT_EQ(formal.scalar_type, type);
      EXPECT_EQ(formal.type_spelling, spelling);
      EXPECT_EQ(symbol.type, spelling);
      EXPECT_EQ(record.alignment, index == 5 ? 16u : bytes);
      EXPECT_EQ(record.explicit_alignment, index == 5);
      EXPECT_EQ(record.byte_extent, bytes);
      ASSERT_TRUE(record.pointer);
      EXPECT_EQ(record.pointer->pointed_alignment, index == 5 ? 32u : 4u);
      EXPECT_EQ(record.pointer->pointed_state_space,
                formal.pointed_state_space);
      const auto checked = validateModule(*resolved);
      EXPECT_TRUE(checked.has_value())
          << (checked ? "" : checked.error().front().message);
    }
  }
}

/** Adding coherent pointer metadata cannot admit unsupported owned scalar types. */
TEST(ModuleValidationContract, RejectsUnsupportedOwnedPointerScalarTypes) {
  for (const auto spelling :
       {".b16", ".s16", ".u16", ".b128", ".f32", ".f64"}) {
    SCOPED_TRACE(spelling);
    auto resolved = resolveOwnedPointerSource(
        ".version 9.3\n.target sm_80\n.entry k(.param " +
        std::string(spelling) + " p) { ret; }");
    ASSERT_TRUE(resolved.has_value());
    ASSERT_TRUE(validateModule(*resolved).has_value());
    auto& function = resolved->functions.front();
    auto& record = function.parameter_declarations.front();
    auto& formal = function.contract.signature.parameters.front();
    record.pointer.emplace();
    record.pointer->pointed_alignment = 4;
    formal.is_pointer = true;
    formal.pointer_alignment = uint64_t{4};
    EXPECT_FALSE(validateModule(*resolved).has_value());
  }
}

/** Pointer metadata still rejects invalid owned roles, shapes, and attributes. */
TEST(ModuleValidationContract, RejectsInvalidOwnedPointerAttributes) {
  for (int mutation = 0; mutation < 6; ++mutation) {
    SCOPED_TRACE(mutation);
    auto resolved = resolveOwnedPointerSource(
        ".version 9.3\n.target sm_80\n.entry k(.param .b64 .ptr .global p) { "
        "ret; }");
    ASSERT_TRUE(resolved.has_value());
    ASSERT_TRUE(validateModule(*resolved).has_value());
    auto& record = resolved->functions.front().parameter_declarations.front();
    switch (mutation) {
      case 0:
        record.role = ParameterDeclarationRole::DeviceInput;
        break;
      case 1:
        record.array_extents = {2};
        break;
      case 2:
        record.pointer->pointed_alignment = 0;
        break;
      case 3:
        record.pointer->pointed_alignment = 6;
        break;
      case 4:
        record.pointer->pointed_state_space =
            call_argument_compatibility::PointedStateSpace::Invalid;
        break;
      case 5:
        record.scalar_type = base::ScalarType::Invalid;
        break;
    }
    EXPECT_FALSE(validateModule(*resolved).has_value());
  }
}

TEST(ModuleValidationContract, SeparatesResolutionFromCompleteValidation) {
  const auto ast = parseModule(R"ptx(
.version 7.8
.entry kernel() {
  ret;
}
)ptx");
  ASSERT_TRUE(ast);

  const auto only = resolveModuleOnly(*ast);
  ASSERT_TRUE(only.has_value()) << only.error().front().message;

  const auto available = resolveModule(*ast);
  EXPECT_TRUE(available.has_value())
      << (available ? "" : available.error().front().message);

  const auto strict = resolveAndValidateModule(*ast);
  ASSERT_FALSE(strict.has_value());
  EXPECT_TRUE(hasCheckerKind(
      strict.error(), checker::CheckDiagnosticKind::MissingValidationContext));

  const auto default_policy = validateModule(*ast, *only);
  ASSERT_FALSE(default_policy.has_value());
  EXPECT_TRUE(
      hasCheckerKind(default_policy.error(),
                     checker::CheckDiagnosticKind::MissingValidationContext));
}

TEST(ModuleValidationContract,
     ResolvesInvalidInstructionBeforeAvailabilityCheck) {
  const auto ast = parseModule(R"ptx(
.version 7.8
.target sm_80
.entry kernel() .reqnctapercluster 2, 1 {
  .reg .u32 %r;
  mov.u32 %r, %cluster_ctarank;
}
)ptx");
  ASSERT_TRUE(ast);

  const auto only = resolveModuleOnly(*ast);
  ASSERT_TRUE(only.has_value()) << only.error().front().message;

  const auto available = resolveModule(*ast);
  ASSERT_FALSE(available.has_value());
  EXPECT_TRUE(
      hasCheckerKind(available.error(),
                     checker::CheckDiagnosticKind::UnsupportedAvailability));

  const auto strict = resolveAndValidateModule(*ast);
  ASSERT_FALSE(strict.has_value());
  EXPECT_TRUE(hasCheckerKind(
      strict.error(), checker::CheckDiagnosticKind::UnsupportedAvailability));
}

TEST(ModuleValidationContract, UnknownTargetIsRejectedByBothPipelines) {
  const auto ast = parseModule(R"ptx(
.version 7.8
.target sm_123a
.entry kernel() {
  ret;
}
)ptx");
  ASSERT_TRUE(ast);

  const auto only = resolveModuleOnly(*ast);
  ASSERT_TRUE(only.has_value()) << only.error().front().message;

  const auto available = resolveModule(*ast);
  ASSERT_FALSE(available.has_value());
  EXPECT_TRUE(hasCheckerKind(available.error(),
                             checker::CheckDiagnosticKind::UnknownTarget));

  const auto strict = resolveAndValidateModule(*ast);
  ASSERT_FALSE(strict.has_value());
  EXPECT_TRUE(hasCheckerKind(strict.error(),
                             checker::CheckDiagnosticKind::UnknownTarget));
}

TEST(ModuleValidationContract, EnforcesPtxVersionAvailabilityAtBoundary) {
  const auto too_old_ast = parseModule(R"ptx(
.version 1.2
.target sm_13
.entry kernel() {
  .reg .u32 %r;
  mov.u32 %r, %laneid;
}
)ptx");
  const auto supported_ast = parseModule(R"ptx(
.version 1.3
.target sm_13
.entry kernel() {
  .reg .u32 %r;
  mov.u32 %r, %laneid;
}
)ptx");
  ASSERT_TRUE(too_old_ast);
  ASSERT_TRUE(supported_ast);

  const auto too_old_only = resolveModuleOnly(*too_old_ast);
  ASSERT_TRUE(too_old_only.has_value()) << too_old_only.error().front().message;
  const auto supported_only = resolveModuleOnly(*supported_ast);
  ASSERT_TRUE(supported_only.has_value())
      << supported_only.error().front().message;

  const auto rejected = resolveModule(*too_old_ast);
  ASSERT_FALSE(rejected.has_value());
  EXPECT_TRUE(hasCheckerKind(
      rejected.error(), checker::CheckDiagnosticKind::UnsupportedPtxVersion));
  EXPECT_TRUE(resolveModule(*supported_ast).has_value());
}

TEST(ModuleValidationContract, AllowsRetargetedSeparatelyParsedSameBody) {
  const auto source = parseModule(R"ptx(
.version 7.8
.func first() {
  ret;
}
)ptx");
  const auto retargeted = parseModule(R"ptx(
.version 7.8
.target sm_80
.func first() {
  ret;
}
)ptx");
  ASSERT_TRUE(source);
  ASSERT_TRUE(retargeted);
  const auto resolved = resolveModuleOnly(*source);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;

  EXPECT_TRUE(checkModuleAvailability(*retargeted, *resolved));
  EXPECT_TRUE(resolveAndValidateModule(*retargeted).has_value());
}

TEST(ModuleValidationContract, KeepsTargetRegionsAfterIrFunctionReordering) {
  const auto ast = parseModule(R"ptx(
.version 7.8
.target sm_90
.entry cluster_user() {
  .reg .u32 %r;
  mov.u32 %r, %cluster_ctarank;
}
.target sm_80
.entry ordinary() { ret; }
)ptx");
  ASSERT_TRUE(ast);
  auto resolved = resolveModuleOnly(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_EQ(resolved->functions.size(), 2u);
  EXPECT_EQ(resolved->functions[0].source_target, "sm_90");
  EXPECT_EQ(resolved->functions[1].source_target, "sm_80");
  EXPECT_EQ(resolved->functions[0].source_version, (checker::PtxVersion{7, 8}));
  std::swap(resolved->functions[0], resolved->functions[1]);
  const auto checked = validateModule(*ast, *resolved);
  EXPECT_TRUE(checked.has_value())
      << (checked ? "" : checked.error().front().message);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
