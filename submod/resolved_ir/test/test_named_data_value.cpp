#include <gtest/gtest.h>
#include <array>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <string>
#include <string_view>
#include "test_syntax_parse_helpers.hpp"
namespace ptx_frontend::resolved_ir {
namespace {
/** One legal data interpretation/geometry and its physical scalar declaration. */
struct NamedValueCase {
  /** Data placeholder is replaced without changing resource/coordinate legality. */
  std::string_view instruction;
  /** Fundamental carrier declaration for each lane. */
  std::string_view carrier;
  /** Declared and logical lane count. */
  uint8_t arity;
};
constexpr std::array named_value_cases{
    NamedValueCase{"sust.b.1d.v2.b8.trap [surf,x], $;", "b8", 2},
    NamedValueCase{"sust.b.2d.v4.b16.clamp [surf,{x,x}], $;", "u16", 4},
    NamedValueCase{"sust.p.3d.v4.b32.zero [surf,{x,x,x,x}], $;", "f32", 4},
    NamedValueCase{"sust.b.a1d.v2.b32.trap [surf,{x,x}], $;", "s32", 2},
};
/** Keep the legal resource and coordinates independent of the data source form. */
std::string named_value_source(const NamedValueCase& item,
                               std::string_view operand) {
  std::string instruction(item.instruction);
  instruction.replace(instruction.find('$'), 1, operand);
  return ".version 9.3\n.target sm_100\n.address_size 64\n.global .surfref "
         "surf;\n.entry kernel() { .reg .s32 x; .reg .v" +
         std::to_string(item.arity) + " ." + std::string(item.carrier) +
         " V<2>, W<2>; " + instruction + " }";
}
TEST(NamedDataValue, AdmitsNamedAlongsideBracesAndMixedValues) {
  for (const auto& item : named_value_cases) {
    SCOPED_TRACE(item.instruction);
    for (const auto operand :
         {item.arity == 2 ? "{V1.x,V1.y}" : "{V1.x,V1.y,V1.z,V1.w}",
          item.arity == 2 ? "{V1.r,1}" : "{V1.r,1,V1.b,3}", "V1"}) {
      SCOPED_TRACE(operand);
      const auto ast =
          test_helpers::parseModule(named_value_source(item, operand));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      const auto draft = resolveModuleOnly(*ast);
      EXPECT_TRUE(draft) << (draft ? "" : draft.error().front().message);
      const auto result = resolveAndValidateModule(*ast);
      EXPECT_TRUE(result) << (result ? "" : result.error().front().message);
      if (result) {
        EXPECT_TRUE(validateModule(*ast, *result));
        EXPECT_TRUE(validateModule(*result));
      }
    }
  }
}
/** Observe the whole actual value payload without extending its owner lifetime. */
struct NamedValueCapture final : detail::IReferenceObserver {
  /** Borrowed until the observed instruction is destroyed or changed. */
  const ResolvedValueVector* value = nullptr;
  /** Original lane locations borrowed from the same owning instruction. */
  std::span<const SourceRange> locations;
  /** Retain the source and original locations during synchronous inspection. */
  void value_vector(const ResolvedValueVector& payload,
                    std::span<const SourceRange> ranges,
                    checker::AddressSymbolResolutionPolicy) override {
    value = &payload;
    locations = ranges;
  }
};
/** Resolve a strict module and destroy its original AST and source storage. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> owned_named_value(
    const NamedValueCase& item, std::string_view operand = "V1") {
  const auto ast = test_helpers::parseModule(named_value_source(item, operand));
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveAndValidateModule(*ast);
}
/** A complete target for direct checks independent of retained AST storage. */
checker::Context named_value_context() {
  const auto profile = base::find_target_profile("sm_100");
  return {
      .target = {.ptx_version = {9, 3},
                 .sm_version = 100,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
}
/** Borrow an actual payload only while the caller retains its owning instruction. */
checker::OperandView named_value_view(const ResolvedValueVector& value,
                                      std::span<const SourceRange> ranges) {
  checker::OperandView view{.field_id = "data",
                            .actual_shape = checker::OperandShape::Vector,
                            .value_vector = &value,
                            .vector_arity = value.elements.size(),
                            .locations = ranges};
  for (size_t index = 0; index < value.elements.size(); ++index) {
    if (const auto* reg =
            std::get_if<ResolvedRegisterRef>(&value.elements[index])) {
      view.vector_element_shapes[index] = checker::OperandShape::Register;
      view.vector_element_types[index] =
          reg->declared_type.value_or(ScalarType::Invalid);
      view.vector_element_registers[index] = reg;
    } else {
      const auto& immediate =
          std::get<ResolvedImmediate>(value.elements[index]);
      view.vector_element_shapes[index] = checker::OperandShape::Immediate;
      view.vector_element_types[index] = immediate.type;
      view.vector_element_immediates[index] = &immediate;
      view.vector_immediate_bits[index] = immediate.bits;
      view.vector_immediate_source_bits[index] = immediate.integer_source_bits;
      view.vector_immediate_negative[index] = immediate.is_negative;
    }
  }
  return view;
}

TEST(NamedDataValue, OwnsSourceAfterAstDeathCopiesAndCoherentRetarget) {
  const auto context = named_value_context();
  for (const auto& item : named_value_cases) {
    SCOPED_TRACE(item.instruction);
    auto original = owned_named_value(item);
    ASSERT_TRUE(original) << original.error().front().message;
    ResolvedModule copied(*original), assigned;
    assigned = copied;
    ResolvedFunction function(copied.functions.front()), function_assigned;
    function_assigned = function;
    EXPECT_NE(function.body.front().get(),
              copied.functions.front().body.front().get());
    EXPECT_TRUE(function_assigned.body.front()->check(context));
    assigned.functions.front().body.front() = function.body.front()->clone();
    EXPECT_TRUE(validateModule(copied));
    EXPECT_TRUE(validateModule(assigned));
    NamedValueCapture capture;
    assigned.functions.front().body.front()->visit_references(capture);
    ASSERT_TRUE(capture.value && capture.value->source.whole_base);
    const auto& value = *capture.value;
    ASSERT_TRUE(valid_value_vector_source(value, capture.locations, true));
    EXPECT_EQ(value.source.kind, ResolvedVectorSourceKind::NamedVector);
    const auto& base = value.source.whole_base->value.register_ref;
    EXPECT_TRUE(base.symbol_id);
    EXPECT_EQ(base.spelling, "V1");
    EXPECT_EQ(base.parameterized_index, 1u);
    EXPECT_EQ(base.vector_width, item.arity);
    for (size_t index = 0; index < value.elements.size(); ++index) {
      const auto& lane = std::get<ResolvedRegisterRef>(value.elements[index]);
      ASSERT_TRUE(lane.component);
      EXPECT_EQ(lane.symbol_id, base.symbol_id);
      EXPECT_EQ(lane.parameterized_index, base.parameterized_index);
      EXPECT_EQ(lane.declared_type, base.declared_type);
      EXPECT_FALSE(lane.vector_width);
      EXPECT_EQ(lane.spelling, base.spelling);
      EXPECT_EQ(lane.component->origin,
                RegisterComponentOrigin::NamedProjection);
      EXPECT_EQ(lane.component->lane, index);
      EXPECT_EQ(lane.component->declaration_width, item.arity);
      EXPECT_EQ(lane.component->base_spelling, base.spelling);
      EXPECT_EQ(lane.component->base_range, value.source.range);
      EXPECT_EQ(lane.component->range, value.source.range);
      EXPECT_FALSE(lane.component->selector);
      EXPECT_EQ(capture.locations[index], value.source.range);
    }
    const auto ast = test_helpers::parseModule(named_value_source(item, "V1"));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    const auto replacement =
        test_helpers::parseModule(named_value_source(item, "W0"));
    ASSERT_MODULE_PARSE_SUCCEEDS(replacement);
    auto target = resolveAndValidateModule(*replacement);
    ASSERT_TRUE(target);
    NamedValueCapture target_capture;
    target->functions.front().body.front()->visit_references(target_capture);
    ASSERT_TRUE(target_capture.value);
    EXPECT_NE(
        base.symbol_id,
        target_capture.value->source.whole_base->value.register_ref.symbol_id);
    auto& changed = const_cast<ResolvedValueVector&>(value);
    changed = *target_capture.value;
    EXPECT_TRUE(assigned.functions.front().body.front()->check(context));
    EXPECT_TRUE(validateModule(assigned));
    // Retained syntax identity still matches; current typed refs are independently checked.
    EXPECT_TRUE(validateModule(*ast, assigned));
    const auto stale = validateModule(*replacement, assigned);
    ASSERT_FALSE(stale);
    EXPECT_EQ(stale.error().front().kind,
              checker::CheckDiagnosticKind::ModuleSourceMismatch);
  }
}

TEST(NamedDataValue, RejectsMalformedNamedProvenanceAndCopiedMutants) {
  const auto context = named_value_context();
  for (const auto& item : named_value_cases) {
    const auto original = owned_named_value(item);
    ASSERT_TRUE(original);
    for (int mutation = 0; mutation < 15; ++mutation) {
      SCOPED_TRACE(item.instruction);
      SCOPED_TRACE(mutation);
      auto changed = *original;
      NamedValueCapture capture;
      changed.functions.front().body.front()->visit_references(capture);
      ASSERT_TRUE(capture.value);
      auto& value = const_cast<ResolvedValueVector&>(*capture.value);
      auto& lane = std::get<ResolvedRegisterRef>(value.elements.front());
      switch (mutation) {
        case 0:
          value.source.whole_base.reset();
          break;
        case 1:
          value.source.range = {};
          break;
        case 2:
          value.source.kind = static_cast<ResolvedVectorSourceKind>(99);
          break;
        case 3:
          lane.symbol_id.reset();
          break;
        case 4:
          lane.parameterized_index = 0;
          break;
        case 5:
          lane.declared_type = ScalarType::B64;
          break;
        case 6:
          std::swap(value.elements[0], value.elements[1]);
          break;
        case 7:
          lane.component->lane = 1;
          break;
        case 8:
          lane.component->range = {};
          break;
        case 9:
          lane = value.source.whole_base->value.register_ref;
          break;
        case 10:
          value.elements[0] = ResolvedImmediate{};
          break;
        case 11:
          value.source.whole_base->value.register_ref.parameterized_index = 2;
          break;
        case 12:
          value.source.whole_base->locs.clear();
          break;
        case 13:
          value.source.kind = ResolvedVectorSourceKind::BraceList;
          value.source.whole_base.reset();
          break;
        case 14:
          lane.register_class = ResolvedRegisterClass::Predicate;
          break;
      }
      EXPECT_FALSE(changed.functions.front().body.front()->check(context));
      EXPECT_FALSE(validateModule(changed));
      ResolvedModule copy(changed), assigned;
      assigned = copy;
      assigned.functions.front().body.front() =
          copy.functions.front().body.front()->clone();
      EXPECT_FALSE(validateModule(copy));
      EXPECT_FALSE(validateModule(assigned));
    }
  }
}

TEST(NamedDataValue, RequiresMatchingActualPayloadAndExplicitPermission) {
  const auto& item = named_value_cases.front();
  const auto owned = owned_named_value(item);
  ASSERT_TRUE(owned);
  NamedValueCapture capture;
  owned->functions.front().body.front()->visit_references(capture);
  ASSERT_TRUE(capture.value);
  auto view = named_value_view(*capture.value, capture.locations);
  constexpr uint8_t arities[]{2};
  checker::OperandDescriptor descriptor{
      .target_field_id = "data",
      .type_expression = {.kind =
                              checker::OperandTypeExpressionKind::FixedScalar,
                          .fixed_scalar_type = ScalarType::B8},
      .role = checker::OperandRole::Source,
      .access = checker::OperandAccess::Read,
      .allowed_shapes = checker::OperandShape::Vector,
      .allowed_vector_arities = arities,
      .vector_type_policy = checker::VectorTypePolicy::Element,
      .source_value_vector = true};
  const auto check = [&] {
    return checker::check_operands(std::span{&descriptor, 1}, {},
                                   std::span{&view, 1}, {}, {});
  };
  EXPECT_FALSE(check());
  descriptor.allow_named_vector = true;
  EXPECT_TRUE(check());
  const auto valid = view;
  auto other = *capture.value;
  for (int mutation = 0; mutation < 7; ++mutation) {
    view = valid;
    switch (mutation) {
      case 0:
        view.value_vector = nullptr;
        break;
      case 1:
        view.value_vector = &other;
        break;
      case 2:
        view.vector_arity = 1;
        break;
      case 3:
        view.vector_element_registers[0] = view.vector_element_registers[1];
        break;
      case 4:
        view.vector_element_types[0] = ScalarType::F32;
        break;
      case 5:
        view.vector_element_shapes[0] = checker::OperandShape::Immediate;
        break;
      case 6:
        view.locations = {};
        break;
    }
    EXPECT_FALSE(check());
  }
  const auto brace = owned_named_value(item, "{V1.r,1}");
  ASSERT_TRUE(brace);
  NamedValueCapture mixed;
  brace->functions.front().body.front()->visit_references(mixed);
  ASSERT_TRUE(mixed.value);
  EXPECT_EQ(mixed.value->source.kind, ResolvedVectorSourceKind::BraceList);
  EXPECT_FALSE(mixed.value->source.whole_base);
  EXPECT_NE(mixed.value->source.range, SourceRange{});
  EXPECT_TRUE(valid_value_vector_source(*mixed.value, mixed.locations, true));
  view = named_value_view(*mixed.value, mixed.locations);
  descriptor.allow_named_vector = false;
  EXPECT_TRUE(check());
  descriptor.source_value_vector = false;
  const auto wrong_descriptor = check();
  ASSERT_FALSE(wrong_descriptor);
  EXPECT_EQ(wrong_descriptor.error().front().kind,
            checker::CheckDiagnosticKind::InvalidVectorOperand);
  descriptor.source_value_vector = true;
  EXPECT_TRUE(check());
  auto immediate_copy = std::get<ResolvedImmediate>(mixed.value->elements[1]);
  view.vector_element_immediates[1] = &immediate_copy;
  EXPECT_FALSE(check());
  view = named_value_view(*mixed.value, mixed.locations);
  view.vector_immediate_bits[1] = 0;
  EXPECT_FALSE(check());
  auto legacy = *mixed.value;
  legacy.source.range = {};
  EXPECT_TRUE(valid_value_vector_source(legacy, mixed.locations));
  EXPECT_FALSE(valid_value_vector_source(legacy, mixed.locations, true));
}

TEST(NamedDataValue, RejectsWrongDeclarationsAndRetainsLexicalBinding) {
  for (const auto& item : named_value_cases) {
    for (const auto operand :
         {"V1.x", "x", "surf", "missing", "V2", "%tid", "_"}) {
      const auto ast =
          test_helpers::parseModule(named_value_source(item, operand));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
    for (const auto declaration :
         {".reg .v2 .pred V<2>;", ".reg .v2 .bf16 V<2>;",
          ".reg .v4 .b64 V<2>;"}) {
      auto source = named_value_source(item, "V1");
      const auto start = source.find(".reg .v");
      source.replace(start, source.find(';', start) - start + 1, declaration);
      const auto ast = test_helpers::parseModule(source);
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
  }
  auto source = named_value_source(named_value_cases.front(), "V1");
  const auto end = source.rfind('}');
  source.insert(end,
                " { .reg .v2 .u8 V<2>; sust.b.1d.v2.b8.trap [surf,x],V1; }");
  const auto ast = test_helpers::parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  const auto module = resolveAndValidateModule(*ast);
  ASSERT_TRUE(module);
  NamedValueCapture outer, inner;
  module->functions.front().body[0]->visit_references(outer);
  module->functions.front().body[1]->visit_references(inner);
  ASSERT_TRUE(outer.value && inner.value);
  EXPECT_NE(outer.value->source.whole_base->value.register_ref.symbol_id,
            inner.value->source.whole_base->value.register_ref.symbol_id);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
