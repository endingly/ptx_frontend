#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <concepts>
#include <expected>
#include <iostream>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/semantic/ptx_declaration_semantics.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace ptx_frontend::resolved_ir {
namespace {
/** One exact source layout; neither flag grants a sibling format. */
struct AtomicF16Case {
  /** True selects the returning atomic; false selects reduction. */
  bool atom;
  /** The global-only layout additionally binds a register cache policy. */
  bool policy;
};
constexpr std::array atomic_f16_cases{
    AtomicF16Case{true, false}, AtomicF16Case{true, true},
    AtomicF16Case{false, false}, AtomicF16Case{false, true}};
/** Replace only the source operand under a lawful complete profile. */
std::string atomicF16Text(AtomicF16Case item, std::string_view operand,
                          std::string_view version = "9.3",
                          std::string_view target = "sm_90",
                          std::string_view extra_declaration = "") {
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) +
         "\n.address_size 64\n.global .align 2 .b16 memory;\n.entry kernel(){\n"
         ".reg .b16 d,b; .reg .f16 h; .reg .b64 policy; .reg .v2 .b16 V; " +
         std::string(extra_declaration) + "\n" + (item.atom ? "atom" : "red") +
         ".global.add.noftz" + (item.policy ? ".L2::cache_hint" : "") +
         ".f16 " + (item.atom ? "d,[memory]," : "[memory],") +
         std::string(operand) + (item.policy ? ",policy;" : ";") + "\n}";
}
/** Borrow the real src member of either supported exact class. */
WithLocs<RegOrImm>& atomicF16Source(Instruction& instruction) {
  if (auto* atom = dynamic_cast<AtomGlobalAddNoftzF16*>(&instruction))
    return atom->src;
  if (auto* red = dynamic_cast<RedGlobalAddNoftzF16*>(&instruction))
    return red->src;
  throw std::logic_error("Unexpected scalar F16 atomic class.");
}
/** Release the complete input buffer, parser and AST before the owner is returned. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> ownedAtomicF16(
    std::string text) {
  PtxSyntaxParser parser(text);
  const auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    throw std::runtime_error("Scalar F16 atomic fixture syntax failed.");
  return resolveAndValidateModule(*ast);
}
/** Borrow the exact public src descriptor without synthesizing a permission or width. */
const checker::OperandDescriptor& atomicF16Binding(AtomicF16Case item) {
  const auto& descriptor =
      item.atom ? atom_resolved_descriptor() : red_resolved_descriptor();
  for (const auto& variant : descriptor.variants)
    if (variant.variant_name == "GlobalAddNoftzF16") {
      // Select the canonical no-hint or with-policy layout only after checking its extent.
      const size_t layout_index = item.policy ? 1u : 0u;
      if (variant.operand_layouts.size() <= layout_index)
        throw std::logic_error(
            "Missing requested scalar F16 atomic operand layout.");
      for (const auto& binding : variant.operand_layouts[layout_index].bindings)
        if (binding.target_field_id == "src")
          return binding;
    }
  throw std::logic_error("Missing canonical scalar F16 atomic src descriptor.");
}
/** Project a real public union member; every scalar pointer borrows its active owner. */
checker::OperandView atomicF16View(const WithLocs<RegOrImm>& source) {
  checker::OperandView view{.field_id = "src",
                            .actual_shape = checker::OperandShape::Immediate,
                            .locations = source.locs};
  if (const auto* reg = std::get_if<ResolvedRegisterRef>(&source.value)) {
    view.actual_shape = checker::OperandShape::Register;
    view.register_type = reg->declared_type;
    view.register_symbol_id = reg->symbol_id;
    view.register_class = reg->register_class;
    view.register_ref = reg;
    view.register_vector_width = reg->vector_width;
  } else {
    const auto& word = std::get<ResolvedImmediate>(source.value);
    view.immediate_type = word.type;
    view.immediate_bits = word.bits;
    view.immediate_is_negative = word.is_negative;
    view.integer_source_bits = word.integer_source_bits;
  }
  return view;
}

/** Verify actual member identity only during a synchronous reference traversal. */
struct AtomicF16Capture final : detail::IReferenceObserver {
  /** Expected member is owned by the instruction for the complete visit. */
  const WithLocs<RegOrImm>& expected;
  /** Number of matching source callbacks and accumulated mirror validity. */
  size_t matches = 0;
  bool valid = true;
  /** Bind the real field before the callback; no callback borrow escapes. */
  explicit AtomicF16Capture(const WithLocs<RegOrImm>& field)
      : expected(field) {}
  /** Compare actual union/payload/range pointers against the owning field. */
  void reg_or_imm(const RegOrImm& current, std::span<const SourceRange> ranges,
                  checker::AddressSymbolResolutionPolicy) override {
    ++matches;
    valid = valid && &current == &expected.value &&
            ranges.data() == expected.locs.data() &&
            std::ranges::equal(ranges, expected.locs);
    if (const auto* reg = std::get_if<ResolvedRegisterRef>(&expected.value))
      valid = valid && std::get_if<ResolvedRegisterRef>(&current) == reg;
    else if (const auto* word = std::get_if<ResolvedImmediate>(&expected.value))
      valid = valid && std::get_if<ResolvedImmediate>(&current) == word;
    else
      valid = false;
  }
};
static_assert(
    std::same_as<decltype(AtomGlobalAddNoftzF16{}.src), WithLocs<RegOrImm>>);
