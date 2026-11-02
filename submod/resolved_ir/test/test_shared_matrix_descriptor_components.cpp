#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include "test_syntax_parse_helpers.hpp"
namespace ptx_frontend::resolved_ir {
namespace {
/** The six already existing dense/sparse shared scalar carrier positions. */
enum class DescriptorRole {
  DenseSharedA,
  DenseSharedB,
  DenseRegisterB,
  SparseSharedA,
  SparseSharedB,
  SparseRegisterB
};
constexpr std::array descriptor_roles{
    DescriptorRole::DenseSharedA,   DescriptorRole::DenseSharedB,
    DescriptorRole::DenseRegisterB, DescriptorRole::SparseSharedA,
    DescriptorRole::SparseSharedB,  DescriptorRole::SparseRegisterB};

/** Build a canonical legal operation with only the selected scalar role replaced. */
std::string descriptorOperation(DescriptorRole role, std::string_view operand) {
  const bool sparse = role == DescriptorRole::SparseSharedA ||
                      role == DescriptorRole::SparseSharedB ||
                      role == DescriptorRole::SparseRegisterB;
  const bool registers = role == DescriptorRole::DenseRegisterB ||
                         role == DescriptorRole::SparseRegisterB;
  const bool a_role = role == DescriptorRole::DenseSharedA ||
                      role == DescriptorRole::SparseSharedA;
  const std::string a = registers ? "{a0,a1,a2,a3}"
                        : a_role  ? std::string(operand)
                                  : "descriptor";
  const std::string b = a_role ? "descriptor" : std::string(operand);
  if (sparse)
    return "wgmma.mma_async.sp.sync.aligned.m64n8k64.s32.s8.u8 {s0,s1,s2,s3}," +
           a + "," + b + ",meta,0,1;";
  return "wgmma.mma_async.sync.aligned.m64n8k8.f32.tf32.tf32 {d0,d1,d2,d3}," +
         a + "," + b + ",1,1,-1;";
}

/** Preserve a real complete profile and independent physical register declarations. */
std::string descriptorSource(DescriptorRole role, std::string_view declaration,
                             std::string_view operand) {
  return ".version 9.3\n.target sm_90a\n.address_size 64\n.entry k(){"
         ".reg .f32 d<4>; .reg .s32 s<4>; .reg .b32 a<4>,meta;"
         ".reg .b64 descriptor; .reg .v2 .b64 Z; " +
         std::string(declaration) + descriptorOperation(role, operand) + "}";
}

/** Return owned IR after parser/AST/input death; every returned error is nonempty.
 * The by-value source owns the real parser buffer until resolution completes.
 * Fixture setup failures throw descriptively before callers can inspect an absent diagnostic.
 */
std::expected<ResolvedModule, ModuleResolveDiagnostics> descriptorOwned(
    std::string source) {
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    throw std::runtime_error(
        "Shared descriptor component fixture did not parse cleanly.");
  auto result = resolveAndValidateModule(*ast);
  if (!result && result.error().empty())
    throw std::runtime_error(
        "Shared descriptor component fixture resolution failed without "
        "diagnostics.");
  return result;
}

/** Borrow the selected field of the actual resolved concrete role, without casting away const. */
WithLocs<ResolvedSharedMatrixDescriptor>& descriptorField(
    ResolvedModule& module, DescriptorRole role) {
  auto& instruction = *module.functions.front().body.front();
  switch (role) {
    case DescriptorRole::DenseSharedA:
      return dynamic_cast<WgmmaMmaAsyncDenseM64n8k8F32Tf32Tf32SharedPlain&>(
                 instruction)
          .a_desc;
    case DescriptorRole::DenseSharedB:
      return dynamic_cast<WgmmaMmaAsyncDenseM64n8k8F32Tf32Tf32SharedPlain&>(
                 instruction)
          .b_desc;
    case DescriptorRole::DenseRegisterB:
      return dynamic_cast<WgmmaMmaAsyncDenseM64n8k8F32Tf32Tf32RegisterPlain&>(
                 instruction)
          .b_desc;
    case DescriptorRole::SparseSharedA:
      return dynamic_cast<WgmmaMmaAsyncSpM64n8k64S32S8U8SharedPlain&>(
                 instruction)
          .a_desc;
    case DescriptorRole::SparseSharedB:
      return dynamic_cast<WgmmaMmaAsyncSpM64n8k64S32S8U8SharedPlain&>(
                 instruction)
          .b_desc;
    case DescriptorRole::SparseRegisterB:
      return dynamic_cast<WgmmaMmaAsyncSpM64n8k64S32S8U8RegisterPlain&>(
                 instruction)
          .b_desc;
  }
  throw std::logic_error("Unknown descriptor role.");
}

/** Inspect actual pointer/component/range joins synchronously; no callback borrows escape. */
struct DescriptorObserver final : detail::IReferenceObserver {
  /** Current immutable field owner lives through this visit. */
  const WithLocs<ResolvedSharedMatrixDescriptor>& owner;
  /** Copied callback count only, never an incoming pointer/span. */
  size_t calls = 0;
  /** Bind one selected actual field before visiting. */
  explicit DescriptorObserver(
      const WithLocs<ResolvedSharedMatrixDescriptor>& value)
      : owner(value) {}
  /** The existing callback retains the real RegRef through its actual descriptor payload. */
  void shared_matrix_descriptor(
      const ResolvedSharedMatrixDescriptor& value,
      std::span<const SourceRange> ranges,
      checker::AddressSymbolResolutionPolicy) override {
    if (&value != &owner.value)
      return;
    ++calls;
    EXPECT_EQ(&value.register_ref, &owner.value.register_ref);
    ASSERT_EQ(ranges.size(), 1u);
    ASSERT_EQ(owner.locs.size(), 1u);
    EXPECT_EQ(ranges.front(), owner.locs.front());
    const auto& ref = value.register_ref;
    ASSERT_TRUE(ref.symbol_id);
    ASSERT_TRUE(ref.component);
    EXPECT_EQ(ref.register_class, ResolvedRegisterClass::General);
    EXPECT_FALSE(ref.vector_width);
    EXPECT_EQ(ref.component->origin, RegisterComponentOrigin::ExplicitSelector);
    EXPECT_EQ(ref.component->declaration_width, 2u);
    EXPECT_EQ(ref.component->range, ranges.front());
    EXPECT_TRUE(valid_register_component(ref));
  }
};

/** Reacquire each copied owner's actual component and validate its containing module. */
void expectDescriptorOwner(ResolvedModule& module, DescriptorRole role) {
  auto& field = descriptorField(module, role);
  DescriptorObserver observer(field);
  module.functions.front().body.front()->visit_references(observer);
  EXPECT_EQ(observer.calls, 1u);
  EXPECT_TRUE(validateModule(module));
}

/** All six existing descriptor roles use scalar lanes, with xy/rg and indexed-family parity. */
TEST(SharedMatrixDescriptorComponents,
     CoversSixRolesSelectorsAndPhysicalIntegerBitWords) {
  for (const auto role : descriptor_roles) {
    for (const auto carrier : {"b64", "s64", "u64"}) {
      const std::string scalar = ".reg ." + std::string(carrier) + " A;";
      ASSERT_TRUE(descriptorOwned(descriptorSource(role, scalar, "A")));
      const std::string vector = ".reg .v2 ." + std::string(carrier) + " A;";
      for (const auto selector : {".x", ".y", ".r", ".g"}) {
        const auto source =
            descriptorSource(role, vector, "A" + std::string(selector));
        const auto ast = test_helpers::parseModule(source);
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        auto owner = descriptorOwned(source);
        ASSERT_TRUE(owner) << owner.error().front().message;
        EXPECT_TRUE(validateModule(*ast, *owner));
        const auto& ref = descriptorField(*owner, role).value.register_ref;
        ASSERT_TRUE(ref.component);
        ASSERT_TRUE(ref.component->selector);
        EXPECT_EQ(ref.component->lane, ordinary_register_lane(selector));
        EXPECT_EQ(ref.component->base_spelling, "A");
        EXPECT_FALSE(ref.parameterized_index);
        expectDescriptorOwner(*owner, role);
        auto copy = *owner;
        ResolvedModule assigned;
        assigned = copy;
        auto function = assigned.functions.front();
        assigned.functions.front() = function;
        assigned.functions.front().body.front() =
            function.body.front()->clone();
        expectDescriptorOwner(copy, role);
        expectDescriptorOwner(assigned, role);
        auto moved = std::move(assigned);
        expectDescriptorOwner(moved, role);
      }
    }
    auto family =
        descriptorOwned(descriptorSource(role, ".reg .v2 .b64 A<2>;", "A1.x"));
    ASSERT_TRUE(family) << family.error().front().message;
    const auto& family_ref = descriptorField(*family, role).value.register_ref;
    ASSERT_TRUE(family_ref.component);
    ASSERT_TRUE(family_ref.component->selector);
    EXPECT_EQ(
        descriptorField(*family, role).value.register_ref.parameterized_index,
        1u);
    EXPECT_EQ(descriptorField(*family, role)
                  .value.register_ref.component->base_spelling,
              "A1");
    expectDescriptorOwner(*family, role);
    EXPECT_FALSE(
        descriptorOwned(descriptorSource(role, ".reg .b64 A<2>;", "A")));
    EXPECT_TRUE(
        descriptorOwned(descriptorSource(role, ".reg .b64 A<2>;", "A1")));
  }
}

/** Source admission does not loosen width/class, whole-container or missing-context rules. */
TEST(SharedMatrixDescriptorComponents,
     KeepsNonScalarAndUnknownSourceBoundaries) {
  for (const auto role : descriptor_roles) {
    for (const auto& [declaration, operand] :
         {std::pair{".reg .v2 .b64 A;", "A"},
          {".reg .v2 .b64 A;", "A.z"},
          {".reg .v2 .b64 A;", "A.w"},
          {".reg .b64 A;", "A.x"},
          {".reg .v2 .b64 A<2>;", "A2.x"},
          {".reg .v2 .f64 A;", "A.x"},
          {".reg .v2 .b32 A;", "A.x"},
          {".reg .v4 .b64 A;", "A.x"},
          {".reg .v2 .bf16x2 A;", "A.x"},
          {".reg .pred A;", "A"},
          {".reg .b64 A;", "%tid.x"},
          {".reg .b64 A;", "{A,A}"},
          {".reg .b64 A;", "[A]"},
          {".reg .b64 A;", "0"},
          {".reg .b64 A;", "_"}}) {
      const auto source = descriptorSource(role, declaration, operand);
      const auto ast = test_helpers::parseModule(source);
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      EXPECT_FALSE(resolveAndValidateModule(*ast));
      // A rejected source supplies no strict owner or component-lifetime success.
    }
    const auto instruction =
        test_helpers::parseInstruction(descriptorOperation(role, "A.x"));
    ASSERT_TRUE(instruction && instruction.diagnostics.empty());
    const auto unbound = resolveInstruction(*instruction);
    ASSERT_FALSE(unbound);
    // Record the actual first unbound-source reason; no scalar/vector facts are fabricated.
  }
}

/** Actual base facts and explicit selector/range metadata remain joined under both validators. */
TEST(SharedMatrixDescriptorComponents,
     RejectsStaleCurrentFactsAndPreservesRealRetargets) {
  for (const auto role : descriptor_roles) {
    const auto source = descriptorSource(role, ".reg .v2 .b64 A;", "A.x");
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto owner = descriptorOwned(source);
    ASSERT_TRUE(owner) << owner.error().front().message;
    for (unsigned mutation = 0; mutation < 16; ++mutation) {
      auto bad = *owner;
      auto& ref = descriptorField(bad, role).value.register_ref;
      ASSERT_TRUE(ref.component);
      ASSERT_TRUE(ref.component->selector);
      if (mutation == 0)
        ref.symbol_id.reset();
      if (mutation == 1)
        ref.symbol_id = bad.functions.front().symbol_id;
      if (mutation == 2)
        ref.declared_type = ScalarType::F64;
      if (mutation == 3)
        ref.register_class = ResolvedRegisterClass::Predicate;
      if (mutation == 4)
        ref.vector_width = 2;
      if (mutation == 5)
        ref.parameterized_index = 0;
      if (mutation == 6)
        ref.component->lane = 1;
      if (mutation == 7)
        ref.component->declaration_width = 4;
      if (mutation == 8)
        ref.component->selector->spelling = ".z";
      if (mutation == 9)
        ref.component->base_range = {{999, 1}, {999, 2}};
      if (mutation == 10)
        ref.component->origin = RegisterComponentOrigin::NamedProjection;
      if (mutation == 11)
        descriptorField(bad, role).locs.front() = {{999, 1}, {999, 2}};
      if (mutation == 12)
        ref.component->range = {{999, 1}, {999, 2}};
      if (mutation == 13)
        ref.component->selector->range = {{999, 1}, {999, 2}};
      if (mutation == 14)
        ref.spelling = "A.y";
      if (mutation == 15)
        ref.component->selector.reset();
      EXPECT_FALSE(validateModule(bad));
      EXPECT_FALSE(validateModule(*ast, bad));
      expectDescriptorOwner(*owner, role);
    }
    const auto other_source = descriptorSource(role, ".reg .v2 .b64 A;", "Z.y");
    auto other = descriptorOwned(other_source);
    ASSERT_TRUE(other) << other.error().front().message;
    auto retarget = *owner;
    descriptorField(retarget, role) = descriptorField(*other, role);
    expectDescriptorOwner(retarget, role);
    EXPECT_TRUE(validateModule(*ast, retarget));
    const auto changed = test_helpers::parseModule(other_source);
    ASSERT_MODULE_PARSE_SUCCEEDS(changed);
    ASSERT_TRUE(resolveAndValidateModule(*changed));
    const auto mismatch = validateModule(*changed, *owner);
    ASSERT_FALSE(mismatch);
    EXPECT_TRUE(std::any_of(
        mismatch.error().begin(), mismatch.error().end(), [](const auto& d) {
          return d.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
        }));
  }
}
/** Require the capacity diagnosis at the saved real component range, never an unrelated context error. */
void expectDescriptorCapacityFailure(const checker::CheckResult& result,
                                     SourceRange range) {
  ASSERT_FALSE(result);
  EXPECT_TRUE(std::any_of(
      result.error().begin(), result.error().end(),
      [&](const auto& diagnostic) {
        return diagnostic.kind ==
                   checker::CheckDiagnosticKind::ModuleSourceMismatch &&
               diagnostic.range == range &&
               diagnostic.message ==
                   "Selected shared matrix descriptor register owner exceeds "
                   "the physical vector width.";
      }));
}

/** Actual current declarations remain authoritative even when payload caches agree or disappear. */
TEST(SharedMatrixDescriptorComponents,
     RechecksCoherentSymbolTableAndCacheEdits) {
  for (const auto role : descriptor_roles) {
    const auto source = descriptorSource(role, ".reg .v2 .b64 A;", "A.x");
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto owner = descriptorOwned(source);
    ASSERT_TRUE(owner) << owner.error().front().message;
    for (unsigned mutation = 0; mutation < 6; ++mutation) {
      SCOPED_TRACE(mutation);
      SCOPED_TRACE(static_cast<unsigned>(role));
      auto bad = *owner;
      auto& ref = descriptorField(bad, role).value.register_ref;
      ASSERT_TRUE(ref.symbol_id);
      ASSERT_TRUE(ref.component);
      auto& symbol =
          const_cast<binding::Symbol&>(bad.symbols.symbol(*ref.symbol_id));
      if (mutation == 0) {
        symbol.type = ".f64";
        ref.declared_type = ScalarType::F64;
      }
      if (mutation == 1) {
        symbol.type = ".b32";
        ref.declared_type = ScalarType::B32;
      }
      if (mutation == 2) {
        symbol.vector_width = 4;
        ref.component->declaration_width = 4;
      }
      if (mutation == 3)
        symbol.is_array = true;
      if (mutation == 4) {
        symbol.type = ".f64";
        ref.declared_type.reset();
      }
      if (mutation == 5) {
        symbol.kind = binding::SymbolKind::Function;
        ref.declared_type.reset();
      }
      const auto current = validateModule(bad);
      const auto supplied = validateModule(*ast, bad);
      EXPECT_FALSE(current);
      EXPECT_FALSE(supplied);
      if (mutation == 2) {
        const auto saved_range = descriptorField(*owner, role).locs.front();
        expectDescriptorCapacityFailure(current, saved_range);
        expectDescriptorCapacityFailure(supplied, saved_range);
        symbol.vector_width = 2;
        ref.component->declaration_width = 2;
        EXPECT_TRUE(validateModule(bad));
        EXPECT_TRUE(validateModule(*ast, bad));
      }
      expectDescriptorOwner(*owner, role);
    }
  }
}
/** Missing availability context remains non-certifying and never hides known component failures. */
TEST(SharedMatrixDescriptorComponents, PartialContextKeepsKnownComponentFacts) {
  for (const auto role : descriptor_roles) {
    auto source = descriptorSource(role, ".reg .v2 .b64 A;", "A.x");
    source.erase(0, source.find(".entry"));
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto owner = resolveModuleOnly(*ast);
    ASSERT_TRUE(owner);
    EXPECT_TRUE(
        validateModule(*owner, ModuleValidationPolicy::AvailableContext));
    EXPECT_TRUE(
        validateModule(*ast, *owner, ModuleValidationPolicy::AvailableContext));
    EXPECT_FALSE(
        validateModule(*owner, ModuleValidationPolicy::RequireCompleteContext));
    auto& ref = descriptorField(*owner, role).value.register_ref;
    ASSERT_TRUE(ref.component);
    ASSERT_TRUE(ref.component->selector);
    ref.component->lane = 3;
    EXPECT_FALSE(
        validateModule(*owner, ModuleValidationPolicy::AvailableContext));
    EXPECT_FALSE(
        validateModule(*ast, *owner, ModuleValidationPolicy::AvailableContext));
    ref.component->lane = 0;
    auto& symbol =
        const_cast<binding::Symbol&>(owner->symbols.symbol(*ref.symbol_id));
    const auto saved_range = descriptorField(*owner, role).locs.front();
    symbol.vector_width = 4;
    ref.component->declaration_width = 4;
    expectDescriptorCapacityFailure(
        validateModule(*owner, ModuleValidationPolicy::AvailableContext),
        saved_range);
    expectDescriptorCapacityFailure(
        validateModule(*ast, *owner, ModuleValidationPolicy::AvailableContext),
        saved_range);
    symbol.vector_width = 2;
    ref.component->declaration_width = 2;
    EXPECT_TRUE(
        validateModule(*owner, ModuleValidationPolicy::AvailableContext));
    EXPECT_TRUE(
        validateModule(*ast, *owner, ModuleValidationPolicy::AvailableContext));
  }
}
/** Written over-capacity declarations fail before any validated owner can be retained. */
TEST(SharedMatrixDescriptorComponents,
     RejectsWrittenOverwideOwnersBeforeProducingIR) {
  for (const auto role : descriptor_roles) {
    const auto source = descriptorSource(role, ".reg .v4 .b64 A;", "A.x");
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    const auto result = resolveAndValidateModule(*ast);
    ASSERT_FALSE(result);
    EXPECT_TRUE(
        std::any_of(result.error().begin(), result.error().end(),
                    [](const auto& diagnostic) {
                      return diagnostic.stage() ==
                             ResolveDiagnosticStage::DeclarationSemantics;
                    }));
    // No owner exists here; this is independent of a healthy current-owner mutation.
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
