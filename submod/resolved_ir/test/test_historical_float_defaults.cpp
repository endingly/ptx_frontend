#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>

#include <ptx_frontend/resolved_ir/model/arithmetic/cos.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/div.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/ex2.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/lg2.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/mad.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/rcp.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/rsqrt.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/sin.gen.hpp>
#include <ptx_frontend/resolved_ir/model/arithmetic/sqrt.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Whether a final class exposes any source mode or FTZ control. */
template <typename T>
concept HasFloatingControls =
    requires(T value) { value.rounding; } || requires(T value) {
      value.approx;
    } || requires(T value) { value.ftz; } || requires(T value) { value.full; };
static_assert(!HasFloatingControls<DivImplicitF32>);
static_assert(!HasFloatingControls<DivImplicitF64>);
static_assert(!HasFloatingControls<RcpImplicitF32>);
static_assert(!HasFloatingControls<RcpImplicitF64>);
static_assert(RcpImplicitF64::floating_point_default ==
              FloatingPointDefault::RoundNearestEven);
static_assert(RcpImplicitF64::kind != RcpRnF64::kind);
static_assert(!HasFloatingControls<SqrtImplicitF32>);
static_assert(!HasFloatingControls<SqrtImplicitF64>);
static_assert(!HasFloatingControls<RsqrtImplicitF32>);
static_assert(!HasFloatingControls<RsqrtImplicitF64>);
static_assert(!HasFloatingControls<SinImplicitF32>);
static_assert(!HasFloatingControls<CosImplicitF32>);
static_assert(!HasFloatingControls<Lg2ImplicitF32>);
static_assert(!HasFloatingControls<Ex2ImplicitF32>);
static_assert(!HasFloatingControls<MadImplicitF64>);
static_assert(DivImplicitF32::floating_point_default ==
              FloatingPointDefault::ApproximateFlushToZero);
static_assert(RcpImplicitF32::floating_point_default ==
              FloatingPointDefault::ApproximateFlushToZero);
static_assert(SqrtImplicitF32::floating_point_default ==
              FloatingPointDefault::ApproximateFlushToZero);
static_assert(RsqrtImplicitF32::floating_point_default ==
              FloatingPointDefault::ApproximateFlushToZero);
static_assert(SinImplicitF32::floating_point_default ==
              FloatingPointDefault::ApproximateFlushToZero);
static_assert(CosImplicitF32::floating_point_default ==
              FloatingPointDefault::ApproximateFlushToZero);
static_assert(Lg2ImplicitF32::floating_point_default ==
              FloatingPointDefault::ApproximateFlushToZero);
static_assert(Ex2ImplicitF32::floating_point_default ==
              FloatingPointDefault::ApproximateFlushToZero);
static_assert(DivImplicitF64::floating_point_default ==
              FloatingPointDefault::RoundNearestEven);
static_assert(SqrtImplicitF64::floating_point_default ==
              FloatingPointDefault::RoundNearestEven);
static_assert(MadImplicitF64::floating_point_default ==
              FloatingPointDefault::RoundNearestEven);
static_assert(RsqrtImplicitF64::floating_point_default ==
              FloatingPointDefault::Approximate);

/** Exact omitted-control spellings and their version-independent identities. */
struct OmittedFloatCase {
  /** Complete source instruction using declared floating registers. */
  std::string_view source;
  /** Exact identity, distinct from every explicit-mode identity. */
  InstructionKind kind;
  /** Native double-precision form requires SM13. */
  bool fp64 = false;
  /** Exact division, reciprocal and square root inherit the source mapping option. */
  bool mapped = false;
};
constexpr std::array kOmittedFloatCases{
    OmittedFloatCase{"div.f32 %f0, %f1, %f2;", DivImplicitF32::kind},
    OmittedFloatCase{"rcp.f32 %f0, %f1;", RcpImplicitF32::kind},
    OmittedFloatCase{"sqrt.f32 %f0, %f1;", SqrtImplicitF32::kind},
    OmittedFloatCase{"rsqrt.f32 %f0, %f1;", RsqrtImplicitF32::kind},
    OmittedFloatCase{"sin.f32 %f0, %f1;", SinImplicitF32::kind},
    OmittedFloatCase{"cos.f32 %f0, %f1;", CosImplicitF32::kind},
    OmittedFloatCase{"lg2.f32 %f0, %f1;", Lg2ImplicitF32::kind},
    OmittedFloatCase{"ex2.f32 %f0, %f1;", Ex2ImplicitF32::kind},
    OmittedFloatCase{"div.f64 %d0, %d1, %d2;", DivImplicitF64::kind, true,
                     true},
    OmittedFloatCase{"rcp.f64 %d0, %d1;", RcpImplicitF64::kind, true, true},
    OmittedFloatCase{"sqrt.f64 %d0, %d1;", SqrtImplicitF64::kind, true, true},
    OmittedFloatCase{"rsqrt.f64 %d0, %d1;", RsqrtImplicitF64::kind, true},
    OmittedFloatCase{"mad.f64 %d0, %d1, %d2, %d3;", MadImplicitF64::kind, true},
};

