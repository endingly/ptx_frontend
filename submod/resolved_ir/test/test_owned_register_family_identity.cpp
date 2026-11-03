#include <gtest/gtest.h>

#include <array>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {
/** One lawful register source, independent of any future immediate permission. */
struct RegisterFamilyCase {
  /** Stable operation label and its single designated lexical operand. */
  std::string_view name, operation;
  /** Source field position within the existing generated class. */
  unsigned position;
};
constexpr std::array family_cases{
    RegisterFamilyCase{"fma_src1", "fma.rn.f32.bf16 f,$,b2,f;", 1},
    RegisterFamilyCase{"fma_src2", "fma.rn.f32.bf16 f,b1,$,f;", 2},
    RegisterFamilyCase{"set_src1", "set.eq.u32.bf16 u,$,b2;", 1},
    RegisterFamilyCase{"set_src2", "set.eq.u32.bf16 u,b1,$;", 2},
    RegisterFamilyCase{"set_boolean_src1", "set.eq.and.u32.bf16 u,$,b2,p;", 1},
    RegisterFamilyCase{"set_boolean_src2", "set.eq.and.u32.bf16 u,b1,$,p;", 2},
};

/** Construct identical declarations for family members and real plain-register donors. */
std::string family_source(const RegisterFamilyCase& item,
                          std::string_view operand = "%r0") {
  std::string operation(item.operation);
  operation.replace(operation.find('$'), 1, operand);
  return ".version 9.3\n.target sm_100a\n.entry kernel(){ .reg .b16 b1,b2; "
         ".reg .f32 f; .reg .u32 u; .reg .pred p; .reg .b16 %r<2>; " +
         operation + " }";
}

/** Borrow the current register-or-immediate field only while its complete module owns it. */
WithLocs<RegOrImm>& family_field(Instruction& instruction, unsigned position) {
  if (auto* value = dynamic_cast<FmaMixedF32Bf16*>(&instruction))
    return position == 1 ? value->src1 : value->src2;
  if (auto* value = dynamic_cast<SetHalfIntegerBf16*>(&instruction))
    return position == 1 ? value->src1 : value->src2;
  if (auto* value = dynamic_cast<SetHalfIntegerBf16Boolean*>(&instruction))
    return position == 1 ? value->src1 : value->src2;
  throw std::logic_error("Expected a canonical BF16 register-source fixture.");
}

/** Return a real owning module after input text, parser and producer AST die. */
ResolvedModule own_family(std::string text, bool partial_context = false) {
  PtxSyntaxParser parser(text);
  const auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    throw std::runtime_error("Register family fixture failed syntax parsing.");
  auto module = partial_context ? resolveModuleOnly(*ast)
                                : resolveAndValidateModule(*ast);
  if (!module)
    throw std::runtime_error(
        module.error().empty() ? "Register family resolution has no diagnostic."
                               : module.error().front().message);
  if (module->functions.size() != 1 ||
      module->functions.front().body.size() != 1)
    throw std::runtime_error(
        "Register family fixture needs one real instruction.");
  return std::move(*module);
}

/** Require the exact saved use location and existing metadata diagnostic cause. */
void expect_family_rejection(const checker::CheckResult& checked,
                             SourceRange range, bool overflow) {
  ASSERT_FALSE(checked);
  ASSERT_FALSE(checked.error().empty());
  EXPECT_TRUE(std::ranges::any_of(checked.error(), [&](const auto& diagnostic) {
    return diagnostic.kind ==
               checker::CheckDiagnosticKind::ModuleSourceMismatch &&
           diagnostic.range == range &&
           diagnostic.message.find(
               overflow ? "invalid parameterized member index"
                        : "register metadata disagrees with its owned "
                          "declaration") != std::string::npos;
  }));
}

