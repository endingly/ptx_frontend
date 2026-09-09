#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size);

namespace {

TEST(PtxCstFuzz, HandlesRepresentativeByteSeeds) {
  constexpr std::array<std::string_view, 8> seeds{
      "",
      std::string_view{"\0\xff", 2},
      ".entry kernel() { {",
      ".version 9.3\n.entry kernel() { add.u32 %r0, %r1, %r2; }\n",
      "/*",
      ".entry k() { ret; }\n/* tail",
      ".entry k() { ret;\n/* tail",
      "` .entry k() { ret; }"};

  for (const std::string_view source : seeds) {
    EXPECT_EQ(LLVMFuzzerTestOneInput(
                  reinterpret_cast<const uint8_t*>(source.data()), source.size()),
              0);
  }
}

/** Generated depth seeds exercise rejection and cleanup without large fixtures. */
TEST(PtxCstFuzz, HandlesDeepConstantTreeSeeds) {
  for (const std::size_t depth : {127u, 128u, 129u, 1024u}) {
    const std::array seeds{
        ".global .u32 x = " + std::string(depth, '+') + "1;",
        ".global .u32 x = " + std::string(depth, '(') + "1" +
            std::string(depth, ')') + ";",
        ".global .u32 x[] = " + std::string(depth, '{') + "1" +
            std::string(depth, '}') + ";",
        ".global .u32 x = " + std::string(depth, '(') + "1 + ("};
    for (const auto& source : seeds) {
      EXPECT_EQ(LLVMFuzzerTestOneInput(
                    reinterpret_cast<const uint8_t*>(source.data()), source.size()),
                0);
    }
  }
}

}  // namespace
