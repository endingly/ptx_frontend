#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

/** Verify AST-free typed-call validation through the installed public API. */
int check_owned_module_handoff() {
  std::optional<ir::ResolvedModule> module;
  {
    const std::string source = R"ptx(
.version 8.0
.target sm_80
.address_size 64
.func callee(.reg .u32 input) { ret; }
.entry caller() {
  .reg .u32 %value;
  call callee, (7);
  call callee, (%value);
  ret;
}
)ptx";
    ptx_frontend::PtxSyntaxParser parser(source);
    auto ast = parser.parseModule();
    if (!ast || !ast.diagnostics.empty())
      return 30;
    auto resolved = ir::resolveModule(*ast);
    if (!resolved)
      return 31;
    module.emplace(std::move(*resolved));
  }
  if (module->header.regions.size() != 2 ||
      module->header.regions[1].address_size_bits != 64 ||
      module->functions.size() != 2 ||
      !ir::validateModule(*module))
    return 32;
  const auto& call = std::get<ir::Call>(module->functions[1].body.front());
  const auto& operands = std::get<ir::Call::Direct::TargetInputOperands>(
      std::get<ir::Call::Direct>(call.variant).operands);
  const auto* literal = std::get_if<ir::ResolvedCallLiteral>(
      &operands.arguments.value.values.front().value);
  const auto signature_space =
      module->functions.front().contract.signature.parameters.front().state_space;
  const auto& register_call = std::get<ir::Call>(module->functions[1].body.at(1));
  const auto& register_operands =
      std::get<ir::Call::Direct::TargetInputOperands>(
          std::get<ir::Call::Direct>(register_call.variant).operands);
  const auto& register_actual = std::get<ir::ResolvedCallParameterRef>(
      register_operands.arguments.value.values.front().value);
  if (literal == nullptr || !literal->value ||
      literal->kind != ptx_frontend::base::LiteralCategory::DecimalInteger ||
      signature_space !=
          ptx_frontend::call_argument_compatibility::CallArgumentStateSpace::Register ||
      register_actual.state_space !=
          ptx_frontend::base::DeclarationStateSpace::Register ||
      literal->value->type != ptx_frontend::base::ScalarType::U32 ||
      literal->value->bits != 7)
    return 33;
  auto mutated = *module;
  auto& changed = std::get<ir::Call::Direct::TargetInputOperands>(
      std::get<ir::Call::Direct>(
          std::get<ir::Call>(mutated.functions[1].body.front()).variant)
          .operands);
  auto& changed_literal = std::get<ir::ResolvedCallLiteral>(
      changed.arguments.value.values.front().value);
  changed_literal.value->type = ptx_frontend::base::ScalarType::U16;
  if (ir::validateModule(mutated))
    return 34;
  auto malformed_return = *module;
  auto& ret = std::get<ir::Ret>(malformed_return.functions[1].body.at(2));
  std::get<ir::Ret::Bare>(ret.variant).operand_layout.value = 99;
  const auto invalid_return = ir::validateModule(malformed_return);
  if (invalid_return || invalid_return.error().empty() ||
      invalid_return.error().front().kind !=
          ir::checker::CheckDiagnosticKind::InvalidOperandLayoutTag ||
      invalid_return.error().front().range !=
          malformed_return.functions[1].instruction_ranges.at(2))
    return 35;
  constexpr std::string_view defaulted_source = R"ptx(
.version 8.0
.target sm_80
.entry defaulted() { ret; }
)ptx";
  ptx_frontend::PtxSyntaxParser defaulted_parser(defaulted_source);
  auto defaulted_ast = defaulted_parser.parseModule();
  if (!defaulted_ast || !defaulted_ast.diagnostics.empty())
    return 36;
  auto defaulted = ir::resolveModule(*defaulted_ast);
  if (!defaulted || defaulted->header.regions.size() != 2 ||
      defaulted->header.regions[1].address_size_bits != 32 ||
      defaulted->header.regions[1].address_size_provenance !=
          ir::SourceConfigurationProvenance::Defaulted)
    return 37;
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
  if (mode == "handoff")
    return check_owned_module_handoff();
  return 2;
}
