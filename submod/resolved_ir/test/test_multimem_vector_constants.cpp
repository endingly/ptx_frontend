#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <ptx_frontend/resolved_ir/model/data_movement/multimem.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {
/** One independently frozen selected type/arity and its actual source partition. */
struct MultimemValueCase {
  /** Existing final-form selector identity; never used to grant permissions. */
  std::string_view variant;
  /** Complete matched register-control instruction. */
  std::string_view instruction;
  /** Fundamental physical carrier, distinct from logical instruction type. */
  std::string_view carrier;
  /** Selected logical element format. */
  ScalarType type;
  /** Number of scalar elements, at most eight. */
  size_t arity;
  /** Adopted literal surface; empty means literal source N_A, not typed-data N_A. */
  std::string_view literal;
  /** Independently adopted current typed word, not calculated by the converter. */
  uint64_t word;
};
constexpr std::array value_cases{
    MultimemValueCase{"multimem_st_float_none_v2_default_weak",
                      "multimem.st.global.v2.f16 [mm], {r0,r1};", "b16",
                      ScalarType::F16, 2, "1.5", 0x3e00},
    MultimemValueCase{"multimem_st_float_none_v2_default_weak",
                      "multimem.st.global.v2.f16x2 [mm], {r0,r1};", "b32",
                      ScalarType::F16x2, 2, "0f3e003e00", 0x3e003e00},
    MultimemValueCase{"multimem_st_float_none_v2_default_weak",
                      "multimem.st.global.v2.bf16 [mm], {r0,r1};", "b16",
                      ScalarType::BF16, 2, "1.5", 0x3fc0},
    MultimemValueCase{"multimem_st_float_none_v2_default_weak",
                      "multimem.st.global.v2.bf16x2 [mm], {r0,r1};", "b32",
                      ScalarType::BF16x2, 2, "0f3fc03fc0", 0x3fc03fc0},
    MultimemValueCase{"multimem_st_float_none_v2_default_weak",
                      "multimem.st.global.v2.f32 [mm], {r0,r1};", "b32",
                      ScalarType::F32, 2, "1.5", 0x3fc00000},
    MultimemValueCase{"multimem_st_float_none_v2_default_weak",
                      "multimem.st.global.v2.e5m2x2 [mm], {r0,r1};", "b16",
                      ScalarType::E5m2x2, 2, "", 0x3e3e},
    MultimemValueCase{"multimem_st_float_none_v2_default_weak",
                      "multimem.st.global.v2.e5m2x4 [mm], {r0,r1};", "b32",
                      ScalarType::E5m2x4, 2, "0f3e3e3e3e", 0x3e3e3e3e},
    MultimemValueCase{"multimem_st_float_none_v2_default_weak",
                      "multimem.st.global.v2.e4m3x2 [mm], {r0,r1};", "b16",
                      ScalarType::E4m3x2, 2, "", 0x3c3c},
    MultimemValueCase{"multimem_st_float_none_v2_default_weak",
                      "multimem.st.global.v2.e4m3x4 [mm], {r0,r1};", "b32",
                      ScalarType::E4m3x4, 2, "0f3c3c3c3c", 0x3c3c3c3c},
    MultimemValueCase{"multimem_st_float_none_v2_default_ordered",
                      "multimem.st.relaxed.gpu.global.v2.f16 [mm], {r0,r1};",
                      "b16", ScalarType::F16, 2, "1.5", 0x3e00},
    MultimemValueCase{"multimem_st_float_none_v2_default_ordered",
                      "multimem.st.relaxed.gpu.global.v2.f16x2 [mm], {r0,r1};",
                      "b32", ScalarType::F16x2, 2, "0f3e003e00", 0x3e003e00},
    MultimemValueCase{"multimem_st_float_none_v2_default_ordered",
                      "multimem.st.relaxed.gpu.global.v2.bf16 [mm], {r0,r1};",
                      "b16", ScalarType::BF16, 2, "1.5", 0x3fc0},
    MultimemValueCase{"multimem_st_float_none_v2_default_ordered",
                      "multimem.st.relaxed.gpu.global.v2.bf16x2 [mm], {r0,r1};",
                      "b32", ScalarType::BF16x2, 2, "0f3fc03fc0", 0x3fc03fc0},
    MultimemValueCase{"multimem_st_float_none_v2_default_ordered",
                      "multimem.st.relaxed.gpu.global.v2.f32 [mm], {r0,r1};",
                      "b32", ScalarType::F32, 2, "1.5", 0x3fc00000},
    MultimemValueCase{"multimem_st_float_none_v2_default_ordered",
                      "multimem.st.relaxed.gpu.global.v2.e5m2x2 [mm], {r0,r1};",
                      "b16", ScalarType::E5m2x2, 2, "", 0x3e3e},
    MultimemValueCase{"multimem_st_float_none_v2_default_ordered",
                      "multimem.st.relaxed.gpu.global.v2.e5m2x4 [mm], {r0,r1};",
                      "b32", ScalarType::E5m2x4, 2, "0f3e3e3e3e", 0x3e3e3e3e},
    MultimemValueCase{"multimem_st_float_none_v2_default_ordered",
                      "multimem.st.relaxed.gpu.global.v2.e4m3x2 [mm], {r0,r1};",
                      "b16", ScalarType::E4m3x2, 2, "", 0x3c3c},
    MultimemValueCase{"multimem_st_float_none_v2_default_ordered",
                      "multimem.st.relaxed.gpu.global.v2.e4m3x4 [mm], {r0,r1};",
                      "b32", ScalarType::E4m3x4, 2, "0f3c3c3c3c", 0x3c3c3c3c},
    MultimemValueCase{
        "multimem_red_float_add_v2_default_red",
        "multimem.red.relaxed.gpu.global.add.v2.f16 [mm], {r0,r1};", "b16",
        ScalarType::F16, 2, "1.5", 0x3e00},
    MultimemValueCase{
        "multimem_red_float_add_v2_default_red",
        "multimem.red.relaxed.gpu.global.add.v2.f16x2 [mm], {r0,r1};", "b32",
        ScalarType::F16x2, 2, "0f3e003e00", 0x3e003e00},
    MultimemValueCase{
        "multimem_red_float_add_v2_default_red",
        "multimem.red.relaxed.gpu.global.add.v2.bf16 [mm], {r0,r1};", "b16",
        ScalarType::BF16, 2, "1.5", 0x3fc0},
    MultimemValueCase{
        "multimem_red_float_add_v2_default_red",
        "multimem.red.relaxed.gpu.global.add.v2.bf16x2 [mm], {r0,r1};", "b32",
        ScalarType::BF16x2, 2, "0f3fc03fc0", 0x3fc03fc0},
    MultimemValueCase{
        "multimem_red_float_add_v2_default_red",
        "multimem.red.relaxed.gpu.global.add.v2.f32 [mm], {r0,r1};", "b32",
        ScalarType::F32, 2, "1.5", 0x3fc00000},
    MultimemValueCase{"multimem_st_float_none_v4_default_weak",
                      "multimem.st.global.v4.f16 [mm], {r0,r1,r2,r3};", "b16",
                      ScalarType::F16, 4, "1.5", 0x3e00},
    MultimemValueCase{"multimem_st_float_none_v4_default_weak",
                      "multimem.st.global.v4.f16x2 [mm], {r0,r1,r2,r3};", "b32",
                      ScalarType::F16x2, 4, "0f3e003e00", 0x3e003e00},
    MultimemValueCase{"multimem_st_float_none_v4_default_weak",
                      "multimem.st.global.v4.bf16 [mm], {r0,r1,r2,r3};", "b16",
                      ScalarType::BF16, 4, "1.5", 0x3fc0},
    MultimemValueCase{"multimem_st_float_none_v4_default_weak",
                      "multimem.st.global.v4.bf16x2 [mm], {r0,r1,r2,r3};",
                      "b32", ScalarType::BF16x2, 4, "0f3fc03fc0", 0x3fc03fc0},
    MultimemValueCase{"multimem_st_float_none_v4_default_weak",
                      "multimem.st.global.v4.f32 [mm], {r0,r1,r2,r3};", "b32",
                      ScalarType::F32, 4, "1.5", 0x3fc00000},
    MultimemValueCase{"multimem_st_float_none_v4_default_weak",
                      "multimem.st.global.v4.e5m2 [mm], {r0,r1,r2,r3};", "b8",
                      ScalarType::E5m2, 4, "1.5", 0x3e},
    MultimemValueCase{"multimem_st_float_none_v4_default_weak",
                      "multimem.st.global.v4.e5m2x2 [mm], {r0,r1,r2,r3};",
                      "b16", ScalarType::E5m2x2, 4, "", 0x3e3e},
    MultimemValueCase{"multimem_st_float_none_v4_default_weak",
                      "multimem.st.global.v4.e5m2x4 [mm], {r0,r1,r2,r3};",
                      "b32", ScalarType::E5m2x4, 4, "0f3e3e3e3e", 0x3e3e3e3e},
    MultimemValueCase{"multimem_st_float_none_v4_default_weak",
                      "multimem.st.global.v4.e4m3 [mm], {r0,r1,r2,r3};", "b8",
                      ScalarType::E4m3, 4, "1.5", 0x3c},
    MultimemValueCase{"multimem_st_float_none_v4_default_weak",
                      "multimem.st.global.v4.e4m3x2 [mm], {r0,r1,r2,r3};",
                      "b16", ScalarType::E4m3x2, 4, "", 0x3c3c},
    MultimemValueCase{"multimem_st_float_none_v4_default_weak",
                      "multimem.st.global.v4.e4m3x4 [mm], {r0,r1,r2,r3};",
                      "b32", ScalarType::E4m3x4, 4, "0f3c3c3c3c", 0x3c3c3c3c},
    MultimemValueCase{
        "multimem_st_float_none_v4_default_ordered",
        "multimem.st.relaxed.gpu.global.v4.f16 [mm], {r0,r1,r2,r3};", "b16",
        ScalarType::F16, 4, "1.5", 0x3e00},
    MultimemValueCase{
        "multimem_st_float_none_v4_default_ordered",
        "multimem.st.relaxed.gpu.global.v4.f16x2 [mm], {r0,r1,r2,r3};", "b32",
        ScalarType::F16x2, 4, "0f3e003e00", 0x3e003e00},
    MultimemValueCase{
        "multimem_st_float_none_v4_default_ordered",
        "multimem.st.relaxed.gpu.global.v4.bf16 [mm], {r0,r1,r2,r3};", "b16",
        ScalarType::BF16, 4, "1.5", 0x3fc0},
    MultimemValueCase{
        "multimem_st_float_none_v4_default_ordered",
        "multimem.st.relaxed.gpu.global.v4.bf16x2 [mm], {r0,r1,r2,r3};", "b32",
        ScalarType::BF16x2, 4, "0f3fc03fc0", 0x3fc03fc0},
    MultimemValueCase{
        "multimem_st_float_none_v4_default_ordered",
        "multimem.st.relaxed.gpu.global.v4.f32 [mm], {r0,r1,r2,r3};", "b32",
        ScalarType::F32, 4, "1.5", 0x3fc00000},
    MultimemValueCase{
        "multimem_st_float_none_v4_default_ordered",
        "multimem.st.relaxed.gpu.global.v4.e5m2 [mm], {r0,r1,r2,r3};", "b8",
        ScalarType::E5m2, 4, "1.5", 0x3e},
    MultimemValueCase{
        "multimem_st_float_none_v4_default_ordered",
        "multimem.st.relaxed.gpu.global.v4.e5m2x2 [mm], {r0,r1,r2,r3};", "b16",
        ScalarType::E5m2x2, 4, "", 0x3e3e},
    MultimemValueCase{
        "multimem_st_float_none_v4_default_ordered",
        "multimem.st.relaxed.gpu.global.v4.e5m2x4 [mm], {r0,r1,r2,r3};", "b32",
        ScalarType::E5m2x4, 4, "0f3e3e3e3e", 0x3e3e3e3e},
    MultimemValueCase{
        "multimem_st_float_none_v4_default_ordered",
        "multimem.st.relaxed.gpu.global.v4.e4m3 [mm], {r0,r1,r2,r3};", "b8",
        ScalarType::E4m3, 4, "1.5", 0x3c},
    MultimemValueCase{
        "multimem_st_float_none_v4_default_ordered",
        "multimem.st.relaxed.gpu.global.v4.e4m3x2 [mm], {r0,r1,r2,r3};", "b16",
        ScalarType::E4m3x2, 4, "", 0x3c3c},
    MultimemValueCase{
        "multimem_st_float_none_v4_default_ordered",
        "multimem.st.relaxed.gpu.global.v4.e4m3x4 [mm], {r0,r1,r2,r3};", "b32",
        ScalarType::E4m3x4, 4, "0f3c3c3c3c", 0x3c3c3c3c},
    MultimemValueCase{
        "multimem_red_float_add_v4_default_red",
        "multimem.red.relaxed.gpu.global.add.v4.f16 [mm], {r0,r1,r2,r3};",
        "b16", ScalarType::F16, 4, "1.5", 0x3e00},
    MultimemValueCase{
        "multimem_red_float_add_v4_default_red",
        "multimem.red.relaxed.gpu.global.add.v4.f16x2 [mm], {r0,r1,r2,r3};",
        "b32", ScalarType::F16x2, 4, "0f3e003e00", 0x3e003e00},
    MultimemValueCase{
        "multimem_red_float_add_v4_default_red",
        "multimem.red.relaxed.gpu.global.add.v4.bf16 [mm], {r0,r1,r2,r3};",
        "b16", ScalarType::BF16, 4, "1.5", 0x3fc0},
    MultimemValueCase{
        "multimem_red_float_add_v4_default_red",
        "multimem.red.relaxed.gpu.global.add.v4.bf16x2 [mm], {r0,r1,r2,r3};",
        "b32", ScalarType::BF16x2, 4, "0f3fc03fc0", 0x3fc03fc0},
    MultimemValueCase{
        "multimem_red_float_add_v4_default_red",
        "multimem.red.relaxed.gpu.global.add.v4.f32 [mm], {r0,r1,r2,r3};",
        "b32", ScalarType::F32, 4, "1.5", 0x3fc00000},
    MultimemValueCase{
        "multimem_st_float_none_v8_default_weak",
        "multimem.st.global.v8.f16 [mm], {r0,r1,r2,r3,r4,r5,r6,r7};", "b16",
        ScalarType::F16, 8, "1.5", 0x3e00},
    MultimemValueCase{
        "multimem_st_float_none_v8_default_weak",
        "multimem.st.global.v8.bf16 [mm], {r0,r1,r2,r3,r4,r5,r6,r7};", "b16",
        ScalarType::BF16, 8, "1.5", 0x3fc0},
    MultimemValueCase{
        "multimem_st_float_none_v8_default_weak",
        "multimem.st.global.v8.e5m2 [mm], {r0,r1,r2,r3,r4,r5,r6,r7};", "b8",
        ScalarType::E5m2, 8, "1.5", 0x3e},
    MultimemValueCase{
        "multimem_st_float_none_v8_default_weak",
        "multimem.st.global.v8.e5m2x2 [mm], {r0,r1,r2,r3,r4,r5,r6,r7};", "b16",
        ScalarType::E5m2x2, 8, "", 0x3e3e},
    MultimemValueCase{
        "multimem_st_float_none_v8_default_weak",
        "multimem.st.global.v8.e4m3 [mm], {r0,r1,r2,r3,r4,r5,r6,r7};", "b8",
        ScalarType::E4m3, 8, "1.5", 0x3c},
    MultimemValueCase{
        "multimem_st_float_none_v8_default_weak",
        "multimem.st.global.v8.e4m3x2 [mm], {r0,r1,r2,r3,r4,r5,r6,r7};", "b16",
        ScalarType::E4m3x2, 8, "", 0x3c3c},
    MultimemValueCase{"multimem_st_float_none_v8_default_ordered",
                      "multimem.st.relaxed.gpu.global.v8.f16 [mm], "
                      "{r0,r1,r2,r3,r4,r5,r6,r7};",
                      "b16", ScalarType::F16, 8, "1.5", 0x3e00},
    MultimemValueCase{"multimem_st_float_none_v8_default_ordered",
                      "multimem.st.relaxed.gpu.global.v8.bf16 [mm], "
                      "{r0,r1,r2,r3,r4,r5,r6,r7};",
                      "b16", ScalarType::BF16, 8, "1.5", 0x3fc0},
    MultimemValueCase{"multimem_st_float_none_v8_default_ordered",
                      "multimem.st.relaxed.gpu.global.v8.e5m2 [mm], "
                      "{r0,r1,r2,r3,r4,r5,r6,r7};",
                      "b8", ScalarType::E5m2, 8, "1.5", 0x3e},
    MultimemValueCase{"multimem_st_float_none_v8_default_ordered",
                      "multimem.st.relaxed.gpu.global.v8.e5m2x2 [mm], "
                      "{r0,r1,r2,r3,r4,r5,r6,r7};",
                      "b16", ScalarType::E5m2x2, 8, "", 0x3e3e},
    MultimemValueCase{"multimem_st_float_none_v8_default_ordered",
                      "multimem.st.relaxed.gpu.global.v8.e4m3 [mm], "
                      "{r0,r1,r2,r3,r4,r5,r6,r7};",
                      "b8", ScalarType::E4m3, 8, "1.5", 0x3c},
    MultimemValueCase{"multimem_st_float_none_v8_default_ordered",
                      "multimem.st.relaxed.gpu.global.v8.e4m3x2 [mm], "
                      "{r0,r1,r2,r3,r4,r5,r6,r7};",
                      "b16", ScalarType::E4m3x2, 8, "", 0x3c3c},
    MultimemValueCase{"multimem_red_float_add_v8_default_red",
                      "multimem.red.relaxed.gpu.global.add.v8.f16 [mm], "
                      "{r0,r1,r2,r3,r4,r5,r6,r7};",
                      "b16", ScalarType::F16, 8, "1.5", 0x3e00},
    MultimemValueCase{"multimem_red_float_add_v8_default_red",
                      "multimem.red.relaxed.gpu.global.add.v8.bf16 [mm], "
                      "{r0,r1,r2,r3,r4,r5,r6,r7};",
                      "b16", ScalarType::BF16, 8, "1.5", 0x3fc0},
};

