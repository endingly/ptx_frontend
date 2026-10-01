#include <cstdint>
#include <iostream>
#include <optional>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace {

/** Report one installed API failure without depending on assertions. */
bool require(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << "TCGEN descriptor consumer: " << message << '\n';
  return condition;
}

}  // namespace

/** Exercise borrowed source identity and separate caller-supplied known bits. */
int main() {
  namespace ir = ptx_frontend::resolved_ir;
  constexpr std::string_view source = R"ptx(
.version 9.0
.target sm_110a
.address_size 64
.visible .entry kernel() {
  .reg .b32 %taddr;
  .reg .b64 %descriptor;
  .shared .align 8 .b64 barrier;
  tcgen05.cp.cta_group::1.128x256b [%taddr], %descriptor;
  tcgen05.commit.cta_group::1.mbarrier::arrive::one.b64 [barrier];
  ret;
}
)ptx";
  std::optional<ir::ResolvedModule> owned;
  {
    ptx_frontend::PtxSyntaxParser parser{source};
    auto ast = parser.parseModule();
    if (!require(ast.has_value() && ast.diagnostics.empty(), "source parses"))
      return 1;
    auto resolved = ir::resolveAndValidateModule(*ast);
    if (!require(resolved.has_value(), "source resolves"))
      return 1;
    owned.emplace(std::move(*resolved));
  }
  const auto* record =
      owned->functions.front().body.front().get_if<ir::Tcgen05>();
  if (!require(record != nullptr, "owned copy survives AST release"))
    return 1;
  const auto* copy = std::get_if<ir::Tcgen05::Cp>(&record->variant);
  if (!require(copy && copy->descriptor_view() &&
                   copy->descriptor_view()->source == &copy->s_desc.value,
               "opaque source role borrows the owned record"))
    return 1;

  // These words are supplied by the consumer. They are not inferred from or
  // authenticated against the register above.
  const ir::TcgenSharedWord shared{(1ULL << 46) | (2ULL << 61)};
  ir::TcgenSharedContext context;
  context.major = ir::TcgenMajor::K;
  context.bytes.repeating_pattern_start = 1024;
  if (!require(ir::validate_tcgen_shared_defined_fields(shared, context)
                   .defined_fields_ok(),
               "known shared word's defined fields"))
    return 1;
  const auto rows = ir::tcgen_relative_layout_rows();
  if (!require(rows.size() == 8 &&
                   rows.data() == ir::tcgen_relative_layout_rows().data(),
               "static layout span lifetime"))
    return 1;
  const ir::TcgenInstructionWord instruction{0x910, ir::TcgenMmaKind::Tf32};
  if (!require(ir::validate_tcgen_instruction_defined_fields(instruction)
                   .defined_fields_ok(),
               "known instruction word defined fields"))
    return 1;
  const ir::TcgenZeroColumnWord zero{uint64_t{3} << 62};
  const auto report = ir::validate_tcgen_zero_defined_fields(zero, {32, 256});
  if (!require(report.defined_fields_ok() &&
                   report.unclassified_bits == (uint64_t{3} << 62) &&
                   ir::tcgen_zero_partition(32, 256)->active_masks == 4,
               "zero-column defined fields and unclassified high bits"))
    return 1;
  return require(ir::validateModule(*owned).has_value(),
                 "owned source still validates after AST release")
             ? 0
             : 1;
}
