#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <optional>
#include <ptx_frontend/resolved_ir/model/comparison_and_selection/setp.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution_detail.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include "../src/ptx_resolved_ir_private.hpp"
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Return an existing scalar source; the instruction owns the borrowed field. */
WithLocs<RegOrImm>& bit16_source(Instruction& instruction, bool first) {
  if (auto* value = dynamic_cast<SetpF16*>(&instruction))
    return first ? value->src1 : value->src2;
  if (auto* value = dynamic_cast<SetpF16Boolean*>(&instruction))
    return first ? value->src1 : value->src2;
  if (auto* value = dynamic_cast<SetpBf16*>(&instruction))
    return first ? value->src1 : value->src2;
  if (auto* value = dynamic_cast<SetpBf16Boolean*>(&instruction))
    return first ? value->src1 : value->src2;
  throw std::runtime_error("Expected a scalar half SETP form.");
}
/** Prepare one legal source field without changing its sibling or target context. */
std::string bit16_module(bool bf16, bool boolean, bool first,
                         std::string_view source) {
  const std::string sibling = bf16 ? "b" : "h";
  return ".version 9.3\n.target sm_90\n.entry kernel(){.reg .pred p,q; .reg "
         ".b16 b; .reg .f16 h;\nsetp.eq" +
         std::string(boolean ? ".and" : "") + (bf16 ? ".bf16 p," : ".f16 p,") +
         (first ? std::string(source) : sibling) + "," +
         (first ? sibling : std::string(source)) + (boolean ? ",q;" : ";") +
         "\n}";
}
/** Own the producer text copy so its buffer, parser and AST die before returning IR. */
ResolvedModule own_bit16(std::string source) {
  PtxSyntaxParser parser(source);
  const auto ast = parser.parseModule();
  if (!ast || !ast.diagnostics.empty())
    throw std::runtime_error("SETP fixture parse failure");
  auto module = resolveAndValidateModule(*ast);
  if (!module)
    throw std::runtime_error(module.error().front().message);
  return std::move(*module);
}

/** Frozen independent source expectations; words are encoded bits, not numeric host conversions. */
struct Bit16SourceCase {
  /** Source spelling owns no transient parser state. */
  std::string_view text;
  /** Exact narrowed/current payload and evaluated64bit source cache. */
  uint64_t bits, source_bits;
  /** True only for a signed negative source, not an unsigned high-bit pattern. */
  bool negative;
};
constexpr std::array raw_cases{
    Bit16SourceCase{"1", 0x0001ULL, 0x0000000000000001ULL, false},
    Bit16SourceCase{"0x3fc0", 0x3fc0ULL, 0x0000000000003fc0ULL, false},
    Bit16SourceCase{"-1", 0xffffULL, 0xffffffffffffffffULL, true},
    Bit16SourceCase{"0x10001", 0x0001ULL, 0x0000000000010001ULL, false},
    Bit16SourceCase{"18446744073709551615U", 0xffffULL, 0xffffffffffffffffULL,
                    false},
    Bit16SourceCase{"(0x10000+1)", 0x0001ULL, 0x0000000000010001ULL, false},
    Bit16SourceCase{"(1.5<2.0)", 0x0001ULL, 0x0000000000000001ULL, false},
    Bit16SourceCase{"!0", 0x0001ULL, 0x0000000000000001ULL, false},
    Bit16SourceCase{"WARP_SZ", 0x0020ULL, 0x0000000000000020ULL, false},
};
/** Independently fixed logical floating expectations for each numerical source. */
struct HalfSourceCase {
  /** Original spelling and two format-specific low16 words. */
  std::string_view text;
  uint64_t f16, bf16;
};
constexpr std::array numeric_cases{
    HalfSourceCase{"1.0", 0x3c00ULL, 0x3f80ULL},
    HalfSourceCase{"1.5", 0x3e00ULL, 0x3fc0ULL},
    HalfSourceCase{"(1+0.5)", 0x3e00ULL, 0x3fc0ULL},
    HalfSourceCase{"0d3ff8000000000000", 0x3e00ULL, 0x3fc0ULL},
    HalfSourceCase{"0f3fc00000", 0x3e00ULL, 0x3fc0ULL},
};
/** Reacquire actual fields after source death, copies, assignments and polymorphic cloning. */
void expectBit16Lifetime(const ResolvedModule& module, bool first,
                         const syntax_ast::AstModule& supplied_ast) {
  auto copied = module;
  ResolvedModule assigned;
  assigned = copied;
  auto moved = std::move(copied);
  ResolvedFunction function = assigned.functions.front(), function_assigned;
  function_assigned = function;
  assigned.functions.front() = function_assigned;
  assigned.functions.front().body.front() =
      function_assigned.body.front()->clone();
  for (auto* owner : {&assigned, &moved}) {
    const auto& field =
        bit16_source(*owner->functions.front().body.front(), first);
    EXPECT_EQ(field.locs.size(), 1u);
    EXPECT_NE(field.locs.front(), SourceRange{});
    EXPECT_TRUE(validateModule(*owner));
    EXPECT_TRUE(validateModule(supplied_ast, *owner));
  }
}