/** Build a source operand without changing its independent address/profile legality. */
std::string valueSource(const MultimemValueCase& item, std::string_view operand,
                        std::string_view extra = "") {
  std::string instruction(item.instruction);
  instruction.replace(instruction.find('{'),
                      instruction.find('}') - instruction.find('{') + 1,
                      operand);
  return ".version 9.3\n.target sm_100a\n.address_size 64\n"
         ".global .align 16 .b8 mm[256];\n.entry kernel(){ .reg ." +
         std::string(item.carrier) + " r<" + std::to_string(item.arity) +
         ">; " + std::string(extra) + instruction + " }";
}

/** Make each lane explicit; mixed sources change only the final scalar leaf. */
std::string valueBraces(const MultimemValueCase& item, std::string_view last,
                        bool all_immediate = false) {
  std::string result = "{";
  for (size_t lane = 0; lane < item.arity; ++lane) {
    if (lane)
      result += ",";
    result += all_immediate || lane + 1 == item.arity
                  ? std::string(last)
                  : "r" + std::to_string(lane);
  }
  return result + "}";
}

/** Return only owned IR, after the actual parser, AST and source buffer die. */
std::expected<ResolvedModule, ModuleResolveDiagnostics> valueOwned(
    std::string source) {
  const auto ast = test_helpers::parseModule(source);
  EXPECT_TRUE(ast && ast.diagnostics.empty());
  if (!ast || !ast.diagnostics.empty())
    return std::unexpected(ModuleResolveDiagnostics{});
  return resolveAndValidateModule(*ast);
}

