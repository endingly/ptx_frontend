#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <ptx_frontend/resolved_ir/model/data_movement/cvt.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** Every destination format shares signed32 inputs but has its own carry topology. */
constexpr std::array<std::string_view, 8> pack_formats{
    "u16", "s16", "u8", "s8", "u4", "s4", "u2", "s2"};
/** Retain initialized same-width carriers and independent destination/carry objects. */
std::string pack_source(std::string_view format, std::string_view first = "a",
                        std::string_view second = "b",
                        std::string_view carrier = "s32") {
  const bool carry = format != "u16" && format != "s16";
  return ".version 9.3\n.target sm_75\n.entry kernel(){\n"
         ".reg .u32 d; .reg ." +
         std::string(carrier) +
         " a,b; .reg .b32 carry; .reg .s32 other,g<2>; .reg .b16 narrow; "
         ".reg .b64 wide; .reg .f32 floating; .reg .pred predicate; .reg .v2 "
         ".b32 V<2>;\n"
         "mov.b32 a,1; mov.b32 b,2; mov.b32 carry,0; mov.b32 other,7; "
         "mov.b32 g1,9; mov.b16 narrow,0; mov.b64 wide,0; "
         "mov.f32 floating,1.0; mov.pred predicate,1; mov.b32 V1.y,4;\n"
         "cvt.pack.sat." +
         std::string(format) + ".s32" + (carry ? ".b32" : "") + " d," +
         std::string(first) + "," + std::string(second) +
         (carry ? ",carry" : "") + ";\n}";
}
/** Check the concrete emitted class and written destination format. */
void expect_pack_form(const Instruction& instruction, std::string_view format) {
  if (format == "u8") {
    ASSERT_NE(dynamic_cast<const CvtPackSatU8S32B32*>(&instruction), nullptr);
    EXPECT_EQ(CvtPackSatU8S32B32::dst_type, ScalarType::U8);
    EXPECT_EQ(CvtPackSatU8S32B32::carry_type, ScalarType::B32);
  } else if (format == "u16" || format == "s16") {
    const auto* form = dynamic_cast<const CvtPackSat16S32*>(&instruction);
    ASSERT_NE(form, nullptr);
    EXPECT_EQ(form->dst_type.value,
              format == "u16" ? ScalarType::U16 : ScalarType::S16);
  } else {
    const auto* form = dynamic_cast<const CvtPackSatSmallS32B32*>(&instruction);
    ASSERT_NE(form, nullptr);
    const ScalarType expected = format == "s8"   ? ScalarType::S8
                                : format == "u4" ? ScalarType::U4
                                : format == "s4" ? ScalarType::S4
                                : format == "u2" ? ScalarType::U2
                                                 : ScalarType::S2;
    EXPECT_EQ(form->dst_type.value, expected);
    EXPECT_EQ(CvtPackSatSmallS32B32::carry_type, ScalarType::B32);
  }
}
/** Observe real initialized data references independently of their operand wrapper. */
struct PackRegisterCapture final : detail::IReferenceObserver {
  /** Expected declaration type of each scalar source. */
  ScalarType expected = ScalarType::Invalid;
  /** Two actual source visits are required; destination and carry are excluded. */
  size_t found = 0;
  /** Compare actual source identity/class/type without consulting source spelling history. */
  void reg(const ResolvedRegisterRef& value, std::span<const SourceRange>,
           checker::AddressSymbolResolutionPolicy) override {
    if (value.spelling != "a" && value.spelling != "b")
      return;
    ++found;
    EXPECT_TRUE(value.symbol_id);
    EXPECT_EQ(value.register_class, ResolvedRegisterClass::General);
    EXPECT_EQ(value.declared_type, expected);
  }
  /** Register-valued RegOrImm follows the same declaration observation. */
  void reg_or_imm(const RegOrImm& value, std::span<const SourceRange> locations,
                  checker::AddressSymbolResolutionPolicy policy) override {
    if (const auto* reference = std::get_if<ResolvedRegisterRef>(&value))
      reg(*reference, locations, policy);
  }
};
/** Source slots and ranges borrow only from the supplied live pack instruction. */
struct PackSourceSlots {
  /** Two independently mutable RegOrImm payloads, in written operand order. */
  std::array<RegOrImm*, 2> values;
  /** Per-source locations with the same instruction lifetime. */
  std::array<std::span<const SourceRange>, 2> ranges;
};
/** Expose the closed set of existing concrete pack forms without an operand framework. */
PackSourceSlots pack_slots(Instruction& instruction) {
  if (auto* form = dynamic_cast<CvtPackSatU8S32B32*>(&instruction))
    return {{{&form->src1.value, &form->src2.value}},
            {{form->src1.locs, form->src2.locs}}};
  if (auto* form = dynamic_cast<CvtPackSat16S32*>(&instruction))
    return {{{&form->src1.value, &form->src2.value}},
            {{form->src1.locs, form->src2.locs}}};
  auto& form = dynamic_cast<CvtPackSatSmallS32B32&>(instruction);
  return {{{&form.src1.value, &form.src2.value}},
          {{form.src1.locs, form.src2.locs}}};
}
/** Independent golden use-width word and original64-bit source facts. */
ResolvedImmediate expected_pack_integer(std::string_view literal) {
  if (literal == "-2")
    return {.bits = 0xfffffffeU,
            .type = ScalarType::S32,
            .is_negative = true,
            .integer_source_bits = 0xfffffffffffffffeULL};
  if (literal == "0xffffffff")
    return {.bits = 0xffffffffU,
            .type = ScalarType::S32,
            .integer_source_bits = 0xffffffffULL};
  if (literal == "0x100000001U")
    return {.bits = 1,
            .type = ScalarType::S32,
            .integer_source_bits = 0x100000001ULL};
  const uint64_t value = literal == "(1+2)" ? 3 : literal == "WARP_SZ" ? 32 : 1;
  return {.bits = value, .type = ScalarType::S32, .integer_source_bits = value};
}
/** Return strict owned IR after destroying its source text, parser and AST. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> owned_pack(
    std::string source) {
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveAndValidateModule(*ast);
}
/** Match the fixture's complete instruction-local target facts. */
const checker::Context kPackContext{
    .target = {.ptx_version = {9, 3}, .sm_version = 75}};
