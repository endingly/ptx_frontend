#include <gtest/gtest.h>

#include <array>
#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Encode a test magnitude in one PTX integer radix using standard formatting. */
std::string maskSpelling(uint64_t magnitude, int radix) {
  std::array<char, 65> digits{};
  const auto result = std::to_chars(
      digits.data(), digits.data() + digits.size(), magnitude, radix);
  EXPECT_EQ(result.ec, std::errc{});
  const std::string prefix = radix == 16  ? "0x"
                             : radix == 2 ? "0b"
                             : radix == 8 ? "0"
                                          : "";
  return prefix + std::string(digits.data(), result.ptr);
}

/** Resolve and validate source before its text, parser, and AST are destroyed. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> resolveMaskSource(
    std::string source) {
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  EXPECT_TRUE(ast.has_value());
  EXPECT_TRUE(ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveAndValidateModule(*ast);
}

/** Equivalent literal spellings preserve every legal byte extraction and relocation. */
TEST(StorageMaskRadices,
     NormalizesAllBytePositionsAcrossSourceAndOwnedValidation) {
  constexpr std::array radices{10, 8, 2, 16};
  for (unsigned byte = 0; byte != 8; ++byte) {
    const uint64_t mask = uint64_t{0xff} << (byte * 8);
    for (const auto radix : radices) {
      for (const auto suffix : {"", "U"}) {
        const auto spelling = maskSpelling(mask, radix) + suffix;
        SCOPED_TRACE(spelling);
        const auto module = resolveMaskSource(
            ".version 9.3\n.target sm_90\n.address_size 64\n"
            ".global .u32 target; .global .u8 numeric = " +
            spelling + "(0x0101010101010101U); .global .u8 address = " +
            spelling + "(target + 1);");
        ASSERT_TRUE(module.has_value())
            << (module.error().empty() ? "No diagnostic"
                                       : module.error().front().message);
        ASSERT_EQ(module->storage_declarations.size(), 3u);
        const auto& numeric = module->storage_declarations[1];
        ASSERT_EQ(numeric.initializer.size(), 1u);
        const auto* constant =
            std::get_if<StorageConstant>(&numeric.initializer.front().value);
        ASSERT_NE(constant, nullptr);
        EXPECT_EQ(constant->bits, 1u);
        EXPECT_EQ(constant->high_bits, 0u);
        const auto& address = module->storage_declarations[2];
        ASSERT_EQ(address.initializer.size(), 1u);
        const auto* relocation =
            std::get_if<StorageRelocation>(&address.initializer.front().value);
        ASSERT_NE(relocation, nullptr);
        EXPECT_EQ(relocation->symbol_id,
                  module->storage_declarations[0].symbol_id);
        EXPECT_EQ(module->symbols.symbol(relocation->symbol_id).name, "target");
        EXPECT_EQ(relocation->address_kind, StorageAddressKind::StateSpace);
        EXPECT_EQ(relocation->addend_bits, 1u);
        EXPECT_EQ(relocation->byte_mask, mask);
        const auto checked = validateModule(*module);
        EXPECT_TRUE(checked.has_value())
            << (checked ? "" : checked.error().front().message);
      }
    }
  }
}

/** The original decimal, octal, binary, and hex reproduction extracts the low byte. */
TEST(StorageMaskRadices, ExtractsLowByteOf257ForEveryLiteralSpelling) {
  for (const auto mask : {"255", "0377", "0b11111111", "0xFF"}) {
    SCOPED_TRACE(mask);
    const auto module = resolveMaskSource(
        ".version 9.3\n.target sm_90\n.address_size 64\n.global .u8 object = " +
        std::string(mask) + "(257);");
    ASSERT_TRUE(module.has_value()) << module.error().front().message;
    const auto& initializer = module->storage_declarations.front().initializer;
    ASSERT_EQ(initializer.size(), 1u);
    EXPECT_EQ(std::get<StorageConstant>(initializer.front().value).bits, 1u);
    EXPECT_TRUE(validateModule(*module));
  }
}

/** Radix normalization preserves separate integer and address version boundaries. */
TEST(StorageMaskRadices, PreservesIntegerAndAddressVersionBoundaries) {
  for (const auto mask : {"255", "0377", "0b11111111", "0xff"}) {
    for (const auto version : {"7.0", "7.1", "7.2", "7.3"}) {
      const std::string header = ".version " + std::string(version) +
                                 "\n.target sm_70\n.address_size 64\n";
      const auto integer = resolveMaskSource(
          header + ".global .u8 numeric = " + mask + std::string("(257);"));
      EXPECT_EQ(integer.has_value(), std::string_view(version) == "7.3");
      if (integer)
        EXPECT_TRUE(validateModule(*integer));
      const auto address = resolveMaskSource(
          header + ".global .u32 target; .global .u8 address = " + mask +
          std::string("(target);"));
      EXPECT_EQ(address.has_value(), std::string_view(version) != "7.0");
      if (address)
        EXPECT_TRUE(validateModule(*address));
    }
  }
}

/** Only exactly aligned one-byte masks remain legal, regardless of radix. */
TEST(StorageMaskRadices, RejectsInvalidMaskValuesAcrossRadices) {
  constexpr std::array<uint64_t, 7> invalid_masks{
      0, 1, 0xfe, 0x1fe, 0xffff, 0xff01, std::numeric_limits<uint64_t>::max()};
  for (const auto mask : invalid_masks) {
    for (const auto radix : {10, 8, 2, 16}) {
      const auto spelling = maskSpelling(mask, radix);
      SCOPED_TRACE(spelling);
      EXPECT_FALSE(resolveMaskSource(
          ".version 9.3\n.target sm_90\n.global .u8 object = " + spelling +
          "(257);"));
      EXPECT_FALSE(
          resolveMaskSource(".version 9.3\n.target sm_90\n.global .u32 target; "
                            ".global .u8 object = " +
                            spelling + "(target);"));
    }
  }
}

/** Mask syntax retains literal-only callees, integer grammar, and relocation order. */
TEST(StorageMaskRadices, RejectsNonliteralAndAdjacentUnsupportedForms) {
  constexpr std::array expressions{"0.255(257)",
                                   "0f437f0000(257)",
                                   "(255)(257)",
                                   "(+255)(257)",
                                   "(254 + 1)(257)",
                                   "18446744073709551616(257)",
                                   "04000000000000000000000(257)",
                                   "0x10000000000000000(257)",
                                   "0b10000000000000000000000000000000000000000"
                                   "000000000000000000000000(257)",
                                   "255(255(target))",
                                   "255(target) + 1",
                                   "generic(255(target))"};
  for (const auto expression : expressions) {
    SCOPED_TRACE(expression);
    EXPECT_FALSE(
        resolveMaskSource(".version 9.3\n.target sm_90\n.global .u32 target; "
                          ".global .u8 object = " +
                          std::string(expression) + ";"));
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
