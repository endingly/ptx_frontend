#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size);

namespace {

TEST(PtxCstFuzz, HandlesRepresentativeByteSeeds) {
  constexpr std::array<std::string_view, 4> seeds{
      "", std::string_view{"\0\xff", 2}, ".entry kernel() { {",
      ".version 9.3\n.entry kernel() { add.u32 %r0, %r1, %r2; }\n"};

  for (const std::string_view source : seeds) {
    EXPECT_EQ(LLVMFuzzerTestOneInput(
                  reinterpret_cast<const uint8_t*>(source.data()), source.size()),
              0);
  }
}

}  // namespace
