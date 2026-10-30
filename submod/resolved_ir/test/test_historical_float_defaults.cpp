#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>

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
  /** Only exact division and square root inherit the source mapping option. */
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

/** Missing mode cannot mix with written FTZ; reciprocal FP64 omission stays excluded. */
TEST(HistoricalFloatDefaults, RejectsMixedControlsAndExcludedReciprocal) {
  for (const auto source :
       {"div.ftz.f32 %f0, %f1, %f2;", "rcp.ftz.f32 %f0, %f1;",
        "sqrt.ftz.f32 %f0, %f1;", "rsqrt.ftz.f32 %f0, %f1;",
        "sin.ftz.f32 %f0, %f1;", "cos.ftz.f32 %f0, %f1;",
        "lg2.ftz.f32 %f0, %f1;", "ex2.ftz.f32 %f0, %f1;", "rcp.f64 %d0, %d1;",
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

}  // namespace
}  // namespace ptx_frontend::resolved_ir
