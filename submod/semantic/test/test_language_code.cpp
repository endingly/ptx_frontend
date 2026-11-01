#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::declaration_semantics {
namespace {
/** Official quoted spellings and codes are explicit API expected values. */
constexpr std::array<std::pair<std::string_view, uint64_t>, 9> names{
    {std::pair<std::string_view, uint64_t>{"unknown", 0},
     {"ptx", 3},
     {"nvvm", 4},
     {"cuda c++", 5},
     {"cuda c++ tile", 6},
     {"tile ir", 7},
     {"python-cutile", 8},
     {"fortran", 9},
     {"optix", 10}}};

/** ASCII folding is deterministic and leaves spaces, pluses and hyphens intact. */
std::string upperLanguage(std::string_view name) {
  std::string value{name};
  for (auto& character : value)
    if (character >= 'a' && character <= 'z')
      character = static_cast<char>(character - 'a' + 'A');
  return value;
}

/** Source redeclarations use ordered decoded values while retaining raw tokens. */
std::vector<DeclarationDiagnostic> languageDeclarations(std::string first,
                                                        std::string second) {
  PtxSyntaxParser parser(".version 9.3\n.target sm_80\n.func f() .language " +
                         first + ";\n.func f() .language " + second +
                         " {ret;}\n");
  const auto ast = parser.parseModule();
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return {};
  const auto bound = binding::bindSymbols(*ast);
  return checkDeclarations(*ast, bound.table);
}

/** Every named and numeric code, including both Reserved integers, is accepted. */
TEST(LanguageCode, DecodesTheExactOfficialDomain) {
  /** Exact mixed-case original tokens from the finite domain fixtures. */
  const std::array<std::string_view, 9> mixed{
      "\"UnKnOwN\"",       "\"PtX\"",           "\"NvVm\"",
      "\"CuDa c++\"",      "\"CuDa c++ tIlE\"", "\"TiLe iR\"",
      "\"PyThOn-cUtIlE\"", "\"FoRtRaN\"",       "\"OpTiX\""};
  for (size_t index = 0; index < mixed.size(); ++index) {
    EXPECT_EQ(languageCode(mixed[index]), names[index].second);
    EXPECT_TRUE(languageDeclarations(std::string(mixed[index]),
                                     std::to_string(names[index].second))
                    .empty());
  }
  for (const auto& [name, code] : names) {
    EXPECT_EQ(languageCode("\"" + std::string(name) + "\""), code);
    EXPECT_EQ(languageCode("\"" + upperLanguage(name) + "\""), code);
    EXPECT_TRUE(languageDeclarations("\"" + upperLanguage(name) + "\"",
                                     std::to_string(code))
                    .empty());
  }
  for (uint64_t code = 0; code <= 10; ++code)
    EXPECT_EQ(languageCode(std::to_string(code)), code);
  for (const auto spelling : {"0x3U", "03u", "0B11U"})
    EXPECT_EQ(languageCode(spelling), 3u);
}

/** One-token decoding never trims, unescapes, evaluates or coerces names/numbers. */
TEST(LanguageCode, RejectsValuesOutsideTheRawTokenContract) {
  for (const std::string_view value : {"\"bogus\"",
                                       "\"\"",
                                       "\"Reserved\"",
                                       "\"3\"",
                                       "ptx",
                                       "11",
                                       "4294967296",
                                       "18446744073709551616U",
                                       "-1",
                                       "+3",
                                       "3.0",
                                       "1+2",
                                       "WARP_SZ",
                                       " 3",
                                       "3 ",
                                       "\" ptx\"",
                                       "\"cuda  c++\"",
                                       "\"python cutile\"",
                                       "\"\\x70tx\"",
                                       "\"cuda\\x20c++\"",
                                       "\"ptx",
                                       "\"ptx\"\"",
                                       "3,4"}) {
    SCOPED_TRACE(value);
    EXPECT_FALSE(languageCode(value));
  }
  EXPECT_FALSE(languageCode(""));
}

/** Redeclaration aliases compare code order, not raw spelling or unordered sets. */
TEST(LanguageCode, PreservesOrderedRedeclarationEquivalence) {
  EXPECT_TRUE(languageDeclarations("\"PTX\", \"nvvm\"", "0x3U, 4").empty());
  EXPECT_TRUE(languageDeclarations("\"PTX\", 3, 1, 2", "3, 03U, 1, 2").empty());
  const auto reversed = languageDeclarations("3, 4", "4, 3");
  EXPECT_TRUE(std::ranges::any_of(
      reversed, [](const DeclarationDiagnostic& diagnostic) {
        return diagnostic.kind ==
               DeclarationDiagnosticKind::IncompatibleRedeclaration;
      }));
}
}  // namespace
}  // namespace ptx_frontend::declaration_semantics
