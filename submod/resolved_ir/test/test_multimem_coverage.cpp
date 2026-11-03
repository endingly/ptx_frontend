#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

#include <fmt/format.h>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <ptx_frontend/resolved_ir/model/data_movement/multimem.gen.hpp>
#include <ptx_frontend/resolved_ir/ptx_resolved_ir_resolution.hpp>
#include <ptx_frontend/syntax/ptx_syntax_parser.hpp>

#include "test_syntax_parse_helpers.hpp"

namespace ptx_frontend::resolved_ir {
namespace {

/** Wrap a candidate in a complete module with typed memory declarations. */
std::string multimem_source(std::string_view body,
                            std::string_view target = "sm_100a",
                            std::string_view version = "9.3") {
  return ".version " + std::string(version) + "\n.target " +
         std::string(target) + R"ptx(
.address_size 64
.global .align 16 .b8 mm[256];
.shared .align 16 .b8 sh[256];
.visible .entry kernel() {
  .local .align 16 .b8 local_value[32];
  .reg .b32 %b<8>;
  .reg .b8 %byte<8>;
  .reg .u32 %u<8>;
  .reg .u64 %u64;
  .reg .u16 %mask;
  .reg .f32 %f<8>;
  .reg .f64 %d;
  .reg .b64 %addr;
)ptx" + std::string(body) +
         "\n  ret;\n}\n";
}

/** Parse and check the owned module with no assembler or GPU execution. */
bool accepts_multimem(std::string_view source) {
  const auto parsed = test_helpers::parseModule(source);
  if (!parsed.has_value() || !parsed.diagnostics.empty())
    return false;
  return resolveAndValidateModule(*parsed).has_value();
}

/** Seven source families retain distinct exact classes and completion identities. */
TEST(Multimem, SevenFamiliesAndOwnedCompletion) {
  const auto parsed = test_helpers::parseModule(multimem_source(R"ptx(
  multimem.ld_reduce.and.b32 %b0, [mm];
  multimem.st.weak.b32 [mm], %b0;
  multimem.red.relaxed.gpu.add.u32 [mm], %u0;
  multimem.st.async.release.gpu.global.u32 [%addr], %u0;
  multimem.red.async.release.sys.add.u64 [%addr], %u64;
  multimem.cp.async.bulk.global.shared::cta.bulk_group [mm], [sh], 16;
  multimem.cp.reduce.async.bulk.global.shared::cta.bulk_group.add.u32 [mm], [sh], 16;
)ptx"));
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  const auto resolved = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().front().message;
  const auto& body = resolved->functions.front().body;
  ASSERT_EQ(body.size(), 8u);
  EXPECT_NE(
      dynamic_cast<MultimemLdReduceIntegerAndScalarDefaultWeak*>(body[0].get()),
      nullptr);
  EXPECT_NE(
      dynamic_cast<MultimemStIntegerNoneScalarDefaultWeak*>(body[1].get()),
      nullptr);
  EXPECT_NE(dynamic_cast<MultimemRedIntegerAddScalarDefaultRed*>(body[2].get()),
            nullptr);
  EXPECT_NE(dynamic_cast<MultimemStAsyncRelease*>(body[3].get()), nullptr);
  EXPECT_NE(dynamic_cast<MultimemRedAsyncRelease*>(body[4].get()), nullptr);
  EXPECT_NE(dynamic_cast<MultimemCpAsyncBulkWeak*>(body[5].get()), nullptr);
  EXPECT_NE(dynamic_cast<MultimemCpReduceAsyncBulkAddDefault*>(body[6].get()),
            nullptr);
  EXPECT_EQ(MultimemStAsyncRelease::completion_kind,
            base::AsyncCompletionKind::None);
  EXPECT_EQ(MultimemRedAsyncRelease::completion_kind,
            base::AsyncCompletionKind::None);
  EXPECT_EQ(MultimemCpAsyncBulkWeak::completion_kind,
            base::AsyncCompletionKind::BulkGroup);
}

/** Normative Syntax and operation tables outrank the conflicting examples. */
TEST(Multimem, SourceExclusionsAndTables) {
  for (std::string_view candidate : {
           "multimem.red.relaxed.gpu.max.f64 [mm], %d;",
           "multimem.ld_reduce.max.f64 %d, [mm];",
           "multimem.ld_reduce.min.f32 %f0, [mm];",
           "multimem.ld_reduce.add.acc::f16.f32 %f0, [mm];",
           "multimem.st.v8.f64 [mm], {%f0,%f1,%f2,%f3,%f4,%f5,%f6,%f7};",
           "multimem.ld_reduce.relaxed.add.u32 %u0, [mm];",
           "multimem.red.async.release.gpu.add.s64 [%addr], %u64;",
           "multimem.st.async.relaxed.gpu.u32 [%addr], %u0;",
           "multimem.st.async.release.cta.u32 [%addr], %u0;",
           "multimem.st.async.release.gpu.u32 [mm], %u0;",
           "multimem.red.async.release.gpu.add.u32 [mm], %u0;",
           "multimem.cp.async.bulk.global.bulk_group [mm], [sh], 16;",
           "multimem.cp.reduce.async.bulk.global.bulk_group.add.u32 [mm], "
           "[sh], 16;",
           "multimem.cp.reduce.async.bulk.global.shared::cta.bulk_group.add."
           "u32 [mm], [sh], 16, [mm];",
           "multimem.cp.async.bulk.global.shared::cta.bulk_group.cp_mask [mm], "
           "[sh], 16;",
           "multimem.cp.async.bulk.relaxed.cta.global.shared::cta.bulk_group "
           "[mm], [sh], 16;",
           "multimem.cp.reduce.async.bulk.global.shared::cta.bulk_group.add."
           "f16 [mm], [sh], 16;",
           "multimem.cp.reduce.async.bulk.global.shared::cta.bulk_group.inc."
           "s32 [mm], [sh], 16;",
       }) {
    SCOPED_TRACE(candidate);
    EXPECT_FALSE(accepts_multimem(multimem_source(candidate)));
  }
  for (std::string_view candidate : {
           "multimem.st.bf16x2 [mm], 0f3f800000;",
           "multimem.ld_reduce.add.f32 %f0, [mm];",
           "multimem.red.add.f32 [mm], %f0;",
           "multimem.red.add.u32 [mm], %u0;",
           "multimem.red.release.sys.add.u32 [mm], %u0;",
           "multimem.red.gpu.add.u32 [mm], %u0;",
           "multimem.red.release.add.u32 [mm], %u0;",
           "multimem.ld_reduce.add.acc::f32.v2.f16x2 {%b0,%b1}, [mm];",
           "multimem.st.v4.f32 [mm], {%f0,%f1,%f2,%f3};",
           "multimem.st.v4.f32 [mm+16], {%f0,%f1,%f2,%f3};",
           "multimem.cp.async.bulk.global.shared::cta.bulk_group.cp_mask [mm], "
           "[sh], 16, %mask;",
           "multimem.cp.async.bulk.relaxed.cta.global.shared::cta.bulk_group."
           "b128 [mm], [sh], 16;",
           "multimem.cp.reduce.async.bulk.global.shared::cta.bulk_group.add."
           "noftz.f16 [mm], [sh], 16;",
       }) {
    SCOPED_TRACE(candidate);
    EXPECT_TRUE(accepts_multimem(multimem_source(candidate)));
  }
}

/** Known address spaces, widths, alignments, and immediate size remain checked. */
TEST(Multimem, StaticAddressAndSizeConstraints) {
  for (std::string_view candidate : {
           "multimem.st.u32 [local_value], %u0;",
           "multimem.cp.async.bulk.global.shared::cta.bulk_group [sh], [sh], "
           "16;",
           "multimem.cp.async.bulk.global.shared::cta.bulk_group [mm], [mm], "
           "16;",
           "multimem.cp.async.bulk.global.shared::cta.bulk_group [mm+4], [sh], "
           "16;",
           "multimem.cp.async.bulk.global.shared::cta.bulk_group [mm], [sh], "
           "15;",
           "multimem.st.v4.f32 [mm+4], {%f0,%f1,%f2,%f3};",
           "multimem.st.u32 [mm+2147483648], %u0;",
           "multimem.st.async.release.gpu.u64 [%addr], %u0;",
           "multimem.st.f32 [mm], 1;",
           "multimem.st.f16x2 [mm], 1;",
           "multimem.red.add.f16x2 [mm], 1;",
           "multimem.st.bf16x2 [mm], 1.5;",
           "multimem.st.e4m3x4 [mm], 1.5;",
           "multimem.st.e5m2x4 [mm], 0d3ff0000000000000;",
           "multimem.ld_reduce.add.u32 1, [mm];",
           "multimem.st.v2.f32 [mm], {1,2};",
       }) {
    SCOPED_TRACE(candidate);
    EXPECT_FALSE(accepts_multimem(multimem_source(candidate)));
  }
  EXPECT_TRUE(
      accepts_multimem(multimem_source("multimem.cp.async.bulk.global.shared::"
                                       "cta.bulk_group [mm+16], [sh], 32;")));
  EXPECT_TRUE(
      accepts_multimem(multimem_source("multimem.st.v2.f32 [mm], {1.5,1.5};")));
}

/** Scalar source literals retain typed use bits after syntax AST destruction. */
TEST(Multimem, ScalarLiteralSourcesAfterAstDeath) {
  std::optional<ResolvedModule> owned;
  {
    auto ast = test_helpers::parseModule(multimem_source(R"ptx(
  multimem.st.u32 [mm], 1;
  multimem.red.add.u32 [mm], 1;
  multimem.st.async.release.gpu.b8 [%addr], 0x1ff;
  multimem.red.async.release.gpu.add.u32 [%addr], 1;
  multimem.st.f32 [mm], 1.5;
  multimem.red.add.f64 [mm], 1.5;
  multimem.st.async.release.gpu.f64 [%addr], 1.5;
)ptx"));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result.has_value()) << result.error().front().message;
    owned.emplace(std::move(*result));
  }
  ASSERT_TRUE(owned);
  EXPECT_TRUE(validateModule(*owned));
  const auto& body = owned->functions.front().body;
  ASSERT_EQ(body.size(), 8u);
  auto* narrow = dynamic_cast<MultimemStAsyncRelease*>(body[2].get());
  ASSERT_NE(narrow, nullptr);
  auto* immediate = std::get_if<ResolvedImmediate>(&narrow->src.value);
  ASSERT_NE(immediate, nullptr);
  EXPECT_EQ(immediate->type, ScalarType::B8);
  EXPECT_EQ(immediate->bits, 0xffu);
  ASSERT_TRUE(immediate->integer_source_bits);
  EXPECT_EQ(*immediate->integer_source_bits, 0x1ffu);
  immediate->bits = 0xfe;
  EXPECT_FALSE(validateModule(*owned));
}

/** Borrow the two concrete packed-store fields only while their instruction lives. */
struct PackedOriginSlot {
  /** Mutable current source; its instruction owns the alternative and ranges. */
  WithLocs<RegOrImm>& source;
  /** Current selected logical type, independent of the immediate's type. */
  WithLocs<ScalarType>& type;
  /** Current selected default layout and its retained source provenance. */
  ResolvedOperandLayoutTag& layout;
};

/** Locate the same existing source field on the weak or ordered packed form. */
PackedOriginSlot packed_origin_slot(Instruction& instruction) {
  if (auto* weak = dynamic_cast<MultimemStFloatFp8PackedNoneScalarDefaultWeak*>(
          &instruction))
    return {weak->src, weak->type, weak->operand_layout};
  if (auto* ordered =
          dynamic_cast<MultimemStFloatFp8PackedNoneScalarDefaultOrdered*>(
              &instruction))
    return {ordered->src, ordered->type, ordered->operand_layout};
  throw std::runtime_error("Expected the selected scalar packed store.");
}

/** Form an existing weak/default or relaxed/sys source, with no hidden producer. */
std::string packed_origin_operation(bool ordered, ScalarType type,
                                    std::string_view value) {
  return std::string("multimem.st.") + (ordered ? "relaxed.sys." : "") +
         (type == ScalarType::E4m3x4 ? "e4m3x4" : "e5m2x4") + " [mm], " +
         std::string(value) + ";";
}

/** Return an independent owner after actual source, parser and AST destruction. */
ResolvedModule packed_origin_owner(std::string_view operation) {
  const auto text = multimem_source(operation);
  const auto ast = test_helpers::parseModule(text);
  if (!ast || !ast.diagnostics.empty())
    throw std::runtime_error("Packed source fixture did not parse.");
  auto module = resolveAndValidateModule(*ast);
  if (!module)
    throw std::runtime_error(module.error().front().message);
  return std::move(*module);
}

/** Supply complete current feature context and the actual instruction extent. */
checker::Context packed_origin_context(const ResolvedModule& module) {
  return {
      .target = {.ptx_version = {9, 3},
                 .sm_version = 100,
                 .identity = *base::parse_target_identity("sm_100a")},
      .instruction_range = module.functions.front().instruction_ranges.front()};
}

/** Require current canonical-word failures at the actual immediate operand. */
void expect_packed_origin_result(const checker::CheckResult& checked,
                                 bool valid, SourceRange range) {
  ASSERT_EQ(bool(checked), valid)
      << (checked ? "unexpected success" : checked.error().front().message);
  if (!valid) {
    ASSERT_FALSE(checked.error().empty());
    EXPECT_EQ(checked.error().front().kind,
              checker::CheckDiagnosticKind::ImmediateValueMismatch);
    EXPECT_EQ(checked.error().front().range, range);
    EXPECT_NE(checked.error().front().message.find("Packed floating immediate"),
              std::string::npos);
  }
}

/** Copy observer values synchronously; no borrowed source or payload escapes. */
struct PackedOriginObservation final : detail::IReferenceObserver {
  /** Independent snapshot of the current scalar data projection. */
  std::optional<ResolvedImmediate> immediate;
  /** Owned source ranges corresponding to the copied projection. */
  std::vector<SourceRange> locations;
  /** Observe only the immediate alternative of the actual typed source. */
  void reg_or_imm(const RegOrImm& operand, std::span<const SourceRange> ranges,
                  checker::AddressSymbolResolutionPolicy) override {
    if (const auto* value = std::get_if<ResolvedImmediate>(&operand)) {
      immediate = *value;
      locations.assign(ranges.begin(), ranges.end());
    }
  }
};

/** Raw words have no integer origin and permit coherent current data edits. */
TEST(Multimem, PackedFp8LiteralBitsAfterAstDeath) {
  constexpr std::array<std::string_view, 8> mutations{
      "sourcebits-zero",    "sourcebits-equal",
      "sourcebits-wide",    "signed-coherent",
      "negative-no-origin", "highbits",
      "word-edit",          "restore"};
  for (const bool ordered : {false, true})
    for (const auto type : {ScalarType::E4m3x4, ScalarType::E5m2x4}) {
      for (size_t index = 0; index < mutations.size(); ++index) {
        SCOPED_TRACE(std::string(ordered ? "ordered-" : "weak-") +
                     std::string(to_string(type)) + "-" +
                     std::string(mutations[index]));
        const auto literal = index == 0   ? "0f00000000"
                             : index == 3 ? "0fffffffff"
                                          : "0f00000001";
        const auto operation = packed_origin_operation(ordered, type, literal);
        auto module = packed_origin_owner(operation);
        ASSERT_EQ(module.functions.front().body.size(), 2u);
        auto& instruction = *module.functions.front().body.front();
        EXPECT_EQ(instruction.instruction_kind(),
                  ordered
                      ? MultimemStFloatFp8PackedNoneScalarDefaultOrdered::kind
                      : MultimemStFloatFp8PackedNoneScalarDefaultWeak::kind);
        auto slot = packed_origin_slot(instruction);
        ASSERT_EQ(slot.type.value, type);
        ASSERT_EQ(slot.layout.value, 0u);
        auto* value = std::get_if<ResolvedImmediate>(&slot.source.value);
        ASSERT_NE(value, nullptr);
        ASSERT_EQ(value->type, type);
        ASSERT_FALSE(value->integer_source_bits);
        ASSERT_FALSE(value->is_negative);
        const auto range = slot.source.locs.front();
        const auto context = packed_origin_context(module);
        ASSERT_TRUE(instruction.check(context));
        ASSERT_TRUE(validateModule(module));
        if (index == 0)
          value->integer_source_bits = 0;
        if (index == 1)
          value->integer_source_bits = 1;
        if (index == 2)
          value->integer_source_bits = 0x100000001ULL;
        if (index == 3) {
          value->integer_source_bits = UINT64_MAX;
          value->is_negative = true;
        }
        if (index == 4)
          value->is_negative = true;
        if (index == 5)
          value->bits = 0x100000000ULL;
        if (index == 6)
          value->bits = 2;
        if (index == 7) {
          value->integer_source_bits = 1;
          EXPECT_FALSE(instruction.check(context));
          value->integer_source_bits.reset();
        }
        const bool valid = index >= 6;
        expect_packed_origin_result(instruction.check(context), valid, range);
        expect_packed_origin_result(validateModule(module), valid, range);
        {
          const auto text = multimem_source(operation);
          const auto ast = test_helpers::parseModule(text);
          ASSERT_MODULE_PARSE_SUCCEEDS(ast);
          expect_packed_origin_result(validateModule(*ast, module), valid,
                                      range);
        }
        auto copied = module;
        ResolvedModule assigned;
        assigned = copied;
        auto moved = std::move(copied);
        ResolvedModule move_assigned;
        move_assigned = std::move(assigned);
        expect_packed_origin_result(validateModule(moved), valid, range);
        expect_packed_origin_result(validateModule(move_assigned), valid,
                                    range);
        const auto cloned = instruction.clone();
        expect_packed_origin_result(cloned->check(context), valid, range);
        PackedOriginObservation observation;
        cloned->visit_references(observation);
        ASSERT_TRUE(observation.immediate);
        EXPECT_EQ(*observation.immediate, *value);
        EXPECT_EQ(observation.locations, slot.source.locs);
        value->integer_source_bits.reset();
        value->is_negative = false;
        value->bits = 1;
        EXPECT_TRUE(instruction.check(context));
        EXPECT_TRUE(validateModule(module));
      }
    }
}

TEST(Multimem, PackedFloatRejectsIntegerSourcesWithoutLosingBitContexts) {
  constexpr std::array<std::string_view, 14> integers{
      "0",           "1",    "0b1",       "01",          "0x3c3c3c3c",
      "0x3c3c3c3cU", "-1",   "-1U",       "0x100000001", "(1+2)",
      "((.u64)1)",   "(!0)", "(1.0<2.0)", "(1?1:2U)"};
  for (const bool ordered : {false, true})
    for (const auto type : {ScalarType::E4m3x4, ScalarType::E5m2x4}) {
      for (const auto literal : integers) {
        const auto operation = packed_origin_operation(ordered, type, literal);
        SCOPED_TRACE(operation);
        const auto ast = test_helpers::parseModule(multimem_source(operation));
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        const auto only = resolveModuleOnly(*ast);
        ASSERT_FALSE(only);
        ASSERT_FALSE(only.error().empty());
        EXPECT_EQ(only.error().front().stage(),
                  ResolveDiagnosticStage::Resolution);
        EXPECT_FALSE(only.error().front().checker_kind);
        EXPECT_NE(only.error().front().message.find("Integer literal"),
                  std::string::npos);
        EXPECT_GT(only.error().front().range.start.line, 0);
        const auto checked = resolveAndValidateModule(*ast);
        ASSERT_FALSE(checked);
        EXPECT_EQ(checked.error().front().stage(),
                  ResolveDiagnosticStage::Resolution);
        EXPECT_TRUE(validateModule(packed_origin_owner(
            packed_origin_operation(ordered, type, "0f00000001"))));
      }
      EXPECT_TRUE(validateModule(
          packed_origin_owner("mov.b32 %b0, 0x3c3c3c3c; " +
                              packed_origin_operation(ordered, type, "%b0"))));
    }
  for (const auto operation :
       {"mov.b32 %b0, 0x100000001;", "multimem.st.b32 [mm], 0x100000001;",
        "mov.b32 %b0, -1;", "mov.b32 %b0, 0xffffffffU;"}) {
    EXPECT_TRUE(validateModule(packed_origin_owner(operation)));
  }
  auto bit = packed_origin_owner("multimem.st.b32 [mm], 0x100000001;");
  auto& store = dynamic_cast<MultimemStIntegerNoneScalarDefaultWeak&>(
      *bit.functions.front().body.front());
  auto& immediate = std::get<ResolvedImmediate>(store.src.value);
  EXPECT_EQ(immediate.bits, 1u);
  EXPECT_EQ(immediate.integer_source_bits, 0x100000001ULL);
  immediate.bits = 2;
  const auto mismatch = store.check(packed_origin_context(bit));
  ASSERT_FALSE(mismatch);
  EXPECT_EQ(mismatch.error().front().kind,
            checker::CheckDiagnosticKind::ImmediateValueMismatch);
  EXPECT_NE(
      mismatch.error().front().message.find("inconsistent with its integer"),
      std::string::npos);
  EXPECT_FALSE(validateModule(bit));
}

TEST(Multimem, PackedFloatPreservesUnsignedRawWordsAndSurfaceLimits) {
  constexpr std::array<uint32_t, 8> words{0,           1,           0x80000000U,
                                          0x3c3c3c3cU, 0x3e3e3e3eU, 0x7fc00001U,
                                          0x7f800001U, 0xffffffffU};
  for (const bool ordered : {false, true})
    for (const auto type : {ScalarType::E4m3x4, ScalarType::E5m2x4}) {
      for (const auto word : words) {
        const auto operation = packed_origin_operation(
            ordered, type, fmt::format("0f{:08x}", word));
        auto module = packed_origin_owner(operation);
        auto slot = packed_origin_slot(*module.functions.front().body.front());
        const auto& immediate = std::get<ResolvedImmediate>(slot.source.value);
        EXPECT_EQ(slot.type.value, type);
        EXPECT_EQ(slot.layout.value, 0u);
        EXPECT_EQ(immediate.type, type);
        EXPECT_EQ(immediate.bits, word);
        EXPECT_FALSE(immediate.integer_source_bits);
        EXPECT_FALSE(immediate.is_negative);
        EXPECT_TRUE(validateModule(module));
        PtxSyntaxParser parser(operation);
        const auto ast = parser.parseInstruction();
        ASSERT_INSTRUCTION_PARSE_SUCCEEDS(ast);
        const auto factory = resolveMultimem(*ast);
        ASSERT_TRUE(factory) << factory.error().message;
        EXPECT_TRUE((*factory)->check(
            checker::Context{.target = packed_origin_context(module).target,
                             .instruction_range = ast->range}));
      }
      for (const auto literal :
           {"+0f3c3c3c3c", "-0f3c3c3c3c", "(0f3c3c3c3c)", "(0f3c3c3c3c+0)",
            "((.u64)0f3c3c3c3c)", "1.5", "0d3ff8000000000000", "(1.0+0.5)"}) {
        const auto operation = packed_origin_operation(ordered, type, literal);
        EXPECT_FALSE(accepts_multimem(multimem_source(operation))) << operation;
      }
      if (ordered) {
        auto operation = packed_origin_operation(true, type, "0f00000001");
        operation.replace(operation.find("relaxed.sys"), 11, "release.gpu");
        EXPECT_TRUE(validateModule(packed_origin_owner(operation)));
      }
    }
}

TEST(Multimem, PackedFloatRetainsIndependentTypeSpaceTargetAndContextChecks) {
  for (const bool ordered : {false, true})
    for (const auto type : {ScalarType::E4m3x4, ScalarType::E5m2x4}) {
      const auto operation =
          packed_origin_operation(ordered, type, "0f00000001");
      auto module = packed_origin_owner(operation);
      auto& instruction = *module.functions.front().body.front();
      auto slot = packed_origin_slot(instruction);
      auto& immediate = std::get<ResolvedImmediate>(slot.source.value);
      const auto other =
          type == ScalarType::E4m3x4 ? ScalarType::E5m2x4 : ScalarType::E4m3x4;
      immediate.type = other;
      EXPECT_FALSE(instruction.check(packed_origin_context(module)));
      EXPECT_FALSE(validateModule(module));
      slot.type.value = other;
      EXPECT_TRUE(instruction.check(packed_origin_context(module)));
      EXPECT_TRUE(validateModule(module));
      for (const auto reg : {"%byte0", "%addr"})
        EXPECT_FALSE(accepts_multimem(
            multimem_source(packed_origin_operation(ordered, type, reg))));
      for (const auto address : {"sh", "local_value"}) {
        auto changed = operation;
        changed.replace(changed.find("[mm]"), 4,
                        "[" + std::string(address) + "]");
        const auto ast = test_helpers::parseModule(multimem_source(changed));
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        const auto normalized = resolveModuleOnly(*ast);
        ASSERT_TRUE(normalized) << normalized.error().front().message;
        EXPECT_FALSE(validateModule(*normalized));
        const auto strict = resolveAndValidateModule(*ast);
        ASSERT_FALSE(strict);
        EXPECT_EQ(strict.error().front().stage(),
                  ResolveDiagnosticStage::Checking);
        EXPECT_TRUE(strict.error().front().checker_kind);
      }
      for (const auto target : {"sm_90", "sm_100"}) {
        const auto ast =
            test_helpers::parseModule(multimem_source(operation, target));
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        const auto strict = resolveAndValidateModule(*ast);
        ASSERT_FALSE(strict);
        EXPECT_EQ(strict.error().front().stage(),
                  ResolveDiagnosticStage::Checking);
      }
      for (const bool remove_version : {false, true}) {
        auto text = multimem_source(operation);
        const std::string header =
            remove_version ? ".version 9.3\n" : ".target sm_100a\n";
        text.erase(text.find(header), header.size());
        const auto ast = test_helpers::parseModule(text);
        ASSERT_MODULE_PARSE_SUCCEEDS(ast);
        auto partial = resolveModuleOnly(*ast);
        ASSERT_TRUE(partial) << partial.error().front().message;
        auto partial_slot =
            packed_origin_slot(*partial->functions.front().body.front());
        std::get<ResolvedImmediate>(partial_slot.source.value)
            .integer_source_bits = 1;
        EXPECT_TRUE(
            validateModule(*partial, ModuleValidationPolicy::AvailableContext));
        EXPECT_TRUE(validateModule(*ast, *partial,
                                   ModuleValidationPolicy::AvailableContext));
        const auto strict = validateModule(*partial);
        ASSERT_FALSE(strict);
        EXPECT_TRUE(std::ranges::any_of(strict.error(), [](const auto& d) {
          return d.kind ==
                 checker::CheckDiagnosticKind::MissingValidationContext;
        }));
        EXPECT_FALSE(partial->functions.front().body.front()->check(
            packed_origin_context(*partial)));
      }
    }
  ResolvedImmediate vector_value{.bits = 1, .type = ScalarType::F32};
  EXPECT_TRUE(valid_value_vector_immediate(vector_value));
  vector_value.integer_source_bits = 0;
  EXPECT_FALSE(valid_value_vector_immediate(vector_value));
  vector_value.integer_source_bits.reset();
  vector_value.is_negative = true;
  EXPECT_FALSE(valid_value_vector_immediate(vector_value));
}

TEST(Multimem, PackedFloatDirectViewsRetainTypedOriginAndBitConsistency) {
  using namespace checker;
  const SourceRange range{{1, 1}, {1, 10}};
  const Context context{.target = {}, .instruction_range = range};
  OperandDescriptor descriptor{
      .target_field_id = "src",
      .type_expression = {.kind = OperandTypeExpressionKind::FixedScalar,
                          .fixed_scalar_type = ScalarType::E4m3x4},
      .role = OperandRole::Source,
      .access = OperandAccess::Read,
      .allowed_shapes = OperandShape::Immediate};
  OperandView operand{.field_id = "src",
                      .actual_shape = OperandShape::Immediate,
                      .immediate_type = ScalarType::E4m3x4,
                      .immediate_bits = 0,
                      .immediate_is_negative = false,
                      .locations = std::span{&range, 1}};
  const auto check = [&] {
    return check_operands(std::span{&descriptor, 1}, {}, std::span{&operand, 1},
                          {}, context);
  };
  for (const auto type : {ScalarType::E4m3x4, ScalarType::E5m2x4}) {
    descriptor.type_expression.fixed_scalar_type = type;
    operand.immediate_type = type;
    ASSERT_TRUE(check());
    operand.integer_source_bits = 0;
    const auto zero = check();
    ASSERT_FALSE(zero);
    EXPECT_EQ(zero.error().front().kind,
              CheckDiagnosticKind::ImmediateValueMismatch);
    operand.integer_source_bits.reset();
    EXPECT_TRUE(check());
    operand.immediate_type = ScalarType::F32;
    EXPECT_FALSE(check());
    operand.immediate_type = type;
  }
  descriptor.type_expression.fixed_scalar_type = ScalarType::B32;
  operand.immediate_type = ScalarType::B32;
  operand.immediate_bits = 1;
  operand.integer_source_bits = 0x100000001ULL;
  EXPECT_TRUE(check());
  operand.immediate_bits = 2;
  EXPECT_FALSE(check());
}

/** Omitted red qualifiers retain absent source ranges and typed defaults. */
TEST(Multimem, RedQualifierProvenance) {
  auto parsed = test_helpers::parseModule(multimem_source(R"ptx(
  multimem.red.add.u32 [mm], %u0;
  multimem.red.sys.add.u32 [mm], %u0;
  multimem.red.release.add.u32 [mm], %u0;
)ptx"));
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto owned = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(owned.has_value()) << owned.error().front().message;
  const auto& body = owned->functions.front().body;
  ASSERT_GE(body.size(), 3u);
  for (std::size_t i = 0; i < 3; ++i)
    ASSERT_NE(
        dynamic_cast<MultimemRedIntegerAddScalarDefaultRed*>(body[i].get()),
        nullptr);
  const auto& bare =
      *static_cast<MultimemRedIntegerAddScalarDefaultRed*>(body[0].get());
  const auto& scope_only =
      *static_cast<MultimemRedIntegerAddScalarDefaultRed*>(body[1].get());
  const auto& semantics_only =
      *static_cast<MultimemRedIntegerAddScalarDefaultRed*>(body[2].get());
  EXPECT_EQ(bare.scope.value, MemoryScope::None);
  EXPECT_TRUE(bare.scope.locs.empty());
  EXPECT_EQ(bare.semantics.value, MemoryConsistency::Omitted);
  EXPECT_TRUE(bare.semantics.locs.empty());
  EXPECT_EQ(scope_only.scope.value, MemoryScope::Sys);
  EXPECT_FALSE(scope_only.scope.locs.empty());
  EXPECT_EQ(scope_only.semantics.value, MemoryConsistency::Omitted);
  EXPECT_EQ(semantics_only.scope.value, MemoryScope::None);
  EXPECT_TRUE(semantics_only.scope.locs.empty());
  EXPECT_EQ(semantics_only.semantics.value, MemoryConsistency::Release);
  EXPECT_FALSE(semantics_only.semantics.locs.empty());
}

/** Every ordinary scope works with omitted, weak, and supported strong ordering. */
TEST(Multimem, OrdinaryScopeAndSemanticsMatrix) {
  for (std::string_view scope : {"cta", "cluster", "gpu", "sys"}) {
    for (std::string_view semantics : {"", "weak.", "relaxed.", "acquire."}) {
      const auto prefix =
          "multimem.ld_reduce." + std::string(semantics) + std::string(scope);
      SCOPED_TRACE(prefix);
      EXPECT_TRUE(
          accepts_multimem(multimem_source(prefix + ".add.u32 %u0, [mm];")));
      EXPECT_TRUE(
          accepts_multimem(multimem_source(prefix + ".and.b64 %u64, [mm];")));
      EXPECT_TRUE(accepts_multimem(multimem_source(
          prefix + ".global.add.acc::f32.v2.f16x2 {%b0,%b1}, [mm+16];")));
      EXPECT_TRUE(accepts_multimem(multimem_source(
          prefix +
          ".max.acc::f16.v8.e4m3 "
          "{%byte0,%byte1,%byte2,%byte3,%byte4,%byte5,%byte6,%byte7}, [mm];")));
      EXPECT_FALSE(accepts_multimem(multimem_source(
          prefix +
          ".max.acc::f16.v8.e4m3 {%b0,%b1,%b2,%b3,%b4,%b5,%b6,%b7}, [mm];")));
    }
    for (std::string_view semantics : {"", "weak.", "relaxed.", "release."}) {
      const auto prefix =
          "multimem.st." + std::string(semantics) + std::string(scope);
      SCOPED_TRACE(prefix);
      EXPECT_TRUE(
          accepts_multimem(multimem_source(prefix + ".u32 [mm], %u0;")));
      EXPECT_TRUE(accepts_multimem(multimem_source(prefix + ".f64 [mm], %d;")));
      EXPECT_TRUE(accepts_multimem(multimem_source(
          prefix + ".global.v4.f32 [mm+16], {%f0,%f1,%f2,%f3};")));
      EXPECT_TRUE(accepts_multimem(multimem_source(
          prefix +
          ".v8.e5m2 [mm], "
          "{%byte0,%byte1,%byte2,%byte3,%byte4,%byte5,%byte6,%byte7};")));
      EXPECT_FALSE(accepts_multimem(multimem_source(
          prefix + ".v8.e5m2 [mm], {%b0,%b1,%b2,%b3,%b4,%b5,%b6,%b7};")));
    }
  }
}

/** Source omission and explicit ranges survive source and syntax destruction. */
TEST(Multimem, OrdinaryWeakQualifierProvenanceAfterAstDeath) {
  std::optional<ResolvedModule> owned;
  {
    const auto source = multimem_source(R"ptx(
  multimem.ld_reduce.add.u32 %u0, [mm];
  multimem.ld_reduce.weak.add.u32 %u0, [mm];
  multimem.ld_reduce.gpu.add.u32 %u0, [mm];
  multimem.ld_reduce.weak.gpu.add.u32 %u0, [mm];
  multimem.st.u32 [mm], %u0;
  multimem.st.weak.u32 [mm], %u0;
  multimem.st.gpu.u32 [mm], %u0;
  multimem.st.weak.gpu.u32 [mm], %u0;
)ptx");
    auto ast = test_helpers::parseModule(source);
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result.has_value()) << result.error().front().message;
    owned.emplace(std::move(*result));
  }
  ASSERT_TRUE(owned);
  ASSERT_TRUE(validateModule(*owned));
  const auto& body = owned->functions.front().body;
  for (std::size_t i = 0; i < 8; ++i) {
    SCOPED_TRACE(i);
    const WithLocs<MemoryConsistency>* semantics = nullptr;
    const WithLocs<MemoryScope>* scope = nullptr;
    if (i < 4) {
      const auto* form =
          dynamic_cast<const MultimemLdReduceIntegerAddScalarDefaultWeak*>(
              body[i].get());
      ASSERT_NE(form, nullptr);
      semantics = &form->semantics;
      scope = &form->scope;
    } else {
      const auto* form =
          dynamic_cast<const MultimemStIntegerNoneScalarDefaultWeak*>(
              body[i].get());
      ASSERT_NE(form, nullptr);
      semantics = &form->semantics;
      scope = &form->scope;
    }
    const bool written_weak = i % 2 == 1;
    const bool written_scope = i % 4 >= 2;
    EXPECT_EQ(semantics->value, written_weak ? MemoryConsistency::Weak
                                             : MemoryConsistency::Omitted);
    EXPECT_EQ(semantics->locs.size(), written_weak ? 1u : 0u);
    EXPECT_EQ(scope->value,
              written_scope ? MemoryScope::Gpu : MemoryScope::None);
    EXPECT_EQ(scope->locs.size(), written_scope ? 1u : 0u);
    if (written_weak && written_scope) {
      EXPECT_EQ(semantics->locs.front().start.line,
                scope->locs.front().start.line);
      EXPECT_LT(semantics->locs.front().start.column,
                scope->locs.front().start.column);
    }
  }
  auto* form =
      dynamic_cast<MultimemStIntegerNoneScalarDefaultWeak*>(body[6].get());
  ASSERT_NE(form, nullptr);
  form->scope.value = static_cast<MemoryScope>(255);
  EXPECT_FALSE(validateModule(*owned));
  form->scope.value = MemoryScope::Gpu;
  form->semantics.value = MemoryConsistency::Release;
  EXPECT_FALSE(validateModule(*owned));
}

/** Optional ordinary scope does not loosen order, operands, or neighboring families. */
TEST(Multimem, OrdinaryScopeNegativeBoundaries) {
  for (std::string_view candidate : {
           "multimem.ld_reduce.acquire.add.u32 %u0, [mm];",
           "multimem.st.release.u32 [mm], %u0;",
           "multimem.st.relaxed.u32 [mm], %u0;",
           "multimem.ld_reduce.weak.warp.add.u32 %u0, [mm];",
           "multimem.st.warp.u32 [mm], %u0;",
           "multimem.ld_reduce.gpu.weak.add.u32 %u0, [mm];",
           "multimem.st.gpu.weak.u32 [mm], %u0;",
           "multimem.ld_reduce.global.gpu.add.u32 %u0, [mm];",
           "multimem.st.global.gpu.u32 [mm], %u0;",
           "multimem.ld_reduce.release.gpu.add.u32 %u0, [mm];",
           "multimem.st.acquire.gpu.u32 [mm], %u0;",
           "multimem.st.weak.gpu.sys.u32 [mm], %u0;",
           "multimem.ld_reduce.gpu.add.s64 %u64, [mm];",
           "multimem.ld_reduce.gpu.min.f32 %f0, [mm];",
           "multimem.ld_reduce.gpu.add.u32 %u0, [sh];",
           "multimem.st.gpu.u32 [local_value], %u0;",
           "multimem.st.gpu.u32 [mm+2147483648], %u0;",
           "multimem.st.gpu.u32 [mm+2], %u0;",
           "multimem.st.gpu.u64 [mm], %u0;",
           "multimem.st.gpu.v2.f32 [mm], {%f0};",
           "multimem.st.weak.gpu.v4.f32 [mm+4], {%f0,%f1,%f2,%f3};",
           "multimem.ld_reduce.gpu.add.v2.f32 {%f0,%f1}, [mm+4];",
           "multimem.st.gpu.v8.f64 [mm], {%f0,%f1,%f2,%f3,%f4,%f5,%f6,%f7};",
           "multimem.red.weak.gpu.add.u32 [mm], %u0;",
           "multimem.st.async.gpu.u32 [%addr], %u0;",
           "multimem.red.async.gpu.add.u32 [%addr], %u0;",
           "multimem.cp.async.bulk.gpu.global.shared::cta.bulk_group [mm], "
           "[sh], 16;",
           "multimem.cp.reduce.async.bulk.gpu.global.shared::cta.bulk_group."
           "add.u32 [mm], [sh], 16;",
       }) {
    SCOPED_TRACE(candidate);
    EXPECT_FALSE(accepts_multimem(multimem_source(candidate)));
  }
  for (std::string_view candidate : {
           "multimem.ld_reduce.gpu.add.u32 %u0, [mm];",
           "multimem.ld_reduce.weak.cta.add.u32 %u0, [mm];",
           "multimem.st.gpu.u32 [mm], %u0;",
           "multimem.st.weak.sys.u32 [mm], %u0;",
       }) {
    SCOPED_TRACE(candidate);
    EXPECT_TRUE(accepts_multimem(multimem_source(candidate, "sm_90", "8.1")));
    EXPECT_FALSE(accepts_multimem(multimem_source(candidate, "sm_90", "8.0")));
    EXPECT_FALSE(accepts_multimem(multimem_source(candidate, "sm_80", "8.1")));
  }
  EXPECT_FALSE(accepts_multimem(multimem_source(
      "multimem.ld_reduce.gpu.add.acc::f32.f16x2 %b0, [mm];", "sm_90", "8.1")));
  EXPECT_FALSE(accepts_multimem(
      multimem_source("multimem.st.gpu.e4m3x4 [mm], %b0;", "sm_90")));
}

/** A vector address uses the total byte width even after typed IR mutation. */
TEST(Multimem, VectorAlignmentAfterResolution) {
  auto parsed = test_helpers::parseModule(
      multimem_source("multimem.st.v4.f32 [mm+16], {%f0,%f1,%f2,%f3};"));
  ASSERT_MODULE_PARSE_SUCCEEDS(parsed);
  auto owned = resolveAndValidateModule(*parsed);
  ASSERT_TRUE(owned.has_value()) << owned.error().front().message;
  auto* form = dynamic_cast<MultimemStFloatNoneV4DefaultWeak*>(
      owned->functions.front().body.front().get());
  ASSERT_NE(form, nullptr);
  ASSERT_TRUE(form->address.value.offset);
  const checker::Context context{
      .target = {.ptx_version = {9, 3}, .sm_version = 100}};
  EXPECT_TRUE(form->check(context));
  form->address.value.offset->value.bits = 4;
  form->address.value.offset->value.integer_source_bits = 4;
  EXPECT_FALSE(form->check(context));
}

/** Source/PTX floors and architectural qualifiers are independent. */
TEST(Multimem, TargetAndVersionIntersections) {
  constexpr std::string_view base =
      "multimem.cp.async.bulk.global.shared::cta.bulk_group [mm], [sh], 16;";
  EXPECT_TRUE(accepts_multimem(multimem_source(base, "sm_90", "9.1")));
  EXPECT_FALSE(accepts_multimem(multimem_source(base, "sm_90", "9.0")));
  constexpr std::string_view explicit_weak =
      "multimem.cp.async.bulk.weak.global.shared::cta.bulk_group [mm], [sh], "
      "16;";
  EXPECT_FALSE(accepts_multimem(multimem_source(explicit_weak, "sm_90")));
  EXPECT_TRUE(accepts_multimem(multimem_source(explicit_weak, "sm_90a")));
  constexpr std::string_view relaxed =
      "multimem.cp.async.bulk.relaxed.cta.global.shared::cta.bulk_group.b128 "
      "[mm], [sh], 16;";
  constexpr std::string_view relaxed_mask =
      "multimem.cp.async.bulk.relaxed.cta.global.shared::cta.bulk_group.cp_"
      "mask.b128 [mm], [sh], 16, %mask;";
  EXPECT_TRUE(accepts_multimem(multimem_source(relaxed, "sm_90a")));
  EXPECT_FALSE(accepts_multimem(multimem_source(relaxed_mask, "sm_90a")));
  EXPECT_TRUE(accepts_multimem(multimem_source(relaxed_mask, "sm_100f")));
  constexpr std::string_view fp8 = "multimem.st.e4m3x4 [mm], %b0;";
  EXPECT_FALSE(accepts_multimem(multimem_source(fp8, "sm_90", "9.3")));
  EXPECT_TRUE(accepts_multimem(multimem_source(fp8, "sm_100a", "9.3")));
  constexpr std::string_view acc_f32 =
      "multimem.ld_reduce.add.acc::f32.f16x2 %b0, [mm];";
  EXPECT_FALSE(accepts_multimem(multimem_source(acc_f32, "sm_90", "8.1")));
  EXPECT_TRUE(accepts_multimem(multimem_source(acc_f32, "sm_90", "8.2")));
  constexpr std::string_view scoped_reduce =
      "multimem.cp.reduce.async.bulk.relaxed.cta.global.shared::cta.bulk_group."
      "add.u32 [mm], [sh], 16;";
  EXPECT_FALSE(
      accepts_multimem(multimem_source(scoped_reduce, "sm_90", "9.1")));
  EXPECT_TRUE(accepts_multimem(multimem_source(scoped_reduce, "sm_90", "9.3")));
  EXPECT_FALSE(accepts_multimem(multimem_source(
      "multimem.st.async.release.gpu.u32 [%addr], %u0;", "sm_90")));
  EXPECT_FALSE(accepts_multimem(multimem_source(
      "multimem.st.async.release.gpu.u32 [%addr], %u0;", "sm_100", "9.2")));
}

/** Bound references and metadata survive AST destruction and reject tampering. */
TEST(Multimem, OwnedModuleBindingAfterAstDeath) {
  std::optional<ResolvedModule> owned;
  {
    auto ast = test_helpers::parseModule(
        multimem_source("multimem.cp.async.bulk.global.shared::cta.bulk_group "
                        "[mm], [sh], 16;"));
    ASSERT_MODULE_PARSE_SUCCEEDS(ast);
    auto result = resolveAndValidateModule(*ast);
    ASSERT_TRUE(result.has_value()) << result.error().front().message;
    owned.emplace(std::move(*result));
  }
  ASSERT_TRUE(owned);
  EXPECT_TRUE(validateModule(*owned));
  auto* form = dynamic_cast<MultimemCpAsyncBulkWeak*>(
      owned->functions.front().body.front().get());
  ASSERT_NE(form, nullptr);
  auto* symbol = std::get_if<ResolvedSymbolRef>(&form->dst.value.base);
  ASSERT_NE(symbol, nullptr);
  ASSERT_TRUE(symbol->address_alignment);
  symbol->address_alignment = 1;
  EXPECT_FALSE(validateModule(*owned));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
