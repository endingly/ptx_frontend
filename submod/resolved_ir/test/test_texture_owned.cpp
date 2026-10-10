#include <gtest/gtest.h>

#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ptx_frontend/resolved_ir/model/data_movement/tex.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/tld4.gen.hpp>
#include <ptx_frontend/resolved_ir/model/data_movement/txq.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve a complete fixture, allowing the syntax tree to die before checking. */
std::optional<ResolvedModule> owned_texture_module(std::string_view source) {
  auto parsed = [&]() {
    try {
      return test_helpers::parseModule(source);
    } catch (const std::exception& error) {
      std::cerr << "texture parse exception: " << error.what() << '\n';
      throw;
    }
  }();
  if (!parsed.has_value() || !parsed.diagnostics.empty()) {
    for (const auto& diagnostic : parsed.diagnostics)
      std::cerr << "texture parse: " << diagnostic.message << '\n';
    return std::nullopt;
  }
  auto resolved = [&]() {
    try {
      return resolveModuleOnly(*parsed);
    } catch (const std::exception& error) {
      std::cerr << "texture resolve exception: " << error.what() << '\n';
      throw;
    }
  }();
  if (!resolved) {
    for (const auto& diagnostic : resolved.error())
      std::cerr << "texture resolve: " << diagnostic.message << '\n';
    return std::nullopt;
  }
  return std::move(*resolved);
}

/** Unified texture, gather, query, and opaque-kind forms share one owned module. */
TEST(TextureOwned, UnifiedFamiliesValidateWithoutAst) {
  const auto module = owned_texture_module(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.global .texref tex0;
.entry kernel() {
  .reg .pred %p;
  .reg .b32 %b<4>;
  .reg .f32 %f<16>;
  .reg .u64 %rd;
  mov.u64 %rd, tex0;
  istypep.texref %p, %rd;
  txq.width.b32 %b0, [tex0];
  tex.2d.v4.f32.f32 {%f0,%f1,%f2,%f3}, [tex0,{%f4,%f5}];
  tld4.r.2d.v4.f32.f32 {%f6,%f7,%f8,%f9}, [tex0,{%f10,%f11}];
  ret;
}
)ptx");
  ASSERT_TRUE(module.has_value());
  ASSERT_EQ(module->functions.size(), 1u);
  ASSERT_EQ(module->functions.front().body.size(), 6u);
  EXPECT_TRUE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
}

/** Bracket omission remains a source-visible tex topology after parsing. */
TEST(TextureOwned, OmittedBracketsValidateWithoutAst) {
  const auto module = owned_texture_module(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.global .texref tex0;
.entry kernel() {
  .reg .f32 %f<6>;
  tex.1d.v4.f32.f32 {%f0,%f1,%f2,%f3}, tex0,%f4;
  tex.1d.v4.f32.f32 {%f0,%f1,%f2,%f3}, tex0,{%f4,%f5,%f4,%f5};
  ret;
}
)ptx");
  ASSERT_TRUE(module.has_value());
  EXPECT_TRUE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
}

/** A sampler-only query cannot be selected in unified texturing mode. */
TEST(TextureOwned, ForceUnnormalizedQueryRequiresIndependentMode) {
  EXPECT_FALSE(owned_texture_module(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.global .samplerref samp0;
.entry kernel() {
  .reg .b32 %b;
  txq.force_unnormalized_coords.b32 %b, [samp0];
  ret;
}
)ptx")
                   .has_value());
}

/** Explicit base uses the mipmap feature floor while omitted mode is older. */
TEST(TextureOwned, ExplicitBaseRequiresMipmapAvailability) {
  const auto module = owned_texture_module(R"ptx(
.version 3.0
.target sm_20
.address_size 64
.global .texref tex0;
.entry kernel() {
  .reg .f32 %f<5>;
  tex.base.1d.v4.f32.f32 {%f0,%f1,%f2,%f3}, [tex0,{%f4}];
  ret;
}
)ptx");
  ASSERT_TRUE(module.has_value());
  EXPECT_FALSE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
}

/** Independent mode binds two direct opaque identities and rejects a register head. */
TEST(TextureOwned, IndependentAccessRequiresDirectTextureAndSampler) {
  const auto direct = owned_texture_module(R"ptx(
.version 9.3
.target sm_80, texmode_independent
.address_size 64
.global .texref tex0;
.global .samplerref samp0;
.entry kernel() {
  .reg .f32 %f<10>;
  tex.2d.v4.f32.f32 {%f0,%f1,%f2,%f3}, [tex0,samp0,{%f4,%f5}];
  tld4.r.2d.v4.f32.f32 {%f0,%f1,%f2,%f3}, [tex0,samp0,{%f4,%f5}];
  ret;
}
)ptx");
  ASSERT_TRUE(direct.has_value());
  EXPECT_TRUE(
      validateModule(*direct, ModuleValidationPolicy::RequireCompleteContext));
  EXPECT_FALSE(owned_texture_module(R"ptx(
.version 9.3
.target sm_80, texmode_independent
.address_size 64
.global .texref tex0;
.global .samplerref samp0;
.entry kernel() {
  .reg .u64 %rd;
  .reg .f32 %f<6>;
  mov.u64 %rd, tex0;
  tex.2d.v4.f32.f32 {%f0,%f1,%f2,%f3}, [%rd,samp0,{%f4,%f5}];
  ret;
}
)ptx")
                   .has_value());
}

