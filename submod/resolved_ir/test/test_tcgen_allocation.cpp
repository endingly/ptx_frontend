#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Parse a complete allocation fixture; parse failures fail the calling test. */
std::optional<syntax_ast::AstModule> parse_allocation_module(
    std::string_view source) {
  PtxSyntaxParser parser(source);
  auto parsed = parser.parseModule();
  if (!parsed || !parsed.diagnostics.empty()) {
    ADD_FAILURE() << (parsed.diagnostics.empty()
                          ? "Tensor Memory fixture did not parse."
                          : parsed.diagnostics.front().message);
    return std::nullopt;
  }
  return std::move(*parsed);
}

/** Wrap one instruction list in a complete module with typed declarations. */
std::string allocation_source(std::string_view instructions,
                              std::string_view ptx = "8.6",
                              std::string_view target = "sm_100a") {
  return ".version " + std::string(ptx) + "\n.target " + std::string(target) +
         R"ptx(
.address_size 64
.visible .entry kernel() {
  .shared .align 4 .b32 slot[2];
  .reg .b32 %t, %n;
  .reg .s32 %signed_count;
  .reg .u32 %unsigned_count;
  .reg .b64 %wide;
  .reg .f32 %float_count;
)ptx" + std::string(instructions) +
         R"ptx(
  ret;
}
)ptx";
}

/** Resolve and check a complete fixture, retaining parser diagnostics. */
bool accepts_allocation(std::string_view source) {
  auto ast = parse_allocation_module(source);
  if (!ast)
    return false;
  return resolveAndValidateModule(*ast).has_value();
}

/** Each allocation action and group accepts its legal written form. */
TEST(TcgenAllocation, AcceptsActionsGroupsAndConvertedCounts) {
  EXPECT_EQ(Tcgen05::AllocSharedCta::allocation_action,
            TcgenAllocationAction::Alloc);
  EXPECT_EQ(Tcgen05::AllocSharedCta::permit_effect,
            TcgenAllocationPermitEffect::RequiresPermit);
  EXPECT_EQ(Tcgen05::Dealloc::allocation_action,
            TcgenAllocationAction::Dealloc);
  EXPECT_EQ(Tcgen05::RelinquishAllocPermit::permit_effect,
            TcgenAllocationPermitEffect::RelinquishesPermit);
  EXPECT_EQ(tcgen_issue_granularity(TcgenCtaGroup::One),
            TcgenIssueGranularity::OneWarp);
  EXPECT_EQ(tcgen_issue_granularity(TcgenCtaGroup::Two),
            TcgenIssueGranularity::WarpPair);
  for (std::string_view columns :
       {"32", "64", "128", "256", "512", "4294967328", "-4294967264"}) {
    SCOPED_TRACE(columns);
    const std::string source = allocation_source(
        "  tcgen05.alloc.cta_group::1.sync.aligned.shared::cta.b32 "
        "[slot], " +
        std::string(columns) +
        ";\n"
        "  ld.shared.b32 %t, [slot];\n"
        "  tcgen05.dealloc.cta_group::1.sync.aligned.b32 %t, " +
        std::string(columns) +
        ";\n"
        "  tcgen05.relinquish_alloc_permit.cta_group::1.sync.aligned;\n");
    EXPECT_TRUE(accepts_allocation(source));
  }
  EXPECT_TRUE(accepts_allocation(allocation_source(
      "  tcgen05.alloc.cta_group::2.sync.aligned.b32 [slot], %n;\n"
      "  tcgen05.dealloc.cta_group::2.sync.aligned.b32 0, %n;\n"
      "  tcgen05.relinquish_alloc_permit.cta_group::2.sync.aligned;\n")));
  EXPECT_TRUE(accepts_allocation(allocation_source(
      "  tcgen05.alloc.cta_group::1.sync.aligned.shared::cta.b32 [slot], "
      "%signed_count;\n"
      "  tcgen05.dealloc.cta_group::1.sync.aligned.b32 %t, "
      "%unsigned_count;\n")));
}

