#pragma once
#include <memory>
#include <string_view>
#include "ptx_token.hpp"

namespace ptx_frontend {

class PtxLexer {
 public:
  struct Token {
    TokenKind kind;
    std::string_view text;
    int line;
    int column;
  };

  explicit PtxLexer(std::string_view src);
  ~PtxLexer();

  PtxLexer(const PtxLexer&) = delete;
  PtxLexer& operator=(const PtxLexer&) = delete;

  Token next();
  Token peek();
  Token consume();

 private:
  struct Impl;  // forward declaration of the implementation (Pimpl idiom)
  std::unique_ptr<Impl> impl_;

  bool has_peek_ = false;
  Token peek_{};
};

}  // namespace ptx_frontend