static_assert(
    std::same_as<decltype(RedGlobalAddNoftzF16{}.src), WithLocs<RegOrImm>>);

/** Each class/layout keeps both legitimate register carriers and admits numeric floating sources. */
TEST(ScalarAtomicF16Sources, RegistersThenNumericSourcesAcrossBothLayouts) {
  for (const auto item : atomic_f16_cases) {
    SCOPED_TRACE(item.atom);
    SCOPED_TRACE(item.policy);
    for (const auto operand : {"b", "h", "V.x"}) {
      auto control = ownedAtomicF16(atomicF16Text(item, operand));
      ASSERT_TRUE(control);
      auto& source = atomicF16Source(*control->functions.front().body.front());
      const auto* reg = std::get_if<ResolvedRegisterRef>(&source.value);
      ASSERT_NE(reg, nullptr);
      EXPECT_TRUE(reg->symbol_id);
      EXPECT_TRUE(validateModule(*control));
      const auto view = atomicF16View(source);
      const auto& binding = atomicF16Binding(item);
      EXPECT_EQ(view.register_ref, reg);
      EXPECT_EQ(view.locations.data(), source.locs.data());
      EXPECT_TRUE(checker::check_operands(std::span{&binding, 1}, {},
                                          std::span{&view, 1}, {}, {}));
    }
    for (const auto spelling :
         {"1.5", "1.0+0.5", "0d3ff8000000000000", "0f3fc00000"}) {
      const auto text = atomicF16Text(item, spelling);
      PtxSyntaxParser parser(text);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      auto sourceAware = resolveAndValidateModule(*ast);
      ASSERT_TRUE(sourceAware);
      auto owned = ownedAtomicF16(text);
      ASSERT_TRUE(owned);
      auto& instruction = *owned->functions.front().body.front();
      auto& source = atomicF16Source(instruction);
      const auto* word = std::get_if<ResolvedImmediate>(&source.value);
      ASSERT_NE(word, nullptr);
      EXPECT_EQ(word->type, ScalarType::F16);
      EXPECT_EQ(word->bits, 0x3e00u);
      EXPECT_FALSE(word->integer_source_bits);
      EXPECT_FALSE(word->is_negative);
      auto view = atomicF16View(source);
      const auto& binding = atomicF16Binding(item);
      EXPECT_EQ(view.immediate_type, word->type);
      EXPECT_EQ(view.immediate_bits, word->bits);
      EXPECT_EQ(view.locations.data(), source.locs.data());
      EXPECT_TRUE(checker::check_operands(std::span{&binding, 1}, {},
                                          std::span{&view, 1}, {}, {}));
      view.immediate_bits = uint64_t{1} << 16;
      EXPECT_FALSE(checker::check_operands(std::span{&binding, 1}, {},
                                           std::span{&view, 1}, {}, {}));
      EXPECT_TRUE(validateModule(*owned));
      ASSERT_EQ(source.locs.size(), 1u);
      EXPECT_NE(source.locs.front(), SourceRange{});
      EXPECT_EQ(
          instruction.instruction_kind(),
          item.atom ? AtomGlobalAddNoftzF16::kind : RedGlobalAddNoftzF16::kind);
      if (item.atom) {
        const auto& exact = dynamic_cast<AtomGlobalAddNoftzF16&>(instruction);
        EXPECT_EQ(exact.operand_layout.value, item.policy ? 1u : 0u);
        EXPECT_EQ(bool(exact.cache_policy), item.policy);
      } else {
        const auto& exact = dynamic_cast<RedGlobalAddNoftzF16&>(instruction);
        EXPECT_EQ(exact.operand_layout.value, item.policy ? 1u : 0u);
        EXPECT_EQ(bool(exact.cache_policy), item.policy);
      }
      ResolvedModule copied(*owned), assigned;
      assigned = copied;
      ResolvedFunction function(copied.functions.front()), function_assigned;
      function_assigned = function;
      assigned.functions.front() = function_assigned;
      assigned.functions.front().body.front() = function.body.front()->clone();
      auto& actual = atomicF16Source(*assigned.functions.front().body.front());
      AtomicF16Capture capture(actual);
      assigned.functions.front().body.front()->visit_references(capture);
      EXPECT_EQ(capture.matches, 1u);
      EXPECT_TRUE(capture.valid);
      EXPECT_TRUE(validateModule(
          assigned, ModuleValidationPolicy::RequireCompleteContext));
      EXPECT_TRUE(validateModule(
          *ast, assigned, ModuleValidationPolicy::RequireCompleteContext));
    }
  }
}

