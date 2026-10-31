#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string>
#include <utility>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Keep selector dtype U16, which already satisfies the exact carrier contract. */
std::string selector_shape_source(std::array<std::string, 4> values = {
                                      "%selector", "%selector", "%selector",
                                      "%selector"}) {
  return R"ptx(
.version 9.3
.target sm_120a
.address_size 64
.entry kernel() {
  .reg .f32 %d<4>, %c<4>;
  .reg .b32 %a<4>, %b<2>, %sa, %sb;
  .reg .u16 %selector;
  .reg .v2 .u16 Whole, Group<2>;
  .reg .v4 .u16 Wide;
  mma.sync.aligned.m16n8k32.row.col.kind::mxf8f6f4.block_scale.scale_vec::1X.f32.e4m3.e4m3.f32.ue8m0
    {%d0,%d1,%d2,%d3}, {%a0,%a1,%a2,%a3}, {%b0,%b1},
    {%c0,%c1,%c2,%c3}, %sa, {)ptx" +
         values[0] + "," + values[1] + "}, %sb, {" + values[2] + "," +
         values[3] + R"ptx(};
}
)ptx";
}
/** Return only the strict resolved module, releasing parser/source/AST. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> selector_shape_module(
    std::array<std::string, 4> values = {"%selector", "%selector", "%selector",
                                         "%selector"}) {
  const auto ast =
      test_helpers::parseModule(selector_shape_source(std::move(values)));
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  const auto source_only = resolveModuleOnly(*ast);
  EXPECT_TRUE(source_only);
  if (source_only)
    EXPECT_TRUE(validateModule(*ast, *source_only));
  return resolveAndValidateModule(*ast);
}

/** A scalar tuple lane cannot carry a coherently bound ordinary whole vector. */
TEST(MmaSelectorLaneShape, RejectsOwnedWholeVectorInScalarTupleLane) {
  for (const auto name : {"Whole", "Wide"}) {
    const auto invalid_source = test_helpers::parseModule(
        selector_shape_source({name, "%selector", "%selector", "%selector"}));
    ASSERT_TRUE(invalid_source && invalid_source.diagnostics.empty());
    EXPECT_FALSE(resolveModuleOnly(*invalid_source));
    EXPECT_FALSE(resolveAndValidateModule(*invalid_source));
  }
  auto module = selector_shape_module();
  ASSERT_TRUE(module) << (module.error().empty()
                              ? ""
                              : module.error().front().message);
  ASSERT_TRUE(validateModule(*module));
  using Scaled =
      MmaSyncAlignedM16n8k32RowColKindMxf8f6f4BlockScaleScaleVec1F32E4m3E4m3F32Ue8m0;
  const auto profile = base::find_target_profile("sm_120a");
  ASSERT_TRUE(profile);
  const checker::Context context{
      .target = {
          .ptx_version = {9, 3},
          .sm_version = profile->identity.architecture.number,
          .enabled_family_features = profile->enabled_family_features,
          .identity = profile->identity,
          .capabilities = profile->capabilities,
      }};
  for (const auto name : {"Whole", "Wide"}) {
    for (unsigned lane = 0; lane < 4; ++lane) {
      SCOPED_TRACE(name);
      SCOPED_TRACE(lane);
      auto copied = *module;
      ResolvedModule assigned;
      assigned = copied;
      auto& function = assigned.functions.front();
      function.body[0] = function.body[0]->clone();
      auto& form = dynamic_cast<Scaled&>(*function.body[0]);
      ASSERT_TRUE(form.check(context));
      auto& located_selector =
          lane < 2 ? form.scale_a_selector : form.scale_b_selector;
      ASSERT_EQ(located_selector.locs.size(), 2u);
      const auto expected_range = located_selector.locs[lane % 2];
      auto& selector = located_selector.value;
      auto& value = lane % 2 == 0 ? selector.byte_id : selector.thread_id;
      auto& ref = std::get<ResolvedRegisterRef>(value);
      ASSERT_EQ(ref.declared_type, ScalarType::U16);
      ASSERT_FALSE(ref.vector_width);
      const auto whole =
          assigned.symbols.lookup(function.declaration_scope, name);
      ASSERT_TRUE(whole);
      const auto& symbol = assigned.symbols.symbol(whole->symbol);
      ASSERT_EQ(symbol.vector_width,
                std::string_view(name) == "Whole" ? 2u : 4u);
      ASSERT_EQ(symbol.type, ".u16");
      ref.symbol_id = whole->symbol;
      ref.spelling = name;
      ref.register_class = ResolvedRegisterClass::General;
      ref.declared_type = ScalarType::U16;
      ref.vector_width = symbol.vector_width;
      ref.index.reset();
      ref.parameterized_index.reset();
      ref.component.reset();
      const auto direct = form.check(context);
      ASSERT_FALSE(direct);
      ASSERT_EQ(direct.error().size(), 1u);
      EXPECT_EQ(direct.error().front().kind,
                checker::CheckDiagnosticKind::UnsupportedOperandShape);
      EXPECT_EQ(direct.error().front().range, expected_range);
      const auto owned = validateModule(assigned);
      ASSERT_FALSE(owned);
      ASSERT_EQ(owned.error().size(), 1u);
      EXPECT_EQ(owned.error().front().kind,
                checker::CheckDiagnosticKind::UnsupportedOperandShape);
      EXPECT_EQ(owned.error().front().range, expected_range);
      auto copied_mutation = assigned;
      EXPECT_FALSE(validateModule(copied_mutation));
      located_selector.locs.clear();
      auto fallback_context = context;
      fallback_context.instruction_range = function.instruction_ranges[0];
      const auto fallback = form.check(fallback_context);
      ASSERT_FALSE(fallback);
      EXPECT_EQ(fallback.error().front().kind,
                checker::CheckDiagnosticKind::UnsupportedOperandShape);
      EXPECT_EQ(fallback.error().front().range,
                fallback_context.instruction_range);
    }
  }
}