TEST(SetpExplicitBit16,
     RoutesIntegerAndFloatingCategoriesAcrossAllEightFields) {
  for (bool bf16 : {false, true})
    for (bool boolean : {false, true})
      for (bool first : {false, true}) {
        for (const auto& item : raw_cases) {
          SCOPED_TRACE(std::string(item.text));
          const auto source = bit16_module(bf16, boolean, first, item.text);
          auto module = own_bit16(source);
          const auto ast = test_helpers::parseModule(source);
          ASSERT_MODULE_PARSE_SUCCEEDS(ast);
          const auto& word = std::get<ResolvedImmediate>(
              bit16_source(*module.functions[0].body[0], first).value);
          EXPECT_EQ(word.type, ScalarType::B16);
          EXPECT_EQ(word.bits, item.bits);
          EXPECT_EQ(word.integer_source_bits, item.source_bits);
          EXPECT_EQ(word.is_negative, item.negative);
          EXPECT_TRUE(validateModule(module));
          EXPECT_TRUE(validateModule(*ast, module));
          expectBit16Lifetime(module, first, *ast);
        }
        for (const auto& item : numeric_cases) {
          SCOPED_TRACE(std::string(item.text));
          const auto source = bit16_module(bf16, boolean, first, item.text);
          auto module = own_bit16(source);
          const auto ast = test_helpers::parseModule(source);
          ASSERT_MODULE_PARSE_SUCCEEDS(ast);
          const auto& word = std::get<ResolvedImmediate>(
              bit16_source(*module.functions[0].body[0], first).value);
          EXPECT_EQ(word.type, bf16 ? ScalarType::BF16 : ScalarType::F16);
          EXPECT_EQ(word.bits, bf16 ? item.bf16 : item.f16);
          EXPECT_FALSE(word.integer_source_bits);
          EXPECT_FALSE(word.is_negative);
          EXPECT_TRUE(validateModule(*ast, module));
          expectBit16Lifetime(module, first, *ast);
        }
      }
}

