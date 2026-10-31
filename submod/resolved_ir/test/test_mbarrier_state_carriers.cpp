#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/model/data_movement/mov.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Distinct semantic roles retain different compatible declaration domains. */
enum class CarrierRole { Opaque, UnsignedWait, RetainedReport };
/** A fixed legal topology with one substituted carrier operand. */
struct CarrierCase {
  /** Exactly one '$' marks the operand whose declared type is tested. */
  std::string_view instruction;
  /** Selects opaque64, unsigned64, or retained-model report8 compatibility. */
  CarrierRole role;
};
constexpr std::array carrier_cases{
    CarrierCase{"mbarrier.arrive.b64 $, [M];", CarrierRole::Opaque},
    CarrierCase{"mbarrier.test_wait.b64 p, [M], $;", CarrierRole::UnsignedWait},
    CarrierCase{"mbarrier.pending_count.b64 c, $;", CarrierRole::Opaque},
    CarrierCase{"mbarrier.test_wait.phase_type::primary.b64 p|p1, $, [M], q;",
                CarrierRole::RetainedReport},
};
/** Keep aligned storage, legal SM90 report form, and real candidate declarations. */
std::string carrier_source(const CarrierCase& item, std::string_view carrier,
                           std::string_view operand = {}) {
  const bool report = item.role == CarrierRole::RetainedReport;
  std::string instruction(item.instruction);
  instruction.replace(instruction.find('$'), 1,
                      operand.empty() ? (report ? "r" : "q") : operand);
  return ".version 9.3\n.target sm_90\n.address_size 64\n"
         ".shared .align 8 .b64 M;\n.entry kernel(){\n"
         ".reg .pred p,p1; .reg .u32 c; .reg ." +
         std::string(report ? "b64" : carrier) + " q; .reg ." +
         std::string(report ? carrier : "b8") +
         " r;\n"
         ".reg .b64 b; .reg .s64 s; .reg .u64 u; .reg .f64 f;\n"
         ".reg .b8 a; .reg .s8 i; .reg .u8 j; .reg .b32 x; .reg .b128 w;\n"
         ".reg .v2 .u64 V<2>; .reg .v2 .f64 F<2>;\n" +
         instruction + "\n}";
}
/** Provide complete cluster-capable direct-check context without binding guesses. */
checker::Context carrier_context() {
  static constexpr std::array<std::string_view, 1> capabilities{"cluster"};
  return {.target = {.ptx_version = {9, 3},
                     .sm_version = 90,
                     .capabilities = capabilities}};
}
/** Observe the role's actual retained declaration, not its instruction modifier. */
struct CarrierCapture final : detail::IReferenceObserver {
  /** Report names distinguish the ordinary result from the token input. */
  std::string_view report_name;
  /** Borrowed until the owning instruction is mutated or destroyed. */
  const ResolvedRegisterRef* value = nullptr;
  /** Original role-operand locations borrowed from that same instruction. */
  std::span<const SourceRange> locations;
  /** Retain a typed token's real scalar reference. */
  void mbarrier_state_token(const ResolvedMbarrierStateToken& token,
                            std::span<const SourceRange> ranges,
                            checker::AddressSymbolResolutionPolicy) override {
    if (report_name.empty() && token.register_ref) {
      value = &*token.register_ref;
      locations = ranges;
    }
  }
  /** Retain only the ordinary report register selected by this fixture. */
  void reg(const ResolvedRegisterRef& reg, std::span<const SourceRange> ranges,
           checker::AddressSymbolResolutionPolicy) override {
    if (!report_name.empty() && reg.spelling == report_name) {
      value = &reg;
      locations = ranges;
    }
  }
};
/** Return a resolution draft after its source text and AST have been destroyed. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> carrier_draft(
    const CarrierCase& item, std::string_view carrier,
    std::string_view operand = {}) {
  const auto ast =
      test_helpers::parseModule(carrier_source(item, carrier, operand));
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveModuleOnly(*ast);
}

TEST(MbarrierStateCarriers, AdmitsRoleSpecificDeclarationsBesideLegalControls) {
  const auto context = carrier_context();
  for (const auto& item : carrier_cases) {
    SCOPED_TRACE(item.instruction);
    const auto baseline =
        item.role == CarrierRole::RetainedReport ? "b8" : "b64";
    const auto control =
        test_helpers::parseModule(carrier_source(item, baseline));
    ASSERT_MODULE_PARSE_SUCCEEDS(control);
    const auto valid = resolveAndValidateModule(*control);
    ASSERT_TRUE(valid) << valid.error().front().message;
    // The report witness exercises the retained B8 model, whose reference type is qualified.
    const auto candidates =
        item.role == CarrierRole::RetainedReport
            ? std::array<std::string_view, 3>{"s8", "u8", "b8"}
        : item.role == CarrierRole::UnsignedWait
            ? std::array<std::string_view, 3>{"s64", "u64", "b64"}
            : std::array<std::string_view, 3>{"s64", "u64", "f64"};
    for (const auto carrier : candidates) {
      SCOPED_TRACE(carrier);
      const auto ast = test_helpers::parseModule(carrier_source(item, carrier));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      const auto draft = resolveModuleOnly(*ast);
      ASSERT_TRUE(draft) << draft.error().front().message;
      EXPECT_TRUE(draft->functions.front().body.front()->check(context));
      EXPECT_TRUE(validateModule(*ast, *draft));
      EXPECT_TRUE(resolveAndValidateModule(*ast));
      auto owned = carrier_draft(item, carrier);
      ASSERT_TRUE(owned);
      EXPECT_TRUE(validateModule(*owned));
      ResolvedModule copied(*owned), assigned;
      assigned = copied;
      EXPECT_TRUE(validateModule(copied));
      EXPECT_TRUE(validateModule(assigned));
      ResolvedFunction function(copied.functions.front()), function_assigned;
      function_assigned = function;
      EXPECT_TRUE(function_assigned.body.front()->check(context));
      assigned.functions.front().body.front() = function.body.front()->clone();
      EXPECT_TRUE(validateModule(assigned));
    }
  }
}
/** Each fundamental declaration is checked against an independent finite role domain. */
struct DeclaredCarrier {
  /** Legal ordinary declaration spelling. */
  std::string_view spelling;
  /** Actual retained declaration type, never rewritten to an instruction modifier. */
  ScalarType type;
};
constexpr std::array<DeclaredCarrier, 18> declared_carriers{
    {DeclaredCarrier{"b8", ScalarType::B8},
     {"b16", ScalarType::B16},
     {"b32", ScalarType::B32},
     {"b64", ScalarType::B64},
     {"b128", ScalarType::B128},
     {"s8", ScalarType::S8},
     {"s16", ScalarType::S16},
     {"s32", ScalarType::S32},
     {"s64", ScalarType::S64},
     {"u8", ScalarType::U8},
     {"u16", ScalarType::U16},
     {"u32", ScalarType::U32},
     {"u64", ScalarType::U64},
     {"f16", ScalarType::F16},
     {"f16x2", ScalarType::F16x2},
     {"f32", ScalarType::F32},
     {"f64", ScalarType::F64},
     {"pred", ScalarType::Pred}}};
