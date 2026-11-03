#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <iostream>
#include <string>
#include <string_view>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {
/** One exact source key; the marker replaces only the designated multiplicand/source. */
struct Bf16SourceCase {
  /** Exact existing final class identity. */
  InstructionKind kind;
  /** One-based source member number, always one or two. */
  unsigned field;
  /** Written operation with one '$' source marker. */
  std::string_view operation;
};
constexpr std::array cases{
    Bf16SourceCase{FmaMixedF32Bf16::kind, 1, "fma.rn.f32.bf16 f,$,b2,f;"},
    Bf16SourceCase{FmaMixedF32Bf16::kind, 2, "fma.rn.f32.bf16 f,b1,$,f;"},
    Bf16SourceCase{SetHalfIntegerBf16::kind, 1, "set.eq.u32.bf16 u,$,b2;"},
    Bf16SourceCase{SetHalfIntegerBf16::kind, 2, "set.eq.u32.bf16 u,b1,$;"},
    Bf16SourceCase{SetHalfIntegerBf16Boolean::kind, 1,
                   "set.eq.and.u32.bf16 u,$,b2,p;"},
    Bf16SourceCase{SetHalfIntegerBf16Boolean::kind, 2,
                   "set.eq.and.u32.bf16 u,b1,$,p;"},
};

/** Borrow the actual changed member only while the supplied instruction remains alive. */
WithLocs<RegOrImm>& bf16Source(Instruction& instruction, unsigned field) {
  if (auto* form = dynamic_cast<FmaMixedF32Bf16*>(&instruction))
    return field == 1 ? form->src1 : form->src2;
  if (auto* form = dynamic_cast<SetHalfIntegerBf16*>(&instruction))
    return field == 1 ? form->src1 : form->src2;
  auto& form = dynamic_cast<SetHalfIntegerBf16Boolean&>(instruction);
  return field == 1 ? form.src1 : form.src2;
}

/** Build otherwise matched source with optional independent valid declaration controls. */
std::string bf16SourceText(const Bf16SourceCase& item, std::string_view operand,
                           std::string_view extra = "",
                           std::string_view target = "sm_100a") {
  std::string operation(item.operation);
  operation.replace(operation.find('$'), 1, operand);
  return ".version 9.3\n.target " + std::string(target) +
         "\n.entry kernel(){ .reg .b16 b1,b2; .reg .f32 f; .reg .u32 u; .reg "
         ".pred p; " +
         std::string(extra) + operation + " }";
}

/** Compute a unique written literal range independently of resolved field locations. */
SourceRange bf16LiteralRange(std::string_view source,
                             std::string_view literal) {
  const auto offset = source.rfind(literal);
  if (offset == std::string_view::npos)
    return {};
  SourcePos position{1, 1};
  for (const char character : source.substr(0, offset)) {
    if (character == '\n') {
      ++position.line;
      position.column = 1;
    } else
      ++position.column;
  }
  return {
      position,
      {position.line, position.column + static_cast<int32_t>(literal.size())}};
}

/** Release source text, parser and AST before returning the owning module. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> ownedBf16Source(
    std::string source, bool strict = true) {
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return strict ? resolveAndValidateModule(*ast) : resolveModuleOnly(*ast);
}

/** Observe identity and source locations synchronously without retaining callback arguments. */
struct Bf16ActualObserver final : detail::IReferenceObserver {
  /** Borrowed actual member, valid through this single traversal. */
  const WithLocs<RegOrImm>& expected;
  /** Number of callbacks referring to exactly that member. */
  size_t matches = 0;
  /** Compare the borrowed callback with its owning field during the callback only. */
  void reg_or_imm(const RegOrImm& value, std::span<const SourceRange> ranges,
                  checker::AddressSymbolResolutionPolicy) override {
    if (&value == &expected.value) {
      ++matches;
      EXPECT_TRUE(std::ranges::equal(ranges, expected.locs));
    }
  }
  /** Bind one owner for the lifetime of a synchronous traversal. */
  explicit Bf16ActualObserver(const WithLocs<RegOrImm>& field)
      : expected(field) {}
};

