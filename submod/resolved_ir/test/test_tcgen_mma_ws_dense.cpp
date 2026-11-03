#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_tcgen_descriptors.hpp>
#include <ptx_frontend/resolved_ir/tcgen_mma_operations.gen.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Supply declared addresses and descriptors for one independent WS source. */
std::string ws_source(std::string_view body, std::string_view version = "9.3",
                      std::string_view target = "sm_100a") {
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) + R"ptx(
.address_size 64
.visible .entry kernel() {
  .reg .b32 %d, %a, %i;
  .reg .b64 %ad, %bd, %z;
  .reg .pred %p;
)ptx" + std::string(body) +
         "\n  ret;\n}\n";
}

/** Parse a tree whose lifetime can end before owned validation. */
std::optional<syntax_ast::AstModule> parse_ws(
    std::string_view body, std::string_view version = "9.3",
    std::string_view target = "sm_100a") {
  const std::string source = ws_source(body, version, target);
  PtxSyntaxParser parser(source);
  auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    return std::nullopt;
  return std::move(*ast);
}

/** Check a complete WS module, including source and owned bindings. */
bool accepts_ws(std::string_view body, std::string_view version = "9.3",
                std::string_view target = "sm_100a") {
  auto ast = parse_ws(body, version, target);
  return ast && resolveAndValidateModule(*ast).has_value();
}

/** Find one typed violation or missing obligation. */
template <typename T>
  requires std::is_enum_v<T>
bool has(const std::vector<T>& values, T wanted) {
  return std::find(values.begin(), values.end(), wanted) != values.end();
}

/** Each WS kind owns four A/zero layouts and sixteen B-control spellings. */
TEST(TcgenMmaWsDense, CanonicalSourceAndExclusions) {
  for (std::string_view kind : {"f16", "tf32", "f8f6f4", "i8"}) {
    const std::string prefix =
        "tcgen05.mma.ws.cta_group::1.kind::" + std::string(kind);
    for (bool shared_a : {false, true}) {
      for (bool zero : {false, true}) {
        const std::string args =
            " [%d], " + std::string(shared_a ? "%ad" : "[%a]") +
            ", %bd, %i, %p" + std::string(zero ? ", %z;" : ";");
        SCOPED_TRACE(prefix + args);
        EXPECT_TRUE(accepts_ws(prefix + args));
        for (unsigned buffer = 0; buffer < 4; ++buffer)
          for (std::string_view op : {"fill", "use", "lastuse", "discard"})
            EXPECT_TRUE(accepts_ws(prefix + ".collector::b" +
                                   std::to_string(buffer) +
                                   "::" + std::string(op) + args));
      }
    }
    EXPECT_FALSE(accepts_ws("tcgen05.mma.ws.cta_group::2.kind::" +
                            std::string(kind) + " [%d], [%a], %bd, %i, %p;"));
    for (std::string_view suffix :
         {".collector::a::fill", ".ashift", ".block_scale", ".mask"})
      EXPECT_FALSE(accepts_ws(prefix + std::string(suffix) +
                              " [%d], [%a], %bd, %i, %p;"));
    EXPECT_FALSE(accepts_ws(prefix + " [%d], [%a], %bd, %i, %p, 0;"));
    EXPECT_FALSE(accepts_ws(prefix + " [%d], [%a], %bd, %i, %p, [%a];"));
    EXPECT_FALSE(accepts_ws(prefix + " [%d], [%a], %bd, %i, %p, %a;"));
    EXPECT_FALSE(
        accepts_ws(prefix + " [%d], [%a], %bd, %i, %p;", "9.3", "sm_90a"));
    EXPECT_FALSE(
        accepts_ws(prefix + " [%d], [%a], %bd, %i, %p;", "8.5", "sm_100a"));
    EXPECT_TRUE(
        accepts_ws(prefix + " [%d], [%a], %bd, %i, %p;", "9.3", "sm_110a"));
  }
}

