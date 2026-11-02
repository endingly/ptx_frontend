#include <gtest/gtest.h>
#include <array>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include "test_modifier_candidate_plan.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Compare independent indexed selection against the unchanged public matcher. */
void compare_selection(
    std::string_view source,
    const check_end::SyntaxInstructionDescriptor& descriptor) {
  PtxSyntaxParser parser{std::string(source)};
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast);
  test_helpers::OwnedCandidatePlan plan(descriptor);
  std::vector<uint64_t> scratch((descriptor.variants.size() + 63) / 64);
  const auto indexed =
      detail::select_indexed_variant(*ast, descriptor, plan.view(), scratch);
  const auto general = select_variant_name(*ast, descriptor);
  ASSERT_EQ(indexed.has_value(), general.has_value()) << source;
  if (general) {
    EXPECT_EQ(descriptor.variants[*indexed].variant_name, *general) << source;
  } else {
    EXPECT_EQ(indexed.error().message, general.error().message) << source;
    EXPECT_EQ(indexed.error().range, general.error().range) << source;
  }
  // Independently accepted forms must survive every spelling intersection.
  for (size_t form = 0; form < descriptor.variants.size(); ++form) {
    if (!collect_actual_modifiers(*ast, descriptor.variants[form]))
      continue;
    for (const auto& modifier : ast->modifiers) {
      const auto row =
          std::ranges::find(plan.rows, modifier.syntax.text,
                            &detail::ModifierCandidateRow::spelling);
      ASSERT_NE(row, plan.rows.end());
      const size_t word =
          (row - plan.rows.begin()) * scratch.size() + form / 64;
      EXPECT_NE(plan.words[word] & (uint64_t{1} << (form % 64)), 0u);
    }
  }
}

/** Cover repeated required spellings, operations, optional slots, and diagnostic precedence. */
TEST(IndexedSelection, DifferentialRealCatalogs) {
  for (auto source :
       {"mov.u32 %r0, %r1;", "mov.u32 %r0;", "mov.u32.u32 %r0, %r1;",
        "mov.bad.u32 %r0, %r1;", "mov.u32.bad %r0, %r1;", "mov %r0, %r1;"})
    compare_selection(source, mov_syntax_descriptor());
  for (auto source :
       {"wgmma.fence.sync.aligned;", "wgmma.commit_group.sync.aligned;",
        "wgmma.wait_group.sync.aligned 0;", "wgmma.sync.fence.aligned;",
        "wgmma.fence.sync.aligned.aligned;", "wgmma.fence.sync.bad;",
        "wgmma.mma_async.sync.aligned.m64n8k16.f16.f16.f16 {%d0,%d1}, %a, %b, "
        "1, -1, 1, 0, 1;"})
    compare_selection(source, wgmma_syntax_descriptor());
  for (auto source : {"add.u32 %r0, %r1, %r2;", "add.sat.s32 %r0, %r1, %r2;",
                      "add.rn.f32 %f0, %f1, %f2;", "add.f32.rn %f0, %f1, %f2;"})
    compare_selection(source, add_syntax_descriptor());
}

/** Exhaust short sequences against overlapping modifiers, arities, and shapes. */
TEST(IndexedSelection, ExhaustiveSyntheticSequences) {
  using namespace check_end;
  const std::array<std::string_view, 1> u32{".u32"};
  const std::array<std::string_view, 1> sat{".sat"};
  const std::array<SyntaxModifierDescriptor, 1> required{
      {{u32, PresenceRequirement::Required, "type"}}};
  const std::array<SyntaxModifierDescriptor, 2> repeated{
      {{u32, PresenceRequirement::Required, "first"},
       {u32, PresenceRequirement::Required, "second"}}};
  const std::array<SyntaxModifierDescriptor, 2> optional{
      {{sat, PresenceRequirement::Optional, "sat"},
       {u32, PresenceRequirement::Required, "type"}}};
  const std::array<SyntaxModifierDescriptor, 2> reordered{
      {optional[1], optional[0]}};
  const std::array<SyntaxModifierOrderDescriptor, 1> valid_alias{{{reordered}}};
  const std::array<SyntaxOperandSlotDescriptor, 1> identifier{
      {{OperandSyntaxShape::Identifier, OperandPresence::Required}}};
  const std::array<SyntaxOperandSlotDescriptor, 1> immediate{
      {{OperandSyntaxShape::Immediate, OperandPresence::Required}}};
  const std::array<SyntaxOperandSlotDescriptor, 2> pair{
      {identifier[0], identifier[0]}};
  const std::array<SyntaxOperandLayoutDescriptor, 1> one{
      {{"one", OperandLayoutKind::Flat, identifier}}};
  const std::array<SyntaxOperandLayoutDescriptor, 1> imm{
      {{"imm", OperandLayoutKind::Flat, immediate}}};
  const std::array<SyntaxOperandLayoutDescriptor, 1> two{
      {{"two", OperandLayoutKind::Flat, pair}}};
  const std::array<SyntaxVariantDescriptor, 5> variants{
      {{"one", required, one},
       {"immediate", required, imm},
       {"two", required, two},
       {"repeated", repeated, two},
       {"optional", optional, two, valid_alias}}};
  const SyntaxInstructionDescriptor descriptor{"mov", variants};
  constexpr std::array<std::string_view, 3> alphabet{".u32", ".sat", ".bad"};
  // Three slots plus the empty sequence bound the exhaustive input space.
  for (size_t length = 0, count = 1; length <= 3;
       ++length, count *= alphabet.size()) {
    for (size_t sequence = 0; sequence < count; ++sequence) {
      std::string modifiers;
      size_t digits = sequence;
      for (size_t position = 0; position < length; ++position) {
        modifiers += alphabet[digits % alphabet.size()];
        digits /= alphabet.size();
      }
      for (auto operands :
           {";", " %r0;", " 1;", " %r0, %r1;", " %r0, %r1, %r2;"})
        compare_selection("mov" + modifiers + operands, descriptor);
    }
  }
}

