#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

namespace ptx_frontend::resolved_ir {
namespace {
/** One exact class/layout source key, with only its source marker substituted. */
struct AtomicBf16Case {
  /** Existing exact public class identity. */
  InstructionKind kind;
  /** Presence of the existing policy operand and matching modifier. */
  bool hinted;
  /** Full written operation; '$' denotes the single changed source. */
  std::string_view operation;
};
constexpr std::array cases{
    AtomicBf16Case{AtomGlobalAddNoftzBf16::kind, false,
                   "atom.global.add.noftz.bf16 d,[data],$;"},
    AtomicBf16Case{
        AtomGlobalAddNoftzBf16::kind, true,
        "atom.global.add.noftz.L2::cache_hint.bf16 d,[data],$,policy;"},
    AtomicBf16Case{RedGlobalAddNoftzBf16::kind, false,
                   "red.global.add.noftz.bf16 [data],$;"},
    AtomicBf16Case{RedGlobalAddNoftzBf16::kind, true,
                   "red.global.add.noftz.L2::cache_hint.bf16 [data],$,policy;"},
};

/** Borrow the actual src member for no longer than its instruction owner. */
WithLocs<RegOrImm>& atomicBf16Source(Instruction& instruction) {
  if (auto* atom = dynamic_cast<AtomGlobalAddNoftzBf16*>(&instruction))
    return atom->src;
  return dynamic_cast<RedGlobalAddNoftzBf16&>(instruction).src;
}

/** Build a legal aligned global profile with independently replaceable source declarations. */
std::string atomicBf16Text(const AtomicBf16Case& item, std::string_view source,
                           std::string_view extra = "",
                           std::string_view target = "sm_90") {
  std::string operation(item.operation);
  operation.replace(operation.find('$'), 1, source);
  return ".version 9.3\n.target " + std::string(target) +
         "\n.global .align 2 .b16 data;\n.entry kernel(){ .reg .b16 h,d; .reg "
         ".b64 policy; " +
         std::string(extra) + operation + " }";
}

/** Resolve with source/parser/AST alive, then return only owned IR after their destruction. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> ownedAtomicBf16(
    std::string source, bool strict = true) {
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  EXPECT_TRUE(ast && ast.diagnostics.empty()) << source;
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  auto result =
      strict ? resolveAndValidateModule(*ast) : resolveModuleOnly(*ast);
  if (result && (result->functions.size() != 1 ||
                 result->functions.front().body.size() != 1 ||
                 !result->functions.front().body.front())) {
    ADD_FAILURE()
        << "Atomic BF16 fixture must own exactly one function/instruction: "
        << source;
    return std::unexpected(ModuleResolveDiagnostics{});
  }
  return result;
}

/** Independently locate one written source token/expression in the full fixture. */
SourceRange atomicBf16Range(std::string_view source, std::string_view operand) {
  const auto offset = source.rfind(operand);
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
      {position.line, position.column + static_cast<int32_t>(operand.size())}};
}

/** Check actual owner identity synchronously; callback references never escape traversal. */
struct AtomicBf16Observer final : detail::IReferenceObserver {
  /** Actual owner member, borrowed only until visit_references returns. */
  const WithLocs<RegOrImm>& expected;
  /** Number of callbacks for that exact source address. */
  size_t matches = 0;
  /** Bind the inspected owner before synchronous traversal. */
  explicit AtomicBf16Observer(const WithLocs<RegOrImm>& source)
      : expected(source) {}
  /** Verify the actual borrowed payload and exact source range mirror. */
  void reg_or_imm(const RegOrImm& value, std::span<const SourceRange> ranges,
                  checker::AddressSymbolResolutionPolicy) override {
    if (&value == &expected.value) {
      ++matches;
      EXPECT_TRUE(std::ranges::equal(ranges, expected.locs));
    }
  }
};

/** Record the real source API's stage, typed checker category, cause and owned source range. */
void logAtomicBf16SourceFailure(std::string_view label,
                                const ModuleResolveDiagnostics& diagnostics) {
  for (const auto& diagnostic : diagnostics)
    std::cout << "ATOMIC_BF16_SOURCE " << label
              << " stage=" << static_cast<unsigned>(diagnostic.stage())
              << " checker="
              << (diagnostic.checker_kind
                      ? static_cast<int>(*diagnostic.checker_kind)
                      : -1)
              << " range=" << diagnostic.range.start.line << ":"
              << diagnostic.range.start.column << "-"
              << diagnostic.range.end.line << ":" << diagnostic.range.end.column
              << " " << diagnostic.message << "\n";
}

/** Require the precise current use range and causal category through both complete validators. */
void expectAtomicBf16Invalid(
    const syntax_ast::AstModule& ast, const ResolvedModule& module,
    SourceRange range, checker::CheckDiagnosticKind kind,
    ModuleValidationPolicy policy =
        ModuleValidationPolicy::RequireCompleteContext) {
  if (policy == ModuleValidationPolicy::RequireCompleteContext &&
      (kind == checker::CheckDiagnosticKind::OperandTypeMismatch ||
       kind == checker::CheckDiagnosticKind::ImmediateValueMismatch)) {
    ASSERT_EQ(module.functions.size(), 1u);
    ASSERT_EQ(module.functions.front().body.size(), 1u);
    const auto& instruction = *module.functions.front().body.front();
    auto& mutable_instruction = *module.functions.front().body.front();
    const auto& source = atomicBf16Source(mutable_instruction);
    if (std::get_if<ResolvedImmediate>(&source.value) &&
        source.locs.size() == 1 && source.locs.front() == range) {
      ASSERT_EQ(module.functions.front().instruction_ranges.size(), 1u);
      const auto direct = instruction.check(
          {.target = {.ptx_version = {9, 3}, .sm_version = 90},
           .instruction_range =
               module.functions.front().instruction_ranges.front()});
      ASSERT_FALSE(direct);
      EXPECT_TRUE(
          std::ranges::any_of(direct.error(), [&](const auto& diagnostic) {
            return diagnostic.kind == kind && diagnostic.range == range &&
                   !diagnostic.message.empty();
          }));
    }
  }
  for (const auto result :
       {validateModule(module, policy), validateModule(ast, module, policy)}) {
    ASSERT_FALSE(result);
    ASSERT_FALSE(result.error().empty());
    EXPECT_TRUE(
        std::ranges::any_of(result.error(), [&](const auto& diagnostic) {
          return diagnostic.kind == kind && diagnostic.range == range &&
                 !diagnostic.message.empty();
        }));
  }
}

/** Observe source permissions independently in both existing policy layouts and classes. */
TEST(ScalarAtomicBf16Sources, FourKeysMatchedRegisterAndNumericSources) {
  for (const auto& item : cases) {
    SCOPED_TRACE(item.operation);
    auto control = ownedAtomicBf16(atomicBf16Text(item, "h"));
    ASSERT_TRUE(control);
    auto& field = atomicBf16Source(*control->functions.front().body.front());
    const auto* reg = std::get_if<ResolvedRegisterRef>(&field.value);
    ASSERT_NE(reg, nullptr);
    EXPECT_TRUE(reg->symbol_id);
    EXPECT_EQ(reg->declared_type, ScalarType::B16);
    for (const auto literal :
         {"1.5", "1.0+0.5", "0d3ff8000000000000", "0f3fc00000"})
      for (const bool strict : {false, true}) {
        const auto text = atomicBf16Text(item, literal);
        auto module = ownedAtomicBf16(text, strict);
        ASSERT_TRUE(module) << text;
        auto& instruction = *module->functions.front().body.front();
        EXPECT_EQ(instruction.instruction_kind(), item.kind);
        const auto& source = atomicBf16Source(instruction);
        const auto* immediate = std::get_if<ResolvedImmediate>(&source.value);
        ASSERT_NE(immediate, nullptr);
        EXPECT_EQ(immediate->type, ScalarType::BF16);
        EXPECT_EQ(immediate->bits,
                  0x3fc0u);  // Accepted scalar BF16 1.5 golden.
        EXPECT_FALSE(immediate->integer_source_bits);
        EXPECT_FALSE(immediate->is_negative);
        ASSERT_EQ(source.locs.size(), 1u);
        EXPECT_EQ(source.locs.front(), atomicBf16Range(text, literal));
        AtomicBf16Observer observer(source);
        instruction.visit_references(observer);
        EXPECT_EQ(observer.matches, 1u);
        if (auto* atom = dynamic_cast<AtomGlobalAddNoftzBf16*>(&instruction)) {
          ASSERT_TRUE(atom->dst.value.register_ref);
          EXPECT_EQ(atom->dst.value.register_ref->declared_type,
                    ScalarType::B16);
          ASSERT_EQ(atom->cache_policy.has_value(), item.hinted);
          if (item.hinted)
            EXPECT_EQ(atom->cache_policy->value.declared_type, ScalarType::B64);
        } else {
          const auto& red = dynamic_cast<RedGlobalAddNoftzBf16&>(instruction);
          ASSERT_EQ(red.cache_policy.has_value(), item.hinted);
          if (item.hinted)
            EXPECT_EQ(red.cache_policy->value.declared_type, ScalarType::B64);
        }
        EXPECT_TRUE(validateModule(
            *module, ModuleValidationPolicy::RequireCompleteContext));
      }
  }
}

/** Direct current words do not require floating history and copies retain ownership. */
TEST(ScalarAtomicBf16Sources, CanonicalCurrentWordsCopiesAndCausalBadPayloads) {
  for (const auto& item : cases) {
    const auto text = atomicBf16Text(item, "h");
    PtxSyntaxParser parser(text);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto producer = resolveAndValidateModule(*ast);
    ASSERT_TRUE(producer);
    ASSERT_EQ(producer->functions.size(), 1u);
    ASSERT_EQ(producer->functions.front().body.size(), 1u);
    ASSERT_TRUE(producer->functions.front().body.front());
    auto& source = atomicBf16Source(*producer->functions.front().body.front());
    source.value = ResolvedImmediate{.bits = 0x3fc0, .type = ScalarType::BF16};
    ASSERT_TRUE(validateModule(*producer));
    for (const uint64_t bits :
         {0u, 0x8000u, 0x3f80u, 0x4000u, 0x7f80u, 0x7fc1u, 0xffffu}) {
      std::get<ResolvedImmediate>(source.value).bits = bits;
      EXPECT_TRUE(validateModule(*producer));
      EXPECT_TRUE(validateModule(*ast, *producer));
    }
    source.value = ResolvedImmediate{.bits = 0x3fc0, .type = ScalarType::BF16};
    const auto expected_locs = source.locs;
    ResolvedModule copied = *producer, assigned;
    assigned = copied;
    ResolvedFunction function = copied.functions.front(), function_assigned;
    function_assigned = function;
    assigned.functions.front() = std::move(function_assigned);
    assigned.functions.front().body.front() = function.body.front()->clone();
    producer = std::unexpected(ModuleResolveDiagnostics{});
    ASSERT_TRUE(validateModule(copied));
    ASSERT_TRUE(validateModule(assigned));
    ResolvedModule moved = std::move(assigned);
    assigned = std::move(moved);
    for (auto* owner : {&copied, &assigned}) {
      auto& field = atomicBf16Source(*owner->functions.front().body.front());
      EXPECT_EQ(field.locs, expected_locs);
      AtomicBf16Observer observer(field);
      owner->functions.front().body.front()->visit_references(observer);
      EXPECT_EQ(observer.matches, 1u);
      EXPECT_TRUE(validateModule(*ast, *owner));
    }
    auto& current = atomicBf16Source(*assigned.functions.front().body.front());
    const auto valid = std::get<ResolvedImmediate>(current.value);
    ASSERT_EQ(assigned.functions.front().instruction_ranges.size(), 1u);
    ASSERT_TRUE(assigned.functions.front().body.front()->check(
        {.target = {.ptx_version = {9, 3}, .sm_version = 90},
         .instruction_range =
             assigned.functions.front().instruction_ranges.front()}));
    for (unsigned mutation = 0; mutation < 4; ++mutation) {
      auto invalid = valid;
      if (mutation == 0)
        invalid.bits |= uint64_t{1} << 16;
      if (mutation == 1)
        invalid.type = ScalarType::F16;
      if (mutation == 2)
        invalid.integer_source_bits = 0;
      if (mutation == 3)
        invalid.is_negative = true;
      current.value = invalid;
      ASSERT_EQ(current.locs.size(), 1u);
      expectAtomicBf16Invalid(
          *ast, assigned, current.locs.front(),
          mutation == 1 ? checker::CheckDiagnosticKind::OperandTypeMismatch
                        : checker::CheckDiagnosticKind::ImmediateValueMismatch);
      current.value = valid;
      EXPECT_TRUE(validateModule(assigned));
      EXPECT_TRUE(validateModule(*ast, assigned));
    }
  }
}

/** Keep numeric origin, register domain, policy, alignment, targets and AST identity independent. */
TEST(ScalarAtomicBf16Sources, CausalSourceAndOwningNegatives) {
  for (const auto& item : cases) {
    SCOPED_TRACE(item.operation);
    for (const auto literal : {"1", "0x3fc0", "1+1"})
      EXPECT_FALSE(ownedAtomicBf16(atomicBf16Text(item, literal)));
    for (const auto type : {"f16", "u16", "s16", "b32", "f32", "pred"})
      EXPECT_FALSE(ownedAtomicBf16(atomicBf16Text(
          item, "bad", ".reg ." + std::string(type) + " bad; ")));
    EXPECT_FALSE(ownedAtomicBf16(atomicBf16Text(item, "1.5", "", "sm_80")));
    const auto text = atomicBf16Text(item, "1.5");
    PtxSyntaxParser parser(text);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty()) << text;
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module);
    ASSERT_EQ(module->functions.size(), 1u);
    ASSERT_EQ(module->functions.front().body.size(), 1u);
    ASSERT_TRUE(module->functions.front().body.front());
    ASSERT_EQ(module->functions.size(), 1u);
    ASSERT_EQ(module->functions.front().body.size(), 1u);
    ASSERT_TRUE(validateModule(*ast, *module));
    auto& source = atomicBf16Source(*module->functions.front().body.front());
    std::get<ResolvedImmediate>(source.value).integer_source_bits = 0;
    EXPECT_FALSE(validateModule(*ast, *module));
    std::get<ResolvedImmediate>(source.value).integer_source_bits.reset();
    const auto changed_text = atomicBf16Text(item, "2.5");
    PtxSyntaxParser changed_parser(changed_text);
    const auto changed = changed_parser.parseModule();
    ASSERT_TRUE(changed && changed.diagnostics.empty()) << changed_text;
    ASSERT_TRUE(resolveAndValidateModule(*changed));
    const auto mismatch = validateModule(*changed, *module);
    ASSERT_FALSE(mismatch);
    EXPECT_TRUE(
        std::ranges::any_of(mismatch.error(), [](const auto& diagnostic) {
          return diagnostic.kind ==
                 checker::CheckDiagnosticKind::ModuleSourceMismatch;
        }));
    auto registers = ownedAtomicBf16(atomicBf16Text(item, "h"));
    ASSERT_TRUE(registers);
    auto& reg = std::get<ResolvedRegisterRef>(
        atomicBf16Source(*registers->functions.front().body.front()).value);
    reg.register_class = ResolvedRegisterClass::Predicate;
    EXPECT_FALSE(validateModule(*registers));
    if (item.hinted) {
      auto& instruction = *module->functions.front().body.front();
      auto& policy =
          item.kind == AtomGlobalAddNoftzBf16::kind
              ? dynamic_cast<AtomGlobalAddNoftzBf16&>(instruction).cache_policy
              : dynamic_cast<RedGlobalAddNoftzBf16&>(instruction).cache_policy;
      ASSERT_TRUE(policy);
      policy->value.declared_type = ScalarType::B32;
      EXPECT_FALSE(validateModule(*module));
    }
    auto misaligned = text;
    const auto address = misaligned.find("[data]");
    ASSERT_NE(address, std::string::npos);
    misaligned.replace(address, 6, "[data+1]");
    EXPECT_FALSE(ownedAtomicBf16(misaligned));
  }
}