/** Closed declaration classes follow the opaque/unsigned/retained-report distinction. */
bool carrier_allowed(CarrierRole role, ScalarType type) {
  if (role == CarrierRole::RetainedReport)
    return type == ScalarType::B8 || type == ScalarType::S8 ||
           type == ScalarType::U8;
  return type == ScalarType::B64 || type == ScalarType::S64 ||
         type == ScalarType::U64 ||
         (role == CarrierRole::Opaque && type == ScalarType::F64);
}
/** Require a type mismatch without confusing binding/source-identity failures. */
void expect_carrier_mismatch(const checker::CheckResult& result) {
  ASSERT_FALSE(result);
  ASSERT_FALSE(result.error().empty());
  EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& error) {
    return error.kind == checker::CheckDiagnosticKind::OperandTypeMismatch;
  }));
  EXPECT_FALSE(std::ranges::any_of(result.error(), [](const auto& error) {
    return error.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
  }));
}

TEST(MbarrierStateCarriers, ClosesEveryFundamentalDeclarationClass) {
  const auto context = carrier_context();
  for (const auto& item : carrier_cases) {
    for (const auto& carrier : declared_carriers) {
      SCOPED_TRACE(item.instruction);
      SCOPED_TRACE(carrier.spelling);
      const bool allowed = carrier_allowed(item.role, carrier.type);
      const auto logical =
          item.role == CarrierRole::RetainedReport ? ScalarType::B8
          : item.role == CarrierRole::UnsignedWait ? ScalarType::U64
                                                   : ScalarType::B64;
      // Predicate eligibility belongs to the register-class boundary, not the Bit row.
      if (carrier.type != ScalarType::Pred)
        EXPECT_EQ(
            base::scalar_types_compatible(
                carrier.type, logical, base::ScalarTypeSizePolicy::SameWidth),
            allowed);
      const auto ast =
          test_helpers::parseModule(carrier_source(item, carrier.spelling));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      const auto strict = resolveAndValidateModule(*ast);
      EXPECT_EQ(strict.has_value(), allowed);
      const auto draft = carrier_draft(item, carrier.spelling);
      if (!draft) {
        EXPECT_FALSE(allowed);
        ASSERT_FALSE(draft.error().empty());
        EXPECT_GT(draft.error().front().range.start.line, 0);
        continue;  // Early resolution rejection has no owned payload to validate.
      }
      if (allowed) {
        EXPECT_TRUE(validateModule(*draft));
        CarrierCapture capture;
        if (item.role == CarrierRole::RetainedReport)
          capture.report_name = "r";
        draft->functions.front().body.front()->visit_references(capture);
        ASSERT_TRUE(capture.value);
        EXPECT_EQ(capture.value->declared_type, carrier.type);
        EXPECT_TRUE(capture.value->symbol_id);
        EXPECT_EQ(capture.value->register_class,
                  ResolvedRegisterClass::General);
        EXPECT_FALSE(capture.value->vector_width);
      } else if (carrier.type != ScalarType::Pred) {
        expect_carrier_mismatch(
            draft->functions.front().body.front()->check(context));
        expect_carrier_mismatch(validateModule(*draft));
      } else
        EXPECT_FALSE(validateModule(*draft));
    }
  }
}