/** Borrow the actual migrated member through its concrete public form. */
WithLocs<ResolvedValueVector>& valueField(Instruction& instruction) {
  if (auto* form =
          dynamic_cast<MultimemStFloatNoneV2DefaultWeak*>(&instruction))
    return form->src;
  if (auto* form =
          dynamic_cast<MultimemStFloatNoneV2DefaultOrdered*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<MultimemRedFloatAddV2DefaultRed*>(&instruction))
    return form->src;
  if (auto* form =
          dynamic_cast<MultimemStFloatNoneV4DefaultWeak*>(&instruction))
    return form->src;
  if (auto* form =
          dynamic_cast<MultimemStFloatNoneV4DefaultOrdered*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<MultimemRedFloatAddV4DefaultRed*>(&instruction))
    return form->src;
  if (auto* form =
          dynamic_cast<MultimemStFloatNoneV8DefaultWeak*>(&instruction))
    return form->src;
  if (auto* form =
          dynamic_cast<MultimemStFloatNoneV8DefaultOrdered*>(&instruction))
    return form->src;
  if (auto* form = dynamic_cast<MultimemRedFloatAddV8DefaultRed*>(&instruction))
    return form->src;
  throw std::logic_error("Expected one of the migrated multimem forms.");
}

/** Full target facts for direct checks, independent of retained syntax lifetime. */
checker::Context valueContext() {
  const auto profile = base::find_target_profile("sm_100a");
  return {
      .target = {.ptx_version = {9, 3},
                 .sm_version = 100,
                 .enabled_family_features = profile->enabled_family_features,
                 .identity = profile->identity,
                 .capabilities = profile->capabilities}};
}