/** An indirect texture handle has its own feature gate, independent of form age. */
TEST(TextureOwned, IndirectResourceRequiresPtx31) {
  const auto module = owned_texture_module(R"ptx(
.version 3.0
.target sm_20
.address_size 64
.entry kernel() {
  .reg .u64 %rd;
  .reg .f32 %f<5>;
  tex.1d.v4.f32.f32 {%f0,%f1,%f2,%f3}, [%rd,{%f4}];
  ret;
}
)ptx");
  ASSERT_TRUE(module.has_value());
  EXPECT_FALSE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
}

/** Opaque static members and entry inputs preserve independent identities. */
TEST(TextureOwned, OpaqueStaticAndEntryRecords) {
  const auto module = owned_texture_module(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.global .texref tex0;
.global .samplerref samp0 = { filter_mode = nearest };
.entry kernel(.param .texref input_tex) {
  ret;
}
)ptx");
  ASSERT_TRUE(module.has_value());
  ASSERT_EQ(module->storage_declarations.size(), 2u);
  ASSERT_EQ(module->functions.front().opaque_entry_parameters.size(), 1u);
  EXPECT_EQ(module->storage_declarations[1].opaque_static_objects.size(), 1u);
  EXPECT_TRUE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
}

/** Public final-form checking rechecks duplicated result destinations. */
TEST(TextureOwned, PublicFormCheckRejectsDuplicateResult) {
  auto module = owned_texture_module(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.global .texref tex0;
.entry kernel() {
  .reg .f32 %f<6>;
  tex.2d.v4.f32.f32 {%f0,%f1,%f2,%f3}, [tex0,{%f4,%f5}];
  ret;
}
)ptx");
  ASSERT_TRUE(module.has_value());
  auto* form = dynamic_cast<TexOmitted2dF32U32S32F32*>(
      module->functions.front().body.front().get());
  ASSERT_NE(form, nullptr);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 80},
      .instruction_range = module->functions.front().instruction_ranges.front(),
  };
  EXPECT_TRUE(form->check(context));
  form->dst.value.data.elements[1] = form->dst.value.data.elements[0];
  EXPECT_FALSE(form->check(context));
  EXPECT_FALSE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
}

/** Optional tails and mixed coordinate lanes survive into one owned module. */
TEST(TextureOwned, OptionalControlsAndMixedCoordinates) {
  const auto module = owned_texture_module(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.global .texref tex0;
.entry kernel() {
  .reg .pred %p;
  .reg .b32 %b<8>;
  .reg .s32 %r<16>;
  .reg .f32 %f<24>;
  tex.2d.v4.f32.f32 {%f0,%f1,%f2,%f3}|%p,
      [tex0,{%f4,%f5}], {%r0,%r1}, %f6;
  tex.grad.2d.v4.f32.f32 {%f0,%f1,%f2,%f3},
      [tex0,{%f4,%f5}], {%f7,%f8}, {%f9,%f10};
  tex.2dms.v4.s32.s32 {%r2,%r3,%r4,%r5},
      [tex0,{%b0,%b1,%b2,%b3}];
  tex.a2dms.v4.s32.s32 {%r6,%r7,%r8,%r9},
      [tex0,{%b4,%b5,%b6,%b7}];
  ret;
}
)ptx");
  ASSERT_TRUE(module.has_value());
  EXPECT_TRUE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
}

