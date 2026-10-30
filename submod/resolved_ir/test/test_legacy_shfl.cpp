#include <gtest/gtest.h>

#include <array>
#include <concepts>
#include <optional>
#include <string>
#include <string_view>

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

/** Exact identity, target flavor and PTX floor are independently required. */
TEST(LegacyShuffle, RequiresExactIdentityFlavorAndPtxFloor) {
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
        EXPECT_FALSE(check());
        for (const auto suffix : {"a", "f"}) {
          target.identity = base::parse_target_identity(spelling + suffix);
          EXPECT_FALSE(check());
        }
        target.identity = base::parse_target_identity("sm_70");
        EXPECT_FALSE(check());
      }
    }
}

/** Unsupported targets do not broaden the finite legacy catalog. */
TEST(LegacyShuffle, RejectsTargetsOutsideExactDomain) {
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

}  // namespace
}  // namespace ptx_frontend::resolved_ir
