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
  ResolvedModule module{};
  module.functions.emplace_back();
  return parameter.symbol_id.value == 1 && literal.spelling == "0" &&
                 module.functions.size() == 1
             ? 0
             : 1;
}