/** A borrowed WS view and its bindings remain valid after AST destruction. */
TEST(TcgenMmaWsDense, OwnedViewAndTamper) {
  std::optional<ResolvedModule> owned;
  {
    auto ast = parse_ws(
        "tcgen05.mma.ws.cta_group::1.kind::f16"
        ".collector::b2::lastuse [%d], [%a], %bd, %i, %p, %z;");
    ASSERT_TRUE(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result.has_value());
    owned.emplace(std::move(*result));
  }
  ASSERT_TRUE(validateModule(*owned).has_value());
  auto* form = dynamic_cast<Tcgen05MmaWsF16*>(
      owned->functions.front().body.front().get());
  ASSERT_TRUE(form);
  const auto view = tcgen_mma_ws_view(*form);
  ASSERT_TRUE(view);
  EXPECT_FALSE(view->a_shared.has_value());
  EXPECT_NE(view->a_tmem, nullptr);
  ASSERT_TRUE(view->zero_column.has_value());
  EXPECT_NE(view->zero_column->source, nullptr);
  EXPECT_EQ(view->collector->value.buffer, TcgenCollectorBuffer::B2);
  EXPECT_EQ(view->collector->value.operation, TcgenCollectorOp::LastUse);
  EXPECT_EQ(view->collector->locs.size(), 1);
  const auto collector = form->collector.value;
  form->collector.value.buffer = TcgenCollectorBuffer::A;
  EXPECT_FALSE(validateModule(*owned).has_value());
  form->collector.value = collector;
  form->collector.value.operation = static_cast<TcgenCollectorOp>(255);
  EXPECT_FALSE(validateModule(*owned).has_value());
  form->collector.value = collector;
  const auto locs = form->collector.locs;
  form->collector.locs.clear();
  EXPECT_FALSE(validateModule(*owned).has_value());
  form->collector.locs = locs;
  auto& zero = form->zero_column_desc->value;
  const auto zero_type = zero.declared_type;
  zero.declared_type = ScalarType::B32;
  EXPECT_FALSE(validateModule(*owned).has_value());
  zero.declared_type = zero_type;
  const auto zero_locs = form->zero_column_desc->locs;
  form->zero_column_desc->locs.clear();
  EXPECT_FALSE(validateModule(*owned).has_value());
  form->zero_column_desc->locs = zero_locs;
  form->zero_column_desc.reset();
  EXPECT_FALSE(validateModule(*owned).has_value());
}

/** Omission keeps an empty source control while B0/discard is derived. */
TEST(TcgenMmaWsDense, OmittedAndWrittenDiscard) {
  auto ast =
      parse_ws("tcgen05.mma.ws.cta_group::1.kind::i8 [%d], %ad, %bd, %i, %p;");
  ASSERT_TRUE(ast);
  auto owned = resolveAndValidateModule(*ast);
  ASSERT_TRUE(owned.has_value());
  auto* form = dynamic_cast<Tcgen05MmaWsI8*>(
      owned->functions.front().body.front().get());
  ASSERT_TRUE(form);
  EXPECT_EQ(form->collector.value.buffer, TcgenCollectorBuffer::Unspecified);
  EXPECT_TRUE(form->collector.locs.empty());
  form->collector.locs.push_back(form->cta_group.locs.front());
  EXPECT_FALSE(validateModule(*owned).has_value());
  EXPECT_TRUE(
      accepts_ws("tcgen05.mma.ws.cta_group::1.kind::i8"
                 ".collector::b0::discard [%d], %ad, %bd, %i, %p;"));
}

