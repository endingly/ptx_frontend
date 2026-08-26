#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string_view>

#include <ptx_frontend/cst/ptx_cst_parser.hpp>
#include <ptx_frontend/lexer/ptx_lexer.hpp>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
  if (size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    return 0;

  const char* bytes =
      size == 0 ? "" : reinterpret_cast<const char*>(data);
  const std::string_view source(bytes, size);
  ptx_frontend::PtxLexer lexer(source);
  for (std::size_t count = 0; count <= size; ++count) {
    const auto token = lexer.consume();
    if (token.kind == ptx_frontend::TokenKind::Eof ||
        token.kind == ptx_frontend::TokenKind::Error)
      break;
    if (count == size)
      std::abort();
  }

  ptx_frontend::PtxCstParser instruction_parser(source);
  const auto instruction = instruction_parser.parseInstruction();
  if (instruction.has_value() && instruction->sourceText() != source)
    std::abort();
  ptx_frontend::PtxCstParser module_parser(source);
  const auto module = module_parser.parseModule();
  if (module.has_value() && module->sourceText() != source)
    std::abort();
  return 0;
}
