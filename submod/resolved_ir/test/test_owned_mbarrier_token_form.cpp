#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "../src/ptx_resolved_ir_private.hpp"
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Exact B64 carriers and aligned shared storage isolate token presence policy. */
std::string token_form_source(std::string_view cluster = "_",
                              std::string_view wait = "q",
                              std::string_view pending = "q") {
  return ".version 9.3\n.target sm_90\n.address_size 64\n"
         ".shared .align 8 .b64 barrier;\n.entry kernel() {\n"
         ".reg .pred p; .reg .u32 c; .reg .b64 q;\n"
         "mbarrier.arrive.shared::cta.b64 q, [barrier];\n"
         "mbarrier.arrive.shared::cta.b64 _, [barrier];\n"
         "mbarrier.arrive.shared::cluster.b64 " +
         std::string(cluster) +
         ", [barrier];\n"
         "mbarrier.test_wait.b64 p, [barrier], " +
         std::string(wait) + ";\nmbarrier.pending_count.b64 c, " +
         std::string(pending) + ";\n}";
}
/** Borrow a token only while its instruction remains alive. */
struct TokenFormCapture final : detail::IReferenceObserver {
  /** Actual optional-register token payload; no synthetic binding is introduced. */
  const ResolvedMbarrierStateToken* token = nullptr;
  /** Exact token operand ranges borrowed from the same instruction. */
  std::span<const SourceRange> locations;
  /** Observe the existing token payload without retaining syntax storage. */
  void mbarrier_state_token(const ResolvedMbarrierStateToken& value,
                            std::span<const SourceRange> ranges,
                            checker::AddressSymbolResolutionPolicy) override {
    token = &value;
    locations = ranges;
  }
};
/** Complete modern target facts retain the cluster requirement for direct checks. */
checker::Context token_form_context() {
  static constexpr std::array<std::string_view, 1> capabilities{"cluster"};
  return {.target = {.ptx_version = {9, 3},
                     .sm_version = 90,
                     .capabilities = capabilities}};
}
/** Destroy the legal original AST/source before returning the owned baseline. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> owned_token_form() {
  const auto ast = test_helpers::parseModule(token_form_source());
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveAndValidateModule(*ast);
}
/** Use only a real bound General B64 scalar from the same module. */
ResolvedRegisterRef real_token_register(const ResolvedModule& module) {
  TokenFormCapture capture;
  module.functions.front().body.front()->visit_references(capture);
  EXPECT_TRUE(capture.token && capture.token->register_ref);
  return *capture.token->register_ref;
}

TEST(OwnedMbarrierTokenForm, SourceFormsAndCtaEitherControls) {
  const auto ast = test_helpers::parseModule(token_form_source());
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  const auto baseline = resolveAndValidateModule(*ast);
  ASSERT_TRUE(baseline) << baseline.error().front().message;
  EXPECT_TRUE(validateModule(*ast, *baseline));
  auto owned = owned_token_form();
  ASSERT_TRUE(owned);
  const auto context = token_form_context();
  const auto real = real_token_register(*owned);
  ASSERT_TRUE(real.symbol_id);
  EXPECT_EQ(real.register_class, ResolvedRegisterClass::General);
  EXPECT_EQ(real.declared_type, ScalarType::B64);
  EXPECT_FALSE(real.vector_width);
  EXPECT_FALSE(real.component);
  for (size_t index = 0; index < owned->functions.front().body.size();
       ++index) {
    TokenFormCapture capture;
    owned->functions.front().body[index]->visit_references(capture);
    ASSERT_TRUE(capture.token);
    ASSERT_EQ(capture.locations.size(), 1u);
    EXPECT_GT(capture.locations.front().start.line, 0);
    EXPECT_GT(capture.locations.front().start.column, 0);
    EXPECT_GT(capture.locations.front().end.column,
              capture.locations.front().start.column);
    EXPECT_TRUE(owned->functions.front().body[index]->check(context));
  }
  for (size_t index : {0u, 1u}) {
    auto changed = *owned;
    TokenFormCapture capture;
    changed.functions.front().body[index]->visit_references(capture);
    auto& token = const_cast<ResolvedMbarrierStateToken&>(*capture.token);
    token.register_ref = index == 0 ? std::nullopt : std::optional{real};
    EXPECT_TRUE(changed.functions.front().body[index]->check(context));
    EXPECT_TRUE(validateModule(changed));
    auto copied = changed;
    ResolvedModule assigned;
    assigned = copied;
    assigned.functions.front().body[index] =
        copied.functions.front().body[index]->clone();
    EXPECT_TRUE(validateModule(copied));
    EXPECT_TRUE(validateModule(assigned));
  }
  for (int form = 0; form < 3; ++form) {
    const auto invalid = test_helpers::parseModule(token_form_source(
        form == 0 ? "q" : "_", form == 1 ? "_" : "q", form == 2 ? "_" : "q"));
    ASSERT_MODULE_PARSE_SUCCEEDS(invalid);
    const auto result = resolveAndValidateModule(*invalid);
    ASSERT_FALSE(result);
    ASSERT_FALSE(result.error().empty());
    EXPECT_GT(result.error().front().range.start.line, 0);
    EXPECT_GT(result.error().front().range.start.column, 0);
  }
}