/** Build a module with legal register declarations and selected header context. */
std::string omittedFloatModule(std::string_view operation,
                               std::string_view version = "1.3",
                               std::string_view target = "sm_13") {
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) +
         "\n.entry k() { .reg .f32 %f<4>; .reg .f64 %d<4>; " +
         std::string(operation) + " }\n";
}

/** Standalone resolution selects omission identity before any contextual check. */
TEST(HistoricalFloatDefaults, StandaloneExactIdentityAndHalfOpenVersionMatrix) {
  for (const auto& item : kOmittedFloatCases) {
    SCOPED_TRACE(item.source);
    std::unique_ptr<Instruction> instruction;
    {
      const auto parsed = test_helpers::parseInstruction(item.source);
      ASSERT_TRUE(parsed);
      auto resolved = resolveInstruction(*parsed);
      ASSERT_TRUE(resolved) << resolved.error().message;
      instruction = std::move(*resolved);
    }
    EXPECT_EQ(instruction->instruction_kind(), item.kind);
    auto clone = instruction->clone();
    EXPECT_EQ(clone->instruction_kind(), item.kind);
    for (const auto version :
         {checker::PtxVersion{1, 0}, {1, 3}, {1, 4}, {9, 3}}) {
      checker::Context context{
          .target = {.ptx_version = version, .sm_version = 13}};
      const bool accepted = version < checker::PtxVersion{1, 4};
      for (const auto* selected : {instruction.get(), clone.get()}) {
        const auto result = selected->check(context);
        EXPECT_EQ(bool(result), accepted);
        if (!accepted) {
          ASSERT_FALSE(result);
          EXPECT_EQ(result.error().front().kind,
                    item.mapped
                        ? checker::CheckDiagnosticKind::UnsupportedAvailability
                        : checker::CheckDiagnosticKind::UnsupportedPtxVersion);
          if (!item.mapped)
            EXPECT_NE(result.error().front().message.find("< 1.4 (exclusive)"),
                      std::string::npos);
        }
      }
    }
  }
}

/** Native FP64 and the exact sm1x source option remain independent requirements. */
TEST(HistoricalFloatDefaults, ExactMappingTargetsAndNativeDoubleExclusions) {
  for (const auto& item : kOmittedFloatCases) {
    const auto parsed = test_helpers::parseInstruction(item.source);
    ASSERT_TRUE(parsed);
    const auto resolved = resolveInstruction(*parsed);
    ASSERT_TRUE(resolved);
    for (const unsigned sm : {10, 11, 12, 13, 20}) {
      for (const bool mapped : {false, true}) {
        SCOPED_TRACE(item.source);
        SCOPED_TRACE(sm);
        SCOPED_TRACE(mapped);
        checker::Context context{
            .target = {
                .ptx_version = {1, 3},
                .sm_version = sm,
                .identity =
                    base::TargetIdentity{{sm}, base::TargetFlavor::Generic, {}},
                .map_f64_to_f32 = mapped}};
        EXPECT_EQ(bool((*resolved)->check(context)),
                  !item.fp64 || sm >= 13 || (item.mapped && mapped));
        if (item.mapped && sm < 13) {
          context.target.identity.reset();
          EXPECT_FALSE((*resolved)->check(context));
          context.target.identity = base::TargetIdentity{
              {sm}, base::TargetFlavor::ArchitectureSpecific, {}};
          EXPECT_FALSE((*resolved)->check(context));
        }
      }
    }
  }
}

