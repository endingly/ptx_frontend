#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** One representative of a family/kind/scale-size selector topology. */
struct SelectorCohort {
  /** Complete mnemonic; fragment payloads are fixed independently below. */
  std::string_view mnemonic;
  /** Expected immutable matrix kind. */
  MatrixKind kind;
  /** Number of logical scale-vector entries. */
  unsigned scale;
  /** Ordered sparse form carries an additional metadata operand and selector. */
  bool sparse;
};
/** Eight approved topology partitions, not an execution quota for all variants. */
constexpr std::array<SelectorCohort, 8> cohorts{{
    {"mma.sync.aligned.m16n8k32.row.col.kind::mxf8f6f4.block_scale.f32.e4m3."
     "e4m3.f32.ue8m0",
     MatrixKind::MXF8F6F4, 1, false},
    {"mma.sync.aligned.m16n8k64.row.col.kind::mxf4.block_scale.f32.e2m1.e2m1."
     "f32.ue8m0",
     MatrixKind::MXF4, 2, false},
    {"mma.sync.aligned.m16n8k64.row.col.kind::mxf4nvf4.block_scale.scale_vec::"
     "2X.f32.e2m1.e2m1.f32.ue8m0",
     MatrixKind::MXF4NVF4, 2, false},
    {"mma.sync.aligned.m16n8k64.row.col.kind::mxf4nvf4.block_scale.scale_vec::"
     "4X.f32.e2m1.e2m1.f32.ue8m0",
     MatrixKind::MXF4NVF4, 4, false},
    {"mma.sp::ordered_metadata.sync.aligned.m16n8k64.row.col.kind::mxf8f6f4."
     "block_scale.f32.e4m3.e4m3.f32.ue8m0",
     MatrixKind::MXF8F6F4, 1, true},
    {"mma.sp::ordered_metadata.sync.aligned.m16n8k128.row.col.kind::mxf4.block_"
     "scale.f32.e2m1.e2m1.f32.ue8m0",
     MatrixKind::MXF4, 2, true},
    {"mma.sp::ordered_metadata.sync.aligned.m16n8k128.row.col.kind::mxf4nvf4."
     "block_scale.scale_vec::2X.f32.e2m1.e2m1.f32.ue8m0",
     MatrixKind::MXF4NVF4, 2, true},
    {"mma.sp::ordered_metadata.sync.aligned.m16n8k128.row.col.kind::mxf4nvf4."
     "block_scale.scale_vec::4X.f32.e2m1.e2m1.f32.ue8m0",
     MatrixKind::MXF4NVF4, 4, true},
}};
/** Borrow the two actual located selector payloads from their exact public forms. */
std::array<WithLocs<ResolvedMatrixScaleSelector>*, 2> selector_fields(
    Instruction& instruction, unsigned cohort) {
  switch (cohort) {
    case 0: {
      auto& form = dynamic_cast<
          MmaSyncAlignedM16n8k32RowColKindMxf8f6f4BlockScaleScaleVecDefaultF32E4m3E4m3F32Ue8m0&>(
          instruction);
      EXPECT_EQ(form.matrix_descriptor()->kind, cohorts[cohort].kind);
      EXPECT_EQ(form.matrix_descriptor()->scale_vector_size,
                cohorts[cohort].scale);
      return {&form.scale_a_selector, &form.scale_b_selector};
    }
    case 1: {
      auto& form = dynamic_cast<
          MmaSyncAlignedM16n8k64RowColKindMxf4BlockScaleScaleVecDefaultF32E2m1E2m1F32Ue8m0&>(
          instruction);
      EXPECT_EQ(form.matrix_descriptor()->kind, cohorts[cohort].kind);
      EXPECT_EQ(form.matrix_descriptor()->scale_vector_size,
                cohorts[cohort].scale);
      return {&form.scale_a_selector, &form.scale_b_selector};
    }
    case 2: {
      auto& form = dynamic_cast<
          MmaSyncAlignedM16n8k64RowColKindMxf4nvf4BlockScaleScaleVec2F32E2m1E2m1F32Ue8m0&>(
          instruction);
      EXPECT_EQ(form.matrix_descriptor()->kind, cohorts[cohort].kind);
      EXPECT_EQ(form.matrix_descriptor()->scale_vector_size,
                cohorts[cohort].scale);
      return {&form.scale_a_selector, &form.scale_b_selector};
    }
    case 3: {
      auto& form = dynamic_cast<
          MmaSyncAlignedM16n8k64RowColKindMxf4nvf4BlockScaleScaleVec4F32E2m1E2m1F32Ue8m0&>(
          instruction);
      EXPECT_EQ(form.matrix_descriptor()->kind, cohorts[cohort].kind);
      EXPECT_EQ(form.matrix_descriptor()->scale_vector_size,
                cohorts[cohort].scale);
      return {&form.scale_a_selector, &form.scale_b_selector};
    }
    case 4: {
      auto& form = dynamic_cast<
          MmaSpOrderedMetadataSyncAlignedM16n8k64RowColKindMxf8f6f4BlockScaleScaleVecDefaultF32E4m3E4m3F32Ue8m0&>(
          instruction);
      EXPECT_EQ(form.matrix_descriptor()->kind, cohorts[cohort].kind);
      EXPECT_EQ(form.matrix_descriptor()->scale_vector_size,
                cohorts[cohort].scale);
      return {&form.scale_a_selector, &form.scale_b_selector};
    }
    case 5: {
      auto& form = dynamic_cast<
          MmaSpOrderedMetadataSyncAlignedM16n8k128RowColKindMxf4BlockScaleScaleVecDefaultF32E2m1E2m1F32Ue8m0&>(
          instruction);
      EXPECT_EQ(form.matrix_descriptor()->kind, cohorts[cohort].kind);
      EXPECT_EQ(form.matrix_descriptor()->scale_vector_size,
                cohorts[cohort].scale);
      return {&form.scale_a_selector, &form.scale_b_selector};
    }
    case 6: {
      auto& form = dynamic_cast<
          MmaSpOrderedMetadataSyncAlignedM16n8k128RowColKindMxf4nvf4BlockScaleScaleVec2F32E2m1E2m1F32Ue8m0&>(
          instruction);
      EXPECT_EQ(form.matrix_descriptor()->kind, cohorts[cohort].kind);
      EXPECT_EQ(form.matrix_descriptor()->scale_vector_size,
                cohorts[cohort].scale);
      return {&form.scale_a_selector, &form.scale_b_selector};
    }
    case 7: {
      auto& form = dynamic_cast<
          MmaSpOrderedMetadataSyncAlignedM16n8k128RowColKindMxf4nvf4BlockScaleScaleVec4F32E2m1E2m1F32Ue8m0&>(
          instruction);
      EXPECT_EQ(form.matrix_descriptor()->kind, cohorts[cohort].kind);
      EXPECT_EQ(form.matrix_descriptor()->scale_vector_size,
                cohorts[cohort].scale);
      return {&form.scale_a_selector, &form.scale_b_selector};
    }
  }
  ADD_FAILURE() << "Unknown test cohort";
  return {};
}
/** Build otherwise legal fragments/scales and four independently declared selector leaves. */
std::string selector_source(unsigned cohort,
                            std::array<std::string, 4> types = {"u16", "u16",
                                                                "u16", "u16"},
                            std::array<std::string, 4> values = {
                                "%s0", "%s1", "%s2", "%s3"}) {
  std::string source = R"ptx(.version 9.3
.target sm_120a
.address_size 64
.entry kernel() {
  .reg .f32 %d<4>, %c<4>;
  .reg .b32 %a<4>, %b<4>, %e, %sa, %sb;
  .reg .b16 bits;
  .reg .s16 signed_value;
  .reg .u16 native;
  .reg .u32 wide;
  .reg .v2 .s16 Group<2>;
  .reg .v4 .b16 Four;
  .reg .v2 .u16 Native;
)ptx";
  for (unsigned index = 0; index < 4; ++index)
    source += ".reg ." + types[index] + " %s" + std::to_string(index) + ";";
  source += std::string(cohorts[cohort].mnemonic) +
            " {%d0,%d1,%d2,%d3}, {%a0,%a1,%a2,%a3}, ";
  source += cohorts[cohort].sparse ? "{%b0,%b1,%b2,%b3}, " : "{%b0,%b1}, ";
  source += "{%c0,%c1,%c2,%c3}, ";
  if (cohorts[cohort].sparse)
    source += "%e, 0, ";
  return source + "%sa, {" + values[0] + "," + values[1] + "}, %sb, {" +
         values[2] + "," + values[3] + "}; }";
}
/** Resolve all source/checker stages and release original text/parser/AST before return. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> selector_module(
    unsigned cohort,
    std::array<std::string, 4> types = {"u16", "u16", "u16", "u16"},
    std::array<std::string, 4> values = {"%s0", "%s1", "%s2", "%s3"}) {
  const auto ast =
      test_helpers::parseModule(selector_source(cohort, types, values));
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  auto resolved = resolveModuleOnly(*ast);
  if (resolved)
    EXPECT_TRUE(validateModule(*ast, *resolved));
  if (!resolved)
    return resolved;
  return resolveAndValidateModule(*ast);
}
/** Preserve clean parsing while checking an independently invalid selector value/type. */
void expect_selector_rejected(unsigned cohort,
                              const std::array<std::string, 4>& types,
                              const std::array<std::string, 4>& values) {
  const auto ast =
      test_helpers::parseModule(selector_source(cohort, types, values));
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  auto source_only = resolveModuleOnly(*ast);
  if (source_only) {
    EXPECT_FALSE(validateModule(*ast, *source_only));
    EXPECT_FALSE(validateModule(*source_only));
  }
  EXPECT_FALSE(resolveAndValidateModule(*ast));
}

