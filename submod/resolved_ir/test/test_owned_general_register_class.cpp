#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/model/data_movement/ld.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/mov.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_descriptors.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Scalar byte load uses an unchanged accepted General-register data role. */
std::string general_class_source(std::string_view destination = "r") {
  return ".version 9.3\n.target sm_90\n.address_size 64\n"
         ".global .align 1 .b8 A = 0;\n.entry kernel(){\n"
         ".reg .b8 r; .reg .pred p,p1; .reg .u64 a;\n"
         "mov.pred p,1;\nmov.pred p1,p;\nmov.u64 a,A;\nld.global.b8 " +
         std::string(destination) + ",[a];\n}";
}
/** Destroy the original source text and AST before returning the owned baseline. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> owned_general_class() {
  const auto source = general_class_source();
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveAndValidateModule(*ast);
}
/** Direct checks receive the same legal modern profile as the strict fixture. */
const checker::Context kGeneralClassContext{
    .target = {.ptx_version = {9, 3}, .sm_version = 90}};

TEST(OwnedGeneralRegisterClass, AcceptedMetadataAndSourceControls) {
  const auto ast = test_helpers::parseModule(general_class_source());
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  const auto original = resolveAndValidateModule(*ast);
  ASSERT_TRUE(original) << original.error().front().message;
  EXPECT_TRUE(validateModule(*ast, *original));
  EXPECT_TRUE(validateModule(*original));
  ASSERT_EQ(original->functions.front().body.size(), 4u);
  const auto& load = dynamic_cast<const LdExplicitScalar&>(
      *original->functions.front().body.back());
  EXPECT_EQ(load.type.value, ScalarType::B8);
  EXPECT_EQ(load.dst.value.register_class, ResolvedRegisterClass::General);
  EXPECT_EQ(load.dst.value.declared_type, ScalarType::B8);
  for (const auto& instruction : original->functions.front().body)
    EXPECT_TRUE(instruction->check(kGeneralClassContext));
  const auto bad = test_helpers::parseModule(general_class_source("p"));
  ASSERT_MODULE_PARSE_SUCCEEDS(bad);
  const auto draft = resolveModuleOnly(*bad);
  ASSERT_FALSE(draft);
  ASSERT_FALSE(draft.error().empty());
  EXPECT_GT(draft.error().front().range.start.line, 0);
  EXPECT_GT(draft.error().front().range.start.column, 0);
  EXPECT_NE(draft.error().front().message.find("non-predicate"),
            std::string::npos);
  EXPECT_FALSE(resolveAndValidateModule(*bad));
}

/** Require a single class diagnostic at the real data operand or chosen lane. */
void expect_general_class_diagnostic(const checker::CheckResult& result,
                                     SourceRange range) {
  ASSERT_FALSE(result);
  ASSERT_EQ(result.error().size(), 1u);
  EXPECT_EQ(result.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedOperandShape);
  EXPECT_EQ(result.error().front().range, range);
  EXPECT_NE(result.error().front().message.find("General-register class"),
            std::string::npos);
}

