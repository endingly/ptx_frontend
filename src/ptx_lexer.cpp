#include "ptx_lexer.hpp"

typedef ptx_frontend::PtxSVal YYSTYPE;
#include "_ptx_lexer.hpp"  // from ${PTX_GEN_DIR}, only visible to the lexer implementation

namespace ptx_frontend {

struct PtxLexer::Impl {
  yyscan_t scanner{};
  YY_BUFFER_STATE buf{};
};

PtxLexer::PtxLexer(std::string_view src) : impl_(std::make_unique<Impl>()) {
  yylex_init(&impl_->scanner);
  impl_->buf = yy_scan_bytes(src.data(), (int)src.size(), impl_->scanner);
  yyset_lineno(1, impl_->scanner);
  yyset_column(1, impl_->scanner);
}

PtxLexer::~PtxLexer() {
  yy_delete_buffer(impl_->buf, impl_->scanner);
  yylex_destroy(impl_->scanner);
}

PtxLexer::Token PtxLexer::next() {
  PtxSVal sval{};
  // 1. record the token line/col
  int32_t start_line = yyget_lineno(impl_->scanner);
  int32_t start_col = yyget_column(impl_->scanner);
  // 2. get the token kind and text
  TokenKind kind = static_cast<TokenKind>(yylex(&sval, impl_->scanner));
  // 3. construct the token with text and line/col info
  int32_t end_line = yyget_lineno(impl_->scanner);
  int32_t end_col = yyget_column(impl_->scanner);
  // 4. construct the token with text and line/col info
  SourceRange range{{start_line, start_col}, {end_line, end_col}};
  return Token{kind, sval.sv, range};
}

PtxLexer::Token PtxLexer::peek() {
  if (!has_peek_) {
    peek_ = next();
    has_peek_ = true;
  }
  return peek_;
}

PtxLexer::Token PtxLexer::consume() {
  if (has_peek_) {
    has_peek_ = false;
    return peek_;
  }
  return next();
}

}  // namespace ptx_frontend