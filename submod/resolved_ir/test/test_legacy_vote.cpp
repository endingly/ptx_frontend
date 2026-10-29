#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <ptx_frontend/resolved_ir/model/parallel_synchronization_and_communication/vote.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Detect that legacy forms expose neither a fabricated sync flag nor a mask. */
template <typename T>
concept MaskFreeVote = !requires(T instruction) { instruction.sync; } &&
                       !requires(T instruction) { instruction.membermask; };
static_assert(MaskFreeVote<VoteAllPred> && MaskFreeVote<VoteAnyPred> &&
              MaskFreeVote<VoteUniPred> && MaskFreeVote<VoteBallotB32>);
static_assert(VoteAllPred::all && VoteAnyPred::any && VoteUniPred::uni &&
              VoteBallotB32::ballot);

/** Source mode and distinct generated identity for one legacy vote form. */
struct LegacyVoteCase {
  /** Written mode, used only to construct test source. */
  std::string_view mode;
  /** Generated source-form identity expected after resolution. */
  InstructionKind kind;
  /** True for the 32-bit mask result, false for predicate results. */
  bool ballot;
};
/** Complete legacy mode inventory with distinct source identities. */
constexpr std::array kLegacyVotes{
    LegacyVoteCase{"all", VoteAllPred::kind, false},
    LegacyVoteCase{"any", VoteAnyPred::kind, false},
    LegacyVoteCase{"uni", VoteUniPred::kind, false},
    LegacyVoteCase{"ballot", VoteBallotB32::kind, true}};

/** Borrow typed vote payloads only while the owning instruction is alive. */
struct LegacyVoteView {
  /** Borrowed predicate value and its owned source ranges. */
  const WithLocs<ResolvedPredicate>* predicate;
  /** Borrowed destination register, including symbol identity and declared type. */
  const ResolvedRegisterRef* destination;
  /** Borrowed destination ranges for this resolved payload. */
  std::span<const SourceRange> destination_ranges;
  /** Fixed result type of the concrete generated form. */
  ScalarType type;
};

/** Inspect all four concrete forms without accepting synchronized counterparts. */
std::optional<LegacyVoteView> legacyVoteView(const Instruction& instruction) {
  if (const auto* vote = dynamic_cast<const VoteAllPred*>(&instruction))
    return LegacyVoteView{&vote->predicate, &vote->dst.value.register_ref,
                          vote->dst.locs, VoteAllPred::type};
  if (const auto* vote = dynamic_cast<const VoteAnyPred*>(&instruction))
    return LegacyVoteView{&vote->predicate, &vote->dst.value.register_ref,
                          vote->dst.locs, VoteAnyPred::type};
  if (const auto* vote = dynamic_cast<const VoteUniPred*>(&instruction))
    return LegacyVoteView{&vote->predicate, &vote->dst.value.register_ref,
                          vote->dst.locs, VoteUniPred::type};
  if (const auto* vote = dynamic_cast<const VoteBallotB32*>(&instruction))
    return LegacyVoteView{&vote->predicate, &vote->dst.value, vote->dst.locs,
                          VoteBallotB32::type};
  return std::nullopt;
}

/** Construct a two-operand vote with an explicitly retained source complement. */
std::string legacyVoteOperation(const LegacyVoteCase& item, bool negated) {
  return "vote." + std::string(item.mode) +
         (item.ballot ? ".b32 %b, " : ".pred %p1, ") +
         (negated ? "!%p0;" : "%p0;");
}

/** Build a complete source region with typed predicate and width controls. */
std::string legacyVoteModule(std::string_view operation,
                             std::string_view target,
                             std::string_view version = "1.2") {
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) +
         "\n.func f() { .reg .pred %p<2>; .reg .b32 %b; "
         ".reg .u32 %u; .reg .b64 %wide; " +
         std::string(operation) + " }\n";
}

