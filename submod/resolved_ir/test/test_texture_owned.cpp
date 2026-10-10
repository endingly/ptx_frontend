#include <gtest/gtest.h>

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ptx_frontend/resolved_ir/model/data_movement/tex.gen.hpp>
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

}  // namespace
}  // namespace ptx_frontend::resolved_ir
