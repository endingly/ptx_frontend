#include <gtest/gtest.h>

#include <algorithm>
#include <concepts>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Safely describe a source failure, including an earlier parser assertion. */
std::string namedMatrixFailure(const ModuleResolveDiagnostics& diagnostics) {
  return diagnostics.empty()
             ? "Module parsing failed; see the parser assertion."
             : diagnostics.front().message;
}

/** Resolve complete source and release all parser/source owners before returning. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> ownedNamedMatrix(
    std::string declarations, std::string instruction,
    std::string_view target = "sm_80") {
  const std::string source = ".version 9.3\n.target " + std::string(target) +
                             "\n.address_size 64\n.entry kernel() { " +
                             declarations + instruction + " }";
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  EXPECT_TRUE(ast && ast.diagnostics.empty())
      << (ast.diagnostics.empty() ? "No module AST."
                                  : ast.diagnostics.front().message);
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveAndValidateModule(*ast);
}

/** Assert the existing whole base and every independently ordered named projection. */
void expectNamedMatrix(const WithLocs<ResolvedRegisterVector>& vector,
                       size_t arity) {
  ASSERT_EQ(vector.value.source.kind, ResolvedVectorSourceKind::NamedVector);
  ASSERT_TRUE(vector.value.source.whole_base);
  const auto& whole = *vector.value.source.whole_base;
  ASSERT_TRUE(whole.value.register_ref.symbol_id);
  EXPECT_EQ(whole.value.register_ref.vector_width, arity);
  ASSERT_EQ(whole.locs.size(), 1u);
  EXPECT_EQ(whole.locs.front(), vector.value.source.range);
  ASSERT_EQ(vector.value.elements.size(), arity);
  ASSERT_EQ(vector.locs.size(), arity);
  for (size_t lane = 0; lane < arity; ++lane) {
    ASSERT_TRUE(vector.value.elements[lane]);
    const auto& reference = *vector.value.elements[lane];
    EXPECT_EQ(reference.symbol_id, whole.value.register_ref.symbol_id);
    EXPECT_EQ(reference.spelling, whole.value.register_ref.spelling);
    EXPECT_EQ(reference.declared_type, whole.value.register_ref.declared_type);
    EXPECT_EQ(reference.parameterized_index,
              whole.value.register_ref.parameterized_index);
    EXPECT_FALSE(reference.vector_width);
    ASSERT_TRUE(reference.component);
    EXPECT_EQ(reference.component->lane, lane);
    EXPECT_EQ(reference.component->declaration_width, arity);
    EXPECT_EQ(reference.component->origin,
              RegisterComponentOrigin::NamedProjection);
    EXPECT_FALSE(reference.component->selector);
    EXPECT_EQ(reference.component->range, vector.value.source.range);
    EXPECT_EQ(reference.component->base_range, vector.value.source.range);
    EXPECT_EQ(vector.locs[lane], vector.value.source.range);
  }
}

/** Named Element lanes retain the same final class, owned topology and current checks. */
TEST(MatrixNamedFragments, LegacyElementOwnershipCloneAndCurrentMutation) {
  using Exact = MmaSyncAlignedM16n8k8RowColF32F16F16F32;
  const auto declaration =
      ".reg .v4 .f32 D, C; .reg .v2 .b32 A; .reg .b32 %b; ";
  const auto instruction =
      "mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32 D,A,{%b},C;";
  auto owned = ownedNamedMatrix(declaration, instruction);
  ASSERT_TRUE(owned) << namedMatrixFailure(owned.error());
  auto& exact = dynamic_cast<Exact&>(*owned->functions.front().body.front());
  expectNamedMatrix(exact.dst, 4);
  expectNamedMatrix(exact.a, 2);
  expectNamedMatrix(exact.c, 4);
  EXPECT_EQ(exact.matrix_descriptor(), &Exact::matrix_topology);
  ResolvedModule copied = *owned;
  ResolvedModule assigned;
  assigned = copied;
  auto clone = assigned.functions.front().body.front()->clone();
  EXPECT_EQ(dynamic_cast<Exact&>(*clone).a, exact.a);
  EXPECT_TRUE(
      validateModule(assigned, ModuleValidationPolicy::RequireCompleteContext));
  auto saved = exact.a;
  exact.a.value.elements[1]->component->lane = 0;
  EXPECT_FALSE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext));
  exact.a = saved;
  EXPECT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext));
  exact.a.value.source.whole_base.reset();
  EXPECT_FALSE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext));
}