/** Every source preserves a lawful B16 register and converts existing scalar Float forms. */
TEST(RemainingBf16FmaSetSources, SixKeysMatchedSourceAndPayload) {
  for (const auto& item : cases) {
    SCOPED_TRACE(item.operation);
    auto control = ownedBf16Source(bf16SourceText(item, "b1"));
    ASSERT_TRUE(control);
    auto& register_field =
        bf16Source(*control->functions.front().body.front(), item.field);
    const auto* reference =
        std::get_if<ResolvedRegisterRef>(&register_field.value);
    ASSERT_NE(reference, nullptr);
    EXPECT_TRUE(reference->symbol_id);
    EXPECT_EQ(reference->declared_type, ScalarType::B16);
    EXPECT_FALSE(reference->vector_width);
    ASSERT_EQ(register_field.locs.size(), 1u);
    for (const auto literal :
         {"1.5", "1.0+0.5", "0d3ff8000000000000", "0f3fc00000"})
      for (const bool strict : {false, true}) {
        const auto text = bf16SourceText(item, literal);
        auto owned = ownedBf16Source(text, strict);
        ASSERT_TRUE(owned) << literal;
        auto& instruction = *owned->functions.front().body.front();
        EXPECT_EQ(instruction.instruction_kind(), item.kind);
        const auto& field = bf16Source(instruction, item.field);
        const auto* immediate = std::get_if<ResolvedImmediate>(&field.value);
        ASSERT_NE(immediate, nullptr);
        EXPECT_EQ(immediate->type, ScalarType::BF16);
        EXPECT_EQ(immediate->bits,
                  0x3fc0u);  // Existing scalar BF16 1.5 golden.
        EXPECT_FALSE(immediate->integer_source_bits);
        EXPECT_FALSE(immediate->is_negative);
        ASSERT_EQ(field.locs.size(), 1u);
        EXPECT_EQ(field.locs.front(), bf16LiteralRange(text, literal));
        Bf16ActualObserver observer(field);
        instruction.visit_references(observer);
        EXPECT_EQ(observer.matches, 1u);
        EXPECT_TRUE(validateModule(
            *owned, ModuleValidationPolicy::RequireCompleteContext));
      }
  }
}

/** Current canonical words require no historical Float literal or conversion cache. */
TEST(RemainingBf16FmaSetSources, CurrentWordsOwnershipAndCausalMutations) {
  for (const auto& item : cases) {
    const auto text = bf16SourceText(item, "b1");
    PtxSyntaxParser parser(text);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module);
    auto& field =
        bf16Source(*module->functions.front().body.front(), item.field);
    field.value = ResolvedImmediate{.bits = 0x3fc0, .type = ScalarType::BF16};
    ASSERT_TRUE(validateModule(*module));
    for (const uint64_t word :
         {0u, 0x8000u, 0x3f80u, 0x4000u, 0x7f80u, 0x7fc1u, 0xffffu}) {
      std::get<ResolvedImmediate>(field.value).bits = word;
      EXPECT_TRUE(validateModule(*module));
      EXPECT_TRUE(validateModule(*ast, *module));
    }
    field.value = ResolvedImmediate{.bits = 0x3fc0, .type = ScalarType::BF16};
    const auto expected_locs = field.locs;
    const auto other =
        bf16Source(*module->functions.front().body.front(), 3 - item.field);
    ResolvedModule copied = *module, assigned;
    assigned = copied;
    ResolvedFunction function = copied.functions.front(), function_assigned;
    function_assigned = function;
    assigned.functions.front() = std::move(function_assigned);
    assigned.functions.front().body.front() = function.body.front()->clone();
    module = std::unexpected(
        ModuleResolveDiagnostics{});  // Destroy the original producer owner.
    ASSERT_TRUE(validateModule(copied));
    ASSERT_TRUE(validateModule(assigned));
    ResolvedModule moved = std::move(assigned);
    assigned = std::move(moved);
    ASSERT_TRUE(validateModule(assigned));
    for (auto* owner : {&copied, &assigned}) {
      auto& selected =
          bf16Source(*owner->functions.front().body.front(), item.field);
      auto& untouched =
          bf16Source(*owner->functions.front().body.front(), 3 - item.field);
      EXPECT_EQ(selected.locs, expected_locs);
      EXPECT_EQ(untouched.value, other.value);
      EXPECT_EQ(untouched.locs, other.locs);
      for (const auto* member : {&selected, &untouched}) {
        Bf16ActualObserver observer(*member);
        owner->functions.front().body.front()->visit_references(observer);
        EXPECT_EQ(observer.matches, 1u);
      }
      EXPECT_TRUE(validateModule(*ast, *owner));
    }
    auto& current =
        bf16Source(*assigned.functions.front().body.front(), item.field);
    const auto valid = std::get<ResolvedImmediate>(current.value);
    for (unsigned mutation = 0; mutation < 4; ++mutation) {
      auto bad = valid;
      if (mutation == 0)
        bad.bits |= uint64_t{1} << 16;
      if (mutation == 1)
        bad.type = ScalarType::F16;
      if (mutation == 2)
        bad.integer_source_bits = 0;
      if (mutation == 3)
        bad.is_negative = true;
      current.value = bad;
      EXPECT_FALSE(validateModule(assigned));
      EXPECT_FALSE(validateModule(*ast, assigned));
      current.value = valid;
      EXPECT_TRUE(validateModule(assigned));
      EXPECT_TRUE(validateModule(*ast, assigned));
    }
  }
}

