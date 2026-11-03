#include <algorithm>
#include <array>
#include <cctype>
#include <concepts>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <gtest/gtest.h>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#ifdef PTX_PACKED_ATOMIC_CAPTURE_GENERATED_VIEWS
namespace ptx_frontend::resolved_ir::packed_atomic_projection {
/** Actual member borrowed only during the immediately enclosed generated check call. */
const WithLocs<RegOrImm>* expected = nullptr;
/** Logical format selected by that actual instruction, independent of current payload mutations. */
ScalarType logical = ScalarType::Invalid;
/** Count actual generated src views, never a hand-built or visitor projection. */
unsigned observed = 0;
/** Accumulate exact pointer/value/location/descriptor mirror checks. */
bool mirrors = true;
#define PACKED_ATOMIC_OPERAND_PARAMETERS                           \
  std::span<const checker::OperandDescriptor> descriptors,         \
      std::span<const checker::FieldView> fields,                  \
      std::span<const checker::OperandView> views,                 \
      std::span<const checker::OperandTypeCompatibilityDescriptor> \
          compatibility, const checker::Context &context,          \
      const MatrixInstructionDescriptor *matrix
/** Call the matching library checker after the test-only link wrapper observes its arguments. */
extern "C" checker::CheckResult
    original_operands(PACKED_ATOMIC_OPERAND_PARAMETERS) asm(
        "__real__ZN12ptx_frontend11resolved_ir7checker14check_"
        "operandsESt4spanIKNS1_17OperandDescriptorELm18446744073709551615EES2_"
        "IKNS1_9FieldViewELm18446744073709551615EES2_IKNS1_"
        "11OperandViewELm18446744073709551615EES2_IKNS1_"
        "34OperandTypeCompatibilityDescriptorELm18446744073709551615EERKNS1_"
        "7ContextEPKNS0_27MatrixInstructionDescriptorE");
/** Intercept only the real generated checker call; production code and API remain untouched. */
extern "C" checker::CheckResult
    generated_operands(PACKED_ATOMIC_OPERAND_PARAMETERS) asm(
        "__wrap__ZN12ptx_frontend11resolved_ir7checker14check_"
        "operandsESt4spanIKNS1_17OperandDescriptorELm18446744073709551615EES2_"
        "IKNS1_9FieldViewELm18446744073709551615EES2_IKNS1_"
        "11OperandViewELm18446744073709551615EES2_IKNS1_"
        "34OperandTypeCompatibilityDescriptorELm18446744073709551615EERKNS1_"
        "7ContextEPKNS0_27MatrixInstructionDescriptorE");
/** Consume borrowed views synchronously, then forward the real checker result unchanged. */
checker::CheckResult generated_operands(PACKED_ATOMIC_OPERAND_PARAMETERS) {
  if (expected)
    for (const auto& view : views)
      if (view.field_id == "src") {
        ++observed;
        const auto descriptor =
            std::ranges::find(descriptors, std::string_view("src"),
                              &checker::OperandDescriptor::target_field_id);
        bool valid = descriptor != descriptors.end() &&
                     descriptor->type_expression.fixed_scalar_type == logical &&
                     descriptor->register_width_policy ==
                         base::ScalarTypeSizePolicy::SameWidth &&
                     view.locations.data() == expected->locs.data() &&
                     std::ranges::equal(view.locations, expected->locs);
        if (const auto* reg =
                std::get_if<ResolvedRegisterRef>(&expected->value))
          valid = valid &&
                  view.actual_shape == checker::OperandShape::Register &&
                  view.register_ref == reg &&
                  view.register_type == reg->declared_type &&
                  view.register_symbol_id == reg->symbol_id &&
                  view.register_class == reg->register_class &&
                  view.register_vector_width == reg->vector_width &&
                  !view.immediate_type && !view.immediate_bits &&
                  !view.integer_source_bits;
        else if (const auto* word =
                     std::get_if<ResolvedImmediate>(&expected->value))
          valid = valid &&
                  view.actual_shape == checker::OperandShape::Immediate &&
                  !view.register_ref && !view.register_type &&
                  !view.register_symbol_id && !view.register_class &&
                  !view.register_vector_width &&
                  view.immediate_type == word->type &&
                  view.immediate_bits == word->bits &&
                  view.immediate_is_negative == word->is_negative &&
                  view.integer_source_bits == word->integer_source_bits;
        else
          valid = false;
        mirrors = mirrors && valid;
      }
  return original_operands(descriptors, fields, views, compatibility, context,
                           matrix);
}
#undef PACKED_ATOMIC_OPERAND_PARAMETERS
}  // namespace ptx_frontend::resolved_ir::packed_atomic_projection
#endif