/** Revalidate independently owned module/function copies, assignments and clones. */
void expect_pack_ownership(const ResolvedModule& module) {
  EXPECT_TRUE(validateModule(module));
  ResolvedModule copy(module), assigned;
  assigned = copy;
  EXPECT_TRUE(validateModule(copy));
  EXPECT_TRUE(validateModule(assigned));
  ResolvedFunction function(module.functions.front()), function_assigned;
  function_assigned = function;
  assigned.functions.front() = std::move(function_assigned);
  assigned.functions.front().body.back() = function.body.back()->clone();
  EXPECT_TRUE(validateModule(assigned));
}
/** Reject malformed payloads without borrowing a mismatched replacement AST. */
void expect_invalid_pack_ownership(const ResolvedModule& module) {
  EXPECT_FALSE(module.functions.front().body.back()->check(kPackContext));
  EXPECT_FALSE(validateModule(module));
  ResolvedModule copy(module), assigned;
  assigned = copy;
  EXPECT_FALSE(validateModule(copy));
  EXPECT_FALSE(validateModule(assigned));
  ResolvedFunction function(module.functions.front()), function_assigned;
  function_assigned = function;
  EXPECT_FALSE(function.body.back()->check(kPackContext));
  EXPECT_FALSE(function_assigned.body.back()->check(kPackContext));
  assigned.functions.front().body.back() = function.body.back()->clone();
  EXPECT_FALSE(validateModule(assigned));
}