/** Missing semantic identity cannot fall back to a numbered convenience cache. */
TEST(OwnedRegisterFamilyIdentity,
     RejectsMissingConflictingAndOverflowingMembers) {
  for (const auto& item : family_cases) {
    SCOPED_TRACE(item.name);
    const auto text = family_source(item);
    PtxSyntaxParser parser(text);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    const auto baseline = own_family(text);
    ASSERT_TRUE(validateModule(baseline));
    ASSERT_TRUE(validateModule(*ast, baseline));
    for (unsigned mutation = 0; mutation < 4; ++mutation) {
      SCOPED_TRACE(mutation);
      auto current = baseline;
      auto& field =
          family_field(*current.functions.front().body.front(), item.position);
      ASSERT_EQ(field.locs.size(), 1u);
      const auto range = field.locs.front();
      auto& ref = std::get<ResolvedRegisterRef>(field.value);
      ASSERT_TRUE(ref.symbol_id);
      ASSERT_EQ(ref.parameterized_index, 0u);
      ASSERT_EQ(ref.index, 0u);
      const auto& symbol = current.symbols.symbol(*ref.symbol_id);
      ASSERT_EQ(symbol.parameterized_count, 2u);
      if (mutation == 0)
        ref.parameterized_index.reset();
      if (mutation == 1)
        ref.spelling = "%r1";
      if (mutation == 2)
        ref.parameterized_index = 1;
      if (mutation == 3)
        ref.parameterized_index = 2;
      EXPECT_EQ(current.source_identity, baseline.source_identity);
      expect_family_rejection(validateModule(current), range, mutation == 3);
      expect_family_rejection(validateModule(*ast, current), range,
                              mutation == 3);
    }
  }
}

/** Current coherent retargets preserve identity without authenticating old spelling. */
TEST(OwnedRegisterFamilyIdentity,
     AcceptsCoherentRetargetNumberedErasureAndPlainRefs) {
  for (const auto& item : family_cases) {
    SCOPED_TRACE(item.name);
    const auto text = family_source(item);
    PtxSyntaxParser parser(text);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    const auto baseline = own_family(text);
    const auto plain = own_family(family_source(item, "b1"));
    for (unsigned mutation = 0; mutation < 4; ++mutation) {
      auto current = baseline;
      auto& ref = std::get<ResolvedRegisterRef>(
          family_field(*current.functions.front().body.front(), item.position)
              .value);
      if (mutation < 2) {
        ref.spelling = "%r1";
        ref.parameterized_index = 1;
        ref.index = 1;
      }
      if (mutation == 1 || mutation == 2)
        ref.index.reset();
      if (mutation == 3) {
        ref = std::get<ResolvedRegisterRef>(
            family_field(*plain.functions.front().body.front(), item.position)
                .value);
        ASSERT_FALSE(ref.parameterized_index);
      }
      EXPECT_EQ(current.source_identity, baseline.source_identity);
      EXPECT_TRUE(validateModule(current));
      EXPECT_TRUE(validateModule(*ast, current));
      auto copied = current;
      ResolvedModule assigned;
      assigned = copied;
      ResolvedFunction function = assigned.functions.front(), function_assigned;
      function_assigned = function;
      assigned.functions.front() = function_assigned;
      assigned.functions.front().body.front() =
          function_assigned.body.front()->clone();
      EXPECT_TRUE(validateModule(copied));
      EXPECT_TRUE(validateModule(assigned));
      EXPECT_TRUE(validateModule(*ast, assigned));
    }
  }
}

/** Known identity contradictions reject before optional target-dependent checks defer. */
TEST(OwnedRegisterFamilyIdentity, ChecksCurrentFactsWithMissingHeaderOrTarget) {
  for (const auto& item : family_cases)
    for (unsigned missing = 0; missing < 3; ++missing) {
      SCOPED_TRACE(item.name);
      SCOPED_TRACE(missing);
      auto text = family_source(item);
      if (missing != 1)
        text.erase(text.find(".version 9.3\n"),
                   std::string_view(".version 9.3\n").size());
      if (missing != 0)
        text.erase(text.find(".target sm_100a\n"),
                   std::string_view(".target sm_100a\n").size());
      PtxSyntaxParser parser(text);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      const auto baseline = own_family(text, true);
      constexpr auto available = ModuleValidationPolicy::AvailableContext;
      ASSERT_TRUE(validateModule(baseline, available));
      ASSERT_TRUE(validateModule(*ast, baseline, available));
      for (bool erase : {false, true}) {
        auto current = baseline;
        auto& field = family_field(*current.functions.front().body.front(),
                                   item.position);
        ASSERT_EQ(field.locs.size(), 1u);
        if (erase)
          std::get<ResolvedRegisterRef>(field.value)
              .parameterized_index.reset();
        else
          std::get<ResolvedRegisterRef>(field.value).spelling = "%r1";
        expect_family_rejection(validateModule(current, available),
                                field.locs.front(), false);
        expect_family_rejection(validateModule(*ast, current, available),
                                field.locs.front(), false);
      }
    }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
