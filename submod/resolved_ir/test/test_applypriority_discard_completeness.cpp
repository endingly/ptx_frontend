#include <gtest/gtest.h>

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

/** Resolve a complete module and require its typed checks to pass. */
void expect_valid_module(std::string_view source) {
  const auto ast = parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  const auto resolved = resolveModule(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto checked = validateModule(*resolved);
  ASSERT_TRUE(checked.has_value()) << checked.error().front().message;
}

/** Require a complete module to fail resolution or semantic validation. */
void expect_invalid_module(std::string_view source) {
  const auto ast = parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  const auto resolved = resolveModule(*ast);
  if (resolved)
    EXPECT_FALSE(validateModule(*resolved));
}

/** Both spellings retain distinct variants and accept an unknown register. */
TEST(ApplypriorityDiscardCompleteness, ResolvesGenericAndExplicitForms) {
  const auto ast = parseModule(R"ptx(
.version 7.4
.target sm_80
.address_size 64
.global .align 128 .b8 global_value[128];
.visible .entry kernel() {
  .reg .u64 %rd0;
  applypriority.L2::evict_normal [%rd0], 128;
  applypriority.L2::evict_normal [global_value], 128;
  applypriority.global.L2::evict_normal [global_value], 128;
  discard.L2 [%rd0], 128;
  discard.L2 [global_value], 128;
  discard.global.L2 [global_value], 128;
}
)ptx");
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  const auto resolved = resolveModule(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  ASSERT_TRUE(validateModule(*resolved));
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 6u);
  EXPECT_TRUE(std::holds_alternative<Applypriority::GenericL2EvictNormal>(
      std::get<Applypriority>(body[0]).variant));
  EXPECT_TRUE(std::holds_alternative<Applypriority::GlobalL2EvictNormal>(
      std::get<Applypriority>(body[2]).variant));
  EXPECT_TRUE(std::holds_alternative<Discard::GenericL2>(
      std::get<Discard>(body[3]).variant));
  EXPECT_TRUE(std::holds_alternative<Discard::GlobalL2>(
      std::get<Discard>(body[5]).variant));
}

/** Generic cache operations require PTX 7.4 and SM 80 independently. */
TEST(ApplypriorityDiscardCompleteness, ChecksTargetFloors) {
  for (const auto instruction :
       {"applypriority.L2::evict_normal", "discard.L2"}) {
    for (const auto target :
         {".version 7.3\n.target sm_80\n", ".version 7.4\n.target sm_79\n"}) {
      SCOPED_TRACE(instruction);
      const std::string source =
          std::string(target) +
          ".address_size 64\n.visible .entry kernel() {\n"
          ".reg .u64 %rd0; " +
          instruction + " [%rd0], 128;\n}";
      expect_invalid_module(source);
    }
  }
}

/** Concrete non-global symbols, weak alignment, and wrong sizes are invalid. */
TEST(ApplypriorityDiscardCompleteness, ChecksAddressAndSize) {
  for (const auto instruction :
       {"applypriority.L2::evict_normal", "discard.L2"}) {
    for (const auto declaration : {
             ".local .align 128 .b8 value[128];",
             ".shared .align 128 .b8 value[128];",
             ".const .align 128 .b8 value[128];",
             ".global .align 64 .b8 value[128];",
         }) {
      SCOPED_TRACE(instruction);
      SCOPED_TRACE(declaration);
      const std::string source =
          std::string(".version 7.4\n.target sm_80\n.address_size 64\n") +
          declaration + "\n.visible .entry kernel() {\n" + instruction +
          " [value], 128;\n}";
      expect_invalid_module(source);
    }
    const std::string wrong_size =
        std::string(".version 7.4\n.target sm_80\n.address_size 64\n") +
        ".global .align 128 .b8 value[128];\n.visible .entry kernel() {\n" +
        instruction + " [value], 64;\n}";
    expect_invalid_module(wrong_size);
  }
  expect_valid_module(R"ptx(
.version 7.4
.target sm_80
.address_size 64
.global .align 128 .b8 value[128];
.visible .entry kernel() {
  applypriority.L2::evict_normal [value], 128;
  discard.L2 [value], 128;
}
)ptx");
}

/** Unsupported generic levels and non-immediate range sizes stay excluded. */
TEST(ApplypriorityDiscardCompleteness, RejectsUnsupportedGenericSyntax) {
  for (const auto source : {
           "applypriority.L1::evict_normal [%rd0], 128;",
           "applypriority.L2::evict_last [%rd0], 128;",
           "discard.L1 [%rd0], 128;",
           "applypriority.L2::evict_normal [%rd0], %r0;",
           "discard.L2 [%rd0], %r0;",
       }) {
    SCOPED_TRACE(source);
    const std::string module =
        std::string(".version 7.4\n.target sm_80\n.address_size 64\n") +
        ".visible .entry kernel() {\n.reg .u64 %rd0; .reg .u32 %r0;\n" +
        source + "\n}";
    expect_invalid_module(module);
  }
}

/** Owned generic address metadata is rechecked after source and AST die. */
TEST(ApplypriorityDiscardCompleteness, RevalidatesOwnedAddressWithoutAst) {
  std::optional<ResolvedModule> owned;
  {
    const std::string source = R"ptx(
.version 7.4
.target sm_80
.address_size 64
.global .align 128 .b8 value[128];
.visible .entry kernel() { discard.L2 [value], 128; }
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
  auto& discard = std::get<Discard::GenericL2>(
      std::get<Discard>(owned->functions.front().body.front()).variant);
  auto& symbol = std::get<ResolvedSymbolRef>(discard.address.value.base);
  ASSERT_EQ(symbol.address_alignment, 128u);
  symbol.address_alignment = 64;
  const auto invalid =
      validateModule(*owned, ModuleValidationPolicy::RequireCompleteContext);
  ASSERT_FALSE(invalid.has_value());
  EXPECT_EQ(invalid.error().front().kind,
            checker::CheckDiagnosticKind::AddressAlignmentMismatch);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