/** Caller-known B history is per buffer; unknown history remains an obligation. */
TEST(TcgenMmaWsDense, KnownFactsAndTable48) {
  TcgenWsKnownFacts facts{
      .source_kind = TcgenMmaKind::F16,
      .group = TcgenCtaGroup::One,
      .a_in_tmem = true,
      .instruction =
          TcgenInstructionWord{(2U << 24) | (8U << 17), TcgenMmaKind::F16},
      .a_lane_half = 0,
      .d_lane_half = 0,
      .collector = {TcgenCollectorBuffer::B2, TcgenCollectorOp::Use},
  };
  auto report = check_tcgen_ws_known_operation(facts);
  EXPECT_TRUE(report.supplied_facts_ok());
  EXPECT_TRUE(has(report.missing, TcgenWsObligation::CollectorHistory));
  EXPECT_TRUE(has(report.missing, TcgenWsObligation::CollectorSequence));
  facts.collector_b_valid[0] = true;
  facts.collector_b_valid[2] = false;
  EXPECT_TRUE(has(check_tcgen_ws_known_operation(facts).violations,
                  TcgenWsViolation::CollectorHistory));
  facts.collector_b_valid[2] = true;
  EXPECT_TRUE(check_tcgen_ws_known_operation(facts).supplied_facts_ok());
  facts.d_lane_half = 16;
  EXPECT_TRUE(has(check_tcgen_ws_known_operation(facts).violations,
                  TcgenWsViolation::DLaneHalf));
  facts.d_lane_half = 0;
  facts.zero_column_operand_present = true;
  EXPECT_TRUE(has(check_tcgen_ws_known_operation(facts).missing,
                  TcgenWsObligation::ZeroColumnWord));
  facts.zero_column_word = TcgenZeroColumnWord{0};
  report = check_tcgen_ws_known_operation(facts);
  EXPECT_TRUE(report.supplied_facts_ok());
  EXPECT_TRUE(has(report.missing, TcgenWsObligation::LiveZeroColumnWord));
  facts.collector = {};
  report = check_tcgen_ws_known_operation(facts);
  EXPECT_EQ(report.effective_collector.buffer, TcgenCollectorBuffer::B0);
  EXPECT_EQ(report.effective_collector.operation, TcgenCollectorOp::Discard);
  EXPECT_NE(report.effective_collector, facts.collector);
  const auto sm100a = base::find_target_profile("sm_100a");
  const auto sm90a = base::find_target_profile("sm_90a");
  ASSERT_TRUE(sm100a);
  ASSERT_TRUE(sm90a);
  facts.target = sm100a->identity;
  facts.ptx_version = checker::PtxVersion{8, 6};
  EXPECT_FALSE(has(check_tcgen_ws_known_operation(facts).violations,
                   TcgenWsViolation::Target));
  facts.target = sm90a->identity;
  EXPECT_TRUE(has(check_tcgen_ws_known_operation(facts).violations,
                  TcgenWsViolation::Target));
}

/** A known shared A word is meaningful only for the shared-A source layout. */
TEST(TcgenMmaWsDense, APlacementFacts) {
  TcgenWsKnownFacts facts{
      .source_kind = TcgenMmaKind::F16,
      .group = TcgenCtaGroup::One,
      .a_in_tmem = true,
      .instruction =
          TcgenInstructionWord{(2U << 24) | (8U << 17), TcgenMmaKind::F16},
      .a_lane_half = 0,
      .d_lane_half = 0,
  };
  EXPECT_TRUE(check_tcgen_ws_known_operation(facts).supplied_facts_ok());
  facts.a_shared_word = TcgenSharedWord{1ULL << 46};
  auto report = check_tcgen_ws_known_operation(facts);
  EXPECT_FALSE(report.supplied_facts_ok());
  EXPECT_TRUE(has(report.violations, TcgenWsViolation::APlacementFacts));
  EXPECT_FALSE(report.a_shared_fields.has_value());

  facts.a_in_tmem = false;
  facts.a_context.major = TcgenMajor::K;
  report = check_tcgen_ws_known_operation(facts);
  EXPECT_TRUE(report.supplied_facts_ok());
  ASSERT_TRUE(report.a_shared_fields);
  EXPECT_TRUE(report.a_shared_fields->defined_fields_ok());
  facts.a_shared_word = TcgenSharedWord{~uint64_t{0}};
  report = check_tcgen_ws_known_operation(facts);
  EXPECT_TRUE(has(report.violations, TcgenWsViolation::ASharedFields));
  facts.a_in_tmem = true;
  report = check_tcgen_ws_known_operation(facts);
  EXPECT_TRUE(has(report.violations, TcgenWsViolation::APlacementFacts));
  EXPECT_FALSE(report.a_shared_fields.has_value());
}