/** Current arms are typed data; optional raw source caches constrain only retained facts. */
TEST(SetpExplicitBit16, ChecksCurrentArmsInsteadOfErasedProducerHistory) {
  for (bool bf16 : {false, true})
    for (bool boolean : {false, true})
      for (bool first : {false, true}) {
        const auto text = bit16_module(bf16, boolean, first, "1");
        auto baseline = own_bit16(text);
        const auto ast = test_helpers::parseModule(text);
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        const checker::Context context{
            .target = {.ptx_version = {9, 3}, .sm_version = 90},
            .instruction_range =
                baseline.functions.front().instruction_ranges.front()};
        for (int mutation = 0; mutation < 11; ++mutation) {
          SCOPED_TRACE(mutation);
          auto module = baseline;
          auto& value = std::get<ResolvedImmediate>(
              bit16_source(*module.functions.front().body.front(), first)
                  .value);
          const auto logical = bf16 ? ScalarType::BF16 : ScalarType::F16;
          if (mutation == 0)
            value.type = ScalarType::U16;
          if (mutation == 1)
            value.type = bf16 ? ScalarType::F16 : ScalarType::BF16;
          if (mutation == 2)
            value.bits = 0x10000;
          if (mutation == 3)
            value.bits = 2;
          if (mutation == 4) {
            value.integer_source_bits.reset();
            value.is_negative = true;
          }
          if (mutation == 5)
            value.is_negative = true;
          if (mutation == 6) {
            value.type = logical;
            value.integer_source_bits = 0;
          }
          if (mutation == 7) {
            value.type = logical;
            value.integer_source_bits.reset();
            value.bits = 0x10000;
          }
          if (mutation == 8) {
            value.type = logical;
            value.integer_source_bits.reset();
            value.is_negative = true;
          }
          if (mutation == 9) {
            value.bits = 0xffff;
            value.integer_source_bits = UINT64_MAX;
            value.is_negative = false;
          }
          if (mutation == 10) {
            value.integer_source_bits.reset();
            value.bits = 2;
            value.is_negative = false;
          }
          const auto direct =
              module.functions.front().body.front()->check(context);
          EXPECT_EQ(bool(direct), mutation >= 9);
          EXPECT_EQ(bool(validateModule(module)), mutation >= 9);
          EXPECT_EQ(bool(validateModule(*ast, module)), mutation >= 9);
          if (mutation < 9) {
            ASSERT_FALSE(direct);
            EXPECT_EQ(
                direct.error().front().range,
                bit16_source(*module.functions.front().body.front(), first)
                    .locs.front());
            EXPECT_EQ(
                direct.error().front().kind,
                mutation < 2
                    ? checker::CheckDiagnosticKind::OperandTypeMismatch
                    : checker::CheckDiagnosticKind::ImmediateValueMismatch);
          }
          if (mutation == 10) {
            expectBit16Lifetime(module, first, *ast);
            value.type = logical;
            value.bits = 1;
            EXPECT_TRUE(validateModule(module));
            EXPECT_TRUE(validateModule(*ast, module));
            value.type = ScalarType::B16;
            value.bits = 2;
            EXPECT_TRUE(validateModule(module));
          }
        }
        const auto changed =
            test_helpers::parseModule(bit16_module(bf16, boolean, first, "2"));
        ASSERT_MODULE_PARSE_SUCCEEDS(changed);
        ASSERT_TRUE(resolveAndValidateModule(*changed));
        const auto mismatch = validateModule(*changed, baseline);
        ASSERT_FALSE(mismatch);
        EXPECT_TRUE(std::any_of(
            mismatch.error().begin(), mismatch.error().end(),
            [](const auto& d) {
              return d.kind ==
                     checker::CheckDiagnosticKind::ModuleSourceMismatch;
            }));
      }
}

