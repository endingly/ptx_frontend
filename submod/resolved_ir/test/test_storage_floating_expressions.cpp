#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Resolve floating storage while releasing source, parser, and AST ownership. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> resolveFloatingStorage(
    std::string source, bool check_source) {
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  EXPECT_TRUE(ast.has_value());
  EXPECT_TRUE(ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return check_source ? resolveAndValidateModule(*ast)
                      : resolveModuleOnly(*ast);
}

/** A storage expression and its independently specified final binary bits. */
struct FloatingStorageFixture {
  /** PTX destination suffix, without its leading period. */
  std::string_view type;
  /** Source expression evaluated before the destination conversion. */
  std::string_view expression;
  /** Expected low word of the owned normalized storage constant. */
  uint64_t bits;
};

/** Common arithmetic, promotion, comparisons, and direct payloads survive AST release. */
TEST(StorageFloatingExpressions,
     NormalizesSourceAndOwnedBitsAcrossNumericCohort) {
  constexpr std::array fixtures{
      FloatingStorageFixture{"f64", "(1.0 + 2.0) * 3.0", 0x4022000000000000},
      FloatingStorageFixture{"f32", "(1.0 + 2.0) * 3.0", 0x41100000},
      FloatingStorageFixture{"f64", "0d4000000000000000 + 1.0",
                             0x4008000000000000},
      FloatingStorageFixture{"f64", "1 ? 2.0 : 3.0", 0x4000000000000000},
      FloatingStorageFixture{"f64", "0 ? 2.0 : 3.0", 0x4008000000000000},
      FloatingStorageFixture{"f64", "1 ? 2 : 3.0", 0x4000000000000000},
      FloatingStorageFixture{"f64", "0 ? 2.0 : 3", 0x4008000000000000},
      FloatingStorageFixture{"f64", "1 + 2.0", 0x4008000000000000},
      FloatingStorageFixture{"f64", "2.0 + -1", 0x3ff0000000000000},
      FloatingStorageFixture{"f64", "-1 + 0.0", 0xbff0000000000000},
      FloatingStorageFixture{"f64", "18446744073709551615U + 0.0",
                             0x43f0000000000000},
      FloatingStorageFixture{"f64", "-0.0", 0x8000000000000000},
      FloatingStorageFixture{"f32", "-0.0", 0x80000000},
      FloatingStorageFixture{"f64", "+0d8000000000000000", 0x8000000000000000},
      FloatingStorageFixture{"f64", "-0d8000000000000000", 0},
      FloatingStorageFixture{"f64", "0d7ff8000000001234", 0x7ff8000000001234},
      FloatingStorageFixture{"f32", "0f7fc01234", 0x7fc01234},
      FloatingStorageFixture{"f32", "0f80000000", 0x80000000},
      FloatingStorageFixture{"f64", "0f3f800000", 0x3ff0000000000000},
      FloatingStorageFixture{"u32", "1.0 < 2.0", 1},
      FloatingStorageFixture{"u32", "2.0 == 2", 1},
      FloatingStorageFixture{"u32", "1 ? (1.0 < 2) : 0", 1},
      FloatingStorageFixture{"u32", "0 ? 1 : (2.0 < 1)", 0},
  };
  for (const auto& fixture : fixtures) {
    for (const auto source_check : {true, false}) {
      SCOPED_TRACE(fixture.expression);
      const auto module = resolveFloatingStorage(
          ".version 9.3\n.target sm_90\n.address_size 64\n.global ." +
              std::string(fixture.type) +
              " object = " + std::string(fixture.expression) + ";",
          source_check);
      ASSERT_TRUE(module.has_value()) << module.error().front().message;
      ASSERT_EQ(module->storage_declarations.size(), 1u);
      const auto& values = module->storage_declarations.front().initializer;
      ASSERT_EQ(values.size(), 1u);
      const auto* value = std::get_if<StorageConstant>(&values.front().value);
      ASSERT_NE(value, nullptr);
      EXPECT_EQ(value->bits, fixture.bits);
      EXPECT_EQ(value->high_bits, 0u);
      const auto checked = validateModule(*module);
      EXPECT_TRUE(checked.has_value())
          << (checked ? "" : checked.error().front().message);
    }
  }
}

/** Exact-F32 expression uses remain invalid even in an unselected branch. */
TEST(StorageFloatingExpressions,
     RejectsExactF32OperatorUsesAndUnsupportedDestinations) {
  constexpr std::array expressions{"+0f3f800000",
                                   "-0f3f800000",
                                   "+(0f3f800000)",
                                   "0f3f800000 + 1.0",
                                   "1.0 + 0f3f800000",
                                   "0f3f800000 == 0f3f800000",
                                   "1 ? 2.0 : 0f3f800000",
                                   "0 ? 0f3f800000 : 2.0",
                                   "0f3f800000 ? 1.0 : 2.0",
                                   "(.u64)0f3f800000"};
  for (const auto expression : expressions) {
    for (const auto source_check : {true, false}) {
      SCOPED_TRACE(expression);
      EXPECT_FALSE(resolveFloatingStorage(
          ".version 9.3\n.target sm_90\n.global .f64 object = " +
              std::string(expression) + ";",
          source_check));
    }
  }
  for (const auto declaration :
       {".b32 bits = (1.0 + 2.0);", ".u32 integer = (1.0 + 2.0);",
        ".f64 floating = (1.0 < 2.0);", ".f64 floating = (1.0 / 0.0);"}) {
    for (const auto source_check : {true, false}) {
      SCOPED_TRACE(declaration);
      EXPECT_FALSE(resolveFloatingStorage(
          ".version 9.3\n.target sm_90\n.global " + std::string(declaration),
          source_check));
    }
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