/** Module checks use current owned or replacement version after source destruction. */
TEST(HistoricalFloatDefaults, ModuleOwnershipCopyAndReplacementVersionContext) {
  for (const auto& item : kOmittedFloatCases) {
    SCOPED_TRACE(item.source);
    std::optional<ResolvedModule> module;
    {
      const auto parsed =
          test_helpers::parseModule(omittedFloatModule(item.source));
      ASSERT_TRUE(parsed);
      auto resolved = resolveAndValidateModule(*parsed);
      ASSERT_TRUE(resolved) << resolved.error().front().message;
      for (const auto policy : {ModuleValidationPolicy::AvailableContext,
                                ModuleValidationPolicy::RequireCompleteContext})
        EXPECT_TRUE(validateModule(*parsed, *resolved, policy));
      module.emplace(std::move(*resolved));
    }
    auto copy = *module;
    EXPECT_EQ(copy.functions.front().instruction_ranges,
              module->functions.front().instruction_ranges);
    EXPECT_EQ(copy.functions.front().body.front()->instruction_kind(),
              item.kind);
    for (const auto policy : {ModuleValidationPolicy::AvailableContext,
                              ModuleValidationPolicy::RequireCompleteContext}) {
      EXPECT_TRUE(validateModule(*module, policy));
      EXPECT_TRUE(validateModule(copy, policy));
    }
    const auto replacement =
        test_helpers::parseModule(omittedFloatModule(item.source, "1.4"));
    ASSERT_TRUE(replacement);
    EXPECT_TRUE(resolveModuleOnly(*replacement));
    EXPECT_FALSE(resolveAndValidateModule(*replacement));
    for (const auto policy : {ModuleValidationPolicy::AvailableContext,
                              ModuleValidationPolicy::RequireCompleteContext})
      EXPECT_FALSE(validateModule(*replacement, copy, policy));
    for (auto& region : copy.header.regions)
      region.version = checker::PtxVersion{1, 4};
    for (auto& function : copy.functions)
      function.source_version = checker::PtxVersion{1, 4};
    for (const auto policy : {ModuleValidationPolicy::AvailableContext,
                              ModuleValidationPolicy::RequireCompleteContext}) {
      const auto result = validateModule(copy, policy);
      ASSERT_FALSE(result);
      EXPECT_EQ(result.error().front().range,
                module->functions.front().instruction_ranges.front());
    }
    EXPECT_TRUE(validateModule(*module));
  }
}

/** Header endpoints and the source mapping option feed both module validators. */
TEST(HistoricalFloatDefaults, ModuleHeaderAndMappingMatrix) {
  for (const auto& item : kOmittedFloatCases) {
    for (const auto version : {"1.0", "1.3", "1.4", "9.3"}) {
      for (const bool mapped : {false, true}) {
        SCOPED_TRACE(item.source);
        SCOPED_TRACE(version);
        SCOPED_TRACE(mapped);
        const bool oldest = std::string_view(version) == "1.0";
        const auto target = oldest || mapped ? "sm_10" : "sm_13";
        const auto parsed = test_helpers::parseModule(omittedFloatModule(
            item.source, version,
            std::string(target) + (mapped ? ", map_f64_to_f32" : "")));
        ASSERT_TRUE(parsed);
        const auto resolved = resolveModuleOnly(*parsed);
        ASSERT_TRUE(resolved) << resolved.error().front().message;
        const bool historical = oldest || std::string_view(version) == "1.3";
        const bool accepted =
            historical &&
            (!item.fp64 || (!oldest && !mapped) || (item.mapped && mapped));
        EXPECT_EQ(bool(resolveAndValidateModule(*parsed)), accepted);
        for (const auto policy :
             {ModuleValidationPolicy::AvailableContext,
              ModuleValidationPolicy::RequireCompleteContext}) {
          EXPECT_EQ(bool(validateModule(*parsed, *resolved, policy)), accepted);
          EXPECT_EQ(bool(validateModule(*resolved, policy)), accepted);
        }
      }
    }
  }
}