/** Copies and moves retain independent owners after the original module and function die. */
TEST(ScalarAtomicF16Sources, DetachedModuleMoveFunctionCopyAndClone) {
  for (const auto item : atomic_f16_cases) {
    auto detached =
        [&]() -> std::expected<ResolvedModule, ModuleResolveDiagnostics> {
      auto original = ownedAtomicF16(atomicF16Text(item, "1.5"));
      if (!original)
        return std::unexpected(original.error());
      ResolvedModule copied(*original), assigned;
      assigned = copied;
      ResolvedFunction function(original->functions.front()), function_assigned;
      function_assigned = function;
      assigned.functions.front() = function_assigned;
      assigned.functions.front().body.front() = function.body.front()->clone();
      ResolvedModule moved(std::move(assigned));
      return moved;
    }();
    ASSERT_TRUE(detached);
    auto& instruction = *detached->functions.front().body.front();
    auto& source = atomicF16Source(instruction);
    AtomicF16Capture capture(source);
    instruction.visit_references(capture);
    EXPECT_EQ(capture.matches, 1u);
    EXPECT_TRUE(capture.valid);
    ASSERT_TRUE(std::holds_alternative<ResolvedImmediate>(source.value));
    EXPECT_EQ(std::get<ResolvedImmediate>(source.value).bits, 0x3e00u);
    EXPECT_TRUE(validateModule(*detached,
                               ModuleValidationPolicy::RequireCompleteContext));
  }
}

/** Valid current F16 words do not need their original floating expression or sign history. */
TEST(ScalarAtomicF16Sources, CurrentWordsAndLocatedIndependentCorruptions) {
  for (const auto item : atomic_f16_cases) {
    const auto text = atomicF16Text(item, "1.5");
    PtxSyntaxParser parser(text);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto owned = ownedAtomicF16(text);
    ASSERT_TRUE(owned);
    auto& source = atomicF16Source(*owned->functions.front().body.front());
    ASSERT_EQ(source.locs.size(), 1u);
    const auto good = source;
    const auto source_identity = owned->source_identity;
    for (const uint16_t bits :
         {uint16_t(0x0001), uint16_t(0x8000), uint16_t(0x7c00),
          uint16_t(0x7e01), uint16_t(0xffff)}) {
      std::get<ResolvedImmediate>(source.value).bits = bits;
      EXPECT_TRUE(validateModule(*owned));
      EXPECT_TRUE(validateModule(*ast, *owned));
    }
    source = good;
    // Current Bit carriers inherit the existing same-width compatibility rule.
    std::get<ResolvedImmediate>(source.value).type = ScalarType::B16;
    const auto& bit_word = std::get<ResolvedImmediate>(source.value);
    EXPECT_EQ(bit_word.bits, 0x3e00u);
    EXPECT_FALSE(bit_word.integer_source_bits);
    EXPECT_FALSE(bit_word.is_negative);
    EXPECT_TRUE(owned->functions.front().body.front()->check(
        {.target = {.ptx_version = {9, 3}, .sm_version = 90},
         .instruction_range =
             owned->functions.front().instruction_ranges.front()}));
    EXPECT_TRUE(validateModule(*owned));
    EXPECT_TRUE(validateModule(*ast, *owned));
    EXPECT_EQ(owned->source_identity, source_identity);
    source = good;
    for (unsigned fault = 0; fault < 5; ++fault) {
      SCOPED_TRACE(fault);
      source = good;
      auto& word = std::get<ResolvedImmediate>(source.value);
      switch (fault) {
        case 0:
          word.bits |= uint64_t{1} << 16;
          break;
        case 1:
          word.type = ScalarType::F32;
          break;
        case 2:
          word.type = ScalarType::BF16;
          break;
        case 3:
          word.integer_source_bits = 0;
          break;
        case 4:
          word.is_negative = true;
          break;
      }
      const auto current = validateModule(*owned),
                 supplied = validateModule(*ast, *owned);
      ASSERT_FALSE(current);
      ASSERT_FALSE(supplied);
      const auto expected_kind =
          fault == 1 || fault == 2
              ? checker::CheckDiagnosticKind::OperandTypeMismatch
              : checker::CheckDiagnosticKind::ImmediateValueMismatch;
      const auto located = [&](const auto& diagnostics) {
        return std::ranges::any_of(diagnostics, [&](const auto& d) {
          return d.kind == expected_kind && d.range == source.locs.front();
        });
      };
      EXPECT_TRUE(located(current.error()));
      EXPECT_TRUE(located(supplied.error()));
      if (fault == 1) {
        const auto direct = owned->functions.front().body.front()->check(
            {.target = {.ptx_version = {9, 3}, .sm_version = 90},
             .instruction_range =
                 owned->functions.front().instruction_ranges.front()});
        ASSERT_FALSE(direct);
        ASSERT_FALSE(direct.error().empty());
        EXPECT_TRUE(
            std::ranges::any_of(direct.error(), [&](const auto& diagnostic) {
              return diagnostic.kind ==
                         checker::CheckDiagnosticKind::OperandTypeMismatch &&
                     diagnostic.range == good.locs.front();
            }));
      }
    }
    source = good;
    auto changed = text;
    changed.replace(changed.find("1.5"), 3, "1.0");
    PtxSyntaxParser changedParser(changed);
    const auto changedAst = changedParser.parseModule();
    ASSERT_TRUE(changedAst && changedAst.diagnostics.empty());
    ASSERT_TRUE(resolveAndValidateModule(*changedAst));
    const auto mismatch = validateModule(*changedAst, *owned);
    ASSERT_FALSE(mismatch);
    EXPECT_TRUE(std::ranges::any_of(mismatch.error(), [](const auto& d) {
      return d.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
    }));
  }
}

