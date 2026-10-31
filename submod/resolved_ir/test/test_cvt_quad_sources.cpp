#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/model/data_movement/cvt.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Written packed output and its exact typed destination identity. */
struct QuadFormat {
  /** PTX output suffix; the four input lanes remain logical F32. */
  std::string_view suffix;
  /** Expected generated modifier, independent of diagnostic strings. */
  ScalarType type;
};
constexpr std::array quad_formats{QuadFormat{"e4m3x4", ScalarType::E4m3x4},
                                  QuadFormat{"e5m2x4", ScalarType::E5m2x4},
                                  QuadFormat{"e2m1x4", ScalarType::E2m1x4},
                                  QuadFormat{"e2m3x4", ScalarType::E2m3x4},
                                  QuadFormat{"e3m2x4", ScalarType::E3m2x4}};
/** Initialize real scalar, whole-vector and compact-member declarations separately. */
std::string quad_source(const QuadFormat& format,
                        std::string_view source = "{a,b,c,e}") {
  return ".version 9.3\n.target sm_100a\n.entry kernel(){\n"
         ".reg .f32 a,b,c,e; .reg .b32 d,r; .reg .b16 h; "
         ".reg .v4 .f32 VF,VC<2>; .reg .v4 .b32 BF,BC<2>; "
         ".reg .v2 .f32 V2; .reg .v4 .u32 UI; .reg .v4 .b16 NB; "
         ".reg .v4 .f16 HF; .reg .b64 wide; .reg .pred p;\n"
         "mov.f32 a,1.0; mov.f32 b,2.0; mov.f32 c,3.0; mov.f32 e,4.0; mov.b32 "
         "r,0;\n"
         "mov.f32 VF.x,a; mov.f32 VF.y,b; mov.f32 VF.z,c; mov.f32 VF.w,e; "
         "mov.f32 VC1.x,a; mov.f32 VC1.y,b; mov.f32 VC1.z,c; mov.f32 VC1.w,e;\n"
         "mov.b32 BF.x,0x3f800000; mov.b32 BF.y,0x40000000; "
         "mov.b32 BF.z,0x40400000; mov.b32 BF.w,0x40800000; "
         "mov.b32 BC1.x,0x3f800000; mov.b32 BC1.y,0x40000000; "
         "mov.b32 BC1.z,0x40400000; mov.b32 BC1.w,0x40800000;\n"
         "cvt.rs.satfinite." +
         std::string(format.suffix) + ".f32 " +
         (format.type == ScalarType::E2m1x4 ? "h," : "d,") +
         std::string(source) + ",r;\n}";
}
/** Verify the selected concrete variant, output modifier and four-source layout. */
void expect_quad_form(const Instruction& instruction,
                      const QuadFormat& format) {
  if (format.type == ScalarType::E2m1x4) {
    const auto* form = dynamic_cast<const CvtRsSatfiniteF4x4F32*>(&instruction);
    ASSERT_NE(form, nullptr);
    EXPECT_EQ(CvtRsSatfiniteF4x4F32::dst_type, format.type);
    EXPECT_EQ(form->src.value.elements.size(), 4u);
  } else if (format.type == ScalarType::E4m3x4 ||
             format.type == ScalarType::E5m2x4) {
    const auto* form = dynamic_cast<const CvtRsSatfiniteF8x4F32*>(&instruction);
    ASSERT_NE(form, nullptr);
    EXPECT_EQ(form->dst_type.value, format.type);
    EXPECT_EQ(form->src.value.elements.size(), 4u);
  } else {
    const auto* form = dynamic_cast<const CvtRsSatfiniteF6x4F32*>(&instruction);
    ASSERT_NE(form, nullptr);
    EXPECT_EQ(form->dst_type.value, format.type);
    EXPECT_EQ(form->src.value.elements.size(), 4u);
  }
}
TEST(CvtQuadSources, AdmitsFloatingLanesBesideInitializedRegisterControls) {
  for (const auto& format : quad_formats) {
    SCOPED_TRACE(format.suffix);
    const auto control = test_helpers::parseModule(quad_source(format));
    ASSERT_MODULE_PARSE_SUCCEEDS(control);
    const auto valid = resolveAndValidateModule(*control);
    ASSERT_TRUE(valid) << valid.error().front().message;
    expect_quad_form(*valid->functions.front().body.back(), format);
    for (const auto literal :
         {"1.5", "1.0+0.5", "0f3fc00000", "0d3ff8000000000000"})
      for (size_t lane = 0; lane < 4; ++lane) {
        std::array<std::string_view, 4> values{"a", "b", "c", "e"};
        values[lane] = literal;
        const std::string source =
            "{" + std::string(values[0]) + "," + std::string(values[1]) + "," +
            std::string(values[2]) + "," + std::string(values[3]) + "}";
        const auto ast = test_helpers::parseModule(quad_source(format, source));
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        const auto draft = resolveModuleOnly(*ast);
        EXPECT_TRUE(draft) << (draft ? "" : draft.error().front().message);
        EXPECT_TRUE(resolveAndValidateModule(*ast));
        if (draft) {
          EXPECT_TRUE(validateModule(*ast, *draft));
          EXPECT_TRUE(validateModule(*draft));
        }
      }
  }
}
TEST(CvtQuadSources, AdmitsWholeDeclarationsAndCompactMembersSeparately) {
  for (const auto& format : quad_formats)
    for (const auto name : {"VF", "VC1", "BF", "BC1"}) {
      SCOPED_TRACE(format.suffix);
      SCOPED_TRACE(name);
      const auto ast = test_helpers::parseModule(quad_source(format, name));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      const auto draft = resolveModuleOnly(*ast);
      EXPECT_TRUE(draft) << (draft ? "" : draft.error().front().message);
      EXPECT_TRUE(resolveAndValidateModule(*ast));
    }
}
/** Borrow the concrete quad source only while its instruction remains alive. */
ResolvedValueVector& quad_values(Instruction& instruction) {
  if (auto* form = dynamic_cast<CvtRsSatfiniteF4x4F32*>(&instruction))
    return form->src.value;
  if (auto* form = dynamic_cast<CvtRsSatfiniteF8x4F32*>(&instruction))
    return form->src.value;
  return dynamic_cast<CvtRsSatfiniteF6x4F32&>(instruction).src.value;
}
/** Observe original lane ranges without extending the owning instruction's lifetime. */
struct QuadCapture final : detail::IReferenceObserver {
  /** Actual source and lane locations borrowed during synchronous inspection. */
  const ResolvedValueVector* value = nullptr;
  /** Original lane locations borrow from the same instruction as value. */
  std::span<const SourceRange> locations;
  /** Retain the actual payload association used by common checking. */
  void value_vector(const ResolvedValueVector& payload,
                    std::span<const SourceRange> ranges,
                    checker::AddressSymbolResolutionPolicy) override {
    value = &payload;
    locations = ranges;
  }
};
/** Resolve strict IR and destroy all parser, AST and source text storage on return. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> owned_quad(
    const QuadFormat& format, std::string_view operand) {
  const auto ast = test_helpers::parseModule(quad_source(format, operand));
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveAndValidateModule(*ast);
}
/** Use the exact architecture-specific profile rather than a numeric SM approximation. */
checker::Context quad_context() {
  const auto profile = base::find_target_profile("sm_100a");
  EXPECT_TRUE(profile);
  return {
      .target = {.ptx_version = {9, 3},
                 .sm_version = 100,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
}
/** Exercise independently owned module/function copies, assignments and instruction clones. */
void expect_quad_lifetime(const ResolvedModule& module, bool valid = true) {
  EXPECT_EQ(bool(validateModule(module)), valid);
  ResolvedModule copied(module), assigned;
  assigned = copied;
  EXPECT_EQ(bool(validateModule(copied)), valid);
  EXPECT_EQ(bool(validateModule(assigned)), valid);
  ResolvedFunction function(module.functions.front()), function_assigned;
  function_assigned = function;
  EXPECT_NE(function.body.back().get(),
            module.functions.front().body.back().get());
  EXPECT_EQ(bool(function.body.back()->check(quad_context())), valid);
  EXPECT_EQ(bool(function_assigned.body.back()->check(quad_context())), valid);
  assigned.functions.front().body.back() = function.body.back()->clone();
  EXPECT_EQ(bool(validateModule(assigned)), valid);
}
/** Borrow an actual payload only while the caller retains its owning instruction. */
checker::OperandView quad_view(const ResolvedValueVector& value,
                               std::span<const SourceRange> ranges) {
  checker::OperandView view{.field_id = "src",
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

TEST(CvtQuadSources, OwnsFloatingWordsAndNamedProjectionAfterAstDeath) {
  for (const auto& format : quad_formats) {
    SCOPED_TRACE(format.suffix);
    for (const auto source : {"{1.5,1.0+0.5,0f3fc00000,0d3ff8000000000000}",
                              "{0d3ff0000010000000,0d3ff0000010000001,"
                              "0f80000000,0d8000000000000000}"}) {
      auto module = owned_quad(format, source);
      ASSERT_TRUE(module) << module.error().front().message;
      auto& instruction = *module->functions.front().body.back();
      expect_quad_form(instruction, format);
      const auto& value = quad_values(instruction);
      EXPECT_EQ(value.source.kind, ResolvedVectorSourceKind::BraceList);
      EXPECT_FALSE(value.source.whole_base);
      EXPECT_NE(value.source.range, SourceRange{});
      const std::array<uint64_t, 4> words =
          std::string_view(source).starts_with("{1.5")
              ? std::array<uint64_t, 4>{0x3fc00000, 0x3fc00000, 0x3fc00000,
                                        0x3fc00000}
              : std::array<uint64_t, 4>{0x3f800000, 0x3f800001, 0x80000000,
                                        0x80000000};
      for (size_t lane = 0; lane < 4; ++lane) {
        const auto& immediate =
            std::get<ResolvedImmediate>(value.elements[lane]);
        EXPECT_EQ(immediate.type, ScalarType::F32);
        EXPECT_EQ(immediate.bits, words[lane]);
        EXPECT_FALSE(immediate.integer_source_bits);
        EXPECT_FALSE(immediate.is_negative);
      }
      expect_quad_lifetime(*module);
    }
    auto components = owned_quad(format, "{VC1.r,VC1.y,VC1.b,VC1.w}");
    ASSERT_TRUE(components);
    expect_quad_lifetime(*components);
    for (const auto name : {"VF", "VC1", "BF", "BC1"}) {
      auto module = owned_quad(format, name);
      ASSERT_TRUE(module) << module.error().front().message;
      const auto& value = quad_values(*module->functions.front().body.back());
      ASSERT_TRUE(value.source.whole_base);
      const auto& base = value.source.whole_base->value.register_ref;
      EXPECT_EQ(value.source.kind, ResolvedVectorSourceKind::NamedVector);
      EXPECT_EQ(base.vector_width, 4u);
      EXPECT_TRUE(base.symbol_id);
      EXPECT_EQ(base.spelling, name);
      EXPECT_EQ(base.parameterized_index, std::string_view(name).ends_with('1')
                                              ? std::optional<uint32_t>(1)
                                              : std::nullopt);
      for (size_t lane = 0; lane < 4; ++lane) {
        const auto& ref = std::get<ResolvedRegisterRef>(value.elements[lane]);
        ASSERT_TRUE(ref.component);
        EXPECT_EQ(ref.symbol_id, base.symbol_id);
        EXPECT_EQ(ref.parameterized_index, base.parameterized_index);
        EXPECT_EQ(ref.declared_type, base.declared_type);
        EXPECT_FALSE(ref.vector_width);
        EXPECT_EQ(ref.component->origin,
                  RegisterComponentOrigin::NamedProjection);
        EXPECT_EQ(ref.component->lane, lane);
        EXPECT_EQ(ref.component->declaration_width, 4u);
        EXPECT_EQ(ref.component->base_range, value.source.range);
        EXPECT_FALSE(ref.component->selector);
      }
      expect_quad_lifetime(*module);
    }
  }
}
TEST(CvtQuadSources, KeepsScalarCategoriesShapesRandomBitsAndAvailability) {
  for (const auto& format : quad_formats) {
    for (const auto operand :
         {"{1,b,c,e}", "{(1.0<2.0),b,c,e}", "{+0f3fc00000,b,c,e}",
          "{-0f3fc00000,b,c,e}", "{(0f3fc00000),b,c,e}",
          "{0f3fc00000+1.0,b,c,e}", "{1?1.0:0f3fc00000,b,c,e}", "{_,b,c,e}",
          "{VF,b,c,e}", "{a,b,c}", "{a,b,c,e,a}", "V2", "UI", "NB", "HF",
          "VF.x", "a", "missing", "_"}) {
      SCOPED_TRACE(operand);
      const auto ast = test_helpers::parseModule(quad_source(format, operand));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
    const auto nested =
        test_helpers::parseModule(quad_source(format, "{{a,b},c,e}"));
    EXPECT_TRUE(!nested || !nested.diagnostics.empty());
    for (const auto rbits : {"0", "0f3f800000", "h", "VF"}) {
      auto source = quad_source(format);
      source.replace(source.rfind(",r;"), 3, "," + std::string(rbits) + ";");
      const auto ast = test_helpers::parseModule(source);
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
    for (const auto rbits : {"wide", "a", "r"}) {
      auto source = quad_source(format, "{1.5,b,c,e}");
      source.replace(source.rfind(",r;"), 3, "," + std::string(rbits) + ";");
      const auto ast = test_helpers::parseModule(source);
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      EXPECT_TRUE(resolveAndValidateModule(*ast));
    }
    for (const auto edit : {"sm_90", "sm_100"}) {
      auto source = quad_source(format);
      source.replace(source.find("sm_100a"), 7, edit);
      const auto ast = test_helpers::parseModule(source);
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
    PtxSyntaxParser standalone(
        "cvt.rs.satfinite." + std::string(format.suffix) + ".f32 " +
        (format.type == ScalarType::E2m1x4 ? "h," : "d,") + "VC1,r;");
    const auto instruction = standalone.parseInstruction();
    ASSERT_TRUE(instruction);
    EXPECT_FALSE(resolveInstruction(*instruction));
    auto source = quad_source(format);
    source.replace(source.find("9.3"), 3, "8.6");
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}
TEST(CvtQuadSources,
     SeparatesCoherentRetargetsFromOwnedProvenanceAndSourceIdentity) {
  for (const auto& format : quad_formats) {
    const auto ast = test_helpers::parseModule(quad_source(format, "VC1"));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto module = owned_quad(format, "VC1");
    auto replacement = owned_quad(format, "BC1");
    ASSERT_TRUE(module && replacement);
    auto& value = quad_values(*module->functions.front().body.back());
    value = quad_values(*replacement->functions.front().body.back());
    EXPECT_TRUE(module->functions.front().body.back()->check(quad_context()));
    EXPECT_TRUE(validateModule(*ast, *module));
    expect_quad_lifetime(*module);
    const auto changed_ast =
        test_helpers::parseModule(quad_source(format, "BF"));
    ASSERT_MODULE_PARSE_SUCCEEDS(changed_ast);
    ASSERT_TRUE(resolveAndValidateModule(*changed_ast));
    const auto mismatch = validateModule(*changed_ast, *module);
    ASSERT_FALSE(mismatch);
    EXPECT_EQ(mismatch.error().front().kind,
              checker::CheckDiagnosticKind::ModuleSourceMismatch);
    const ResolvedModule saved(*module);
    for (unsigned mutation = 0; mutation < 13; ++mutation) {
      ResolvedModule invalid(saved);
      auto& changed = quad_values(*invalid.functions.front().body.back());
      auto& lane = std::get<ResolvedRegisterRef>(changed.elements.front());
      auto& base = changed.source.whole_base->value.register_ref;
      switch (mutation) {
        case 0:
          changed.source.whole_base.reset();
          break;
        case 1:
          base.symbol_id.reset();
          break;
        case 2:
          base.parameterized_index = 0;
          break;
        case 3:
          lane.parameterized_index = 0;
          break;
        case 4:
          lane.component->lane = 1;
          break;
        case 5:
          lane.component->origin = RegisterComponentOrigin::ExplicitSelector;
          break;
        case 6:
          changed.source.range = {};
          break;
        case 7:
          lane.declared_type = ScalarType::U32;
          break;
        case 8:
          std::swap(changed.elements[0], changed.elements[1]);
          break;
        case 9:
          lane.component.reset();
          lane.vector_width = 4;
          break;
        case 10:
          changed.elements[0] =
              ResolvedImmediate{.bits = 0x3fc00000, .type = ScalarType::F32};
          break;
        case 11:
          changed.source.kind = static_cast<ResolvedVectorSourceKind>(255);
          break;
        case 12:
          lane.component->base_range = {};
          break;
      }
      expect_quad_lifetime(invalid, false);
    }
  }
}
TEST(CvtQuadSources, RejectsNoncanonicalOwnedImmediateWords) {
  auto saved = owned_quad(quad_formats.front(), "{1.5,b,c,e}");
  ASSERT_TRUE(saved);
  for (unsigned mutation = 0; mutation < 4; ++mutation) {
    ResolvedModule invalid(*saved);
    auto& immediate = std::get<ResolvedImmediate>(
        quad_values(*invalid.functions.front().body.back()).elements[0]);
    switch (mutation) {
      case 0:
        immediate.bits |= uint64_t{1} << 32;
        break;
      case 1:
        immediate.integer_source_bits = immediate.bits;
        break;
      case 2:
        immediate.is_negative = true;
        break;
      case 3:
        immediate.type = ScalarType::B32;
        break;
    }
    expect_quad_lifetime(invalid, false);
  }
}
TEST(CvtQuadSources, RequiresTheActualPayloadProjectionAndNamedPermission) {
  for (const auto operand : {"VC1", "{a,1.5,c,e}"}) {
    auto module = owned_quad(quad_formats.front(), operand);
    ASSERT_TRUE(module);
    QuadCapture capture;
    module->functions.front().body.back()->visit_references(capture);
    ASSERT_TRUE(capture.value);
    auto view = quad_view(*capture.value, capture.locations);
    constexpr uint8_t arities[]{4};
    checker::OperandDescriptor descriptor{
        .target_field_id = "src",
        .type_expression = {.kind =
                                checker::OperandTypeExpressionKind::FixedScalar,
                            .fixed_scalar_type = ScalarType::F32},
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
    EXPECT_EQ(bool(check()), std::string_view(operand) != "VC1");
    descriptor.allow_named_vector = true;
    EXPECT_TRUE(check());
    const auto saved = view;
    auto other = *capture.value;
    for (unsigned mutation = 0; mutation < 11; ++mutation) {
      view = saved;
      switch (mutation) {
        case 0:
          view.value_vector = nullptr;
          break;
        case 1:
          view.value_vector = &other;
          break;
        case 2:
          view.vector_arity = 3;
          break;
        case 3:
          view.vector_element_registers[0] = nullptr;
          break;
        case 4:
          view.vector_element_types[0] = ScalarType::B16;
          break;
        case 5:
          view.locations = {};
          break;
        case 6:
          descriptor.source_value_vector = false;
          break;
        case 7:
          if (std::string_view(operand) != "VC1")
            view.vector_element_immediates[1] =
                &std::get<ResolvedImmediate>(other.elements[1]);
          else
            view.vector_element_registers[1] = nullptr;
          break;
        case 8:
          view.vector_element_types[1] = ScalarType::F64;
          break;
        case 9:
          if (std::string_view(operand) != "VC1")
            view.vector_immediate_bits[1] = *view.vector_immediate_bits[1] ^ 1;
          else
            view.vector_element_registers[1] = nullptr;
          break;
        case 10:
          if (std::string_view(operand) != "VC1")
            view.vector_immediate_source_bits[1] = 0;
          else
            view.vector_element_registers[1] = nullptr;
          break;
      }
      EXPECT_FALSE(check());
      descriptor.source_value_vector = true;
    }
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
