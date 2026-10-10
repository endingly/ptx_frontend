#include <gtest/gtest.h>

#include <array>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <ptx_frontend/resolved_ir/model/surface/suld.gen.hpp>
#include <ptx_frontend/resolved_ir/model/surface/suq.gen.hpp>
#include <ptx_frontend/resolved_ir/model/surface/sured.gen.hpp>
#include <ptx_frontend/resolved_ir/model/surface/sust.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve a complete source fixture, releasing syntax before the caller checks. */
std::optional<ResolvedModule> owned_surface_module(std::string_view source) {
  auto parsed = test_helpers::parseModule(source);
  if (!parsed || !parsed.diagnostics.empty()) {
    for (const auto& diagnostic : parsed.diagnostics)
      std::cerr << "surface parse: " << diagnostic.message << '\n';
    return std::nullopt;
  }
  auto resolved = resolveModuleOnly(*parsed);
  if (!resolved) {
    for (const auto& diagnostic : resolved.error())
      std::cerr << "surface resolve: " << diagnostic.message << '\n';
    return std::nullopt;
  }
  return std::move(*resolved);
}

/** Complete source declarations shared by compact surface behavior fixtures. */
std::string surface_source(std::string_view body, std::string_view mode = "") {
  return ".version 9.3\n.target sm_80" + std::string(mode) + R"ptx(
.address_size 64
.global .surfref surf0;
.global .surfref surf1;
.global .texref tex0;
.entry kernel(.param .surfref input_surface) {
  .reg .b8 %byte<4>;
  .reg .b16 %half<4>;
  .reg .b32 %b<4>;
  .reg .b64 %d<4>;
  .reg .s32 %x, %y, %z;
  .reg .u32 %layer;
  .reg .u64 %handle;
  .reg .f32 %f;
  .reg .b32 %pad;
)ptx" + std::string(body) +
         "\nret;\n}\n";
}

/** Borrow one access or query for precise post-resolution mutation checks. */
struct SurfaceCapture final : detail::IReferenceObserver {
  /** Borrowed references stay valid while the owning instruction is alive. */
  const ResolvedSurfaceAccess* access = nullptr;
  const ResolvedSurfaceQueryResource* query = nullptr;
  /** Borrowed ordinary source vector, used to test widened lane rechecking. */
  const ResolvedValueVector* source_vector = nullptr;
  /** Observe ordinary mixed register/immediate surface source lanes. */
  void value_vector(const ResolvedValueVector& value,
                    std::span<const SourceRange>,
                    checker::AddressSymbolResolutionPolicy) override {
    source_vector = &value;
  }
  /** Observe the independently typed surface access payload. */
  void surface_access(const ResolvedSurfaceAccess& value,
                      std::span<const SourceRange>,
                      checker::AddressSymbolResolutionPolicy) override {
    access = &value;
  }
  /** Observe a surface query without assuming texture-mode policy. */
  void surface_query_resource(const ResolvedSurfaceQueryResource& value,
                              std::span<const SourceRange>,
                              checker::AddressSymbolResolutionPolicy) override {
    query = &value;
  }
};

