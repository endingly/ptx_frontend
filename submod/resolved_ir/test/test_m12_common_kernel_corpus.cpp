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

struct CorpusCase {
  std::string_view file_name;
  std::string_view target;
  size_t function_count;
};

void expectM12CorpusModule(const CorpusCase& corpus_case) {
  const auto corpus = std::filesystem::path{__FILE__}
                          .parent_path()
                          .parent_path()
                          .parent_path()
                          .parent_path() /
                      "corpus" / "m12";
  const auto file = corpus / corpus_case.file_name;
  SCOPED_TRACE(file.string());
  PtxSyntaxParser parser(readCorpusFile(file));
  const auto parsed = parser.parseModule();
  ASSERT_TRUE(parsed.has_value()) << file;
  ASSERT_GE(parsed->items.size(), 3u);
  EXPECT_EQ(std::get<syntax_ast::AstVersionDirective>(parsed->items[0]).version.text,
            "9.3");
  const auto& target = std::get<syntax_ast::AstTargetDirective>(parsed->items[1]);
  ASSERT_EQ(target.targets.size(), 1u);
  EXPECT_EQ(target.targets[0].text, corpus_case.target);
  EXPECT_EQ(std::get<syntax_ast::AstAddressSizeDirective>(parsed->items[2])
                .bit_width.text,
            "64");

  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value())
      << (resolved.error().empty() ? "resolution failed"
                                   : resolved.error().front().message);
  ASSERT_EQ(resolved->functions.size(), corpus_case.function_count);

  const auto profile = base::find_target_profile(corpus_case.target);
  ASSERT_TRUE(profile.has_value()) << corpus_case.target;
  const checker::Context context{
      .target = {.ptx_version = {9, 3},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities},
  };
  for (const auto& function : resolved->functions) {
    ASSERT_FALSE(function.body.empty()) << function.name << file;
    for (const auto& instruction : function.body) {
      const auto checked = std::visit(
          [&](const auto& value) { return checker::check(value, context); },
          instruction);
      EXPECT_TRUE(checked.has_value()) << function.name << file;
    }
  }
}

TEST(ResolvedModule, ResolvesAndChecksEveryM12CommonKernelCorpusModule) {
  for (const CorpusCase corpus_case : std::array{
           CorpusCase{"common_kernel_sm80.ptx", "sm_80", 31},
           CorpusCase{"common_kernel_sm90a.ptx", "sm_90a", 32},
           CorpusCase{"common_kernel_sm100.ptx", "sm_100", 31},
       })
    expectM12CorpusModule(corpus_case);
}

TEST(ResolvedModule, ResolvesAndChecksEveryM12NaturalKernelCorpusModule) {
  for (const CorpusCase corpus_case : std::array{
           CorpusCase{"natural_kernel_sm80.ptx", "sm_80", 1},
           CorpusCase{"natural_kernel_sm90a.ptx", "sm_90a", 1},
           CorpusCase{"natural_kernel_sm100.ptx", "sm_100", 1},
       })
    expectM12CorpusModule(corpus_case);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