TEST(MbarrierStateCarriers,
     RejoinsRealScalarRetargetsWithoutInventingSourceHistory) {
  const auto context = carrier_context();
  for (const auto& item : carrier_cases) {
    const bool report = item.role == CarrierRole::RetainedReport;
    const auto baseline = report ? "b8" : "b64";
    const auto original = carrier_draft(item, baseline);
    ASSERT_TRUE(original);
    ASSERT_TRUE(validateModule(*original));
    const auto names =
        report ? std::array<std::string_view, 5>{"a", "i", "j", "x", "b"}
               : std::array<std::string_view, 5>{"b", "s", "u", "f", "x"};
    for (const auto name : names) {
      SCOPED_TRACE(item.instruction);
      SCOPED_TRACE(name);
      auto changed = *original;
      CarrierCapture capture;
      if (report)
        capture.report_name = "r";
      changed.functions.front().body.front()->visit_references(capture);
      ASSERT_TRUE(capture.value);
      const auto real = carrier_draft(item, baseline, name);
      ASSERT_TRUE(real);
      CarrierCapture candidate;
      if (report)
        candidate.report_name = name;
      real->functions.front().body.front()->visit_references(candidate);
      ASSERT_TRUE(candidate.value && candidate.value->declared_type);
      EXPECT_NE(capture.value->symbol_id, candidate.value->symbol_id);
      const bool allowed =
          carrier_allowed(item.role, *candidate.value->declared_type);
      const_cast<ResolvedRegisterRef&>(*capture.value) = *candidate.value;
      ResolvedModule copied(changed), assigned;
      assigned = copied;
      ResolvedFunction function(copied.functions.front()), function_assigned;
      function_assigned = function;
      assigned.functions.front().body.front() = function.body.front()->clone();
      if (allowed) {
        EXPECT_TRUE(changed.functions.front().body.front()->check(context));
        EXPECT_TRUE(validateModule(changed));
        EXPECT_TRUE(validateModule(copied));
        EXPECT_TRUE(validateModule(assigned));
        EXPECT_TRUE(function_assigned.body.front()->check(context));
        const auto ast =
            test_helpers::parseModule(carrier_source(item, baseline));
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        EXPECT_TRUE(validateModule(*ast, changed));
      } else {
        expect_carrier_mismatch(
            changed.functions.front().body.front()->check(context));
        expect_carrier_mismatch(validateModule(changed));
        expect_carrier_mismatch(validateModule(copied));
        expect_carrier_mismatch(validateModule(assigned));
        expect_carrier_mismatch(function_assigned.body.front()->check(context));
      }
    }
  }
}