/** Manual metadata uses existing scalar facts, without mnemonic authentication or new shape tags. */
TEST(SetpExplicitBit16, ManualMetadataFailsClosedAndKeepsAbsentDefaults) {
  using namespace checker;
  EXPECT_FALSE(OperandDescriptor{}.integer_immediate_type);
  const SourceRange range{{1, 1}, {1, 2}};
  for (const auto logical : {ScalarType::F16, ScalarType::BF16}) {
    OperandDescriptor descriptor{
        .target_field_id = "src",
        .type_expression = {.kind = OperandTypeExpressionKind::FixedScalar,
                            .fixed_scalar_type = logical},
        .role = OperandRole::Source,
        .access = OperandAccess::Read,
        .allowed_shapes = OperandShape::Register | OperandShape::Immediate,
        .integer_immediate_type = ScalarType::B16};
    OperandView view{.field_id = "src",
                     .actual_shape = OperandShape::Immediate,
                     .immediate_type = ScalarType::B16,
                     .immediate_bits = 1,
                     .immediate_is_negative = false,
                     .locations = std::span{&range, 1}};
    const Context context{.instruction_range = range};
    const auto check = [&] {
      return check_operands(std::span{&descriptor, 1}, {}, std::span{&view, 1},
                            {}, context);
    };
    ASSERT_TRUE(check());
    view.immediate_type = logical;
    ASSERT_TRUE(check());
    view.immediate_type = ScalarType::B16;
    view.immediate_bits.reset();
    const auto no_bits = check();
    ASSERT_FALSE(no_bits);
    EXPECT_EQ(no_bits.error().front().kind,
              CheckDiagnosticKind::ImmediateValueMismatch);
    view.immediate_bits = 1;
    view.immediate_type.reset();
    const auto no_type = check();
    ASSERT_FALSE(no_type);
    EXPECT_EQ(no_type.error().front().kind,
              CheckDiagnosticKind::ImmediateValueMismatch);
    view.immediate_type = ScalarType::B16;
    const auto valid = descriptor;
    for (const auto actual :
         {OperandShape::Immediate, OperandShape::Register}) {
      view.actual_shape = actual;
      for (int mutation = 0; mutation < 17; ++mutation) {
        descriptor = valid;
        SCOPED_TRACE(mutation);
        if (mutation < 7) {
          constexpr std::array invalid{ScalarType::S16,
                                       ScalarType::U16,
                                       ScalarType::F16,
                                       ScalarType::B8,
                                       ScalarType::B32,
                                       ScalarType::Invalid,
                                       static_cast<ScalarType>(255)};
          descriptor.integer_immediate_type = invalid[mutation];
        }
        if (mutation == 7)
          descriptor.type_expression.fixed_scalar_type = ScalarType::F32;
        if (mutation == 8)
          descriptor.type_expression.fixed_scalar_type = ScalarType::B16;
        if (mutation == 9)
          descriptor.type_expression.fixed_scalar_type = ScalarType::F16x2;
        if (mutation == 10)
          descriptor.type_expression.kind =
              OperandTypeExpressionKind::ModifierField;
        if (mutation == 11)
          descriptor.access = OperandAccess::Write;
        if (mutation == 12)
          descriptor.access = OperandAccess::ReadWrite;
        if (mutation == 13)
          descriptor.role = OperandRole::Destination;
        if (mutation == 14)
          descriptor.allowed_shapes = OperandShape::Register;
        if (mutation == 15)
          descriptor.allowed_shapes = OperandShape::Immediate;
        if (mutation == 16)
          descriptor.allowed_shapes =
              OperandShape::Vector | OperandShape::Immediate;
        const auto rejected = check();
        ASSERT_FALSE(rejected);
        EXPECT_EQ(rejected.error().front().kind,
                  CheckDiagnosticKind::RuleViolation);
        EXPECT_EQ(rejected.error().front().range, range);
      }
    }
    descriptor = valid;
    descriptor.integer_immediate_type.reset();
    view.actual_shape = OperandShape::Immediate;
    view.immediate_type = logical;
    EXPECT_TRUE(check());
  }
}

