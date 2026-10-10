#include <iostream>
#include <optional>
#include <string_view>

#include <ptx_frontend/resolved_ir/model/surface/suld.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace {
namespace ir = ptx_frontend::resolved_ir;

/** Return owned surface IR after the parser and syntax tree have been released. */
std::optional<ir::ResolvedModule> surface_module() {
  constexpr std::string_view source = R"ptx(
.version 9.3
.target sm_80, texmode_independent
.address_size 64
.global .surfref surface0;
.entry kernel() {
  .reg .b32 %b<4>;
  .reg .s32 %x;
  .reg .u64 %handle;
  mov.u64 %handle, surface0;
  suld.b.1d.b32.trap %b0, [surface0,%x];
  sust.b.1d.v2.b32.zero [%handle,{0}], {%b0,1};
  sured.p.min.1d.b64.clamp [surface0,{%x}], %handle;
  suq.memory_layout.b32 %b1, [surface0];
  ret;
}
)ptx";
  ptx_frontend::PtxSyntaxParser parser{source};
  auto parsed = parser.parseModule();
  if (!parsed || !parsed.diagnostics.empty())
    return std::nullopt;
  auto resolved = ir::resolveModuleOnly(*parsed);
  if (!resolved)
    return std::nullopt;
  return std::move(*resolved);
}
}  // namespace

/** Exercise installed exact-form headers, source identity, and AST-free checks. */
int main() {
  auto module = surface_module();
  if (!module ||
      !ir::validateModule(*module,
                          ir::ModuleValidationPolicy::RequireCompleteContext)) {
    std::cerr << "surface consumer: owned module validation failed\n";
    return 1;
  }
  auto* load = dynamic_cast<ir::SuldBData1dV1TrapB32B64*>(
      module->functions.front().body[1].get());
  if (!load ||
      load->surface_descriptor()->addressing !=
          ir::SurfaceAddressingMode::Byte ||
      load->surface_selected_types().data_type != ir::ScalarType::B32 ||
      load->access.value.surface.expected_kind !=
          ptx_frontend::base::OpaqueResourceKind::Surface ||
      load->access.value.coordinates.front().role !=
          ir::SurfaceLaneRole::Spatial) {
    std::cerr << "surface consumer: public typed payload mismatch\n";
    return 1;
  }
  load->access.value.coordinates.front().role = ir::SurfaceLaneRole::ArrayLayer;
  if (ir::validateModule(*module,
                         ir::ModuleValidationPolicy::RequireCompleteContext)) {
    std::cerr << "surface consumer: mutable lane role escaped rechecking\n";
    return 1;
  }
  std::cout << "surface consumer passed\n";
  return 0;
}
