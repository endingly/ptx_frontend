#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_storage_declarations.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace {

namespace ir = ptx_frontend::resolved_ir;
using ptx_frontend::SourceRange;

/** Parse and resolve a complete source module, returning only owned IR data. */
std::optional<ir::ResolvedModule> parse_owned(std::string_view source,
                                              std::string_view label) {
  ptx_frontend::PtxSyntaxParser parser{source};
  auto parsed = parser.parseModule();
  if (!parsed || !parsed.diagnostics.empty()) {
    std::cerr << label << ": syntax parse failed\n";
    for (const auto& diagnostic : parsed.diagnostics)
      std::cerr << "  " << diagnostic.message << '\n';
    return std::nullopt;
  }

  auto resolved = ir::resolveModuleOnly(*parsed);
  if (!resolved) {
    std::cerr << label << ": module resolution failed\n";
    for (const auto& diagnostic : resolved.error())
      std::cerr << "  " << diagnostic.message << '\n';
    return std::nullopt;
  }
  return std::move(*resolved);
}

/** Report a failed consumer assertion without relying on a test framework. */
bool expect(bool condition, std::string_view description) {
  if (!condition)
    std::cerr << "consumer assertion failed: " << description << '\n';
  return condition;
}

/** Observe and copy public texture-family references during a synchronous visit. */
struct TextureCapture final : ir::detail::IReferenceObserver {
  std::optional<ir::ResolvedTextureAccess> access;
  std::optional<ir::ResolvedTextureQueryResource> query_resource;
  std::optional<ir::ResolvedTextureResult> result;
  std::optional<ir::ResolvedMovSource> move_source;
  std::vector<ir::ResolvedRegisterRef> registers;

  /** Copy one access payload while its owning instruction is alive. */
  void texture_access(const ir::ResolvedTextureAccess& value,
                      std::span<const SourceRange>,
                      ir::checker::AddressSymbolResolutionPolicy) override {
    access = value;
  }

  /** Copy one query resource while its owning instruction is alive. */
  void texture_query_resource(
      const ir::ResolvedTextureQueryResource& value,
      std::span<const SourceRange>,
      ir::checker::AddressSymbolResolutionPolicy) override {
    query_resource = value;
  }

  /** Copy one data and optional-residency result while it is owned. */
  void texture_result(const ir::ResolvedTextureResult& value,
                      std::span<const SourceRange>,
                      ir::checker::AddressSymbolResolutionPolicy) override {
    result = value;
  }

  /** Copy the opaque source identity selected by a `mov` instruction. */
  void mov_source(const ir::ResolvedMovSource& value,
                  std::span<const SourceRange>,
                  ir::checker::AddressSymbolResolutionPolicy) override {
    move_source = value;
  }

  /** Copy register uses so the `istypep` handle remains inspectable. */
  void reg(const ir::ResolvedRegisterRef& value, std::span<const SourceRange>,
           ir::checker::AddressSymbolResolutionPolicy) override {
    registers.push_back(value);
  }
};

/** Find one module-level declaration by its typed opaque resource identity. */
const ir::ResolvedStorageDeclaration* find_storage(
    const ir::ResolvedModule& module, ir::StorageOpaqueType kind) {
  for (const auto& declaration : module.storage_declarations) {
    const auto* actual =
        std::get_if<ir::StorageOpaqueType>(&declaration.element_type);
    if (actual && *actual == kind)
      return &declaration;
  }
  return nullptr;
}

/** Find one named field on the first opaque object without fabricating defaults. */
const ir::OpaqueStaticMember* find_static_member(
    const ir::ResolvedStorageDeclaration& declaration,
    ir::OpaqueStaticField field) {
  if (declaration.opaque_static_objects.empty())
    return nullptr;
  const auto& members = declaration.opaque_static_objects.front().members;
  const auto found = std::find_if(
      members.begin(), members.end(),
      [field](const auto& member) { return member.field == field; });
  return found == members.end() ? nullptr : &*found;
}

/** Return the entry function in a module with one kernel. */
const ir::ResolvedFunction* find_entry(const ir::ResolvedModule& module) {
  const auto found =
      std::find_if(module.functions.begin(), module.functions.end(),
                   [](const auto& function) { return function.is_entry; });
  return found == module.functions.end() ? nullptr : &*found;
}

