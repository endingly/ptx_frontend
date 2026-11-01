#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>

#include <ptx_frontend/resolved_ir/model/data_movement/shfl.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Limit shared inspection to the four generated legacy shuffle forms. */
template <typename T>
concept LegacyShuffleForm =
    std::same_as<T, ShflUpB32> || std::same_as<T, ShflDownB32> ||
    std::same_as<T, ShflBflyB32> || std::same_as<T, ShflIdxB32>;

/** Legacy forms cannot manufacture synchronization or member-mask fields. */
template <LegacyShuffleForm T>
constexpr bool kMaskFreeShuffle = !requires(T instruction) {
  instruction.sync;
} && !requires(T instruction) { instruction.membermask; };
static_assert(kMaskFreeShuffle<ShflUpB32> && kMaskFreeShuffle<ShflDownB32> &&
              kMaskFreeShuffle<ShflBflyB32> && kMaskFreeShuffle<ShflIdxB32>);
static_assert(ShflUpB32::up && ShflDownB32::down && ShflBflyB32::bfly &&
              ShflIdxB32::idx);

/** One written mode and its distinct generated source-form identity. */
struct LegacyShuffleCase {
  /** Source spelling used to construct the fixture. */
  std::string_view mode;
  /** Identity retained independently of target legality. */
  InstructionKind kind;
};
/** Complete mode inventory; every entry runs both destination layouts. */
constexpr std::array kLegacyShuffles{
    LegacyShuffleCase{"up", ShflUpB32::kind},
    LegacyShuffleCase{"down", ShflDownB32::kind},
    LegacyShuffleCase{"bfly", ShflBflyB32::kind},
    LegacyShuffleCase{"idx", ShflIdxB32::kind}};

/** Typed references borrow from an instruction whose owner remains alive. */
struct LegacyShuffleView {
  /** Active register-only destination, or null for the paired layout. */
  const WithLocs<ResolvedRegisterRef>* bare;
  /** Active data/predicate destination, or null for the bare layout. */
  const WithLocs<ResolvedShflSyncDestination>* paired;
  /** Data input with its owned source ranges. */
  const WithLocs<ResolvedRegisterRef>* source;
  /** Lane control b with its owned source ranges. */
  const WithLocs<RegOrImm>* lane;
  /** Clamp control c with its owned source ranges. */
  const WithLocs<RegOrImm>* clamp;
};

/** Inspect the two mutually exclusive layouts without losing typed controls. */
template <LegacyShuffleForm T>
LegacyShuffleView shuffleView(const T& instruction) {
  return {instruction.dst_register ? &*instruction.dst_register : nullptr,
          instruction.dst_shfl_destination ? &*instruction.dst_shfl_destination
                                           : nullptr,
          &instruction.src, &instruction.lane, &instruction.clamp};
}

/** Select only unsynchronized forms by their concrete generated type. */
std::optional<LegacyShuffleView> legacyShuffleView(
    const Instruction& instruction) {
  if (const auto* form = dynamic_cast<const ShflUpB32*>(&instruction))
    return shuffleView(*form);
  if (const auto* form = dynamic_cast<const ShflDownB32*>(&instruction))
    return shuffleView(*form);
  if (const auto* form = dynamic_cast<const ShflBflyB32*>(&instruction))
    return shuffleView(*form);
  if (const auto* form = dynamic_cast<const ShflIdxB32*>(&instruction))
    return shuffleView(*form);
  return std::nullopt;
}

/** Construct a mask-free source operation with independent b/c control choices. */
std::string legacyShuffleOperation(std::string_view mode, bool paired,
                                   bool register_lane, bool register_clamp) {
  return "shfl." + std::string(mode) + ".b32 %b0" + (paired ? "|%p" : "") +
         ", %b1, " + (register_lane ? "%u0" : "1") + ", " +
         (register_clamp ? "%u1" : "31") + ";";
}