/** Missing mode cannot mix with written FTZ or conflicting explicit controls. */
TEST(HistoricalFloatDefaults, RejectsMixedControls) {
  for (const auto source :
       {"div.ftz.f32 %f0, %f1, %f2;", "rcp.ftz.f32 %f0, %f1;",
        "sqrt.ftz.f32 %f0, %f1;", "rsqrt.ftz.f32 %f0, %f1;",
        "sin.ftz.f32 %f0, %f1;", "cos.ftz.f32 %f0, %f1;",
        "lg2.ftz.f32 %f0, %f1;", "ex2.ftz.f32 %f0, %f1;",
        "mad.ftz.f64 %d0, %d1, %d2, %d3;"}) {
    SCOPED_TRACE(source);
    const auto parsed = test_helpers::parseInstruction(source);
    if (parsed)
      EXPECT_FALSE(resolveInstruction(*parsed));
  }
}

/** Both availability shapes honor the exclusive upper bound and retain minimum-only use. */
TEST(HistoricalFloatDefaults, PlainAndDnfAvailabilityBounds) {
  checker::AvailabilityDescriptor plain{
      .minimum_ptx_version = {1, 0}, .ptx_before = checker::PtxVersion{1, 4}};
  checker::AvailabilityDescriptor dnf{
      .any_of = {{{.minimum_ptx_version = {1, 0},
                   .ptx_before = checker::PtxVersion{1, 4}}}},
      .any_of_count = 1};
  for (const auto version :
       {checker::PtxVersion{0, 9}, {1, 0}, {1, 3}, {1, 4}, {9, 3}}) {
    const checker::TargetInfo target{.ptx_version = version};
    EXPECT_EQ(checker::is_available(plain, target),
              (version >= checker::PtxVersion{1, 0} &&
               version < checker::PtxVersion{1, 4}));
    EXPECT_EQ(checker::is_available(dnf, target),
              checker::is_available(plain, target));
  }
  plain.ptx_before.reset();
  EXPECT_TRUE(checker::is_available(plain, {.ptx_version = {9, 3}}));
}

/** Finite real source profile matrix preserves header rejection as a separate cause. */
TEST(LegacyRcpF64, RealLegacyProfilesAndExplicitWindowControls) {
  for (int minor : {0, 1, 2, 3})
    for (int sm : {10, 11, 12, 13})
      for (bool map : {false, true}) {
        SCOPED_TRACE(::testing::Message()
                     << minor << " sm" << sm << " map" << map);
        const auto ast = test_helpers::parseModule(omittedFloatModule(
            "rcp.f64 %d0,%d1;", "1." + std::to_string(minor),
            "sm_" + std::to_string(sm) + (map ? ", map_f64_to_f32" : "")));
        ASSERT_TRUE(ast);
        const bool header_bad = (minor < 2 && sm >= 12) || (sm == 13 && map);
        const bool accepted = !header_bad && (sm == 13 || map);
        auto module = resolveModuleOnly(*ast);
        if (header_bad) {
          EXPECT_FALSE(resolveAndValidateModule(*ast));
          continue;
        }
        ASSERT_TRUE(module) << module.error().front().message;
        EXPECT_EQ(module->functions.front().body.front()->instruction_kind(),
                  RcpImplicitF64::kind);
        EXPECT_EQ(bool(resolveAndValidateModule(*ast)), accepted);
        for (auto policy : {ModuleValidationPolicy::AvailableContext,
                            ModuleValidationPolicy::RequireCompleteContext}) {
          for (auto result : {validateModule(*module, policy),
                              validateModule(*ast, *module, policy)}) {
            EXPECT_EQ(bool(result), accepted);
            if (!accepted) {
              ASSERT_FALSE(result);
              EXPECT_EQ(result.error().front().kind,
                        checker::CheckDiagnosticKind::UnsupportedAvailability);
            }
          }
        }
      }
  for (const auto& [op, version, target, accepted] :
       {std::tuple{"rcp.f64 %d0,%d1;", "1.4", "sm_13", false},
        {"rcp.f64 %d0,%d1;", "9.3", "sm_90", false},
        {"rcp.rn.f64 %d0,%d1;", "1.4", "sm_13", true},
        {"rcp.rn.f64 %d0,%d1;", "1.3", "sm_13", false},
        {"rcp.rn.f64 %d0,%d1;", "9.3", "sm_90", true},
        {"rcp.rn.f64 %d0,%d1;", "1.4", "sm_10, map_f64_to_f32", true}}) {
    auto ast =
        test_helpers::parseModule(omittedFloatModule(op, version, target));
    ASSERT_TRUE(ast);
    EXPECT_EQ(bool(resolveAndValidateModule(*ast)), accepted);
  }
}

