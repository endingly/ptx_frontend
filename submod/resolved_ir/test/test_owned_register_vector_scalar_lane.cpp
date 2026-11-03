#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Selected min-zero register-vector consumers, plus the protected value-vector path. */
struct ScalarLaneCase {
  /** Instruction with '$' identifying one scalar leaf. */
  std::string_view instruction;
  /** Existing value-vector branch is already protected and serves as a control. */
  bool value_vector = false;
};
constexpr std::array scalar_lane_cases{
    ScalarLaneCase{"st.async.weak.shared::cluster.mbarrier::complete_tx::bytes."
                   "v2.u32 [sd], {$,word0}, [bar];"},
    ScalarLaneCase{"st.async.weak.shared::cluster.mbarrier::complete_tx::bytes."
                   "v4.u32 [sd], {$,word0,word1,word2}, [bar];"},
    ScalarLaneCase{"st.async.shared::cluster.mbarrier::complete_tx::bytes.v2."
                   "u32 [sd], {$,word0}, [bar];"},
    ScalarLaneCase{"st.async.shared::cluster.mbarrier::complete_tx::bytes.v4."
                   "u32 [sd], {$,word0,word1,word2}, [bar];"},
    ScalarLaneCase{"suld.b.1d.v2.b32.trap {$,word0}, [surf,x];"},
    ScalarLaneCase{"multimem.ld_reduce.add.acc::f32.v2.f16x2 {$,word0}, [mm];"},
    ScalarLaneCase{"ldu.global.v2.u32 {$,word0}, [mm];"},
    ScalarLaneCase{"st.global.v2.u32 [mm], {$,word0};"},
    ScalarLaneCase{"sust.b.1d.v2.b32.trap [surf,x], {$,word0};", true},
    ScalarLaneCase{"mov.b64 packed, {$,word0};"},
    ScalarLaneCase{"mov.b64 {$,word0}, packed;"},
    ScalarLaneCase{"atom.global.add.v2.f32 {$,word0}, [mm], {word1,word2};"},
};
/** All carriers are B32: no widening or wrong-kind policy can mask the shape. */
std::string scalar_lane_source(std::string_view instruction,
                               std::string_view operand = "V1.y") {
  std::string body(instruction);
  if (const auto placeholder = body.find('$'); placeholder != std::string::npos)
    body.replace(placeholder, 1, operand);
  return ".version 9.3\n.target sm_100a\n.address_size 64\n"
         ".global .align 32 .b8 mm[256]; .global .surfref surf; "
         ".shared .align 8 .b64 bar; .entry kernel() { "
         ".reg .v2 .b32 V<2>; .reg .v4 .b32 Q<2>; .reg .b32 word<3>; .reg .b64 "
         "packed; "
         ".reg .u64 sd; .reg .s32 x; " +
         body + " }";
}
/** Return a strictly valid owned module after its original AST/source are destroyed. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> checked_scalar_lane(
    std::string source) {
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveAndValidateModule(*ast);
}
/** Observe scalar components inside existing register/value-vector payloads. */
struct ScalarLaneCapture final : detail::IReferenceObserver {
  /** Borrowed only while the observed instruction remains alive. */
  const ResolvedRegisterRef* leaf = nullptr;
  /** Original lane location, borrowed from the actual vector field. */
  std::span<const SourceRange> locations;
  /** Record the explicit register-vector lane without visiting a named root. */
  void register_vector(const ResolvedRegisterVector& value,
                       std::span<const SourceRange> ranges,
                       checker::AddressSymbolResolutionPolicy) override {
    if (!leaf && !value.elements.empty() && value.elements.front() &&
        value.elements.front()->component) {
      leaf = &*value.elements.front();
      locations = ranges;
    }
  }
  /** The existing value-vector scalar-shape check is an independent control. */
  void value_vector(const ResolvedValueVector& value,
                    std::span<const SourceRange> ranges,
                    checker::AddressSymbolResolutionPolicy) override {
    if (!leaf && !value.elements.empty()) {
      const auto* first =
          std::get_if<ResolvedRegisterRef>(&value.elements.front());
      if (first && first->component) {
        leaf = first;
        locations = ranges;
      }
    }
  }
};

