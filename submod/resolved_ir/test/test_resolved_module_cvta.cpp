#include <gtest/gtest.h>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

TEST(ResolveCvta, ResolvesForwardSymbolAndOffsetSources) {
  PtxSyntaxParser parser(R"ptx(
.version 9.3
.target sm_80
.address_size 64
.global .u32 value;
.entry kernel() {
  .reg .u64 %rd<2>;
  cvta.global.u64 %rd0, value;
  cvta.global.u64 %rd1, value+4;
}
)ptx");
  const auto parsed = parser.parseModule();
  ASSERT_TRUE(parsed.has_value()) << parsed.diagnostics.front().message;
  const auto resolved = resolveModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 2u);

  const Cvta& direct = std::get<Cvta>(body[0]);
  const auto& direct_variant = std::get<Cvta::GlobalU64>(direct.variant);
  EXPECT_TRUE(
      std::holds_alternative<ResolvedSymbolRef>(direct_variant.src.value));

  const Cvta& offset = std::get<Cvta>(body[1]);
  const auto& offset_variant = std::get<Cvta::GlobalU64>(offset.variant);
  const auto* address = std::get_if<ResolvedAddress>(&offset_variant.src.value);
  ASSERT_NE(address, nullptr);
  EXPECT_TRUE(std::holds_alternative<ResolvedSymbolRef>(address->base));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