/** Check actual payload/ranges synchronously; callback inputs never escape. */
struct ValueObserver final : detail::IReferenceObserver {
  /** Expected owner, borrowed only during the enclosing synchronous visit. */
  const WithLocs<ResolvedValueVector>& expected;
  /** Copied success count; no received pointer or span is retained. */
  size_t calls = 0;
  /** Bind an owner which remains alive and immutable throughout this visit. */
  explicit ValueObserver(const WithLocs<ResolvedValueVector>& owner)
      : expected(owner) {}
  /** Inspect every actual register/immediate lane and the named whole base. */
  void value_vector(const ResolvedValueVector& value,
                    std::span<const SourceRange> ranges,
                    checker::AddressSymbolResolutionPolicy) override {
    ++calls;
    EXPECT_EQ(&value, &expected.value);
    ASSERT_EQ(ranges.size(), value.elements.size());
    ASSERT_EQ(ranges.size(), expected.locs.size());
    EXPECT_TRUE(
        valid_value_vector_source(value, ranges, value.elements.size() != 8));
    for (size_t lane = 0; lane < value.elements.size(); ++lane) {
      EXPECT_EQ(ranges[lane], expected.locs[lane]);
      EXPECT_NE(ranges[lane], SourceRange{});
      if (const auto* reg =
              std::get_if<ResolvedRegisterRef>(&value.elements[lane])) {
        EXPECT_TRUE(reg->symbol_id);
        EXPECT_TRUE(reg->declared_type);
        if (value.source.whole_base) {
          EXPECT_EQ(reg->symbol_id,
                    value.source.whole_base->value.register_ref.symbol_id);
          ASSERT_TRUE(reg->component);
          EXPECT_EQ(reg->component->lane, lane);
          EXPECT_EQ(reg->component->origin,
                    RegisterComponentOrigin::NamedProjection);
          EXPECT_EQ(reg->component->base_range, value.source.range);
        }
      } else {
        const auto& word = std::get<ResolvedImmediate>(value.elements[lane]);
        EXPECT_TRUE(valid_value_vector_immediate(word));
        EXPECT_FALSE(value.source.whole_base);
      }
    }
  }
};