/** The kind change adds no integer-origin, whole-vector or unrelated register permissions. */
TEST(ScalarAtomicF16Sources, SourceAndCurrentRegisterGuardsRemainIndependent) {
  for (const auto item : atomic_f16_cases) {
    ASSERT_TRUE(ownedAtomicF16(atomicF16Text(item, "b")));
    for (const auto operand : {"1", "0x3e00", "1+0", "V", "%tid.x"})
      EXPECT_FALSE(ownedAtomicF16(atomicF16Text(item, operand)));
    for (const auto type : {"u16", "s16", "bf16", "b32", "f32", "u32", "s32",
                            "f16x2", "bf16x2", "b64", "pred"}) {
      const auto declaration = ".reg ." + std::string(type) + " wrong;";
      EXPECT_FALSE(ownedAtomicF16(
          atomicF16Text(item, "wrong", "9.3", "sm_90", declaration)));
    }
    const auto text = atomicF16Text(item, "b");
    PtxSyntaxParser p(text);
    const auto ast = p.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto module = ownedAtomicF16(text);
    ASSERT_TRUE(module);
    auto& source = atomicF16Source(*module->functions.front().body.front());
    const auto saved = source;
    for (unsigned fault = 0; fault < 3; ++fault) {
      source = saved;
      auto& reg = std::get<ResolvedRegisterRef>(source.value);
      if (fault == 0)
        reg.declared_type = ScalarType::U16;
      else if (fault == 1)
        reg.register_class = ResolvedRegisterClass::Predicate;
      else
        reg.symbol_id.reset();
      EXPECT_FALSE(validateModule(*module));
      EXPECT_FALSE(validateModule(*ast, *module));
    }
  }
}

/** Shared reduction retains its existing source domain; cache-policy layouts remain global-only. */
TEST(ScalarAtomicF16Sources, PreservesSharedReductionAndCacheStateSpace) {
  const auto shared = [](std::string text) {
    text.replace(text.find(".global .align"), 7, ".shared");
    text.replace(text.find("red.global"), 10, "red.shared");
    return text;
  };
  ASSERT_TRUE(ownedAtomicF16(shared(atomicF16Text({false, false}, "b"))));
  EXPECT_TRUE(ownedAtomicF16(shared(atomicF16Text({false, false}, "1.5"))));
  EXPECT_FALSE(ownedAtomicF16(shared(atomicF16Text({false, true}, "1.5"))));
}

