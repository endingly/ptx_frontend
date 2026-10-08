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

/** Resolve and check a fixture, optionally reporting its first diagnostic. */
bool accepts_allocation(std::string_view source, bool report = false) {
  auto ast = parse_allocation_module(source);
  if (!ast)
    return false;
  auto resolved = resolveAndValidateModule(*ast);
  if (!resolved && report) {
    ADD_FAILURE() << (resolved.error().empty()
                          ? "Allocation module rejected without diagnostics."
                          : resolved.error().front().message);
  }
  return resolved.has_value();
}

/** Each allocation action and group accepts its legal written form. */
TEST(TcgenAllocation, AcceptsActionsGroupsAndConvertedCounts) {
  EXPECT_EQ(Tcgen05AllocSharedCta::allocation_action,
            TcgenAllocationAction::Alloc);
  EXPECT_EQ(Tcgen05AllocSharedCta::permit_effect,
            TcgenAllocationPermitEffect::RequiresPermit);
  EXPECT_EQ(Tcgen05Dealloc::allocation_action, TcgenAllocationAction::Dealloc);
  EXPECT_EQ(Tcgen05RelinquishAllocPermit::permit_effect,
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
    EXPECT_TRUE(accepts_allocation(source, true));
  }
  EXPECT_TRUE(accepts_allocation(
      allocation_source(
          "  tcgen05.alloc.cta_group::2.sync.aligned.b32 [slot], %n;\n"
          "  tcgen05.dealloc.cta_group::2.sync.aligned.b32 0, %n;\n"
          "  tcgen05.relinquish_alloc_permit.cta_group::2.sync.aligned;\n"),
      true));
  EXPECT_TRUE(accepts_allocation(allocation_source(
      "  tcgen05.alloc.cta_group::1.sync.aligned.shared::cta.b32 [slot], "
      "%signed_count;\n"
      "  tcgen05.dealloc.cta_group::1.sync.aligned.b32 %t, "
      "%unsigned_count;\n")));
  for (std::string_view carrier : {"%t", "%unsigned_count", "%signed_count"}) {
    SCOPED_TRACE(carrier);
    const std::string instruction =
        "  tcgen05.alloc.cta_group::1.sync.aligned.b32 [slot], " +
        std::string(carrier) + ";\n" +
        "  tcgen05.dealloc.cta_group::1.sync.aligned.b32 " +
        std::string(carrier) + ", " + std::string(carrier) + ";\n";
    EXPECT_TRUE(accepts_allocation(allocation_source(instruction), true));
  }
}