/** Acquire fresh callback views from each actual owner after every copy or mutation. */
void expectValueOwner(ResolvedModule& module) {
  auto& instruction = *module.functions.front().body.front();
  const auto& field = valueField(instruction);
  ValueObserver visitor(field);
  instruction.visit_references(visitor);
  EXPECT_EQ(visitor.calls, 1u);
  EXPECT_TRUE(instruction.check(valueContext()));
  EXPECT_TRUE(validateModule(module));
}

/** Real register controls precede literal positives; packed16 source stays absent. */
TEST(MultimemValueVector, PreservesAllRegistersAndDefinedSourcePartitions) {
  for (const auto& item : value_cases) {
    SCOPED_TRACE(std::string(item.variant) + " " +
                 std::string(to_string(item.type)));
    const auto registers =
        valueBraces(item, "r" + std::to_string(item.arity - 1));
    auto control = valueOwned(valueSource(item, registers));
    ASSERT_TRUE(control) << control.error().front().message;
    expectValueOwner(*control);
    if (item.literal.empty()) {
      EXPECT_FALSE(
          valueOwned(valueSource(item, valueBraces(item, "1.5", true))));
      EXPECT_FALSE(
          valueOwned(valueSource(item, valueBraces(item, "0f00003c3c", true))));
      continue;
    }
    for (const bool all_immediate : {false, true}) {
      const auto source =
          valueSource(item, valueBraces(item, item.literal, all_immediate));
      const auto ast = test_helpers::parseModule(source);
      ASSERT_MODULE_PARSE_SUCCEEDS(ast);
      ASSERT_TRUE(resolveModuleOnly(*ast));
      auto module = valueOwned(source);
      ASSERT_TRUE(module) << module.error().front().message;
      EXPECT_TRUE(validateModule(*ast, *module));
      auto& field = valueField(*module->functions.front().body.front());
      ASSERT_EQ(field.value.elements.size(), item.arity);
      for (size_t lane = 0; lane < item.arity; ++lane) {
        if (!all_immediate && lane + 1 != item.arity)
          continue;
        const auto& word =
            std::get<ResolvedImmediate>(field.value.elements[lane]);
        EXPECT_EQ(word.type, item.type);
        EXPECT_EQ(word.bits, item.word);
        EXPECT_FALSE(word.integer_source_bits);
        EXPECT_FALSE(word.is_negative);
      }
      auto copy = *module;
      ResolvedModule assigned;
      assigned = copy;
      auto function = assigned.functions.front();
      assigned.functions.front() = function;
      assigned.functions.front().body.front() = function.body.front()->clone();
      expectValueOwner(copy);
      expectValueOwner(assigned);
    }
  }
}

/** All64 typed branches admit canonical current words, independent of source availability. */
TEST(MultimemValueVector,
     RechecksActualCurrentWordsWithoutInventingSourceHistory) {
  for (const auto& item : value_cases) {
    auto owned = valueOwned(valueSource(
        item, valueBraces(item, "r" + std::to_string(item.arity - 1))));
    ASSERT_TRUE(owned) << owned.error().front().message;
    const auto ast = test_helpers::parseModule(valueSource(
        item, valueBraces(item, "r" + std::to_string(item.arity - 1))));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto& instruction = *owned->functions.front().body.front();
    auto& field = valueField(instruction);
    for (auto& lane : field.value.elements)
      lane = ResolvedImmediate{.bits = item.word, .type = item.type};
    expectValueOwner(*owned);
    EXPECT_TRUE(validateModule(*ast, *owned));
    auto& word = std::get<ResolvedImmediate>(field.value.elements.back());
    const auto original = word;
    word.bits = 2;
    expectValueOwner(*owned);
    word = original;
    for (int mutation = 0; mutation < 4; ++mutation) {
      if (mutation == 0)
        word.bits = uint64_t{1} << (base::scalar_size_of(item.type) * 8);
      if (mutation == 1)
        word.integer_source_bits = 0;
      if (mutation == 2)
        word.is_negative = true;
      if (mutation == 3)
        word.type = ScalarType::U32;
      EXPECT_FALSE(instruction.check(valueContext()));
      EXPECT_FALSE(validateModule(*owned));
      EXPECT_FALSE(validateModule(*ast, *owned));
      word = original;
      expectValueOwner(*owned);
    }
    auto copy = *owned;
    auto function = copy.functions.front();
    copy.functions.front() = function;
    copy.functions.front().body.front() = function.body.front()->clone();
    expectValueOwner(copy);
  }
}

/** Named2/4 roots retain real declarations; V8 stays brace-only and integers stay distinct. */
TEST(MultimemValueVector, KeepsNamedRootsAndSourcePermissionBoundaries) {
  for (const auto& item : value_cases) {
    const auto declaration = ".reg .v" +
                             std::to_string(item.arity == 8 ? 4 : item.arity) +
                             " ." + std::string(item.carrier) + " V<2>; ";
    auto named = valueOwned(valueSource(item, "V1", declaration));
    if (item.arity == 8) {
      EXPECT_FALSE(named);
    } else {
      ASSERT_TRUE(named) << named.error().front().message;
      expectValueOwner(*named);
      auto& field = valueField(*named->functions.front().body.front());
      ASSERT_TRUE(field.value.source.whole_base);
      const auto saved = field.value.source;
      field.value.source.whole_base->value.register_ref.symbol_id.reset();
      EXPECT_FALSE(validateModule(*named));
      field.value.source = saved;
      expectValueOwner(*named);
    }
    for (const auto integer : {"0", "1.5==1.5"})
      EXPECT_FALSE(
          valueOwned(valueSource(item, valueBraces(item, integer, true))));
    EXPECT_FALSE(valueOwned(valueSource(item, valueBraces(item, "_"))));
  }
}