/** Current declaration carriers and their caches remain independent of the numeric grant. */
TEST(ScalarAtomicBf16Sources, CurrentRegisterTypesIdentityAndCachesBothApis) {
  for (const auto& item : cases) {
    const auto text = atomicBf16Text(item, "h",
                                     ".reg .f16 fbad; .reg .b32 wide; .reg "
                                     ".b16 peer; .reg .v2 .b16 whole; ");
    PtxSyntaxParser parser(text);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module);
    ASSERT_EQ(module->functions.size(), 1u);
    ASSERT_EQ(module->functions.front().body.size(), 1u);
    ASSERT_TRUE(module->functions.front().body.front());
    auto& field = atomicBf16Source(*module->functions.front().body.front());
    const auto valid = std::get<ResolvedRegisterRef>(field.value);
    ASSERT_EQ(field.locs.size(), 1u);
    const auto range = field.locs.front();
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
      expectAtomicBf16Invalid(
          *ast, *module, range,
          checker::CheckDiagnosticKind::ModuleSourceMismatch);
      field.value = valid;
      ASSERT_TRUE(validateModule(*module));
      ASSERT_TRUE(validateModule(*ast, *module));
    }
    const auto scope =
        module->symbols.symbol(module->functions.front().symbol_id).owned_scope;
    ASSERT_TRUE(scope);
    const auto whole = module->symbols.lookup(*scope, "whole");
    ASSERT_TRUE(whole);
    auto whole_ref = valid;
    whole_ref.spelling = "whole";
    whole_ref.symbol_id = whole->symbol;
    whole_ref.parameterized_index = whole->parameterized_index;
    whole_ref.index.reset();
    whole_ref.vector_width = 2;
    field.value = whole_ref;
    expectAtomicBf16Invalid(
        *ast, *module, range,
        checker::CheckDiagnosticKind::UnsupportedOperandShape);
    field.value = valid;
    ASSERT_TRUE(validateModule(*ast, *module));
    const auto compatible = module->symbols.lookup(*scope, "peer");
    ASSERT_TRUE(compatible);
    auto retarget = valid;
    retarget.spelling = "peer";
    retarget.symbol_id = compatible->symbol;
    retarget.parameterized_index = compatible->parameterized_index;
    retarget.index.reset();
    const auto identity = module->source_identity;
    field.value = retarget;
    EXPECT_EQ(module->source_identity, identity);
    EXPECT_TRUE(validateModule(*module));
    EXPECT_TRUE(validateModule(*ast, *module));
    field.value = valid;
    for (const auto& donor : {std::pair{"fbad", ScalarType::F16},
                              std::pair{"wide", ScalarType::B32}}) {
      const auto bound = module->symbols.lookup(*scope, donor.first);
      ASSERT_TRUE(bound);
      auto bad = valid;
      bad.spelling = donor.first;
      bad.symbol_id = bound->symbol;
      bad.parameterized_index = bound->parameterized_index;
      bad.declared_type = donor.second;
      bad.index.reset();
      field.value =
          bad;  // Coherent current binding; only the physical source type is illegal.
      expectAtomicBf16Invalid(
          *ast, *module, range,
          checker::CheckDiagnosticKind::OperandTypeMismatch);
      field.value = valid;
      ASSERT_TRUE(validateModule(*ast, *module));
    }
  }
}