/** Once-classified source arms retain mixed operands, root syntax and accepted conversion anchors. */
TEST(SetpExplicitBit16, PreservesMixedArmsRootSurfacesAndIndependentAnchors) {
  for (bool bf16 : {false, true})
    for (bool boolean : {false, true}) {
      for (bool first : {false, true}) {
        auto mixed = bit16_module(bf16, boolean, first, "1");
        const std::string_view sibling =
            first ? (bf16 ? ",b" : ",h") : (bf16 ? "b," : "h,");
        const auto pos = mixed.rfind(sibling);
        ASSERT_NE(pos, std::string::npos);
        mixed.replace(pos, sibling.size(), first ? ",1.5" : "1.5,");
        auto module = own_bit16(mixed);
        const auto& raw = std::get<ResolvedImmediate>(
            bit16_source(*module.functions.front().body.front(), first).value);
        const auto& numeric = std::get<ResolvedImmediate>(
            bit16_source(*module.functions.front().body.front(), !first).value);
        EXPECT_EQ(raw.type, ScalarType::B16);
        EXPECT_EQ(numeric.type, bf16 ? ScalarType::BF16 : ScalarType::F16);
        auto ftz = mixed;
        ftz.insert(ftz.rfind(bf16 ? ".bf16" : ".f16"), ".ftz");
        const auto ast = test_helpers::parseModule(ftz);
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        EXPECT_EQ(bool(resolveAndValidateModule(*ast)), !bf16);
      }
      for (const auto text : {"+0f3f800000", "-0f3f800000", "(0f3f800000)",
                              "0f3f800000+1.0", "(1?0f3f800000:0f3f800000)"}) {
        const auto ast =
            test_helpers::parseModule(bit16_module(bf16, boolean, true, text));
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        EXPECT_FALSE(resolveAndValidateModule(*ast));
      }
      auto anchor = own_bit16(
          bit16_module(bf16, boolean, true,
                       bf16 ? "0d3ff0100000000001" : "0d3ff0020000001000"));
      EXPECT_EQ(
          std::get<ResolvedImmediate>(
              bit16_source(*anchor.functions.front().body.front(), true).value)
              .bits,
          bf16 ? 0x3f81u : 0x3c01u);
    }
  for (const auto type : {ScalarType::F16, ScalarType::BF16}) {
    syntax_ast::AstImmediate source{
        .syntax = {.text = "1", .range = {{1, 1}, {1, 2}}},
        .kind = base::AstImmediateKind::DecimalInteger};
    EXPECT_FALSE(resolve_immediate_literal(source, type));
  }
}

/** Engaged manual resolver metadata must describe the existing RegOrImm field before dispatch. */
TEST(SetpExplicitBit16,
     ManualResolverChecksActualFieldKindBeforeRegisterDispatch) {
  using namespace check_end;
  ResolvedOperandBindingDescriptor binding{
      .target_field_id = "src",
      .type_expression = {.kind =
                              checker::OperandTypeExpressionKind::FixedScalar,
                          .fixed_scalar_type = ScalarType::F16},
      .role = checker::OperandRole::Source,
      .access = checker::OperandAccess::Read,
      .allowed_shapes =
          checker::OperandShape::Register | checker::OperandShape::Immediate,
      .integer_immediate_type = ScalarType::B16};
  for (const auto actual : {"%h0", "1", "1.5"}) {
    const auto parsed = test_helpers::parseInstruction(
        std::string("setp.eq.f16 p,h,") + actual + ";");
    ASSERT_TRUE(parsed && parsed.diagnostics.empty());
    ResolvedFieldDescriptor field{.field_id = "src",
                                  .value_kind = ResolvedValueKind::RegOrImm};
    const auto resolved = detail::resolve_operand_value(
        field, binding, parsed->operands.back(), {}, nullptr);
    ASSERT_TRUE(resolved) << resolved.error().message;
    for (const auto wrong :
         {ResolvedValueKind::Register, ResolvedValueKind::Immediate,
          ResolvedValueKind::ValueVector}) {
      field.value_kind = wrong;
      const auto result = detail::resolve_operand_value(
          field, binding, parsed->operands.back(), {}, nullptr);
      ASSERT_FALSE(result);
      EXPECT_EQ(result.error().range,
                syntax_ast::sourceRange(parsed->operands.back()));
      EXPECT_NE(result.error().message.find("integer immediate alternative"),
                std::string::npos);
    }
  }
}