/** Validate the public owned-module contract after parser and AST destruction. */
bool validate_owned(const ir::ResolvedModule& module, std::string_view label) {
  const auto checked = ir::validateModule(
      module, ir::ModuleValidationPolicy::RequireCompleteContext);
  if (!checked) {
    std::cerr << label << ": AST-free module validation failed\n";
    for (const auto& diagnostic : checked.error())
      std::cerr << "  " << diagnostic.message << '\n';
    return false;
  }
  return true;
}

/** Check the register carrier and semantic role of one owned coordinate lane. */
bool lane_is(const ir::ResolvedTextureLane& lane, ir::TextureLaneRole role,
             ir::ScalarType type) {
  if (lane.role != role)
    return false;
  const auto* reg = std::get_if<ir::ResolvedRegisterRef>(&lane.value);
  return reg && reg->declared_type && *reg->declared_type == type;
}

}  // namespace

/** Exercise installed texture APIs using only owned data and no GPU runtime. */
int main() {
  using OpaqueResourceKind = ptx_frontend::base::OpaqueResourceKind;
  using ir::OpaqueStaticEnum;
  using ir::OpaqueStaticField;
  using ir::ScalarType;
  using ir::TextureGeometry;
  using ir::TextureMode;
  using ir::TextureQuery;

  constexpr std::string_view unified_source = R"ptx(
.version 9.3
.target sm_80
.address_size 64
.global .texref tex0;
.global .samplerref samp0 = { filter_mode = nearest };
.global .surfref surf0;
.entry unified_kernel() {
  .reg .pred %p<4>;
  .reg .b32 %b<4>;
  .reg .u32 %r<4>;
  .reg .f32 %f<32>;
  .reg .u64 %rd;
  mov.u64 %rd, tex0;
  istypep.texref %p0, %rd;
  istypep.samplerref %p1, %rd;
  istypep.surfref %p2, %rd;
  txq.width.b32 %b0, [tex0];
  txq.filter_mode.b32 %b1, [tex0];
  tex.2d.v4.f32.f32 {%f0,%f1,%f2,%f3}, [tex0,{%f4,%f5}];
  tex.2d.v4.f32.f32 {%f6,%f7,%f8,%f9}, [tex0,{%f10,%f11,%f12,%f13}];
  tex.a2d.v4.f32.f32 {%f14,%f15,%f16,%f17}, [tex0,{%r0,%f18,%f19,%r1}];
  tld4.r.2d.v4.f32.f32 {%f20,%f21,%f22,%f23}, [tex0,{%f24,%f25}];
  ret;
}
)ptx";

  auto unified = parse_owned(unified_source, "unified fixture");
  if (!unified || !validate_owned(*unified, "unified fixture"))
    return 1;
  if (!expect(!unified->header.regions.empty() &&
                  unified->header.regions.front().texture_mode ==
                      TextureMode::Unified,
              "default source texturing mode is retained as Unified"))
    return 2;

  const auto* global_tex = find_storage(*unified, OpaqueResourceKind::Texture);
  const auto* global_sampler =
      find_storage(*unified, OpaqueResourceKind::Sampler);
  const auto* global_surface =
      find_storage(*unified, OpaqueResourceKind::Surface);
  if (!expect(global_tex && global_sampler && global_surface,
              "global texref, samplerref, and surfref declarations are typed"))
    return 3;
  const auto* filter =
      find_static_member(*global_sampler, OpaqueStaticField::FilterMode);
  if (!expect(global_sampler->initialization ==
                      ir::StorageInitializationKind::OpaqueStatic &&
                  filter &&
                  std::holds_alternative<OpaqueStaticEnum>(filter->value) &&
                  std::get<OpaqueStaticEnum>(filter->value) ==
                      OpaqueStaticEnum::Nearest,
              "global named sampler initializer stays a typed static field"))
    return 4;

  const auto* unified_entry = find_entry(*unified);
  if (!expect(unified_entry != nullptr,
              "unified module retains its entry function"))
    return 5;
  const ir::Instruction* legacy_tex_2d = nullptr;
  const ir::Instruction* standard_tex_2d = nullptr;
  const ir::Instruction* array_tex_2d = nullptr;
  const ir::Instruction* gather_2d = nullptr;
  const ir::Instruction* width_query = nullptr;
  const ir::Instruction* sampler_attribute_query = nullptr;
  std::vector<const ir::Instruction*> kind_tests;
  const ir::Instruction* move = nullptr;
  for (const auto& instruction : unified_entry->body) {
    if (instruction->opcode_name() == "mov")
      move = instruction.get();
    const auto* descriptor = instruction->texture_descriptor();
    if (!descriptor)
      continue;
    if (descriptor->tested_kind) {
      kind_tests.push_back(instruction.get());
    } else if (descriptor->query == TextureQuery::Width) {
      width_query = instruction.get();
    } else if (descriptor->query == TextureQuery::FilterMode) {
      sampler_attribute_query = instruction.get();
    } else if (descriptor->component) {
      gather_2d = instruction.get();
    } else if (descriptor->geometry == TextureGeometry::ArrayTwoD) {
      array_tex_2d = instruction.get();
    } else if (descriptor->geometry == TextureGeometry::TwoD) {
      TextureCapture captured;
      instruction->visit_references(captured);
      if (captured.access && captured.access->coordinates.size() == 4)
        legacy_tex_2d = instruction.get();
      else
        standard_tex_2d = instruction.get();
    }
  }
  if (!expect(move && kind_tests.size() == 3 && width_query &&
                  sampler_attribute_query && standard_tex_2d && legacy_tex_2d &&
                  array_tex_2d && gather_2d,
              "unified module contains typed tex, tld4, txq, and all istypep "
              "forms"))
    return 6;

  TextureCapture move_payload;
  move->visit_references(move_payload);
  const auto* moved_identity =
      move_payload.move_source
          ? std::get_if<ir::ResolvedOpaqueSymbolRef>(&*move_payload.move_source)
          : nullptr;
  if (!expect(moved_identity && moved_identity->spelling == "tex0" &&
                  moved_identity->kind == OpaqueResourceKind::Texture,
              "mov.u64 preserves the source texture symbol identity"))
    return 7;

  for (const auto* test : kind_tests) {
    const auto* descriptor = test->texture_descriptor();
    if (!expect(descriptor && descriptor->tested_kind,
                "istypep exposes its tested opaque kind"))
      return 8;
    TextureCapture payload;
    test->visit_references(payload);
    const auto source =
        std::find_if(payload.registers.begin(), payload.registers.end(),
                     [](const auto& reg) { return reg.spelling == "%rd"; });
    if (!expect(source != payload.registers.end() && source->declared_type &&
                    *source->declared_type == ScalarType::U64,
                "istypep retains an unknown runtime handle as a u64 register"))
      return 9;
  }

  const auto check_access = [&](const ir::Instruction* instruction,
                                TextureGeometry geometry,
                                std::size_t coordinate_count, bool gather) {
    if (!instruction)
      return false;
    const auto* descriptor = instruction->texture_descriptor();
    TextureCapture payload;
    instruction->visit_references(payload);
    const auto* direct = payload.access
                             ? std::get_if<ir::ResolvedOpaqueSymbolRef>(
                                   &payload.access->texture.value)
                             : nullptr;
    const auto selected = instruction->texture_selected_types();
    return descriptor && descriptor->geometry == geometry &&
           descriptor->result_arity == 4 &&
           descriptor->component.has_value() == gather &&
           selected.coordinate_type == ScalarType::F32 && payload.access &&
           payload.result && payload.result->data.elements.size() == 4 &&
           direct && direct->spelling == "tex0" &&
           direct->kind == OpaqueResourceKind::Texture &&
           payload.access->coordinates.size() == coordinate_count;
  };
  if (!expect(
          check_access(standard_tex_2d, TextureGeometry::TwoD, 2, false) &&
              check_access(legacy_tex_2d, TextureGeometry::TwoD, 4, false) &&
              check_access(array_tex_2d, TextureGeometry::ArrayTwoD, 4,
                           false) &&
              check_access(gather_2d, TextureGeometry::TwoD, 2, true),
          "typed tex/tld4 descriptors retain geometry, types, result arity, "
          "and direct resource"))
    return 10;

  TextureCapture legacy_payload;
  legacy_tex_2d->visit_references(legacy_payload);
  if (!expect(legacy_payload.access &&
                  legacy_payload.access->coordinates[0].role ==
                      ir::TextureLaneRole::Spatial &&
                  legacy_payload.access->coordinates[1].role ==
                      ir::TextureLaneRole::Spatial &&
                  legacy_payload.access->coordinates[2].role ==
                      ir::TextureLaneRole::Ignored &&
                  legacy_payload.access->coordinates[3].role ==
                      ir::TextureLaneRole::Ignored,
              "legacy v4 tex coordinates retain ignored padding lanes"))
    return 11;

  TextureCapture array_payload;
  array_tex_2d->visit_references(array_payload);
  if (!expect(array_payload.access &&
                  lane_is(array_payload.access->coordinates[0],
                          ir::TextureLaneRole::ArrayLayer, ScalarType::U32) &&
                  lane_is(array_payload.access->coordinates[1],
                          ir::TextureLaneRole::Spatial, ScalarType::F32) &&
                  lane_is(array_payload.access->coordinates[2],
                          ir::TextureLaneRole::Spatial, ScalarType::F32) &&
                  lane_is(array_payload.access->coordinates[3],
                          ir::TextureLaneRole::Ignored, ScalarType::U32),
              "a2d coordinate lanes retain layer/spatial/padding roles and "
              "carriers"))
    return 12;

  TextureCapture width_payload;
  width_query->visit_references(width_payload);
  const auto* width_symbol =
      width_payload.query_resource
          ? std::get_if<ir::ResolvedOpaqueSymbolRef>(
                &width_payload.query_resource->resource.value)
          : nullptr;
  if (!expect(width_query->texture_descriptor()->query == TextureQuery::Width &&
                  width_payload.query_resource && width_symbol &&
                  width_symbol->spelling == "tex0" &&
                  width_payload.query_resource->resource.expected_kind ==
                      OpaqueResourceKind::Texture,
              "txq width descriptor binds a direct texture resource"))
    return 13;
  TextureCapture filter_payload;
  sampler_attribute_query->visit_references(filter_payload);
  if (!expect(sampler_attribute_query->texture_descriptor()->query ==
                      TextureQuery::FilterMode &&
                  filter_payload.query_resource &&
                  filter_payload.query_resource->resource.expected_kind ==
                      OpaqueResourceKind::Texture,
              "unified sampler-attribute query uses a texref resource"))
    return 14;

  constexpr std::string_view independent_source = R"ptx(