/** All 1,864 legal written mnemonic combinations resolve and check without AST. */
TEST(SurfaceOwned, CompleteSyntaxMatrix) {
  std::string body;
  size_t count = 0;
  const std::array geometries{"1d", "2d", "3d", "a1d", "a2d"};
  const std::array coordinates{"%x", "{%x,%y}", "{%x,%y,%z,%b3}", "{%layer,%x}",
                               "{%layer,%x,%y,%b3}"};
  const std::array boundaries{"trap", "clamp", "zero"};
  for (const std::string_view opcode : {"suld", "sust"}) {
    for (size_t geometry = 0; geometry < geometries.size(); ++geometry) {
      for (const int arity : {1, 2, 4}) {
        for (const std::string_view type : {"b8", "b16", "b32", "b64"}) {
          if (arity == 4 && type == "b64")
            continue;
          const std::string prefix = type == "b8"    ? "%byte"
                                     : type == "b16" ? "%half"
                                     : type == "b32" ? "%b"
                                                     : "%d";
          std::string data = prefix + "0";
          if (arity != 1) {
            data = "{";
            for (int lane = 0; lane < arity; ++lane)
              data += (lane ? "," : "") + prefix + std::to_string(lane);
            data += "}";
          }
          const std::array<std::string_view, 5> load_caches{"", ".ca", ".cg",
                                                            ".cs", ".cv"};
          const std::array<std::string_view, 5> store_caches{"", ".wb", ".cg",
                                                             ".cs", ".wt"};
          for (const auto cache :
               opcode == "suld" ? load_caches : store_caches) {
            for (const auto boundary : boundaries) {
              const std::string access =
                  "[surf0," + std::string(coordinates[geometry]) + "]";
              body += std::string(opcode) + ".b." + geometries[geometry] +
                      std::string(cache) +
                      (arity == 1 ? "" : ".v" + std::to_string(arity)) + "." +
                      std::string(type) + "." + boundary + " " +
                      (opcode == "suld" ? data + "," + access
                                        : access + "," + data) +
                      ";\n";
              ++count;
            }
          }
        }
      }
    }
  }
  for (size_t geometry = 0; geometry < 3; ++geometry) {
    const std::string access =
        "[surf0," + std::string(coordinates[geometry]) + "]";
    for (const int arity : {1, 2, 4}) {
      const std::string data = arity == 1   ? "%b0"
                               : arity == 2 ? "{%b0,%b1}"
                                            : "{%b0,%b1,%b2,%b3}";
      for (const auto boundary : boundaries) {
        body += "sust.p." + std::string(geometries[geometry]) +
                (arity == 1 ? "" : ".v" + std::to_string(arity)) + ".b32." +
                boundary + " " + access + "," + data + ";\n";
        ++count;
      }
    }
    for (const std::string_view mode : {"b", "p"}) {
      for (const std::string_view operation :
           {"add", "min", "max", "and", "or"}) {
        for (const std::string_view type :
             {"u32", "s32", "u64", "s64", "b32", "b64"}) {
          const bool minmax = operation == "min" || operation == "max";
          const bool legal =
              mode == "p" ? (type == "b32" || (minmax && type == "b64"))
              : minmax    ? (type == "u32" || type == "s32" || type == "u64" ||
                          type == "s64")
              : operation == "add"
                  ? (type == "u32" || type == "s32" || type == "u64")
                  : type == "b32";
          if (!legal)
            continue;
          for (const auto boundary : boundaries) {
            body += "sured." + std::string(mode) + "." +
                    std::string(operation) + "." + geometries[geometry] + "." +
                    std::string(type) + "." + boundary + " " + access + "," +
                    (type.ends_with("64") ? "%d0" : "%b0") + ";\n";
            ++count;
          }
        }
      }
    }
  }
  for (const std::string_view query :
       {"width", "height", "depth", "channel_data_type", "channel_order",
        "array_size", "memory_layout"}) {
    body += "suq." + std::string(query) + ".b32 %b0,[surf0];\n";
    ++count;
  }
  ASSERT_EQ(count, 1864u);
  auto module = owned_surface_module(surface_source(body));
  ASSERT_TRUE(module);
  ASSERT_EQ(module->functions.front().body.size(), count + 1);
  const auto checked =
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext);
  if (!checked)
    for (const auto& diagnostic : checked.error())
      std::cerr << diagnostic.message << '\n';
  EXPECT_TRUE(checked);
}

/** Scalar/singleton coordinates, mixed immediate data, and both resource modes work. */
TEST(SurfaceOwned, SourceValuesAndIndependentMode) {
  for (const std::string_view mode : {"", ", texmode_independent"}) {
    auto module = owned_surface_module(surface_source(R"ptx(
mov.u64 %handle, surf0;
suld.b.1d.b8.trap %byte0, [surf0,%x];
suld.b.1d.b16.trap %b0, [input_surface,{%x}];
suld.b.a2d.v2.b8.zero {%b0,%b1}, [surf0,{%layer,%x,%y,%b3}];
sust.b.1d.b8.trap [surf0,{0}], 1;
sust.b.1d.v2.b16.trap [surf0,%x], {%half0,2};
sust.b.1d.v2.b8.trap [surf0,%x], {%b0,1};
sust.b.1d.v2.b16.trap [surf0,%x], {%b0,%b1};
sust.p.3d.v4.b32.clamp [surf0,{%x,%y,%z,%b3}], {%b0,1,%b2,3};
sured.b.add.1d.u32.trap [surf0,{%x}], 1;
suq.width.b32 %b0, [%handle];
suld.b.1d.b32.trap %b0, [%handle,0];
)ptx",
                                                      mode));
    ASSERT_TRUE(module);
    EXPECT_TRUE(validateModule(*module,
                               ModuleValidationPolicy::RequireCompleteContext));
  }
}

