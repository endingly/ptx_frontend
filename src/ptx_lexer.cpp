#include "ptx_lexer.hpp"
#include "ptx_token.hpp"

typedef ptx_frontend::PtxSVal YYSTYPE;
#include "_ptx_lexer.hpp"  // from ${PTX_GEN_DIR}, only visible to lexer impl

namespace ptx_frontend {

struct PtxLexer::Impl {
  yyscan_t scanner{};
  YY_BUFFER_STATE buf{};
  PtxLexerExtra extra{};
};

PtxLexer::PtxLexer(std::string_view src) : impl_(std::make_unique<Impl>()) {
  yylex_init(&impl_->scanner);

  yyset_extra(&impl_->extra, impl_->scanner);

  impl_->buf =
      yy_scan_bytes(src.data(), static_cast<int>(src.size()), impl_->scanner);
}

PtxLexer::~PtxLexer() {
  yy_delete_buffer(impl_->buf, impl_->scanner);
  yylex_destroy(impl_->scanner);
}

PtxLexer::Token PtxLexer::next() {
  PtxSVal sval{};
  TokenKind kind = static_cast<TokenKind>(yylex(&sval, impl_->scanner));
  return Token{kind, sval.sv, sval.range};
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