/** Strong family identity is current SymbolTable/member identity, never a numbered-cache fallback. */
TEST(ScalarAtomicBf16Sources,
     FamilyContradictionsAndCoherentRetargetsBothApis) {
  for (const auto& item : cases) {
    const auto text = atomicBf16Text(item, "%r0", ".reg .b16 %r<2>; ");
    PtxSyntaxParser parser(text);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto baseline = resolveAndValidateModule(*ast);
    ASSERT_TRUE(baseline);
    ASSERT_EQ(baseline->functions.size(), 1u);
    ASSERT_EQ(baseline->functions.front().body.size(), 1u);
    ASSERT_TRUE(baseline->functions.front().body.front());
    auto& field = atomicBf16Source(*baseline->functions.front().body.front());
    const auto valid = std::get<ResolvedRegisterRef>(field.value);
    ASSERT_TRUE(valid.symbol_id);
    ASSERT_EQ(valid.parameterized_index, 0u);
    ASSERT_EQ(baseline->symbols.symbol(*valid.symbol_id).parameterized_count,
              2u);
    ASSERT_EQ(field.locs.size(), 1u);
    for (unsigned mutation = 0; mutation < 4; ++mutation) {
      auto bad = valid;
      if (mutation == 0)
        bad.parameterized_index.reset();
      if (mutation == 1)
        bad.spelling = "%r1";
      if (mutation == 2)
        bad.parameterized_index = 1;
      if (mutation == 3)
        bad.parameterized_index = 2;
      field.value = bad;
      expectAtomicBf16Invalid(
          *ast, *baseline, field.locs.front(),
          checker::CheckDiagnosticKind::ModuleSourceMismatch);
      field.value = valid;
      ASSERT_TRUE(validateModule(*baseline));
      ASSERT_TRUE(validateModule(*ast, *baseline));
    }
    const auto identity = baseline->source_identity;
    for (unsigned positive = 0; positive < 3; ++positive) {
      auto current = *baseline;
      auto& ref = std::get<ResolvedRegisterRef>(
          atomicBf16Source(*current.functions.front().body.front()).value);
      if (positive < 2) {
        ref.spelling = "%r1";
        ref.parameterized_index = 1;
        ref.index = 1;
      }
      if (positive > 0)
        ref.index.reset();
      EXPECT_EQ(current.source_identity, identity);
      EXPECT_TRUE(validateModule(current));
      EXPECT_TRUE(validateModule(*ast, current));
      ResolvedModule copied = current, assigned;
      assigned = copied;
      assigned.functions.front().body.front() =
          copied.functions.front().body.front()->clone();
      auto& cloned = atomicBf16Source(*assigned.functions.front().body.front());
      AtomicBf16Observer observer(cloned);
      assigned.functions.front().body.front()->visit_references(observer);
      EXPECT_EQ(observer.matches, 1u);
      EXPECT_TRUE(validateModule(*ast, assigned));
    }
  }
}

