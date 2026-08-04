#include "ptx_ir/ptx_resolved_ir.hpp"
#include <vector>

namespace ptx_frontend::resolved_ir {

std::expected<Add, ResolveDiagnostic> Add::resolve(
    const syntax_ast::AstInstruction& ast) {}

std::expected<Add::VariantType, ResolveDiagnostic> Add::selectVariant(
    const syntax_ast::AstInstruction& ast) {

  std::vector<std::string> modifiers = {
      ".sat", ".u16",  ".s16",  ".u64", ".s64", "u32",
      "s32",  "u16x2", "s16x2", "u8x4", "s8x4",
  };

  
}

};  // namespace ptx_frontend::resolved_ir