namespace ptx_frontend::resolved_ir {
namespace {

/** One existing class and one selected cache-policy layout, without a new opcode. */
struct PackedAtomicCase {
  /** True selects red; false selects atom and its unchanged destination. */
  bool reduction;
  /** Logical source format; its runtime packed value always occupies 32 bits. */
  bool bfloat;
  /** Select the existing layout with one extra B64 register operand. */
  bool policy;
};

/** Borrow a mutable source only from the four exact public classes being migrated. */
WithLocs<RegOrImm>* packedAtomicMember(Instruction& instruction) {
  if (auto* form = dynamic_cast<AtomGlobalAddNoftzF16x2*>(&instruction))
    return &form->src;
  if (auto* form = dynamic_cast<AtomGlobalAddNoftzBf16x2*>(&instruction))
    return &form->src;
  if (auto* form = dynamic_cast<RedGlobalAddNoftzF16x2*>(&instruction))
    return &form->src;
  if (auto* form = dynamic_cast<RedGlobalAddNoftzBf16x2*>(&instruction))
    return &form->src;
  return nullptr;
}

/** Read the selected layout from an exact class, preserving both existing layout indices. */
unsigned packedAtomicLayout(const Instruction& instruction) {
  if (const auto* p =
          dynamic_cast<const AtomGlobalAddNoftzF16x2*>(&instruction))
    return p->operand_layout.value;
  if (const auto* p =
          dynamic_cast<const AtomGlobalAddNoftzBf16x2*>(&instruction))
    return p->operand_layout.value;
  if (const auto* p = dynamic_cast<const RedGlobalAddNoftzF16x2*>(&instruction))
    return p->operand_layout.value;
  if (const auto* p =
          dynamic_cast<const RedGlobalAddNoftzBf16x2*>(&instruction))
    return p->operand_layout.value;
  throw std::logic_error("Expected one of the four packed atomic forms");
}

/** Return a logical packed format, independently of the B32 register carrier. */
ScalarType packedAtomicType(PackedAtomicCase item) {
  return item.bfloat ? ScalarType::BF16x2 : ScalarType::F16x2;
}

/** Build a written source using existing qualifier order and operand layout. */
std::string packedAtomicStatement(PackedAtomicCase item,
                                  std::string_view value) {
  return std::string(item.reduction ? "red" : "atom") + ".global.add.noftz" +
         (item.policy ? ".L2::cache_hint" : "") +
         (item.bfloat ? ".bf16x2 " : ".f16x2 ") +
         (item.reduction ? "" : "%dst, ") + "[cell], " + std::string(value) +
         (item.policy ? ", %policy;" : ";");
}

/** Own declaration text; spare actual symbols support coherent current retargets. */
std::string packedAtomicSource(PackedAtomicCase item, std::string_view value,
                               std::string_view version = "9.3",
                               std::string_view target = "sm_90") {
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) + R"ptx(
.address_size 64
.global .align 4 .b32 cell;
.entry packed_atomic() {
.reg .b32 %dst, %bits, %alternate;
.reg .b32 %family<2>;
.reg .f16x2 %native;
.reg .f32 %wrong;
.reg .b64 %policy, %wide;
.reg .b16 %short;
.reg .u32 %unsigned;
.reg .s32 %signed;
.reg .pred %predicate;
.reg .v2 .b32 %vector;
mov.b32 %alternate, %bits;
mov.b32 %wrong, %bits;
mov.b64 %wide, 0;
)ptx" + packedAtomicStatement(item, value) +
         "\nret;\n}\n";
}

/** Release the source string, parser and AST before returning the owning module. */
std::optional<ResolvedModule> packedAtomicOwned(PackedAtomicCase item,
                                                std::string_view value) {
  const std::string source = packedAtomicSource(item, value);
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty()) {
    ADD_FAILURE() << "Packed atomic source did not parse";
    return std::nullopt;
  }
  auto result = resolveAndValidateModule(*ast);
  if (!result) {
    ADD_FAILURE() << (result.error().empty() ? "Missing diagnostic"
                                             : result.error().front().message);
    return std::nullopt;
  }
  EXPECT_TRUE(validateModule(*ast, *result,
                             ModuleValidationPolicy::RequireCompleteContext));
  return std::move(*result);
}

/** Find the current atomic owner without confusing its preceding MOV instructions. */
Instruction* packedAtomicInstruction(ResolvedModule& module) {
  if (module.functions.size() != 1)
    return nullptr;
  for (auto& instruction : module.functions.front().body)
    if (instruction && packedAtomicMember(*instruction))
      return instruction.get();
  return nullptr;
}

