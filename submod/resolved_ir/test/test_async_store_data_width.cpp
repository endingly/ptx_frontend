#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include <ptx_frontend/resolved_ir/model/data_movement/st.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Exact public classes and written arities; combined-cluster flags stay distinct. */
constexpr std::array<std::string_view, 8> store_names{
    "AsyncSharedScalar",        "AsyncSharedV2",         "AsyncSharedV4",
    "AsyncSharedClusterScalar", "AsyncSharedClusterV2",  "AsyncSharedClusterV4",
    "AsyncGlobalRelease",       "AsyncGlobalMmioRelease"};
/** Number of scalar elements in the selected instruction, independent of carriers. */
unsigned store_arity(unsigned variant) {
  return variant < 6 ? std::array<unsigned, 3>{1, 2, 4}[variant % 3] : 1;
}
/** Render valid declarations and one exact async source topology. */
std::string store_source(unsigned variant, std::string_view type,
                         std::string_view carrier, bool all_wide = false,
                         std::string_view first = "probe",
                         std::string_view version = "9.3",
                         std::string_view target = "sm_100") {
  std::string instruction = "st.async";
  if (variant < 3)
    instruction += ".weak.shared::cluster.mbarrier::complete_tx::bytes";
  else if (variant < 6)
    instruction += ".shared::cluster.mbarrier::complete_tx::bytes";
  else
    instruction +=
        variant == 6 ? ".release.gpu.global" : ".mmio.release.sys.global";
  const unsigned arity = store_arity(variant);
  if (arity != 1)
    instruction += ".v" + std::to_string(arity);
  instruction +=
      "." + std::string(type) + " [" + (variant < 6 ? "sd" : "gd") + "], ";
  if (arity != 1)
    instruction += "{";
  instruction += first;
  for (unsigned lane = 1; lane < arity; ++lane)
    instruction +=
        "," + std::string(all_wide ? "tail" : "n") + std::to_string(lane - 1);
  if (arity != 1)
    instruction += "}";
  instruction += variant < 6 ? ", [bar];" : ";";
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) +
         "\n.address_size 64\n"
         ".shared .align 8 .b64 bar; .shared .align 16 .b8 s[64]; "
         ".global .align 16 .b8 g[64]; .entry kernel() { .reg ." +
         std::string(carrier) + " probe,tail<3>; .reg ." + std::string(type) +
         " n<3>; .reg .u64 sd,gd,candidate; "
         ".reg .b128 wider; .reg .u32 word; .reg .b16 narrow; .reg .f32 wrong; "
         ".reg .v2 .b64 V<2>; " +
         instruction + " }";
}
/** Borrow the concrete logical type and scalar leaves, retaining owner locations. */
struct StoreFields {
  /** Instruction type field, unaffected by register widening. */
  WithLocs<ScalarType>& type;
  /** Borrowed non-sink scalar register leaves in written order. */
  std::vector<ResolvedRegisterRef*> lanes;
  /** Borrowed operand locations, valid until the instruction changes owner. */
  std::span<const SourceRange> locations;
  /** Selected default-layout provenance. */
  ResolvedOperandLayoutTag& layout;
};
/** Borrow a scalar field without adding a second carrier representation. */
StoreFields scalar_store_fields(WithLocs<ScalarType>& type,
                                WithLocs<ResolvedRegisterRef>& value,
                                ResolvedOperandLayoutTag& layout) {
  return {type, {&value.value}, value.locs, layout};
}
/** Borrow explicit non-sink vector leaves without flattening named declarations. */
StoreFields vector_store_fields(WithLocs<ScalarType>& type,
                                WithLocs<ResolvedRegisterVector>& value,
                                ResolvedOperandLayoutTag& layout) {
  std::vector<ResolvedRegisterRef*> refs;
  for (auto& ref : value.value.elements)
    refs.push_back(&ref.value());
  return {type, std::move(refs), value.locs, layout};
}
/** Dynamic casts prove selection of all eight exact concrete variants. */
StoreFields store_fields(Instruction& instruction, unsigned variant) {
  if (variant == 0) {
    auto& f = dynamic_cast<StAsyncSharedScalar&>(instruction);
    return scalar_store_fields(f.type, f.value, f.operand_layout);
  }
  if (variant == 1) {
    auto& f = dynamic_cast<StAsyncSharedV2&>(instruction);
    return vector_store_fields(f.type, f.value, f.operand_layout);
  }
  if (variant == 2) {
    auto& f = dynamic_cast<StAsyncSharedV4&>(instruction);
    return vector_store_fields(f.type, f.value, f.operand_layout);
  }
  if (variant == 3) {
    auto& f = dynamic_cast<StAsyncSharedClusterScalar&>(instruction);
    return scalar_store_fields(f.type, f.value, f.operand_layout);
  }
  if (variant == 4) {
    auto& f = dynamic_cast<StAsyncSharedClusterV2&>(instruction);
    return vector_store_fields(f.type, f.value, f.operand_layout);
  }
  if (variant == 5) {
    auto& f = dynamic_cast<StAsyncSharedClusterV4&>(instruction);
    return vector_store_fields(f.type, f.value, f.operand_layout);
  }
  if (variant == 6) {
    auto& f = dynamic_cast<StAsyncGlobalRelease&>(instruction);
    return scalar_store_fields(f.type, f.value, f.operand_layout);
  }
  auto& f = dynamic_cast<StAsyncGlobalMmioRelease&>(instruction);
  return scalar_store_fields(f.type, f.value, f.operand_layout);
}
/** Resolve and check before source/parser/AST destruction, then transfer ownership. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> owned_store(
    std::string source) {
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveAndValidateModule(*ast);
}

TEST(AsyncStoreDataWidth, AcceptsWiderSourcesAcrossAllEightConcreteSelectors) {
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 100}};
  for (unsigned variant = 0; variant < 8; ++variant) {
    SCOPED_TRACE(store_names[variant]);
    const auto baseline =
        test_helpers::parseModule(store_source(variant, "u32", "u32"));
    ASSERT_TRUE(baseline && baseline.diagnostics.empty());
    auto nominal = resolveAndValidateModule(*baseline);
    ASSERT_TRUE(nominal) << nominal.error().front().message;
    auto fields =
        store_fields(*nominal->functions.front().body.front(), variant);
    EXPECT_EQ(fields.type.value, ScalarType::U32);
    EXPECT_EQ(fields.layout.value, 0);
    ASSERT_EQ(fields.lanes.size(), store_arity(variant));
    const auto ast =
        test_helpers::parseModule(store_source(variant, "u32", "u64"));
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto draft = resolveModuleOnly(*ast);
    if (draft) {
      EXPECT_TRUE(draft->functions.front().body.front()->check(context));
      EXPECT_TRUE(validateModule(*ast, *draft));
    } else {
      ASSERT_FALSE(draft.error().empty());
      EXPECT_EQ(draft.error().front().stage(),
                ResolveDiagnosticStage::Resolution);
      EXPECT_GT(draft.error().front().range.start.line, 0);
    }
    EXPECT_TRUE(resolveAndValidateModule(*ast));
    auto owned = owned_store(store_source(variant, "u32", "u32"));
    ASSERT_TRUE(owned);
    const auto symbol = owned->symbols.lookup(
        owned->functions.front().declaration_scope, "candidate");
    ASSERT_TRUE(symbol);
    auto leaf = store_fields(*owned->functions.front().body.front(), variant)
                    .lanes.front();
    *leaf =
        ResolvedRegisterRef{.spelling = "candidate",
                            .register_class = ResolvedRegisterClass::General,
                            .symbol_id = symbol->symbol,
                            .declared_type = ScalarType::U64};
    EXPECT_TRUE(owned->functions.front().body.front()->check(context));
    EXPECT_TRUE(validateModule(*baseline, *owned));
    EXPECT_TRUE(validateModule(*owned));
    auto copied = *owned;
    copied.functions.front().body.front() =
        copied.functions.front().body.front()->clone();
    EXPECT_TRUE(validateModule(copied));
  }
}
TEST(AsyncStoreDataWidth, RetainsLogicalElementSizesForWiderScalarBraceLanes) {
  const auto& descriptor = st_resolved_descriptor();
  for (unsigned variant = 0; variant < 8; ++variant) {
    SCOPED_TRACE(store_names[variant]);
    const auto found =
        std::ranges::find_if(descriptor.variants, [&](const auto& row) {
          return row.variant_name == store_names[variant];
        });
    ASSERT_NE(found, descriptor.variants.end());
    ASSERT_EQ(found->operand_layouts.size(), 1u);
    const auto& bindings = found->operand_layouts.front().bindings;
    const auto value = std::ranges::find_if(bindings, [](const auto& row) {
      return row.target_field_id == "value";
    });
    ASSERT_NE(value, bindings.end());
    EXPECT_EQ(value->register_width_policy,
              base::ScalarTypeSizePolicy::EqualOrWider);
    EXPECT_EQ(value->type_expression.kind,
              checker::OperandTypeExpressionKind::ModifierField);
    EXPECT_EQ(value->type_expression.modifier_field_id, "type");
    for (const auto carrier : {"b64", "b128"}) {
      for (const bool all_wide : {false, true}) {
        auto module =
            owned_store(store_source(variant, "u32", carrier, all_wide));
        ASSERT_TRUE(module) << module.error().front().message;
        ResolvedModule copied(*module);
        ResolvedModule assigned;
        assigned = copied;
        ResolvedFunction function(copied.functions.front());
        ResolvedFunction function_assigned;
        function_assigned = function;
        ASSERT_NE(function.body.front().get(),
                  copied.functions.front().body.front().get());
        ASSERT_NE(function_assigned.body.front().get(),
                  function.body.front().get());
        assigned.functions.front() = function_assigned;
        assigned.functions.front().body.front() =
            assigned.functions.front().body.front()->clone();
        auto fields =
            store_fields(*assigned.functions.front().body.front(), variant);
        EXPECT_EQ(fields.type.value, ScalarType::U32);
        ASSERT_EQ(fields.lanes.size(), store_arity(variant));
        // Logical transfer/completion bytes are type*arity; the carriers add no lanes.
        EXPECT_EQ(base::scalar_size_of(fields.type.value) * fields.lanes.size(),
                  4u * store_arity(variant));
        for (unsigned lane = 0; lane < fields.lanes.size(); ++lane) {
          const auto& ref = *fields.lanes[lane];
          ASSERT_TRUE(ref.symbol_id);
          EXPECT_EQ(ref.declared_type, lane == 0 || all_wide
                                           ? (std::string_view(carrier) == "b64"
                                                  ? ScalarType::B64
                                                  : ScalarType::B128)
                                           : ScalarType::U32);
          const auto& symbol = assigned.symbols.symbol(*ref.symbol_id);
          EXPECT_EQ(symbol.name, lane == 0 ? "probe" : all_wide ? "tail" : "n");
          EXPECT_EQ(
              ref.parameterized_index,
              lane == 0 ? std::nullopt : std::optional<uint32_t>{lane - 1});
        }
        EXPECT_TRUE(validateModule(assigned));
      }
    }
  }
  EXPECT_EQ(StAsyncSharedScalar::completion_kind,
            base::AsyncCompletionKind::MbarrierCompleteTxBytes);
  EXPECT_EQ(StAsyncSharedClusterV4::completion_kind,
            base::AsyncCompletionKind::MbarrierCompleteTxBytes);
  EXPECT_EQ(StAsyncGlobalRelease::completion_kind,
            base::AsyncCompletionKind::None);
  EXPECT_EQ(StAsyncGlobalMmioRelease::completion_kind,
            base::AsyncCompletionKind::None);
}

TEST(AsyncStoreDataWidth, KeepsWidthKindAndSourceShapePredicatesIndependent) {
  for (unsigned variant = 0; variant < 8; ++variant) {
    SCOPED_TRACE(store_names[variant]);
    for (const auto carrier :
         {"u32", "s32", "b32", "u64", "s64", "b64", "b128"}) {
      const auto result =
          owned_store(store_source(variant, "u32", carrier, true));
      EXPECT_TRUE(result) << result.error().front().message;
    }
    for (const auto carrier : {"f32", "b32", "b64", "b128"}) {
      const auto result =
          owned_store(store_source(variant, "f32", carrier, true));
      EXPECT_TRUE(result) << result.error().front().message;
    }
    for (const auto carrier :
         {"f32", "f64", "f16x2", "b32", "u64", "s64", "b128"}) {
      const auto result =
          owned_store(store_source(variant, "b32", carrier, true));
      EXPECT_TRUE(result) << result.error().front().message;
    }
    // BF16 is a declaration-eligibility negative; valid fundamental carriers
    // separately exercise operand kind, width and predicate-role rejection.
    for (const auto carrier :
         {"b16", "u16", "f32", "f64", "f16x2", "pred", "bf16"}) {
      const auto ast =
          test_helpers::parseModule(store_source(variant, "u32", carrier));
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
    for (const auto carrier : {"u32", "s32", "f64", "f16x2", "b16"}) {
      const auto ast =
          test_helpers::parseModule(store_source(variant, "f32", carrier));
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
    for (const auto operand : {"0", "V1"}) {
      const auto ast = test_helpers::parseModule(
          store_source(variant, "u32", "u32", false, operand));
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
    auto source = store_source(variant, "u32", "u32");
    if (store_arity(variant) != 1) {
      const auto lane = source.find(",n0");
      ASSERT_NE(lane, std::string::npos);
      source.erase(lane, 3);
      const auto ast = test_helpers::parseModule(source);
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
  }
}

TEST(AsyncStoreDataWidth, OwnsWiderComponentsAndCoherentActualTypeMutations) {
  for (unsigned variant = 0; variant < 8; ++variant) {
    SCOPED_TRACE(store_names[variant]);
    const auto ast = test_helpers::parseModule(
        store_source(variant, "u32", "u32", false, "V1.y"));
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto owned =
        owned_store(store_source(variant, "u32", "u32", false, "V1.y"));
    ASSERT_TRUE(owned);
    auto copied = *owned;
    copied.functions.front().body.front() =
        copied.functions.front().body.front()->clone();
    auto fields = store_fields(*copied.functions.front().body.front(), variant);
    auto& ref = *fields.lanes.front();
    ASSERT_TRUE(ref.symbol_id);
    ASSERT_TRUE(ref.component);
    EXPECT_EQ(copied.symbols.symbol(*ref.symbol_id).name, "V");
    EXPECT_EQ(ref.declared_type, ScalarType::B64);
    EXPECT_EQ(ref.parameterized_index, 1);
    EXPECT_EQ(ref.component->lane, 1);
    EXPECT_EQ(ref.component->origin, RegisterComponentOrigin::ExplicitSelector);
    EXPECT_FALSE(ref.vector_width);
    ASSERT_FALSE(fields.locations.empty());
    EXPECT_EQ(ref.component->range, fields.locations.front());
    EXPECT_TRUE(validateModule(copied));
    const auto saved = ref;
    ref.component->lane = 2;
    EXPECT_FALSE(validateModule(copied));
    ref = saved;
    ++ref.component->range.end.column;
    EXPECT_FALSE(validateModule(copied));
    ref = saved;
    ref.parameterized_index = 2;
    EXPECT_FALSE(validateModule(copied));
    ref = saved;
    ref.component.reset();
    ref.vector_width = 2;
    ref.spelling = "V1";
    EXPECT_FALSE(validateModule(copied));
    for (const auto name : {"candidate", "wider", "word", "narrow", "wrong"}) {
      auto retarget = *owned;
      auto& target =
          *store_fields(*retarget.functions.front().body.front(), variant)
               .lanes.front();
      const auto symbol = retarget.symbols.lookup(
          retarget.functions.front().declaration_scope, name);
      ASSERT_TRUE(symbol);
      const auto type = std::string_view(name) == "candidate" ? ScalarType::U64
                        : std::string_view(name) == "wider"   ? ScalarType::B128
                        : std::string_view(name) == "word"    ? ScalarType::U32
                        : std::string_view(name) == "narrow"  ? ScalarType::B16
                                                              : ScalarType::F32;
      target =
          ResolvedRegisterRef{.spelling = name,
                              .register_class = ResolvedRegisterClass::General,
                              .symbol_id = symbol->symbol,
                              .declared_type = type};
      const bool legal = type == ScalarType::U64 || type == ScalarType::B128 ||
                         type == ScalarType::U32;
      EXPECT_EQ(validateModule(retarget).has_value(), legal);
      EXPECT_EQ(validateModule(*ast, retarget).has_value(), legal);
      if (legal) {
        target.declared_type =
            type == ScalarType::B128 ? ScalarType::B64 : ScalarType::B128;
        EXPECT_FALSE(validateModule(retarget));
      }
    }
    const auto replacement = test_helpers::parseModule(
        store_source(variant, "u32", "u32", false, "word"));
    ASSERT_TRUE(replacement && replacement.diagnostics.empty());
    ASSERT_TRUE(resolveAndValidateModule(*replacement));
    const auto stale = validateModule(*replacement, *owned);
    ASSERT_FALSE(stale);
    ASSERT_EQ(stale.error().size(), 1u);
    EXPECT_EQ(stale.error().front().kind,
              checker::CheckDiagnosticKind::ModuleSourceMismatch);
  }
}

TEST(AsyncStoreDataWidth, RetainsInstructionB128GatesAndDistinctMultimemRules) {
  for (const unsigned variant : {0u, 3u}) {
    auto old_carrier = owned_store(
        store_source(variant, "u32", "b128", false, "probe", "8.3", "sm_90"));
    ASSERT_TRUE(old_carrier);
    EXPECT_TRUE(validateModule(*old_carrier));
    for (const auto version : {"9.1", "9.2"}) {
      const auto ast = test_helpers::parseModule(store_source(
          variant, "b128", "b128", false, "probe", version, "sm_90"));
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      EXPECT_EQ(resolveAndValidateModule(*ast).has_value(),
                std::string_view(version) == "9.2");
    }
    const auto small =
        test_helpers::parseModule(store_source(variant, "b128", "b64"));
    ASSERT_TRUE(small && small.diagnostics.empty());
    EXPECT_FALSE(resolveAndValidateModule(*small));
  }
  for (const unsigned variant : {1u, 4u}) {
    for (const auto type : {"u64", "f64"}) {
      auto large = owned_store(store_source(variant, type, "b128", true));
      ASSERT_TRUE(large);
      const auto fields =
          store_fields(*large->functions.front().body.front(), variant);
      EXPECT_EQ(base::scalar_size_of(fields.type.value) * fields.lanes.size(),
                16u);
    }
    const auto bad =
        test_helpers::parseModule(store_source(variant, "b128", "b128", true));
    ASSERT_TRUE(bad && bad.diagnostics.empty());
    EXPECT_FALSE(resolveAndValidateModule(*bad));
  }
  for (const unsigned variant : {2u, 5u}) {
    for (const auto type : {"u64", "f64", "b128"}) {
      const auto bad =
          test_helpers::parseModule(store_source(variant, type, "b128", true));
      ASSERT_TRUE(bad && bad.diagnostics.empty());
      EXPECT_FALSE(resolveAndValidateModule(*bad));
    }
  }
  auto source = store_source(6, "u32", "u64");
  const auto start = source.find("st.async.release.gpu.global.u32");
  ASSERT_NE(start, std::string::npos);
  source.replace(start, std::string("st.async.release.gpu.global.u32").size(),
                 "multimem.st.async.release.gpu.global.u32");
  const auto multimem = test_helpers::parseModule(source);
  ASSERT_TRUE(multimem && multimem.diagnostics.empty());
  EXPECT_FALSE(resolveAndValidateModule(*multimem));
  source = store_source(6, "u32", "u64");
  const auto operation = source.find("st.async.release.gpu.global.u32");
  source.replace(operation,
                 std::string("st.async.release.gpu.global.u32").size(),
                 "st.global.u32");
  const auto sync = test_helpers::parseModule(source);
  ASSERT_TRUE(sync && sync.diagnostics.empty());
  EXPECT_TRUE(resolveAndValidateModule(*sync));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