TEST(OwnedRegisterVectorScalarLane,
     RejectsWholeVectorLeavesWithSameWidthAndRealIdentity) {
  const auto profile = base::find_target_profile("sm_100a");
  ASSERT_TRUE(profile);
  const checker::Context context{
      .target = {.ptx_version = {9, 3},
                 .sm_version = 100,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
  for (const auto& test : scalar_lane_cases) {
    SCOPED_TRACE(test.instruction);
    SCOPED_TRACE(test.value_vector ? "protected value-vector control"
                                   : "min-zero register-vector candidate");
    const auto ast =
        test_helpers::parseModule(scalar_lane_source(test.instruction));
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    ASSERT_TRUE(resolveAndValidateModule(*ast));
    const auto bad_source =
        test_helpers::parseModule(scalar_lane_source(test.instruction, "V1"));
    ASSERT_TRUE(bad_source && bad_source.diagnostics.empty());
    EXPECT_FALSE(resolveAndValidateModule(*bad_source));
    auto owned = checked_scalar_lane(scalar_lane_source(test.instruction));
    ASSERT_TRUE(owned) << owned.error().front().message;
    ResolvedModule copied(*owned);
    ResolvedModule assigned;
    assigned = copied;
    ResolvedFunction function(copied.functions.front());
    ResolvedFunction function_assigned;
    function_assigned = function;
    assigned.functions.front() = function_assigned;
    assigned.functions.front().body.front() =
        assigned.functions.front().body.front()->clone();
    ASSERT_TRUE(validateModule(assigned));
    ScalarLaneCapture capture;
    assigned.functions.front().body.front()->visit_references(capture);
    ASSERT_NE(capture.leaf, nullptr);
    ASSERT_FALSE(capture.locations.empty());
    auto& ref = const_cast<ResolvedRegisterRef&>(*capture.leaf);
    ASSERT_TRUE(ref.symbol_id);
    ASSERT_TRUE(ref.component);
    EXPECT_EQ(ref.parameterized_index, 1);
    EXPECT_EQ(ref.component->lane, 1);
    EXPECT_EQ(ref.component->origin, RegisterComponentOrigin::ExplicitSelector);
    EXPECT_EQ(ref.component->range, capture.locations.front());
    EXPECT_EQ(ref.declared_type, ScalarType::B32);
    EXPECT_FALSE(ref.vector_width);
    const auto original = ref;
    const auto symbol = *ref.symbol_id;
    EXPECT_EQ(assigned.symbols.symbol(symbol).name, "V");
    ref.component.reset();
    ref.vector_width = 2;
    ref.spelling = "V1";
    EXPECT_EQ(ref.symbol_id, symbol);
    EXPECT_EQ(ref.parameterized_index, 1);
    EXPECT_EQ(ref.declared_type, ScalarType::B32);
    EXPECT_EQ(ref.register_class, ResolvedRegisterClass::General);
    const auto direct = assigned.functions.front().body.front()->check(context);
    ASSERT_FALSE(direct);
    ASSERT_EQ(direct.error().size(), 1u);
    EXPECT_EQ(direct.error().front().kind,
              test.value_vector
                  ? checker::CheckDiagnosticKind::OperandTypeMismatch
                  : checker::CheckDiagnosticKind::UnsupportedOperandShape);
    EXPECT_EQ(direct.error().front().range, capture.locations.front());
    EXPECT_FALSE(validateModule(*ast, assigned));
    const auto owned_check = validateModule(assigned);
    ASSERT_FALSE(owned_check);
    EXPECT_TRUE(
        std::ranges::none_of(owned_check.error(), [](const auto& diagnostic) {
          return diagnostic.kind ==
                 checker::CheckDiagnosticKind::ModuleSourceMismatch;
        }));
    auto bad_copy = assigned;
    ResolvedModule bad_assigned;
    bad_assigned = bad_copy;
    bad_assigned.functions.front().body.front() =
        bad_assigned.functions.front().body.front()->clone();
    EXPECT_FALSE(validateModule(bad_assigned));
    ref = original;
    EXPECT_TRUE(validateModule(assigned));
  }
}

TEST(OwnedRegisterVectorScalarLane,
     PreservesGenuineNamedRootsAndExplicitComponents) {
  for (const auto instruction :
       {"mov.b64 packed, V1;", "mov.b64 V1, packed;", "ld.v2.u32 V1, [mm];",
        "st.v2.u32 [mm], V1;", "ldu.global.v2.u32 V1, [mm];"}) {
    const auto ast = test_helpers::parseModule(scalar_lane_source(instruction));
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    ASSERT_TRUE(resolveAndValidateModule(*ast));
    auto owned = checked_scalar_lane(scalar_lane_source(instruction));
    ASSERT_TRUE(owned) << owned.error().front().message;
    auto copied = *owned;
    copied.functions.front().body.front() =
        copied.functions.front().body.front()->clone();
    EXPECT_TRUE(validateModule(copied));
  }
}
TEST(OwnedRegisterVectorScalarLane,
     DiagnosesChosenLaneAndMissingLocationFallback) {
  using namespace checker;
  const std::array<uint8_t, 1> arities{2};
  OperandDescriptor descriptor;
  descriptor.target_field_id = "src";
  descriptor.type_expression = {.kind = OperandTypeExpressionKind::FixedScalar,
                                .fixed_scalar_type = ScalarType::B64};
  descriptor.register_width_policy = base::ScalarTypeSizePolicy::SameWidth;
  descriptor.role = OperandRole::Source;
  descriptor.access = OperandAccess::Read;
  descriptor.allowed_shapes = OperandShape::Vector;
  descriptor.allowed_vector_arities = arities;
  descriptor.vector_type_policy = VectorTypePolicy::Aggregate;
  ResolvedRegisterRef scalar{.spelling = "r",
                             .register_class = ResolvedRegisterClass::General,
                             .declared_type = ScalarType::B32};
  ResolvedRegisterRef whole{.spelling = "V",
                            .register_class = ResolvedRegisterClass::General,
                            .declared_type = ScalarType::B32,
                            .vector_width = 2};
  OperandView operand;
  operand.field_id = "src";
  operand.actual_shape = OperandShape::Vector;
  operand.vector_arity = 2;
  operand.vector_element_types[0] = operand.vector_element_types[1] =
      ScalarType::B32;
  operand.vector_element_shapes[0] = operand.vector_element_shapes[1] =
      OperandShape::Register;
  operand.vector_element_registers[0] = &scalar;
  operand.vector_element_registers[1] = &whole;
  const std::array<SourceRange, 2> locations{SourceRange{{10, 2}, {10, 3}},
                                             SourceRange{{10, 5}, {10, 6}}};
  const SourceRange fallback{{10, 1}, {10, 8}};
  const Context context{.instruction_range = fallback};
  for (const bool located : {true, false}) {
    operand.locations = located ? std::span<const SourceRange>{locations}
                                : std::span<const SourceRange>{};
    const auto checked = check_operands(std::span{&descriptor, 1}, {},
                                        std::span{&operand, 1}, {}, context);
    ASSERT_FALSE(checked);
    ASSERT_EQ(checked.error().size(), 1u);
    EXPECT_EQ(checked.error().front().kind,
              CheckDiagnosticKind::UnsupportedOperandShape);
    EXPECT_EQ(checked.error().front().range, located ? locations[1] : fallback);
  }
  whole.vector_width.reset();
  EXPECT_TRUE(check_operands(std::span{&descriptor, 1}, {},
                             std::span{&operand, 1}, {}, context));
}

TEST(OwnedRegisterVectorScalarLane,
     PreservesSelectorAliasesAndOwnedSourceIdentity) {
  const auto& test = scalar_lane_cases[0];
  for (const auto component :
       {"V1.x", "V1.y", "V1.r", "V1.g", "Q1.z", "Q1.w", "Q1.b", "Q1.a"}) {
    auto owned =
        checked_scalar_lane(scalar_lane_source(test.instruction, component));
    ASSERT_TRUE(owned);
    auto copied = *owned;
    copied.functions.front().body.front() =
        copied.functions.front().body.front()->clone();
    EXPECT_TRUE(validateModule(copied));
    if (std::string_view(component).starts_with("Q")) {
      ScalarLaneCapture capture;
      copied.functions.front().body.front()->visit_references(capture);
      ASSERT_NE(capture.leaf, nullptr);
      auto& ref = const_cast<ResolvedRegisterRef&>(*capture.leaf);
      ref.component.reset();
      ref.vector_width = 4;
      ref.spelling = "Q1";
      const auto checked = validateModule(copied);
      ASSERT_FALSE(checked);
      EXPECT_TRUE(
          std::ranges::any_of(checked.error(), [](const auto& diagnostic) {
            return diagnostic.kind ==
                   checker::CheckDiagnosticKind::UnsupportedOperandShape;
          }));
      auto bad_copy = copied;
      bad_copy.functions.front().body.front() =
          bad_copy.functions.front().body.front()->clone();
      EXPECT_FALSE(validateModule(bad_copy));
    }
  }
  auto owned = checked_scalar_lane(scalar_lane_source(test.instruction));
  ASSERT_TRUE(owned);
  const auto replacement =
      test_helpers::parseModule(scalar_lane_source(test.instruction, "V1.x"));
  ASSERT_TRUE(replacement && replacement.diagnostics.empty());
  ASSERT_TRUE(resolveAndValidateModule(*replacement));
  const auto stale = validateModule(*replacement, *owned);
  ASSERT_FALSE(stale);
  ASSERT_EQ(stale.error().size(), 1u);
  EXPECT_EQ(stale.error().front().kind,
            checker::CheckDiagnosticKind::ModuleSourceMismatch);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