/** Construct a complete target region and all register-type negative controls. */
std::string legacyShuffleModule(std::string_view operation,
                                std::string_view target,
                                std::string_view version = "3.0") {
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) +
         "\n.func f() { .reg .b32 %b<2>; .reg .pred %p; .reg .u32 %u<2>; "
         ".reg .b64 %wide; .reg .b16 %half; .reg .f32 %f; " +
         std::string(operation) + " }\n";
}

/** Availability is a checker decision, shared by AST-backed and owned paths. */
void expectShuffleAvailability(const std::string& source, bool available) {
  PtxSyntaxParser parser{source};
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast);
  ASSERT_TRUE(ast.diagnostics.empty());
  const auto resolved = resolveModuleOnly(*ast);
  ASSERT_TRUE(resolved) << resolved.error().front().message;
  EXPECT_EQ(validateModule(*ast, *resolved).has_value(), available);
  EXPECT_EQ(validateModule(*resolved).has_value(), available);
}

/** Every mode/layout/control/target combination retains identity after AST destruction. */
TEST(LegacyShuffle, OwnsEveryModeLayoutControlAndTarget) {
  for (const auto& item : kLegacyShuffles)
    for (const auto target : {"sm_30", "sm_32", "sm_35", "sm_37", "sm_50",
                              "sm_52", "sm_53", "sm_60", "sm_61", "sm_62"})
      for (const bool paired : {false, true})
        for (const bool register_lane : {false, true})
          for (const bool register_clamp : {false, true}) {
            const auto operation = legacyShuffleOperation(
                item.mode, paired, register_lane, register_clamp);
            SCOPED_TRACE(operation);
            SCOPED_TRACE(target);
            std::optional<ResolvedModule> owned;
            {
              const auto source = legacyShuffleModule(operation, target, "9.3");
              PtxSyntaxParser parser{source};
              const auto ast = parser.parseModule();
              ASSERT_TRUE(ast);
              auto resolved = resolveAndValidateModule(*ast);
              ASSERT_TRUE(resolved) << resolved.error().front().message;
              ASSERT_TRUE(validateModule(*ast, *resolved));
              owned.emplace(std::move(*resolved));
            }
            ASSERT_TRUE(validateModule(*owned));
            const auto& function = owned->functions.front();
            const auto& instruction = *function.body.front();
            EXPECT_EQ(instruction.instruction_kind(), item.kind);
            EXPECT_EQ(function.instruction_opcodes.front(), "shfl");
            EXPECT_EQ(function.source_target, target);
            const auto view = legacyShuffleView(instruction);
            ASSERT_TRUE(view);
            EXPECT_EQ(view->paired != nullptr, paired);
            EXPECT_EQ(view->bare != nullptr, !paired);
            const ResolvedRegisterRef* destination = nullptr;
            if (paired) {
              ASSERT_TRUE(view->paired->value.data);
              ASSERT_TRUE(view->paired->value.predicate);
              destination = &view->paired->value.data->value;
              const auto& predicate = *view->paired->value.predicate;
              EXPECT_EQ(predicate.value.register_ref.spelling, "%p");
              EXPECT_EQ(predicate.value.register_ref.declared_type,
                        ScalarType::Pred);
              EXPECT_TRUE(predicate.value.register_ref.symbol_id);
              EXPECT_FALSE(predicate.value.negated);
              EXPECT_NE(predicate.loc, SourceRange{});
              EXPECT_NE(view->paired->value.data->loc, SourceRange{});
              EXPECT_FALSE(view->paired->locs.empty());
            } else {
              destination = &view->bare->value;
              EXPECT_FALSE(view->bare->locs.empty());
            }
            EXPECT_EQ(destination->spelling, "%b0");
            EXPECT_EQ(destination->declared_type, ScalarType::B32);
            EXPECT_TRUE(destination->symbol_id);
            EXPECT_EQ(view->source->value.spelling, "%b1");
            EXPECT_EQ(view->source->value.declared_type, ScalarType::B32);
            EXPECT_TRUE(view->source->value.symbol_id);
            EXPECT_FALSE(view->source->locs.empty());
            for (const auto& control :
                 {std::pair{view->lane, register_lane},
                  std::pair{view->clamp, register_clamp}}) {
              EXPECT_FALSE(control.first->locs.empty());
              EXPECT_EQ(std::holds_alternative<ResolvedRegisterRef>(
                            control.first->value),
                        control.second);
              if (control.second) {
                const auto& reg =
                    std::get<ResolvedRegisterRef>(control.first->value);
                EXPECT_EQ(reg.declared_type, ScalarType::U32);
                EXPECT_EQ(reg.spelling,
                          control.first == view->lane ? "%u0" : "%u1");
                EXPECT_TRUE(reg.symbol_id);
              } else {
                const auto& immediate =
                    std::get<ResolvedImmediate>(control.first->value);
                EXPECT_EQ(immediate.type, ScalarType::U32);
                EXPECT_EQ(immediate.bits,
                          control.first == view->lane ? 1u : 31u);
              }
            }
            auto clone = instruction.clone();
            EXPECT_EQ(clone->instruction_kind(), item.kind);
            const auto cloned = legacyShuffleView(*clone);
            ASSERT_TRUE(cloned);
            EXPECT_EQ(cloned->paired != nullptr, paired);
            EXPECT_EQ(cloned->source->value.spelling, "%b1");
            EXPECT_EQ(cloned->lane->value, view->lane->value);
            EXPECT_EQ(cloned->clamp->value, view->clamp->value);
            if (paired) {
              EXPECT_EQ(cloned->paired->value.data, view->paired->value.data);
              EXPECT_EQ(cloned->paired->value.predicate,
                        view->paired->value.predicate);
            }
          }
}