/** Use complete healthy target facts so malformed payloads cannot hide behind availability. */
checker::Context packedAtomicContext() {
  const auto profile = base::find_target_profile("sm_90");
  if (!profile)
    throw std::logic_error("Missing supported sm_90 target profile");
  return {
      .target = {.ptx_version = {9, 3},
                 .sm_version = profile->identity.architecture.number,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
}

/** Observe the actual source synchronously; the generated descriptor is checked by the separate link wrapper. */
struct PackedAtomicObserver final : detail::IReferenceObserver {
  /** Actual owner member; the caller keeps it alive throughout synchronous traversal. */
  const WithLocs<RegOrImm>& expected;
  /** Logical packed type required by this exact selected class. */
  ScalarType type;
  /** Count only src callbacks, excluding the existing cache-policy register. */
  unsigned count = 0;
  /** Borrowed descriptor only between operand_context(d) and operand_context(nullptr). */
  const checker::OperandDescriptor* current = nullptr;

  /** Bind the observer to one live source member, without retaining callback values. */
  PackedAtomicObserver(const WithLocs<RegOrImm>& value, ScalarType logical)
      : expected(value), type(logical) {}

  /** Keep source context only for its synchronous operand bracket. */
  void operand_context(const checker::OperandDescriptor* descriptor,
                       std::optional<MemoryStateSpace>) override {
    current = descriptor && descriptor->target_field_id == "src" ? descriptor
                                                                 : nullptr;
  }

  /** Consume real variant alternatives and rebuild an actual current OperandView in place. */
  void reg_or_imm(const RegOrImm& value, std::span<const SourceRange> locations,
                  checker::AddressSymbolResolutionPolicy) override {
    if (&value != &expected.value)
      return;
    const checker::OperandDescriptor projection{
        .target_field_id = "src",
        .type_expression = {.kind =
                                checker::OperandTypeExpressionKind::FixedScalar,
                            .fixed_scalar_type = type},
        .role = checker::OperandRole::Source,
        .access = checker::OperandAccess::Read,
        .allowed_shapes =
            checker::OperandShape::Register | checker::OperandShape::Immediate};
    ++count;
    EXPECT_EQ(&value, &expected.value);
    EXPECT_EQ(locations.size(), expected.locs.size());
    ASSERT_FALSE(locations.empty());
    ASSERT_FALSE(expected.locs.empty());
    EXPECT_EQ(locations.front(), expected.locs.front());
    EXPECT_EQ(locations.data(), expected.locs.data());
    EXPECT_EQ(projection.allowed_shapes, checker::OperandShape::Register |
                                             checker::OperandShape::Immediate);
    checker::OperandView view{.field_id = "src",
                              .actual_shape = checker::OperandShape::Immediate};
    view.locations = locations;
    if (const auto* word = std::get_if<ResolvedImmediate>(&value)) {
      view.immediate_type = word->type;
      view.immediate_bits = word->bits;
      view.immediate_is_negative = word->is_negative;
      view.integer_source_bits = word->integer_source_bits;
      EXPECT_EQ(word->type, type);
      EXPECT_FALSE(word->integer_source_bits);
      EXPECT_FALSE(word->is_negative);
      EXPECT_EQ(word->bits >> 32, 0u);
    } else {
      const auto* reg = std::get_if<ResolvedRegisterRef>(&value);
      ASSERT_NE(reg, nullptr);
      view.actual_shape = checker::OperandShape::Register;
      view.register_type = reg->declared_type;
      view.register_symbol_id = reg->symbol_id;
      view.register_class = reg->register_class;
      view.register_ref = reg;
      view.register_vector_width = reg->vector_width;
      EXPECT_TRUE(reg->symbol_id);
    }
    EXPECT_TRUE(checker::check_operands({&projection, 1}, {}, {&view, 1}, {},
                                        packedAtomicContext()));
  }
};

/** Require the generated public member's exact existing union type. */
static_assert(
    std::same_as<decltype(AtomGlobalAddNoftzF16x2{}.src), WithLocs<RegOrImm>>);
static_assert(
    std::same_as<decltype(AtomGlobalAddNoftzBf16x2{}.src), WithLocs<RegOrImm>>);
static_assert(
    std::same_as<decltype(RedGlobalAddNoftzF16x2{}.src), WithLocs<RegOrImm>>);
static_assert(
    std::same_as<decltype(RedGlobalAddNoftzBf16x2{}.src), WithLocs<RegOrImm>>);

/** Check actual current owner, exact identity, view and observer values together. */
void inspectPackedAtomic(ResolvedModule& module, PackedAtomicCase item,
                         std::optional<std::uint64_t> bits) {
  auto* instruction = packedAtomicInstruction(module);
  ASSERT_NE(instruction, nullptr);
  const auto expected_kind =
      item.reduction ? (item.bfloat ? InstructionKind::RedGlobalAddNoftzBf16x2
                                    : InstructionKind::RedGlobalAddNoftzF16x2)
                     : (item.bfloat ? InstructionKind::AtomGlobalAddNoftzBf16x2
                                    : InstructionKind::AtomGlobalAddNoftzF16x2);
  EXPECT_EQ(instruction->instruction_kind(), expected_kind);
  EXPECT_EQ(packedAtomicLayout(*instruction), item.policy ? 1u : 0u);
  auto* source = packedAtomicMember(*instruction);
  ASSERT_NE(source, nullptr);
  ASSERT_EQ(source->locs.size(), 1u);
  EXPECT_TRUE(instruction->check(packedAtomicContext()));
  EXPECT_TRUE(
      validateModule(module, ModuleValidationPolicy::RequireCompleteContext));
  if (bits) {
    const auto* word = std::get_if<ResolvedImmediate>(&source->value);
    ASSERT_NE(word, nullptr);
    EXPECT_EQ(word->type, packedAtomicType(item));
    EXPECT_EQ(word->bits, *bits);
  } else {
    const auto* reg = std::get_if<ResolvedRegisterRef>(&source->value);
    ASSERT_NE(reg, nullptr);
    EXPECT_TRUE(reg->symbol_id);
    EXPECT_TRUE(reg->declared_type);
  }
  PackedAtomicObserver observer(*source, packedAtomicType(item));
  instruction->visit_references(observer);
  EXPECT_EQ(observer.count, 1u);
  EXPECT_EQ(observer.current, nullptr);
}

/** Both layouts preserve legal register carriers and admit complete direct 0f words. */
TEST(Packed32AtomicSources, RegistersAndExactWords) {
  for (bool reduction : {false, true})
    for (bool bfloat : {false, true})
      for (bool policy : {false, true}) {
        const PackedAtomicCase item{reduction, bfloat, policy};
        SCOPED_TRACE(packedAtomicStatement(item, "src"));
        for (std::string_view carrier : {"%bits", "%native"}) {
          if (bfloat && carrier == "%native")
            continue;
          auto module = packedAtomicOwned(item, carrier);
          ASSERT_TRUE(module);
          inspectPackedAtomic(*module, item, std::nullopt);
          auto* instruction = packedAtomicInstruction(*module);
          ASSERT_NE(instruction, nullptr);
          auto* source = packedAtomicMember(*instruction);
          ASSERT_NE(source, nullptr);
          const auto* reg = std::get_if<ResolvedRegisterRef>(&source->value);
          ASSERT_NE(reg, nullptr);
          const auto saved = *reg;
          const auto ranges = source->locs;
          auto copied = *module;
          auto clone = instruction->clone();
          module.reset();
          inspectPackedAtomic(copied, item, std::nullopt);
          auto* copied_instruction = packedAtomicInstruction(copied);
          ASSERT_NE(copied_instruction, nullptr);
          auto* copied_source = packedAtomicMember(*copied_instruction);
          ASSERT_NE(copied_source, nullptr);
          const auto* copied_register =
              std::get_if<ResolvedRegisterRef>(&copied_source->value);
          ASSERT_NE(copied_register, nullptr);
          EXPECT_EQ(*copied_register, saved);
          EXPECT_EQ(copied_source->locs, ranges);
          auto* clone_source = packedAtomicMember(*clone);
          ASSERT_NE(clone_source, nullptr);
          const auto* clone_register =
              std::get_if<ResolvedRegisterRef>(&clone_source->value);
          ASSERT_NE(clone_register, nullptr);
          EXPECT_EQ(*clone_register, saved);
          EXPECT_EQ(clone_source->locs, ranges);
          EXPECT_TRUE(clone->check(packedAtomicContext()));
        }
        const auto literal = bfloat ? "0f3fc03fc0" : "0f3e003e00";
        const std::uint64_t word = bfloat ? 0x3fc03fc0 : 0x3e003e00;
        for (const auto& [spelling, bits] :
             {std::pair{literal, word},
              std::pair{"0fffffffff", std::uint64_t{0xffffffff}},
              std::pair{"0f80000000", std::uint64_t{0x80000000}}}) {
          auto module = packedAtomicOwned(item, spelling);
          ASSERT_TRUE(module);
          inspectPackedAtomic(*module, item, bits);
        }
      }
}

/** Compute an independently written final operand range, without consulting a resolved member. */
SourceRange packedAtomicWrittenRange(std::string_view text,
                                     std::string_view operand) {
  const auto offset = text.rfind(operand);
  if (offset == std::string_view::npos)
    throw std::logic_error("Missing written operand");
  SourcePos start{1, 1};
  for (char c : text.substr(0, offset)) {
    if (c == '\n') {
      ++start.line;
      start.column = 1;
    } else
      ++start.column;
  }
  return {start,
          {start.line, start.column + static_cast<int32_t>(operand.size())}};
}

/** Require current-reference identity failures at the independently saved use range. */
void expectPackedReferenceCause(const checker::CheckResult& checked,
                                SourceRange range, bool overflow = false) {
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

/** Source-only failures retain their real diagnostic origin, with no fictitious owner. */
TEST(Packed32AtomicSources, SourceBoundaries) {
  for (bool reduction : {false, true})
    for (bool bfloat : {false, true})
      for (bool policy : {false, true}) {
        const PackedAtomicCase item{reduction, bfloat, policy};
        auto control = packedAtomicOwned(item, "%bits");
        ASSERT_TRUE(control);
        for (std::string_view declared :
             {"b16", "f16", "u16", "s16", "bf16", "f32", "u32", "s32", "bf16x2",
              "f16x2", "b64", "pred"}) {
          if (!bfloat && declared == "f16x2")
            continue;
          std::string text = packedAtomicSource(item, "%candidate");
          const auto declarations = text.find(".reg .b32");
          ASSERT_NE(declarations, std::string::npos);
          text.insert(declarations,
                      ".reg ." + std::string(declared) + " %candidate;\n");
          PtxSyntaxParser parser(text);
          const auto ast = parser.parseModule();
          ASSERT_TRUE(ast && ast.diagnostics.empty());
          const auto result = resolveAndValidateModule(*ast);
          ASSERT_FALSE(result) << declared;
          ASSERT_FALSE(result.error().empty());
          const auto written = packedAtomicWrittenRange(text, "%candidate");
          EXPECT_TRUE(std::ranges::any_of(result.error(), [&](const auto&
                                                                  diagnostic) {
            auto message = diagnostic.message;
            std::transform(message.begin(), message.end(), message.begin(),
                           [](unsigned char c) {
                             return static_cast<char>(std::tolower(c));
                           });
            const bool intrinsic = declared == "bf16" || declared == "bf16x2";
            if (intrinsic)
              return diagnostic.range.start.line > 0 &&
                     message.find(declared) != std::string::npos &&
                     (diagnostic.stage() ==
                          ResolveDiagnosticStage::DeclarationSemantics ||
                      diagnostic.checker_kind ==
                          checker::CheckDiagnosticKind::OperandTypeMismatch);
            return diagnostic.range == written &&
                   (diagnostic.checker_kind ==
                        checker::CheckDiagnosticKind::OperandTypeMismatch ||
                    message.find("non-predicate register") !=
                        std::string::npos);
          }));
          for (const auto& diagnostic : result.error())
            std::cout << "packed-atomic-register-declaration|" << reduction
                      << bfloat << policy << "|" << declared
                      << "|stage=" << static_cast<int>(diagnostic.stage())
                      << "|checker="
                      << (diagnostic.checker_kind
                              ? static_cast<int>(*diagnostic.checker_kind)
                              : -1)
                      << "|" << diagnostic.message << '\n';
        }
        for (std::string_view token :
             {"0", "0x3e003e00", "1+2", "1.5==1.5", "-0f3e003e00",
              "+0f3e003e00", "(0f3e003e00)", "0d3ff8000000000000", "1.5",
              "%short", "%wrong", "%unsigned", "%signed", "%wide", "%predicate",
              "%vector"}) {
          SCOPED_TRACE(packedAtomicStatement(item, token));
          const std::string source = packedAtomicSource(item, token);
          PtxSyntaxParser parser(source);
          const auto ast = parser.parseModule();
          ASSERT_TRUE(ast && ast.diagnostics.empty());
          const auto result = resolveAndValidateModule(*ast);
          ASSERT_FALSE(result);
          ASSERT_FALSE(result.error().empty());
          const auto written = packedAtomicWrittenRange(
              source,
              token == "(0f3e003e00)" ? std::string_view("0f3e003e00") : token);
          if (token.front() != '%') {
            EXPECT_TRUE(std::ranges::any_of(
                result.error(), [&](const auto& diagnostic) {
                  return diagnostic.stage() ==
                             ResolveDiagnosticStage::Resolution &&
                         !diagnostic.checker_kind &&
                         diagnostic.range == written &&
                         (diagnostic.message.find("incompatible") !=
                              std::string::npos ||
                          diagnostic.message.find("cannot have a sign") !=
                              std::string::npos ||
                          (token == "(0f3e003e00)" &&
                           diagnostic.message ==
                               "Exact single-precision bit patterns cannot "
                               "participate in constant expressions."));
                }));
          } else {
            EXPECT_TRUE(std::ranges::any_of(
                result.error(), [&](const auto& diagnostic) {
                  return diagnostic.range == written &&
                         (diagnostic.checker_kind ==
                              checker::CheckDiagnosticKind::
                                  OperandTypeMismatch ||
                          diagnostic.message.find("non-predicate") !=
                              std::string::npos ||
                          diagnostic.message.find("Vector register") !=
                              std::string::npos);
                }));
          }
          for (const auto& diagnostic : result.error()) {
            EXPECT_GT(diagnostic.range.start.line, 0);
            std::cout << "packed-atomic-source|" << reduction << bfloat
                      << policy << "|" << token
                      << "|stage=" << static_cast<int>(diagnostic.stage())
                      << "|checker="
                      << (diagnostic.checker_kind
                              ? static_cast<int>(*diagnostic.checker_kind)
                              : -1)
                      << "|" << diagnostic.message << '\n';
          }
        }
      }
}

/** Require the typed packed-word cause and saved range from either module validation API. */
void expectPackedWordCause(const checker::CheckResult& checked,
                           SourceRange range,
                           checker::CheckDiagnosticKind kind) {
  ASSERT_FALSE(checked);
  ASSERT_FALSE(checked.error().empty());
  EXPECT_TRUE(std::ranges::any_of(checked.error(), [&](const auto& diagnostic) {
    return diagnostic.kind == kind && diagnostic.range == range &&
           !diagnostic.message.empty();
  }));
}

/** Current words are checked by type/width/flags, without a floating lexical-history veto. */
TEST(Packed32AtomicSources, CurrentWordsAndOwnedLifetime) {
  for (bool reduction : {false, true})
    for (bool bfloat : {false, true})
      for (bool policy : {false, true}) {
        const PackedAtomicCase item{reduction, bfloat, policy};
        const auto literal = bfloat ? "0f3fc03fc0" : "0f3e003e00";
        auto module = packedAtomicOwned(item, literal);
        ASSERT_TRUE(module);
        auto* instruction = packedAtomicInstruction(*module);
        ASSERT_NE(instruction, nullptr);
        auto* source = packedAtomicMember(*instruction);
        ASSERT_NE(source, nullptr);
        const auto original = source->value;
        const auto* original_word = std::get_if<ResolvedImmediate>(&original);
        ASSERT_NE(original_word, nullptr);
        const auto original_locs = source->locs;
        ASSERT_EQ(original_locs.size(), 1u);
        source->value = ResolvedImmediate{.bits = 0xffffffff,
                                          .type = packedAtomicType(item)};
        inspectPackedAtomic(*module, item, 0xffffffff);
        {
          const std::string original_text = packedAtomicSource(item, literal);
          PtxSyntaxParser original_parser(original_text);
          const auto original_ast = original_parser.parseModule();
          ASSERT_TRUE(original_ast && original_ast.diagnostics.empty());
          EXPECT_TRUE(validateModule(*original_ast, *module));
        }
        auto copied = *module;
        ResolvedModule assigned;
        assigned = copied;
        auto moved = std::move(assigned);
        auto function_copy = moved.functions.front();
        ResolvedFunction function_assigned;
        function_assigned = function_copy;
        auto clone = instruction->clone();
        EXPECT_TRUE(clone->check(packedAtomicContext()));
        auto* cloned_source = packedAtomicMember(*clone);
        ASSERT_NE(cloned_source, nullptr);
        EXPECT_EQ(cloned_source->locs, original_locs);
        EXPECT_NE(cloned_source, source);
        module.reset();
        inspectPackedAtomic(moved, item, 0xffffffff);
        ASSERT_GT(function_assigned.body.size(), 3u);
        auto* function_instruction = function_assigned.body.at(3).get();
        ASSERT_NE(function_instruction, nullptr);
        EXPECT_TRUE(function_instruction->check(packedAtomicContext()));
        auto* function_source = packedAtomicMember(*function_instruction);
        ASSERT_NE(function_source, nullptr);
        PackedAtomicObserver function_observer(*function_source,
                                               packedAtomicType(item));
        function_instruction->visit_references(function_observer);
        EXPECT_EQ(function_observer.count, 1u);
        instruction = packedAtomicInstruction(moved);
        ASSERT_NE(instruction, nullptr);
        source = packedAtomicMember(*instruction);
        ASSERT_NE(source, nullptr);
        for (unsigned mutation = 0; mutation < 4; ++mutation) {
          source->value = original;
          auto* word = std::get_if<ResolvedImmediate>(&source->value);
          ASSERT_NE(word, nullptr);
          if (mutation == 0)
            word->bits |= std::uint64_t{1} << 32;
          if (mutation == 1) {
            word->integer_source_bits = 0;
            ASSERT_TRUE(word->integer_source_bits.has_value());
            EXPECT_EQ(*word->integer_source_bits, 0u);
          }
          if (mutation == 2)
            word->is_negative = true;
          if (mutation == 3)
            word->type = bfloat ? ScalarType::F16x2 : ScalarType::BF16x2;
          const auto direct = instruction->check(packedAtomicContext());
          ASSERT_FALSE(direct);
          ASSERT_FALSE(direct.error().empty());
          EXPECT_TRUE(std::ranges::any_of(direct.error(), [&](const auto&
                                                                  diagnostic) {
            return diagnostic.range == original_locs.front() &&
                   diagnostic.kind ==
                       (mutation == 3
                            ? checker::CheckDiagnosticKind::OperandTypeMismatch
                            : checker::CheckDiagnosticKind::
                                  ImmediateValueMismatch);
          }));
          const auto expected_kind =
              mutation == 3
                  ? checker::CheckDiagnosticKind::OperandTypeMismatch
                  : checker::CheckDiagnosticKind::ImmediateValueMismatch;
          expectPackedWordCause(validateModule(moved), original_locs.front(),
                                expected_kind);
          auto bad_copy = moved;
          EXPECT_FALSE(validateModule(bad_copy));
          auto bad_clone = instruction->clone();
          EXPECT_FALSE(bad_clone->check(packedAtomicContext()));
          const std::string good_source = packedAtomicSource(item, literal);
          PtxSyntaxParser parser(good_source);
          const auto good_ast = parser.parseModule();
          ASSERT_TRUE(good_ast && good_ast.diagnostics.empty());
          expectPackedWordCause(validateModule(*good_ast, moved),
                                original_locs.front(), expected_kind);
          source->value = original;
          ASSERT_TRUE(instruction->check(packedAtomicContext()));
          ASSERT_TRUE(validateModule(moved));
          ASSERT_TRUE(validateModule(*good_ast, moved));
        }
        source->value = original;
        inspectPackedAtomic(moved, item, original_word->bits);
        const std::string changed_source =
            packedAtomicSource(item, "0f00000000");
        PtxSyntaxParser changed_parser(changed_source);
        const auto changed_ast = changed_parser.parseModule();
        ASSERT_TRUE(changed_ast && changed_ast.diagnostics.empty());
        ASSERT_TRUE(resolveAndValidateModule(*changed_ast));
        const auto mismatch = validateModule(*changed_ast, moved);
        ASSERT_FALSE(mismatch);
        EXPECT_TRUE(
            std::ranges::any_of(mismatch.error(), [](const auto& diagnostic) {
              return diagnostic.kind ==
                     checker::CheckDiagnosticKind::ModuleSourceMismatch;
            }));
      }
}

/** Coherent declaration retargets differ from corrupt type, class and SymbolId caches. */
TEST(Packed32AtomicSources, RegisterAndComponentProvenance) {
  for (bool reduction : {false, true})
    for (bool bfloat : {false, true})
      for (bool policy : {false, true}) {
        const PackedAtomicCase item{reduction, bfloat, policy};
        auto module = packedAtomicOwned(item, "%bits");
        ASSERT_TRUE(module);
        auto* instruction = packedAtomicInstruction(*module);
        ASSERT_NE(instruction, nullptr);
        auto* source = packedAtomicMember(*instruction);
        ASSERT_NE(source, nullptr);
        ASSERT_EQ(source->locs.size(), 1u);
        const auto range = source->locs.front();
        const std::string written = packedAtomicSource(item, "%bits");
        EXPECT_EQ(range, packedAtomicWrittenRange(written, "%bits"));
        PtxSyntaxParser provided_parser(written);
        const auto provided = provided_parser.parseModule();
        ASSERT_TRUE(provided && provided.diagnostics.empty());
        const auto original = source->value;
        auto& body = module->functions.front().body;
        ASSERT_GE(body.size(), 4u);
        for (unsigned donor : {0u, 1u, 2u}) {
          const auto* mov = dynamic_cast<const MovScalar*>(body[donor].get());
          ASSERT_NE(mov, nullptr);
          ASSERT_TRUE(mov->dst_register);
          source->value = mov->dst_register->value;
          if (donor == 0) {
            inspectPackedAtomic(*module, item, std::nullopt);
          } else {
            const auto direct = instruction->check(packedAtomicContext());
            ASSERT_FALSE(direct);
            EXPECT_TRUE(std::ranges::any_of(
                direct.error(), [&](const auto& diagnostic) {
                  return diagnostic.kind == checker::CheckDiagnosticKind::
                                                OperandTypeMismatch &&
                         diagnostic.range == range;
                }));
            EXPECT_FALSE(validateModule(*module));
            EXPECT_FALSE(validateModule(*provided, *module));
          }
          source->value = original;
          EXPECT_TRUE(validateModule(*module));
          EXPECT_TRUE(validateModule(*provided, *module));
        }
        for (unsigned mutation = 0; mutation < 4; ++mutation) {
          source->value = original;
          auto* reg = std::get_if<ResolvedRegisterRef>(&source->value);
          ASSERT_NE(reg, nullptr);
          if (mutation == 0)
            reg->symbol_id = binding::SymbolId{.value = 999999u};
          if (mutation == 1)
            reg->declared_type.reset();
          if (mutation == 2)
            reg->register_class = ResolvedRegisterClass::Predicate;
          if (mutation == 3)
            reg->vector_width = 2;
          const auto current = validateModule(*module);
          const auto with_source = validateModule(*provided, *module);
          ASSERT_FALSE(current);
          ASSERT_FALSE(with_source);
          for (const auto* result : {&current, &with_source})
            EXPECT_TRUE(std::ranges::any_of(
                result->error(), [&](const auto& diagnostic) {
                  return diagnostic.kind == checker::CheckDiagnosticKind::
                                                ModuleSourceMismatch &&
                         diagnostic.range == range;
                }));
        }
        source->value = original;
        EXPECT_TRUE(validateModule(*module));
        auto component = packedAtomicOwned(item, "%vector.x");
        ASSERT_TRUE(component);
        auto* component_instruction = packedAtomicInstruction(*component);
        ASSERT_NE(component_instruction, nullptr);
        auto* member = packedAtomicMember(*component_instruction);
        ASSERT_NE(member, nullptr);
        auto* reference = std::get_if<ResolvedRegisterRef>(&member->value);
        ASSERT_NE(reference, nullptr);
        ASSERT_TRUE(reference->component);
        const auto saved = *reference;
        ASSERT_TRUE(saved.component->selector);
        ASSERT_EQ(member->locs.size(), 1u);
        const auto component_range = member->locs.front();
        const std::string component_text =
            packedAtomicSource(item, "%vector.x");
        EXPECT_EQ(component_range,
                  packedAtomicWrittenRange(component_text, "%vector.x"));
        PtxSyntaxParser component_parser(component_text);
        const auto component_ast = component_parser.parseModule();
        ASSERT_TRUE(component_ast && component_ast.diagnostics.empty());
        for (unsigned mutation = 0; mutation < 8; ++mutation) {
          *reference = saved;
          auto& part = *reference->component;
          if (mutation == 0)
            part.lane = part.declaration_width;
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
            reference->vector_width = 2;
          if (mutation == 7)
            reference->parameterized_index = 0;
          const auto current_component = validateModule(*component);
          const auto supplied_component =
              validateModule(*component_ast, *component);
          ASSERT_FALSE(current_component);
          ASSERT_FALSE(supplied_component);
          for (const auto* checked : {&current_component, &supplied_component})
            EXPECT_TRUE(std::ranges::any_of(
                checked->error(), [&](const auto& diagnostic) {
                  return diagnostic.range == component_range &&
                         (diagnostic.kind == checker::CheckDiagnosticKind::
                                                 ModuleSourceMismatch ||
                          diagnostic.kind == checker::CheckDiagnosticKind::
                                                 InvalidVectorOperand ||
                          diagnostic.kind == checker::CheckDiagnosticKind::
                                                 OperandTypeMismatch) &&
                         (diagnostic.message.find("component") !=
                              std::string::npos ||
                          diagnostic.message.find("register metadata") !=
                              std::string::npos ||
                          diagnostic.message.find("vector") !=
                              std::string::npos);
                }));
        }
        *reference = saved;
        inspectPackedAtomic(*component, item, std::nullopt);
        EXPECT_TRUE(validateModule(*component_ast, *component));
      }
}

/** Published instruction and layout floors remain independent of the source grant. */
TEST(Packed32AtomicSources, ContextAndUnrelatedOperands) {
  for (bool reduction : {false, true})
    for (bool bfloat : {false, true})
      for (bool policy : {false, true}) {
        const PackedAtomicCase item{reduction, bfloat, policy};
        auto module =
            packedAtomicOwned(item, bfloat ? "0f3fc03fc0" : "0f3e003e00");
        ASSERT_TRUE(module);
        auto* instruction = packedAtomicInstruction(*module);
        ASSERT_NE(instruction, nullptr);
        auto below = packedAtomicContext();
        below.target.ptx_version =
            bfloat ? checker::PtxVersion{7, 7} : checker::PtxVersion{6, 1};
        EXPECT_FALSE(instruction->check(below));
        const auto below_profile =
            base::find_target_profile(bfloat   ? "sm_80"
                                      : policy ? "sm_75"
                                               : "sm_50");
        ASSERT_TRUE(below_profile);
        checker::Context below_sm{
            .target = {
                .ptx_version = {9, 3},
                .sm_version = below_profile->identity.architecture.number,
                .enabled_family_features =
                    below_profile->enabled_family_features,
                .identity = below_profile->identity,
                .capabilities = below_profile->capabilities}};
        EXPECT_FALSE(instruction->check(below_sm));
        auto missing = packedAtomicContext();
        missing.target.ptx_version = {};
        EXPECT_FALSE(instruction->check(missing));
        if (!bfloat && policy) {
          auto below_layout = packedAtomicContext();
          below_layout.target.ptx_version = {7, 3};
          EXPECT_FALSE(instruction->check(below_layout));
        }
        std::string partial_source = packedAtomicSource(item, "0f3e003e00");
        const auto target_line = partial_source.find(".target sm_90\n");
        ASSERT_NE(target_line, std::string::npos);
        partial_source.erase(target_line,
                             std::string_view(".target sm_90\n").size());
        PtxSyntaxParser partial_parser(partial_source);
        const auto partial_ast = partial_parser.parseModule();
        ASSERT_TRUE(partial_ast && partial_ast.diagnostics.empty());
        auto partial = resolveModuleOnly(*partial_ast);
        ASSERT_TRUE(partial);
        EXPECT_FALSE(validateModule(
            *partial, ModuleValidationPolicy::RequireCompleteContext));
        EXPECT_TRUE(
            validateModule(*partial, ModuleValidationPolicy::AvailableContext));
        if (policy) {
          std::string source = packedAtomicSource(item, "0f3e003e00");
          const auto use = source.rfind(", %policy;");
          ASSERT_NE(use, std::string::npos);
          source.replace(use, 10, ", 0;");
          PtxSyntaxParser parser(source);
          const auto ast = parser.parseModule();
          ASSERT_TRUE(ast && ast.diagnostics.empty());
          EXPECT_FALSE(resolveAndValidateModule(*ast));
        }
      }
}

/** Exercise actual family membership and coherent current references with both module APIs. */
TEST(Packed32AtomicSources, ParameterizedCurrentReferencesBothApis) {
  for (bool reduction : {false, true})
    for (bool bfloat : {false, true})
      for (bool policy : {false, true}) {
        const PackedAtomicCase item{reduction, bfloat, policy};
        const std::string text = packedAtomicSource(item, "%family0");
        PtxSyntaxParser parser(text);
        const auto ast = parser.parseModule();
        ASSERT_TRUE(ast && ast.diagnostics.empty());
        auto baseline = packedAtomicOwned(item, "%family0");
        ASSERT_TRUE(baseline);
        auto* instruction = packedAtomicInstruction(*baseline);
        ASSERT_NE(instruction, nullptr);
        auto* field = packedAtomicMember(*instruction);
        ASSERT_NE(field, nullptr);
        ASSERT_EQ(field->locs.size(), 1u);
        const auto range = packedAtomicWrittenRange(text, "%family0");
        EXPECT_EQ(field->locs.front(), range);
        const auto* initial = std::get_if<ResolvedRegisterRef>(&field->value);
        ASSERT_NE(initial, nullptr);
        const auto valid = *initial;
        ASSERT_TRUE(valid.symbol_id);
        ASSERT_EQ(valid.parameterized_index, 0u);
        ASSERT_EQ(valid.index, 0u);
        EXPECT_EQ(
            baseline->symbols.symbol(*valid.symbol_id).parameterized_count, 2u);
        EXPECT_TRUE(validateModule(*baseline));
        EXPECT_TRUE(validateModule(*ast, *baseline));
        for (unsigned mutation = 0; mutation < 4; ++mutation) {
          auto current = *baseline;
          auto* owner = packedAtomicInstruction(current);
          ASSERT_NE(owner, nullptr);
          auto* slot = packedAtomicMember(*owner);
          ASSERT_NE(slot, nullptr);
          auto bad = valid;
          if (mutation == 0)
            bad.parameterized_index.reset();
          if (mutation == 1)
            bad.spelling = "%family1";
          if (mutation == 2)
            bad.parameterized_index = 1;
          if (mutation == 3)
            bad.parameterized_index = 2;
          slot->value = bad;
          EXPECT_EQ(current.source_identity, baseline->source_identity);
          expectPackedReferenceCause(validateModule(current), range,
                                     mutation == 3);
          expectPackedReferenceCause(validateModule(*ast, current), range,
                                     mutation == 3);
        }
        for (unsigned positive = 0; positive < 4; ++positive) {
          auto current = *baseline;
          auto* owner = packedAtomicInstruction(current);
          ASSERT_NE(owner, nullptr);
          auto* slot = packedAtomicMember(*owner);
          ASSERT_NE(slot, nullptr);
          auto ref = valid;
          if (positive < 2) {
            ref.spelling = "%family1";
            ref.parameterized_index = 1;
            ref.index = 1;
          }
          if (positive == 1 || positive == 2)
            ref.index.reset();
          if (positive == 3) {
            ASSERT_FALSE(current.functions.front().body.empty());
            const auto* mov = dynamic_cast<const MovScalar*>(
                current.functions.front().body.front().get());
            ASSERT_NE(mov, nullptr);
            ASSERT_TRUE(mov->dst_register);
            ref = mov->dst_register->value;
            EXPECT_FALSE(ref.parameterized_index);
          }
          slot->value = ref;
          EXPECT_EQ(current.source_identity, baseline->source_identity);
          EXPECT_TRUE(validateModule(current));
          EXPECT_TRUE(validateModule(*ast, current));
          auto copied = current;
          ResolvedModule assigned;
          assigned = copied;
          ResolvedFunction function = assigned.functions.front(),
                           function_assigned;
          function_assigned = function;
          assigned.functions.front() = function_assigned;
          auto* assigned_instruction = packedAtomicInstruction(assigned);
          ASSERT_NE(assigned_instruction, nullptr);
          auto clone = assigned_instruction->clone();
          EXPECT_TRUE(clone->check(packedAtomicContext()));
          EXPECT_TRUE(validateModule(assigned));
          EXPECT_TRUE(validateModule(*ast, assigned));
          inspectPackedAtomic(copied, item, std::nullopt);
        }
      }
}

#ifdef PTX_PACKED_ATOMIC_CAPTURE_GENERATED_VIEWS
/** Require the direct generated projection, distinct from the synchronous visitor/manual OperandView test. */
TEST(Packed32AtomicSources, ActualGeneratedOperandViews) {
  for (bool reduction : {false, true})
    for (bool bfloat : {false, true})
      for (bool policy : {false, true})
        for (bool immediate : {false, true}) {
          const PackedAtomicCase item{reduction, bfloat, policy};
          auto module = packedAtomicOwned(
              item,
              immediate ? (bfloat ? "0f3fc03fc0" : "0f3e003e00") : "%bits");
          ASSERT_TRUE(module);
          auto* instruction = packedAtomicInstruction(*module);
          ASSERT_NE(instruction, nullptr);
          auto* member = packedAtomicMember(*instruction);
          ASSERT_NE(member, nullptr);
          const auto before = packed_atomic_projection::observed;
          packed_atomic_projection::mirrors = true;
          packed_atomic_projection::logical = packedAtomicType(item);
          packed_atomic_projection::expected = member;
          const auto checked = instruction->check(packedAtomicContext());
          packed_atomic_projection::expected = nullptr;
          ASSERT_TRUE(checked);
          EXPECT_TRUE(packed_atomic_projection::mirrors);
          EXPECT_EQ(packed_atomic_projection::observed, before + 1);
          auto clone = instruction->clone();
          auto* clone_member = packedAtomicMember(*clone);
          ASSERT_NE(clone_member, nullptr);
          EXPECT_NE(clone_member, member);
          const auto clone_before = packed_atomic_projection::observed;
          packed_atomic_projection::expected = clone_member;
          const auto cloned = clone->check(packedAtomicContext());
          packed_atomic_projection::expected = nullptr;
          EXPECT_TRUE(cloned);
          EXPECT_TRUE(packed_atomic_projection::mirrors);
          EXPECT_EQ(packed_atomic_projection::observed, clone_before + 1);
        }
}
#endif

}  // namespace
}  // namespace ptx_frontend::resolved_ir