/** Arity, owned ranges and saved source identity remain independent of lane permission. */
TEST(MultimemValueVector, PreservesArityRangesAndIndependentSourceIdentity) {
  for (const size_t arity : {2u, 4u, 8u}) {
    const auto found = std::find_if(
        value_cases.begin(), value_cases.end(), [arity](const auto& item) {
          return item.arity == arity && item.type == ScalarType::F16;
        });
    ASSERT_NE(found, value_cases.end());
    const auto source = valueSource(*found, valueBraces(*found, "1.5", true));
    auto module = valueOwned(source);
    ASSERT_TRUE(module) << module.error().front().message;
    auto& field = valueField(*module->functions.front().body.front());
    const auto saved = field;
    field.value.elements.pop_back();
    EXPECT_FALSE(validateModule(*module));
    field = saved;
    field.value.elements.push_back(field.value.elements.back());
    EXPECT_FALSE(validateModule(*module));
    field = saved;
    field.locs.clear();
    EXPECT_FALSE(validateModule(*module));
    field = saved;
    field.value.source.range = {};
    EXPECT_FALSE(validateModule(*module));
    field = saved;
    expectValueOwner(*module);
    const auto changed = test_helpers::parseModule(
        valueSource(*found, valueBraces(*found, "2.5", true)));
    ASSERT_MODULE_PARSE_SUCCEEDS(changed);
    ASSERT_TRUE(resolveAndValidateModule(*changed));
    const auto mismatch = validateModule(*changed, *module);
    ASSERT_FALSE(mismatch);
    EXPECT_TRUE(
        std::any_of(mismatch.error().begin(), mismatch.error().end(),
                    [](const auto& diagnostic) {
                      return diagnostic.kind ==
                             checker::CheckDiagnosticKind::ModuleSourceMismatch;
                    }));
  }
  for (const auto dtype : {"f16x2", "bf16x2"}) {
    for (const auto prefix :
         {"multimem.st.", "multimem.st.relaxed.sys.", "multimem.red.add."}) {
      const std::string source = R"ptx(.version 9.3
.target sm_100a
.address_size 64
.global .align 16 .b8 mm[256]; .entry k(){)ptx" +
                                 std::string(prefix) + dtype +
                                 " [mm],1.5;}";
      EXPECT_FALSE(valueOwned(source));
    }
  }
}
/** Build an ordinary checker projection from this live instruction's actual lanes. */
checker::OperandView valueView(const WithLocs<ResolvedValueVector>& owner) {
  checker::OperandView view{.field_id = "src",
                            .actual_shape = checker::OperandShape::Vector,
                            .value_vector = &owner.value,
                            .vector_arity = owner.value.elements.size(),
                            .locations = owner.locs};
  for (size_t i = 0; i < owner.value.elements.size(); ++i) {
    if (const auto* reg =
            std::get_if<ResolvedRegisterRef>(&owner.value.elements[i])) {
      view.vector_element_shapes[i] = checker::OperandShape::Register;
      view.vector_element_types[i] =
          reg->declared_type.value_or(ScalarType::Invalid);
      view.vector_element_registers[i] = reg;
    } else {
      const auto& word = std::get<ResolvedImmediate>(owner.value.elements[i]);
      view.vector_element_shapes[i] = checker::OperandShape::Immediate;
      view.vector_element_types[i] = word.type;
      view.vector_element_immediates[i] = &word;
      view.vector_immediate_bits[i] = word.bits;
      view.vector_immediate_source_bits[i] = word.integer_source_bits;
      view.vector_immediate_negative[i] = word.is_negative;
    }
  }
  return view;
}

/** Every projection must describe its current owner, with real immediates distinct from sinks. */
TEST(MultimemValueVector, RejectsBorrowedProjectionForgeriesAtEveryArity) {
  for (const size_t arity : {2u, 4u, 8u}) {
    const auto item = std::find_if(
        value_cases.begin(), value_cases.end(), [arity](const auto& x) {
          return x.arity == arity && x.type == ScalarType::F16;
        });
    ASSERT_NE(item, value_cases.end());
    auto module = valueOwned(valueSource(*item, valueBraces(*item, "1.5")));
    ASSERT_TRUE(module);
    const auto& field = valueField(*module->functions.front().body.front());
    auto view = valueView(field);
    const uint8_t arities[]{static_cast<uint8_t>(arity)};
    checker::OperandDescriptor descriptor{
        .target_field_id = "src",
        .type_expression = {.kind =
                                checker::OperandTypeExpressionKind::FixedScalar,
                            .fixed_scalar_type = ScalarType::F16},
        .role = checker::OperandRole::Source,
        .access = checker::OperandAccess::Read,
        .allowed_shapes = checker::OperandShape::Vector,
        .allowed_vector_arities = arities,
        .vector_type_policy = checker::VectorTypePolicy::Element,
        .source_value_vector = true};
    const auto check = [&] {
      return checker::check_operands(std::span{&descriptor, 1}, {},
                                     std::span{&view, 1}, {}, {});
    };
    ASSERT_TRUE(check());
    const auto good = view;
    auto copied = field.value;
    const auto last = arity - 1;
    for (int mutation = 0; mutation < 11; ++mutation) {
      SCOPED_TRACE(mutation);
      view = good;
      if (mutation == 0)
        view.value_vector = nullptr;
      if (mutation == 1)
        view.value_vector = &copied;
      if (mutation == 2)
        view.vector_element_registers[0] = nullptr;
      if (mutation == 3)
        view.vector_element_immediates[last] = nullptr;
      if (mutation == 4)
        view.vector_element_immediates[last] =
            &std::get<ResolvedImmediate>(copied.elements[last]);
      if (mutation == 5)
        view.vector_element_shapes[last] = checker::OperandShape::Register;
      if (mutation == 6)
        view.vector_element_types[last] = ScalarType::F32;
      if (mutation == 7)
        view.vector_immediate_bits[last] = 0;
      if (mutation == 8)
        view.vector_immediate_source_bits[last] = 0;
      if (mutation == 9)
        view.vector_immediate_negative[last] = true;
      if (mutation == 10)
        view.vector_sink_count = 1;
      EXPECT_FALSE(check());
    }
    view = good;
    EXPECT_TRUE(check());
  }
}