TEST(MbarrierStateCarriers, PreservesComponentAndCompactMemberProvenance) {
  const auto context = carrier_context();
  for (size_t index = 0; index < 3; ++index) {
    const auto& item = carrier_cases[index];
    const auto original = carrier_draft(item, "b64", "V1.g");
    ASSERT_TRUE(original);
    ASSERT_TRUE(validateModule(*original));
    CarrierCapture capture;
    original->functions.front().body.front()->visit_references(capture);
    ASSERT_TRUE(capture.value && capture.value->component);
    EXPECT_EQ(capture.value->parameterized_index, 1u);
    EXPECT_EQ(capture.value->declared_type, ScalarType::U64);
    EXPECT_EQ(capture.value->component->lane, 1);
    EXPECT_EQ(capture.value->component->declaration_width, 2);
    EXPECT_EQ(capture.value->component->origin,
              RegisterComponentOrigin::ExplicitSelector);
    EXPECT_FALSE(capture.value->vector_width);
    for (int mutation = 0; mutation < 4; ++mutation) {
      auto changed = *original;
      CarrierCapture bad;
      changed.functions.front().body.front()->visit_references(bad);
      auto& ref = const_cast<ResolvedRegisterRef&>(*bad.value);
      if (mutation == 0)
        ref.component->lane = 2;
      if (mutation == 1) {
        ref.parameterized_index = 2;
        ref.spelling = "V2.g";
        ref.component->base_spelling = "V2";
      }
      if (mutation == 2)
        ref.declared_type = ScalarType::B32;
      if (mutation == 3) {
        ref.component.reset();
        ref.vector_width = 2;
        ref.spelling = "V1";
      }
      EXPECT_FALSE(validateModule(changed));
      auto copy = changed;
      copy.functions.front().body.front() =
          changed.functions.front().body.front()->clone();
      EXPECT_FALSE(validateModule(copy));
    }
    const auto target = carrier_draft(item, "b64", "F1.y");
    ASSERT_TRUE(target);
    CarrierCapture real;
    target->functions.front().body.front()->visit_references(real);
    ASSERT_TRUE(real.value);
    auto changed = *original;
    CarrierCapture current;
    changed.functions.front().body.front()->visit_references(current);
    const_cast<ResolvedRegisterRef&>(*current.value) = *real.value;
    if (item.role == CarrierRole::Opaque) {
      EXPECT_TRUE(validateModule(changed));
      const auto ast =
          test_helpers::parseModule(carrier_source(item, "b64", "V1.g"));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      EXPECT_TRUE(validateModule(*ast, changed));
      const auto replacement =
          test_helpers::parseModule(carrier_source(item, "b64", "F1.y"));
      ASSERT_MODULE_PARSE_SUCCEEDS(replacement);
      ASSERT_TRUE(resolveAndValidateModule(*replacement));
      const auto stale = validateModule(*replacement, changed);
      ASSERT_FALSE(stale);
      EXPECT_EQ(stale.error().front().kind,
                checker::CheckDiagnosticKind::ModuleSourceMismatch);
    } else
      expect_carrier_mismatch(validateModule(changed));
  }
}

TEST(MbarrierStateCarriers, RetainsScalarRegisterAndDeclarationFences) {
  for (size_t index = 0; index < carrier_cases.size(); ++index) {
    const auto& item = carrier_cases[index];
    const auto baseline =
        item.role == CarrierRole::RetainedReport ? "b8" : "b64";
    for (const auto operand : {"V1", "%clock64", "1", "_", "missing"}) {
      SCOPED_TRACE(item.instruction);
      SCOPED_TRACE(operand);
      const auto ast =
          test_helpers::parseModule(carrier_source(item, baseline, operand));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      EXPECT_EQ(resolveAndValidateModule(*ast).has_value(),
                index == 0 && std::string_view(operand) == "_");
    }
    const auto invalid =
        test_helpers::parseModule(carrier_source(item, "bf16"));
    ASSERT_MODULE_PARSE_SUCCEEDS(invalid);
    EXPECT_FALSE(resolveAndValidateModule(*invalid));
  }
}

/** Require the independent class guard, without type or source-association masking. */
void expect_report_class_mismatch(const checker::CheckResult& result,
                                  SourceRange range) {
  ASSERT_FALSE(result);
  ASSERT_EQ(result.error().size(), 1u);
  EXPECT_EQ(result.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedOperandShape);
  EXPECT_EQ(result.error().front().range, range);
  EXPECT_NE(result.error().front().message.find("General-register class"),
            std::string::npos);
}