/** Explicit ordinary component base/selector/leaf caches must rejoin their real declaration and use range. */
TEST(ScalarAtomicBf16Sources, ComponentProvenanceAndRangesBothApis) {
  for (const auto& item : cases) {
    const auto text = atomicBf16Text(item, "v.x", ".reg .v2 .b16 v; ");
    PtxSyntaxParser parser(text);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module);
    ASSERT_EQ(module->functions.size(), 1u);
    ASSERT_EQ(module->functions.front().body.size(), 1u);
    ASSERT_TRUE(module->functions.front().body.front());
    auto& field = atomicBf16Source(*module->functions.front().body.front());
    const auto valid = std::get<ResolvedRegisterRef>(field.value);
    const auto locations = field.locs;
    ASSERT_EQ(locations.size(), 1u);
    ASSERT_TRUE(valid.component && valid.component->selector);
    for (unsigned mutation = 0; mutation < 10; ++mutation) {
      auto bad = valid;
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
      if (mutation == 8)
        part.base_spelling = "h";
      field.value = bad;
      if (mutation == 9)
        field.locs.front().end.column++;
      expectAtomicBf16Invalid(
          *ast, *module, field.locs.front(),
          checker::CheckDiagnosticKind::ModuleSourceMismatch);
      field.value = valid;
      field.locs = locations;
      ASSERT_TRUE(validateModule(*module));
      ASSERT_TRUE(validateModule(*ast, *module));
    }
  }
}