/** Layout/source floors remain distinct from header legality and literal conversion. */
TEST(ScalarAtomicF16Sources, AvailabilityAndUnchangedCachePolicyDomains) {
  for (const auto item : atomic_f16_cases) {
    ASSERT_TRUE(ownedAtomicF16(atomicF16Text(item, "1.5")));
    EXPECT_FALSE(
        ownedAtomicF16(atomicF16Text(item, "1.5", item.policy ? "7.3" : "6.2",
                                     item.policy ? "sm_80" : "sm_70")));
    EXPECT_FALSE(ownedAtomicF16(
        atomicF16Text(item, "1.5", "9.3", item.policy ? "sm_70" : "sm_60")));
    auto text = atomicF16Text(item, "1.5");
    text.erase(0, text.find(".global"));
    PtxSyntaxParser parser(text);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto partial = resolveModuleOnly(*ast);
    ASSERT_TRUE(partial);
    EXPECT_TRUE(
        validateModule(*partial, ModuleValidationPolicy::AvailableContext));
    EXPECT_TRUE(validateModule(*ast, *partial,
                               ModuleValidationPolicy::AvailableContext));
    EXPECT_FALSE(validateModule(
        *partial, ModuleValidationPolicy::RequireCompleteContext));
    if (item.policy) {
      auto bad = atomicF16Text(item, "1.5");
      const auto at = bad.rfind(",policy;");
      ASSERT_NE(at, std::string::npos);
      bad.replace(at, 8, ",1;");
      EXPECT_FALSE(ownedAtomicF16(bad));
    }
  }
}
}  // namespace
/** Locate a complete written operand independently from the generated member cache. */
SourceRange atomicWrittenRange(std::string_view text,
                               std::string_view spelling) {
  const auto offset = text.rfind(spelling);
  if (offset == std::string_view::npos)
    throw std::logic_error("Missing atomic fixture spelling.");
  SourcePos start{1, 1};
  for (char c : text.substr(0, offset))
    if (c == '\n') {
      ++start.line;
      start.column = 1;
    } else
      ++start.column;
  return {start,
          {start.line, start.column + static_cast<int32_t>(spelling.size())}};
}
/** Pin actual current-check diagnostics without assuming diagnostic ordering or count. */
void expectAtomicCurrentCause(const checker::CheckResult& result,
                              checker::CheckDiagnosticKind kind,
                              SourceRange range,
                              std::string_view message = {}) {
  ASSERT_FALSE(result);
  ASSERT_FALSE(result.error().empty());
  EXPECT_TRUE(std::ranges::any_of(result.error(), [&](const auto& diagnostic) {
    return diagnostic.kind == kind && diagnostic.range == range &&
           diagnostic.message.find(message) != std::string::npos;
  }));
}
/** Integer origins fail in conversion while physical-carrier errors keep their real phase. */
TEST(ScalarAtomicF16Sources,
     SourceFailuresRetainIndependentStageCategoryAndRange) {
  for (const auto item : atomic_f16_cases) {
    ASSERT_TRUE(ownedAtomicF16(atomicF16Text(item, "b")));
    ASSERT_TRUE(ownedAtomicF16(atomicF16Text(item, "1.5")));
    for (const auto operand : {"1", "0x3e00", "1+0"}) {
      const auto text = atomicF16Text(item, operand);
      PtxSyntaxParser parser(text);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      const auto result = resolveAndValidateModule(*ast);
      ASSERT_FALSE(result);
      ASSERT_FALSE(result.error().empty());
      const auto range = atomicWrittenRange(text, operand);
      EXPECT_TRUE(
          std::ranges::any_of(result.error(), [&](const auto& diagnostic) {
            return diagnostic.stage() == ResolveDiagnosticStage::Resolution &&
                   !diagnostic.checker_kind && !diagnostic.binding_kind &&
                   !diagnostic.declaration_kind && diagnostic.range == range &&
                   diagnostic.message.find("Integer literal") !=
                       std::string::npos &&
                   diagnostic.message.find("incompatible") != std::string::npos;
          }));
      // No valid Immediate-owning module exists for this source: lifetime is SourceOwned_N_A.
    }
    for (const auto type : {"u16", "s16", "bf16", "b32", "f32", "u32", "s32",
                            "f16x2", "bf16x2", "b64", "pred"}) {
      const auto text = atomicF16Text(item, "wrong", "9.3", "sm_90",
                                      ".reg ." + std::string(type) + " wrong;");
      PtxSyntaxParser parser(text);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      const auto result = resolveAndValidateModule(*ast);
      ASSERT_FALSE(result);
      ASSERT_FALSE(result.error().empty());
      const bool intrinsic = std::string_view(type) == "bf16" ||
                             std::string_view(type) == "bf16x2";
      const bool predicate = std::string_view(type) == "pred";
      const auto range = intrinsic
                             ? atomicWrittenRange(text, "." + std::string(type))
                             : atomicWrittenRange(text, "wrong");
      const auto stage = intrinsic
                             ? ResolveDiagnosticStage::DeclarationSemantics
                         : predicate ? ResolveDiagnosticStage::Resolution
                                     : ResolveDiagnosticStage::Checking;
      EXPECT_TRUE(
          std::ranges::any_of(result.error(), [&](const auto& diagnostic) {
            if (diagnostic.stage() != stage || diagnostic.range != range ||
                diagnostic.message.empty())
              return false;
            if (intrinsic)
              return diagnostic.declaration_kind ==
                     declaration_semantics::DeclarationDiagnosticKind::
                         UnsupportedRegisterDeclarationType;
            if (predicate)
              return !diagnostic.checker_kind && !diagnostic.binding_kind &&
                     !diagnostic.declaration_kind;
            return diagnostic.checker_kind ==
                   checker::CheckDiagnosticKind::OperandTypeMismatch;
          }));
      for (const auto& diagnostic : result.error())
        std::cout << "ATOMIC_SOURCE type=" << type
                  << " stage=" << int(diagnostic.stage()) << " checker="
                  << (diagnostic.checker_kind ? int(*diagnostic.checker_kind)
                                              : -1)
                  << " range=" << diagnostic.range.start.line << ":"
                  << diagnostic.range.start.column << "-"
                  << diagnostic.range.end.line << ":"
                  << diagnostic.range.end.column << " " << diagnostic.message
                  << "\n";
    }
  }
}

