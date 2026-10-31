#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** An independent parser/selector boundary with a legal physical carrier. */
struct NamedDataCase {
  /** Source instruction with one data-operand placeholder. */
  std::string_view operation;
  /** Fundamental declared scalar type spelling for each physical lane. */
  std::string_view carrier = "b32";
  /** Ordinary declaration width and expected instruction lane count. */
  uint8_t arity = 2;
};
constexpr std::array named_data_cases{
    NamedDataCase{"multimem.ld_reduce.add.acc::f32.v2.f16x2 $, [mm];"},
    NamedDataCase{"multimem.st.v4.f32 [mm], $;", "f32", 4},
    NamedDataCase{"multimem.red.relaxed.gpu.add.v2.f32 [mm], $;", "f32"},
    NamedDataCase{"multimem.ld_reduce.add.acc::f32.v2.bf16x2 $, [mm];"},
    NamedDataCase{"suld.b.1d.v2.b32.trap $, [surf,x];"},
    NamedDataCase{"suld.b.2d.v4.b16.zero $, [surf,{x,x}];", "b16", 4},
    NamedDataCase{"st.async.weak.shared::cluster.mbarrier::complete_tx::bytes."
                  "v2.u32 [sd], $, [bar];"},
    NamedDataCase{"st.async.weak.shared::cluster.mbarrier::complete_tx::bytes."
                  "v4.u32 [sd], $, [bar];",
                  "b32", 4},
    NamedDataCase{"st.async.shared::cluster.mbarrier::complete_tx::bytes.v2."
                  "u32 [sd], $, [bar];"},
    NamedDataCase{"st.async.shared::cluster.mbarrier::complete_tx::bytes.v4."
                  "u32 [sd], $, [bar];",
                  "b32", 4},
};
/** Replace only the vector operand while preserving target/address legality. */
std::string named_data_source(const NamedDataCase& item,
                              std::string_view operand) {
  std::string operation(item.operation);
  operation.replace(operation.find('$'), 1, operand);
  return ".version 9.3\n.target sm_100a\n.address_size 64\n"
         ".global .align 32 .b8 mm[256]; .global .surfref surf; "
         ".shared .align 8 .b64 bar; .entry kernel() { "
         ".reg .v" +
         std::to_string(item.arity) + " ." + std::string(item.carrier) +
         " V<2>; .reg .u64 sd; .reg .s32 x; " + operation + " }";
}
/** Observe only the data vector; address coordinates are separate payloads. */
struct NamedDataCapture final : detail::IReferenceObserver {
  /** Borrowed while its owning instruction remains alive. */
  const ResolvedRegisterVector* value = nullptr;
  std::span<const SourceRange> locations;
  /** Retain the actual data field and its per-lane source locations. */
  void register_vector(const ResolvedRegisterVector& vector,
                       std::span<const SourceRange> ranges,
                       checker::AddressSymbolResolutionPolicy) override {
    value = &vector;
    locations = ranges;
  }
};
/** Produce an owned model whose source AST and input text are already dead. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> owned_named_data(
    const NamedDataCase& item, std::string_view operand = "V1") {
  const auto ast = test_helpers::parseModule(named_data_source(item, operand));
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveAndValidateModule(*ast);
}

TEST(NamedDataRegister, AdmitsNamedBesideMatchedBraceControls) {
  for (const auto& item : named_data_cases) {
    SCOPED_TRACE(item.operation);
    const auto brace =
        item.arity == 2 ? "{V1.x,V1.y}" : "{V1.x,V1.y,V1.z,V1.w}";
    const auto control = owned_named_data(item, brace);
    ASSERT_TRUE(control) << control.error().front().message;
    const auto ast = test_helpers::parseModule(named_data_source(item, "V1"));
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
/** A complete target isolates shape/provenance from availability diagnostics. */
checker::Context named_data_context() {
  const auto profile = base::find_target_profile("sm_100a");
  return {
      .target = {.ptx_version = {9, 3},
                 .sm_version = 100,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
}

TEST(NamedDataRegister, OwnsWholeBaseAndCanonicalLanesAfterSourceDeath) {
  const auto context = named_data_context();
  for (const auto& item : named_data_cases) {
    SCOPED_TRACE(item.operation);
    auto owned = owned_named_data(item);
    ASSERT_TRUE(owned) << owned.error().front().message;
    ResolvedModule copied(*owned), assigned;
    assigned = copied;
    ResolvedFunction function(copied.functions.front()), function_assigned;
    function_assigned = function;
    EXPECT_TRUE(validateModule(copied));
    EXPECT_TRUE(validateModule(assigned));
    EXPECT_NE(function.body.front().get(),
              copied.functions.front().body.front().get());
    EXPECT_TRUE(function_assigned.body.front()->check(context));
    assigned.functions.front().body.front() = function.body.front()->clone();
    EXPECT_TRUE(validateModule(assigned));
    NamedDataCapture capture;
    assigned.functions.front().body.front()->visit_references(capture);
    ASSERT_NE(capture.value, nullptr);
    const auto& value = *capture.value;
    ASSERT_EQ(value.source.kind, ResolvedVectorSourceKind::NamedVector);
    ASSERT_TRUE(value.source.whole_base);
    const auto& base = value.source.whole_base->value.register_ref;
    EXPECT_TRUE(base.symbol_id);
    EXPECT_EQ(base.parameterized_index, 1u);
    EXPECT_EQ(base.spelling, "V1");
    EXPECT_EQ(base.vector_width, item.arity);
    EXPECT_FALSE(base.component);
    ASSERT_EQ(value.elements.size(), item.arity);
    ASSERT_EQ(capture.locations.size(), item.arity);
    for (size_t index = 0; index < item.arity; ++index) {
      ASSERT_TRUE(value.elements[index]);
      const auto& lane = *value.elements[index];
      ASSERT_TRUE(lane.component);
      EXPECT_EQ(lane.symbol_id, base.symbol_id);
      EXPECT_EQ(lane.parameterized_index, base.parameterized_index);
      EXPECT_EQ(lane.declared_type, base.declared_type);
      EXPECT_EQ(lane.spelling, base.spelling);
      EXPECT_FALSE(lane.vector_width);
      EXPECT_EQ(lane.component->origin,
                RegisterComponentOrigin::NamedProjection);
      EXPECT_EQ(lane.component->lane, index);
      EXPECT_EQ(lane.component->declaration_width, item.arity);
      EXPECT_FALSE(lane.component->selector);
      EXPECT_EQ(lane.component->base_spelling, base.spelling);
      EXPECT_EQ(lane.component->base_range, value.source.range);
      EXPECT_EQ(lane.component->range, value.source.range);
      EXPECT_EQ(capture.locations[index], value.source.range);
    }
    // The current real member is authoritative; retained syntax identity is unchanged.
    auto& changed = const_cast<ResolvedRegisterVector&>(*capture.value);
    changed.source.whole_base->value.register_ref.parameterized_index = 0;
    changed.source.whole_base->value.register_ref.spelling = "V0";
    for (auto& lane : changed.elements) {
      lane->parameterized_index = 0;
      lane->spelling = "V0";
      lane->component->base_spelling = "V0";
    }
    EXPECT_TRUE(assigned.functions.front().body.front()->check(context));
    EXPECT_TRUE(validateModule(assigned));
    const auto original_ast =
        test_helpers::parseModule(named_data_source(item, "V1"));
    ASSERT_MODULE_PARSE_SUCCEEDS(original_ast);
    EXPECT_TRUE(validateModule(*original_ast, assigned));
    const auto replacement =
        test_helpers::parseModule(named_data_source(item, "V0"));
    ASSERT_MODULE_PARSE_SUCCEEDS(replacement);
    ASSERT_TRUE(resolveAndValidateModule(*replacement));
    const auto stale = validateModule(*replacement, assigned);
    ASSERT_FALSE(stale);
    EXPECT_EQ(stale.error().front().kind,
              checker::CheckDiagnosticKind::ModuleSourceMismatch);
  }
}

/** Width, declaration shape and unrelated source representations stay separate. */
TEST(NamedDataRegister, RetainsCarrierAndDeclarationLimits) {
  for (const auto& item :
       {NamedDataCase{"multimem.ld_reduce.add.acc::f32.v2.f16 $, [mm];", "b16"},
        NamedDataCase{"multimem.ld_reduce.add.acc::f32.v2.f16x2 $, [mm];",
                      "f16x2"},
        NamedDataCase{"multimem.st.v2.e4m3x4 [mm], $;"},
        NamedDataCase{"st.async.weak.shared::cluster.mbarrier::complete_tx::"
                      "bytes.v2.u32 [sd], $, [bar];",
                      "b64"}}) {
    SCOPED_TRACE(item.operation);
    const auto good = owned_named_data(item);
    ASSERT_TRUE(good) << good.error().front().message;
    EXPECT_TRUE(validateModule(*good));
  }
  for (const auto& item :
       {NamedDataCase{"multimem.ld_reduce.add.acc::f32.v2.bf16x2 $, [mm];",
                      "f16x2"},
        NamedDataCase{"multimem.st.v2.f32 [mm], $;", "u32"},
        NamedDataCase{"multimem.red.add.v2.f32 [mm], $;", "b64"},
        NamedDataCase{"suld.b.1d.v2.b16.trap $, [surf,x];"}}) {
    SCOPED_TRACE(item.operation);
    const auto ast = test_helpers::parseModule(named_data_source(item, "V1"));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
  for (const auto& item : named_data_cases) {
    auto source = named_data_source(item, "V1");
    const auto start = source.find(".reg .v");
    source.replace(start + 7, 1, item.arity == 2 ? "4" : "2");
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
  const NamedDataCase v8{"multimem.st.v8.e5m2 [mm], $;"};
  auto source = named_data_source(v8, "{b0,b1,b2,b3,b4,b5,b6,b7}");
  source.insert(source.find(".reg .v"), ".reg .b8 b<8>; ");
  const auto brace = test_helpers::parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(brace);
  ASSERT_TRUE(resolveAndValidateModule(*brace));
  const auto named = test_helpers::parseModule(named_data_source(v8, "V1"));
  ASSERT_MODULE_PARSE_SUCCEEDS(named);
  EXPECT_FALSE(resolveAndValidateModule(*named));
}

TEST(NamedDataRegister, RejectsIncompleteOrIncoherentOwnedProjection) {
  const auto context = named_data_context();
  for (const auto& item : named_data_cases) {
    SCOPED_TRACE(item.operation);
    const auto original = owned_named_data(item);
    ASSERT_TRUE(original) << original.error().front().message;
    for (int mutation = 0; mutation < 10; ++mutation) {
      SCOPED_TRACE(mutation);
      auto changed = *original;
      NamedDataCapture capture;
      changed.functions.front().body.front()->visit_references(capture);
      ASSERT_TRUE(capture.value && capture.value->source.whole_base);
      auto& value = const_cast<ResolvedRegisterVector&>(*capture.value);
      auto& lane = *value.elements.front();
      switch (mutation) {
        case 0:
          value.source.whole_base.reset();
          break;
        case 1:
          value.source.whole_base->value.register_ref.parameterized_index = 2;
          break;
        case 2:
          lane.parameterized_index = 0;
          break;
        case 3:
          lane.declared_type = ScalarType::B64;
          break;
        case 4:
          lane.symbol_id.reset();
          break;
        case 5:
          lane.component->lane = 1;
          break;
        case 6:
          std::swap(value.elements[0], value.elements[1]);
          break;
        case 7:
          lane.component->range = {};
          break;
        case 8:
          value.source.range = {};
          break;
        case 9:
          value.source.whole_base->value.register_ref.vector_width =
              4 == item.arity ? 2 : 4;
          break;
      }
      EXPECT_FALSE(changed.functions.front().body.front()->check(context));
      EXPECT_FALSE(validateModule(changed));
      ResolvedModule copied(changed), assigned;
      assigned = copied;
      assigned.functions.front().body.front() =
          copied.functions.front().body.front()->clone();
      EXPECT_FALSE(validateModule(copied));
      EXPECT_FALSE(validateModule(assigned));
    }
  }
}

TEST(NamedDataRegister, RetainsDeclarationAndBindingBoundaries) {
  for (const auto& item : named_data_cases) {
    SCOPED_TRACE(item.operation);
    for (const auto operand :
         {"V1.x", "sd", "%tid", "x", "missing", "V2", "_", "bar"}) {
      SCOPED_TRACE(operand);
      const auto ast =
          test_helpers::parseModule(named_data_source(item, operand));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
    std::string instruction(item.operation);
    instruction.replace(instruction.find('$'), 1, "V1");
    PtxSyntaxParser parser{instruction};
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast);
    EXPECT_FALSE(resolveInstruction(*ast));
    for (const auto declaration :
         {".reg .v2 .pred V<2>;", ".reg .v2 .bf16 V<2>;",
          ".reg .v4 .b64 V<2>;"}) {
      auto source = named_data_source(item, "V1");
      const auto start = source.find(".reg .v");
      const auto end = source.find(';', start);
      source.replace(start, end - start + 1, declaration);
      const auto invalid = test_helpers::parseModule(source);
      ASSERT_MODULE_PARSE_SUCCEEDS(invalid);
      EXPECT_FALSE(resolveAndValidateModule(*invalid));
    }
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
