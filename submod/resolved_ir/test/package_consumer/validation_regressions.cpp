#include <iostream>
#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ir = ptx_frontend::resolved_ir;

/** Verify copied public IR is revalidated using only the installed API. */
int check_modifier_domain() {
  constexpr std::string_view source = R"ptx(
.version 8.0
.target sm_80
.address_size 64
.entry k() {
  .reg .f32 %f0, %f1, %f2;
  mov.f32 %f1, 1.0;
  mov.f32 %f2, 2.0;
  add.rn.f32 %f0, %f1, %f2;
  ret;
}
)ptx";
  ptx_frontend::PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    return 10;
  auto module = ir::resolveModule(*ast);
  if (!module)
    return 11;
  const auto profile = ptx_frontend::base::find_target_profile("sm_80");
  if (!profile)
    return 12;
  const ir::checker::Context context{
      .target = {.ptx_version = {8, 0},
                 .sm_version = 80,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities},
      .instruction_range = module->functions.front().instruction_ranges.at(2),
  };
  const auto valid = std::get<ir::Add>(module->functions.front().body.at(2));
  if (!ir::checker::check(valid, context))
    return 13;
  auto changed = valid;
  std::get<ir::Add::FloatF32>(changed.variant).rounding.value =
      ptx_frontend::base::RoundingMode::Rzi;
  if (ir::checker::check(changed, context)) {
    std::cerr << "Checker accepted out-of-domain Add rounding Rzi.\n";
    return 14;
  }
  return 0;
}

/** Verify implicit/explicit natural alignment through the installed pipeline. */
int check_effective_alignment() {
  for (const bool explicit_first : {false, true}) {
    const std::string implicit = ".extern .global .u32 g;\n";
    const std::string explicit_value = ".extern .global .align 4 .u32 g;\n";
    const std::string source =
        ".version 8.0\n.target sm_80\n.address_size 64\n" +
        (explicit_first ? explicit_value + implicit : implicit + explicit_value) +
        ".entry k() { ret; }\n";
    ptx_frontend::PtxSyntaxParser parser(source);
    auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return 20;
    auto module = ir::resolveModule(*ast);
    if (!module) {
      for (const auto& diagnostic : module.error())
        std::cerr << diagnostic.message << '\n';
      return 21;
    }
    const auto& declarations = module->storage_declarations;
    if (declarations.size() != 2 || declarations[0].alignment != 4 ||
        declarations[1].alignment != 4 ||
        declarations[0].symbol_id != declarations[1].symbol_id ||
        declarations[0].explicit_alignment.has_value() != explicit_first ||
        declarations[1].explicit_alignment.has_value() == explicit_first)
      return 22;
  }
  return 0;
}

/** Run one separately reported installed-consumer regression. */
int main(int argc, char** argv) {
  if (argc != 2)
    return 1;
  const std::string_view mode = argv[1];
  if (mode == "modifier")
    return check_modifier_domain();
  if (mode == "alignment")
    return check_effective_alignment();
  return 2;
}
