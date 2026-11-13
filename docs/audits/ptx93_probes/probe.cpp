#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include <string>
#ifndef PARSE_ONLY
#include <magic_enum/magic_enum.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#endif
/** Emit a source range with one-based frontend coordinates. */
void range(const ptx_frontend::SourceRange& r) {
  std::cout << r.start.line << ':' << r.start.column << '-' << r.end.line << ':'
            << r.end.column;
}
/** Parse one file and optionally resolve and validate its complete module. */
int main(int argc, char** argv) {
  bool parse_only = false;
  std::string path;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--parse-only")
      parse_only = true;
    else
      path = argv[i];
  }
  if (path.empty()) {
    std::cerr << "usage: probe [--parse-only] file.ptx\n";
    return 64;
  }
  std::ifstream f(path);
  if (!f)
    return 66;
  const std::string source{std::istreambuf_iterator<char>(f), {}};
  try {
    ptx_frontend::PtxSyntaxParser parser(source);
    auto ast = parser.parseModule();
    std::cout << "parse value=" << bool(ast)
              << " diagnostics=" << ast.diagnostics.size() << '\n';
    for (const auto& d : ast.diagnostics) {
      std::cout << "stage=Syntax range=";
      range(d.range);
      std::cout << " message=" << std::quoted(d.message) << '\n';
    }
    if (!ast || !ast.diagnostics.empty())
      return 2;
#ifdef PARSE_ONLY
    return 0;
#else
    if (parse_only)
      return 0;
    auto resolved = ptx_frontend::resolved_ir::resolveAndValidateModule(*ast);
    std::cout << "resolveAndValidate value=" << bool(resolved)
              << " diagnostics=" << (resolved ? 0 : resolved.error().size())
              << '\n';
    if (resolved)
      return 0;
    for (const auto& d : resolved.error()) {
      std::cout << "stage=" << magic_enum::enum_name(d.stage());
      if (d.binding_kind)
        std::cout << " kind=" << magic_enum::enum_name(*d.binding_kind);
      if (d.declaration_kind)
        std::cout << " kind=" << magic_enum::enum_name(*d.declaration_kind);
      if (d.checker_kind)
        std::cout << " kind=" << magic_enum::enum_name(*d.checker_kind);
      std::cout << " range=";
      range(d.range);
      if (d.previous_range) {
        std::cout << " previous_range=";
        range(*d.previous_range);
      }
      std::cout << " message=" << std::quoted(d.message) << '\n';
    }
    return 3;
#endif
  } catch (const std::exception& e) {
    std::cout << "exception=" << std::quoted(e.what()) << '\n';
    return 4;
  }
}