/** Source failures do not masquerade as owned failures, and AST identity remains independent. */
TEST(RemainingBf16FmaSetSources, IntegerRegisterAndSourceAssociationNegatives) {
  for (const auto& item : cases) {
    for (const auto integer : {"1", "0x3fc0", "1+1"})
      EXPECT_FALSE(ownedBf16Source(bf16SourceText(item, integer)));
    for (const auto type : {"f16", "u16", "s16", "b32", "f32", "pred"})
      EXPECT_FALSE(ownedBf16Source(bf16SourceText(
          item, "bad", ".reg ." + std::string(type) + " bad; ")));
    const auto text = bf16SourceText(item, "1.5");
    PtxSyntaxParser parser(text);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto owned = resolveAndValidateModule(*ast);
    ASSERT_TRUE(owned);
    ASSERT_TRUE(validateModule(*ast, *owned));
    auto& field =
        bf16Source(*owned->functions.front().body.front(), item.field);
    std::get<ResolvedImmediate>(field.value).integer_source_bits = 0;
    EXPECT_FALSE(validateModule(*ast, *owned));
    std::get<ResolvedImmediate>(field.value).integer_source_bits.reset();
    const auto changed_text = bf16SourceText(item, "2.5");
    PtxSyntaxParser changed_parser(changed_text);
    const auto changed = changed_parser.parseModule();
    ASSERT_TRUE(changed && changed.diagnostics.empty());
    ASSERT_TRUE(resolveAndValidateModule(*changed));
    const auto mismatch = validateModule(*changed, *owned);
    ASSERT_FALSE(mismatch);
    EXPECT_TRUE(
        std::ranges::any_of(mismatch.error(), [](const auto& diagnostic) {
          return diagnostic.kind ==
                 checker::CheckDiagnosticKind::ModuleSourceMismatch;
        }));
    const auto old_target =
        item.kind == FmaMixedF32Bf16::kind ? "sm_90" : "sm_80";
    EXPECT_FALSE(ownedBf16Source(bf16SourceText(item, "1.5", "", old_target)));
    auto registers = ownedBf16Source(bf16SourceText(item, "b1"));
    ASSERT_TRUE(registers);
    auto& reg = std::get<ResolvedRegisterRef>(
        bf16Source(*registers->functions.front().body.front(), item.field)
            .value);
    reg.register_class = ResolvedRegisterClass::Predicate;
    EXPECT_FALSE(validateModule(*registers));
  }
}

/** Require a causal, located checker failure through both owning validation entry points. */
void expectBf16Invalid(const syntax_ast::AstModule& ast,
                       const ResolvedModule& module) {
  for (const auto result :
       {validateModule(module), validateModule(ast, module)}) {
    ASSERT_FALSE(result);
    ASSERT_FALSE(result.error().empty());
    for (const auto& diagnostic : result.error()) {
      EXPECT_GT(diagnostic.range.start.line, 0);
      EXPECT_FALSE(diagnostic.message.empty());
      std::cout << "CURRENT_DIAG kind="
                << static_cast<unsigned>(diagnostic.kind)
                << " range=" << diagnostic.range.start.line << ":"
                << diagnostic.range.start.column << "-"
                << diagnostic.range.end.line << ":"
                << diagnostic.range.end.column << " " << diagnostic.message
                << "\n";
    }
  }
}