/** Build real physical donor symbols and independent same-width bit-data controls. */
std::string descriptor_role_source(std::string_view statement) {
  return R"ptx(.version 9.3
.target sm_100a
.address_size 64
.entry descriptor_roles() {
.reg .b32 %d<2>, %b32;
.reg .u32 %u32;
.reg .s32 %s32;
.reg .f32 %f32;
.reg .b64 %b64;
.reg .u64 %u64;
.reg .s64 %s64;
.reg .f64 %f64;
.reg .v2 .b32 %v32;
.reg .v2 .b64 %v64;
.reg .pred %p;
mov.b32 %f32, 0;
mov.b64 %f64, 0;
mov.b32 %b32, %f32;
mov.b64 %b64, %f64;
mov.u32 %u32, 0;
mov.s32 %s32, 0;
mov.u64 %u64, 0;
mov.s64 %s64, 0;
add.rn.f64 %f64,%f64,%f64;
)ptx" + std::string(statement) +
         "\nret;\n}\n";
}

/** Parse owned source without retaining parser storage. */
std::optional<syntax_ast::AstModule> descriptor_role_ast(
    std::string_view source) {
  PtxSyntaxParser parser(source);
  auto parsed = parser.parseModule();
  if (!parsed || !parsed.diagnostics.empty())
    return std::nullopt;
  return std::move(*parsed);
}