/** Current register metadata is checked against the actual declaration, not a lying view. */
TEST(ScalarAtomicF16Sources,
     KnownRegisterCachesFamiliesComponentsAndCoherentOwners) {
  for (const auto item : atomic_f16_cases) {
    const auto text = atomicF16Text(item, "b", "9.3", "sm_90",
                                    ".reg .u16 wrong; .reg .b16 %r<2>;");
    PtxSyntaxParser parser(text);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto baseline = ownedAtomicF16(text);
    ASSERT_TRUE(baseline);
    const auto good =
        atomicF16Source(*baseline->functions.front().body.front());
    ASSERT_EQ(good.locs.size(), 1u);
    const auto range = good.locs.front();
    ASSERT_EQ(range, atomicWrittenRange(text, "b"));
    auto donor = ownedAtomicF16(atomicF16Text(
        item, "h", "9.3", "sm_90", ".reg .u16 wrong; .reg .b16 %r<2>;"));
    ASSERT_TRUE(donor);
    const auto donor_ref = std::get<ResolvedRegisterRef>(
        atomicF16Source(*donor->functions.front().body.front()).value);
    for (unsigned fault = 0; fault < 5; ++fault) {
      auto current = *baseline;
      auto& instruction = *current.functions.front().body.front();
      auto& field = atomicF16Source(instruction);
      auto& ref = std::get<ResolvedRegisterRef>(field.value);
      if (fault == 0)
        ref.declared_type = ScalarType::U16;
      if (fault == 1)
        ref.register_class = ResolvedRegisterClass::Predicate;
      if (fault == 2)
        ref.vector_width = 2;
      if (fault == 3)
        ref.symbol_id.reset();
      if (fault == 4)
        ref.symbol_id = donor_ref.symbol_id;
      const auto message =
          fault == 3 ? "no binding identity" : "register metadata disagrees";
      expectAtomicCurrentCause(
          validateModule(current),
          checker::CheckDiagnosticKind::ModuleSourceMismatch, range, message);
      expectAtomicCurrentCause(
          validateModule(*ast, current),
          checker::CheckDiagnosticKind::ModuleSourceMismatch, range, message);
      if (fault == 0 || fault == 1 || fault == 2)
        expectAtomicCurrentCause(
            instruction.check(
                {.target = {.ptx_version = {9, 3}, .sm_version = 90},
                 .instruction_range =
                     current.functions.front().instruction_ranges.front()}),
            fault == 0 ? checker::CheckDiagnosticKind::OperandTypeMismatch
                       : checker::CheckDiagnosticKind::UnsupportedOperandShape,
            range);
    }
    auto coherent = *baseline;
    auto& actual = atomicF16Source(*coherent.functions.front().body.front());
    actual.value = donor_ref;
    EXPECT_EQ(coherent.source_identity, baseline->source_identity);
    EXPECT_TRUE(validateModule(coherent));
    EXPECT_TRUE(validateModule(*ast, coherent));
    auto lying = atomicF16View(actual);
    lying.register_type = ScalarType::U16;
    EXPECT_NE(lying.register_type,
              std::get<ResolvedRegisterRef>(actual.value).declared_type);
    EXPECT_TRUE(validateModule(coherent));
    EXPECT_TRUE(validateModule(*ast, coherent));
    EXPECT_EQ(atomicF16View(actual).register_ref,
              std::get_if<ResolvedRegisterRef>(&actual.value));
    const auto wrong = coherent.symbols.lookup(
        coherent.functions.front().declaration_scope, "wrong");
    ASSERT_TRUE(wrong);
    auto& wrong_ref = std::get<ResolvedRegisterRef>(actual.value);
    wrong_ref.symbol_id = wrong->symbol;
    wrong_ref.parameterized_index = wrong->parameterized_index;
    wrong_ref.spelling = "wrong";
    wrong_ref.declared_type = ScalarType::U16;
    wrong_ref.index.reset();
    expectAtomicCurrentCause(validateModule(coherent),
                             checker::CheckDiagnosticKind::OperandTypeMismatch,
                             range);
    expectAtomicCurrentCause(validateModule(*ast, coherent),
                             checker::CheckDiagnosticKind::OperandTypeMismatch,
                             range);

    const auto family_text =
        atomicF16Text(item, "%r0", "9.3", "sm_90", ".reg .b16 %r<2>;");
    PtxSyntaxParser family_parser(family_text);
    const auto family_ast = family_parser.parseModule();
    ASSERT_TRUE(family_ast && family_ast.diagnostics.empty());
    auto family = ownedAtomicF16(family_text);
    ASSERT_TRUE(family);
    for (unsigned fault = 0; fault < 4; ++fault) {
      auto current = *family;
      auto& field = atomicF16Source(*current.functions.front().body.front());
      ASSERT_EQ(field.locs.size(), 1u);
      auto& ref = std::get<ResolvedRegisterRef>(field.value);
      ASSERT_EQ(ref.parameterized_index, 0u);
      ASSERT_EQ(ref.index, 0u);
      if (fault == 0)
        ref.parameterized_index.reset();
      if (fault == 1)
        ref.spelling = "%r1";
      if (fault == 2)
        ref.parameterized_index = 1;
      if (fault == 3)
        ref.parameterized_index = 2;
      const auto message = fault == 3 ? "invalid parameterized member index"
                                      : "register metadata disagrees";
      expectAtomicCurrentCause(
          validateModule(current),
          checker::CheckDiagnosticKind::ModuleSourceMismatch,
          field.locs.front(), message);
      expectAtomicCurrentCause(
          validateModule(*family_ast, current),
          checker::CheckDiagnosticKind::ModuleSourceMismatch,
          field.locs.front(), message);
    }
    for (bool clear_numbered : {false, true}) {
      auto current = *family;
      auto& ref = std::get<ResolvedRegisterRef>(
          atomicF16Source(*current.functions.front().body.front()).value);
      if (clear_numbered)
        ref.index.reset();
      else {
        ref.spelling = "%r1";
        ref.parameterized_index = 1;
        ref.index = 1;
      }
      EXPECT_EQ(current.source_identity, family->source_identity);
      EXPECT_TRUE(validateModule(current));
      EXPECT_TRUE(validateModule(*family_ast, current));
    }
    const auto component_text = atomicF16Text(item, "V.x");
    PtxSyntaxParser component_parser(component_text);
    const auto component_ast = component_parser.parseModule();
    ASSERT_TRUE(component_ast && component_ast.diagnostics.empty());
    auto component = ownedAtomicF16(component_text);
    ASSERT_TRUE(component);
    auto& field = atomicF16Source(*component->functions.front().body.front());
    ASSERT_EQ(field.locs.size(), 1u);
    auto& ref = std::get<ResolvedRegisterRef>(field.value);
    ASSERT_TRUE(ref.component);
    ++ref.component->range.end.column;
    expectAtomicCurrentCause(validateModule(*component),
                             checker::CheckDiagnosticKind::ModuleSourceMismatch,
                             field.locs.front(), "register metadata disagrees");
    expectAtomicCurrentCause(validateModule(*component_ast, *component),
                             checker::CheckDiagnosticKind::ModuleSourceMismatch,
                             field.locs.front(), "register metadata disagrees");
  }
}