/** The PTX floor and numeric SM bound are independent of identity and flavor. */
TEST(LegacyShuffle, RequiresPtxFloorWithoutExactIdentityCoupling) {
  for (const auto& item : kLegacyShuffles)
    for (const bool paired : {false, true}) {
      const auto source = legacyShuffleModule(
          legacyShuffleOperation(item.mode, paired, true, false), "sm_52");
      PtxSyntaxParser parser{source};
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast);
      const auto resolved = resolveModuleOnly(*ast);
      ASSERT_TRUE(resolved);
      const auto& instruction = *resolved->functions.front().body.front();
      for (const auto sm : {30u, 32u, 35u, 37u, 50u, 52u, 53u, 60u, 61u, 62u}) {
        const auto spelling = "sm_" + std::to_string(sm);
        checker::TargetInfo target{
            .ptx_version = {3, 0},
            .sm_version = sm,
            .identity = base::parse_target_identity(spelling)};
        const auto check = [&] {
          return instruction.check(checker::Context{.target = target});
        };
        EXPECT_TRUE(check());
        target.ptx_version = {2, 3};
        EXPECT_FALSE(check());
        target.ptx_version = {3, 0};
        target.identity.reset();
        EXPECT_TRUE(check());
        for (const auto suffix : {"a", "f"}) {
          target.identity = base::parse_target_identity(spelling + suffix);
          EXPECT_TRUE(check());
        }
        target.identity = base::parse_target_identity("sm_70");
        EXPECT_TRUE(check());
      }
    }
}

/** Low SM and removed modern configurations remain unavailable. */
TEST(LegacyShuffle, RejectsLowSmAndRemovedModernConfigurations) {
  for (const auto& item : kLegacyShuffles)
    for (const bool paired : {false, true}) {
      const auto operation =
          legacyShuffleOperation(item.mode, paired, false, true);
      for (const auto target : {"sm_20", "sm_70", "sm_80", "sm_100"})
        expectShuffleAvailability(legacyShuffleModule(operation, target, "9.3"),
                                  false);
      expectShuffleAvailability(legacyShuffleModule(operation, "sm_52", "2.3"),
                                false);
      const auto source = legacyShuffleModule(operation, "sm_63");
      PtxSyntaxParser parser{source};
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast);
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
}