/** Coherent ordinary words preserve signaling payloads and finite encodings without conversion. */
TEST(MultimemValueVector,
     PreservesCurrentSpecialWordsAndIntegerSourceBoundaries) {
  for (const auto& item : value_cases) {
    SCOPED_TRACE(std::string(item.variant) + " " +
                 std::string(to_string(item.type)));
    auto module = valueOwned(valueSource(
        item, valueBraces(item, "r" + std::to_string(item.arity - 1))));
    ASSERT_TRUE(module);
    auto& field = valueField(*module->functions.front().body.front());
    const uint64_t bits = item.type == ScalarType::BF16   ? 0x7f81
                          : item.type == ScalarType::E5m2 ? 0x7d
                          : item.type == ScalarType::E4m3 ? 0x78
                                                          : item.word;
    for (auto& lane : field.value.elements)
      lane = ResolvedImmediate{.bits = bits, .type = item.type};
    expectValueOwner(*module);
    for (const auto& integer : {std::string("0"), std::to_string(item.word),
                                std::string("1+2"), std::string("1.5==1.5")})
      EXPECT_FALSE(
          valueOwned(valueSource(item, valueBraces(item, integer, true))));
  }
  for (const auto type :
       {ScalarType::F16, ScalarType::BF16, ScalarType::F32, ScalarType::E4m3,
        ScalarType::E5m2, ScalarType::F16x2, ScalarType::BF16x2,
        ScalarType::E4m3x4, ScalarType::E5m2x4}) {
    const auto item =
        std::find_if(value_cases.begin(), value_cases.end(),
                     [type](const auto& x) { return x.type == type; });
    ASSERT_NE(item, value_cases.end());
    const bool packed =
        type == ScalarType::F16x2 || type == ScalarType::BF16x2 ||
        type == ScalarType::E4m3x4 || type == ScalarType::E5m2x4;
    for (const auto literal : {"1.5", "0d3ff8000000000000", "1.0+0.5"})
      EXPECT_EQ(bool(valueOwned(
                    valueSource(*item, valueBraces(*item, literal, true)))),
                !packed);
    if (packed)
      for (const auto literal : {"+0f3e003e00", "-0f3e003e00", "(0f3e003e00)",
                                 "0f3e003e00+0f00000000"})
        EXPECT_FALSE(
            valueOwned(valueSource(*item, valueBraces(*item, literal, true))));
  }
}

/** Named ownership joins actual table identities, whole bases and each projected member. */
TEST(MultimemValueVector, RejoinsNamedRootsAndPhysicalDeclarations) {
  for (const auto& item : value_cases) {
    if (item.arity == 8)
      continue;
    const auto decl = ".reg .v" + std::to_string(item.arity) + " ." +
                      std::string(item.carrier) + " V<2>; ";
    const auto source = valueSource(item, "V1", decl);
    auto original = valueOwned(source);
    ASSERT_TRUE(original);
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto replacement = valueOwned(valueSource(item, "V0", decl));
    ASSERT_TRUE(replacement);
    auto coherent = *original;
    valueField(*coherent.functions.front().body.front()) =
        valueField(*replacement->functions.front().body.front());
    expectValueOwner(coherent);
    EXPECT_TRUE(validateModule(*ast, coherent));
    for (int mutation = 0; mutation < 8; ++mutation) {
      auto bad = *original;
      auto& value = valueField(*bad.functions.front().body.front()).value;
      auto& lane = std::get<ResolvedRegisterRef>(value.elements.front());
      if (mutation == 0)
        value.source.whole_base->value.register_ref.symbol_id.reset();
      if (mutation == 1)
        lane.declared_type = ScalarType::U64;
      if (mutation == 2)
        lane.register_class = ResolvedRegisterClass::Predicate;
      if (mutation == 3)
        value.source.whole_base->value.register_ref.vector_width = 3;
      if (mutation == 4)
        lane.component->origin = RegisterComponentOrigin::ExplicitSelector;
      if (mutation == 5)
        value.source.whole_base.reset();
      if (mutation == 6)
        lane.component->base_range = {};
      if (mutation == 7)
        lane.parameterized_index = 0;
      EXPECT_FALSE(validateModule(bad));
      EXPECT_FALSE(validateModule(*ast, bad));
    }
  }
  const auto item = std::find_if(
      value_cases.begin(), value_cases.end(),
      [](const auto& x) { return x.type == ScalarType::F32 && x.arity == 2; });
  ASSERT_NE(item, value_cases.end());
  auto module = valueOwned(valueSource(*item, "{r0,r1}",
                                       ".reg .b16 small; .reg .b64 big; .reg "
                                       ".pred flag; .reg .b32 compatible;"));
  ASSERT_TRUE(module);
  const auto scope =
      module->symbols.functionScope(module->functions.front().range);
  ASSERT_TRUE(scope);
  auto& lane = std::get<ResolvedRegisterRef>(
      valueField(*module->functions.front().body.front())
          .value.elements.front());
  const auto saved = lane;
  for (const auto& [name, type, category] :
       {std::tuple{"small", ScalarType::B16, ResolvedRegisterClass::General},
        std::tuple{"big", ScalarType::B64, ResolvedRegisterClass::General},
        std::tuple{"flag", ScalarType::B32, ResolvedRegisterClass::General},
        std::tuple{"compatible", ScalarType::B32,
                   ResolvedRegisterClass::General}}) {
    const auto actual = module->symbols.lookup(*scope, name);
    ASSERT_TRUE(actual);
    lane = saved;
    lane.spelling = name;
    lane.symbol_id = actual->symbol;
    lane.parameterized_index = actual->parameterized_index;
    lane.declared_type = type;
    lane.register_class = category;
    EXPECT_EQ(bool(validateModule(*module)),
              std::string_view(name) == "compatible");
  }
  lane = saved;
  expectValueOwner(*module);
  for (const auto type :
       {ScalarType::F16, ScalarType::F16x2, ScalarType::F32}) {
    const auto selected =
        std::find_if(value_cases.begin(), value_cases.end(),
                     [type](const auto& x) { return x.type == type; });
    ASSERT_NE(selected, value_cases.end());
    auto native = valueOwned(
        valueSource(*selected, valueBraces(*selected, "typed"),
                    ".reg ." +
                        std::string(type == ScalarType::F16     ? "f16"
                                    : type == ScalarType::F16x2 ? "f16x2"
                                                                : "f32") +
                        " typed; "));
    ASSERT_TRUE(native) << native.error().front().message;
    expectValueOwner(*native);
  }
}

