#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

std::string readCorpusFile(const std::filesystem::path& file) {
  std::ifstream input(file);
  if (!input)
    throw std::runtime_error("cannot read corpus file: " + file.string());
  return {std::istreambuf_iterator<char>{input}, {}};
}

TEST(ResolvedModule, ResolvesM11MultiGenerationTargetCorpus) {
  const auto corpus = std::filesystem::path{__FILE__}
                          .parent_path()
                          .parent_path()
                          .parent_path()
                          .parent_path() /
                      "corpus" / "m11";
  for (const std::string_view name : std::array{
           "sm80_supported.ptx",
           "sm90a_supported.ptx",
           "sm100_supported.ptx",
           "multi_target_profiles.ptx",
       }) {
    const auto file = corpus / name;
    SCOPED_TRACE(file.string());
    PtxSyntaxParser parser(readCorpusFile(file));
    const auto parsed = parser.parseModule();
    ASSERT_TRUE(parsed.has_value()) << file;
    EXPECT_TRUE(parsed.diagnostics.empty()) << file;

    const auto resolved = resolveModule(*parsed);
    ASSERT_TRUE(resolved.has_value())
        << (resolved.error().empty() ? "resolution failed"
                                     : resolved.error().front().message)
        << file;
    ASSERT_EQ(resolved->functions.size(),
              name == "multi_target_profiles.ptx" ? 3u : 1u);
    EXPECT_FALSE(resolved->functions.front().body.empty());
  }
}

TEST(ResolvedModule, DiagnosesM11UnsupportedTargetCorpus) {
  const auto corpus = std::filesystem::path{__FILE__}
                          .parent_path()
                          .parent_path()
                          .parent_path()
                          .parent_path() /
                      "corpus" / "m11";

  const auto resolve = [&](std::string_view name) {
    const auto file = corpus / name;
    SCOPED_TRACE(file.string());
    PtxSyntaxParser parser(readCorpusFile(file));
    const auto parsed = parser.parseModule();
    EXPECT_TRUE(parsed.has_value()) << file;
    EXPECT_TRUE(parsed.diagnostics.empty()) << file;
    return resolveModule(*parsed);
  };

  const auto cluster = resolve("sm80_cluster_unsupported.ptx");
  ASSERT_FALSE(cluster.has_value());
  ASSERT_EQ(cluster.error().size(), 2u);
  EXPECT_EQ(cluster.error()[0].message,
            ".reqnctapercluster is unavailable for module target 'sm_80'.");
  EXPECT_EQ(cluster.error()[1].message,
            "Operand value '%cluster_ctarank' has no matching availability "
            "clause.");

  const auto unknown = resolve("unknown_target_unsupported.ptx");
  ASSERT_FALSE(unknown.has_value());
  ASSERT_EQ(unknown.error().size(), 1u);
  EXPECT_EQ(unknown.error().front().message,
            "Unknown validation target 'sm_123a'.");
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