/** Data/predicate pairing, b/c width, modifiers, and mask absence stay strict. */
TEST(LegacyShuffle, RejectsWrongTypesShapesModifiersAndCounts) {
  for (const auto& item : kLegacyShuffles)
    for (const auto operands :
         {"%wide, %b1, 1, 31",      "%half, %b1, 1, 31",
          "%p, %b1, 1, 31",         "%b0|%u0, %b1, 1, 31",
          "%wide|%p, %b1, 1, 31",   "%b0|_, %b1, 1, 31",
          "_|%p, %b1, 1, 31",       "_, %b1, 1, 31",
          "{%b0}, %b1, 1, 31",      "%b0, %wide, 1, 31",
          "%b0, %half, 1, 31",      "%b0, %p, 1, 31",
          "%b0, {%b1}, 1, 31",      "%b0, 1, 1, 31",
          "%b0, %b1, %wide, 31",    "%b0, %b1, %half, 31",
          "%b0, %b1, %p, 31",       "%b0, %b1, 1, %wide",
          "%b0, %b1, 1, %p",        "%b0, %b1, 1",
          "%b0|%p, %b1, 1",         "%b0, %b1, 1, 31, 0xffffffff",
          "%b0|%p, %b1, 1, 31, %u0"}) {
      const auto operation =
          "shfl." + std::string(item.mode) + ".b32 " + operands + ";";
      SCOPED_TRACE(operation);
      const auto source = legacyShuffleModule(operation, "sm_52", "4.1");
      PtxSyntaxParser parser{source};
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast);
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
  for (const auto operation :
       {"shfl.up.b64 %wide, %wide, 1, 31;", "shfl.down.u32 %u0, %u1, 1, 31;",
        "shfl.bfly.pred %p, %p, 1, 31;", "shfl.up.down.b32 %b0, %b1, 1, 31;",
        "shfl.up.sync.b32 %b0, %b1, 1, 31;",
        "shfl.idx.ftz.b32 %b0, %b1, 1, 31;"}) {
    SCOPED_TRACE(operation);
    const auto source = legacyShuffleModule(operation, "sm_52", "4.1");
    PtxSyntaxParser parser{source};
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}

/** A recoverable AST does not erase the syntax rejection of a negated output. */
TEST(LegacyShuffle, RejectsNegatedDestinationAtSyntaxBoundary) {
  for (const auto& item : kLegacyShuffles)
    for (const bool synchronized : {false, true}) {
      const auto operation =
          std::string("shfl.") + (synchronized ? "sync." : "") +
          std::string(item.mode) + ".b32 %b0|!%p, %b1, 1, 31" +
          (synchronized ? ", 0xffffffff;" : ";");
      SCOPED_TRACE(operation);
      const auto source = legacyShuffleModule(
          operation, synchronized ? "sm_80" : "sm_52", "9.3");
      PtxSyntaxParser parser{source};
      const auto ast = parser.parseModule();
      EXPECT_FALSE(ast.diagnostics.empty());
    }
}

/** Synchronized counterparts retain masks, both layouts, and their existing gates. */
TEST(LegacyShuffle, PreservesSynchronizedLayoutsMasksAndAvailability) {
  for (const auto& item : kLegacyShuffles)
    for (const bool paired : {false, true}) {
      const auto operands =
          std::string(".b32 %b0") + (paired ? "|%p" : "") + ", %b1, 1, 31";
      const auto operation =
          "shfl.sync." + std::string(item.mode) + operands + ", 0xffffffff;";
      expectShuffleAvailability(legacyShuffleModule(operation, "sm_30", "6.0"),
                                true);
      expectShuffleAvailability(legacyShuffleModule(operation, "sm_80", "7.0"),
                                true);
      expectShuffleAvailability(legacyShuffleModule(operation, "sm_30", "5.0"),
                                false);
      expectShuffleAvailability(legacyShuffleModule(operation, "sm_20", "6.0"),
                                false);
      const auto missing_mask = legacyShuffleModule(
          "shfl.sync." + std::string(item.mode) + operands + ";", "sm_80",
          "9.3");
      PtxSyntaxParser parser{missing_mask};
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast);
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
}

/** AST retargeting rejects availability without rewriting owned legacy identity. */
TEST(LegacyShuffle, RetargetsWithoutAddingSyncOrMask) {
  for (const auto& item : kLegacyShuffles)
    for (const bool paired : {false, true}) {
      const auto operation =
          legacyShuffleOperation(item.mode, paired, false, true);
      const auto source = legacyShuffleModule(operation, "sm_52", "4.1");
      const auto modern_source = legacyShuffleModule(operation, "sm_80", "9.3");
      PtxSyntaxParser parser{source};
      PtxSyntaxParser modern_parser{modern_source};
      const auto ast = parser.parseModule();
      const auto modern = modern_parser.parseModule();
      ASSERT_TRUE(ast);
      ASSERT_TRUE(modern);
      const auto resolved = resolveAndValidateModule(*ast);
      ASSERT_TRUE(resolved);
      EXPECT_FALSE(validateModule(*modern, *resolved));
      EXPECT_TRUE(validateModule(*resolved));
      const auto& instruction = *resolved->functions.front().body.front();
      EXPECT_EQ(instruction.instruction_kind(), item.kind);
      ASSERT_TRUE(legacyShuffleView(instruction));
      EXPECT_EQ(legacyShuffleView(instruction)->paired != nullptr, paired);
    }
}

/** All eight source forms share the complete historical window under real recognized profiles. */
TEST(LegacyShuffle, CompleteHistoricalWindowAllEightForms) {
  for (const auto& item : kLegacyShuffles)
    for (bool paired : {false, true}) {
      auto op = legacyShuffleOperation(item.mode, paired, false, true);
      for (const auto& cell : {std::tuple{"3.0", "sm_30", true},
                               {"3.0", "sm_20", false},
                               {"6.0", "sm_70", true},
                               {"6.3", "sm_70", true},
                               {"6.3", "sm_75", true},
                               {"6.4", "sm_70", false},
                               {"6.4", "sm_75", false},
                               {"6.4", "sm_62", true},
                               {"9.3", "sm_62", true},
                               {"9.3", "sm_90", false}}) {
        const auto& [version, target, expected] = cell;
        SCOPED_TRACE(op + version + target);
        auto source = legacyShuffleModule(op, target, version);
        PtxSyntaxParser p(source);
        auto ast = p.parseModule();
        ASSERT_TRUE(ast);
        auto m = resolveModuleOnly(*ast);
        ASSERT_TRUE(m) << m.error().front().message;
        EXPECT_EQ(bool(resolveAndValidateModule(*ast)), expected);
        for (auto policy : {ModuleValidationPolicy::AvailableContext,
                            ModuleValidationPolicy::RequireCompleteContext})
          for (auto result :
               {validateModule(*m, policy), validateModule(*ast, *m, policy)}) {
            EXPECT_EQ(bool(result), expected);
            if (!expected) {
              ASSERT_FALSE(result);
              EXPECT_EQ(result.error().front().kind,
                        checker::CheckDiagnosticKind::UnsupportedAvailability);
            }
          }
      }
    }
}

/** Numeric-bound units intentionally include synthetic SM29/69 rather than source profile claims. */
TEST(LegacyShuffle, ExactNumericPredicateWithoutIdentityOrFlavorCoupling) {
  for (const auto& item : kLegacyShuffles)
    for (bool paired : {false, true}) {
      auto op = legacyShuffleOperation(item.mode, paired, false, false);
      auto source = legacyShuffleModule(op, "sm_62", "6.4");
      PtxSyntaxParser parser(source);
      auto a = parser.parseModule();
      ASSERT_TRUE(a);
      auto m = resolveModuleOnly(*a);
      ASSERT_TRUE(m);
      for (auto v : {checker::PtxVersion{2, 3}, {3, 0}, {6, 3}, {6, 4}, {9, 3}})
        for (uint32_t sm : {20u, 29u, 30u, 62u, 69u, 70u, 75u}) {
          const bool expected = v >= checker::PtxVersion{3, 0} && sm >= 30 &&
                                (v < checker::PtxVersion{6, 4} || sm < 70);
          checker::Context c{.target = {.ptx_version = v, .sm_version = sm}};
          EXPECT_EQ(bool(m->functions.front().body.front()->check(c)),
                    expected);
          for (auto flavor : {base::TargetFlavor::ArchitectureSpecific,
                              base::TargetFlavor::FamilySpecific}) {
            c.target.identity = base::TargetIdentity{{sm}, flavor, {}};
            EXPECT_EQ(bool(m->functions.front().body.front()->check(c)),
                      expected);
          }
        }
    }
}

/** Retarget context coherently without changing the source identity or introducing history. */
void shuffleContext(ResolvedModule& m,
                    std::optional<checker::PtxVersion> version,
                    std::optional<std::string> target) {
  for (auto& r : m.header.regions) {
    r.version = version;
    r.version_provenance = version ? SourceConfigurationProvenance::Explicit
                                   : SourceConfigurationProvenance::Missing;
    r.target_options.clear();
    if (target)
      r.target_options.push_back(*target);
    r.target_provenance = target ? SourceConfigurationProvenance::Explicit
                                 : SourceConfigurationProvenance::Missing;
  }
  for (auto& f : m.functions) {
    f.source_version = version;
    f.source_target = target;
  }
}

/** Source-dead copies, assignment and cloned instructions retain the same current-context predicate. */
TEST(LegacyShuffle, CurrentAndSuppliedContextsAcrossRemovalAfterSourceDeath) {
  for (const auto& item : kLegacyShuffles)
    for (bool paired : {false, true}) {
      auto op = legacyShuffleOperation(item.mode, paired, false, true);
      std::optional<ResolvedModule> owned;
      {
        auto source = legacyShuffleModule(op, "sm_75", "6.3");
        PtxSyntaxParser p(source);
        auto ast = p.parseModule();
        ASSERT_TRUE(ast);
        auto m = resolveAndValidateModule(*ast);
        ASSERT_TRUE(m);
        owned = std::move(*m);
      }
      auto copy = *owned;
      ResolvedModule assigned;
      assigned = copy;
      auto moved = std::move(assigned);
      for (auto& f : moved.functions)
        for (auto& i : f.body)
          i = i->clone();
      EXPECT_EQ(moved.functions.front().instruction_ranges,
                owned->functions.front().instruction_ranges);
      EXPECT_TRUE(validateModule(moved));
      auto supplied_source = legacyShuffleModule(op, "sm_75", "6.4");
      PtxSyntaxParser p(supplied_source);
      auto ast = p.parseModule();
      ASSERT_TRUE(ast);
      for (auto policy : {ModuleValidationPolicy::AvailableContext,
                          ModuleValidationPolicy::RequireCompleteContext}) {
        auto result = validateModule(*ast, moved, policy);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().front().kind,
                  checker::CheckDiagnosticKind::UnsupportedAvailability);
      }
      shuffleContext(moved, checker::PtxVersion{6, 4}, "sm_75");
      EXPECT_FALSE(validateModule(moved));
      shuffleContext(moved, checker::PtxVersion{6, 4}, "sm_62");
      EXPECT_TRUE(validateModule(moved));
      shuffleContext(moved, checker::PtxVersion{6, 4}, "sm_70");
      EXPECT_FALSE(validateModule(moved));
      shuffleContext(moved, checker::PtxVersion{6, 3}, "sm_75");
      EXPECT_TRUE(validateModule(moved));
      EXPECT_TRUE(validateModule(*owned));
    }
}