/** Register A and descriptor A share syntax but select through actual lexical declarations. */
TEST(MatrixNamedFragments, WgmmaPairedPrivateDeclarationSelection) {
  const auto common = ".reg .v4 .f32 D; .reg .b64 %bdesc; ";
  const auto operation =
      "wgmma.mma_async.sync.aligned.m64n8k8.f32.tf32.tf32 D,A,%bdesc,1,1,-1;";
  auto named = ownedNamedMatrix(common + std::string(".reg .v4 .b32 A; "),
                                operation, "sm_90a");
  ASSERT_TRUE(named) << namedMatrixFailure(named.error());
  auto* register_a =
      dynamic_cast<WgmmaMmaAsyncDenseM64n8k8F32Tf32Tf32RegisterPlain*>(
          named->functions.front().body.front().get());
  ASSERT_NE(register_a, nullptr);
  expectNamedMatrix(register_a->a, 4);
  auto scalar = ownedNamedMatrix(common + std::string(".reg .b64 A; "),
                                 operation, "sm_90a");
  ASSERT_TRUE(scalar) << namedMatrixFailure(scalar.error());
  EXPECT_NE(dynamic_cast<WgmmaMmaAsyncDenseM64n8k8F32Tf32Tf32SharedPlain*>(
                scalar->functions.front().body.front().get()),
            nullptr);
  for (const auto bad : {".reg .v2 .b32 A; ", ".reg .v4 .b16 A; "}) {
    auto rejected =
        ownedNamedMatrix(common + std::string(bad), operation, "sm_90a");
    EXPECT_FALSE(
        rejected);  // Wrong whole count/type cannot be rescued as a scalar descriptor.
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir {
namespace {
/** Physical Element compatibility uses each declared fundamental lane, without aggregate unpacking. */
TEST(MatrixNamedFragments,
     LegacyACompatiblePhysicalLanesWithMatchedBraceControl) {
  using Exact = MmaSyncAlignedM16n8k8RowColF32F16F16F32;
  for (const auto lane_type : {"b32", "f16x2"}) {
    const auto declaration =
        ".reg .f32 %d<4>, %c<4>; .reg .b32 %b; .reg .v2 ." +
        std::string(lane_type) + " A; .reg ." + std::string(lane_type) +
        " %a<2>; ";
    const auto prefix =
        "mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32 "
        "{%d0,%d1,%d2,%d3},";
    const auto suffix = ",{%b},{%c0,%c1,%c2,%c3};";
    auto named =
        ownedNamedMatrix(declaration, std::string(prefix) + "A" + suffix);
    auto brace = ownedNamedMatrix(declaration,
                                  std::string(prefix) + "{%a0,%a1}" + suffix);
    ASSERT_TRUE(named) << lane_type;
    ASSERT_TRUE(brace) << lane_type;
    auto& exact = dynamic_cast<Exact&>(*named->functions.front().body.front());
    expectNamedMatrix(exact.a, 2);
    EXPECT_EQ(named->functions.front().body.front()->instruction_kind(),
              brace->functions.front().body.front()->instruction_kind());
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir {
namespace {
/** Inspect actual borrowed vector references while their owning instruction lives. */
struct MatrixReferenceCapture final : detail::IReferenceObserver {
  /** The single whole named field under test; no borrowed value outlives the owner. */
  const ResolvedRegisterVector* named = nullptr;
  std::span<const SourceRange> locations;
  /** Keep only the genuine whole-vector field; brace siblings are independent. */
  void register_vector(const ResolvedRegisterVector& value,
                       std::span<const SourceRange> ranges,
                       checker::AddressSymbolResolutionPolicy) override {
    if (value.source.kind == ResolvedVectorSourceKind::NamedVector) {
      named = &value;
      locations = ranges;
    }
  }
};

/** Replace one explicit fixture marker without changing any sibling operand. */
std::string matrixFixtureReplace(std::string text, std::string_view key,
                                 std::string_view replacement) {
  text.replace(text.find(key), key.size(), replacement);
  return text;
}

/** Exercise real source, current owners, exact references and independent corruptions. */
template <typename Exact>
  requires std::derived_from<Exact, Instruction>
void checkMatrixCohort(std::string declarations, std::string prefix,
                       std::string suffix, size_t arity,
                       std::initializer_list<std::string_view> types,
                       WithLocs<ResolvedRegisterVector> Exact::* field,
                       std::string_view target) {
  for (const auto type : types) {
    SCOPED_TRACE(type);
    const auto decl = matrixFixtureReplace(declarations, "$TYPE", type);
    std::string brace = "{";
    for (size_t lane = 0; lane < arity; ++lane) {
      if (lane)
        brace += ",";
      brace += "N1." + std::string(1, "xyzw"[lane]);
    }
    brace += "}";
    auto control = ownedNamedMatrix(decl, prefix + brace + suffix, target);
    ASSERT_TRUE(control) << (control ? ""
                                     : namedMatrixFailure(control.error()));
    const std::string source = ".version 9.3\n.target " + std::string(target) +
                               "\n.address_size 64\n.entry kernel() { " + decl +
                               prefix + "N1" + suffix + " }";
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto supplied = resolveAndValidateModule(*ast);
    ASSERT_TRUE(supplied) << (supplied ? ""
                                       : namedMatrixFailure(supplied.error()));
    auto owned = ownedNamedMatrix(decl, prefix + "N1" + suffix, target);
    ASSERT_TRUE(owned) << (owned ? "" : namedMatrixFailure(owned.error()));
    EXPECT_EQ(owned->functions.front().body.front()->instruction_kind(),
              control->functions.front().body.front()->instruction_kind());
    ResolvedModule copied(*owned), assigned;
    assigned = copied;
    assigned.functions.front().body.front() =
        copied.functions.front().body.front()->clone();
    auto& exact =
        dynamic_cast<Exact&>(*assigned.functions.front().body.front());
    auto& vector = exact.*field;
    expectNamedMatrix(vector, arity);
    EXPECT_EQ(
        vector.value.source.whole_base->value.register_ref.parameterized_index,
        1u);
    MatrixReferenceCapture observer;
    exact.visit_references(observer);
    ASSERT_EQ(observer.named, &vector.value);
    EXPECT_EQ(observer.locations.data(), vector.locs.data());
    EXPECT_TRUE(validateModule(assigned,
                               ModuleValidationPolicy::RequireCompleteContext));
    EXPECT_TRUE(validateModule(*ast, assigned,
                               ModuleValidationPolicy::RequireCompleteContext));
    const auto good = vector;
    // Every mutation starts from the same healthy real owner and valid supplied AST.
    for (unsigned mutation = 0; mutation < 6; ++mutation) {
      SCOPED_TRACE(mutation);
      vector = good;
      switch (mutation) {
        case 0:
          vector.value.source.whole_base.reset();
          break;
        case 1:
          vector.value.elements[1]->component->lane = 0;
          break;
        case 2:
          vector.value.elements[0]->symbol_id.reset();
          break;
        case 3:
          vector.locs[0] = {};
          break;
        case 4:
          vector.value.elements[0]->declared_type = ScalarType::B128;
          break;
        case 5:
          vector.value.source.whole_base->value.register_ref
              .parameterized_index = 0;
          break;
      }
      EXPECT_FALSE(validateModule(
          assigned, ModuleValidationPolicy::RequireCompleteContext));
      EXPECT_FALSE(validateModule(
          *ast, assigned, ModuleValidationPolicy::RequireCompleteContext));
    }
    vector = good;
    EXPECT_TRUE(validateModule(assigned));
    const auto changed = matrixFixtureReplace(source, "N1", "N0");
    PtxSyntaxParser changed_parser(changed);
    const auto changed_ast = changed_parser.parseModule();
    ASSERT_TRUE(changed_ast && changed_ast.diagnostics.empty());
    const auto mismatch = validateModule(*changed_ast, assigned);
    ASSERT_FALSE(mismatch);
    EXPECT_TRUE(
        std::ranges::any_of(mismatch.error(), [](const auto& diagnostic) {
          return diagnostic.kind ==
                 checker::CheckDiagnosticKind::ModuleSourceMismatch;
        }));
  }
  // Count and physical type errors cannot gain scalar-descriptor rescue.
  for (const auto width : {arity == 2 ? 4u : 2u}) {
    auto decl = matrixFixtureReplace(declarations, "$TYPE", *types.begin());
    const auto marker = ".v" + std::to_string(arity) + " ";
    decl =
        matrixFixtureReplace(decl, marker, ".v" + std::to_string(width) + " ");
    EXPECT_FALSE(ownedNamedMatrix(decl, prefix + "N1" + suffix, target));
  }
  EXPECT_FALSE(
      ownedNamedMatrix(matrixFixtureReplace(declarations, "$TYPE", "b16"),
                       prefix + "N1" + suffix, target));
}
/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC01) {
  checkMatrixCohort<MmaSyncAlignedM16n8k8RowColF32Bf16Bf16F32>(
      ".reg .f32 r0<4>; .reg .v2 .$TYPE N<2>; .reg .b32 r2<1>; .reg .f32 "
      "r3<4>; ",
      "mma.sync.aligned.m16n8k8.row.col.f32.bf16.bf16.f32 {r00,r01,r02,r03},",
      ",{r20},{r30,r31,r32,r33};", 2, {"b32", "s32", "u32", "f32", "f16x2"},
      &MmaSyncAlignedM16n8k8RowColF32Bf16Bf16F32::a, "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC02) {
  checkMatrixCohort<MmaSyncAlignedM16n8k16RowColF32Bf16Bf16F32>(
      ".reg .f32 r0<4>; .reg .v4 .$TYPE N<2>; .reg .b32 r2<2>; .reg .f32 "
      "r3<4>; ",
      "mma.sync.aligned.m16n8k16.row.col.f32.bf16.bf16.f32 {r00,r01,r02,r03},",
      ",{r20,r21},{r30,r31,r32,r33};", 4, {"b32", "s32", "u32", "f32", "f16x2"},
      &MmaSyncAlignedM16n8k16RowColF32Bf16Bf16F32::a, "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC03) {
  checkMatrixCohort<MmaSyncAlignedM16n8k8RowColF32F16F16F32>(
      ".reg .f32 r0<4>; .reg .v2 .$TYPE N<2>; .reg .f16x2 r2<1>; .reg .f32 "
      "r3<4>; ",
      "mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32 {r00,r01,r02,r03},",
      ",{r20},{r30,r31,r32,r33};", 2, {"b32", "f16x2"},
      &MmaSyncAlignedM16n8k8RowColF32F16F16F32::a, "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC04) {
  checkMatrixCohort<MmaSyncAlignedM16n8k16RowColF16F16F16F16>(
      ".reg .f16x2 r0<2>; .reg .v4 .$TYPE N<2>; .reg .f16x2 r2<2>; .reg .f16x2 "
      "r3<2>; ",
      "mma.sync.aligned.m16n8k16.row.col.f16.f16.f16.f16 {r00,r01},",
      ",{r20,r21},{r30,r31};", 4, {"b32", "f16x2"},
      &MmaSyncAlignedM16n8k16RowColF16F16F16F16::a, "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC05) {
  checkMatrixCohort<MmaSyncAlignedM16n8k4RowColF64F64F64F64>(
      ".reg .f64 r0<4>; .reg .v2 .$TYPE N<2>; .reg .f64 r2<1>; .reg .f64 "
      "r3<4>; ",
      "mma.sync.aligned.m16n8k4.row.col.f64.f64.f64.f64 {r00,r01,r02,r03},",
      ",{r20},{r30,r31,r32,r33};", 2, {"b64", "f64"},
      &MmaSyncAlignedM16n8k4RowColF64F64F64F64::a, "sm_90");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC06) {
  checkMatrixCohort<MmaSyncAlignedM16n8k16RowColF32Bf16Bf16F32>(
      ".reg .f32 r0<4>; .reg .b32 r1<4>; .reg .v2 .$TYPE N<2>; .reg .f32 "
      "r3<4>; ",
      "mma.sync.aligned.m16n8k16.row.col.f32.bf16.bf16.f32 "
      "{r00,r01,r02,r03},{r10,r11,r12,r13},",
      ",{r30,r31,r32,r33};", 2, {"b32", "s32", "u32", "f32", "f16x2"},
      &MmaSyncAlignedM16n8k16RowColF32Bf16Bf16F32::b, "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC07) {
  checkMatrixCohort<MmaSpSpSyncAlignedM16n8k32RowColF32Bf16Bf16F32>(
      ".reg .f32 r0<4>; .reg .b32 r1<4>; .reg .v4 .$TYPE N<2>; .reg .f32 "
      "r3<4>; .reg .b32 r4; ",
      "mma.sp.sync.aligned.m16n8k32.row.col.f32.bf16.bf16.f32 "
      "{r00,r01,r02,r03},{r10,r11,r12,r13},",
      ",{r30,r31,r32,r33},r4,0;", 4, {"b32", "s32", "u32", "f32", "f16x2"},
      &MmaSpSpSyncAlignedM16n8k32RowColF32Bf16Bf16F32::b, "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC08) {
  checkMatrixCohort<MmaSyncAlignedM8n8k4RowRowF16F16F16F16>(
      ".reg .f16x2 r0<4>; .reg .f16x2 r1<2>; .reg .v2 .$TYPE N<2>; .reg .f16x2 "
      "r3<4>; ",
      "mma.sync.aligned.m8n8k4.row.row.f16.f16.f16.f16 "
      "{r00,r01,r02,r03},{r10,r11},",
      ",{r30,r31,r32,r33};", 2, {"b32", "f16x2"},
      &MmaSyncAlignedM8n8k4RowRowF16F16F16F16::b, "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC09) {
  checkMatrixCohort<MmaSpSpSyncAlignedM16n8k32RowColF16F16F16F16>(
      ".reg .f16x2 r0<2>; .reg .f16x2 r1<4>; .reg .v4 .$TYPE N<2>; .reg .f16x2 "
      "r3<2>; .reg .b32 r4; ",
      "mma.sp.sync.aligned.m16n8k32.row.col.f16.f16.f16.f16 "
      "{r00,r01},{r10,r11,r12,r13},",
      ",{r30,r31},r4,0;", 4, {"b32", "f16x2"},
      &MmaSpSpSyncAlignedM16n8k32RowColF16F16F16F16::b, "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC10) {
  checkMatrixCohort<MmaSyncAlignedM16n8k8RowColF64F64F64F64>(
      ".reg .f64 r0<4>; .reg .f64 r1<4>; .reg .v2 .$TYPE N<2>; .reg .f64 "
      "r3<4>; ",
      "mma.sync.aligned.m16n8k8.row.col.f64.f64.f64.f64 "
      "{r00,r01,r02,r03},{r10,r11,r12,r13},",
      ",{r30,r31,r32,r33};", 2, {"b64", "f64"},
      &MmaSyncAlignedM16n8k8RowColF64F64F64F64::b, "sm_90");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC11) {
  checkMatrixCohort<MmaSyncAlignedM16n8k8RowColF16F16F16F16>(
      ".reg .f16x2 r0<2>; .reg .f16x2 r1<2>; .reg .f16x2 r2<1>; .reg .v2 "
      ".$TYPE N<2>; ",
      "mma.sync.aligned.m16n8k8.row.col.f16.f16.f16.f16 "
      "{r00,r01},{r10,r11},{r20},",
      ";", 2, {"b32", "f16x2"}, &MmaSyncAlignedM16n8k8RowColF16F16F16F16::c,
      "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC12) {
  checkMatrixCohort<MmaSyncAlignedM8n8k4RowRowF16F16F16F16>(
      ".reg .f16x2 r0<4>; .reg .f16x2 r1<2>; .reg .f16x2 r2<2>; .reg .v4 "
      ".$TYPE N<2>; ",
      "mma.sync.aligned.m8n8k4.row.row.f16.f16.f16.f16 "
      "{r00,r01,r02,r03},{r10,r11},{r20,r21},",
      ";", 4, {"b32", "f16x2"}, &MmaSyncAlignedM8n8k4RowRowF16F16F16F16::c,
      "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC13) {
  checkMatrixCohort<MmaSyncAlignedM16n8k8RowColF32F16F16F32>(
      ".reg .f32 r0<4>; .reg .f16x2 r1<2>; .reg .f16x2 r2<1>; .reg .v4 .$TYPE "
      "N<2>; ",
      "mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32 "
      "{r00,r01,r02,r03},{r10,r11},{r20},",
      ";", 4, {"b32", "f32"}, &MmaSyncAlignedM16n8k8RowColF32F16F16F32::c,
      "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC14) {
  checkMatrixCohort<MmaSyncAlignedM8n8k4RowColF64F64F64F64>(
      ".reg .f64 r0<2>; .reg .f64 r1<1>; .reg .f64 r2<1>; .reg .v2 .$TYPE "
      "N<2>; ",
      "mma.sync.aligned.m8n8k4.row.col.f64.f64.f64.f64 {r00,r01},{r10},{r20},",
      ";", 2, {"b64", "f64"}, &MmaSyncAlignedM8n8k4RowColF64F64F64F64::c,
      "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC15) {
  checkMatrixCohort<MmaSyncAlignedM8n8k16RowColS32S8S8S32>(
      ".reg .s32 r0<2>; .reg .b32 r1<1>; .reg .b32 r2<1>; .reg .v2 .$TYPE "
      "N<2>; ",
      "mma.sync.aligned.m8n8k16.row.col.s32.s8.s8.s32 {r00,r01},{r10},{r20},",
      ";", 2, {"b32", "s32", "u32"}, &MmaSyncAlignedM8n8k16RowColS32S8S8S32::c,
      "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC16) {
  checkMatrixCohort<MmaSyncAlignedM16n8k16RowColS32S8S8S32>(
      ".reg .s32 r0<4>; .reg .b32 r1<2>; .reg .b32 r2<1>; .reg .v4 .$TYPE "
      "N<2>; ",
      "mma.sync.aligned.m16n8k16.row.col.s32.s8.s8.s32 "
      "{r00,r01,r02,r03},{r10,r11},{r20},",
      ";", 4, {"b32", "s32", "u32"}, &MmaSyncAlignedM16n8k16RowColS32S8S8S32::c,
      "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC17) {
  checkMatrixCohort<MmaSyncAlignedM16n8k8RowColF16F16F16F16>(
      ".reg .v2 .$TYPE N<2>; .reg .f16x2 r1<2>; .reg .f16x2 r2<1>; .reg .f16x2 "
      "r3<2>; ",
      "mma.sync.aligned.m16n8k8.row.col.f16.f16.f16.f16 ",
      ",{r10,r11},{r20},{r30,r31};", 2, {"b32", "f16x2"},
      &MmaSyncAlignedM16n8k8RowColF16F16F16F16::dst, "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC18) {
  checkMatrixCohort<MmaSyncAlignedM8n8k4RowRowF16F16F16F16>(
      ".reg .v4 .$TYPE N<2>; .reg .f16x2 r1<2>; .reg .f16x2 r2<2>; .reg .f16x2 "
      "r3<4>; ",
      "mma.sync.aligned.m8n8k4.row.row.f16.f16.f16.f16 ",
      ",{r10,r11},{r20,r21},{r30,r31,r32,r33};", 4, {"b32", "f16x2"},
      &MmaSyncAlignedM8n8k4RowRowF16F16F16F16::dst, "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC19) {
  checkMatrixCohort<MmaSyncAlignedM16n8k8RowColF32F16F16F32>(
      ".reg .v4 .$TYPE N<2>; .reg .f16x2 r1<2>; .reg .f16x2 r2<1>; .reg .f32 "
      "r3<4>; ",
      "mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32 ",
      ",{r10,r11},{r20},{r30,r31,r32,r33};", 4, {"b32", "f32"},
      &MmaSyncAlignedM16n8k8RowColF32F16F16F32::dst, "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC20) {
  checkMatrixCohort<MmaSyncAlignedM8n8k4RowColF64F64F64F64>(
      ".reg .v2 .$TYPE N<2>; .reg .f64 r1<1>; .reg .f64 r2<1>; .reg .f64 "
      "r3<2>; ",
      "mma.sync.aligned.m8n8k4.row.col.f64.f64.f64.f64 ",
      ",{r10},{r20},{r30,r31};", 2, {"b64", "f64"},
      &MmaSyncAlignedM8n8k4RowColF64F64F64F64::dst, "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC21) {
  checkMatrixCohort<MmaSyncAlignedM8n8k16RowColS32S8S8S32>(
      ".reg .v2 .$TYPE N<2>; .reg .b32 r1<1>; .reg .b32 r2<1>; .reg .s32 "
      "r3<2>; ",
      "mma.sync.aligned.m8n8k16.row.col.s32.s8.s8.s32 ",
      ",{r10},{r20},{r30,r31};", 2, {"b32", "s32", "u32"},
      &MmaSyncAlignedM8n8k16RowColS32S8S8S32::dst, "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC22) {
  checkMatrixCohort<MmaSyncAlignedM16n8k16RowColS32S8S8S32>(
      ".reg .v4 .$TYPE N<2>; .reg .b32 r1<2>; .reg .b32 r2<1>; .reg .s32 "
      "r3<4>; ",
      "mma.sync.aligned.m16n8k16.row.col.s32.s8.s8.s32 ",
      ",{r10,r11},{r20},{r30,r31,r32,r33};", 4, {"b32", "s32", "u32"},
      &MmaSyncAlignedM16n8k16RowColS32S8S8S32::dst, "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC23) {
  checkMatrixCohort<WgmmaMmaAsyncDenseM64n8k16F16F16F16RegisterPlain>(
      ".reg .b32 r0<2>; .reg .v4 .$TYPE N<2>; .reg .b64 r2; ",
      "wgmma.mma_async.sync.aligned.m64n8k16.f16.f16.f16 {r00,r01},",
      ",r2,1,1,1,0;", 4, {"b32", "s32", "u32", "f32", "f16x2"},
      &WgmmaMmaAsyncDenseM64n8k16F16F16F16RegisterPlain::a, "sm_90a");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC24) {
  checkMatrixCohort<WgmmaMmaAsyncDenseM64n8k16F16F16F16SharedPlain>(
      ".reg .v2 .$TYPE N<2>; .reg .b64 r1; .reg .b64 r2; ",
      "wgmma.mma_async.sync.aligned.m64n8k16.f16.f16.f16 ", ",r1,r2,1,1,1,0,0;",
      2, {"b32", "s32", "u32", "f32", "f16x2"},
      &WgmmaMmaAsyncDenseM64n8k16F16F16F16SharedPlain::d, "sm_90a");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC25) {
  checkMatrixCohort<WgmmaMmaAsyncDenseM64n16k16F16F16F16SharedPlain>(
      ".reg .v4 .$TYPE N<2>; .reg .b64 r1; .reg .b64 r2; ",
      "wgmma.mma_async.sync.aligned.m64n16k16.f16.f16.f16 ",
      ",r1,r2,1,1,1,0,0;", 4, {"b32", "s32", "u32", "f32", "f16x2"},
      &WgmmaMmaAsyncDenseM64n16k16F16F16F16SharedPlain::d, "sm_90a");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC26) {
  checkMatrixCohort<WgmmaMmaAsyncDenseM64n8k16F32F16F16SharedPlain>(
      ".reg .v4 .$TYPE N<2>; .reg .b64 r1; .reg .b64 r2; ",
      "wgmma.mma_async.sync.aligned.m64n8k16.f32.f16.f16 ", ",r1,r2,1,1,1,0,0;",
      4, {"b32", "f32"}, &WgmmaMmaAsyncDenseM64n8k16F32F16F16SharedPlain::d,
      "sm_90a");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC27) {
  checkMatrixCohort<WgmmaMmaAsyncDenseM64n8k32S32S8S8SharedPlain>(
      ".reg .v4 .$TYPE N<2>; .reg .b64 r1; .reg .b64 r2; ",
      "wgmma.mma_async.sync.aligned.m64n8k32.s32.s8.s8 ", ",r1,r2,1;", 4,
      {"b32", "s32", "u32"}, &WgmmaMmaAsyncDenseM64n8k32S32S8S8SharedPlain::d,
      "sm_90a");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC28) {
  checkMatrixCohort<WmmaMmaM16n16k16RowRowS32S8S8S32>(
      ".reg .s32 r0<8>; .reg .v2 .$TYPE N<2>; .reg .b32 r2<2>; .reg .s32 "
      "r3<8>; ",
      "wmma.mma.sync.aligned.row.row.m16n16k16.s32.s8.s8.s32 "
      "{r00,r01,r02,r03,r04,r05,r06,r07},",
      ",{r20,r21},{r30,r31,r32,r33,r34,r35,r36,r37};", 2,
      {"b32", "s32", "u32", "f32", "f16x2"},
      &WmmaMmaM16n16k16RowRowS32S8S8S32::a, "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC29) {
  checkMatrixCohort<WmmaMmaM16n16k16RowRowF32Bf16Bf16F32>(
      ".reg .f32 r0<8>; .reg .v4 .$TYPE N<2>; .reg .b32 r2<4>; .reg .f32 "
      "r3<8>; ",
      "wmma.mma.sync.aligned.row.row.m16n16k16.f32.bf16.bf16.f32 "
      "{r00,r01,r02,r03,r04,r05,r06,r07},",
      ",{r20,r21,r22,r23},{r30,r31,r32,r33,r34,r35,r36,r37};", 4,
      {"b32", "s32", "u32", "f32", "f16x2"},
      &WmmaMmaM16n16k16RowRowF32Bf16Bf16F32::a, "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC30) {
  checkMatrixCohort<WmmaMmaM16n16k16RowRowS32S8S8S32>(
      ".reg .s32 r0<8>; .reg .b32 r1<2>; .reg .v2 .$TYPE N<2>; .reg .s32 "
      "r3<8>; ",
      "wmma.mma.sync.aligned.row.row.m16n16k16.s32.s8.s8.s32 "
      "{r00,r01,r02,r03,r04,r05,r06,r07},{r10,r11},",
      ",{r30,r31,r32,r33,r34,r35,r36,r37};", 2,
      {"b32", "s32", "u32", "f32", "f16x2"},
      &WmmaMmaM16n16k16RowRowS32S8S8S32::b, "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC31) {
  checkMatrixCohort<WmmaMmaM16n16k16RowRowF32Bf16Bf16F32>(
      ".reg .f32 r0<8>; .reg .b32 r1<4>; .reg .v4 .$TYPE N<2>; .reg .f32 "
      "r3<8>; ",
      "wmma.mma.sync.aligned.row.row.m16n16k16.f32.bf16.bf16.f32 "
      "{r00,r01,r02,r03,r04,r05,r06,r07},{r10,r11,r12,r13},",
      ",{r30,r31,r32,r33,r34,r35,r36,r37};", 4,
      {"b32", "s32", "u32", "f32", "f16x2"},
      &WmmaMmaM16n16k16RowRowF32Bf16Bf16F32::b, "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC32) {
  checkMatrixCohort<WmmaMmaM16n16k16RowRowF16F16F16F16>(
      ".reg .f16x2 r0<4>; .reg .f16x2 r1<8>; .reg .f16x2 r2<8>; .reg .v4 "
      ".$TYPE N<2>; ",
      "wmma.mma.sync.aligned.row.row.m16n16k16.f16.f16 "
      "{r00,r01,r02,r03},{r10,r11,r12,r13,r14,r15,r16,r17},{r20,r21,r22,r23,"
      "r24,r25,r26,r27},",
      ";", 4, {"b32", "f16x2"}, &WmmaMmaM16n16k16RowRowF16F16F16F16::c,
      "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC33) {
  checkMatrixCohort<WmmaMmaM8n8k4RowRowF64F64F64F64>(
      ".reg .f64 r0<2>; .reg .f64 r1<1>; .reg .f64 r2<1>; .reg .v2 .$TYPE "
      "N<2>; ",
      "wmma.mma.sync.aligned.row.row.m8n8k4.f64.f64.f64.f64 "
      "{r00,r01},{r10},{r20},",
      ";", 2, {"b64", "f64"}, &WmmaMmaM8n8k4RowRowF64F64F64F64::c, "sm_80");
}

/** Matched source and current ownership checks for one physical role cohort. */
TEST(MatrixNamedFragments, MNAMEDC34) {
  checkMatrixCohort<WmmaMmaM8n8k32RowColS32S4S4S32>(
      ".reg .s32 r0<2>; .reg .b32 r1<1>; .reg .b32 r2<1>; .reg .v2 .$TYPE "
      "N<2>; ",
      "wmma.mma.sync.aligned.row.col.m8n8k32.s32.s4.s4.s32 "
      "{r00,r01},{r10},{r20},",
      ";", 2, {"b32", "s32", "u32"}, &WmmaMmaM8n8k32RowColS32S4S4S32::c,
      "sm_80");
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir {
namespace {
/** Packed-half physical carriers retain their narrower type set beside a valid bit control. */
TEST(MatrixNamedFragments,
     PackedHalfDoesNotGainSignedUnsignedOrFloatPhysicalLanes) {
  const std::string prefix =
      "mma.sync.aligned.m16n8k8.row.col.f32.f16.f16.f32 "
      "{%d0,%d1,%d2,%d3},";
  const std::string suffix = ",{%b},{%c0,%c1,%c2,%c3};";
  const std::string common = ".reg .f32 %d<4>,%c<4>; .reg .b32 %b; ";
  ASSERT_TRUE(
      ownedNamedMatrix(common + ".reg .v2 .b32 A;", prefix + "A" + suffix));
  for (const auto type : {"s32", "u32", "f32"}) {
    SCOPED_TRACE(type);
    const auto declarations =
        common + ".reg .v2 ." + type + " A; .reg ." + type + " %a<2>;";
    EXPECT_FALSE(ownedNamedMatrix(declarations, prefix + "A" + suffix));
    EXPECT_FALSE(ownedNamedMatrix(declarations, prefix + "{%a0,%a1}" + suffix));
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