TEST(MbarrierStateCarriers, RejectsRealPredicateRetargetInReportRole) {
  const auto& item = carrier_cases.back();
  auto source = carrier_source(item, "b8");
  source.insert(source.rfind('}'), "mov.pred p,p1;");
  auto owned = [&] {
    const auto ast = test_helpers::parseModule(source);
    EXPECT_TRUE(ast && ast.diagnostics.empty());
    return resolveAndValidateModule(*ast);
  }();
  ASSERT_TRUE(owned);
  const auto real =
      dynamic_cast<const MovPred&>(*owned->functions.front().body.back())
          .dst.value.register_ref;
  ASSERT_EQ(real.register_class, ResolvedRegisterClass::Predicate);
  ASSERT_EQ(real.declared_type, ScalarType::Pred);
  ASSERT_TRUE(real.symbol_id);
  CarrierCapture capture;
  capture.report_name = "r";
  owned->functions.front().body.front()->visit_references(capture);
  ASSERT_TRUE(capture.value);
  ASSERT_FALSE(capture.locations.empty());
  const auto range = capture.locations.front();
  const auto original = *capture.value;
  const_cast<ResolvedRegisterRef&>(*capture.value) = real;
  expect_report_class_mismatch(
      owned->functions.front().body.front()->check(carrier_context()), range);
  expect_report_class_mismatch(validateModule(*owned), range);
  auto copy = *owned;
  expect_report_class_mismatch(validateModule(copy), range);
  ResolvedModule assigned;
  assigned = copy;
  assigned.functions.front().body.front() =
      copy.functions.front().body.front()->clone();
  expect_report_class_mismatch(validateModule(assigned), range);
  const_cast<ResolvedRegisterRef&>(*capture.value) = original;
  EXPECT_TRUE(validateModule(*owned));
}

TEST(MbarrierStateCarriers, CoversControlLayoutsAndPreservesClusterSink) {
  for (const auto operation :
       {"mbarrier.arrive_drop.b64 $, [M];",
        "mbarrier.arrive.release.cta.shared::cta.b64 $, [M], 1;",
        "mbarrier.arrive.expect_tx.release.cta.shared.b64 $, [M], 1;",
        "mbarrier.arrive.noComplete.b64 $, [M], 1;",
        "mbarrier.arrive_drop.noComplete.release.cta.shared::cta.b64 $, [M], "
        "1;",
        "mbarrier.pending_count.layout::v0.b64 c, $;"}) {
    const CarrierCase item{operation, CarrierRole::Opaque};
    for (const auto type : {"b64", "s64", "u64", "f64"}) {
      const auto ast = test_helpers::parseModule(carrier_source(item, type));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      const auto module = resolveAndValidateModule(*ast);
      ASSERT_TRUE(module) << module.error().front().message;
      EXPECT_TRUE(validateModule(*module));
    }
  }
  for (const auto operation :
       {"mbarrier.try_wait.b64 p,[M],$;",
        "mbarrier.try_wait.acquire.cta.shared::cta.b64 p,[M],$,8;",
        "mbarrier.test_wait.phase_type::primary.b64 p|p1,r,[M],$;",
        "mbarrier.try_wait.phase_type::primary.acquire.cta.b64 "
        "p|p1,r,[M],$,8;"}) {
    const CarrierCase item{operation, CarrierRole::UnsignedWait};
    for (const auto type : {"b64", "s64", "u64", "f64"}) {
      const auto ast = test_helpers::parseModule(carrier_source(item, type));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      EXPECT_EQ(resolveAndValidateModule(*ast).has_value(),
                std::string_view(type) != "f64");
    }
  }
  for (const auto operation :
       {"mbarrier.test_wait.parity.phase_type::primary.b64 p|p1,$,[M],0;",
        "mbarrier.try_wait.phase_type::primary.b64 p|p1,$,[M],q,8;",
        "mbarrier.try_wait.parity.phase_type::primary.b64 p|p1,$,[M],0,8;"}) {
    const CarrierCase item{operation, CarrierRole::RetainedReport};
    for (const auto type : {"b8", "s8", "u8"}) {
      const auto ast = test_helpers::parseModule(carrier_source(item, type));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      ASSERT_TRUE(resolveAndValidateModule(*ast));
    }
  }
  for (const auto operand : {"_", "f"}) {
    const CarrierCase item{"mbarrier.arrive.shared::cluster.b64 $, [M];",
                           CarrierRole::Opaque};
    const auto ast =
        test_helpers::parseModule(carrier_source(item, "f64", operand));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_EQ(resolveAndValidateModule(*ast).has_value(),
              std::string_view(operand) == "_");
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
