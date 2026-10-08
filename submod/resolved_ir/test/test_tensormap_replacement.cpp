#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Build one self-contained descriptor update with a bound 128-byte object. */
std::string replacement_module(
    std::string_view instruction,
    std::string_view declaration = ".global .align 128 .b8 map[128];",
    std::string_view target = "sm_90a") {
  return ".version 9.3\n.target " + std::string(target) +
         "\n.address_size 64\n" + std::string(declaration) +
         "\n.entry kernel() {\n" + std::string(instruction) + "\n}\n";
}

/** Use a catalog profile so exact, family, and capability gates are exercised. */
checker::Context context_for(std::string_view target,
                             checker::PtxVersion version) {
  const auto profile = base::find_target_profile(target);
  if (!profile)
    return {};
  return {
      .target = {.ptx_version = version,
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
}

/** Check a concrete tensor-map form through its public instruction contract. */
checker::CheckResult check_tensormap(const Instruction& instruction,
                                     const checker::Context& context) {
  return instruction.check(context);
}

/** Every replacement field keeps its own variant and encoded field identity. */
TEST(TensorMapReplacement, AllFieldsAndOpaqueReference) {
  constexpr std::array<std::string_view, 11> forms{
      "global_address.b1024.b64 [map], 0;",
      "rank.b1024.b32 [map], 0;",
      "box_dim.b1024.b32 [map], 4, 0;",
      "global_dim.b1024.b32 [map], 4, 0;",
      "global_stride.b1024.b64 [map], 4, 0;",
      "element_stride.b1024.b32 [map], 4, 0;",
      "elemtype.b1024.b32 [map], 12;",
      "interleave_layout.b1024.b32 [map], 2;",
      "swizzle_mode.b1024.b32 [map], 3;",
      "swizzle_atomicity.b1024.b32 [map], 2;",
      "fill_mode.b1024.b32 [map], 1;",
  };
  for (const auto form : forms) {
    const auto parsed = test_helpers::parseModule(
        replacement_module("tensormap.replace.tile." + std::string(form),
                           ".global .align 128 .b8 map[128];", "sm_103a"));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveModuleOnly(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    const auto& update = (*resolved->functions.front().body.front());
    EXPECT_TRUE(
        check_tensormap(update, context_for("sm_103a", {9, 3})).has_value())
        << form;
  }

  std::optional<ResolvedModule> owned;
  {
    const auto parsed = test_helpers::parseModule(replacement_module(
        "tensormap.replace.tile.elemtype.global.b1024.b32 [map], 15;"));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    auto resolved = resolveModuleOnly(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned.emplace(std::move(*resolved));
  }
  const auto* variant = dynamic_cast<const TensormapReplaceTileElemtype*>(
      owned->functions.front().body.front().get());
  ASSERT_NE(variant, nullptr);
  static_assert(TensormapReplaceTileElemtype::replacement_field ==
                TensorMapReplaceField::Elemtype);
  const auto reference = variant->tensor_map_ref();
  ASSERT_TRUE(reference);
  const auto* symbol = std::get_if<ResolvedSymbolRef>(&reference->address.base);
  ASSERT_NE(symbol, nullptr);
  EXPECT_EQ(symbol->spelling, "map");
  EXPECT_EQ(symbol->address_alignment, 128u);
  EXPECT_TRUE(symbol->symbol_id.has_value());
  const auto* stored_symbol =
      std::get_if<ResolvedSymbolRef>(&variant->tensor_map.value.base);
  ASSERT_NE(stored_symbol, nullptr);
  EXPECT_EQ(symbol->symbol_id, stored_symbol->symbol_id);
  EXPECT_EQ(reference->range, variant->tensor_map.locs.front());
  EXPECT_TRUE(variant->encoded_value().has_value());
  EXPECT_EQ(*variant->encoded_value(), TensorMapElementType::B6x16P32OrB6p2x16);
  EXPECT_EQ(variant->new_val.value.integer_source_bits, 15u);
  EXPECT_GT(reference->range.start.line, 0);
}

/** Typed values narrow at use; selector and Table 33 source codes stay exact. */
TEST(TensorMapReplacement, ConvertedValuesAndExactSelectors) {
  constexpr std::array<std::string_view, 5> accepted{
      "rank.b1024.b32 [map], 4294967296;",
      "rank.b1024.b32 [map], -4294967296;",
      "rank.b1024.b32 [map], 4294967297;",
      "box_dim.b1024.b32 [map], 0, 4294967296;",
      "global_address.b1024.b64 [map], 18446744073709551615;",
  };
  for (const auto form : accepted) {
    const auto parsed = test_helpers::parseModule(
        replacement_module("tensormap.replace.tile." + std::string(form)));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveModuleOnly(*parsed);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    EXPECT_TRUE(check_tensormap((*resolved->functions.front().body.front()),
                                context_for("sm_90a", {9, 3}))
                    .has_value())
        << form;
  }
  constexpr std::array<std::string_view, 5> rejected{
      "rank.b1024.b32 [map], 5;",
      "rank.b1024.b32 [map], 4294967301;",
      "box_dim.b1024.b32 [map], 4294967296, 0;",
      "elemtype.b1024.b32 [map], 4294967296;",
      "swizzle_mode.b1024.b32 [map], -4294967296;",
  };
  for (const auto form : rejected) {
    const auto parsed = test_helpers::parseModule(
        replacement_module("tensormap.replace.tile." + std::string(form)));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveModuleOnly(*parsed);
    if (resolved)
      EXPECT_FALSE(check_tensormap((*resolved->functions.front().body.front()),
                                   context_for("sm_90a", {9, 3}))
                       .has_value())
          << form;
  }
  const auto unknown_rank = test_helpers::parseModule(replacement_module(
      ".reg .b32 %rank;\ntensormap.replace.tile.rank.b1024.b32 [map], %rank;"));
  ASSERT_MODULE_PARSE_SUCCEEDS(unknown_rank);
  const auto unknown_ir = resolveModuleOnly(*unknown_rank);
  ASSERT_TRUE(unknown_ir.has_value());
  EXPECT_TRUE(check_tensormap((*unknown_ir->functions.front().body.front()),
                              context_for("sm_90a", {9, 3}))
                  .has_value());
}

/** Fixed field widths and field3 immediate shape reject adjacent spellings. */
TEST(TensorMapReplacement, FieldWidthsAndImmediateShape) {
  constexpr std::array<std::string_view, 4> invalid{
      "tensormap.replace.tile.rank.b1024.b64 [map], 0;",
      "tensormap.replace.tile.global_address.b1024.b32 [map], 0;",
      "tensormap.replace.tile.elemtype.b1024.b64 [map], 0;",
      ".reg .b32 %value;\ntensormap.replace.tile.elemtype.b1024.b32 "
      "[map], %value;",
  };
  for (const auto form : invalid) {
    const auto parsed = test_helpers::parseModule(replacement_module(form));
    if (!parsed.has_value() || !parsed.diagnostics.empty())
      continue;
    EXPECT_FALSE(resolveModuleOnly(*parsed).has_value()) << form;
  }
}

/** Exact and family target clauses remain distinct from field-code floors. */
TEST(TensorMapReplacement, TargetAndCodeAvailability) {
  const auto basic = test_helpers::parseModule(
      replacement_module("tensormap.replace.tile.rank.b1024.b32 [map], 0;"));
  ASSERT_MODULE_PARSE_SUCCEEDS(basic);
  const auto basic_ir = resolveModuleOnly(*basic);
  ASSERT_TRUE(basic_ir.has_value());
  const auto& update = (*basic_ir->functions.front().body.front());
  for (const auto [target, version, available] :
       std::array<std::tuple<std::string_view, checker::PtxVersion, bool>, 11>{{
           {"sm_90a", {8, 3}, true},
           {"sm_90", {9, 3}, false},
           {"sm_100a", {8, 6}, true},
           {"sm_100", {9, 3}, false},
           {"sm_100f", {8, 8}, true},
           {"sm_103a", {8, 8}, true},
           {"sm_110a", {9, 0}, true},
           {"sm_110", {9, 3}, false},
           {"sm_120a", {8, 7}, true},
           {"sm_120f", {8, 8}, true},
           {"sm_121a", {8, 8}, true},
       }})
    EXPECT_EQ(check_tensormap(update, context_for(target, version)).has_value(),
              available)
        << target;

  for (const auto [form, target, version, available] :
       std::array<std::tuple<std::string_view, std::string_view,
                             checker::PtxVersion, bool>,
                  8>{{
           {"swizzle_atomicity.b1024.b32 [map], 0;", "sm_90a", {9, 3}, false},
           {"swizzle_atomicity.b1024.b32 [map], 0;", "sm_100a", {8, 6}, true},
           {"elemtype.b1024.b32 [map], 15;", "sm_100a", {8, 6}, false},
           {"elemtype.b1024.b32 [map], 15;", "sm_120a", {8, 7}, true},
           {"swizzle_mode.b1024.b32 [map], 4;", "sm_100f", {9, 3}, false},
           {"swizzle_mode.b1024.b32 [map], 4;", "sm_103f", {9, 3}, false},
           {"swizzle_mode.b1024.b32 [map], 4;", "sm_103a", {8, 7}, false},
           {"swizzle_mode.b1024.b32 [map], 4;", "sm_103a", {8, 8}, true},
       }}) {
    const auto parsed = test_helpers::parseModule(
        replacement_module("tensormap.replace.tile." + std::string(form),
                           ".global .align 128 .b8 map[128];", "sm_103a"));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveModuleOnly(*parsed);
    ASSERT_TRUE(resolved.has_value()) << form;
    EXPECT_EQ(check_tensormap((*resolved->functions.front().body.front()),
                              context_for(target, version))
                  .has_value(),
              available)
        << form << ' ' << target;
  }
}

/** The weak whole-object operation checks known 128-byte alignment. */
TEST(TensorMapReplacement, AddressSpaceAndAlignment) {
  for (const auto [declaration, valid] :
       std::array<std::pair<std::string_view, bool>, 5>{{
           {".global .align 128 .b8 map[128];", true},
           {".shared .align 128 .b8 map[128];", true},
           {".global .align 64 .b8 map[128];", false},
           {".const .align 128 .b8 map[128];", false},
           {".local .align 128 .b8 map[128];", false},
       }}) {
    const auto parsed = test_helpers::parseModule(replacement_module(
        "tensormap.replace.tile.rank.b1024.b32 [map], 0;", declaration));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveModuleOnly(*parsed);
    if (resolved)
      EXPECT_EQ(check_tensormap((*resolved->functions.front().body.front()),
                                context_for("sm_90a", {9, 3}))
                    .has_value(),
                valid);
    else
      EXPECT_FALSE(valid) << declaration;
  }
  for (const auto [offset, valid] : std::array<std::pair<int, bool>, 3>{
           {{0, true}, {64, false}, {128, true}}}) {
    const auto parsed = test_helpers::parseModule(
        replacement_module("tensormap.replace.tile.rank.b1024.b32 [map+" +
                           std::to_string(offset) + "], 0;"));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveModuleOnly(*parsed);
    if (resolved)
      EXPECT_EQ(check_tensormap((*resolved->functions.front().body.front()),
                                context_for("sm_90a", {9, 3}))
                    .has_value(),
                valid)
          << offset;
    else
      EXPECT_FALSE(valid) << offset;
  }
  for (const auto [qualifier, declaration, valid] :
       std::array<std::tuple<std::string_view, std::string_view, bool>, 4>{{
           {".global", ".global .align 128 .b8 map[128];", true},
           {".shared::cta", ".shared .align 128 .b8 map[128];", true},
           {".global", ".shared .align 128 .b8 map[128];", false},
           {".shared::cta", ".global .align 128 .b8 map[128];", false},
       }}) {
    const auto parsed = test_helpers::parseModule(
        replacement_module("tensormap.replace.tile.rank" +
                               std::string(qualifier) + ".b1024.b32 [map], 0;",
                           declaration));
    ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
    const auto resolved = resolveModuleOnly(*parsed);
    if (resolved)
      EXPECT_EQ(check_tensormap((*resolved->functions.front().body.front()),
                                context_for("sm_90a", {9, 3}))
                    .has_value(),
                valid);
    else
      EXPECT_FALSE(valid);
  }
  for (const auto [register_type, valid] :
       std::array<std::pair<std::string_view, bool>, 10>{{{"b8", false},
                                                          {"b16", false},
                                                          {"u8", false},
                                                          {"u16", false},
                                                          {"b32", true},
                                                          {"b64", true},
                                                          {"u32", true},
                                                          {"u64", true},
                                                          {"b128", false},
                                                          {"f32", false}}}) {
    const auto parsed = test_helpers::parseModule(replacement_module(
        ".reg ." + std::string(register_type) +
        " %addr;\ntensormap.replace.tile.rank.b1024.b32 [%addr], 0;"));
    if (!parsed.has_value() || !parsed.diagnostics.empty()) {
      EXPECT_FALSE(valid) << register_type;
      continue;
    }
    const auto resolved = resolveModuleOnly(*parsed);
    if (resolved)
      EXPECT_EQ(check_tensormap((*resolved->functions.front().body.front()),
                                context_for("sm_90a", {9, 3}))
                    .has_value(),
                valid)
          << register_type;
    else
      EXPECT_FALSE(valid) << register_type;
  }
}

/** Proxy fence has a fixed 128-byte copy and separate scope variants. */
TEST(TensorMapReplacement, ProxyFenceScopesAndSize) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .align 128 .b8 gmap[128];
.shared .align 128 .b8 smap[128];
.entry kernel() {
  tensormap.cp_fenceproxy.global.shared::cta.tensormap::generic.release.cta.sync.aligned [gmap], [smap], 128;
  tensormap.cp_fenceproxy.global.shared::cta.tensormap::generic.release.cluster.sync.aligned [gmap], [smap], 128;
  tensormap.cp_fenceproxy.global.shared::cta.tensormap::generic.release.gpu.sync.aligned [gmap], [smap], 128;
  tensormap.cp_fenceproxy.global.shared::cta.tensormap::generic.release.sys.sync.aligned [gmap], [smap], 128;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveModuleOnly(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 4u);
  for (const auto& instruction : body)
    EXPECT_TRUE(check_tensormap((*instruction), context_for("sm_90", {9, 3}))
                    .has_value());
  auto* cta = dynamic_cast<TensormapCpFenceproxyOrdinary*>(body[0].get());
  auto* cluster = dynamic_cast<TensormapCpFenceproxyCluster*>(body[1].get());
  ASSERT_NE(cta, nullptr);
  ASSERT_NE(cluster, nullptr);
  EXPECT_EQ(cta->scope.value, MemoryScope::Cta);
  EXPECT_EQ(cluster->scope.value, MemoryScope::Cluster);
  EXPECT_EQ(cta->proxy_pair, ProxyKindPair::TensormapToGeneric);
  EXPECT_EQ(cta->semantics, MemoryConsistency::Release);
  EXPECT_EQ(cta->size.value.bits, 128u);
  cta->size.value.bits = 64;
  EXPECT_FALSE(
      check_tensormap((*body[0]), context_for("sm_90", {9, 3})).has_value());
  cta->size.value.bits = 128;
  cta->size.value.integer_source_bits.reset();
  EXPECT_FALSE(
      check_tensormap((*body[0]), context_for("sm_90", {9, 3})).has_value());
  cta->size.value.integer_source_bits = 0x100000080ULL;
  EXPECT_FALSE(
      check_tensormap((*body[0]), context_for("sm_90", {9, 3})).has_value());
  for (const auto [size, valid] :
       std::array<std::pair<std::string_view, bool>, 3>{
           {{"128", true}, {"64", false}, {"4294967424", false}}}) {
    auto source = replacement_module(
        "tensormap.cp_fenceproxy.global.shared::cta.tensormap::generic."
        "release.gpu.sync.aligned [gmap], [smap], " +
            std::string(size) + ";",
        ".global .align 128 .b8 gmap[128];\n.shared .align 128 .b8 smap[128];",
        "sm_90");
    const auto candidate = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(candidate);
    const auto checked = resolveModuleOnly(*candidate);
    if (checked)
      EXPECT_EQ(check_tensormap((*checked->functions.front().body.front()),
                                context_for("sm_90", {9, 3}))
                    .has_value(),
                valid)
          << size;
    else
      EXPECT_FALSE(valid) << size;
  }
  for (const auto [global_alignment, shared_alignment, valid] :
       std::array<std::tuple<int, int, bool>, 3>{
           {{128, 128, true}, {64, 128, false}, {128, 64, false}}}) {
    const auto candidate = test_helpers::parseModule(replacement_module(
        "tensormap.cp_fenceproxy.global.shared::cta.tensormap::generic."
        "release.gpu.sync.aligned [gmap], [smap], 128;",
        ".global .align " + std::to_string(global_alignment) +
            " .b8 gmap[128];\n.shared .align " +
            std::to_string(shared_alignment) + " .b8 smap[128];",
        "sm_90"));
    ASSERT_MODULE_PARSE_SUCCEEDS(candidate);
    const auto checked = resolveModuleOnly(*candidate);
    if (checked)
      EXPECT_EQ(check_tensormap((*checked->functions.front().body.front()),
                                context_for("sm_90", {9, 3}))
                    .has_value(),
                valid);
    else
      EXPECT_FALSE(valid);
  }
  const auto reversed = test_helpers::parseModule(replacement_module(
      "tensormap.cp_fenceproxy.global.shared::cta.tensormap::generic."
      "release.gpu.sync.aligned [smap], [gmap], 128;",
      ".global .align 128 .b8 gmap[128];\n.shared .align 128 .b8 smap[128];",
      "sm_90"));
  ASSERT_MODULE_PARSE_SUCCEEDS(reversed);
  const auto reversed_ir = resolveModuleOnly(*reversed);
  if (reversed_ir)
    EXPECT_FALSE(check_tensormap((*reversed_ir->functions.front().body.front()),
                                 context_for("sm_90", {9, 3}))
                     .has_value());
  for (const auto [dst_offset, src_offset, valid] :
       std::array<std::tuple<int, int, bool>, 3>{
           {{128, 128, true}, {64, 128, false}, {128, 64, false}}}) {
    const auto shifted = test_helpers::parseModule(replacement_module(
        "tensormap.cp_fenceproxy.global.shared::cta.tensormap::generic."
        "release.gpu.sync.aligned [gmap+" +
            std::to_string(dst_offset) + "], [smap+" +
            std::to_string(src_offset) + "], 128;",
        ".global .align 128 .b8 gmap[256];\n.shared .align 128 .b8 smap[256];",
        "sm_90"));
    ASSERT_MODULE_PARSE_SUCCEEDS(shifted);
    const auto shifted_ir = resolveModuleOnly(*shifted);
    if (shifted_ir)
      EXPECT_EQ(check_tensormap((*shifted_ir->functions.front().body.front()),
                                context_for("sm_90", {9, 3}))
                    .has_value(),
                valid);
    else
      EXPECT_FALSE(valid);
  }
  for (const auto [dst_type, src_type, valid] :
       std::array<std::tuple<std::string_view, std::string_view, bool>, 3>{{
           {"b64", "b32", true},
           {"b16", "b64", false},
           {"b64", "b8", false},
       }}) {
    const auto registers = test_helpers::parseModule(replacement_module(
        ".reg ." + std::string(dst_type) + " %dst;\n.reg ." +
            std::string(src_type) +
            " %src;\ntensormap.cp_fenceproxy.global.shared::cta."
            "tensormap::generic.release.gpu.sync.aligned [%dst], [%src], 128;",
        ".global .align 128 .b8 unused[128];", "sm_90"));
    ASSERT_MODULE_PARSE_SUCCEEDS(registers);
    const auto register_ir = resolveModuleOnly(*registers);
    if (register_ir)
      EXPECT_EQ(check_tensormap((*register_ir->functions.front().body.front()),
                                context_for("sm_90", {9, 3}))
                    .has_value(),
                valid);
    else
      EXPECT_FALSE(valid);
  }
}

/** Public code decoding is closed over field and exact source-code pairs. */
TEST(TensorMapReplacement, EncodedTableAndWrongDomains) {
  EXPECT_EQ(tensor_map_encoded_codes.size(), 30u);
  for (const auto& row : tensor_map_encoded_codes) {
    const ResolvedImmediate value{.bits = row.code,
                                  .type = ScalarType::B32,
                                  .is_negative = false,
                                  .integer_source_bits = row.code};
    const auto decoded = tensor_map_encoded_code(row.field, value);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->code, row.code);
    EXPECT_EQ(decoded->field, row.field);
    EXPECT_EQ(decoded->value, row.value);
    EXPECT_FALSE(tensor_map_encoded_code(TensorMapReplaceField::Rank, value));
  }
  for (const auto [field, first_invalid] :
       std::array<std::pair<TensorMapReplaceField, uint64_t>, 5>{{
           {TensorMapReplaceField::Elemtype, 16},
           {TensorMapReplaceField::InterleaveLayout, 3},
           {TensorMapReplaceField::SwizzleMode, 5},
           {TensorMapReplaceField::SwizzleAtomicity, 4},
           {TensorMapReplaceField::FillMode, 2},
       }}) {
    const ResolvedImmediate value{.bits = first_invalid,
                                  .type = ScalarType::B32,
                                  .is_negative = false,
                                  .integer_source_bits = first_invalid};
    EXPECT_FALSE(tensor_map_encoded_code(field, value));
  }
  const ResolvedImmediate zero{.bits = 0,
                               .type = ScalarType::B32,
                               .is_negative = false,
                               .integer_source_bits = 0};
  EXPECT_FALSE(project_tensor_map_encoded_value<TensorMapFillMode>(
      TensorMapReplaceField::SwizzleMode, zero));
  ResolvedImmediate invalid = zero;
  invalid.integer_source_bits = 0x100000000ULL;
  EXPECT_FALSE(
      tensor_map_encoded_code(TensorMapReplaceField::Elemtype, invalid));
  invalid.integer_source_bits = 0;
  invalid.bits = 1;
  EXPECT_FALSE(
      tensor_map_encoded_code(TensorMapReplaceField::Elemtype, invalid));
  invalid.bits = 0;
  invalid.type = ScalarType::B64;
  EXPECT_FALSE(
      tensor_map_encoded_code(TensorMapReplaceField::Elemtype, invalid));
  invalid.type = ScalarType::B32;
  invalid.integer_source_bits.reset();
  EXPECT_FALSE(
      tensor_map_encoded_code(TensorMapReplaceField::Elemtype, invalid));
}

/** Malformed owned field codes cannot be projected or accepted by the checker. */
TEST(TensorMapReplacement, OwnedCodeMutation) {
  const auto parsed = test_helpers::parseModule(replacement_module(
      "tensormap.replace.tile.swizzle_mode.b1024.b32 [map], 3;"));
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveModuleOnly(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  auto& update = (*resolved->functions.front().body.front());
  auto& mode = dynamic_cast<TensormapReplaceTileSwizzleMode&>(update);
  ASSERT_EQ(mode.encoded_value(), TensorMapSwizzleMode::Bytes128);
  mode.new_val.value.bits = 2;
  EXPECT_FALSE(mode.encoded_value().has_value());
  EXPECT_FALSE(
      check_tensormap(update, context_for("sm_90a", {9, 3})).has_value());
  mode.new_val.value.bits = 3;
  mode.new_val.value.integer_source_bits = 0x100000003ULL;
  EXPECT_FALSE(mode.encoded_value().has_value());
  EXPECT_FALSE(
      check_tensormap(update, context_for("sm_90a", {9, 3})).has_value());
  mode.new_val.value.integer_source_bits.reset();
  EXPECT_FALSE(mode.encoded_value().has_value());
  EXPECT_FALSE(
      check_tensormap(update, context_for("sm_90a", {9, 3})).has_value());
  mode.tensor_map.locs.clear();
  EXPECT_FALSE(mode.tensor_map_ref().has_value());
}

/** Rank and ordinal mutations are rechecked against their distinct domains. */
TEST(TensorMapReplacement, OwnedRankAndOrdinalMutation) {
  const auto rank_parsed = test_helpers::parseModule(replacement_module(
      "tensormap.replace.tile.rank.b1024.b32 [map], 4294967297;"));
  ASSERT_MODULE_PARSE_SUCCEEDS(rank_parsed);
  auto rank_ir = resolveModuleOnly(*rank_parsed);
  ASSERT_TRUE(rank_ir.has_value());
  auto& rank_update = (*rank_ir->functions.front().body.front());
  auto& rank = dynamic_cast<TensormapReplaceTileRank&>(rank_update);
  auto& rank_value = std::get<ResolvedImmediate>(rank.new_val.value);
  EXPECT_EQ(rank_value.bits, 1u);
  EXPECT_EQ(rank_value.integer_source_bits, 0x100000001ULL);
  rank_value.bits = 5;
  EXPECT_FALSE(
      check_tensormap(rank_update, context_for("sm_90a", {9, 3})).has_value());
  rank_value.bits = 1;
  rank_value.integer_source_bits = 0;
  EXPECT_FALSE(
      check_tensormap(rank_update, context_for("sm_90a", {9, 3})).has_value());

  const auto ordinal_parsed = test_helpers::parseModule(replacement_module(
      "tensormap.replace.tile.box_dim.b1024.b32 [map], 4, 0;"));
  ASSERT_MODULE_PARSE_SUCCEEDS(ordinal_parsed);
  auto ordinal_ir = resolveModuleOnly(*ordinal_parsed);
  ASSERT_TRUE(ordinal_ir.has_value());
  auto& ordinal_update = (*ordinal_ir->functions.front().body.front());
  auto& box = dynamic_cast<TensormapReplaceTileBoxDim&>(ordinal_update);
  box.ord.value.integer_source_bits = 0x100000004ULL;
  EXPECT_FALSE(check_tensormap(ordinal_update, context_for("sm_90a", {9, 3}))
                   .has_value());
}

/** Bound pointer widths remain checked after a caller edits the public IR. */
TEST(TensorMapReplacement, OwnedAddressRegisterMutation) {
  const auto parsed = test_helpers::parseModule(replacement_module(
      ".reg .b64 %addr;\ntensormap.replace.tile.rank.b1024.b32 [%addr], 0;"));
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveModuleOnly(*parsed);
  ASSERT_TRUE(resolved.has_value());
  auto& update = (*resolved->functions.front().body.front());
  auto& rank = dynamic_cast<TensormapReplaceTileRank&>(update);
  auto& address_register =
      std::get<ResolvedRegisterRef>(rank.tensor_map.value.base);
  ASSERT_TRUE(address_register.symbol_id);
  EXPECT_TRUE(
      check_tensormap(update, context_for("sm_90a", {9, 3})).has_value());
  EXPECT_TRUE(
      validateModule(*resolved, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
  address_register.vector_width = 2;
  EXPECT_FALSE(
      check_tensormap(update, context_for("sm_90a", {9, 3})).has_value());
  EXPECT_FALSE(
      validateModule(*resolved, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
  address_register.vector_width.reset();
  address_register.declared_type = ScalarType::B16;
  EXPECT_FALSE(
      check_tensormap(update, context_for("sm_90a", {9, 3})).has_value());
  address_register.declared_type.reset();
  EXPECT_FALSE(
      check_tensormap(update, context_for("sm_90a", {9, 3})).has_value());
}

/** Both proxy-copy pointer roles reject vector shapes in AST-released IR. */
TEST(TensorMapReplacement, OwnedProxyFenceVectorAddressMutation) {
  const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.entry kernel() {
  .reg .b64 %dst;
  .reg .b64 %src;
  tensormap.cp_fenceproxy.global.shared::cta.tensormap::generic.release.cta.sync.aligned [%dst], [%src], 128;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto resolved = resolveModuleOnly(*parsed);
  ASSERT_TRUE(resolved.has_value());
  auto& update = (*resolved->functions.front().body.front());
  auto& fence = dynamic_cast<TensormapCpFenceproxyOrdinary&>(update);
  auto& dst = std::get<ResolvedRegisterRef>(fence.dst.value.base);
  auto& src = std::get<ResolvedRegisterRef>(fence.src.value.base);
  ASSERT_TRUE(dst.symbol_id);
  ASSERT_TRUE(src.symbol_id);
  ASSERT_TRUE(
      check_tensormap(update, context_for("sm_90", {9, 3})).has_value());
  ASSERT_TRUE(
      validateModule(*resolved, ModuleValidationPolicy::RequireCompleteContext)
          .has_value());
  for (auto* pointer : {&dst, &src}) {
    pointer->vector_width = 2;
    EXPECT_FALSE(
        check_tensormap(update, context_for("sm_90", {9, 3})).has_value());
    EXPECT_FALSE(validateModule(*resolved,
                                ModuleValidationPolicy::RequireCompleteContext)
                     .has_value());
    pointer->vector_width.reset();
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