TEST(MmaSelectorCarriers, AcceptsIndependentIntegerCarriersInAllEightCohorts) {
  for (unsigned cohort = 0; cohort < cohorts.size(); ++cohort) {
    auto baseline = selector_module(cohort);
    ASSERT_TRUE(baseline) << (baseline.error().empty()
                                  ? ""
                                  : baseline.error().front().message);
    for (unsigned position = 0; position < 4; ++position) {
      for (const auto type : {"b16", "s16", "u16"}) {
        SCOPED_TRACE(cohort);
        SCOPED_TRACE(position);
        SCOPED_TRACE(type);
        std::array<std::string, 4> types{"u16", "u16", "u16", "u16"};
        types[position] = type;
        auto module = selector_module(cohort, types);
        ASSERT_TRUE(module)
            << (module.error().empty() ? "" : module.error().front().message);
        auto copy = *module;
        ResolvedModule assigned;
        assigned = copy;
        assigned.functions[0] = ResolvedFunction(assigned.functions[0]);
        assigned.functions[0].body[0] = assigned.functions[0].body[0]->clone();
        auto& instruction = *assigned.functions[0].body[0];
        const auto fields = selector_fields(instruction, cohort);
        ASSERT_NE(fields[position / 2], nullptr);
        ASSERT_EQ(fields[position / 2]->locs.size(), 2u);
        const auto& selector = fields[position / 2]->value;
        const auto& ref = std::get<ResolvedRegisterRef>(
            position % 2 == 0 ? selector.byte_id : selector.thread_id);
        ASSERT_TRUE(ref.symbol_id);
        const auto* metadata =
            base::find_scalar_type_metadata("." + std::string(type));
        ASSERT_NE(metadata, nullptr);
        EXPECT_EQ(ref.declared_type, metadata->type);
        EXPECT_EQ(assigned.symbols.symbol(*ref.symbol_id).name,
                  "%s" + std::to_string(position));
        EXPECT_TRUE(validateModule(assigned));
      }
    }
    for (const auto values :
         {std::array<std::string, 4>{"0", "%s1", "%s2", "3"},
          std::array<std::string, 4>{"%s0", "1", "0", "%s3"}}) {
      auto mixed =
          selector_module(cohort, {"b16", "s16", "b16", "s16"}, values);
      ASSERT_TRUE(mixed);
      EXPECT_TRUE(validateModule(*mixed));
    }
  }
}