/** Owned extents and address/profile constraints remain independent of the new lane kinds. */
TEST(MultimemValueVector, KeepsRangesProfilesAndContextIndependent) {
  for (const size_t arity : {2u, 4u, 8u}) {
    const auto item = std::find_if(
        value_cases.begin(), value_cases.end(), [arity](const auto& x) {
          return x.type == ScalarType::F16 && x.arity == arity;
        });
    ASSERT_NE(item, value_cases.end());
    const auto source = valueSource(*item, valueBraces(*item, "1.5", true));
    const auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto module = valueOwned(source);
    ASSERT_TRUE(module);
    for (int mutation = 0; mutation < 3; ++mutation) {
      auto bad = *module;
      auto& field = valueField(*bad.functions.front().body.front());
      if (mutation == 0)
        std::swap(field.locs.front().start, field.locs.front().end);
      if (mutation == 1)
        field.locs.back() = {{500, 1}, {500, 2}};
      if (mutation == 2)
        field.value.source.range = {};
      EXPECT_FALSE(validateModule(bad));
      EXPECT_FALSE(validateModule(*ast, bad));
    }
    for (const auto offset : {1, 2}) {
      auto changed = source;
      changed.replace(changed.find("[mm]"), 4,
                      "[mm+" + std::to_string(offset) + "]");
      EXPECT_FALSE(valueOwned(changed));
    }
    auto headerless = source;
    headerless.erase(headerless.find(".target sm_100a\n"), 16);
    const auto partial = test_helpers::parseModule(headerless);
    ASSERT_MODULE_PARSE_SUCCEEDS(partial);
    auto deferred = resolveModuleOnly(*partial);
    ASSERT_TRUE(deferred);
    EXPECT_TRUE(
        validateModule(*deferred, ModuleValidationPolicy::AvailableContext));
    const auto strict = validateModule(*deferred);
    ASSERT_FALSE(strict);
    EXPECT_TRUE(std::any_of(
        strict.error().begin(), strict.error().end(), [](const auto& d) {
          return d.kind ==
                 checker::CheckDiagnosticKind::MissingValidationContext;
        }));
    auto unknown = *module;
    for (auto& region : unknown.header.regions)
      for (auto& option : region.target_options)
        if (option.starts_with("sm_"))
          option = "sm_999";
    for (auto& function : unknown.functions)
      function.source_target = "sm_999";
    const auto rejected = validateModule(unknown);
    ASSERT_FALSE(rejected);
    EXPECT_TRUE(std::any_of(
        rejected.error().begin(), rejected.error().end(), [](const auto& d) {
          return d.kind == checker::CheckDiagnosticKind::UnknownTarget;
        }));
    auto under = valueBraces(*item, "1.5", true);
    under.erase(under.rfind(','), 4);
    EXPECT_FALSE(valueOwned(valueSource(*item, under)));
    auto over = valueBraces(*item, "1.5", true);
    over.insert(over.size() - 1, ",1.5");
    EXPECT_FALSE(valueOwned(valueSource(*item, over)));
    auto shared = source;
    shared.replace(shared.find(".global .align"), 7, ".shared");
    EXPECT_FALSE(valueOwned(shared));
    auto low = valueContext();
    low.target.ptx_version = {8, 0};
    low.target.sm_version = 90;
    EXPECT_FALSE(module->functions.front().body.front()->check(low));
    auto good = valueContext();
    good.target.ptx_version = {8, 1};
    good.target.sm_version = 90;
    EXPECT_TRUE(module->functions.front().body.front()->check(good));
    good.target.sm_version = 80;
    EXPECT_FALSE(module->functions.front().body.front()->check(good));
  }
  const auto fp8 = std::find_if(
      value_cases.begin(), value_cases.end(),
      [](const auto& x) { return x.type == ScalarType::E4m3 && x.arity == 4; });
  ASSERT_NE(fp8, value_cases.end());
  auto module = valueOwned(valueSource(*fp8, valueBraces(*fp8, "1.5", true)));
  ASSERT_TRUE(module);
  auto ctx = valueContext();
  ctx.target.ptx_version = {8, 6};
  EXPECT_TRUE(module->functions.front().body.front()->check(ctx));
  ctx.target.ptx_version = {8, 5};
  EXPECT_FALSE(module->functions.front().body.front()->check(ctx));
  for (const auto target : {"sm_100", "sm_100f"}) {
    const auto profile = base::find_target_profile(target);
    ASSERT_TRUE(profile);
    ctx = valueContext();
    ctx.target.ptx_version = {8, 8};
    ctx.target.identity = profile->identity;
    ctx.target.enabled_family_features = profile->enabled_family_features;
    ctx.target.capabilities = profile->capabilities;
    EXPECT_EQ(bool(module->functions.front().body.front()->check(ctx)),
              std::string_view(target) == "sm_100f");
  }
}
}  // namespace
}  // namespace ptx_frontend::resolved_ir