TEST(OwnedGeneralRegisterClass, RejectsRealPredicateInScalarGeneralSlot) {
  auto owned = owned_general_class();
  ASSERT_TRUE(owned) << owned.error().front().message;
  auto& load =
      dynamic_cast<LdExplicitScalar&>(*owned->functions.front().body.back());
  const auto original = load.dst.value;
  const auto real =
      dynamic_cast<const MovPred&>(*owned->functions.front().body.front())
          .dst.value.register_ref;
  ASSERT_TRUE(real.symbol_id);
  EXPECT_EQ(real.register_class, ResolvedRegisterClass::Predicate);
  EXPECT_EQ(real.declared_type, ScalarType::Pred);
  EXPECT_EQ(real.spelling, "p");
  EXPECT_FALSE(real.vector_width);
  EXPECT_FALSE(real.component);
  EXPECT_FALSE(real.parameterized_index);
  ASSERT_EQ(load.dst.locs.size(), 1u);
  const auto range = load.dst.locs.front();
  EXPECT_GT(range.start.line, 0);
  EXPECT_GT(range.start.column, 0);
  EXPECT_EQ(range.end.column - range.start.column, 1);
  EXPECT_NE(original.symbol_id, real.symbol_id);
  load.dst.value = real;
  EXPECT_EQ(load.dst.locs.front(), range);
  EXPECT_EQ(load.dst.value, real);
  expect_general_class_diagnostic(load.check(kGeneralClassContext), range);
  expect_general_class_diagnostic(validateModule(*owned), range);
  ResolvedModule copy(*owned), assigned;
  assigned = copy;
  expect_general_class_diagnostic(validateModule(copy), range);
  expect_general_class_diagnostic(validateModule(assigned), range);
  ResolvedFunction function(copy.functions.front()), function_assigned;
  function_assigned = function;
  expect_general_class_diagnostic(
      function.body.back()->check(kGeneralClassContext), range);
  expect_general_class_diagnostic(
      function_assigned.body.back()->check(kGeneralClassContext), range);
  assigned.functions.front().body.back() = function.body.back()->clone();
  expect_general_class_diagnostic(validateModule(assigned), range);
  load.dst.value = original;
  EXPECT_TRUE(load.check(kGeneralClassContext));
  EXPECT_TRUE(validateModule(*owned));
  EXPECT_TRUE(owned->functions.front().body[0]->check(kGeneralClassContext));
  EXPECT_TRUE(owned->functions.front().body[1]->check(kGeneralClassContext));
}
/** Borrow General scalar or vector payloads only while the observed instruction lives. */
struct GeneralClassCapture final : detail::IReferenceObserver {
  /** Data register spelling for scalar observations. */
  std::string_view name = "r";
  /** Borrowed actual data reference or vector, never copied from a shape cache. */
  const ResolvedRegisterRef* reg_value = nullptr;
  const ResolvedRegisterVector* vector_value = nullptr;
  /** Borrowed operand ranges, valid while the visited instruction lives. */
  std::span<const SourceRange> locations;
  /** Retain the scalar data operand without confusing address/coordinate registers. */
  void reg(const ResolvedRegisterRef& value,
           std::span<const SourceRange> ranges,
           checker::AddressSymbolResolutionPolicy) override {
    if (value.spelling == name) {
      reg_value = &value;
      locations = ranges;
    }
  }
  /** Retain the full register-vector payload and per-lane locations. */
  void register_vector(const ResolvedRegisterVector& value,
                       std::span<const SourceRange> ranges,
                       checker::AddressSymbolResolutionPolicy) override {
    vector_value = &value;
    locations = ranges;
  }
};
/** Resolve a legal complete source and destroy both original text and AST. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> checked_class_source(
    std::string source) {
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveAndValidateModule(*ast);
}

TEST(OwnedGeneralRegisterClass, CoversSameWidthSurfaceAndByteVectorLeaf) {
  for (const bool vector : {false, true}) {
    auto source = general_class_source();
    if (vector) {
      source.replace(source.find(".reg .b8 r;"), 11, ".reg .b8 r,r1;");
      const auto start = source.find("ld.global.b8 r,[a]");
      source.replace(start, std::string_view("ld.global.b8 r,[a]").size(),
                     "ld.global.v2.b8 {r,r1},[a]");
    } else {
      source.insert(source.find(".entry"), ".global .surfref surf;\n");
      source.insert(source.find("mov.pred p,1"), ".reg .s32 x;\n");
      const auto start = source.find("ld.global.b8 r,[a]");
      source.replace(start, std::string_view("ld.global.b8 r,[a]").size(),
                     "suld.b.1d.b8.trap r,[surf,x]");
    }
    const auto valid_source = source;
    auto owned = checked_class_source(source);
    ASSERT_TRUE(owned) << owned.error().front().message;
    const auto real =
        dynamic_cast<const MovPred&>(*owned->functions.front().body.front())
            .dst.value.register_ref;
    GeneralClassCapture capture;
    owned->functions.front().body.back()->visit_references(capture);
    ASSERT_FALSE(capture.locations.empty());
    const auto range = capture.locations.front();
    if (vector) {
      ASSERT_TRUE(capture.vector_value);
      auto& value = const_cast<ResolvedRegisterVector&>(*capture.vector_value);
      ASSERT_EQ(value.elements.size(), 2u);
      value.elements[0] = real;
    } else {
      ASSERT_TRUE(capture.reg_value);
      const_cast<ResolvedRegisterRef&>(*capture.reg_value) = real;
    }
    expect_general_class_diagnostic(
        owned->functions.front().body.back()->check(kGeneralClassContext),
        range);
    expect_general_class_diagnostic(validateModule(*owned), range);
    auto copied = *owned;
    copied.functions.front().body.back() =
        owned->functions.front().body.back()->clone();
    expect_general_class_diagnostic(validateModule(copied), range);
    auto bad_source = valid_source;
    const auto marker = bad_source.find(vector ? "{r,r1}" : "trap r,");
    ASSERT_NE(marker, std::string::npos);
    const auto target = marker + (vector ? 1 : 5);
    bad_source[target] = 'p';
    const auto bad = test_helpers::parseModule(bad_source);
    ASSERT_MODULE_PARSE_SUCCEEDS(bad);
    EXPECT_FALSE(resolveAndValidateModule(*bad));
  }
}

TEST(OwnedGeneralRegisterClass,
     ReconcilesKnownClassFactsWithoutRequiringUnknownTypes) {
  const auto owned = owned_general_class();
  ASSERT_TRUE(owned);
  const auto& load = dynamic_cast<const LdExplicitScalar&>(
      *owned->functions.front().body.back());
  const auto real_pred =
      dynamic_cast<const MovPred&>(*owned->functions.front().body.front())
          .dst.value.register_ref;
  auto general = load.dst.value;
  checker::OperandDescriptor descriptor{
      .target_field_id = "data",
      .type_expression = {.kind =
                              checker::OperandTypeExpressionKind::FixedScalar,
                          .fixed_scalar_type = ScalarType::B8},
      .role = checker::OperandRole::Source,
      .access = checker::OperandAccess::Read,
      .allowed_shapes =
          checker::OperandShape::Register | checker::OperandShape::Immediate};
  checker::OperandView view{.field_id = "data",
                            .actual_shape = checker::OperandShape::Register,
                            .register_type = ScalarType::B8,
                            .register_class = ResolvedRegisterClass::General,
                            .register_ref = &general,
                            .locations = load.dst.locs};
  const auto check = [&] {
    return checker::check_operands(std::span{&descriptor, 1}, {},
                                   std::span{&view, 1}, {},
                                   kGeneralClassContext);
  };
  EXPECT_TRUE(check());
  const auto valid = view;
  for (int mutation = 0; mutation < 6; ++mutation) {
    SCOPED_TRACE(mutation);
    view = valid;
    auto altered = general;
    if (mutation == 0)
      view.register_ref =
          &real_pred;  // Forged General/B8 caches cannot conceal the real Predicate.
    if (mutation == 1)
      view.register_class = ResolvedRegisterClass::Predicate;
    if (mutation == 2)
      view.register_class = static_cast<ResolvedRegisterClass>(99);
    if (mutation == 3) {
      altered.register_class = static_cast<ResolvedRegisterClass>(99);
      view.register_ref = &altered;
    }
    if (mutation == 4) {
      altered.declared_type = ScalarType::Pred;
      view.register_ref = &altered;
    }
    if (mutation == 5) {
      view.register_ref = nullptr;
      view.register_type = ScalarType::Pred;
    }
    expect_general_class_diagnostic(check(), load.dst.locs.front());
  }
  view = valid;
  general.declared_type.reset();
  general.symbol_id.reset();
  view.register_type.reset();
  view.register_class.reset();
  EXPECT_TRUE(check());
  view.register_ref = nullptr;
  EXPECT_TRUE(
      check());  // Legacy direct views with no actual facts remain deferred.
  descriptor.role = checker::OperandRole::Destination;
  descriptor.access = checker::OperandAccess::Write;
  descriptor.allow_destination_sink = true;
  view.is_sink = true;
  EXPECT_TRUE(check());
}

TEST(OwnedGeneralRegisterClass, SharesFixedCardinalityAndFallbackLaneRules) {
  const auto owned = owned_general_class();
  ASSERT_TRUE(owned);
  const auto real_pred =
      dynamic_cast<const MovPred&>(*owned->functions.front().body.front())
          .dst.value.register_ref;
  const auto general = dynamic_cast<const LdExplicitScalar&>(
                           *owned->functions.front().body.back())
                           .dst.value;
  const auto ranges = owned->functions.front().instruction_ranges;
  checker::OperandDescriptor descriptor{
      .target_field_id = "tuple",
      .type_expression = {.kind =
                              checker::OperandTypeExpressionKind::FixedScalar,
                          .fixed_scalar_type = ScalarType::B8},
      .role = checker::OperandRole::Source,
      .access = checker::OperandAccess::Read,
      .allowed_shapes = checker::OperandShape::Vector,
      .minimum_elements = 2,
      .maximum_elements = 2,
      .allowed_element_shapes = checker::OperandShape::Register};
  checker::OperandView view{.field_id = "tuple",
                            .actual_shape = checker::OperandShape::Vector,
                            .vector_arity = 2,
                            .locations = ranges};
  for (size_t index = 0; index < 2; ++index) {
    view.vector_element_shapes[index] = checker::OperandShape::Register;
    view.vector_element_types[index] = ScalarType::B8;
    view.vector_element_registers[index] = &general;
  }
  const auto check = [&](const checker::Context& context) {
    return checker::check_operands(std::span{&descriptor, 1}, {},
                                   std::span{&view, 1}, {}, context);
  };
  EXPECT_TRUE(check(kGeneralClassContext));
  view.vector_element_registers[1] = &real_pred;
  expect_general_class_diagnostic(check(kGeneralClassContext), ranges[1]);
  view.locations = std::span{ranges}.first(1);
  expect_general_class_diagnostic(check(kGeneralClassContext), ranges.front());
  view.locations = {};
  auto context = kGeneralClassContext;
  context.instruction_range = ranges.back();
  expect_general_class_diagnostic(check(context), ranges.back());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