/** Source failures carry their actual stage/category/use range; intrinsic early failures are owned-N_A. */
TEST(ScalarAtomicBf16Sources,
     SourceKindsPhysicalCarriersAndAvailabilityCauses) {
  for (const auto& item : cases) {
    for (const auto literal : {"1", "0x3fc0", "1+1"}) {
      const auto text = atomicBf16Text(item, literal);
      PtxSyntaxParser parser(text);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      for (const bool strict : {false, true}) {
        const auto result =
            strict ? resolveAndValidateModule(*ast) : resolveModuleOnly(*ast);
        ASSERT_FALSE(result);
        ASSERT_FALSE(result.error().empty());
        logAtomicBf16SourceFailure(literal, result.error());
        EXPECT_EQ(result.error().front().stage(),
                  ResolveDiagnosticStage::Resolution);
        EXPECT_FALSE(result.error().front().checker_kind);
        EXPECT_EQ(result.error().front().range, atomicBf16Range(text, literal));
      }
    }
    for (const auto type : {"f16", "u16", "s16", "bf16", "b32", "f32", "u32",
                            "s32", "f16x2", "bf16x2", "b64", "pred"}) {
      const auto text =
          atomicBf16Text(item, "bad", ".reg ." + std::string(type) + " bad; ");
      PtxSyntaxParser parser(text);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      const auto result = resolveAndValidateModule(*ast);
      ASSERT_FALSE(result);
      ASSERT_FALSE(result.error().empty());
      logAtomicBf16SourceFailure(type, result.error());
      const bool intrinsic = std::string_view(type) == "bf16" ||
                             std::string_view(type) == "bf16x2";
      const auto stage = intrinsic
                             ? ResolveDiagnosticStage::DeclarationSemantics
                         : std::string_view(type) == "pred"
                             ? ResolveDiagnosticStage::Resolution
                             : ResolveDiagnosticStage::Checking;
      EXPECT_EQ(result.error().front().stage(), stage);
      if (stage == ResolveDiagnosticStage::Checking) {
        EXPECT_EQ(result.error().front().checker_kind,
                  checker::CheckDiagnosticKind::OperandTypeMismatch);
        EXPECT_EQ(result.error().front().range, atomicBf16Range(text, "bad"));
      }
      // A source-intrinsic rejection never supplies an owned module for mutation.
      if (intrinsic)
        EXPECT_FALSE(resolveModuleOnly(*ast));
    }
    for (const auto operand : {"v", "%tid.x"}) {
      const auto text = atomicBf16Text(item, operand, ".reg .v2 .b16 v; ");
      PtxSyntaxParser parser(text);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      const auto result = resolveAndValidateModule(*ast);
      ASSERT_FALSE(result);
      ASSERT_FALSE(result.error().empty());
      logAtomicBf16SourceFailure(operand, result.error());
      EXPECT_EQ(result.error().front().stage(),
                ResolveDiagnosticStage::Resolution);
      EXPECT_FALSE(result.error().front().checker_kind);
      std::string operation(item.operation);
      operation.replace(operation.find('$'), 1, operand);
      EXPECT_TRUE(
          result.error().front().range == atomicBf16Range(text, operand) ||
          result.error().front().range == atomicBf16Range(text, operation));
      // These source-level shape rejections do not manufacture an owned negative.
    }
    for (unsigned context = 0; context < 5; ++context) {
      auto text = atomicBf16Text(item, "1.5");
      if (context == 0)
        text.replace(0, std::string(".version 9.3").size(), ".version 7.7");
      if (context == 1)
        text = atomicBf16Text(item, "1.5", "", "sm_80");
      if (context == 2)
        text.erase(0, text.find(".global"));
      if (context == 3)
        text.erase(0, text.find('\n') + 1);
      if (context == 4) {
        const auto start = text.find(".target");
        text.erase(start, text.find('\n', start) - start + 1);
      }
      PtxSyntaxParser parser(text);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      const auto module = resolveModuleOnly(*ast);
      ASSERT_TRUE(module);
      ASSERT_EQ(module->functions.size(), 1u);
      ASSERT_EQ(module->functions.front().body.size(), 1u);
      ASSERT_TRUE(module->functions.front().body.front());
      const auto strict = resolveAndValidateModule(*ast);
      ASSERT_FALSE(strict);
      ASSERT_FALSE(strict.error().empty());
      logAtomicBf16SourceFailure("context", strict.error());
      const auto kind =
          context == 0 ? checker::CheckDiagnosticKind::UnsupportedPtxVersion
          : context == 1
              ? checker::CheckDiagnosticKind::UnsupportedSmVersion
              : checker::CheckDiagnosticKind::MissingValidationContext;
      EXPECT_TRUE(
          std::ranges::any_of(strict.error(), [&](const auto& diagnostic) {
            return diagnostic.stage() == ResolveDiagnosticStage::Checking &&
                   diagnostic.checker_kind == kind &&
                   diagnostic.range.start.line > 0;
          }));
      const bool available = context >= 2;
      EXPECT_EQ(
          validateModule(*module, ModuleValidationPolicy::AvailableContext)
              .has_value(),
          available);
      EXPECT_EQ(validateModule(*ast, *module,
                               ModuleValidationPolicy::AvailableContext)
                    .has_value(),
                available);
      EXPECT_FALSE(validateModule(*module));
      EXPECT_FALSE(validateModule(*ast, *module));
    }
  }
}

