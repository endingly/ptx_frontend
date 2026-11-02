#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

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
  /** Borrowed real vector payload while its owning instruction remains alive. */
  std::variant<std::monostate, const ResolvedRegisterVector*,
               const ResolvedValueVector*>
      data;
  /** Source ranges paired with the borrowed data vector's actual lanes. */
  std::span<const SourceRange> locations;
  /** Number of vector callbacks received from the observed instruction. */
  std::size_t callback_count = 0;
  /** Retain the actual RegVector field and its per-lane source locations. */
  void register_vector(const ResolvedRegisterVector& vector,
                       std::span<const SourceRange> ranges,
                       checker::AddressSymbolResolutionPolicy) override {
    data = &vector;
    locations = ranges;
    ++callback_count;
  }
  /** Retain the actual ValueVector field and its per-lane source locations. */
  void value_vector(const ResolvedValueVector& vector,
                    std::span<const SourceRange> ranges,
                    checker::AddressSymbolResolutionPolicy) override {
    data = &vector;
    locations = ranges;
    ++callback_count;
  }
};
/** Return source provenance directly from either borrowed vector payload. */
const ResolvedVectorSource* named_data_source(
    const NamedDataCapture& capture) {
  if (const auto* vector =
          std::get_if<const ResolvedRegisterVector*>(&capture.data);
      vector != nullptr && *vector != nullptr)
    return &(*vector)->source;
  if (const auto* vector =
          std::get_if<const ResolvedValueVector*>(&capture.data);
      vector != nullptr && *vector != nullptr)
    return &(*vector)->source;
  return nullptr;
}
/** Return the actual lane count without copying either carrier's elements. */
std::size_t named_data_lane_count(const NamedDataCapture& capture) {
  if (const auto* vector =
          std::get_if<const ResolvedRegisterVector*>(&capture.data);
      vector != nullptr && *vector != nullptr)
    return (*vector)->elements.size();
  if (const auto* vector =
          std::get_if<const ResolvedValueVector*>(&capture.data);
      vector != nullptr && *vector != nullptr)
    return (*vector)->elements.size();
  return 0;
}
/** Return the real register reference stored in either vector's selected lane. */
const ResolvedRegisterRef* named_data_lane(const NamedDataCapture& capture,
                                          std::size_t index) {
  if (const auto* vector =
          std::get_if<const ResolvedRegisterVector*>(&capture.data);
      vector != nullptr && *vector != nullptr) {
    if (index >= (*vector)->elements.size() || !(*vector)->elements[index])
      return nullptr;
    return &*(*vector)->elements[index];
  }
  if (const auto* vector =
          std::get_if<const ResolvedValueVector*>(&capture.data);
      vector != nullptr && *vector != nullptr) {
    if (index >= (*vector)->elements.size())
      return nullptr;
    return std::get_if<ResolvedRegisterRef>(&(*vector)->elements[index]);
  }
  return nullptr;
}
/** Swap two actual lane payloads while preserving the observed carrier type. */
bool named_data_swap_first_lanes(NamedDataCapture& capture) {
  if (auto* vector =
          std::get_if<const ResolvedRegisterVector*>(&capture.data);
      vector != nullptr && *vector != nullptr &&
      (*vector)->elements.size() >= 2) {
    auto& actual = const_cast<ResolvedRegisterVector&>(**vector);
    std::swap(actual.elements[0], actual.elements[1]);
    return true;
  }
  if (auto* vector = std::get_if<const ResolvedValueVector*>(&capture.data);
      vector != nullptr && *vector != nullptr &&
      (*vector)->elements.size() >= 2) {
    auto& actual = const_cast<ResolvedValueVector&>(**vector);
    std::swap(actual.elements[0], actual.elements[1]);
    return true;
  }
  return false;
}
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
  ASSERT_EQ(named_data_cases.size(), 10u);
  std::size_t register_vector_case_count = 0;
  std::size_t value_vector_case_count = 0;
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
    ASSERT_EQ(capture.callback_count, 1u);
    const bool is_register_vector = std::holds_alternative<
        const ResolvedRegisterVector*>(capture.data);
    const bool is_value_vector =
        std::holds_alternative<const ResolvedValueVector*>(capture.data);
    ASSERT_NE(is_register_vector, is_value_vector);
    if (is_register_vector)
      ++register_vector_case_count;
    else
      ++value_vector_case_count;
    const auto* source = named_data_source(capture);
    ASSERT_NE(source, nullptr);
    ASSERT_EQ(source->kind, ResolvedVectorSourceKind::NamedVector);
    ASSERT_TRUE(source->whole_base);
    const auto& base = source->whole_base->value.register_ref;
    EXPECT_TRUE(base.symbol_id);
    EXPECT_EQ(base.parameterized_index, 1u);
    EXPECT_EQ(base.spelling, "V1");
    EXPECT_EQ(base.vector_width, item.arity);
    EXPECT_FALSE(base.component);
    ASSERT_EQ(named_data_lane_count(capture), item.arity);
    ASSERT_EQ(capture.locations.size(), item.arity);
    for (size_t index = 0; index < item.arity; ++index) {
      const auto* lane_value = named_data_lane(capture, index);
      ASSERT_NE(lane_value, nullptr);
      const auto& lane = *lane_value;
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
      EXPECT_EQ(lane.component->base_range, source->range);
      EXPECT_EQ(lane.component->range, source->range);
      EXPECT_EQ(capture.locations[index], source->range);
    }
    // The current real member is authoritative; retained syntax identity is unchanged.
    auto& changed_source = const_cast<ResolvedVectorSource&>(*source);
    ASSERT_TRUE(changed_source.whole_base);
    changed_source.whole_base->value.register_ref.parameterized_index = 0;
    changed_source.whole_base->value.register_ref.spelling = "V0";
    for (size_t index = 0; index < item.arity; ++index) {
      auto* lane = const_cast<ResolvedRegisterRef*>(
          named_data_lane(capture, index));
      ASSERT_NE(lane, nullptr);
      ASSERT_TRUE(lane->component);
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
  EXPECT_EQ(register_vector_case_count + value_vector_case_count,
            named_data_cases.size());
  EXPECT_GT(register_vector_case_count, 0u);
  EXPECT_GT(value_vector_case_count, 0u);
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
  ASSERT_EQ(named_data_cases.size(), 10u);
  std::array<std::size_t, 10> register_vector_mutations{};
  std::array<std::size_t, 10> value_vector_mutations{};
  std::size_t register_vector_case_count = 0;
  std::size_t value_vector_case_count = 0;
  for (const auto& item : named_data_cases) {
    SCOPED_TRACE(item.operation);
    const auto original = owned_named_data(item);
    ASSERT_TRUE(original) << original.error().front().message;
    for (int mutation = 0; mutation < 10; ++mutation) {
      SCOPED_TRACE(mutation);
      auto changed = *original;
      NamedDataCapture capture;
      changed.functions.front().body.front()->visit_references(capture);
      ASSERT_EQ(capture.callback_count, 1u);
      const bool is_register_vector = std::holds_alternative<
          const ResolvedRegisterVector*>(capture.data);
      const bool is_value_vector =
          std::holds_alternative<const ResolvedValueVector*>(capture.data);
      ASSERT_NE(is_register_vector, is_value_vector);
      if (mutation == 0) {
        if (is_register_vector)
          ++register_vector_case_count;
        else
          ++value_vector_case_count;
      }
      if (is_register_vector)
        ++register_vector_mutations[static_cast<std::size_t>(mutation)];
      else
        ++value_vector_mutations[static_cast<std::size_t>(mutation)];
      const auto* observed_source = named_data_source(capture);
      ASSERT_NE(observed_source, nullptr);
      ASSERT_TRUE(observed_source->whole_base);
      ASSERT_EQ(named_data_lane_count(capture), item.arity);
      auto& source = const_cast<ResolvedVectorSource&>(*observed_source);
      auto* lane = const_cast<ResolvedRegisterRef*>(
          named_data_lane(capture, 0));
      ASSERT_NE(lane, nullptr);
      ASSERT_TRUE(lane->component);
      switch (mutation) {
        case 0:
          source.whole_base.reset();
          break;
        case 1:
          source.whole_base->value.register_ref.parameterized_index = 2;
          break;
        case 2:
          lane->parameterized_index = 0;
          break;
        case 3:
          lane->declared_type = ScalarType::B64;
          break;
        case 4:
          lane->symbol_id.reset();
          break;
        case 5:
          lane->component->lane = 1;
          break;
        case 6:
          ASSERT_TRUE(named_data_swap_first_lanes(capture));
          break;
        case 7:
          lane->component->range = {};
          break;
        case 8:
          source.range = {};
          break;
        case 9:
          source.whole_base->value.register_ref.vector_width =
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
  EXPECT_EQ(register_vector_case_count + value_vector_case_count,
            named_data_cases.size());
  EXPECT_GT(register_vector_case_count, 0u);
  EXPECT_GT(value_vector_case_count, 0u);
  for (std::size_t mutation = 0; mutation < register_vector_mutations.size();
       ++mutation) {
    EXPECT_GT(register_vector_mutations[mutation], 0u) << mutation;
    EXPECT_GT(value_vector_mutations[mutation], 0u) << mutation;
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