/** Require one precise form-policy diagnostic at the original token location. */
void expect_token_form_diagnostic(const checker::CheckResult& result,
                                  SourceRange range) {
  ASSERT_FALSE(result);
  ASSERT_EQ(result.error().size(), 1u);
  EXPECT_EQ(result.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedOperandShape);
  EXPECT_EQ(result.error().front().range, range);
  EXPECT_NE(result.error().front().message.find("Mbarrier state token"),
            std::string::npos);
}

/** Require wrong forms to reject across independent owned value-copy routes. */
void expect_token_form_rejected(size_t index) {
  auto owned = owned_token_form();
  ASSERT_TRUE(owned);
  const auto real = real_token_register(*owned);
  TokenFormCapture capture;
  owned->functions.front().body[index]->visit_references(capture);
  ASSERT_TRUE(capture.token);
  ASSERT_EQ(capture.locations.size(), 1u);
  const auto range = capture.locations.front();
  ASSERT_GT(range.start.line, 0);
  ASSERT_GT(range.start.column, 0);
  ASSERT_GT(range.end.column, range.start.column);
  auto& token = const_cast<ResolvedMbarrierStateToken&>(*capture.token);
  const auto valid = token;
  token.register_ref = index == 2 ? std::optional{real} : std::nullopt;
  const auto context = token_form_context();
  expect_token_form_diagnostic(
      owned->functions.front().body[index]->check(context), range);
  expect_token_form_diagnostic(validateModule(*owned), range);
  ResolvedModule copied(*owned), assigned;
  assigned = copied;
  expect_token_form_diagnostic(validateModule(copied), range);
  expect_token_form_diagnostic(validateModule(assigned), range);
  ResolvedFunction function(copied.functions.front()), function_assigned;
  function_assigned = function;
  expect_token_form_diagnostic(function.body[index]->check(context), range);
  expect_token_form_diagnostic(function_assigned.body[index]->check(context),
                               range);
  assigned.functions.front().body[index] = function.body[index]->clone();
  expect_token_form_diagnostic(validateModule(assigned), range);
  token = valid;
  EXPECT_TRUE(owned->functions.front().body[index]->check(context));
  EXPECT_TRUE(validateModule(*owned));
}
TEST(OwnedMbarrierTokenForm, RejectsRegisterInClusterSinkToken) {
  expect_token_form_rejected(2);
}
TEST(OwnedMbarrierTokenForm, RejectsSinkInNonparityWaitToken) {
  expect_token_form_rejected(3);
}
TEST(OwnedMbarrierTokenForm, RejectsSinkInPendingCountToken) {
  expect_token_form_rejected(4);
}
TEST(OwnedMbarrierTokenForm,
     ValidatesExistingMirrorsAndOptionalPolicyBoundary) {
  const auto owned = owned_token_form();
  ASSERT_TRUE(owned);
  const auto real = real_token_register(*owned);
  TokenFormCapture capture;
  owned->functions.front().body.front()->visit_references(capture);
  ASSERT_EQ(capture.locations.size(), 1u);
  checker::OperandDescriptor descriptor{
      .target_field_id = "state",
      .type_expression = {.kind =
                              checker::OperandTypeExpressionKind::FixedScalar,
                          .fixed_scalar_type = ScalarType::B64},
      .role = checker::OperandRole::Destination,
      .access = checker::OperandAccess::Write,
      .allowed_shapes = checker::OperandShape::Register,
      .mbarrier_state_token_form =
          checker::MbarrierStateTokenForm::RegisterOrSink};
  checker::OperandView view{.field_id = "state",
                            .actual_shape = checker::OperandShape::Register,
                            .register_type = real.declared_type,
                            .register_ref = &real,
                            .locations = capture.locations};
  const auto context = token_form_context();
  const auto check = [&] {
    return checker::check_operands(std::span{&descriptor, 1}, {},
                                   std::span{&view, 1}, {}, context);
  };
  EXPECT_TRUE(check());
  const auto valid = view;
  for (int mutation = 0; mutation < 8; ++mutation) {
    SCOPED_TRACE(mutation);
    view = valid;
    switch (mutation) {
      case 0:
        view.actual_shape = checker::OperandShape::Address;
        break;
      case 1:
        view.is_sink = true;
        break;
      case 2:
        view.register_ref = nullptr;
        break;
      case 3:
        view.register_type = ScalarType::U64;
        break;
      case 4:
        descriptor.mbarrier_state_token_form =
            static_cast<checker::MbarrierStateTokenForm>(99);
        break;
      case 5:
        descriptor.role = checker::OperandRole::Source;
        break;
      case 6:
        descriptor.access = checker::OperandAccess::Read;
        break;
      case 7:
        view.register_ref = nullptr;
        view.is_sink = true;
        break;
    }
    const auto invalid = check();
    ASSERT_FALSE(invalid);
    ASSERT_EQ(invalid.error().size(), 1u);
    EXPECT_EQ(invalid.error().front().range, capture.locations.front());
    EXPECT_NE(invalid.error().front().message.find("Mbarrier state token"),
              std::string::npos);
    descriptor.role = checker::OperandRole::Destination;
    descriptor.access = checker::OperandAccess::Write;
    descriptor.mbarrier_state_token_form =
        checker::MbarrierStateTokenForm::RegisterOrSink;
  }
  view = valid;
  view.register_ref = nullptr;
  view.register_type.reset();
  view.is_sink = true;
  EXPECT_TRUE(check());
  descriptor.mbarrier_state_token_form =
      checker::MbarrierStateTokenForm::Register;
  EXPECT_FALSE(check());
  descriptor.mbarrier_state_token_form = checker::MbarrierStateTokenForm::Sink;
  EXPECT_TRUE(check());
  view = valid;
  EXPECT_FALSE(check());
  descriptor.mbarrier_state_token_form =
      checker::MbarrierStateTokenForm::Register;
  descriptor.role = checker::OperandRole::Source;
  descriptor.access = checker::OperandAccess::Read;
  auto unknown = real;
  unknown.declared_type.reset();
  view.register_ref = &unknown;
  view.register_type.reset();
  EXPECT_TRUE(check());
  descriptor.mbarrier_state_token_form.reset();
  descriptor.role = checker::OperandRole::Destination;
  descriptor.access = checker::OperandAccess::Write;
  descriptor.allow_destination_sink = true;
  view.register_ref = nullptr;
  view.is_sink = true;
  EXPECT_TRUE(check());
  descriptor.allowed_shapes = checker::OperandShape::Predicate;
  descriptor.type_expression.fixed_scalar_type = ScalarType::Pred;
  descriptor.allow_predicate_sink = true;
  view.actual_shape = checker::OperandShape::Predicate;
  EXPECT_TRUE(check());
}