/** Source permission cannot rescue a bad policy/destination/address or missing required noftz. */
TEST(ScalarAtomicBf16Sources, IndependentPolicyDestinationAndAddressCauses) {
  for (const auto& item : cases) {
    const auto text = atomicBf16Text(item, "1.5");
    PtxSyntaxParser parser(text);
    const auto ast = parser.parseModule();
    ASSERT_TRUE(ast && ast.diagnostics.empty());
    auto module = resolveAndValidateModule(*ast);
    ASSERT_TRUE(module);
    ASSERT_EQ(module->functions.size(), 1u);
    ASSERT_EQ(module->functions.front().body.size(), 1u);
    ASSERT_TRUE(module->functions.front().body.front());
    auto& instruction = *module->functions.front().body.front();
    if (item.hinted) {
      auto& policy =
          item.kind == AtomGlobalAddNoftzBf16::kind
              ? dynamic_cast<AtomGlobalAddNoftzBf16&>(instruction).cache_policy
              : dynamic_cast<RedGlobalAddNoftzBf16&>(instruction).cache_policy;
      ASSERT_TRUE(policy && policy->locs.size() == 1);
      const auto original = policy->value;
      policy->value.declared_type = ScalarType::B32;
      expectAtomicBf16Invalid(
          *ast, *module, policy->locs.front(),
          checker::CheckDiagnosticKind::OperandTypeMismatch);
      policy->value = original;
    }
    if (auto* atom = dynamic_cast<AtomGlobalAddNoftzBf16*>(&instruction)) {
      ASSERT_TRUE(atom->dst.value.register_ref && atom->dst.locs.size() == 1);
      const auto original = *atom->dst.value.register_ref;
      atom->dst.value.register_ref->declared_type = ScalarType::B32;
      expectAtomicBf16Invalid(
          *ast, *module, atom->dst.locs.front(),
          checker::CheckDiagnosticKind::OperandTypeMismatch);
      atom->dst.value.register_ref = original;
    }
    ASSERT_TRUE(validateModule(*ast, *module));
    auto unaligned = text;
    const auto start = unaligned.find("[data]");
    ASSERT_NE(start, std::string::npos);
    unaligned.replace(start, 6, "[data+1]");
    PtxSyntaxParser bad_parser(unaligned);
    const auto bad = bad_parser.parseModule();
    ASSERT_TRUE(bad && bad.diagnostics.empty());
    const auto bad_result = resolveAndValidateModule(*bad);
    ASSERT_FALSE(bad_result);
    EXPECT_TRUE(
        std::ranges::any_of(bad_result.error(), [&](const auto& diagnostic) {
          return diagnostic.stage() == ResolveDiagnosticStage::Checking &&
                 diagnostic.checker_kind ==
                     checker::CheckDiagnosticKind::AddressAlignmentMismatch &&
                 diagnostic.range == atomicBf16Range(unaligned, "[data+1]");
        }));
    auto no_flag = text;
    const auto flag = no_flag.find(".noftz");
    ASSERT_NE(flag, std::string::npos);
    no_flag.erase(flag, 6);
    PtxSyntaxParser flag_parser(no_flag);
    const auto flag_ast = flag_parser.parseModule();
    ASSERT_TRUE(flag_ast && flag_ast.diagnostics.empty());
    const auto flag_result = resolveAndValidateModule(*flag_ast);
    ASSERT_FALSE(flag_result);
    ASSERT_FALSE(flag_result.error().empty());
    EXPECT_EQ(flag_result.error().front().stage(),
              ResolveDiagnosticStage::Resolution);
    EXPECT_GT(flag_result.error().front().range.start.line, 0);
  }
}