/** Require AST-backed and strict owned checks to agree on legal source forms. */
void expectVoteAvailability(const std::string& source, bool available) {
  PtxSyntaxParser parser{source};
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast);
  ASSERT_TRUE(ast.diagnostics.empty());
  const auto resolved = resolveModuleOnly(*ast);
  ASSERT_TRUE(resolved) << resolved.error().front().message;
  EXPECT_EQ(validateModule(*ast, *resolved).has_value(), available);
  EXPECT_EQ(validateModule(*resolved).has_value(), available);
}

/** All catalog domains and both source-complement states survive AST destruction. */
TEST(LegacyVote, OwnsEveryModeTargetAndPredicateComplement) {
  for (const auto& item : kLegacyVotes) {
    for (const auto sm :
         {12u, 13u, 20u, 30u, 32u, 35u, 37u, 50u, 52u, 53u, 60u, 61u, 62u}) {
      if (item.ballot && sm < 20)
        continue;
      const auto target = "sm_" + std::to_string(sm);
      for (const bool negated : {false, true}) {
        SCOPED_TRACE(item.mode);
        SCOPED_TRACE(target);
        std::optional<ResolvedModule> owned;
        {
          const auto source = legacyVoteModule(
              legacyVoteOperation(item, negated), target, "9.3");
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
        ASSERT_EQ(function.body.size(), 1u);
        const auto& instruction = *function.body.front();
        EXPECT_EQ(instruction.instruction_kind(), item.kind);
        EXPECT_EQ(function.instruction_opcodes.front(), "vote");
        EXPECT_EQ(function.source_target, target);
        const auto view = legacyVoteView(instruction);
        ASSERT_TRUE(view);
        EXPECT_EQ(view->type, item.ballot ? ScalarType::B32 : ScalarType::Pred);
        EXPECT_EQ(view->destination->declared_type, view->type);
        EXPECT_TRUE(view->destination->symbol_id);
        EXPECT_FALSE(view->destination_ranges.empty());
        EXPECT_EQ(view->predicate->value.negated, negated);
        EXPECT_EQ(view->predicate->value.register_ref.spelling, "%p0");
        EXPECT_EQ(view->predicate->value.register_ref.declared_type,
                  ScalarType::Pred);
        EXPECT_TRUE(view->predicate->value.register_ref.symbol_id);
        EXPECT_FALSE(view->predicate->locs.empty());
        auto clone = instruction.clone();
        EXPECT_EQ(clone->instruction_kind(), item.kind);
        const auto cloned = legacyVoteView(*clone);
        ASSERT_TRUE(cloned);
        EXPECT_EQ(cloned->predicate->value.negated, negated);
      }
    }
  }
}

/** Exact identity/flavor and PTX1.2 are required independently for every mode. */
TEST(LegacyVote, RequiresExactIdentityFlavorAndPtxFloor) {
  for (const auto& item : kLegacyVotes) {
    SCOPED_TRACE(item.mode);
    const auto source =
        legacyVoteModule(legacyVoteOperation(item, true), "sm_52");
    PtxSyntaxParser parser{source};
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    const auto resolved = resolveModuleOnly(*ast);
    ASSERT_TRUE(resolved) << resolved.error().front().message;
    const auto& instruction = *resolved->functions.front().body.front();
    for (const auto sm :
         {12u, 13u, 20u, 30u, 32u, 35u, 37u, 50u, 52u, 53u, 60u, 61u, 62u}) {
      if (item.ballot && sm < 20)
        continue;
      const auto spelling = "sm_" + std::to_string(sm);
      checker::TargetInfo target{
          .ptx_version = {1, 2},
          .sm_version = sm,
          .identity = base::parse_target_identity(spelling)};
      const auto check = [&] {
        return instruction.check(checker::Context{.target = target});
      };
      EXPECT_TRUE(check());
      target.ptx_version = {1, 1};
      EXPECT_FALSE(check());
      target.ptx_version = {1, 2};
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

/** Unsupported old/modern domains reject without inferring historical exceptions. */
TEST(LegacyVote, RejectsTargetsOutsideEachExactDomain) {
  for (const auto& item : kLegacyVotes) {
    const auto operation = legacyVoteOperation(item, false);
    for (const auto target : {"sm_10", "sm_11", "sm_70", "sm_80", "sm_100"})
      expectVoteAvailability(legacyVoteModule(operation, target, "9.3"), false);
    if (item.ballot)
      for (const auto target : {"sm_12", "sm_13"})
        expectVoteAvailability(legacyVoteModule(operation, target), false);
    expectVoteAvailability(legacyVoteModule(operation, "sm_70", "6.3"), false);
    expectVoteAvailability(legacyVoteModule(operation, "sm_52", "1.1"), false);
  }
}

/** Result types, operand counts, mask absence and predicate shapes remain strict. */
TEST(LegacyVote, RejectsWrongTypesWidthsMasksAndShapes) {
  for (const auto operation : {
           "vote.all.pred %b, %p0;",
           "vote.any.pred %wide, %p0;",
           "vote.uni.pred !%p1, %p0;",
           "vote.ballot.b32 %wide, %p0;",
           "vote.ballot.b32 %p1, %p0;",
           "vote.ballot.b64 %wide, %p0;",
           "vote.all.b32 %b, %p0;",
           "vote.all.pred %p1, %u;",
           "vote.any.pred %p1, 1;",
           "vote.uni.pred %p1, {%p0};",
           "vote.all.pred _, %p0;",
           "vote.ballot.b32 _, %p0;",
           "vote.all.pred %p1, %p0, 0xffffffff;",
           "vote.any.pred %p1, %p0, %u;",
           "vote.uni.pred %p1, %p0, 0;",
           "vote.ballot.b32 %b, %p0, 0xffffffff;",
           "vote.all.pred %p1;",
           "vote.ballot.b32 %b;",
           "vote.all.any.pred %p1, %p0;",
           "vote.all.sync.pred %p1, %p0;",
       }) {
    SCOPED_TRACE(operation);
    const auto source = legacyVoteModule(operation, "sm_20");
    PtxSyntaxParser parser{source};
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}

/** Synchronized forms keep their mandatory masks and PTX6.0/SM30 boundaries. */
TEST(LegacyVote, PreservesSynchronizedMasksAndAvailability) {
  for (const auto& item : kLegacyVotes) {
    const auto operation = "vote.sync." + std::string(item.mode) +
                           (item.ballot ? ".b32 %b, !%p0, 0xffffffff;"
                                        : ".pred %p1, !%p0, 0xffffffff;");
    expectVoteAvailability(legacyVoteModule(operation, "sm_30", "6.0"), true);
    expectVoteAvailability(legacyVoteModule(operation, "sm_80", "6.0"), true);
    expectVoteAvailability(legacyVoteModule(operation, "sm_30", "5.9"), false);
    expectVoteAvailability(legacyVoteModule(operation, "sm_20", "6.0"), false);
    const auto missing_mask =
        "vote.sync." + std::string(item.mode) +
        (item.ballot ? ".b32 %b, %p0;" : ".pred %p1, %p0;");
    const auto source = legacyVoteModule(missing_mask, "sm_80", "9.3");
    PtxSyntaxParser parser{source};
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
}

/** Checker retargeting never rewrites the owned unsynchronized form. */
TEST(LegacyVote, RetargetsWithoutAddingSyncOrMask) {
  for (const auto& item : kLegacyVotes) {
    const auto operation = legacyVoteOperation(item, true);
    const auto source = legacyVoteModule(operation, "sm_52");
    const auto modern_source = legacyVoteModule(operation, "sm_80", "9.3");
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
    ASSERT_TRUE(legacyVoteView(instruction));
    EXPECT_TRUE(legacyVoteView(instruction)->predicate->value.negated);
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