/** Selected components remain scalar, independently of member and vector lane. */
TEST(MmaSelectorLaneShape,
     PreservesComponentsCompactMembersAndMixedImmediates) {
  using Scaled =
      MmaSyncAlignedM16n8k32RowColKindMxf8f6f4BlockScaleScaleVec1F32E4m3E4m3F32Ue8m0;
  for (unsigned lane = 0; lane < 4; ++lane) {
    for (const auto name :
         {"Whole.x", "Whole.g", "Wide.z", "Wide.a", "Group1.y"}) {
      SCOPED_TRACE(lane);
      SCOPED_TRACE(name);
      std::array<std::string, 4> values{"%selector", "%selector", "%selector",
                                        "%selector"};
      values[lane] = name;
      auto module = selector_shape_module(values);
      ASSERT_TRUE(module) << (module.error().empty()
                                  ? ""
                                  : module.error().front().message);
      auto copied = *module;
      copied.functions[0].body[0] = copied.functions[0].body[0]->clone();
      auto& form = dynamic_cast<Scaled&>(*copied.functions[0].body[0]);
      auto& selector =
          lane < 2 ? form.scale_a_selector.value : form.scale_b_selector.value;
      auto& ref = std::get<ResolvedRegisterRef>(
          lane % 2 == 0 ? selector.byte_id : selector.thread_id);
      ASSERT_TRUE(ref.component);
      ASSERT_TRUE(ref.symbol_id);
      EXPECT_FALSE(ref.vector_width);
      EXPECT_EQ(ref.declared_type, ScalarType::U16);
      EXPECT_EQ(ref.component->origin,
                RegisterComponentOrigin::ExplicitSelector);
      EXPECT_EQ(ref.parameterized_index, std::string_view(name) == "Group1.y"
                                             ? std::optional<uint32_t>{1}
                                             : std::nullopt);
      EXPECT_TRUE(validateModule(copied));
      if (ref.parameterized_index) {
        ref.parameterized_index = 0;
        ref.spelling = "Group0.y";
        ref.component->base_spelling = "Group0";
        EXPECT_TRUE(validateModule(copied));
        ref.parameterized_index = 2;
        EXPECT_FALSE(validateModule(copied));
        ref.parameterized_index = 0;
      }
      ref.component->lane = 4;
      EXPECT_FALSE(validateModule(copied));
    }
  }
  auto mixed = selector_shape_module({"0", "%selector", "2", "3"});
  ASSERT_TRUE(mixed);
  auto copied = *mixed;
  EXPECT_TRUE(validateModule(copied));
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