/** Legacy unknown diagnostics outrank failed candidates and duplicate diagnostics. */
TEST(IndexedSelection, FrozenUnknownDiagnostic) {
  PtxSyntaxParser parser("mov.u32.u32.bad %r0, %r1;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast);
  const auto& descriptor = mov_syntax_descriptor();
  test_helpers::OwnedCandidatePlan plan(descriptor);
  std::vector<uint64_t> scratch((descriptor.variants.size() + 63) / 64);
  const auto indexed =
      detail::select_indexed_variant(*ast, descriptor, plan.view(), scratch);
  ASSERT_FALSE(indexed);
  EXPECT_EQ(indexed.error().message, "Unknown modifier '.bad'.");
  EXPECT_EQ(indexed.error().range, ast->modifiers.back().syntax.range);
}

/** Absent is known without consuming, and alias ambiguity remains an exception. */
TEST(IndexedSelection, SyntheticSlotContracts) {
  using namespace check_end;
  const std::array<std::string_view, 1> spelling{".u32"};
  const std::array<SyntaxModifierDescriptor, 1> absent{
      {{spelling, PresenceRequirement::Absent, "forbidden"}}};
  std::array<SyntaxVariantDescriptor, 1> variants{{{"absent", absent, {}}}};
  SyntaxInstructionDescriptor descriptor{"mov", variants};
  compare_selection("mov.u32 %r0, %r1;", descriptor);
  const std::array<SyntaxModifierDescriptor, 1> canonical{
      {{spelling, PresenceRequirement::Required, "first"}}};
  const std::array<SyntaxModifierDescriptor, 1> alias{
      {{spelling, PresenceRequirement::Required, "second"}}};
  const std::array<SyntaxModifierOrderDescriptor, 1> aliases{{{alias}}};
  variants[0] = {"ambiguous", canonical, {}, aliases};
  PtxSyntaxParser parser("mov.u32 %r0, %r1;");
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast);
  test_helpers::OwnedCandidatePlan plan(descriptor);
  std::array<uint64_t, 1> scratch;
  EXPECT_THROW(select_variant_name(*ast, descriptor), ResolveException);
  EXPECT_THROW(
      detail::select_indexed_variant(*ast, descriptor, plan.view(), scratch),
      ResolveException);
  const std::array<SyntaxModifierDescriptor, 2> duplicate_slots{
      {canonical[0], canonical[0]}};
  variants[0] = {"duplicate_slots", duplicate_slots, {}};
  test_helpers::OwnedCandidatePlan duplicate_plan(descriptor);
  EXPECT_THROW(select_variant_name(*ast, descriptor), ResolveException);
  EXPECT_THROW(detail::select_indexed_variant(*ast, descriptor,
                                              duplicate_plan.view(), scratch),
               ResolveException);
}

