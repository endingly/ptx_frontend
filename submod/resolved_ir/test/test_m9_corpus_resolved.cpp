#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

TEST(ResolvedModule, ResolvesAndChecksEveryM9CorpusModule) {
  const auto corpus = std::filesystem::path{__FILE__}
                          .parent_path()
                          .parent_path()
                          .parent_path()
                          .parent_path() /
                      "corpus" / "m9";
  ASSERT_TRUE(std::filesystem::is_directory(corpus));

  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(corpus)) {
    if (entry.path().extension() == ".ptx")
      files.push_back(entry.path());
  }
  std::ranges::sort(files);
  ASSERT_FALSE(files.empty());

  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 80},
  };
  for (const auto& file : files) {
    SCOPED_TRACE(file.string());
    std::ifstream input(file);
    ASSERT_TRUE(input) << file;
    const std::string source{std::istreambuf_iterator<char>{input}, {}};

    PtxSyntaxParser parser(source);
    const auto parsed = parser.parseModule();
    ASSERT_TRUE(parsed.has_value()) << file;
    EXPECT_TRUE(parsed.diagnostics.empty()) << file;

    const auto resolved = resolveModule(*parsed);
    ASSERT_TRUE(resolved.has_value()) << file;
    ASSERT_EQ(resolved->functions.size(), 1u);
    const auto& function = resolved->functions.front();
    EXPECT_FALSE(function.is_prototype);
    ASSERT_FALSE(function.body.empty());

    for (const auto& instruction : function.body) {
      const auto checked = std::visit(
          [&](const auto& value) { return checker::check(value, context); },
          instruction);
      EXPECT_TRUE(checked.has_value()) << file;
    }
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