/** Depth comparisons accept typed scalar immediates on tex and tld4 forms. */
TEST(TextureOwned, DepthCompareImmediatesRetainScalarContract) {
  auto module = owned_texture_module(R"ptx(
.version 9.3
.target sm_80
.global .texref tex0;
.entry kernel() {
  .reg .f32 %f<7>;
  tex.2d.v4.f32.f32 {%f0,%f1,%f2,%f3},
      [tex0,{%f4,%f5}], 0f3f000000;
  tex.2d.v4.f32.f32 {%f0,%f1,%f2,%f3},
      [tex0,{%f4,%f5}], {-8,7}, 0f3f000000;
  tld4.r.2d.v4.f32.f32 {%f0,%f1,%f2,%f3},
      [tex0,{%f4,%f5}], 0f3f000000;
  tex.level.2d.v4.f32.f32 {%f0,%f1,%f2,%f3},
      [tex0,{%f4,%f5}], 0f00000000, 0f3f000000;
  tex.2d.v4.f32.f32 {%f0,%f1,%f2,%f3},
      [tex0,{%f4,%f5}], %f6;
  ret;
}
)ptx");
  ASSERT_TRUE(module);
  ASSERT_TRUE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
  ASSERT_EQ(module->functions.front().body.size(), 6u);

  auto* plain = dynamic_cast<TexOmitted2dF32U32S32F32*>(
      module->functions.front().body[0].get());
  auto* offset = dynamic_cast<TexOmitted2dF32U32S32F32*>(
      module->functions.front().body[1].get());
  auto* gather =
      dynamic_cast<Tld4R2d*>(module->functions.front().body[2].get());
  auto* mip = dynamic_cast<TexLevel2dF32U32S32F32*>(
      module->functions.front().body[3].get());
  auto* register_compare = dynamic_cast<TexOmitted2dF32U32S32F32*>(
      module->functions.front().body[4].get());
  ASSERT_NE(plain, nullptr);
  ASSERT_NE(offset, nullptr);
  ASSERT_NE(gather, nullptr);
  ASSERT_NE(mip, nullptr);
  ASSERT_NE(register_compare, nullptr);
  ASSERT_TRUE(plain->compare);
  ASSERT_TRUE(offset->compare);
  ASSERT_TRUE(gather->compare);
  ASSERT_TRUE(mip->compare);
  ASSERT_TRUE(register_compare->compare);
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(plain->compare->value));
  EXPECT_TRUE(offset->offset.has_value());
  EXPECT_TRUE(
      std::holds_alternative<ResolvedImmediate>(offset->compare->value));
  EXPECT_TRUE(
      std::holds_alternative<ResolvedImmediate>(gather->compare->value));
  EXPECT_TRUE(std::holds_alternative<ResolvedImmediate>(mip->compare->value));
  EXPECT_TRUE(std::holds_alternative<ResolvedRegisterRef>(
      register_compare->compare->value));

  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 80},
      .instruction_range = module->functions.front().instruction_ranges[0],
  };
  auto& immediate = std::get<ResolvedImmediate>(plain->compare->value);
  EXPECT_EQ(immediate.type, ScalarType::F32);
  EXPECT_TRUE(plain->check(context));
  immediate.type = ScalarType::S32;
  EXPECT_FALSE(plain->check(context));
  immediate.type = ScalarType::F32;
  EXPECT_TRUE(plain->check(context));
  const auto saved_compare = plain->compare;
  plain->compare.reset();
  EXPECT_FALSE(plain->check(context));
  plain->compare = saved_compare;
  EXPECT_TRUE(plain->check(context));
}