/** Converted column count, carrier shape, and result-slot placement are checked. */
TEST(TcgenAllocation, RejectsKnownInvalidOperandsAndLocations) {
  for (std::string_view columns : {"16", "48", "513", "-1"}) {
    SCOPED_TRACE(columns);
    EXPECT_FALSE(accepts_allocation(allocation_source(
        "  tcgen05.alloc.cta_group::1.sync.aligned.shared::cta.b32 "
        "[slot], " +
        std::string(columns) + ";\n")));
  }
  for (std::string_view source : {
           "tcgen05.dealloc.cta_group::1.sync.aligned.b32 %wide, 32;",
           "tcgen05.dealloc.cta_group::1.sync.aligned.b32 %float_count, 32;",
           "tcgen05.alloc.cta_group::1.sync.aligned.shared::cta.b32 "
           "[slot], %wide;",
           "tcgen05.alloc.cta_group::1.sync.aligned.shared::cta.b32 "
           "[slot], %float_count;",
           "tcgen05.alloc.cta_group::1.sync.aligned.shared::cta.b32 "
           "[slot+2], 32;",
       }) {
    SCOPED_TRACE(source);
    EXPECT_FALSE(accepts_allocation(allocation_source(source)));
  }
  const auto wrong_space = allocation_source(
      "  .local .align 4 .b32 output;\n"
      "  tcgen05.alloc.cta_group::1.sync.aligned.shared::cta.b32 "
      "[output], 32;\n");
  EXPECT_FALSE(accepts_allocation(wrong_space));
  EXPECT_TRUE(accepts_allocation(allocation_source(
      "  tcgen05.alloc.cta_group::1.sync.aligned.shared::cta.b32 "
      "[slot+4], 32;\n")));
}

/** Qualified target introductions stay distinct from generic and later SMs. */
TEST(TcgenAllocation, PreservesExactTargetAndVersionFloors) {
  constexpr std::string_view instruction =
      "tcgen05.alloc.cta_group::1.sync.aligned.shared::cta.b32 [slot], 32;";
  for (const auto [ptx, target, accepted] : {
           std::tuple{"8.5", "sm_100a", false},
           std::tuple{"8.6", "sm_100a", true},
           std::tuple{"8.7", "sm_100f", false},
           std::tuple{"8.8", "sm_100f", true},
           std::tuple{"8.8", "sm_110a", false},
           std::tuple{"9.0", "sm_110a", true},
           std::tuple{"9.0", "sm_110f", true},
           std::tuple{"9.3", "sm_120a", false},
           std::tuple{"9.3", "sm_100", false},
       }) {
    SCOPED_TRACE(std::string(ptx) + " " + target);
    EXPECT_EQ(accepts_allocation(allocation_source(instruction, ptx, target)),
              accepted);
  }
}