TEST(OwnedMbarrierTokenForm, ResolverRequiresAnEngagedKnownPolicy) {
  const auto ast =
      test_helpers::parseInstruction("mbarrier.pending_count.b64 c,q;");
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  const check_end::ResolvedFieldDescriptor field{
      .field_id = "state",
      .value_kind = check_end::ResolvedValueKind::MbarrierStateToken};
  check_end::ResolvedOperandBindingDescriptor binding{.target_field_id =
                                                          "state"};
  for (const auto form :
       {std::optional<checker::MbarrierStateTokenForm>{},
        std::optional{static_cast<checker::MbarrierStateTokenForm>(99)}}) {
    binding.mbarrier_state_token_form = form;
    const auto result = detail::resolve_operand_value(
        field, binding, ast->operands.back(), {}, nullptr);
    ASSERT_FALSE(result);
    EXPECT_GT(result.error().range.start.line, 0);
    EXPECT_GT(result.error().range.start.column, 0);
    EXPECT_NE(result.error().message.find("valid form policy"),
              std::string::npos);
  }
}

TEST(OwnedMbarrierTokenForm, CoversArrivalAndWaitRoleTopologies) {
  for (const auto operation :
       {"mbarrier.arrive.b64 q,[barrier],1;",
        "mbarrier.arrive_drop.b64 q,[barrier];",
        "mbarrier.arrive_drop.shared::cta.b64 _,[barrier],1;",
        "mbarrier.arrive.expect_tx.release.cta.shared.b64 _,[barrier],1;",
        "mbarrier.arrive.noComplete.release.cta.shared::cta.b64 q,[barrier],1;",
        "mbarrier.arrive_drop.noComplete.release.cta.shared::cta.b64 "
        "_,[barrier],1;",
        "mbarrier.arrive_drop.shared::cluster.b64 _,[barrier],1;",
        "mbarrier.test_wait.phase_type::primary.b64 p|p1,r,[barrier],q;",
        "mbarrier.try_wait.b64 p,[barrier],q;",
        "mbarrier.try_wait.b64 p,[barrier],q,8;",
        "mbarrier.try_wait.phase_type::primary.b64 p|p1,r,[barrier],q,8;"}) {
    SCOPED_TRACE(operation);
    auto source = token_form_source();
    source.insert(source.rfind('}'),
                  std::string(".reg .pred p1; .reg .b8 r; ") + operation);
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module) << module.error().front().message;
    EXPECT_TRUE(validateModule(*module));
    auto copy = *module;
    copy.functions.front().body.back() =
        module->functions.front().body.back()->clone();
    EXPECT_TRUE(validateModule(copy));
  }
}