/** Texture result bit buckets preserve lane positions and reject source sinks. */
TEST(TextureOwned, TextureResultSinksRetainDestinationPolicy) {
  auto module = owned_texture_module(R"ptx(
.version 9.3
.target sm_80
.global .texref tex0;
.entry kernel() {
  .reg .f32 %f<7>;
  tex.2d.v4.f32.f32 {%f0,%f1,_,_}, [tex0,{%f4,%f5}];
  tld4.r.2d.v4.f32.f32 {_,%f1,%f2,_}, [tex0,{%f4,%f5}];
  ret;
}
)ptx");
  ASSERT_TRUE(module);
  ASSERT_TRUE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
  auto* tex = dynamic_cast<TexOmitted2dF32U32S32F32*>(
      module->functions.front().body[0].get());
  auto* tld4 = dynamic_cast<Tld4R2d*>(module->functions.front().body[1].get());
  ASSERT_NE(tex, nullptr);
  ASSERT_NE(tld4, nullptr);
  ASSERT_EQ(tex->dst.value.data.elements.size(), 4u);
  ASSERT_EQ(tex->dst.value.data_ranges.size(), 4u);
  EXPECT_NE(tex->dst.value.data_ranges[2], tex->dst.value.data_ranges[3]);
  EXPECT_FALSE(tex->dst.value.data.elements[2]);
  EXPECT_FALSE(tex->dst.value.data.elements[3]);
  EXPECT_FALSE(tld4->dst.value.data.elements[0]);
  EXPECT_FALSE(tld4->dst.value.data.elements[3]);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 80},
      .instruction_range = module->functions.front().instruction_ranges[0],
  };
  ASSERT_TRUE(tex->check(context));
  const auto saved_first = tex->dst.value.data.elements[0];
  const auto saved_second = tex->dst.value.data.elements[1];
  tex->dst.value.data.elements[0].reset();
  tex->dst.value.data.elements[1].reset();
  EXPECT_FALSE(tex->check(context));
  tex->dst.value.data.elements[0] = saved_first;
  tex->dst.value.data.elements[1] = saved_second;
  ASSERT_TRUE(tex->check(context));
  tex->dst.value.data.elements[1]->register_class =
      ResolvedRegisterClass::Predicate;
  EXPECT_FALSE(tex->check(context));
  tex->dst.value.data.elements[1] = saved_second;
  tex->dst.value.data.elements[1] = saved_first;
  EXPECT_FALSE(tex->check(context));
  tex->dst.value.data.elements[1] = saved_second;
  EXPECT_TRUE(tex->check(context));

  EXPECT_FALSE(owned_texture_module(R"ptx(
.version 9.3
.target sm_80
.global .texref tex0;
.entry kernel() {
  .reg .f32 %f<6>;
  tex.2d.v4.f32.f32 {%f0,%f1,%f2,%f3}, [tex0,{_,%f5}];
  ret;
}
)ptx"));
  EXPECT_FALSE(owned_texture_module(R"ptx(
.version 9.3
.target sm_80
.global .texref tex0;
.entry kernel() {
  .reg .f32 %f<6>;
  tex.2d.v4.f32.f32 {%f0,%f1,%f2,%f3}, [tex0,{%f4,%f5}], {_,1};
  ret;
}
)ptx"));
}

/** Nested coordinate immediates keep source and converted bits coherent. */
TEST(TextureOwned, CoordinateImmediateMetadataRetainsLaneProvenance) {
  auto module = owned_texture_module(R"ptx(
.version 9.3
.target sm_80
.global .texref tex0;
.entry kernel() {
  .reg .s32 %r<10>;
  .reg .f32 %f<6>;
  tex.2d.v4.s32.s32 {%r0,%r1,%r2,%r3}, [tex0,{1,%r5}];
  tex.a2dms.v4.s32.s32 {%r0,%r1,%r2,%r3}, [tex0,{1,2,3,4}];
  tex.2d.v4.f32.f32 {%f0,%f1,%f2,%f3},
      [tex0,{0f3f000000,%f5}];
  tex.2d.v4.s32.s32 {%r0,%r1,%r2,%r3}, [tex0,{-1,%r5}];
  tex.2d.v4.s32.s32 {%r0,%r1,%r2,%r3},
      [tex0,{0x100000000,%r5}];
  ret;
}
)ptx");
  ASSERT_TRUE(module);
  ASSERT_TRUE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
  auto* pure = dynamic_cast<TexOmitted2dS32U32S32F32*>(
      module->functions.front().body[0].get());
  auto* mixed = dynamic_cast<TexOmittedA2dmsS32U32S32F32*>(
      module->functions.front().body[1].get());
  auto* floating = dynamic_cast<TexOmitted2dF32U32S32F32*>(
      module->functions.front().body[2].get());
  auto* negative = dynamic_cast<TexOmitted2dS32U32S32F32*>(
      module->functions.front().body[3].get());
  auto* high = dynamic_cast<TexOmitted2dS32U32S32F32*>(
      module->functions.front().body[4].get());
  ASSERT_NE(pure, nullptr);
  ASSERT_NE(mixed, nullptr);
  ASSERT_NE(floating, nullptr);
  ASSERT_NE(negative, nullptr);
  ASSERT_NE(high, nullptr);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 80},
      .instruction_range = module->functions.front().instruction_ranges[0],
  };
  auto& pure_lane =
      std::get<ResolvedImmediate>(pure->access.value.coordinates[0].value);
  const auto original_pure = pure_lane;
  pure_lane.bits = 2;
  auto invalid = pure->check(context);
  ASSERT_FALSE(invalid);
  EXPECT_TRUE(std::ranges::any_of(invalid.error(), [&](const auto& diagnostic) {
    return diagnostic.range == pure->access.value.coordinates[0].range;
  }));
  EXPECT_FALSE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
  pure_lane = original_pure;
  pure_lane.bits = uint64_t{1} << 32;
  EXPECT_FALSE(pure->check(context));
  pure_lane = original_pure;
  EXPECT_TRUE(pure->check(context));

  auto& negative_lane =
      std::get<ResolvedImmediate>(negative->access.value.coordinates[0].value);
  const auto original_negative = negative_lane;
  EXPECT_TRUE(negative_lane.is_negative);
  EXPECT_EQ(negative_lane.bits, UINT32_MAX);
  negative_lane.bits = 0;
  EXPECT_FALSE(negative->check(context));
  negative_lane = original_negative;
  EXPECT_TRUE(negative->check(context));

  auto& high_lane =
      std::get<ResolvedImmediate>(high->access.value.coordinates[0].value);
  const auto original_high = high_lane;
  EXPECT_EQ(high_lane.integer_source_bits, uint64_t{0x100000000});
  EXPECT_EQ(high_lane.bits, 0u);
  high_lane.bits = 1;
  EXPECT_FALSE(high->check(context));
  high_lane = original_high;
  EXPECT_TRUE(high->check(context));

  auto& mixed_lane =
      std::get<ResolvedImmediate>(mixed->access.value.coordinates[3].value);
  const auto original_mixed = mixed_lane;
  mixed_lane.integer_source_bits = 0;
  invalid = mixed->check(context);
  ASSERT_FALSE(invalid);
  EXPECT_TRUE(std::ranges::any_of(invalid.error(), [&](const auto& diagnostic) {
    return diagnostic.range == mixed->access.value.coordinates[3].range;
  }));
  mixed_lane = original_mixed;
  EXPECT_TRUE(mixed->check(context));

  auto& float_lane =
      std::get<ResolvedImmediate>(floating->access.value.coordinates[0].value);
  const auto original_float = float_lane;
  float_lane.integer_source_bits = 1;
  EXPECT_FALSE(floating->check(context));
  float_lane = original_float;
  float_lane.bits = uint64_t{1} << 32;
  EXPECT_FALSE(floating->check(context));
  float_lane = original_float;
  EXPECT_TRUE(floating->check(context));
  EXPECT_TRUE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
}

