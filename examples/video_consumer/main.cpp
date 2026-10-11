#include <array>
#include <iostream>
#include <optional>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/model/video/vadd4.gen.hpp>
#include <ptx_frontend/resolved_ir/model/video/vmad.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/resolved_ir/ptx_video.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace {
namespace ir = ptx_frontend::resolved_ir;

/** Resolve source into an owned module whose AST is destroyed before return. */
std::optional<ir::ResolvedModule> owned_module(std::string_view source) {
  ptx_frontend::PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  auto module = ir::resolveModuleOnly(*ast);
  if (!module)
    return std::nullopt;
  return std::move(*module);
}

/** Return a visible failure for an installed public API contract violation. */
bool require(bool value, std::string_view label) {
  if (!value)
    std::cerr << label << '\n';
  return value;
}
}  // namespace

/** Exercise installed typed video forms, owned provenance and AST-free checks. */
int main() {
  constexpr std::string_view source = R"ptx(.version 9.3
.target sm_80
.address_size 64
.entry k() {
  .reg .b32 %r<4>;
  vmad.u32.s32.u32.sat.shr7 %r0, -%r1.b3, -%r2.h0, -1;
  vadd4.s32.u32.s32.add %r0.b31, %r1.b7777, %r2, %r3;
})ptx";
  auto module = owned_module(source);
  if (!require(module.has_value(), "resolve owned video module"))
    return 1;
  if (!require(ir::validateModule(*module).has_value(),
               "validate after AST release"))
    return 1;
  auto& mad = dynamic_cast<ir::VmadScalar&>(*module->functions[0].body[0]);
  auto& packed = dynamic_cast<ir::Vadd4Packed&>(*module->functions[0].body[1]);
  static_assert(ir::VmadScalar::video_operation == ir::VideoOperation::Mad);
  static_assert(ir::Vadd4Packed::video_lanes == ir::VideoLanes::Four);
  if (!require(
          mad.a.value.negated && mad.b.value.negated && !mad.c.value.negated,
          "register negation differs from negative literal"))
    return 1;
  if (!require(mad.a.value.minus_range.has_value() &&
                   mad.a.value.selector.has_value(),
               "own minus and selector locations"))
    return 1;
  const auto sign = ir::video_mad_interpretation(
      mad.atype.value, mad.btype.value, mad.a.value.negated,
      mad.b.value.negated, mad.c.value.negated);
  if (!require(
          sign && sign->product_signed && sign->c_signed && sign->result_signed,
          "derive sign independently of written unsigned dtype"))
    return 1;
  const auto& immediate =
      std::get<ir::ResolvedImmediate>(mad.c.value.value.value);
  if (!require(immediate.type == ir::ScalarType::B32 &&
                   immediate.integer_source_bits.has_value(),
               "preserve bit-carrier constant provenance"))
    return 1;
  const auto effective =
      ir::video_effective_selector(packed.b.value, ir::Vadd4Packed::video_lanes,
                                   ir::VideoOperandPosition::B);
  if (!require(!packed.b.value.selector && effective &&
                   std::get<ir::VideoByteSwizzle>(*effective).indices ==
                       std::array<uint8_t, 4>{7, 6, 5, 4},
               "typed default retains written omission"))
    return 1;
  mad.po.value = true;
  if (!require(!ir::validateModule(*module),
               "reject mutated po with written minus"))
    return 1;
  std::cout << "video consumer passed\n";
}
