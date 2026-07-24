#include "ptx_parser.hpp"

#include <utility>

#include "ptx_parser_core.hpp"
#include "ptx_parser_registry.gen.hpp"

namespace ptx_frontend {

class PtxParser::Impl {
 public:
  Impl(std::string_view source, ParserOptions options)
      : parser(source, std::move(options)) {}

  ParserCore parser;
};

PtxParser::PtxParser(std::string_view source, ParserOptions options)
    : impl_(std::make_unique<Impl>(source, std::move(options))) {}

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
    std::string_view source, ParserOptions options) {
  PtxParser parser(source, std::move(options));
  return parser.parseInstruction();
}

}  // namespace ptx_frontend