/** Known current family contradictions reject even when whole-instruction checking awaits target facts. */
TEST(ScalarAtomicBf16Sources, CurrentFamilyFactsPrecedeContextDeferral) {
  for (const auto& item : cases)
    for (unsigned missing = 0; missing < 3; ++missing) {
      auto text = atomicBf16Text(item, "%r0", ".reg .b16 %r<2>; ");
      if (missing != 1)
        text.erase(0, std::string(".version 9.3\n").size());
      if (missing != 0) {
        const auto start = text.find(".target");
        text.erase(start, text.find('\n', start) - start + 1);
      }
      PtxSyntaxParser parser(text);
      const auto ast = parser.parseModule();
      ASSERT_TRUE(ast && ast.diagnostics.empty());
      const auto baseline = resolveModuleOnly(*ast);
      ASSERT_TRUE(baseline);
      ASSERT_EQ(baseline->functions.size(), 1u);
      ASSERT_EQ(baseline->functions.front().body.size(), 1u);
      ASSERT_TRUE(baseline->functions.front().body.front());
      constexpr auto policy = ModuleValidationPolicy::AvailableContext;
      ASSERT_TRUE(validateModule(*baseline, policy));
      ASSERT_TRUE(validateModule(*ast, *baseline, policy));
      for (const bool erase : {false, true}) {
        auto current = *baseline;
        auto& field = atomicBf16Source(*current.functions.front().body.front());
        auto& ref = std::get<ResolvedRegisterRef>(field.value);
        if (erase)
          ref.parameterized_index.reset();
        else
          ref.spelling = "%r1";
        ASSERT_EQ(field.locs.size(), 1u);
        expectAtomicBf16Invalid(
            *ast, current, field.locs.front(),
            checker::CheckDiagnosticKind::ModuleSourceMismatch, policy);
      }
    }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