/** A deprecated source spelling retains texture identity and mov retrieval. */
TEST(TextureOwned, LegacyTextureDeclarationAndMovIdentity) {
  const auto module = owned_texture_module(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.tex .u64 legacy_tex;
.entry kernel() {
  .reg .u64 %rd;
  .reg .f32 %f<5>;
  mov.u64 %rd, legacy_tex;
  tex.1d.v4.f32.f32 {%f0,%f1,%f2,%f3}, [legacy_tex,{%f4}];
  ret;
}
)ptx");
  ASSERT_TRUE(module.has_value());
  ASSERT_EQ(module->storage_declarations.size(), 1u);
  EXPECT_TRUE(module->storage_declarations.front().legacy_texture);
  EXPECT_TRUE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
}

/** Malformed named-member input must diagnose without a lazy-lookahead throw. */
TEST(TextureOwned, MalformedNamedInitializerDiagnoses) {
  EXPECT_NO_THROW({
    const auto parsed = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_80
.global .samplerref samp0 = { filter_mode = nearest;
.entry kernel() { ret; }
)ptx");
    EXPECT_FALSE(parsed.diagnostics.empty());
  });
}

/** Mutable resource identity, lane roles, and source mode are rechecked. */
TEST(TextureOwned, ModuleMutationRejectsResourceAndCoordinateDrift) {
  auto module = owned_texture_module(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.global .texref tex0;
.entry kernel() {
  .reg .f32 %f<6>;
  tex.2d.v4.f32.f32 {%f0,%f1,%f2,%f3}, [tex0,{%f4,%f5}];
  ret;
}
)ptx");
  ASSERT_TRUE(module.has_value());
  auto* form = dynamic_cast<TexOmitted2dF32U32S32F32*>(
      module->functions.front().body.front().get());
  ASSERT_NE(form, nullptr);
  ASSERT_TRUE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));

  const auto original_role = form->access.value.coordinates.front().role;
  form->access.value.coordinates.front().role = TextureLaneRole::ArrayLayer;
  EXPECT_FALSE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
  form->access.value.coordinates.front().role = original_role;

  auto* symbol =
      std::get_if<ResolvedOpaqueSymbolRef>(&form->access.value.texture.value);
  ASSERT_NE(symbol, nullptr);
  const auto original_kind = symbol->kind;
  symbol->kind = base::OpaqueResourceKind::Sampler;
  EXPECT_FALSE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
  symbol->kind = original_kind;

  const auto original_mode = module->header.regions.back().texture_mode;
  module->header.regions.back().texture_mode = TextureMode::Independent;
  EXPECT_FALSE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
  module->header.regions.back().texture_mode = original_mode;
  EXPECT_TRUE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
}

/** Static member names and values remain checked after the syntax tree dies. */
TEST(TextureOwned, ModuleMutationRejectsOpaqueStaticDrift) {
  auto module = owned_texture_module(R"ptx(
.version 9.3
.target sm_80
.global .samplerref samp0 = { filter_mode = nearest };
.entry kernel() { ret; }
)ptx");
  ASSERT_TRUE(module.has_value());
  ASSERT_EQ(module->storage_declarations.size(), 1u);
  auto& declaration = module->storage_declarations.front();
  ASSERT_EQ(declaration.opaque_static_objects.size(), 1u);
  auto& member = declaration.opaque_static_objects.front().members.front();
  EXPECT_TRUE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));

  const auto original_field = member.field;
  member.field = static_cast<OpaqueStaticField>(255);
  EXPECT_FALSE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
  member.field = original_field;

  const auto original_value = member.value;
  member.value = uint64_t{9};
  EXPECT_FALSE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
  member.value = original_value;

  declaration.opaque_static_objects.front().members.push_back(member);
  EXPECT_FALSE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
}

/** Opaque declarations carry identities, not ordinary load/store addresses. */
TEST(TextureOwned, OpaqueResourceIsNotAddressableStorage) {
  EXPECT_FALSE(owned_texture_module(R"ptx(
.version 9.3
.target sm_80
.global .texref tex0;
.entry kernel() {
  .reg .b32 %r;
  ld.global.b32 %r, [tex0];
  ret;
}
)ptx")
                   .has_value());
}