TEST(CvtPackSources, AdmitsIntegerSourcesAfterInitializedRegisterControls) {
  for (const auto format : pack_formats) {
    SCOPED_TRACE(format);
    for (const auto carrier : {"b32", "s32", "u32"}) {
      const auto ast =
          test_helpers::parseModule(pack_source(format, "a", "b", carrier));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      const auto control = resolveAndValidateModule(*ast);
      ASSERT_TRUE(control) << control.error().front().message;
      expect_pack_form(*control->functions.front().body.back(), format);
      PackRegisterCapture capture;
      capture.expected = std::string_view(carrier) == "b32"   ? ScalarType::B32
                         : std::string_view(carrier) == "s32" ? ScalarType::S32
                                                              : ScalarType::U32;
      control->functions.front().body.back()->visit_references(capture);
      EXPECT_EQ(capture.found, 2u);
    }
    for (const auto literal : {"1", "-2", "0xffffffff", "0x100000001U", "(1+2)",
                               "WARP_SZ", "(1.0<2.0)"})
      for (const unsigned position : {1U, 2U, 3U}) {
        SCOPED_TRACE(literal);
        SCOPED_TRACE(position);
        const auto ast = test_helpers::parseModule(
            pack_source(format, position & 1U ? literal : "a",
                        position & 2U ? literal : "b"));
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        const auto draft = resolveModuleOnly(*ast);
        EXPECT_TRUE(draft) << (draft ? "" : draft.error().front().message);
        EXPECT_TRUE(resolveAndValidateModule(*ast));
        if (draft)
          EXPECT_TRUE(validateModule(*ast, *draft));
        auto owned =
            owned_pack(pack_source(format, position & 1U ? literal : "a",
                                   position & 2U ? literal : "b"));
        ASSERT_TRUE(owned) << owned.error().front().message;
        const auto slots = pack_slots(*owned->functions.front().body.back());
        for (size_t index = 0; index < 2; ++index)
          if (position & (1U << index)) {
            const auto* immediate =
                std::get_if<ResolvedImmediate>(slots.values[index]);
            ASSERT_NE(immediate, nullptr);
            EXPECT_EQ(*immediate, expected_pack_integer(literal));
          }
        EXPECT_TRUE(owned->functions.front().body.back()->check(kPackContext));
        expect_pack_ownership(*owned);
      }
  }
}
TEST(CvtPackSources, PreservesIntegerCategoryAndIndependentCarryPrecision) {
  for (const auto format : pack_formats) {
    for (const auto value :
         {"1.5", "0f3f800000", "0d3ff0000000000000", "(1.0+2.0)"}) {
      const auto ast =
          test_helpers::parseModule(pack_source(format, value, "b"));
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
    if (format == "u16" || format == "s16")
      continue;
    for (const auto carry : {"0", "0f3f800000"}) {
      auto source = pack_source(format, "1", "-2");
      source.replace(source.rfind(",carry;"), 7,
                     "," + std::string(carry) + ";");
      const auto owned = owned_pack(source);
      ASSERT_TRUE(owned) << owned.error().front().message;
      expect_pack_ownership(*owned);
    }
  }
}

TEST(CvtPackSources, RechecksCurrentIntegerWordsAndRetainedSourceIdentity) {
  const auto source = pack_source("u8", "1", "b");
  const auto ast = test_helpers::parseModule(source);
  ASSERT_MODULE_PARSE_SUCCEEDS(ast);
  auto owned = owned_pack(source);
  ASSERT_TRUE(owned);
  const auto slots = pack_slots(*owned->functions.front().body.back());
  auto& value = std::get<ResolvedImmediate>(*slots.values[0]);
  value = {.bits = 2, .type = ScalarType::S32, .integer_source_bits = 2};
  EXPECT_TRUE(validateModule(*owned));
  EXPECT_TRUE(validateModule(*ast, *owned));
  value = {.bits = 0,
           .type = ScalarType::S32,
           .integer_source_bits = 0x8000000000000000ULL};
  EXPECT_TRUE(validateModule(
      *owned));  // Unsigned original sources need not carry a negative flag.
  value = {.bits = 1,
           .type = ScalarType::S32,
           .is_negative = true,
           .integer_source_bits = 0xffffffff00000001ULL};
  EXPECT_TRUE(validateModule(
      *owned));  // The original sign is independent of narrowed S32 sign.
  for (int mutation = 0; mutation < 3; ++mutation) {
    value = expected_pack_integer("1");
    if (mutation == 0)
      value.bits |= uint64_t{1} << 32;
    if (mutation == 1)
      value.integer_source_bits = 2;
    if (mutation == 2)
      value.type = ScalarType::F32;
    expect_invalid_pack_ownership(*owned);
  }
  value = expected_pack_integer("1");
  const auto replacement =
      test_helpers::parseModule(pack_source("u8", "2", "b"));
  ASSERT_MODULE_PARSE_SUCCEEDS(replacement);
  ASSERT_TRUE(resolveAndValidateModule(*replacement));
  const auto mismatch = validateModule(*replacement, *owned);
  ASSERT_FALSE(mismatch);
  EXPECT_TRUE(std::ranges::any_of(mismatch.error(), [](const auto& error) {
    return error.kind == checker::CheckDiagnosticKind::ModuleSourceMismatch;
  }));
}

TEST(CvtPackSources,
     RejoinsRealRegistersAndRetainsComponentsAndCompactMembers) {
  auto owned = owned_pack(pack_source("u8"));
  ASSERT_TRUE(owned);
  const auto slots = pack_slots(*owned->functions.front().body.back());
  auto& reference = std::get<ResolvedRegisterRef>(*slots.values[0]);
  const auto original = reference;
  const auto scope =
      owned->symbols.symbol(owned->functions.front().symbol_id).owned_scope;
  ASSERT_TRUE(scope);
  for (const auto& [name, type] :
       std::to_array<std::pair<std::string_view, ScalarType>>(
           {{"other", ScalarType::S32},
            {"narrow", ScalarType::B16},
            {"wide", ScalarType::B64},
            {"floating", ScalarType::F32},
            {"predicate", ScalarType::Pred}})) {
    const auto symbol = owned->symbols.lookup(*scope, name);
    ASSERT_TRUE(symbol);
    reference = original;
    reference.spelling = name;
    reference.symbol_id = symbol->symbol;
    reference.declared_type = type;
    reference.register_class = type == ScalarType::Pred
                                   ? ResolvedRegisterClass::Predicate
                                   : ResolvedRegisterClass::General;
    if (name == "other") {
      expect_pack_ownership(*owned);
      reference.declared_type = ScalarType::U32;
      EXPECT_FALSE(validateModule(*owned));
    } else
      expect_invalid_pack_ownership(*owned);
  }
  reference = original;
  for (const auto operand : {"V1.y", "V1.g", "g1"}) {
    auto component = owned_pack(pack_source("s16", operand, "b"));
    ASSERT_TRUE(component) << component.error().front().message;
    expect_pack_ownership(*component);
    auto& ref = std::get<ResolvedRegisterRef>(
        *pack_slots(*component->functions.front().body.back()).values[0]);
    ASSERT_TRUE(ref.symbol_id);
    ASSERT_EQ(ref.parameterized_index, 1u);
    if (std::string_view(operand) == "g1") {
      ref.parameterized_index = 2;
      ref.spelling = "g2";
      EXPECT_FALSE(validateModule(*component));
    } else {
      ASSERT_TRUE(ref.component);
      EXPECT_EQ(ref.component->lane, 1u);
      const auto scalar = ref;
      ref.spelling = ref.component->base_spelling;
      ref.component.reset();
      ref.vector_width = 2;
      expect_invalid_pack_ownership(*component);
      ref = scalar;
      ref.component->lane = 2;
      expect_invalid_pack_ownership(*component);
    }
  }
}

TEST(CvtPackSources, RetainsSourceShapesCarryTopologyAndTargetBoundaries) {
  for (const auto operand :
       {"narrow", "wide", "floating", "predicate", "%clock", "V1"}) {
    const auto ast = test_helpers::parseModule(pack_source("u8", operand, "b"));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
  for (const auto format : pack_formats) {
    for (const auto target : {"sm_72", "sm_75"}) {
      auto source = pack_source(format, "1", "2");
      source.replace(source.find("sm_75"), 5, target);
      const auto ast = test_helpers::parseModule(source);
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      const bool small =
          format == "u4" || format == "s4" || format == "u2" || format == "s2";
      EXPECT_EQ(resolveAndValidateModule(*ast).has_value(),
                !small || std::string_view(target) == "sm_75");
    }
  }
  for (const auto format : {"u16", "u8"}) {
    for (const auto removal : {".pack", ".sat"}) {
      auto source = pack_source(format, "1", "2");
      source.erase(source.find(removal), std::string_view(removal).size());
      const auto ast = test_helpers::parseModule(source);
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      EXPECT_FALSE(resolveAndValidateModule(*ast));
    }
    auto destination = pack_source(format, "1", "2");
    destination.replace(destination.find(" d,1"), 2, " 0");
    const auto ast = test_helpers::parseModule(destination);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_FALSE(resolveAndValidateModule(*ast));
  }
  for (const auto random : {"random", "0"}) {
    const std::string source =
        ".version 9.3\n.target sm_100a\n.entry kernel(){"
        ".reg .b32 d; .reg .b64 random; .reg .f32 a,b; "
        "cvt.rs.f16x2.f32 d,a,b," +
        std::string(random) + ";}";
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    EXPECT_EQ(resolveAndValidateModule(*ast).has_value(),
              std::string_view(random) == "random");
  }
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
