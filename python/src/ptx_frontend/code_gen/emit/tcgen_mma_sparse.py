"""Render shared non-block-scaled sparse MMA rows from the operations owner."""

from __future__ import annotations

from ptx_frontend.spec import tcgen_mma_operations as operations


def render_sparse_header() -> str:
    """Declare typed sparse rows, caller facts and conditional query results."""

    return r'''
namespace ptx_frontend::resolved_ir {
/** One sparse Table 42 logical shape; A is stored with half of K. */
struct TcgenSparseShapeRow {
  /** Exact descriptor kind selected by the source identity. */
  TcgenMmaKind kind;
  /** Typed source CTA group. */
  TcgenCtaGroup group;
  /** Output element type from the known word. */
  MatrixElementType d_type;
  /** Closed M values in rows. */
  std::array<uint16_t, 2> m_values;
  /** Exceptional N values before the regular grid, zero padded. */
  std::array<uint16_t, 4> n_small;
  /** Number of exceptional N entries. */
  uint8_t n_small_count;
  /** First regular N in columns. */
  uint16_t n_first;
  /** Legal N stride in columns. */
  uint16_t n_step;
  /** Last regular N in columns. */
  uint16_t n_last;
  /** Logical K in elements; compressed A has K/2 stored elements. */
  uint16_t k;
  /** Legal encoded A types for this exact output row, zero padded. */
  std::array<MatrixElementType, 5> a_types;
  /** Number of defined A entries. */
  uint8_t a_type_count;
  /** Legal encoded B types for this exact output row, zero padded. */
  std::array<MatrixElementType, 5> b_types;
  /** Number of defined B entries. */
  uint8_t b_type_count;
};
/** Sparse non-WS group/M datapath with a possible half-lane restriction. */
struct TcgenSparsePathRow {
  /** CTA group selecting the path. */
  TcgenCtaGroup group;
  /** M dimension selecting the path. */
  uint16_t m;
  /** Fixed layout identity A, C, D or F. */
  char layout;
  /** Whether A when in Tensor Memory, D, and metadata use one lane half. */
  bool half_path;
  /** Defined lane-half identities for each applicable address role. */
  std::array<uint8_t, 2> allowed_lane_halves;
  /** Number of defined lane-half identities. */
  uint8_t allowed_lane_count;
};
/** Element grouping represented by one metadata nibble. */
enum class TcgenSparseGranularity : uint8_t { OneOfTwo, TwoOfFour };
/** One kind-specific normative metadata index domain. */
struct TcgenSparseMetadataRule {
  /** Kind whose metadata this row describes. */
  TcgenMmaKind kind;
  /** Logical nonzero-to-total element grouping. */
  TcgenSparseGranularity granularity;
  /** Meaningful nibble values, zero padded to seven slots. */
  std::array<uint8_t, 7> valid_nibbles;
  /** Number of defined values in valid_nibbles. */
  uint8_t valid_count;
};
/** Borrow generated sparse shape rows from the single operational catalogue. */
[[nodiscard]] std::span<const TcgenSparseShapeRow>
tcgen_sparse_shape_rows() noexcept;
/** Borrow generated sparse datapath rows. */
[[nodiscard]] std::span<const TcgenSparsePathRow>
tcgen_sparse_path_rows() noexcept;
/** Borrow generated kind-specific metadata rules. */
[[nodiscard]] std::span<const TcgenSparseMetadataRule>
tcgen_sparse_metadata_rules() noexcept;
/** Facts actually checked with enough independent information. */
enum class TcgenSparseChecked : uint8_t {
  InstructionFields, Sparse, Shape, Types, Selector, MetadataIndices,
  ASharedFields, BSharedFields, BTransposeN, Datapath, HalfAlignment, Target
};
/** Contradictions proven by independently supplied words or source facts. */
enum class TcgenSparseViolation : uint8_t {
  Kind, Group, SparseBit, Shape, AType, BType, Selector, ScaleD,
  MetadataIndex, APlacementFacts, AMajor, BMajor, ASwizzle, BSwizzle,
  BTransposeN,
  ALaneHalf, DLaneHalf, MetadataLaneHalf, HalfAlignment, Target,
  InvalidContext
};
/** Missing facts or live-content duties not established by static source. */
enum class TcgenSparseObligation : uint8_t {
  ASharedWord, BSharedWord, AMajor, BMajor, MixedInputPair,
  ALowBitTransposeRule, BLowBitTransposeRule,
  ALowBitPackingRule, BLowBitPackingRule,
  MetadataIndices, LiveMetadataContents, SparseAContents,
  ALaneHalf, DLaneHalf, MetadataLaneHalf, Target
};
/** Independent known words and source topology; no register bytes are read. */
struct TcgenSparseKnownFacts {
  /** Exact kind from the source spelling, checked against instruction.kind. */
  TcgenMmaKind source_kind;
  /** Typed source CTA group. */
  TcgenCtaGroup group;
  /** Whether A uses Tensor Memory rather than a shared descriptor. */
  bool a_in_tmem;
  /** Independently known instruction descriptor bits. */
  TcgenInstructionWord instruction;
  /** Optional source D-scale immediate with original evaluated value. */
  std::optional<int64_t> scale_d;
  /** Optional independently known shared A word. */
  std::optional<TcgenSharedWord> a_shared_word;
  /** Optional independently known shared B word. */
  std::optional<TcgenSharedWord> b_shared_word;
  /** Context for the shared A field check. */
  TcgenSharedContext a_context;
  /** Context for the shared B field check. */
  TcgenSharedContext b_context;
  /** Known Tensor Memory A lane half, in lanes 0 or 16. */
  std::optional<uint8_t> a_lane_half;
  /** Known D lane half, in lanes 0 or 16. */
  std::optional<uint8_t> d_lane_half;
  /** Known sparse metadata lane half, in lanes 0 or 16. */
  std::optional<uint8_t> metadata_lane_half;
  /** Independently supplied nibble claims; never decoded from live memory. */
  std::optional<std::vector<uint8_t>> metadata_nibbles;
  /** Catalogued target identity, when known. */
  std::optional<base::TargetIdentity> target;
  /** PTX version, when known. */
  std::optional<checker::PtxVersion> ptx_version;
};
/** Defined-field results and conditional sparse operational conclusions. */
struct TcgenSparseOperationalReport {
  /** Table 45 defined-field result for the known instruction word. */
  TcgenDefinedFieldReport instruction_fields;
  /** Table 43 result for a supplied shared A word. */
  std::optional<TcgenDefinedFieldReport> a_shared_fields;
  /** Table 43 result for a supplied shared B word. */
  std::optional<TcgenDefinedFieldReport> b_shared_fields;
  /** Rules actually evaluated with enough facts. */
  std::vector<TcgenSparseChecked> checked;
  /** Contradictions proven by supplied values. */
  std::vector<TcgenSparseViolation> violations;
  /** Missing facts and live-content obligations. */
  std::vector<TcgenSparseObligation> missing;
  /** Selected Table 42 row, if every shape field is known and legal. */
  std::optional<TcgenSparseShapeRow> shape;
  /** Selected sparse metadata domain, if the kind is supported. */
  std::optional<TcgenSparseMetadataRule> metadata_rule;
  /** Selected non-WS datapath, if group/M match a supported shape. */
  std::optional<char> path_layout;
  /** Stored A K when shape is selected, measured in elements. */
  std::optional<uint16_t> compressed_a_k;
  /** Logical B K when shape is selected, measured in elements. */
  std::optional<uint16_t> logical_b_k;
  /** Whether each supplied and evaluated rule passed independently. */
  [[nodiscard]] bool supplied_facts_ok() const noexcept {
    return instruction_fields.defined_fields_ok() &&
           (!a_shared_fields || a_shared_fields->defined_fields_ok()) &&
           (!b_shared_fields || b_shared_fields->defined_fields_ok()) &&
           violations.empty();
  }
};
/** Check source-kind/known-word consistency and sparse operational facts.
 *  Live metadata contents, A sparsity and synchronization remain obligations.
 */
[[nodiscard]] TcgenSparseOperationalReport check_tcgen_sparse_known_operation(
    const TcgenSparseKnownFacts& facts);
}  // namespace ptx_frontend::resolved_ir
'''