/** Recheck current scalar identity, cached facts, family and selected-component provenance. */
TEST(RemainingBf16FmaSetSources, CurrentRegistersBothApis) {
  for (const auto& item : cases) {
    const auto text = bf16SourceText(item, "b1");
    PtxSyntaxParser parser(text);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module);
    auto& field =
        bf16Source(*module->functions.front().body.front(), item.field);
    const auto valid = std::get<ResolvedRegisterRef>(field.value);
    for (unsigned mutation = 0; mutation < 7; ++mutation) {
      SCOPED_TRACE(mutation);
      auto bad = valid;
      if (mutation == 0)
        bad.declared_type = ScalarType::F16;
      if (mutation == 1)
        bad.declared_type = ScalarType::B32;
      if (mutation == 2)
        bad.register_class = ResolvedRegisterClass::Predicate;
      if (mutation == 3)
        bad.symbol_id.reset();
      if (mutation == 4)
        bad.parameterized_index = 0;
      if (mutation == 5)
        bad.vector_width = 2;
      if (mutation == 6)
        bad.symbol_id = module->functions.front().symbol_id;
      field.value = bad;
      expectBf16Invalid(*ast, *module);
      field.value = valid;
      ASSERT_TRUE(validateModule(*ast, *module));
      ASSERT_TRUE(validateModule(*module));
    }
    const auto component_text =
        bf16SourceText(item, "v.x", ".reg .v2 .b16 v; ");
    PtxSyntaxParser component_parser(component_text);
    const auto component_ast = component_parser.parseModule();
    ASSERT_TRUE(component_ast && component_ast.diagnostics.empty());
    auto component_module = resolveAndValidateModule(*component_ast);
    ASSERT_TRUE(component_module);
    auto& component_field = bf16Source(
        *component_module->functions.front().body.front(), item.field);
    const auto component_valid =
        std::get<ResolvedRegisterRef>(component_field.value);
    ASSERT_TRUE(component_valid.component);
    for (unsigned mutation = 0; mutation < 8; ++mutation) {
      SCOPED_TRACE(mutation);
      auto bad = component_valid;
      auto& part = *bad.component;
      if (mutation == 0)
        part.lane = 2;
      if (mutation == 1)
        part.declaration_width = 4;
      if (mutation == 2)
        part.base_range.end.column++;
      if (mutation == 3)
        part.range.end.column--;
      if (mutation == 4)
        part.selector->spelling = ".y";
      if (mutation == 5)
        part.selector->range.start.column++;
      if (mutation == 6)
        bad.vector_width = 2;
      if (mutation == 7)
        bad.parameterized_index = 0;
      component_field.value = bad;
      expectBf16Invalid(*component_ast, *component_module);
      component_field.value = component_valid;
      ASSERT_TRUE(validateModule(*component_module));
      ASSERT_TRUE(validateModule(*component_ast, *component_module));
    }
  }
}

