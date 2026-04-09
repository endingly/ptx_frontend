// #include <cstdint>
// #include <expected>
// #include <optional>
// #include <string_view>
// #include <variant>
// #include <vector>
import std;
import ptx_lexer;
#include <gtest/gtest.h>
#include "ptx_token.hpp"

using namespace ptx_frontend;

std::string_view ptx_intern(const char* s, std::size_t len, void*) {
  return {s, len};
}

TEST(PtxLexer, SimpleStatementMultiline) {
  std::string_view src = R"(
.version 8.0 .target sm_80

mov.b32 %r0, %r1;
  )";
  PtxLexer lexer(src);

  struct Expected {
    TokenKind kind;
    std::string_view text;
    int line;
  };
  Expected expected[] = {
      {TokenKind::DotVersion, ".version", 2},
      {TokenKind::F64, "8.0", 2},
      {TokenKind::DotTarget, ".target", 2},
      {TokenKind::Ident, "sm_80", 2},
      {TokenKind::Mov, "mov", 4},
      {TokenKind::DotB32, ".b32", 4},
      {TokenKind::Ident, "%r0", 4},
      {TokenKind::Comma, ",", 4},
      {TokenKind::Ident, "%r1", 4},
      {TokenKind::Semicolon, ";", 4},
      {TokenKind::Eof, "", 5},
  };

  for (auto& e : expected) {
    auto tok = lexer.next();
    EXPECT_EQ(tok.kind, e.kind) << "text=" << tok.text;
    EXPECT_EQ(tok.text, e.text);
    EXPECT_EQ(tok.line, e.line);
  }
}