/** Missing instruction context defers the whole availability predicate; unknown profiles remain errors. */
TEST(LegacyShuffle, PartialContextDeferralStrictAndUnknownTarget) {
  auto op = legacyShuffleOperation("up", false, false, true);
  for (int mode : {0, 1, 2}) {
    auto source = legacyShuffleModule(op, "sm_75", "6.3");
    PtxSyntaxParser p(source);
    auto ast = p.parseModule();
    ASSERT_TRUE(ast);
    auto m = resolveAndValidateModule(*ast);
    ASSERT_TRUE(m);
    auto version = mode == 0   ? std::optional<checker::PtxVersion>{{6, 4}}
                   : mode == 1 ? std::optional<checker::PtxVersion>{{2, 3}}
                               : std::nullopt;
    auto target =
        mode == 2 ? std::optional<std::string>{"sm_20"} : std::nullopt;
    shuffleContext(*m, version, target);
    auto partial_source = legacyShuffleModule(op, target ? *target : "sm_75",
                                              mode == 1 ? "2.3" : "6.4");
    if (!target) {
      auto start = partial_source.find(".target");
      partial_source.erase(start, partial_source.find('\n', start) - start + 1);
    } else
      partial_source.erase(0, partial_source.find('\n') + 1);
    PtxSyntaxParser pp(partial_source);
    auto supplied = pp.parseModule();
    ASSERT_TRUE(supplied);
    EXPECT_TRUE(validateModule(*m, ModuleValidationPolicy::AvailableContext));
    EXPECT_TRUE(validateModule(*supplied, *m,
                               ModuleValidationPolicy::AvailableContext));
    for (auto result :
         {validateModule(*m, ModuleValidationPolicy::RequireCompleteContext),
          validateModule(*supplied, *m,
                         ModuleValidationPolicy::RequireCompleteContext)}) {
      ASSERT_FALSE(result);
      EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& d) {
        return d.kind == checker::CheckDiagnosticKind::MissingValidationContext;
      }));
    }
  }
  auto unknown_source = legacyShuffleModule(op, "sm_999", "6.4");
  PtxSyntaxParser p(unknown_source);
  auto a = p.parseModule();
  ASSERT_TRUE(a);
  auto m = resolveModuleOnly(*a);
  ASSERT_TRUE(m);
  for (auto policy : {ModuleValidationPolicy::AvailableContext,
                      ModuleValidationPolicy::RequireCompleteContext})
    for (auto result :
         {validateModule(*m, policy), validateModule(*a, *m, policy)}) {
      ASSERT_FALSE(result);
      EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& d) {
        return d.kind == checker::CheckDiagnosticKind::UnknownTarget;
      }));
    }
  for (auto source : {legacyShuffleModule(op, "sm_70", "3.0"),
                      legacyShuffleModule(op, "sm_75", "6.0")}) {
    PtxSyntaxParser pp(source);
    auto aa = pp.parseModule();
    ASSERT_TRUE(aa);
    EXPECT_FALSE(resolveAndValidateModule(*aa));
  }
}