/** Current context is mutable state, while a source form and owned operands retain their identity. */
TEST(LegacyRcpF64, SameBarePayloadSuppliedHeaderAndCurrentContext) {
  auto ast = test_helpers::parseModule(omittedFloatModule("rcp.f64 %d0,%d1;"));
  ASSERT_TRUE(ast);
  auto module = resolveAndValidateModule(*ast);
  ASSERT_TRUE(module) << module.error().front().message;
  auto replacement =
      test_helpers::parseModule(omittedFloatModule("rcp.f64 %d0,%d1;", "1.4"));
  ASSERT_TRUE(replacement);
  for (auto policy : {ModuleValidationPolicy::AvailableContext,
                      ModuleValidationPolicy::RequireCompleteContext}) {
    auto bad = validateModule(*replacement, *module, policy);
    ASSERT_FALSE(bad);
    EXPECT_EQ(bad.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedAvailability);
    EXPECT_EQ(bad.error().front().range,
              module->functions.front().instruction_ranges.front());
    EXPECT_TRUE(validateModule(*ast, *module, policy));
  }
  auto copy = *module;
  ResolvedModule assigned;
  assigned = copy;
  auto moved = std::move(assigned);
  for (auto& function : moved.functions)
    for (auto& instruction : function.body)
      instruction = instruction->clone();
  EXPECT_EQ(moved.functions.front().instruction_ranges,
            module->functions.front().instruction_ranges);
  EXPECT_TRUE(validateModule(moved));
  for (auto& r : moved.header.regions)
    r.version = checker::PtxVersion{1, 4};
  for (auto& f : moved.functions)
    f.source_version = checker::PtxVersion{1, 4};
  auto bad = validateModule(moved);
  ASSERT_FALSE(bad);
  EXPECT_EQ(bad.error().front().kind,
            checker::CheckDiagnosticKind::UnsupportedAvailability);
  for (auto& r : moved.header.regions)
    r.version = checker::PtxVersion{1, 3};
  for (auto& f : moved.functions)
    f.source_version = checker::PtxVersion{1, 3};
  EXPECT_TRUE(validateModule(moved));
}