/** Public form checking rejects malformed nested predicate and carrier fields. */
TEST(TextureOwned, PublicFormCheckRejectsNestedPayloadMutation) {
  auto module = owned_texture_module(R"ptx(
.version 9.3
.target sm_80
.global .texref tex0;
.entry kernel() {
  .reg .pred %p;
  .reg .f32 %f<6>;
  tex.2d.v4.f32.f32 {%f0,%f1,%f2,%f3}|%p, [tex0,{%f4,%f5}];
  ret;
}
)ptx");
  ASSERT_TRUE(module.has_value());
  auto* form = dynamic_cast<TexOmitted2dF32U32S32F32*>(
      module->functions.front().body.front().get());
  ASSERT_NE(form, nullptr);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 80},
      .instruction_range = module->functions.front().instruction_ranges.front(),
  };
  ASSERT_TRUE(form->check(context));
  ASSERT_TRUE(form->dst.value.residency.has_value());

  const auto saved_predicate = form->dst.value.residency;
  form->dst.value.residency.reset();
  EXPECT_FALSE(form->check(context));
  form->dst.value.residency = saved_predicate;
  form->dst.value.residency->negated = true;
  EXPECT_FALSE(form->check(context));
  form->dst.value.residency = saved_predicate;

  auto& result_lane = *form->dst.value.data.elements.front();
  result_lane.register_class = ResolvedRegisterClass::Predicate;
  EXPECT_FALSE(form->check(context));
  result_lane.register_class = ResolvedRegisterClass::General;

  form->dst.value.residency->register_ref.register_class =
      ResolvedRegisterClass::General;
  EXPECT_FALSE(form->check(context));
  form->dst.value.residency = saved_predicate;
  form->dst.value.residency->register_ref.declared_type = ScalarType::F32;
  EXPECT_FALSE(form->check(context));
  form->dst.value.residency = saved_predicate;

  auto* direct =
      std::get_if<ResolvedOpaqueSymbolRef>(&form->access.value.texture.value);
  ASSERT_NE(direct, nullptr);
  direct->kind = base::OpaqueResourceKind::Sampler;
  EXPECT_FALSE(form->check(context));
  direct->kind = base::OpaqueResourceKind::Texture;

  const auto saved_type = form->access.value.coordinates.front().value;
  auto* coordinate = std::get_if<ResolvedRegisterRef>(
      &form->access.value.coordinates.front().value);
  ASSERT_NE(coordinate, nullptr);
  coordinate->declared_type = ScalarType::U64;
  EXPECT_FALSE(form->check(context));
  form->access.value.coordinates.front().value = saved_type;
  EXPECT_TRUE(form->check(context));
}

/** Opaque array initializer paths keep object identity without byte offsets. */
TEST(TextureOwned, OpaqueArrayStaticMembersKeepObjectPaths) {
  const auto module = owned_texture_module(R"ptx(
.version 9.3
.target sm_80
.global .samplerref samples[2] = {
  { filter_mode = nearest },
  { filter_mode = linear, addr_mode_0 = wrap }
};
.entry kernel() { ret; }
)ptx");
  ASSERT_TRUE(module.has_value());
  ASSERT_EQ(module->storage_declarations.size(), 1u);
  const auto& storage = module->storage_declarations.front();
  ASSERT_EQ(storage.opaque_static_objects.size(), 2u);
  EXPECT_EQ(storage.opaque_static_objects[0].indices,
            (std::vector<uint64_t>{0}));
  EXPECT_EQ(storage.opaque_static_objects[1].indices,
            (std::vector<uint64_t>{1}));
  EXPECT_FALSE(storage.byte_extent.has_value());
  EXPECT_TRUE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
}

/** An external unsized opaque array retains its unknown first extent. */
TEST(TextureOwned, ExternalOpaqueArrayKeepsUnsizedFirstAxis) {
  const auto module = owned_texture_module(R"ptx(
.version 9.3
.target sm_80
.extern .global .texref textures[];
.entry kernel() { ret; }
)ptx");
  ASSERT_TRUE(module.has_value());
  ASSERT_EQ(module->storage_declarations.size(), 1u);
  ASSERT_EQ(module->storage_declarations.front().array_extents.size(), 1u);
  EXPECT_FALSE(
      module->storage_declarations.front().array_extents[0].has_value());
  EXPECT_TRUE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
}

/** Public checks validate indirect carrier width and the canonical gate. */
TEST(TextureOwned, PublicFormsRecheckIndirectResourceAndQuery) {
  auto module = owned_texture_module(R"ptx(
.version 9.3
.target sm_80
.entry kernel() {
  .reg .u64 %rd;
  .reg .f32 %f<6>;
  .reg .b32 %b;
  tex.2d.v4.f32.f32 {%f0,%f1,%f2,%f3}, [%rd,{%f4,%f5}];
  txq.width.b32 %b, [%rd];
  ret;
}
)ptx");
  ASSERT_TRUE(module.has_value());
  auto* tex = dynamic_cast<TexOmitted2dF32U32S32F32*>(
      module->functions.front().body[0].get());
  auto* query =
      dynamic_cast<TxqWidth*>(module->functions.front().body[1].get());
  ASSERT_NE(tex, nullptr);
  ASSERT_NE(query, nullptr);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 80},
      .instruction_range = module->functions.front().instruction_ranges.front(),
  };
  ASSERT_TRUE(tex->check(context));
  ASSERT_TRUE(query->check(context));
  auto* tex_handle =
      std::get_if<ResolvedRegisterRef>(&tex->access.value.texture.value);
  auto* query_handle =
      std::get_if<ResolvedRegisterRef>(&query->resource.value.resource.value);
  ASSERT_NE(tex_handle, nullptr);
  ASSERT_NE(query_handle, nullptr);
  tex_handle->declared_type = ScalarType::U32;
  EXPECT_FALSE(tex->check(context));
  tex_handle->declared_type = ScalarType::U64;
  query_handle->register_class = ResolvedRegisterClass::Predicate;
  EXPECT_FALSE(query->check(context));
  query_handle->register_class = ResolvedRegisterClass::General;
  query->resource.value.bracketed = false;
  EXPECT_FALSE(query->check(context));
  query->resource.value.bracketed = true;
  const checker::Context old_context{
      .target = {.ptx_version = {3, 0}, .sm_version = 20},
      .instruction_range = context.instruction_range,
  };
  EXPECT_FALSE(tex->check(old_context));
  EXPECT_FALSE(query->check(old_context));
}