/** Register declarations stay independent of the logical comparison format and raw immediate arm. */
TEST(SetpExplicitBit16,
     RetainsPhysicalRegisterDomainsAndUnknownStandaloneFacts) {
  for (bool bf16 : {false, true}) {
    auto module = own_bit16(bit16_module(bf16, false, true, "b"));
    auto& source = bit16_source(*module.functions.front().body.front(), true);
    const auto saved = source;
    const checker::Context ctx{
        .target = {.ptx_version = {9, 3}, .sm_version = 90}};
    for (const auto type : {ScalarType::S16, ScalarType::U16, ScalarType::B32,
                            ScalarType::B64, ScalarType::BF16}) {
      source = saved;
      auto& reg = std::get<ResolvedRegisterRef>(source.value);
      reg.declared_type = type;
      EXPECT_FALSE(module.functions.front().body.front()->check(ctx));
      EXPECT_FALSE(validateModule(module));
    }
    source = saved;
    std::get<ResolvedRegisterRef>(source.value).register_class =
        ResolvedRegisterClass::Predicate;
    EXPECT_FALSE(module.functions.front().body.front()->check(ctx));
    source = saved;
    std::get<ResolvedRegisterRef>(source.value).vector_width = 2;
    EXPECT_FALSE(module.functions.front().body.front()->check(ctx));
    source = saved;
    auto& unknown = std::get<ResolvedRegisterRef>(source.value);
    unknown.symbol_id.reset();
    unknown.declared_type.reset();
    EXPECT_TRUE(module.functions.front().body.front()->check(ctx));
    source = saved;
    EXPECT_TRUE(validateModule(module));
    const auto special =
        test_helpers::parseModule(bit16_module(bf16, false, true, "%tid.x"));
    ASSERT_MODULE_PARSE_SUCCEEDS(special);
    EXPECT_FALSE(resolveAndValidateModule(*special));
    auto whole = bit16_module(bf16, false, true, "V");
    whole.insert(whole.find("setp.eq"), ".reg .v2 .b16 V; ");
    const auto vector = test_helpers::parseModule(whole);
    ASSERT_MODULE_PARSE_SUCCEEDS(vector);
    EXPECT_FALSE(resolveAndValidateModule(*vector));
  }
}

/** Partial context is deferred rather than certified; complete targets keep the original floors. */
TEST(SetpExplicitBit16, PreservesAvailabilityMissingAndUnknownContext) {
  for (bool bf16 : {false, true}) {
    const auto text = bit16_module(bf16, false, true, "1");
    auto owned = own_bit16(text);
    for (const std::string token : {".version 9.3\n", ".target sm_90\n"}) {
      auto partial = text;
      partial.erase(partial.find(token), token.size());
      const auto ast = test_helpers::parseModule(partial);
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      auto module = resolveModuleOnly(*ast);
      ASSERT_TRUE(module);
      EXPECT_TRUE(
          validateModule(*module, ModuleValidationPolicy::AvailableContext));
      EXPECT_TRUE(validateModule(*ast, *module,
                                 ModuleValidationPolicy::AvailableContext));
      const auto strict = validateModule(*module);
      ASSERT_FALSE(strict);
      EXPECT_TRUE(std::any_of(
          strict.error().begin(), strict.error().end(), [](const auto& d) {
            return d.kind ==
                   checker::CheckDiagnosticKind::MissingValidationContext;
          }));
    }
    auto unknown = owned;
    for (auto& region : unknown.header.regions)
      for (auto& option : region.target_options)
        if (option.starts_with("sm_"))
          option = "sm_999";
    for (auto& f : unknown.functions)
      f.source_target = "sm_999";
    const auto rejected = validateModule(unknown);
    ASSERT_FALSE(rejected);
    EXPECT_TRUE(std::any_of(
        rejected.error().begin(), rejected.error().end(), [](const auto& d) {
          return d.kind == checker::CheckDiagnosticKind::UnknownTarget;
        }));
    checker::Context good{
        .target = {.ptx_version = bf16 ? checker::PtxVersion{7, 8}
                                       : checker::PtxVersion{4, 2},
                   .sm_version = bf16 ? 90u : 53u}};
    EXPECT_TRUE(owned.functions.front().body.front()->check(good));
    auto low = good;
    low.target.ptx_version =
        bf16 ? checker::PtxVersion{7, 7} : checker::PtxVersion{4, 1};
    EXPECT_FALSE(owned.functions.front().body.front()->check(low));
    low = good;
    --low.target.sm_version;
    EXPECT_FALSE(owned.functions.front().body.front()->check(low));
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