/** Floors, missing contexts and cache-source failures retain their own diagnostic categories. */
TEST(ScalarAtomicF16Sources, AvailabilityFailuresAreNotLiteralSourceProof) {
  for (const auto item : atomic_f16_cases) {
    const auto healthy_text = atomicF16Text(item, "1.5");
    auto healthy = ownedAtomicF16(healthy_text);
    ASSERT_TRUE(healthy);
    for (unsigned context = 0; context < 5; ++context) {
      auto text = healthy_text;
      if (context == 0)
        text = atomicF16Text(item, "1.5", item.policy ? "7.3" : "6.2",
                             item.policy ? "sm_80" : "sm_70");
      if (context == 1)
        text =
            atomicF16Text(item, "1.5", "9.3", item.policy ? "sm_70" : "sm_60");
      if (context == 2)
        text.erase(0, text.find(".global"));
      if (context == 3)
        text.erase(0, text.find('\n') + 1);
      if (context == 4) {
        const auto at = text.find(".target");
        text.erase(at, text.find('\n', at) - at + 1);
      }
      PtxSyntaxParser parser(text);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      auto module = resolveModuleOnly(*ast);
      ASSERT_TRUE(module);
      const auto strict = resolveAndValidateModule(*ast);
      ASSERT_FALSE(strict);
      ASSERT_FALSE(strict.error().empty());
      const auto kind =
          context == 0 ? checker::CheckDiagnosticKind::UnsupportedPtxVersion
          : context == 1
              ? checker::CheckDiagnosticKind::UnsupportedSmVersion
              : checker::CheckDiagnosticKind::MissingValidationContext;
      EXPECT_TRUE(
          std::ranges::any_of(strict.error(), [&](const auto& diagnostic) {
            return diagnostic.stage() == ResolveDiagnosticStage::Checking &&
                   diagnostic.checker_kind == kind &&
                   diagnostic.range ==
                       (context < 2 ? module->functions.front()
                                          .instruction_ranges.front()
                        : context == 4 ? module->functions.front().range
                                       : ast->range);
          }));
      if (context >= 2) {
        EXPECT_TRUE(
            validateModule(*module, ModuleValidationPolicy::AvailableContext));
        EXPECT_TRUE(validateModule(*ast, *module,
                                   ModuleValidationPolicy::AvailableContext));
      }
    }
  }
}