/** A fixed texture-property query rejects a coherently mutated sampler kind. */
TEST(TextureOwned, PublicQueryRejectsCoherentWrongResourceKind) {
  auto module = owned_texture_module(R"ptx(
.version 9.3
.target sm_80
.global .texref tex0;
.entry kernel() {
  .reg .b32 %b;
  txq.width.b32 %b, [tex0];
  ret;
}
)ptx");
  ASSERT_TRUE(module.has_value());
  auto* query =
      dynamic_cast<TxqWidth*>(module->functions.front().body.front().get());
  ASSERT_NE(query, nullptr);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 80},
      .instruction_range = module->functions.front().instruction_ranges.front(),
  };
  ASSERT_TRUE(query->check(context));
  auto* direct = std::get_if<ResolvedOpaqueSymbolRef>(
      &query->resource.value.resource.value);
  ASSERT_NE(direct, nullptr);
  query->resource.value.resource.expected_kind =
      base::OpaqueResourceKind::Sampler;
  direct->kind = base::OpaqueResourceKind::Sampler;
  EXPECT_FALSE(query->check(context));
}

/** Source value vectors retain mixed lanes and reject known invalid offsets. */
TEST(TextureOwned, ValueVectorImmediatesAndOwnedMutation) {
  EXPECT_TRUE(valid_value_vector_immediate(
      ResolvedImmediate{.bits = UINT64_MAX,
                        .type = ScalarType::U64,
                        .is_negative = false,
                        .integer_source_bits = UINT64_MAX}));
  auto module = owned_texture_module(R"ptx(
.version 9.3
.target sm_80
.global .texref tex0;
.entry kernel() {
  .reg .f32 %f<9>;
  .reg .s32 %r;
  tex.2d.v4.f32.f32 {%f0,%f1,%f2,%f3}, [tex0,{%f4,%f5}], {-8,7};
  tex.2d.v4.f32.f32 {%f0,%f1,%f2,%f3}, [tex0,{%f4,%f5}], {0,%r};
  tex.grad.2d.v4.f32.f32 {%f0,%f1,%f2,%f3},
      [tex0,{%f4,%f5}], {0f00000000,%f6}, {%f7,0f00000000};
  tld4.r.2d.v4.f32.f32 {%f0,%f1,%f2,%f3}, [tex0,{%f4,%f5}], {0,1};
  ret;
}
)ptx");
  ASSERT_TRUE(module.has_value());
  ASSERT_TRUE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
  auto* form = dynamic_cast<TexOmitted2dF32U32S32F32*>(
      module->functions.front().body.front().get());
  ASSERT_NE(form, nullptr);
  ASSERT_TRUE(form->offset);
  ASSERT_EQ(form->offset->value.elements.size(), 2u);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 80},
      .instruction_range = module->functions.front().instruction_ranges.front(),
  };
  ASSERT_TRUE(form->check(context));
  auto& immediate =
      std::get<ResolvedImmediate>(form->offset->value.elements[0]);
  EXPECT_TRUE(immediate.is_negative);
  EXPECT_EQ(immediate.integer_source_bits, UINT64_MAX - 7);
  const auto original = immediate;
  immediate.integer_source_bits = uint64_t{0x100000000};
  immediate.bits = 0;
  immediate.is_negative = false;
  EXPECT_FALSE(form->check(context));
  immediate = original;
  EXPECT_TRUE(form->check(context));
  auto* mixed = dynamic_cast<TexOmitted2dF32U32S32F32*>(
      module->functions.front().body[1].get());
  ASSERT_NE(mixed, nullptr);
  ASSERT_TRUE(mixed->offset);
  ASSERT_EQ(mixed->offset->locs.size(), 2u);
  ASSERT_TRUE(std::holds_alternative<ResolvedRegisterRef>(
      mixed->offset->value.elements[1]));
  auto& register_lane =
      std::get<ResolvedRegisterRef>(mixed->offset->value.elements[1]);
  const checker::Context mixed_context{
      .target = {.ptx_version = {9, 3}, .sm_version = 80},
      .instruction_range = module->functions.front().instruction_ranges[1],
  };
  ASSERT_TRUE(mixed->check(mixed_context));
  const auto original_class = register_lane.register_class;
  register_lane.register_class = ResolvedRegisterClass::Predicate;
  EXPECT_FALSE(mixed->check(mixed_context));
  register_lane.register_class = original_class;
  const auto original_symbol = register_lane.symbol_id;
  register_lane.symbol_id = binding::SymbolId{UINT32_MAX};
  const auto invalid =
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid);
  EXPECT_TRUE(std::ranges::any_of(invalid.error(), [&](const auto& diagnostic) {
    return diagnostic.range == mixed->offset->locs[1];
  }));
  register_lane.symbol_id = original_symbol;
  EXPECT_TRUE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
  auto* gradient = dynamic_cast<TexGradient2dF32U32S32F32*>(
      module->functions.front().body[2].get());
  ASSERT_NE(gradient, nullptr);
  ASSERT_TRUE(std::holds_alternative<ResolvedImmediate>(
      gradient->ddx.value.elements.front()));
  const checker::Context gradient_context{
      .target = {.ptx_version = {9, 3}, .sm_version = 80},
      .instruction_range = module->functions.front().instruction_ranges[2],
  };
  ASSERT_TRUE(gradient->check(gradient_context));
  std::get<ResolvedImmediate>(gradient->ddx.value.elements.front()).type =
      ScalarType::S32;
  EXPECT_FALSE(gradient->check(gradient_context));
  form->offset->value.elements.push_back(ResolvedImmediate{
      .bits = 0, .type = ScalarType::S32, .integer_source_bits = 0});
  EXPECT_FALSE(form->check(context));
}

