#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

using test_helpers::parseModule;

/** Bound addresses and declared registers retain space, width, and alignment checks. */
TEST(LduCompleteness, ChecksModuleAddressAndDestinationContracts) {
  const auto valid_ast = parseModule(R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .align 16 .b8 global_value[16];
.entry kernel() {
  .reg .u64 %wide;
  .reg .u32 %r<4>;
  .reg .f64 %fd<2>;
  .reg .b128 %b0;
  ldu.u32 %wide, [global_value];
  ldu.global.v4.u32 {%r0, %r1, %r2, %r3}, [global_value];
  ldu.global.v2.f64 {%fd0, %fd1}, [global_value];
  ldu.global.b128 %b0, [global_value];
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(valid_ast);
  const auto valid = resolveModule(*valid_ast);
  ASSERT_TRUE(valid.has_value()) << valid.error().front().message;
  EXPECT_TRUE(validateModule(*valid));

  for (const auto source : {
           R"ptx(.version 9.3
.target sm_90
.address_size 64
.entry kernel() {
  .local .align 16 .b8 local_value[16];
  .reg .u32 %r<4>;
  ldu.v4.u32 {%r0, %r1, %r2, %r3}, [local_value];
})ptx",
           R"ptx(.version 9.3
.target sm_90
.address_size 64
.global .align 4 .b8 global_value[16];
.entry kernel() {
  .reg .u32 %r<4>;
  ldu.global.v4.u32 {%r0, %r1, %r2, %r3}, [global_value];
})ptx",
           R"ptx(.version 9.3
.target sm_90
.address_size 64
.global .align 4 .u32 global_value;
.entry kernel() {
  .reg .u16 %narrow;
  ldu.global.u32 %narrow, [global_value];
})ptx",
       }) {
    SCOPED_TRACE(source);
    const auto ast = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    const auto resolved = resolveModule(*ast);
    if (resolved)
      EXPECT_FALSE(validateModule(*resolved));
  }
}

/** Retained symbol alignment is rechecked after the source AST is destroyed. */
TEST(LduCompleteness, RevalidatesOwnedBoundAddressWithoutAst) {
  std::optional<ResolvedModule> owned;
  {
    const std::string source = R"ptx(
.version 9.3
.target sm_90
.address_size 64
.global .align 16 .b8 global_value[16];
.entry kernel() {
  .reg .u32 %r<4>;
  ldu.global.v4.u32 {%r0, %r1, %r2, %r3}, [global_value];
}
)ptx";
    const auto ast = parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto resolved = resolveModuleOnly(*ast);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned.emplace(std::move(*resolved));
  }
  ASSERT_TRUE(owned.has_value());
  ASSERT_TRUE(
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext));
  auto& load = std::get<Ldu::ExplicitV4>(
      std::get<Ldu>(owned->functions.front().body.front()).variant);
  auto& symbol = std::get<ResolvedSymbolRef>(load.address.value.base);
  ASSERT_EQ(symbol.address_alignment, 16u);
  symbol.address_alignment = 4;
  const auto invalid =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid.has_value());
  EXPECT_EQ(invalid.error().front().kind,
            checker::CheckDiagnosticKind::AddressAlignmentMismatch);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