/** Attribute source failures using the real diagnostic stage and checker category. */
TEST(RemainingBf16FmaSetSources, SourceStagesAndPartialContext) {
  for (const auto& item : cases) {
    for (const auto type : {"f16", "u16", "s16", "bf16", "b32", "f32", "u32",
                            "s32", "f16x2", "bf16x2", "b64", "pred"}) {
      const auto text =
          bf16SourceText(item, "bad", ".reg ." + std::string(type) + " bad; ");
      PtxSyntaxParser parser(text);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      const auto result = resolveAndValidateModule(*ast);
      ASSERT_FALSE(result) << type;
      ASSERT_FALSE(result.error().empty());
      const bool intrinsic = std::string_view(type) == "bf16" ||
                             std::string_view(type) == "bf16x2";
      const auto expected_stage =
          intrinsic ? ResolveDiagnosticStage::DeclarationSemantics
          : std::string_view(type) == "pred"
              ? ResolveDiagnosticStage::Resolution
              : ResolveDiagnosticStage::Checking;
      EXPECT_EQ(result.error().front().stage(), expected_stage);
      if (expected_stage == ResolveDiagnosticStage::Checking) {
        EXPECT_EQ(result.error().front().checker_kind,
                  checker::CheckDiagnosticKind::OperandTypeMismatch);
        EXPECT_EQ(result.error().front().range, bf16LiteralRange(text, "bad"));
      }
      for (const auto& diagnostic : result.error()) {
        EXPECT_GT(diagnostic.range.start.line, 0);
        std::cout << "SOURCE_REGISTER type=" << type
                  << " stage=" << static_cast<unsigned>(diagnostic.stage())
                  << " checker="
                  << (diagnostic.checker_kind
                          ? static_cast<int>(*diagnostic.checker_kind)
                          : -1)
                  << " range=" << diagnostic.range.start.line << ":"
                  << diagnostic.range.start.column << "-"
                  << diagnostic.range.end.line << ":"
                  << diagnostic.range.end.column << " " << diagnostic.message
                  << "\n";
      }
    }
    for (unsigned context = 0; context < 5; ++context) {
      auto text = bf16SourceText(item, "1.5");
      if (context == 0)
        text.replace(0, std::string(".version 9.3").size(),
                     item.kind == FmaMixedF32Bf16::kind ? ".version 8.5"
                                                        : ".version 7.7");
      if (context == 1)
        text = bf16SourceText(
            item, "1.5", "",
            item.kind == FmaMixedF32Bf16::kind ? "sm_90" : "sm_80");
      if (context == 2)
        text.erase(0, text.find(".entry"));
      if (context == 3)
        text.erase(0, text.find('\n') + 1);
      if (context == 4) {
        const auto start = text.find(".target");
        text.erase(start, text.find('\n', start) - start + 1);
      }
      PtxSyntaxParser parser(text);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      auto module = resolveModuleOnly(*ast);
      ASSERT_TRUE(module) << context;
      const auto strict = resolveAndValidateModule(*ast);
      ASSERT_FALSE(strict) << context;
      ASSERT_FALSE(strict.error().empty());
      for (const auto& diagnostic : strict.error())
        std::cout << "CONTEXT_DIAG context=" << context
                  << " stage=" << static_cast<unsigned>(diagnostic.stage())
                  << " checker="
                  << (diagnostic.checker_kind
                          ? static_cast<int>(*diagnostic.checker_kind)
                          : -1)
                  << " range=" << diagnostic.range.start.line << ":"
                  << diagnostic.range.start.column << "-"
                  << diagnostic.range.end.line << ":"
                  << diagnostic.range.end.column << " " << diagnostic.message
                  << "\n";
      EXPECT_EQ(strict.error().front().stage(),
                ResolveDiagnosticStage::Checking);
      EXPECT_TRUE(strict.error().front().checker_kind);
      const auto expected_kind =
          context == 0 ? checker::CheckDiagnosticKind::UnsupportedPtxVersion
          : context == 1
              ? checker::CheckDiagnosticKind::UnsupportedSmVersion
              : checker::CheckDiagnosticKind::MissingValidationContext;
      EXPECT_TRUE(
          std::ranges::any_of(strict.error(), [&](const auto& diagnostic) {
            return diagnostic.stage() == ResolveDiagnosticStage::Checking &&
                   diagnostic.checker_kind == expected_kind;
          }));
      if (context >= 2) {
        EXPECT_TRUE(
            validateModule(*module, ModuleValidationPolicy::AvailableContext));
        EXPECT_TRUE(validateModule(*ast, *module,
                                   ModuleValidationPolicy::AvailableContext));
      } else {
        EXPECT_FALSE(
            validateModule(*module, ModuleValidationPolicy::AvailableContext));
        EXPECT_FALSE(validateModule(*ast, *module,
                                    ModuleValidationPolicy::AvailableContext));
      }
      EXPECT_FALSE(validateModule(*module));
      EXPECT_FALSE(validateModule(*ast, *module));
    }
  }
}

/** Keep parameterized-register families distinct from scalar caches and validate their actual count. */
TEST(RemainingBf16FmaSetSources, ParameterizedFamilyBothApis) {
  for (const auto& item : cases) {
    const auto text = bf16SourceText(item, "%r0", ".reg .b16 %r<2>; ");
    PtxSyntaxParser parser(text);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module);
    auto& field =
        bf16Source(*module->functions.front().body.front(), item.field);
    const auto valid = std::get<ResolvedRegisterRef>(field.value);
    ASSERT_TRUE(valid.symbol_id);
    ASSERT_TRUE(valid.parameterized_index);
    EXPECT_EQ(*valid.parameterized_index, 0u);
    ASSERT_TRUE(validateModule(*module));
    ASSERT_TRUE(validateModule(*ast, *module));
    for (unsigned mutation = 0; mutation < 3; ++mutation) {
      auto bad = valid;
      if (mutation == 0)
        bad.parameterized_index = 2;
      if (mutation == 1)
        bad.parameterized_index.reset();
      if (mutation == 2)
        bad.spelling = "%r1";
      field.value = bad;
      expectBf16Invalid(*ast, *module);
      field.value = valid;
      ASSERT_TRUE(validateModule(*module));
      ASSERT_TRUE(validateModule(*ast, *module));
    }
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