/** CTA-group equality is local to each kernel or device-function body. */
TEST(TcgenAllocation, ChecksGroupConsistencyPerBody) {
  EXPECT_TRUE(accepts_allocation(R"ptx(
.version 9.0
.target sm_110a
.address_size 64
.entry first() {
  .shared .align 4 .b32 slot;
  tcgen05.alloc.cta_group::1.sync.aligned.shared::cta.b32 [slot], 32;
  ret;
}
.entry second() {
  .shared .align 4 .b32 slot;
  tcgen05.alloc.cta_group::2.sync.aligned.shared::cta.b32 [slot], 32;
  ret;
}
)ptx"));
  EXPECT_FALSE(accepts_allocation(allocation_source(
      "  tcgen05.alloc.cta_group::1.sync.aligned.shared::cta.b32 "
      "[slot], 32;\n"
      "  tcgen05.dealloc.cta_group::2.sync.aligned.b32 %t, 32;\n")));
}

/** Owned operands remain checked after the source AST and parser are gone. */
TEST(TcgenAllocation, RejectsOwnedCountAddressAndGroupTampering) {
  std::optional<ResolvedModule> owned;
  {
    auto ast = parse_allocation_module(allocation_source(
        "  tcgen05.alloc.cta_group::1.sync.aligned.shared::cta.b32 "
        "[slot], 32;\n"
        "  tcgen05.dealloc.cta_group::1.sync.aligned.b32 %t, 32;\n"));
    ASSERT_TRUE(ast);
    auto resolved = resolveAndValidateModule(*ast);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned = std::move(*resolved);
  }
  ASSERT_TRUE(owned);
  auto& alloc = *owned->functions.front().body[0].get_if<Tcgen05>();
  auto& selected = std::get<Tcgen05::AllocSharedCta>(alloc.variant);
  const auto saved_layout = selected.operand_layout;
  selected.operand_layout = ResolvedOperandLayoutTag{99};
  EXPECT_FALSE(validateModule(*owned).has_value());
  selected.operand_layout = saved_layout;
  auto& count = std::get<ResolvedImmediate>(selected.ncols.value);
  count.bits = 64;
  EXPECT_FALSE(validateModule(*owned).has_value());
  count.bits = 32;
  count.integer_source_bits = 64;
  EXPECT_FALSE(validateModule(*owned).has_value());
  count.integer_source_bits = 32;
  selected.cta_group.value = static_cast<TcgenCtaGroup>(255);
  EXPECT_FALSE(validateModule(*owned).has_value());
  selected.cta_group.value = TcgenCtaGroup::One;
  auto& slot = std::get<ResolvedSymbolRef>(selected.dst.value.base);
  const auto saved_slot_id = slot.symbol_id;
  slot.symbol_id = binding::SymbolId{.value = 999999u};
  EXPECT_FALSE(validateModule(*owned).has_value());
  slot.symbol_id = saved_slot_id;
  auto& dealloc = *owned->functions.front().body[1].get_if<Tcgen05>();
  auto& dealloc_form = std::get<Tcgen05::Dealloc>(dealloc.variant);
  dealloc_form.cta_group.value = TcgenCtaGroup::Two;
  EXPECT_FALSE(validateModule(*owned).has_value());
  dealloc_form.cta_group.value = TcgenCtaGroup::One;
  auto& address = std::get<ResolvedRegisterRef>(dealloc_form.taddr.value.value);
  address.vector_width = 2;
  EXPECT_FALSE(validateModule(*owned).has_value());
  address.vector_width.reset();
  address.register_class = ResolvedRegisterClass::Predicate;
  EXPECT_FALSE(validateModule(*owned).has_value());
  address.register_class = ResolvedRegisterClass::General;
  EXPECT_TRUE(validateModule(*owned).has_value());
}

/** A deallocation address keeps its own converted bits and source identity. */
TEST(TcgenAllocation, RejectsOwnedImmediateTensorAddressTampering) {
  std::optional<ResolvedModule> owned;
  {
    auto ast = parse_allocation_module(allocation_source(
        "  tcgen05.dealloc.cta_group::1.sync.aligned.b32 0, 32;\n"));
    ASSERT_TRUE(ast);
    auto resolved = resolveAndValidateModule(*ast);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
    owned = std::move(*resolved);
  }
  ASSERT_TRUE(owned);
  auto& operation = *owned->functions.front().body[0].get_if<Tcgen05>();
  auto& dealloc = std::get<Tcgen05::Dealloc>(operation.variant);
  auto& address = std::get<ResolvedImmediate>(dealloc.taddr.value.value);
  address.bits = 1;
  EXPECT_FALSE(validateModule(*owned).has_value());
  address.bits = 0;
  address.integer_source_bits = 1;
  EXPECT_FALSE(validateModule(*owned).has_value());
  address.integer_source_bits = 0;
  EXPECT_TRUE(validateModule(*owned).has_value());
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