def render_sparse_source() -> str:
    """Emit immutable sparse rows and their common conditional C++ checker."""

    operations.validate_catalogue()
    shape_rows = []
    for row in operations.SPARSE_SHAPES:
        group = "One" if row.group == 1 else "Two"
        small = ", ".join(str(value) for value in
                          row.n_small + (0,) * (4 - len(row.n_small)))
        a_types = ", ".join("MatrixElementType::" + value for value in
                            row.a_types + (row.a_types[0],) *
                            (5 - len(row.a_types)))
        b_types = ", ".join("MatrixElementType::" + value for value in
                            row.b_types + (row.b_types[0],) *
                            (5 - len(row.b_types)))
        shape_rows.append(
            f'  TcgenSparseShapeRow{{TcgenMmaKind::{row.kind}, '
            f'TcgenCtaGroup::{group}, MatrixElementType::{row.d_type}, '
            f'{{{{{row.m_values[0]}, {row.m_values[1]}}}}}, '
            f'{{{{{small}}}}}, {len(row.n_small)}, {row.n_first}, '
            f'{row.n_step}, {row.n_last}, {row.k}, '
            f'{{{{{a_types}}}}}, {len(row.a_types)}, '
            f'{{{{{b_types}}}}}, {len(row.b_types)}}}')
    path_rows = [
        f"  TcgenSparsePathRow{{TcgenCtaGroup::{('One' if row.group == 1 else 'Two')}, "
        f"{row.m}, '{row.layout}', {'true' if row.half_path else 'false'}, "
        f"{{{{{', '.join(str(value) for value in row.allowed_lane_halves + (0,) * (2 - len(row.allowed_lane_halves)))}}}}}, "
        f"{len(row.allowed_lane_halves)}}}"
        for row in operations.SPARSE_PATHS
    ]
    metadata_rows = []
    for row in operations.SPARSE_METADATA_RULES:
        values = ", ".join(str(value) for value in
                           row.valid_nibbles +
                           (0,) * (7 - len(row.valid_nibbles)))
        granularity = ("OneOfTwo" if row.granularity is
                       operations.SparseMetadataGranularity.ONE_OF_TWO
                       else "TwoOfFour")
        metadata_rows.append(
            f'  TcgenSparseMetadataRule{{TcgenMmaKind::{row.kind}, '
            f'TcgenSparseGranularity::{granularity}, '
            f'{{{{{values}}}}}, {len(row.valid_nibbles)}}}')
    gates = []
    for kind, gate in operations.SPARSE_TARGET_GATES:
        gates.append(
            f'  TcgenSparseTargetGate{{TcgenMmaKind::{kind}, '
            f'"{gate.feature}", {str(gate.exact).lower()}, '
            f'{{{gate.ptx_major}, {gate.ptx_minor}}}, '
            f'{str(gate.scaled_d).lower()}}}')
    source = _SOURCE
    for old, new in (
        ("__SHAPE_COUNT__", str(len(shape_rows))),
        ("__SHAPE_ROWS__", ",\n".join(shape_rows)),
        ("__PATH_COUNT__", str(len(path_rows))),
        ("__PATH_ROWS__", ",\n".join(path_rows)),
        ("__META_COUNT__", str(len(metadata_rows))),
        ("__META_ROWS__", ",\n".join(metadata_rows)),
        ("__TARGET_COUNT__", str(len(gates))),
        ("__TARGET_ROWS__", ",\n".join(gates)),
    ):
        source = source.replace(old, new)
    return source