/** Each written immediate offset is checked against the signed source domain. */
TEST(TextureOwned, OutOfRangeTextureOffsetsRejectSource) {
  for (const auto value : {"-9", "8", "0x100000000"}) {
    const std::string source = std::string{R"ptx(
.version 9.3
.target sm_80
.global .texref tex0;
.entry kernel() {
  .reg .f32 %f<6>;
  tex.2d.v4.f32.f32 {%f0,%f1,%f2,%f3}, [tex0,{%f4,%f5}], {)ptx"} +
                               value + R"ptx(,0};
  ret;
}
)ptx";
    EXPECT_FALSE(owned_texture_module(source)) << value;
  }
}

/** A target directive's mode is shared by all source and owned regions. */
TEST(TextureOwned, ConflictingTargetRegionModesReject) {
  const auto conflicting = owned_texture_module(R"ptx(
.version 9.3
.target sm_80, texmode_unified
.entry first() { ret; }
.target sm_80, texmode_independent
.entry second() { ret; }
)ptx");
  ASSERT_TRUE(conflicting);
  EXPECT_FALSE(validateModule(*conflicting,
                              ModuleValidationPolicy::RequireCompleteContext));
  auto module = owned_texture_module(R"ptx(
.version 9.3
.target sm_80, texmode_independent
.entry first() { ret; }
.target sm_80
.entry second() { ret; }
)ptx");
  ASSERT_TRUE(module);
  for (const auto& region : module->header.regions)
    EXPECT_EQ(region.texture_mode, TextureMode::Independent);
  EXPECT_TRUE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
  module->header.regions.back().texture_mode = TextureMode::Unified;
  EXPECT_FALSE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
}

/** Owned declaration availability survives AST release and coherent retargeting. */
TEST(TextureOwned, OpaqueDeclarationsRetainVersionGate) {
  for (const auto* kind : {"texref", "samplerref", "surfref"}) {
    const std::string source = std::string{R"ptx(
.version 9.3
.target sm_80
.global .)ptx"} + kind + R"ptx( resource;
.entry kernel(.param .)ptx" + kind +
                               R"ptx( input) { ret; }
)ptx";
    auto module = owned_texture_module(source);
    ASSERT_TRUE(module) << kind;
    for (auto& region : module->header.regions)
      region.version = checker::PtxVersion{1, 4};
    for (auto& function : module->functions)
      function.source_version = checker::PtxVersion{1, 4};
    EXPECT_FALSE(
        validateModule(*module, ModuleValidationPolicy::RequireCompleteContext))
        << kind;
  }
  const auto legacy = owned_texture_module(R"ptx(
.version 1.4
.target sm_13
.tex .u32 tex0;
.entry kernel() { ret; }
)ptx");
  ASSERT_TRUE(legacy);
  EXPECT_TRUE(
      validateModule(*legacy, ModuleValidationPolicy::RequireCompleteContext));
}

/** A replacement source may change mode when the body is mode independent. */
TEST(TextureOwned, ReplacementAstUsesItsOwnModuleMode) {
  auto plain = owned_texture_module(R"ptx(
.version 9.3
.target sm_80, texmode_unified
.entry kernel() { ret; }
)ptx");
  ASSERT_TRUE(plain);
  auto independent = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_80, texmode_independent
.entry kernel() { ret; }
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(independent);
  EXPECT_TRUE(validateModule(*independent, *plain,
                             ModuleValidationPolicy::RequireCompleteContext));

  auto texture = owned_texture_module(R"ptx(
.version 9.3
.target sm_80, texmode_unified
.global .texref tex0;
.entry kernel() {
  .reg .f32 %f<6>;
  tex.2d.v4.f32.f32 {%f0,%f1,%f2,%f3}, [tex0,{%f4,%f5}];
  ret;
}
)ptx");
  ASSERT_TRUE(texture);
  auto replacement = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_80, texmode_independent
.global .texref tex0;
.entry kernel() {
  .reg .f32 %f<6>;
  tex.2d.v4.f32.f32 {%f0,%f1,%f2,%f3}, [tex0,{%f4,%f5}];
  ret;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(replacement);
  EXPECT_FALSE(validateModule(*replacement, *texture,
                              ModuleValidationPolicy::RequireCompleteContext));

  auto conflicting = test_helpers::parseModule(R"ptx(
.version 9.3
.target sm_80, texmode_unified
.entry kernel() { ret; }
.target sm_80, texmode_independent
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(conflicting);
  EXPECT_FALSE(validateModule(*conflicting, *plain,
                              ModuleValidationPolicy::RequireCompleteContext));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