/** Wrong resource declarations, coordinate tuples, and data carriers are rejected. */
TEST(SurfaceOwned, RejectsWrongBindingsAndShapes) {
  for (const std::string_view body :
       {"suld.b.1d.b32.trap %b0,[tex0,%x];",
        "suld.b.1d.b32.trap %b0,[surf0,{%x,%y}];",
        "suld.b.2d.b32.trap %b0,[surf0,%x];",
        "suld.b.1d.b32.trap %b0,[surf0,%f];",
        "suld.b.1d.b32.trap %half0,[surf0,%x];",
        "suld.b.1d.b32.trap %d0,[surf0,%x];",
        "sust.b.1d.b32.trap [surf0,%x],%d0;",
        "sust.b.1d.v2.b16.trap [surf0,%x],{%byte0,%byte1};",
        "sured.b.add.1d.u32.trap [surf0,%x],%f;",
        "sust.b.1d.v2.b32.trap [surf0,%x],{%half0,%half1};",
        "suld.b.1d.v4.b64.trap {%d0,%d1,%d2,%d3},[surf0,%x];",
        "sust.b.1d.v4.b64.trap [surf0,%x],{%d0,%d1,%d2,%d3};",
        "sured.p.min.1d.u32.trap [surf0,%x],%b0;", "suq.width.b32 %b0,[tex0];",
        "suq.width.b32 %b0,[surf0+4];"}) {
    SCOPED_TRACE(body);
    auto module = owned_surface_module(surface_source(body));
    EXPECT_TRUE(!module ||
                !validateModule(
                    *module, ModuleValidationPolicy::RequireCompleteContext));
  }
}

/** Mutable lanes, padding, brackets, resource kinds, and references are rechecked. */
TEST(SurfaceOwned, MutationRechecksNestedPayload) {
  auto module = owned_surface_module(surface_source(
      "suld.b.a2d.b32.trap %b0,[surf0,{%layer,%x,%y,%b3}];\nsuq.width.b32 "
      "%b1,[surf0];"));
  ASSERT_TRUE(module);
  auto& instruction = *module->functions.front().body.front();
  SurfaceCapture capture;
  instruction.visit_references(capture);
  ASSERT_NE(capture.access, nullptr);
  auto& access = const_cast<ResolvedSurfaceAccess&>(*capture.access);
  const auto original = access;
  ASSERT_EQ(access.coordinates.size(), 4u);
  EXPECT_EQ(access.coordinates[0].role, SurfaceLaneRole::ArrayLayer);
  EXPECT_EQ(access.coordinates[3].role, SurfaceLaneRole::Ignored);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 80},
      .instruction_range =
          module->functions.front().instruction_ranges.front()};
  access.coordinates[0].role = SurfaceLaneRole::Spatial;
  EXPECT_FALSE(instruction.check(context));
  access = original;
  access.coordinates.pop_back();
  EXPECT_FALSE(instruction.check(context));
  access = original;
  access.bracketed = false;
  EXPECT_FALSE(instruction.check(context));
  access = original;
  access.surface.expected_kind = base::OpaqueResourceKind::Texture;
  EXPECT_FALSE(instruction.check(context));
  access = original;
  std::get<ResolvedRegisterRef>(access.coordinates[3].value).declared_type =
      ScalarType::F32;
  EXPECT_FALSE(instruction.check(context));
  access = original;
  std::get<ResolvedRegisterRef>(access.coordinates[1].value).symbol_id.reset();
  EXPECT_FALSE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
  access = original;
  auto& query_instruction = *module->functions.front().body[1];
  SurfaceCapture query_capture;
  query_instruction.visit_references(query_capture);
  ASSERT_NE(query_capture.query, nullptr);
  auto& query = const_cast<ResolvedSurfaceQueryResource&>(*query_capture.query);
  query.bracketed = false;
  EXPECT_FALSE(query_instruction.check(context));
  EXPECT_FALSE(
      validateModule(*module, ModuleValidationPolicy::RequireCompleteContext));
}

/** Widened source vector carriers are checked again after owned-IR mutation. */
TEST(SurfaceOwned, WidenedSourceVectorMutation) {
  auto module = owned_surface_module(
      surface_source("sust.b.1d.v2.b16.trap [surf0,%x],{%b0,1};"));
  ASSERT_TRUE(module);
  auto& instruction = *module->functions.front().body.front();
  SurfaceCapture capture;
  instruction.visit_references(capture);
  ASSERT_NE(capture.source_vector, nullptr);
  auto& values = const_cast<ResolvedValueVector&>(*capture.source_vector);
  const auto original = values;
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 80},
      .instruction_range =
          module->functions.front().instruction_ranges.front()};
  EXPECT_TRUE(instruction.check(context));
  std::get<ResolvedRegisterRef>(values.elements.front()).declared_type =
      ScalarType::B8;
  EXPECT_FALSE(instruction.check(context));
  values = original;
  std::get<ResolvedImmediate>(values.elements.back()).type = ScalarType::B32;
  EXPECT_FALSE(instruction.check(context));
  values = original;
  EXPECT_TRUE(instruction.check(context));
}