/** Borrow the real optional cache-policy register, preserving its separate operand role. */
WithLocs<ResolvedRegisterRef>& atomicCacheSource(Instruction& instruction) {
  if (auto* form = dynamic_cast<AtomGlobalAddNoftzF16*>(&instruction)) {
    if (!form->cache_policy)
      throw std::logic_error("Missing atomic cache-policy fixture.");
    return *form->cache_policy;
  }
  if (auto* form = dynamic_cast<RedGlobalAddNoftzF16*>(&instruction)) {
    if (!form->cache_policy)
      throw std::logic_error("Missing reduction cache-policy fixture.");
    return *form->cache_policy;
  }
  throw std::logic_error("Unexpected cache-policy owner.");
}
/** Cache failures and shared-address layout restrictions cannot masquerade as source failures. */
TEST(ScalarAtomicF16Sources, CacheAndSharedFailuresKeepSeparateCauses) {
  for (const auto item : atomic_f16_cases)
    if (item.policy) {
      const auto text = atomicF16Text(item, "1.5");
      PtxSyntaxParser parser(text);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      auto baseline = ownedAtomicF16(text);
      ASSERT_TRUE(baseline);
      for (bool erase : {false, true}) {
        auto current = *baseline;
        auto& cache =
            atomicCacheSource(*current.functions.front().body.front());
        ASSERT_EQ(cache.locs.size(), 1u);
        if (erase)
          cache.value.symbol_id.reset();
        else
          cache.value.declared_type = ScalarType::B32;
        const auto message =
            erase ? "no binding identity" : "register metadata disagrees";
        expectAtomicCurrentCause(
            validateModule(current),
            checker::CheckDiagnosticKind::ModuleSourceMismatch,
            cache.locs.front(), message);
        expectAtomicCurrentCause(
            validateModule(*ast, current),
            checker::CheckDiagnosticKind::ModuleSourceMismatch,
            cache.locs.front(), message);
        const auto& source = std::get<ResolvedImmediate>(
            atomicF16Source(*current.functions.front().body.front()).value);
        EXPECT_EQ(source.type, ScalarType::F16);
        EXPECT_EQ(source.bits, 0x3e00u);
        EXPECT_FALSE(source.integer_source_bits);
      }
      auto bad = text;
      const auto at = bad.rfind(",policy;");
      ASSERT_NE(at, std::string::npos);
      bad.replace(at, 8, ",1;");
      PtxSyntaxParser bad_parser(bad);
      const auto bad_ast = bad_parser.parseModule();
      ASSERT_TRUE(bad_ast && bad_ast.diagnostics.empty());
      const auto result = resolveAndValidateModule(*bad_ast);
      ASSERT_FALSE(result);
      ASSERT_FALSE(result.error().empty());
      const auto opcode = bad.rfind(item.atom ? "atom.global" : "red.global");
      ASSERT_NE(opcode, std::string::npos);
      const auto terminator = bad.find(';', opcode);
      ASSERT_NE(terminator, std::string::npos);
      const auto layout_range = atomicWrittenRange(
          bad, std::string_view(bad).substr(opcode, terminator - opcode + 1));
      EXPECT_TRUE(
          std::ranges::any_of(result.error(), [&](const auto& diagnostic) {
            return diagnostic.stage() == ResolveDiagnosticStage::Resolution &&
                   !diagnostic.checker_kind &&
                   diagnostic.range == layout_range &&
                   diagnostic.message.find(
                       "Operands do not match any layout") != std::string::npos;
          }));
    }
  auto shared = atomicF16Text({false, false}, "1.5");
  shared.replace(shared.find(".global .align"), 7, ".shared");
  shared.replace(shared.find("red.global"), 10, "red.shared");
  ASSERT_TRUE(ownedAtomicF16(shared));
  auto with_policy = atomicF16Text({false, true}, "1.5");
  with_policy.replace(with_policy.find(".global .align"), 7, ".shared");
  with_policy.replace(with_policy.find("red.global"), 10, "red.shared");
  PtxSyntaxParser parser(with_policy);
  const auto ast = parser.parseModule();
  ASSERT_TRUE(ast && ast.diagnostics.empty());
  auto current = resolveModuleOnly(*ast);
  ASSERT_TRUE(current);
  const auto result = resolveAndValidateModule(*ast);
  ASSERT_FALSE(result);
  ASSERT_FALSE(result.error().empty());
  const auto range = atomicWrittenRange(with_policy, "[memory]");
  EXPECT_TRUE(std::ranges::any_of(result.error(), [&](const auto& diagnostic) {
    return diagnostic.stage() == ResolveDiagnosticStage::Checking &&
           diagnostic.checker_kind ==
               checker::CheckDiagnosticKind::AddressStateSpaceMismatch &&
           diagnostic.range == range &&
           diagnostic.message.find("requires a global address") !=
               std::string::npos;
  }));
  for (const auto& diagnostic : result.error())
    std::cout << "ATOMIC_SHARED stage=" << int(diagnostic.stage())
              << " checker="
              << (diagnostic.checker_kind ? int(*diagnostic.checker_kind) : -1)
              << " range=" << diagnostic.range.start.line << ":"
              << diagnostic.range.start.column << "-"
              << diagnostic.range.end.line << ":" << diagnostic.range.end.column
              << " " << diagnostic.message << "\n";
}

}  // namespace ptx_frontend::resolved_ir