/** A valid original AST cannot hide a currently absent required destination. */
TEST(LegacyShuffle, RechecksCurrentPayloadWithOriginalValidAst) {
  auto source = legacyShuffleModule(
      legacyShuffleOperation("up", false, false, true), "sm_75", "6.3");
  PtxSyntaxParser p(source);
  auto a = p.parseModule();
  ASSERT_TRUE(a);
  auto m = resolveAndValidateModule(*a);
  ASSERT_TRUE(m);
  auto* form =
      dynamic_cast<ShflUpB32*>(m->functions.front().body.front().get());
  ASSERT_NE(form, nullptr);
  form->dst_register.reset();
  for (auto policy : {ModuleValidationPolicy::AvailableContext,
                      ModuleValidationPolicy::RequireCompleteContext})
    for (auto result :
         {validateModule(*m, policy), validateModule(*a, *m, policy)}) {
      ASSERT_FALSE(result);
      EXPECT_TRUE(std::ranges::any_of(result.error(), [](const auto& d) {
        return d.kind == checker::CheckDiagnosticKind::MissingOperand ||
               d.kind ==
                   checker::CheckDiagnosticKind::OperandLayoutPayloadMismatch ||
               d.kind == checker::CheckDiagnosticKind::UnsupportedOperandShape;
      }));
    }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