TEST(MmaSelectorCarriers, RejectsWrongClassesWidthsAndTupleShapes) {
  for (unsigned cohort = 0; cohort < cohorts.size(); ++cohort) {
    ASSERT_TRUE(selector_module(cohort));
    for (unsigned position = 0; position < 4; ++position) {
      for (const auto type :
           {"f16", "b8", "b32", "u32", "b64", "b128", "pred", "bf16"}) {
        SCOPED_TRACE(cohort);
        SCOPED_TRACE(position);
        SCOPED_TRACE(type);
        std::array<std::string, 4> types{"u16", "u16", "u16", "u16"};
        types[position] = type;
        expect_selector_rejected(cohort, types, {"%s0", "%s1", "%s2", "%s3"});
      }
      for (const auto invalid : {"Native", "%tid.x"}) {
        std::array<std::string, 4> values{"%s0", "%s1", "%s2", "%s3"};
        values[position] = invalid;
        expect_selector_rejected(cohort, {"u16", "u16", "u16", "u16"}, values);
      }
    }
    const auto base = selector_source(cohort);
    auto nested = base;
    nested.replace(nested.find("{%s0,%s1}"), std::string("{%s0,%s1}").size(),
                   "{{%s0,%s1},%s2}");
    const auto nested_ast = test_helpers::parseModule(nested);
    EXPECT_TRUE(!nested_ast || !nested_ast.diagnostics.empty() ||
                !resolveModuleOnly(*nested_ast));
    for (const auto tuple : {"Native", "{%s0}", "{%s0,%s1,%s2}"}) {
      auto source = base;
      source.replace(source.find("{%s0,%s1}"), std::string("{%s0,%s1}").size(),
                     tuple);
      const auto ast = test_helpers::parseModule(source);
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
  }
}

TEST(MmaSelectorCarriers, RetainsComponentMemberAndTypedCacheProvenance) {
  for (unsigned cohort = 0; cohort < cohorts.size(); ++cohort) {
    for (unsigned position = 0; position < 4; ++position) {
      for (const auto name : {"Group1.y", "Four.a", "Native.r"}) {
        std::array<std::string, 4> values{"%s0", "%s1", "%s2", "%s3"};
        values[position] = name;
        auto module =
            selector_module(cohort, {"u16", "u16", "u16", "u16"}, values);
        ASSERT_TRUE(module)
            << (module.error().empty() ? "" : module.error().front().message);
        auto copy = *module;
        copy.functions[0].body[0] = copy.functions[0].body[0]->clone();
        auto fields = selector_fields(*copy.functions[0].body[0], cohort);
        auto& selector = fields[position / 2]->value;
        auto& ref = std::get<ResolvedRegisterRef>(
            position % 2 == 0 ? selector.byte_id : selector.thread_id);
        ASSERT_TRUE(ref.component);
        ASSERT_TRUE(ref.symbol_id);
        EXPECT_FALSE(ref.vector_width);
        EXPECT_EQ(ref.component->origin,
                  RegisterComponentOrigin::ExplicitSelector);
        EXPECT_EQ(ref.parameterized_index, std::string_view(name) == "Group1.y"
                                               ? std::optional<uint32_t>{1}
                                               : std::nullopt);
        EXPECT_EQ(ref.component->range,
                  fields[position / 2]->locs[position % 2]);
        EXPECT_TRUE(validateModule(copy));
        if (ref.parameterized_index) {
          ref.parameterized_index = 0;
          ref.component->base_spelling = "Group0";
          ref.spelling = "Group0.y";
          EXPECT_TRUE(validateModule(copy));
          ref.parameterized_index = 2;
          EXPECT_FALSE(validateModule(copy));
          ref.parameterized_index = 0;
        }
        ref.component->lane = 4;
        EXPECT_FALSE(validateModule(copy));
      }
      auto module = selector_module(cohort);
      ASSERT_TRUE(module);
      for (const auto name : {"bits", "signed_value", "native", "wide"}) {
        auto copy = *module;
        auto fields = selector_fields(*copy.functions[0].body[0], cohort);
        auto& selector = fields[position / 2]->value;
        auto& ref = std::get<ResolvedRegisterRef>(
            position % 2 == 0 ? selector.byte_id : selector.thread_id);
        const auto symbol =
            copy.symbols.lookup(copy.functions[0].declaration_scope, name);
        ASSERT_TRUE(symbol);
        ref.symbol_id = symbol->symbol;
        ref.spelling = name;
        ref.declared_type =
            std::string_view(name) == "bits"           ? ScalarType::B16
            : std::string_view(name) == "signed_value" ? ScalarType::S16
            : std::string_view(name) == "native"       ? ScalarType::U16
                                                       : ScalarType::U32;
        EXPECT_EQ(validateModule(copy).has_value(),
                  std::string_view(name) != "wide");
        if (std::string_view(name) == "signed_value") {
          ref.declared_type = ScalarType::U16;
          EXPECT_FALSE(validateModule(copy));
        }
      }
    }
  }
}

TEST(MmaSelectorCarriers,
     PreservesUnsignedLiteralMasksAndSeparateThreadBounds) {
  for (unsigned cohort = 0; cohort < cohorts.size(); ++cohort) {
    const unsigned mask = cohorts[cohort].scale == 1   ? 15
                          : cohorts[cohort].scale == 2 ? 5
                                                       : 1;
    for (unsigned position = 0; position < 4; ++position) {
      const unsigned maximum = position % 2 == 0 ? 4 : position == 1 ? 2 : 4;
      for (unsigned value = 0; value <= maximum; ++value) {
        std::array<std::string, 4> values{"%s0", "%s1", "%s2", "%s3"};
        values[position] = std::to_string(value);
        const auto ast = test_helpers::parseModule(
            selector_source(cohort, {"u16", "u16", "u16", "u16"}, values));
        ASSERT_TRUE(ast && ast.diagnostics.empty());
        const bool valid = position % 2 == 0
                               ? (mask & (1u << value)) != 0
                               : value < (position == 1 ? 2u : 4u);
        EXPECT_EQ(resolveAndValidateModule(*ast).has_value(), valid);
      }
      for (const auto value :
           {"-1", "65536", "18446744073709551616U", "1.0", "0f00000000"}) {
        std::array<std::string, 4> values{"%s0", "%s1", "%s2", "%s3"};
        values[position] = value;
        expect_selector_rejected(cohort, {"u16", "u16", "u16", "u16"}, values);
      }
    }
    auto module = selector_module(cohort, {"u16", "u16", "u16", "u16"},
                                  {"0", "1", "0", "3"});
    ASSERT_TRUE(module);
    auto copy = *module;
    auto fields = selector_fields(*copy.functions[0].body[0], cohort);
    auto& value = std::get<ResolvedImmediate>(fields[0]->value.byte_id);
    value.integer_source_bits = 65536;
    EXPECT_FALSE(validateModule(copy));
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