/** Exact-width descriptor carriers differ from fundamental bit-data compatibility. */
TEST(TcgenMmaWsDense, DeclaredDescriptorCarriersAndCurrentRoles) {
  const auto profile = base::find_target_profile("sm_100a");
  ASSERT_TRUE(profile);
  const checker::Context context{
      .target = {.ptx_version = {9, 3},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
  {
    SCOPED_TRACE("TCGEN-WS-zero");
    const auto source_for = [](std::string_view carrier) {
      std::string statement =
          "tcgen05.mma.ws.cta_group::1.kind::f16 "
          "[%d0],[%d1],%b64,%b32,%p,CARRIER;";
      statement.replace(statement.find("CARRIER"), 7, carrier);
      return descriptor_role_source(statement);
    };
    for (std::string_view carrier : {"%b64", "%u64", "%s64"}) {
      const auto source = source_for(carrier);
      auto ast = descriptor_role_ast(source);
      ASSERT_TRUE(ast);
      auto result = resolveAndValidateModule(*ast);
      ASSERT_TRUE(result) << result.error().front().message;
      auto copy = *result;
      EXPECT_TRUE(
          validateModule(copy, ModuleValidationPolicy::RequireCompleteContext));
    }
    for (std::string_view carrier :
         {"%f64", "%b32", "%v64", "%p", "0", "[%b64]"}) {
      auto ast = descriptor_role_ast(source_for(carrier));
      ASSERT_TRUE(ast) << carrier;
      const auto lowered = resolveModule(*ast);
      ASSERT_FALSE(lowered) << carrier;
      EXPECT_FALSE(lowered.error().empty());
      EXPECT_FALSE(resolveAndValidateModule(*ast)) << carrier;
    }
    const auto good_source = source_for("%b64");
    std::optional<ResolvedModule> owned;
    {
      auto ast = descriptor_role_ast(good_source);
      ASSERT_TRUE(ast);
      auto result = resolveAndValidateModule(*ast);
      ASSERT_TRUE(result);
      owned.emplace(std::move(*result));
    }
    auto& function = owned->functions.front();
    auto* form = dynamic_cast<Tcgen05MmaWsF16*>(function.body[9].get());
    ASSERT_NE(form, nullptr);
    auto& role = form->zero_column_desc->value;
    const auto original = role;
    const auto donor = [&](ScalarType type) {
      for (const auto& item : function.body) {
        if (const auto* mov = dynamic_cast<const MovScalar*>(item.get());
            mov && mov->dst_register &&
            mov->dst_register->value.declared_type == type)
          return mov->dst_register->value;
      }
      ADD_FAILURE() << "Missing real declared donor";
      return original;
    };
    const auto reject_role = [&]() {
      const auto direct = form->check(context);
      ASSERT_FALSE(direct);
      EXPECT_TRUE(
          std::ranges::any_of(direct.error(), [](const auto& diagnostic) {
            return diagnostic.kind ==
                   checker::CheckDiagnosticKind::OperandTypeMismatch;
          }));
      const auto current = validateModule(
          *owned, ModuleValidationPolicy::RequireCompleteContext);
      ASSERT_FALSE(current);
      EXPECT_TRUE(
          std::ranges::any_of(current.error(), [](const auto& diagnostic) {
            return diagnostic.kind ==
                   checker::CheckDiagnosticKind::OperandTypeMismatch;
          }));
      auto good_ast = descriptor_role_ast(good_source);
      ASSERT_TRUE(good_ast);
      EXPECT_FALSE(validateModule(*good_ast, *owned));
    };
    for (auto type : {ScalarType::F64, ScalarType::B32}) {
      role = donor(type);
      reject_role();
      auto bad_copy = *owned;
      EXPECT_FALSE(validateModule(bad_copy));
      auto bad_function = function;
      EXPECT_FALSE(bad_function.body[9]->check(context));
      auto bad_clone = form->clone();
      EXPECT_FALSE(bad_clone->check(context));
      role = original;
      EXPECT_TRUE(validateModule(*owned));
    }
    role.declared_type.reset();
    reject_role();
    role = original;
    role.declared_type = ScalarType::F64;
    const auto forged_cache = validateModule(*owned);
    ASSERT_FALSE(forged_cache);
    EXPECT_TRUE(
        std::ranges::any_of(forged_cache.error(), [](const auto& diagnostic) {
          return diagnostic.kind ==
                 checker::CheckDiagnosticKind::ModuleSourceMismatch;
        }));
    role = donor(ScalarType::F64);
    role.declared_type = ScalarType::B64;
    EXPECT_FALSE(validateModule(*owned));
    role.declared_type.reset();
    EXPECT_FALSE(validateModule(*owned));
    role = original;
    role.symbol_id.reset();
    role.declared_type.reset();
    EXPECT_TRUE(form->check(context));
    EXPECT_FALSE(validateModule(*owned));
    role = original;
    for (auto type : {ScalarType::U64, ScalarType::S64}) {
      role = donor(type);
      EXPECT_TRUE(form->check(context));
      EXPECT_TRUE(validateModule(*owned));
    }
    role = original;
    auto copy = *owned;
    ResolvedModule assigned;
    assigned = copy;
    auto moved = std::move(assigned);
    EXPECT_TRUE(validateModule(moved));
    auto function_copy = function;
    function_copy = function;
    auto instruction_copy = form->clone();
    EXPECT_TRUE(instruction_copy->check(context));
    auto* copied_form =
        dynamic_cast<Tcgen05MmaWsF16*>(function_copy.body[9].get());
    ASSERT_TRUE(copied_form);
    const auto copied_view = tcgen_mma_ws_view(*copied_form);
    ASSERT_TRUE(copied_view && copied_view->zero_column);
    EXPECT_EQ(copied_view->zero_column->source,
              &copied_form->zero_column_desc->value);
    EXPECT_NE(copied_view->zero_column->source, &form->zero_column_desc->value);
    auto good_ast = descriptor_role_ast(good_source);
    ASSERT_TRUE(good_ast);
    EXPECT_TRUE(validateModule(*good_ast, *owned));
    auto changed_ast = descriptor_role_ast(source_for("%u64"));
    ASSERT_TRUE(changed_ast);
    const auto changed_result = validateModule(*changed_ast, *owned);
    ASSERT_FALSE(changed_result);
    EXPECT_TRUE(
        std::ranges::any_of(changed_result.error(), [](const auto& diagnostic) {
          return diagnostic.kind ==
                 checker::CheckDiagnosticKind::ModuleSourceMismatch;
        }));
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
