#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend {
namespace {

TEST(PtxSyntaxParser, ParsesM9CorpusModulesWithoutDiagnostics) {
  const auto root = std::filesystem::path(__FILE__)
                        .parent_path()
                        .parent_path()
                        .parent_path()
                        .parent_path();
  for (const char* name :
       std::array{"ret.ptx", "exit.ptx", "trap.ptx", "scalar_integer.ptx",
                  "scalar_float.ptx", "address_conversion.ptx"}) {
    std::ifstream input(root / "corpus" / "m9" / name);
    ASSERT_TRUE(input) << name;
    const std::string source{std::istreambuf_iterator<char>{input}, {}};

    PtxSyntaxParser parser(source);
    const auto module = parser.parseModule();
    ASSERT_TRUE(module.has_value()) << name;
    EXPECT_TRUE(module.diagnostics.empty()) << name;
  }
}

}  // namespace
}  // namespace ptx_frontend