_SOURCE = r'''
#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace ptx_frontend::resolved_ir {
namespace {
constexpr std::array<TcgenSparseShapeRow, __SHAPE_COUNT__> kSparseShapes = {{
__SHAPE_ROWS__
}};
constexpr std::array<TcgenSparsePathRow, __PATH_COUNT__> kSparsePaths = {{
__PATH_ROWS__
}};
constexpr std::array<TcgenSparseMetadataRule, __META_COUNT__> kSparseMetadata = {{
__META_ROWS__
}};
/** One fixed target floor for a source kind and optional D-scale. */
struct TcgenSparseTargetGate {
  /** Exact source kind. */
  TcgenMmaKind kind;
  /** Target spelling or enabled family feature. */
  std::string_view feature;
  /** Whether the spelling must match exactly. */
  bool exact;
  /** Minimum fixed PTX version. */
  checker::PtxVersion minimum_ptx;
  /** Whether D-scale is present. */
  bool scaled_d;
};
constexpr std::array<TcgenSparseTargetGate, __TARGET_COUNT__> kSparseTargets = {{
__TARGET_ROWS__
}};

/** Match an independently supplied target through the accepted profile map. */
bool accepts_sparse_target(const TcgenSparseKnownFacts& facts) {
  const auto profile = base::find_target_profile(facts.target->source_spelling);
  if (!profile || profile->identity != *facts.target)
    return false;
  for (const auto& gate : kSparseTargets) {
    if (gate.kind != facts.source_kind ||
        gate.scaled_d != facts.scale_d.has_value() ||
        *facts.ptx_version < gate.minimum_ptx)
      continue;
    if (gate.exact && facts.target->source_spelling == gate.feature)
      return true;
    if (!gate.exact &&
        std::find(profile->enabled_family_features.begin(),
                  profile->enabled_family_features.end(), gate.feature) !=
            profile->enabled_family_features.end())
      return true;
  }
  return false;
}

/** Check a caller-known shared descriptor without reading source registers. */
void check_sparse_shared(bool is_a, bool transpose,
                         std::optional<MatrixElementType> type,
                         TcgenSharedWord word, TcgenSharedContext context,
                         const TcgenSparseKnownFacts& facts,
                         TcgenSparseOperationalReport& report) {
  if (context.target && facts.target && *context.target != *facts.target)
    report.violations.push_back(TcgenSparseViolation::InvalidContext);
  if (context.ptx_version && facts.ptx_version &&
      *context.ptx_version != *facts.ptx_version)
    report.violations.push_back(TcgenSparseViolation::InvalidContext);
  if (!context.target) context.target = facts.target;
  if (!context.ptx_version) context.ptx_version = facts.ptx_version;
  if (is_a) context.transpose_a = transpose;
  else context.transpose_b = transpose;
  auto fields = validate_tcgen_shared_defined_fields(word, context);
  if (is_a) {
    report.a_shared_fields = std::move(fields);
    report.checked.push_back(TcgenSparseChecked::ASharedFields);
  } else {
    report.b_shared_fields = std::move(fields);
    report.checked.push_back(TcgenSparseChecked::BSharedFields);
  }
  const bool low = facts.source_kind == TcgenMmaKind::F8F6F4 && type &&
      (*type == MatrixElementType::E2M3 ||
       *type == MatrixElementType::E3M2 ||
       *type == MatrixElementType::E2M1);
  if (!context.major)
    report.missing.push_back(is_a ? TcgenSparseObligation::AMajor
                                  : TcgenSparseObligation::BMajor);
  else if (!low || !transpose) {
    if (*context.major != (transpose ? TcgenMajor::MN : TcgenMajor::K))
      report.violations.push_back(is_a ? TcgenSparseViolation::AMajor
                                        : TcgenSparseViolation::BMajor);
  }
  const auto decoded = decode_tcgen_shared(word);
  const bool bad_swizzle = !decoded.swizzle ||
      (transpose && (facts.source_kind == TcgenMmaKind::Tf32
          ? *decoded.swizzle != TcgenSwizzle::B128Atom32
          : *decoded.swizzle == TcgenSwizzle::B128Atom32));
  if (bad_swizzle)
    report.violations.push_back(is_a ? TcgenSparseViolation::ASwizzle
                                      : TcgenSparseViolation::BSwizzle);
  if (low && transpose)
    report.missing.push_back(
        is_a ? TcgenSparseObligation::ALowBitTransposeRule
             : TcgenSparseObligation::BLowBitTransposeRule);
}
}  // namespace

std::span<const TcgenSparseShapeRow> tcgen_sparse_shape_rows() noexcept {
  return kSparseShapes;
}
std::span<const TcgenSparsePathRow> tcgen_sparse_path_rows() noexcept {
  return kSparsePaths;
}
std::span<const TcgenSparseMetadataRule>
tcgen_sparse_metadata_rules() noexcept {
  return kSparseMetadata;
}

TcgenSparseOperationalReport check_tcgen_sparse_known_operation(
    const TcgenSparseKnownFacts& facts) {
  TcgenSparseOperationalReport report;
  report.instruction_fields =
      validate_tcgen_instruction_defined_fields(facts.instruction);
  report.checked.push_back(TcgenSparseChecked::InstructionFields);
  if (facts.source_kind != TcgenMmaKind::F16 &&
      facts.source_kind != TcgenMmaKind::Tf32 &&
      facts.source_kind != TcgenMmaKind::F8F6F4 &&
      facts.source_kind != TcgenMmaKind::I8) {
    report.violations.push_back(TcgenSparseViolation::Kind);
    return report;
  }
  if (facts.instruction.kind != facts.source_kind) {
    report.violations.push_back(TcgenSparseViolation::Kind);
    return report;
  }
  if (facts.group != TcgenCtaGroup::One && facts.group != TcgenCtaGroup::Two) {
    report.violations.push_back(TcgenSparseViolation::Group);
    return report;
  }
  const auto decoded = decode_tcgen_instruction(facts.instruction);
  if (!decoded.sparse)
    report.violations.push_back(TcgenSparseViolation::SparseBit);
  report.checked.push_back(TcgenSparseChecked::Sparse);
  for (const auto& row : kSparseShapes) {
    if (row.kind != facts.source_kind || row.group != facts.group ||
        !decoded.d_type || *decoded.d_type != row.d_type ||
        (decoded.m != row.m_values[0] && decoded.m != row.m_values[1]) ||
        row.k == 0)
      continue;
    const bool small = decoded.n != 0 &&
        std::find(row.n_small.begin(),
                  row.n_small.begin() + row.n_small_count, decoded.n) !=
            row.n_small.begin() + row.n_small_count;
    const bool regular = decoded.n >= row.n_first &&
        decoded.n <= row.n_last &&
        (decoded.n - row.n_first) % row.n_step == 0;
    if (!small && !regular) continue;
    report.shape = row;
    report.compressed_a_k = row.k / 2;
    report.logical_b_k = row.k;
    break;
  }
  if (!report.shape)
    report.violations.push_back(TcgenSparseViolation::Shape);
  report.checked.push_back(TcgenSparseChecked::Shape);
  if (report.shape && decoded.a_type && decoded.b_type) {
    const auto& row = *report.shape;
    if (std::find(row.a_types.begin(),
                  row.a_types.begin() + row.a_type_count, *decoded.a_type) ==
        row.a_types.begin() + row.a_type_count)
      report.violations.push_back(TcgenSparseViolation::AType);
    if (std::find(row.b_types.begin(),
                  row.b_types.begin() + row.b_type_count, *decoded.b_type) ==
        row.b_types.begin() + row.b_type_count)
      report.violations.push_back(TcgenSparseViolation::BType);
    if (facts.source_kind == TcgenMmaKind::F16 && decoded.d_type &&
        *decoded.d_type == MatrixElementType::F32 &&
        *decoded.a_type != *decoded.b_type)
      report.missing.push_back(TcgenSparseObligation::MixedInputPair);
    report.checked.push_back(TcgenSparseChecked::Types);
  }
  if (facts.source_kind == TcgenMmaKind::F8F6F4) {
    const auto low_bit = [](std::optional<MatrixElementType> type) {
      return type && (*type == MatrixElementType::E2M1 ||
                      *type == MatrixElementType::E2M3 ||
                      *type == MatrixElementType::E3M2);
    };
    if (low_bit(decoded.a_type))
      report.missing.push_back(TcgenSparseObligation::ALowBitPackingRule);
    if (low_bit(decoded.b_type))
      report.missing.push_back(TcgenSparseObligation::BLowBitPackingRule);
  }
  if (facts.scale_d &&
      ((facts.source_kind != TcgenMmaKind::F16 &&
        facts.source_kind != TcgenMmaKind::Tf32) ||
       *facts.scale_d < 0 || *facts.scale_d > 15))
    report.violations.push_back(TcgenSparseViolation::ScaleD);
  if ((facts.source_kind == TcgenMmaKind::I8 ||
       facts.source_kind == TcgenMmaKind::F8F6F4) &&
      decoded.sparse_selector != 0)
    report.violations.push_back(TcgenSparseViolation::Selector);
  report.checked.push_back(TcgenSparseChecked::Selector);
  if (facts.source_kind == TcgenMmaKind::F8F6F4 &&
      decoded.transpose_b && decoded.b_type &&
      (*decoded.b_type == MatrixElementType::E4M3 ||
       *decoded.b_type == MatrixElementType::E5M2)) {
    const bool allowed_n = facts.group == TcgenCtaGroup::One
        ? decoded.n >= 16 && decoded.n <= 256 && decoded.n % 16 == 0
        : decoded.n >= 32 && decoded.n <= 256 && decoded.n % 32 == 0;
    if (!allowed_n)
      report.violations.push_back(TcgenSparseViolation::BTransposeN);
    report.checked.push_back(TcgenSparseChecked::BTransposeN);
  }
  for (const auto& rule : kSparseMetadata) {
    if (rule.kind != facts.source_kind) continue;
    report.metadata_rule = rule;
    if (!facts.metadata_nibbles)
      report.missing.push_back(TcgenSparseObligation::MetadataIndices);
    else {
      for (uint8_t value : *facts.metadata_nibbles)
        if (std::find(rule.valid_nibbles.begin(),
                      rule.valid_nibbles.begin() + rule.valid_count, value) ==
            rule.valid_nibbles.begin() + rule.valid_count)
          report.violations.push_back(TcgenSparseViolation::MetadataIndex);
      report.checked.push_back(TcgenSparseChecked::MetadataIndices);
    }
    break;
  }
  report.missing.push_back(TcgenSparseObligation::LiveMetadataContents);
  report.missing.push_back(TcgenSparseObligation::SparseAContents);
  if (facts.a_in_tmem) {
    if (facts.a_shared_word)
      report.violations.push_back(TcgenSparseViolation::APlacementFacts);
  } else if (!facts.a_shared_word) {
    report.missing.push_back(TcgenSparseObligation::ASharedWord);
  } else {
    check_sparse_shared(true, decoded.transpose_a, decoded.a_type,
                        *facts.a_shared_word, facts.a_context, facts, report);
  }
  if (!facts.b_shared_word)
    report.missing.push_back(TcgenSparseObligation::BSharedWord);
  else
    check_sparse_shared(false, decoded.transpose_b, decoded.b_type,
                        *facts.b_shared_word, facts.b_context, facts, report);
  for (const auto& row : kSparsePaths) {
    if (!report.shape || row.group != facts.group || row.m != decoded.m)
      continue;
    report.path_layout = row.layout;
    report.checked.push_back(TcgenSparseChecked::Datapath);
    const auto check_lane = [&report](std::optional<uint8_t> value,
                                    TcgenSparseViolation violation) {
      if (value && *value != 0 && *value != 16)
        report.violations.push_back(violation);
    };
    check_lane(facts.a_lane_half, TcgenSparseViolation::ALaneHalf);
    check_lane(facts.d_lane_half, TcgenSparseViolation::DLaneHalf);
    check_lane(facts.metadata_lane_half,
               TcgenSparseViolation::MetadataLaneHalf);
    std::optional<uint8_t> selected;
    const auto compare_lane = [&report, &selected, &row](
        std::optional<uint8_t> value, TcgenSparseObligation obligation,
        TcgenSparseViolation violation) {
      if (!value) {
        report.missing.push_back(obligation);
      } else if (*value == 0 || *value == 16) {
        if (std::find(row.allowed_lane_halves.begin(),
                      row.allowed_lane_halves.begin() + row.allowed_lane_count,
                      *value) ==
            row.allowed_lane_halves.begin() + row.allowed_lane_count) {
          report.violations.push_back(violation);
        } else if (row.half_path) {
          if (selected && *selected != *value)
            report.violations.push_back(TcgenSparseViolation::HalfAlignment);
          selected = *value;
        }
      }
    };
    if (facts.a_in_tmem)
      compare_lane(facts.a_lane_half, TcgenSparseObligation::ALaneHalf,
                   TcgenSparseViolation::ALaneHalf);
    compare_lane(facts.d_lane_half, TcgenSparseObligation::DLaneHalf,
                 TcgenSparseViolation::DLaneHalf);
    compare_lane(facts.metadata_lane_half,
                 TcgenSparseObligation::MetadataLaneHalf,
                 TcgenSparseViolation::MetadataLaneHalf);
    if (row.half_path)
      report.checked.push_back(TcgenSparseChecked::HalfAlignment);
    break;
  }
  if (!facts.target || !facts.ptx_version)
    report.missing.push_back(TcgenSparseObligation::Target);
  else {
    if (!accepts_sparse_target(facts))
      report.violations.push_back(TcgenSparseViolation::Target);
    report.checked.push_back(TcgenSparseChecked::Target);
  }
  return report;
}
}  // namespace ptx_frontend::resolved_ir
'''
