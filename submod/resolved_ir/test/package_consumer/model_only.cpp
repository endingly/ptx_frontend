#include <ptx_frontend/resolved_ir/ptx_resolved_unified_id.hpp>

// Keep the shared value header independently includable by installed clients.
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_model.hpp>

#include <optional>

/** Compile the installed model surface without parser or checker headers. */
int main() {
  using namespace ptx_frontend::resolved_ir;
  ResolvedParameterDeclaration parameter{
      .symbol_id = {.value = 1},
      .scope_id = {.value = 0},
      .role = ParameterDeclarationRole::EntryInput,
  };
  ResolvedCallLiteral literal{.spelling = "0"};
  ResolvedUnifiedId unified_id{.upper = 1u, .lower = 2u};
  ResolvedModule module{};
  module.functions.emplace_back();
  return parameter.symbol_id.value == 1 && literal.spelling == "0" &&
                 unified_id.upper == 1 && unified_id.lower == 2 &&
                 module.functions.size() == 1
             ? 0
             : 1;
}