.version 9.3
.target sm_80, texmode_independent
.address_size 64
.global .texref independent_tex;
.global .samplerref independent_samp = { force_unnormalized_coords = 1 };
.entry independent_kernel(
    .param .align 8 .texref input_tex,
    .param .samplerref input_samp) {
  .reg .b32 %b<4>;
  .reg .u32 %r<4>;
  .reg .f32 %f<24>;
  tex.2d.v4.f32.f32 {%f0,%f1,%f2,%f3},
      [input_tex,input_samp,{%f4,%f5}];
  tld4.g.a2d.v4.f32.f32 {%f6,%f7,%f8,%f9},
      [input_tex,input_samp,{%r0,%f10,%f11,%r1}];
  txq.force_unnormalized_coords.b32 %b0, [input_samp];
  ret;
}
)ptx";
  auto independent = parse_owned(independent_source, "independent fixture");
  if (!independent || !validate_owned(*independent, "independent fixture"))
    return 15;
  if (!expect(!independent->header.regions.empty() &&
                  independent->header.regions.back().texture_mode ==
                      TextureMode::Independent,
              "explicit target texturing mode is retained as Independent"))
    return 16;

  const auto* independent_entry = find_entry(*independent);
  if (!expect(
          independent_entry &&
              independent_entry->opaque_entry_parameters.size() == 2 &&
              independent_entry->opaque_entry_parameters[0].kind ==
                  OpaqueResourceKind::Texture &&
              independent_entry->opaque_entry_parameters[0]
                      .explicit_alignment == std::optional<std::uint64_t>{8} &&
              independent_entry->opaque_entry_parameters[1].kind ==
                  OpaqueResourceKind::Sampler &&
              !independent_entry->opaque_entry_parameters[0].is_array,
          "opaque entry inputs retain kinds, explicit alignment, and shape"))
    return 17;

  const ir::Instruction* independent_tex = nullptr;
  const ir::Instruction* independent_gather = nullptr;
  const ir::Instruction* independent_force_query = nullptr;
  for (const auto& instruction : independent_entry->body) {
    const auto* descriptor = instruction->texture_descriptor();
    if (!descriptor)
      continue;
    if (descriptor->query == TextureQuery::ForceUnnormalizedCoords)
      independent_force_query = instruction.get();
    else if (descriptor->component)
      independent_gather = instruction.get();
    else if (descriptor->geometry == TextureGeometry::TwoD)
      independent_tex = instruction.get();
  }
  if (!expect(independent_tex && independent_gather && independent_force_query,
              "independent module contains access, gather, and sampler query "
              "forms"))
    return 18;

  TextureCapture independent_access_payload;
  independent_tex->visit_references(independent_access_payload);
  const auto* entry_tex =
      independent_access_payload.access
          ? std::get_if<ir::ResolvedOpaqueSymbolRef>(
                &independent_access_payload.access->texture.value)
          : nullptr;
  const auto* entry_sampler =
      independent_access_payload.access &&
              independent_access_payload.access->sampler
          ? std::get_if<ir::ResolvedOpaqueSymbolRef>(
                &independent_access_payload.access->sampler->value)
          : nullptr;
  if (!expect(entry_tex && entry_tex->spelling == "input_tex" &&
                  entry_tex->entry_input && entry_sampler &&
                  entry_sampler->spelling == "input_samp" &&
                  entry_sampler->entry_input &&
                  independent_access_payload.access->sampler->expected_kind ==
                      OpaqueResourceKind::Sampler,
              "independent access binds both direct opaque entry identities"))
    return 19;

  TextureCapture independent_gather_payload;
  independent_gather->visit_references(independent_gather_payload);
  if (!expect(independent_gather->texture_descriptor()->geometry ==
                      TextureGeometry::ArrayTwoD &&
                  independent_gather->texture_descriptor()->component ==
                      ir::TextureComponent::Green &&
                  independent_gather_payload.access &&
                  independent_gather_payload.access->coordinates.size() == 4 &&
                  independent_gather_payload.access->sampler,
              "tld4 descriptor retains component, a2d geometry, and explicit "
              "sampler"))
    return 20;

  TextureCapture force_payload;
  independent_force_query->visit_references(force_payload);
  const auto* entry_sampler_query =
      force_payload.query_resource
          ? std::get_if<ir::ResolvedOpaqueSymbolRef>(
                &force_payload.query_resource->resource.value)
          : nullptr;
  if (!expect(
          independent_force_query->texture_descriptor()->query ==
                  TextureQuery::ForceUnnormalizedCoords &&
              force_payload.query_resource && entry_sampler_query &&
              entry_sampler_query->spelling == "input_samp" &&
              force_payload.query_resource->resource.expected_kind ==
                  OpaqueResourceKind::Sampler,
          "force-unnormalized query binds the independent sampler entry input"))
    return 21;

  const auto* independent_global_sampler =
      find_storage(*independent, OpaqueResourceKind::Sampler);
  if (!expect(independent_global_sampler != nullptr,
              "independent global sampler declaration remains typed"))
    return 22;
  const auto* force_member = find_static_member(
      *independent_global_sampler, OpaqueStaticField::ForceUnnormalizedCoords);
  if (!expect(force_member &&
                  std::holds_alternative<std::uint64_t>(force_member->value) &&
                  std::get<std::uint64_t>(force_member->value) == 1,
              "independent global sampler static integer field remains source "
              "data"))
    return 23;

  std::cout << "installed PTX texture consumer passed\n";
  return 0;
}