/** Each reference-bearing surface position is rebound against replacement source. */
TEST(SurfaceOwned, ReplacementSourceRechecksBindings) {
  const std::array replacements{
      std::tuple{"suld.b.1d.b32.trap %b0,[surf0,%x];", ".surfref surf0",
                 ".texref surf0"},
      std::tuple{"suq.width.b32 %b0,[surf0];", ".surfref surf0",
                 ".texref surf0"},
      std::tuple{"suld.b.1d.b32.trap %b0,[%handle,%x];", ".u64 %handle",
                 ".u32 %handle"},
      std::tuple{"suq.width.b32 %b0,[%handle];", ".u64 %handle",
                 ".u32 %handle"},
      std::tuple{"suld.b.1d.b32.trap %b0,[surf0,%x];", ".s32 %x, %y, %z",
                 ".s64 %x, %y, %z"},
      std::tuple{"suld.b.3d.b32.trap %b0,[surf0,{%x,%y,%z,%pad}];", ".b32 %pad",
                 ".b64 %pad"}};
  for (const auto& [body, old_declaration, new_declaration] : replacements) {
    SCOPED_TRACE(body);
    const auto source = surface_source(body);
    auto module = owned_surface_module(source);
    ASSERT_TRUE(module);
    auto same_source = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(same_source);
    EXPECT_TRUE(validateModule(*same_source, *module,
                               ModuleValidationPolicy::RequireCompleteContext));
    auto changed_source = source;
    const auto position = changed_source.find(old_declaration);
    ASSERT_NE(position, std::string::npos);
    changed_source.replace(position, std::string_view(old_declaration).size(),
                           new_declaration);
    auto replacement = test_helpers::parseModule(changed_source);
    ASSERT_MODULE_PARSE_SUCCEEDS(replacement);
    EXPECT_FALSE(validateModule(
        *replacement, *module, ModuleValidationPolicy::RequireCompleteContext));
  }
}

/** Complete modules check the exact PTX and SM floors for 64-bit min/max. */
TEST(SurfaceOwned, MinimumTargetModules) {
  std::string body;
  const std::array geometries{"1d", "2d", "3d"};
  const std::array coordinates{"%x", "{%x,%y}", "{%x,%y,%z,%pad}"};
  for (const std::string_view operation : {"min", "max"}) {
    for (size_t geometry = 0; geometry < geometries.size(); ++geometry) {
      for (const std::string_view boundary : {"trap", "clamp", "zero"}) {
        for (const auto& [mode, type] :
             std::array{std::pair{"b", "u64"}, std::pair{"b", "s64"},
                        std::pair{"p", "b64"}}) {
          body += "sured." + std::string(mode) + "." + std::string(operation) +
                  "." + geometries[geometry] + "." + type + "." +
                  std::string(boundary) + " [surf0," + coordinates[geometry] +
                  "],%d0;\n";
        }
      }
    }
  }
  for (const auto& [version, target, valid] : std::array{
           std::tuple{"8.1", "sm_50", true}, std::tuple{"8.0", "sm_50", false},
           std::tuple{"8.1", "sm_30", false}}) {
    auto source = surface_source(body);
    source.replace(source.find("9.3"), 3, version);
    source.replace(source.find("sm_80"), 5, target);
    auto module = owned_surface_module(source);
    ASSERT_TRUE(module) << version << " " << target;
    EXPECT_EQ(static_cast<bool>(validateModule(
                  *module, ModuleValidationPolicy::RequireCompleteContext)),
              valid)
        << version << " " << target;
  }
}

/** Indirect handles and 64-bit min/max retain independent feature floors. */
TEST(SurfaceOwned, ExactFeatureGates) {
  for (const auto& [body, ptx, sm] :
       std::array{std::tuple{"suld.b.1d.b32.trap %b0,[%handle,%x];",
                             checker::PtxVersion{3, 0}, 20u},
                  std::tuple{"sured.b.min.1d.u64.trap [surf0,%x],%d0;",
                             checker::PtxVersion{8, 0}, 80u},
                  std::tuple{"sured.p.max.1d.b64.trap [surf0,%x],%d0;",
                             checker::PtxVersion{9, 3}, 35u}}) {
    auto module = owned_surface_module(surface_source(body));
    ASSERT_TRUE(module);
    const auto& instruction = *module->functions.front().body.front();
    const checker::Context context{
        .target = {.ptx_version = ptx, .sm_version = sm},
        .instruction_range =
            module->functions.front().instruction_ranges.front()};
    EXPECT_FALSE(instruction.check(context));
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