/** The public selector keeps arbitrary caller descriptors and their opcode diagnostics. */
TEST(IndexedSelection, CustomDescriptorCompatibility) {
  auto custom = mov_syntax_descriptor();
  custom.Opcode_name = "custom";
  compare_selection("mov.u32 %r0, %r1;", custom);
  custom.Opcode_name = "mov";
  custom.variants = custom.variants.first(1);
  compare_selection("mov.u32 %r0, %r1;", custom);
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir {
namespace {
/** Preserve exact public ambiguity and private no-context behavior for the new identifier slice. */
TEST(IndexedSelection, NamedWgmmaPublicAmbiguityAndUnknownContext) {
  PtxSyntaxParser parser{std::string(
      "wgmma.mma_async.sync.aligned.m64n8k8.f32.tf32.tf32 D,A,%bdesc,1,1,-1;")};
  const auto ast = parser.parseInstruction();
  ASSERT_TRUE(ast);
  const auto& syntax = wgmma_syntax_descriptor();
  const auto& resolved = wgmma_resolved_descriptor();
  const auto public_result = select_variant_name(*ast, syntax);
  ASSERT_FALSE(public_result);
  EXPECT_EQ(public_result.error().range, ast->range);
  EXPECT_EQ(public_result.error().message,
            "Ambiguous modifier, operand-count, and syntax-shape combination "
            "for instruction 'wgmma'.");
  test_helpers::OwnedCandidatePlan plan(syntax);
  std::vector<uint64_t> scratch((syntax.variants.size() + 63) / 64);
  const auto unknown = detail::select_indexed_variant(
      *ast, syntax, plan.view(), scratch, &resolved, nullptr);
  ASSERT_FALSE(unknown);
  EXPECT_EQ(unknown.error().range, public_result.error().range);
  EXPECT_EQ(unknown.error().message, public_result.error().message);
  compare_selection(
      "wgmma.mma_async.sync.aligned.m64n8k8.f32.tf32.tf32 {%d0,%d1,%d2,%d3},"
      "{%a0,%a1,%a2,%a3},%bdesc,1,1,-1;",
      syntax);
  compare_selection(
      "wgmma.mma_async.sync.aligned.m64n8k8.f32.tf32.tf32 {%d0,%d1,%d2,%d3},"
      "%adesc,%bdesc,1,1,-1;",
      syntax);
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir

namespace ptx_frontend::resolved_ir {
namespace {
/** Actual declaration facts choose an ordinal independently of target availability. */
TEST(IndexedSelection, NamedElementActualDeclarationsAndWrongVectorRetention) {
  const std::string operation =
      "wgmma.mma_async.sync.aligned.m64n8k8.f32.tf32.tf32 "
      "{%d0,%d1,%d2,%d3},A,%bdesc,1,1,-1;";
  const auto& syntax = wgmma_syntax_descriptor();
  const auto& resolved = wgmma_resolved_descriptor();
  test_helpers::OwnedCandidatePlan plan(syntax);
  for (const auto declaration : {".reg .v4 .b32 A;", ".reg .b64 A;",
                                 ".reg .v2 .b32 A;", ".reg .v4 .b16 A;", ""}) {
    SCOPED_TRACE(declaration);
    const std::string source =
        ".entry kernel(){.reg .f32 %d<4>; .reg .b64 %bdesc; " +
        std::string(declaration) + "}";
    PtxSyntaxParser parser(source);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    const auto bound = binding::bindSymbols(*ast);
    ASSERT_TRUE(bound.diagnostics.empty());
    const auto symbol = bound.table.lookup(bound.table.moduleScope(), "kernel");
    ASSERT_TRUE(symbol);
    const auto scope = bound.table.symbol(symbol->symbol).owned_scope;
    ASSERT_TRUE(scope);
    const ResolveContext context{
        .symbols = bound.table, .scope = *scope, .function_is_entry = true};
    PtxSyntaxParser instruction_parser(operation);
    const auto instruction = instruction_parser.parseInstruction();
    ASSERT_TRUE(instruction);
    std::vector<uint64_t> scratch((syntax.variants.size() + 63) / 64);
    const auto selected = detail::select_indexed_variant(
        *instruction, syntax, plan.view(), scratch, &resolved, &context);
    if (std::string_view(declaration).empty()) {
      ASSERT_FALSE(selected);
      EXPECT_EQ(selected.error().range, instruction->range);
      EXPECT_EQ(selected.error().message,
                "Ambiguous modifier, operand-count, and syntax-shape "
                "combination for instruction 'wgmma'.");
      continue;
    }
    ASSERT_TRUE(selected) << selected.error().message;
    const bool scalar = std::string_view(declaration) == ".reg .b64 A;";
    EXPECT_EQ(syntax.variants[*selected].variant_name,
              scalar ? "MmaAsyncDenseM64n8k8F32Tf32Tf32SharedPlain"
                     : "MmaAsyncDenseM64n8k8F32Tf32Tf32RegisterPlain");
    // Resolution runs once after selection. Wrong vectors stay on the vector path.
    const auto actual = resolveInstruction(*instruction, context);
    EXPECT_EQ(bool(actual),
              scalar || std::string_view(declaration) == ".reg .v4 .b32 A;");
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