TEST(OwnedMbarrierTokenForm, KeepsAvailabilityAndSourceAssociationIndependent) {
  for (const auto version : {"7.0", "7.1"}) {
    for (const auto token : {"q", "_"}) {
      const std::string source =
          std::string(".version ") + version +
          "\n.target sm_80\n.address_size 64\n.shared .align 8 .b64 barrier; "
          ".entry k(){.reg .b64 q; mbarrier.arrive.b64 " +
          token + ",[barrier];}";
      const auto ast = test_helpers::parseModule(source);
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      const auto result = resolveAndValidateModule(*ast);
      EXPECT_EQ(result.has_value(), std::string_view(token) == "q" ||
                                        std::string_view(version) == "7.1");
    }
  }
  for (const auto target : {"sm_80", "sm_90"}) {
    auto source = token_form_source();
    source.replace(source.find("9.3"), 3, "8.0");
    source.replace(source.find("sm_90"), 5, target);
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_EQ(resolveAndValidateModule(*ast).has_value(),
              std::string_view(target) == "sm_90");
  }
  const auto original = test_helpers::parseModule(token_form_source());
  ASSERT_MODULE_PARSE_SUCCEEDS(original);
  auto module = resolveAndValidateModule(*original);
  ASSERT_TRUE(module);
  TokenFormCapture capture;
  module->functions.front().body.front()->visit_references(capture);
  const_cast<ResolvedMbarrierStateToken&>(*capture.token).register_ref.reset();
  EXPECT_TRUE(validateModule(*original, *module));
  auto changed_source = token_form_source();
  const auto start = changed_source.find("mbarrier.arrive.shared::cta.b64 q");
  changed_source[start +
                 std::string_view("mbarrier.arrive.shared::cta.b64 ").size()] =
      '_';
  const auto changed = test_helpers::parseModule(changed_source);
  ASSERT_MODULE_PARSE_SUCCEEDS(changed);
  ASSERT_TRUE(resolveAndValidateModule(*changed));
  const auto mismatch = validateModule(*changed, *module);
  ASSERT_FALSE(mismatch);
  ASSERT_EQ(mismatch.error().size(), 1u);
  EXPECT_EQ(mismatch.error().front().kind,
            checker::CheckDiagnosticKind::ModuleSourceMismatch);
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
