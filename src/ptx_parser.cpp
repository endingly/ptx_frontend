#include "ptx_parser.hpp"

#include <utility>

#include "ptx_parser_core.hpp"
#include "ptx_parser_registry.gen.hpp"

namespace ptx_frontend {

class PtxParser::Impl {
 public:
  explicit Impl(std::string_view source) : parser(source) {}

  ParserCore parser;
};

PtxParser::PtxParser(std::string_view source)
    : impl_(std::make_unique<Impl>(source)) {}

PtxParser::~PtxParser() = default;

PtxParser::PtxParser(PtxParser&&) noexcept = default;

PtxParser& PtxParser::operator=(PtxParser&&) noexcept = default;

std::expected<ParsedInstruction, ParseDiagnostic>
PtxParser::parseInstruction() {
  if (!impl_) {
    return std::unexpected(ParseDiagnostic{
        .range = {},
        .message = "cannot use a moved-from PTX parser",
    });
  }

  try {
    return generated::parseInstruction(impl_->parser);
  } catch (const ParseError& error) {
    return std::unexpected(ParseDiagnostic{
        .range = error.range(),
        .message = error.what(),
    });
  }
}

std::expected<ParsedInstruction, ParseDiagnostic> parseInstruction(
    std::string_view source) {
  PtxParser parser(source);
  return parser.parseInstruction();
}

}  // namespace ptx_frontend
