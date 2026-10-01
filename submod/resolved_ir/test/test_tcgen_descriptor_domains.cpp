#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

#include <gtest/gtest.h>

#include <ptx_frontend/resolved_ir/ptx_tcgen_descriptors.hpp>

namespace ptx_frontend::resolved_ir {
namespace {

/** Find one independently expected violation in a defined-field report. */
bool has_violation(const TcgenDefinedFieldReport& report,
                   TcgenDescriptorViolation kind) {
  return std::any_of(
      report.violations.begin(), report.violations.end(),
      [kind](const TcgenFieldFinding& item) { return item.kind == kind; });
}

/** Find one missing fact without conflating it with a field violation. */
bool has_obligation(const TcgenDefinedFieldReport& report,
                    TcgenDescriptorObligation kind) {
  return std::find(report.missing_context.begin(), report.missing_context.end(),
                   kind) != report.missing_context.end();
}

TEST(TcgenDescriptorDomains, SharedLiteralFieldsAndHighAddressBits) {
  // Fixed code 1 at 46; swizzle 2 at 61; start=1 and leading=1.
  constexpr uint64_t bits = (1ULL << 46) | (2ULL << 61) | (1ULL << 16) | 1;
  const TcgenSharedWord word{bits};
  const auto decoded = decode_tcgen_shared(word);
  EXPECT_EQ(decoded.start_low18_bytes, 16U);
  EXPECT_EQ(decoded.leading_low18_bytes, 16U);
  EXPECT_EQ(decoded.swizzle, TcgenSwizzle::B128Atom16);
  TcgenSharedContext context;
  context.major = TcgenMajor::K;
  context.bytes.start = (1ULL << 40) | 16;
  context.bytes.leading = (1ULL << 42) | 16;
  context.bytes.repeating_pattern_start = 1024;
  EXPECT_TRUE(
      validate_tcgen_shared_defined_fields(word, context).defined_fields_ok());
  context.bytes.start = 17;
  EXPECT_TRUE(has_violation(validate_tcgen_shared_defined_fields(word, context),
                            TcgenDescriptorViolation::Alignment));
  context.bytes.start = 32;
  EXPECT_TRUE(has_violation(validate_tcgen_shared_defined_fields(word, context),
                            TcgenDescriptorViolation::EncodedByteMismatch));
  context.bytes.start = 16;
  context.bytes.leading = 32;
  EXPECT_FALSE(
      has_violation(validate_tcgen_shared_defined_fields(word, context),
                    TcgenDescriptorViolation::EncodedByteMismatch));
  context.major = TcgenMajor::MN;
  EXPECT_TRUE(has_violation(validate_tcgen_shared_defined_fields(word, context),
                            TcgenDescriptorViolation::EncodedByteMismatch));
  EXPECT_TRUE(has_violation(
      validate_tcgen_shared_defined_fields(TcgenSharedWord{bits | (1ULL << 14)},
                                           TcgenSharedContext{}),
      TcgenDescriptorViolation::FixedField));
}

TEST(TcgenDescriptorDomains, SharedFixedBitsAndPatternContext) {
  constexpr uint64_t fixed = 1ULL << 46;
  const auto* table = tcgen_word_table(TcgenWordTableKind::Shared);
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(table->fixed_mask & fixed, fixed);
  EXPECT_EQ(table->reserved_mask & fixed, 0U);
  EXPECT_EQ(table->unclassified_mask, 0U);
  EXPECT_EQ(tcgen_word_table(static_cast<TcgenWordTableKind>(255)), nullptr);
  for (const auto bit :
       {14U, 15U, 30U, 31U, 47U, 48U, 53U, 54U, 55U, 56U, 57U, 58U, 59U, 60U}) {
    SCOPED_TRACE(bit);
    EXPECT_TRUE(has_violation(
        validate_tcgen_shared_defined_fields({fixed | (1ULL << bit)}, {}),
        TcgenDescriptorViolation::FixedField));
  }
  EXPECT_TRUE(has_violation(validate_tcgen_shared_defined_fields({0}, {}),
                            TcgenDescriptorViolation::FixedField));
  // Ordinary code 4 has a 512-byte repeating-pattern boundary.
  const TcgenSharedWord word{fixed | (4ULL << 61)};
  TcgenSharedContext context;
  context.major = TcgenMajor::MN;
  EXPECT_TRUE(
      has_obligation(validate_tcgen_shared_defined_fields(word, context),
                     TcgenDescriptorObligation::PatternStart));
  context.bytes.repeating_pattern_start = 512;
  EXPECT_TRUE(
      validate_tcgen_shared_defined_fields(word, context).defined_fields_ok());
  context.bytes.repeating_pattern_start = 128;
  EXPECT_TRUE(has_violation(validate_tcgen_shared_defined_fields(word, context),
                            TcgenDescriptorViolation::PatternBase));
  const auto special =
      validate_tcgen_shared_defined_fields({fixed | (1ULL << 61)}, context);
  EXPECT_TRUE(special.defined_fields_ok());
  EXPECT_TRUE(
      has_obligation(special, TcgenDescriptorObligation::PatternApplicability));
}

TEST(TcgenDescriptorDomains, OrdinaryPatternBoundaryAndNonzeroBase) {
  constexpr uint64_t fixed = 1ULL << 46;
  constexpr std::array<std::pair<uint64_t, uint64_t>, 3> patterns{{
      {2, 1024},
      {4, 512},
      {6, 256},
  }};
  for (const auto [code, boundary] : patterns) {
    SCOPED_TRACE(code);
    const TcgenSharedWord base_zero{fixed | (code << 61)};
    const TcgenSharedWord base_one{base_zero.bits | (1ULL << 49)};
    TcgenSharedContext context;
    context.major = TcgenMajor::MN;
    EXPECT_TRUE(
        has_obligation(validate_tcgen_shared_defined_fields(base_zero, context),
                       TcgenDescriptorObligation::PatternStart));
    context.bytes.repeating_pattern_start = boundary;
    EXPECT_FALSE(
        has_violation(validate_tcgen_shared_defined_fields(base_zero, context),
                      TcgenDescriptorViolation::PatternBase));
    EXPECT_TRUE(
        has_violation(validate_tcgen_shared_defined_fields(base_one, context),
                      TcgenDescriptorViolation::PatternBase));
    context.bytes.repeating_pattern_start = 128;
    EXPECT_FALSE(
        has_violation(validate_tcgen_shared_defined_fields(base_one, context),
                      TcgenDescriptorViolation::PatternBase));
    EXPECT_TRUE(
        has_violation(validate_tcgen_shared_defined_fields(base_zero, context),
                      TcgenDescriptorViolation::PatternBase));
    context.bytes.repeating_pattern_start = 129;
    EXPECT_FALSE(
        has_violation(validate_tcgen_shared_defined_fields(base_one, context),
                      TcgenDescriptorViolation::PatternBase));
    context.bytes.repeating_pattern_start = 16;
    EXPECT_TRUE(
        has_violation(validate_tcgen_shared_defined_fields(base_zero, context),
                      TcgenDescriptorViolation::PatternBase));
    EXPECT_TRUE(
        has_violation(validate_tcgen_shared_defined_fields(base_one, context),
                      TcgenDescriptorViolation::PatternBase));
  }
}

TEST(TcgenDescriptorDomains, AllSwizzleCodesAndEightStaticRows) {
  constexpr uint64_t fixed = 1ULL << 46;
  for (const auto code : {0U, 1U, 2U, 4U, 6U})
    EXPECT_TRUE(
        decode_tcgen_shared(TcgenSharedWord{fixed | (uint64_t(code) << 61)})
            .swizzle.has_value());
  for (const auto code : {3U, 5U, 7U})
    EXPECT_TRUE(
        has_violation(validate_tcgen_shared_defined_fields(
                          TcgenSharedWord{fixed | (uint64_t(code) << 61)}, {}),
                      TcgenDescriptorViolation::InvalidCode));
  const auto rows = tcgen_relative_layout_rows();
  ASSERT_EQ(rows.size(), 8U);
  for (const auto major : {TcgenMajor::K, TcgenMajor::MN})
    for (const auto swizzle : {TcgenSwizzle::None, TcgenSwizzle::B32,
                               TcgenSwizzle::B64, TcgenSwizzle::B128Atom16})
      EXPECT_NE(tcgen_relative_layout(major, swizzle), nullptr);
  EXPECT_EQ(tcgen_relative_layout(TcgenMajor::K, TcgenSwizzle::B128Atom32),
            nullptr);
  EXPECT_EQ(tcgen_special_atom(TcgenMajor::MN, TcgenSwizzle::B128Atom32),
            (std::array<uint8_t, 2>{8, 4}));
  EXPECT_FALSE(tcgen_special_atom(TcgenMajor::K, TcgenSwizzle::B128Atom32));
  // The row holds symbolic 2*T, not an integer-truncated packed type width.
  const auto* k32 = tcgen_relative_layout(TcgenMajor::K, TcgenSwizzle::B32);
  ASSERT_NE(k32, nullptr);
  EXPECT_EQ(k32->stride[0].terms[0].multiplier, 2);
  EXPECT_EQ(k32->stride[0].terms[0].symbol, TcgenLayoutSymbol::T);
  EXPECT_EQ(rows.data(), tcgen_relative_layout_rows().data());
}

TEST(TcgenDescriptorDomains, AbsoluteModeRequiresExactKnownContext) {
  constexpr uint64_t bits = (1ULL << 46) | (2ULL << 61) | (1ULL << 52);
  TcgenSharedContext context;
  auto missing = validate_tcgen_shared_defined_fields({bits}, context);
  EXPECT_TRUE(has_obligation(missing, TcgenDescriptorObligation::Major));
  EXPECT_TRUE(has_obligation(missing, TcgenDescriptorObligation::Target));
  EXPECT_TRUE(
      has_obligation(missing, TcgenDescriptorObligation::InstructionTranspose));
  context.major = TcgenMajor::K;
  context.target =
      base::TargetIdentity{base::TargetArchitecture{103},
                           base::TargetFlavor::ArchitectureSpecific, "sm_103a"};
  context.ptx_version = checker::PtxVersion{8, 8};
  context.transpose_a = false;
  context.transpose_b = false;
  EXPECT_TRUE(validate_tcgen_shared_defined_fields({bits}, context)
                  .defined_fields_ok());
  context.target->source_spelling = "sm_103f";
  EXPECT_TRUE(
      has_violation(validate_tcgen_shared_defined_fields({bits}, context),
                    TcgenDescriptorViolation::AbsoluteTarget));
  context.target->source_spelling = "sm_103a";
  context.major = TcgenMajor::MN;
  EXPECT_TRUE(
      has_violation(validate_tcgen_shared_defined_fields({bits}, context),
                    TcgenDescriptorViolation::AbsoluteMajor));
  context.major = TcgenMajor::K;
  context.transpose_b = true;
  EXPECT_TRUE(
      has_violation(validate_tcgen_shared_defined_fields({bits}, context),
                    TcgenDescriptorViolation::AbsoluteTranspose));
}

TEST(TcgenDescriptorDomains, AllSevenKindsUseIntrinsicFieldRulesOnly) {
  // Independent Table 45 tf32: D=F32, A=B=tf32, M=N=0 is field-valid.
  constexpr uint32_t tf32 = 0x910;
  EXPECT_TRUE(
      validate_tcgen_instruction_defined_fields({tf32, TcgenMmaKind::Tf32})
          .defined_fields_ok());
  EXPECT_EQ(decode_tcgen_instruction({tf32, TcgenMmaKind::Tf32}).a_type,
            MatrixElementType::TF32);
  EXPECT_TRUE(validate_tcgen_instruction_defined_fields({0, TcgenMmaKind::F16})
                  .defined_fields_ok());
  EXPECT_TRUE(
      validate_tcgen_instruction_defined_fields({0, TcgenMmaKind::F8F6F4})
          .defined_fields_ok());
  EXPECT_TRUE(
      validate_tcgen_instruction_defined_fields({0x28, TcgenMmaKind::I8})
          .defined_fields_ok());
  EXPECT_TRUE(validate_tcgen_instruction_defined_fields(
                  {1U << 23, TcgenMmaKind::MxF8F6F4})
                  .defined_fields_ok());
  constexpr uint32_t f4_types = (1U << 7) | (1U << 10);
  EXPECT_TRUE(validate_tcgen_instruction_defined_fields(
                  {f4_types | (1U << 23), TcgenMmaKind::MxF4})
                  .defined_fields_ok());
  EXPECT_TRUE(validate_tcgen_instruction_defined_fields(
                  {f4_types, TcgenMmaKind::MxF4NvF4})
                  .defined_fields_ok());
  EXPECT_TRUE(has_violation(
      validate_tcgen_instruction_defined_fields(
          {f4_types | (1U << 2) | (1U << 31), TcgenMmaKind::MxF4NvF4}),
      TcgenDescriptorViolation::SparseKChoice));
  EXPECT_TRUE(has_violation(validate_tcgen_instruction_defined_fields(
                                {0, static_cast<TcgenMmaKind>(255)}),
                            TcgenDescriptorViolation::InvalidKind));
  EXPECT_TRUE(has_violation(validate_tcgen_instruction_defined_fields(
                                {tf32 | (1U << 6), TcgenMmaKind::Tf32}),
                            TcgenDescriptorViolation::FixedField));
  // Dense selector and non-WS reuse are encoded but not invented reserved bits.
  EXPECT_TRUE(validate_tcgen_instruction_defined_fields(
                  {tf32 | 3U | (3U << 30), TcgenMmaKind::Tf32})
                  .defined_fields_ok());
  EXPECT_EQ(decode_tcgen_instruction({tf32 | (3U << 30), TcgenMmaKind::Tf32})
                .reuse_shift,
            TcgenReuseShift::B32);
  EXPECT_TRUE(has_violation(validate_tcgen_instruction_defined_fields(
                                {tf32 | (1U << 3), TcgenMmaKind::Tf32}),
                            TcgenDescriptorViolation::Saturation));
  EXPECT_TRUE(has_violation(validate_tcgen_instruction_defined_fields(
                                {0x28 | (1U << 13), TcgenMmaKind::I8}),
                            TcgenDescriptorViolation::Negation));
  EXPECT_TRUE(has_violation(
      validate_tcgen_instruction_defined_fields(
          {f4_types | (1U << 23) | (1U << 29), TcgenMmaKind::MxF4}),
      TcgenDescriptorViolation::ScaleId));
  EXPECT_TRUE(has_violation(
      validate_tcgen_instruction_defined_fields({f4_types, TcgenMmaKind::MxF4}),
      TcgenDescriptorViolation::InvalidCode));
  EXPECT_TRUE(
      has_violation(validate_tcgen_instruction_defined_fields(
                        {(1U << 23) | (1U << 31), TcgenMmaKind::MxF8F6F4}),
                    TcgenDescriptorViolation::FixedField));
}

TEST(TcgenDescriptorDomains, InstructionFixedBitsRemainTableSpecific) {
  constexpr uint32_t tf32 = 0x910;
  for (const auto bit : {6U, 23U, 29U}) {
    SCOPED_TRACE(bit);
    EXPECT_TRUE(has_violation(validate_tcgen_instruction_defined_fields(
                                  {tf32 | (1U << bit), TcgenMmaKind::Tf32}),
                              TcgenDescriptorViolation::FixedField));
  }
  constexpr uint32_t mixed = 1U << 23;
  for (const auto bit : {0U, 1U, 3U, 6U, 24U, 25U, 26U, 31U}) {
    SCOPED_TRACE(bit);
    EXPECT_TRUE(
        has_violation(validate_tcgen_instruction_defined_fields(
                          {mixed | (1U << bit), TcgenMmaKind::MxF8F6F4}),
                      TcgenDescriptorViolation::FixedField));
  }
  EXPECT_TRUE(has_violation(
      validate_tcgen_instruction_defined_fields({0, TcgenMmaKind::MxF8F6F4}),
      TcgenDescriptorViolation::FixedField));
  constexpr uint32_t f4 = (1U << 7) | (1U << 10) | (1U << 23);
  for (const auto bit : {0U, 1U, 3U, 6U, 12U, 15U, 16U, 24U, 25U, 26U}) {
    SCOPED_TRACE(bit);
    EXPECT_TRUE(has_violation(validate_tcgen_instruction_defined_fields(
                                  {f4 | (1U << bit), TcgenMmaKind::MxF4}),
                              TcgenDescriptorViolation::FixedField));
  }
  for (const auto bit : {7U, 10U}) {
    SCOPED_TRACE(bit);
    EXPECT_TRUE(has_violation(validate_tcgen_instruction_defined_fields(
                                  {f4 & ~(1U << bit), TcgenMmaKind::MxF4}),
                              TcgenDescriptorViolation::FixedField));
  }
}

TEST(TcgenDescriptorDomains, ZeroMaskPartitionsAndUnclassifiedBits) {
  EXPECT_EQ(tcgen_zero_partition(128, 256)->active_masks, 1);
  EXPECT_EQ(tcgen_zero_partition(64, 256)->columns_per_mask, 128U);
  EXPECT_EQ(tcgen_zero_partition(32, 256)->columns_per_mask, 64U);
  EXPECT_FALSE(tcgen_zero_partition(32, 255));
  const TcgenZeroColumnWord word{(uint64_t{3} << 62) | (uint64_t{255} << 40)};
  const auto decoded = decode_tcgen_zero_column(word);
  EXPECT_EQ(decoded.skip_span_columns, 256U);
  EXPECT_EQ(decoded.use_span_columns, 1U);
  EXPECT_EQ(decoded.unclassified_bits, (uint64_t{3} << 62));
  auto report = validate_tcgen_zero_defined_fields(word, {32, 256});
  EXPECT_TRUE(report.defined_fields_ok());
  EXPECT_EQ(report.unclassified_bits, (uint64_t{3} << 62));
  EXPECT_TRUE(has_violation(validate_tcgen_zero_defined_fields(
                                {word.bits | (uint64_t{17} << 56)}, {32, 256}),
                            TcgenDescriptorViolation::ZeroShift));
  EXPECT_FALSE(has_violation(validate_tcgen_zero_defined_fields(
                                 {word.bits | (uint64_t{16} << 56)}, {32, 256}),
                             TcgenDescriptorViolation::ZeroShift));
  EXPECT_TRUE(has_violation(validate_tcgen_zero_defined_fields(
                                {word.bits | (uint64_t{33} << 56)}, {64, 256}),
                            TcgenDescriptorViolation::ZeroShift));
  EXPECT_TRUE(has_obligation(validate_tcgen_zero_defined_fields(word, {}),
                             TcgenDescriptorObligation::ZeroM));
  EXPECT_TRUE(has_violation(validate_tcgen_zero_defined_fields(
                                {word.bits | (1ULL << 36)}, {128, 256}),
                            TcgenDescriptorViolation::FixedField));
  const auto independent = decode_tcgen_zero_column(
      {0x04030201ULL | (0xAULL << 32) | (1ULL << 39) | (255ULL << 48)});
  EXPECT_EQ(independent.start_counts, (std::array<uint8_t, 4>{1, 2, 3, 4}));
  EXPECT_EQ(independent.first_span,
            (std::array<bool, 4>{false, true, false, true}));
  EXPECT_TRUE(independent.generate_mask);
  const auto mask_disabled = decode_tcgen_zero_column(
      {0x04030201ULL | (0xAULL << 32) | (255ULL << 48)});
  EXPECT_FALSE(mask_disabled.generate_mask);
  EXPECT_EQ(mask_disabled.start_counts, independent.start_counts);
  EXPECT_TRUE(
      validate_tcgen_zero_defined_fields({word.bits | (1ULL << 39)}, {32, 256})
          .defined_fields_ok());
  EXPECT_EQ(independent.use_span_columns, 256U);
  EXPECT_TRUE(has_violation(
      validate_tcgen_zero_defined_fields({word.bits | (1ULL << 37)}, {32, 255}),
      TcgenDescriptorViolation::ZeroPartition));
  EXPECT_FALSE(has_violation(validate_tcgen_zero_defined_fields(
                                 {word.bits | (uint64_t{32} << 56)}, {64, 256}),
                             TcgenDescriptorViolation::ZeroShift));
  EXPECT_TRUE(
      has_violation(validate_tcgen_zero_defined_fields(
                        {word.bits | (1ULL << 39) | (1ULL << 36)}, {128, 256}),
                    TcgenDescriptorViolation::FixedField));
}

}  // namespace
}  // namespace ptx_frontend::resolved_ir