/** Standalone allocation carriers retain deferred declaration checking. */
TEST(TcgenAllocation, AcceptsStandaloneUnknownScalarRegisters) {
  const auto target = base::find_target_profile("sm_110a");
  ASSERT_TRUE(target.has_value());
  for (std::string_view source : {
           "tcgen05.alloc.cta_group::1.sync.aligned.b32 [%rd0], %r0;",
           "tcgen05.dealloc.cta_group::1.sync.aligned.b32 %r0, 32;",
           "tcgen05.dealloc.cta_group::1.sync.aligned.b32 0, %r0;",
       }) {
    SCOPED_TRACE(source);
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseInstruction();
    ASSERT_TRUE(ast.has_value());
    auto resolved = resolveInstruction(*ast);
    ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
    const checker::Context context{
        .target = {.ptx_version = {9, 3},
                   .sm_version = target->identity.architecture.number,
                   .enabled_family_features = target->enabled_family_features,
                   .identity = target->identity,
                   .capabilities = target->capabilities},
        .instruction_range = ast->range,
    };
    const auto result = (*resolved)->check(context);
    EXPECT_TRUE(result.has_value())
        << (result ? "" : result.error().front().message);
  }
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
           "tcgen05.dealloc.cta_group::1.sync.aligned.b32 %t, %wide;",
           "tcgen05.dealloc.cta_group::1.sync.aligned.b32 %t, %float_count;",
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
  auto& alloc = *owned->functions.front().body[0].get();
  auto& selected = dynamic_cast<Tcgen05AllocSharedCta&>(alloc);
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
  auto& dealloc = *owned->functions.front().body[1].get();
  auto& dealloc_form = dynamic_cast<Tcgen05Dealloc&>(dealloc);
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

/** Bound allocation scalars require cached types after syntax is released. */
TEST(TcgenAllocation, RejectsOwnedScalarMetadataTampering) {
  auto ast = parse_allocation_module(allocation_source(
      "  tcgen05.alloc.cta_group::1.sync.aligned.shared::cta.b32 "
      "[slot], %n;\n"
      "  tcgen05.dealloc.cta_group::1.sync.aligned.b32 %t, %n;\n"));
  ASSERT_TRUE(ast);
  auto resolved = resolveAndValidateModule(*ast);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  std::optional<ResolvedModule> owned = std::move(*resolved);
  ast.reset();
  auto& alloc =
      dynamic_cast<Tcgen05AllocSharedCta&>(*owned->functions.front().body[0]);
  auto& dealloc =
      dynamic_cast<Tcgen05Dealloc&>(*owned->functions.front().body[1]);
  auto& alloc_count = std::get<ResolvedRegisterRef>(alloc.ncols.value);
  auto& dealloc_address =
      std::get<ResolvedRegisterRef>(dealloc.taddr.value.value);
  auto& dealloc_count = std::get<ResolvedRegisterRef>(dealloc.ncols.value);
  const auto target = base::find_target_profile("sm_100a");
  ASSERT_TRUE(target.has_value());
  const checker::Context alloc_context{
      .target = {.ptx_version = {8, 6},
                 .sm_version = target->identity.architecture.number,
                 .enabled_family_features = target->enabled_family_features,
                 .identity = target->identity,
                 .capabilities = target->capabilities},
      .instruction_range = owned->functions.front().instruction_ranges[0],
  };
  checker::Context dealloc_context = alloc_context;
  dealloc_context.instruction_range =
      owned->functions.front().instruction_ranges[1];
  for (ResolvedRegisterRef* carrier :
       {&alloc_count, &dealloc_address, &dealloc_count}) {
    ASSERT_TRUE(carrier->symbol_id);
    const auto saved_type = carrier->declared_type;
    const Instruction* instruction =
        carrier == &alloc_count ? static_cast<const Instruction*>(&alloc)
                                : static_cast<const Instruction*>(&dealloc);
    const checker::Context& context =
        carrier == &alloc_count ? alloc_context : dealloc_context;
    carrier->declared_type.reset();
    EXPECT_FALSE(instruction->check(context).has_value());
    EXPECT_FALSE(validateModule(*owned).has_value());
    carrier->declared_type = ScalarType::F32;
    EXPECT_FALSE(instruction->check(context).has_value());
    EXPECT_FALSE(validateModule(*owned).has_value());
    carrier->declared_type = saved_type;
  }
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
  auto& operation = *owned->functions.front().body[0].get();
  auto& dealloc = dynamic_cast<Tcgen05Dealloc&>(operation);
  auto& address = std::get<ResolvedImmediate>(dealloc.taddr.value.value);
  address.bits = 1;
  EXPECT_FALSE(validateModule(*owned).has_value());
  address.bits = 0;
  address.integer_source_bits = 1;
  EXPECT_FALSE(validateModule(*owned).has_value());
  address.integer_source_bits = 0;
  EXPECT_TRUE(validateModule(*owned).has_value());
}

/** Both allocation spellings recheck a bound pointer after AST destruction. */
TEST(TcgenAllocation, RejectsOwnedResultSlotRegisterTampering) {
  for (const auto& [qualifier, pointer] :
       {std::pair{"", "%wide"}, std::pair{".shared::cta", "%t"}}) {
    SCOPED_TRACE(qualifier);
    std::optional<ResolvedModule> owned;
    {
      const std::string instruction =
          "  tcgen05.alloc.cta_group::1.sync.aligned" + std::string(qualifier) +
          ".b32 [" + pointer + "], 32;\n";
      auto ast = parse_allocation_module(allocation_source(instruction));
      ASSERT_TRUE(ast);
      auto resolved = resolveAndValidateModule(*ast);
      ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
      owned = std::move(*resolved);
    }
    ASSERT_TRUE(owned);
    auto& allocation = *owned->functions.front().body[0].get();
    auto& slot = qualifier[0] == '\0'
                     ? dynamic_cast<Tcgen05AllocGeneric&>(allocation).dst
                     : dynamic_cast<Tcgen05AllocSharedCta&>(allocation).dst;
    auto& register_ref = std::get<ResolvedRegisterRef>(slot.value.base);
    ASSERT_TRUE(register_ref.symbol_id.has_value());
    ASSERT_TRUE(register_ref.declared_type.has_value());
    const auto saved_type = register_ref.declared_type;
    const auto saved_id = register_ref.symbol_id;
    const auto target = base::find_target_profile("sm_100a");
    ASSERT_TRUE(target.has_value());
    const checker::Context context{
        .target = {.ptx_version = {8, 6},
                   .sm_version = target->identity.architecture.number,
                   .enabled_family_features = target->enabled_family_features,
                   .identity = target->identity,
                   .capabilities = target->capabilities},
        .instruction_range = owned->functions.front().instruction_ranges[0],
    };
    const auto expect_rejected = [&]() {
      const auto direct = allocation.check(context);
      ASSERT_FALSE(direct.has_value());
      EXPECT_EQ(direct.error().front().kind,
                checker::CheckDiagnosticKind::OperandTypeMismatch);
      EXPECT_FALSE(validateModule(*owned).has_value());
    };
    EXPECT_TRUE(allocation.check(context).has_value());
    EXPECT_TRUE(validateModule(*owned).has_value());

    register_ref.vector_width = 2;
    expect_rejected();
    register_ref.vector_width.reset();
    register_ref.register_class = ResolvedRegisterClass::Predicate;
    expect_rejected();
    register_ref.register_class = ResolvedRegisterClass::General;
    for (const auto invalid_type :
         {base::ScalarType::F32, base::ScalarType::B16}) {
      register_ref.declared_type = invalid_type;
      expect_rejected();
    }
    register_ref.declared_type.reset();
    expect_rejected();
    register_ref.symbol_id.reset();
    EXPECT_TRUE(allocation.check(context).has_value());
    register_ref.symbol_id = saved_id;
    register_ref.declared_type = saved_type;
    EXPECT_TRUE(allocation.check(context).has_value());
    EXPECT_TRUE(validateModule(*owned).has_value());
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