/** Whole-instruction availability still defers under incomplete context, never certifies partial legality. */
TEST(LegacyRcpF64, MissingContextDefersAndStrictRequiresContext) {
  for (bool missing_version : {false, true}) {
    auto source = omittedFloatModule("rcp.f64 %d0,%d1;");
    auto ast = test_helpers::parseModule(source);
    ASSERT_TRUE(ast);
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module);
    auto partial_source =
        missing_version
            ? std::string(
                  ".target sm_13\n.entry k(){.reg .f64 %d<4>;rcp.f64 %d0,%d1;}")
            : std::string(
                  ".version 1.4\n.entry k(){.reg .f64 %d<4>;rcp.f64 %d0,%d1;}");
    auto partial_ast = test_helpers::parseModule(partial_source);
    ASSERT_TRUE(partial_ast);
    for (auto& r : module->header.regions) {
      if (missing_version) {
        r.version.reset();
        r.version_provenance = SourceConfigurationProvenance::Missing;
      } else {
        r.version = checker::PtxVersion{1, 4};
        r.target_options.clear();
        r.target_provenance = SourceConfigurationProvenance::Missing;
      }
    }
    for (auto& f : module->functions)
      if (missing_version)
        f.source_version.reset();
      else {
        f.source_version = checker::PtxVersion{1, 4};
        f.source_target.reset();
      }
    EXPECT_TRUE(
        validateModule(*module, ModuleValidationPolicy::AvailableContext));
    // Supplied payload and ranges must be identical; only the header is changed.
    auto provided = test_helpers::parseModule(
        missing_version
            ? std::string(".target sm_13\n.entry k() { .reg .f32 %f<4>; .reg "
                          ".f64 %d<4>; rcp.f64 %d0,%d1; }\n")
            : std::string(".version 1.4\n.entry k() { .reg .f32 %f<4>; .reg "
                          ".f64 %d<4>; rcp.f64 %d0,%d1; }\n"));
    ASSERT_TRUE(provided);
    EXPECT_TRUE(validateModule(*provided, *module,
                               ModuleValidationPolicy::AvailableContext));
    for (auto result :
         {validateModule(*module,
                         ModuleValidationPolicy::RequireCompleteContext),
          validateModule(*provided, *module,
                         ModuleValidationPolicy::RequireCompleteContext)}) {
      ASSERT_FALSE(result);
      EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& d) {
        return d.kind == checker::CheckDiagnosticKind::MissingValidationContext;
      }));
    }
  }
  auto unknown = test_helpers::parseModule(
      omittedFloatModule("rcp.f64 %d0,%d1;", "1.3", "sm_999"));
  ASSERT_TRUE(unknown);
  auto unknown_module = resolveModuleOnly(*unknown);
  ASSERT_TRUE(unknown_module) << unknown_module.error().front().message;
  for (auto policy : {ModuleValidationPolicy::AvailableContext,
                      ModuleValidationPolicy::RequireCompleteContext}) {
    for (auto result : {validateModule(*unknown_module, policy),
                        validateModule(*unknown, *unknown_module, policy)}) {
      ASSERT_FALSE(result);
      EXPECT_TRUE(
          std::ranges::any_of(result.error(), [](const auto& diagnostic) {
            return diagnostic.kind ==
                       checker::CheckDiagnosticKind::UnknownTarget &&
                   diagnostic.message.find("sm_999") != std::string::npos;
          }));
    }
  }
  const auto strict_unknown = resolveAndValidateModule(*unknown);
  ASSERT_FALSE(strict_unknown);
  EXPECT_TRUE(
      std::ranges::any_of(strict_unknown.error(), [](const auto& diagnostic) {
        return diagnostic.checker_kind ==
               checker::CheckDiagnosticKind::UnknownTarget;
      }));
}

/** Removing a real source mapping option changes availability without inventing profile capability. */
TEST(LegacyRcpF64, CurrentMappingOptionAndOperandPolicy) {
  auto source =
      omittedFloatModule("rcp.f64 %d0,%d1;", "1.0", "sm_10, map_f64_to_f32");
  auto ast = test_helpers::parseModule(source);
  ASSERT_TRUE(ast);
  auto m = resolveAndValidateModule(*ast);
  ASSERT_TRUE(m);
  for (auto& r : m->header.regions) {
    r.map_f64_to_f32 = false;
    std::erase(r.target_options, std::string("map_f64_to_f32"));
  }
  auto plain = test_helpers::parseModule(
      omittedFloatModule("rcp.f64 %d0,%d1;", "1.0", "sm_10"));
  ASSERT_TRUE(plain);
  for (auto result : {validateModule(*m), validateModule(*plain, *m)}) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().front().kind,
              checker::CheckDiagnosticKind::UnsupportedAvailability);
  }
  for (auto op :
       {"rcp.ftz.f64 %d0,%d1;", "rcp.approx.f64 %d0,%d1;",
        "rcp.approx.rn.f64 %d0,%d1;", "rcp.f64 %f0,%f1;", "rcp.f64 %d0,1;"}) {
    auto a = test_helpers::parseModule(omittedFloatModule(op));
    ASSERT_TRUE(a);
    EXPECT_FALSE(resolveAndValidateModule(*a));
  }
  for (auto op : {"rcp.f64 %d0,%d1;", "rcp.f64 %d0,1.0;"}) {
    auto a = test_helpers::parseModule(omittedFloatModule(op));
    ASSERT_TRUE(a);
    EXPECT_TRUE(resolveAndValidateModule(*a));
  }
  auto bit = test_helpers::parseModule(
      ".version 1.3\n.target sm_13\n.entry k(){.reg .b64 %b<2>;rcp.f64 "
      "%b0,%b1;}");
  ASSERT_TRUE(bit);
  EXPECT_TRUE(resolveAndValidateModule(*bit));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
