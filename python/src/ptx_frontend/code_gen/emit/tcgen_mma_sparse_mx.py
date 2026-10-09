"""Emit sparse block-scale MMA rows and conditional C++ operational checks."""

from ptx_frontend.spec import tcgen_mma_operations as operations


def render_sparse_mx_header() -> str:
    """Expose typed sparse MX rows and caller-known facts without live decoding."""

    return r'''
namespace ptx_frontend::resolved_ir {
/** Borrow the three sparse MX Table 42 shape grids. */
[[nodiscard]] std::span<const TcgenSparseShapeRow>
tcgen_sparse_mx_shape_rows() noexcept;
/** Borrow distinct 2:4 and pairwise 4:8 sparse MX metadata rules. */
[[nodiscard]] std::span<const TcgenSparseMetadataRule>
tcgen_sparse_mx_metadata_rules() noexcept;
/** Which supplied sparse MX rule was evaluated. */
enum class TcgenSparseMxChecked : uint8_t {
  InstructionFields, Shape, Types, Metadata, ScaleA, ScaleB,
  ASharedFields, BSharedFields, Datapath, Target
};
/** Contradiction established from independent source and known-word facts. */
enum class TcgenSparseMxViolation : uint8_t {
  Kind, Group, SparseBit, KChoice, Shape, AType, BType, Selector,
  ScaleType, ScaleAId, ScaleBId, ScaleALayout, ScaleBLayout,
  ScaleAAlignment, ScaleBAlignment, MetadataIndex,
  ALaneHalf, DLaneHalf, MetadataLaneHalf, APlacementFacts,
  AMajor, BMajor, ASwizzle, BSwizzle, BTransposeN,
  APackingFact, BPackingFact, Target, InvalidContext
};
/** Unknown caller fact or runtime content that a source cannot certify. */
enum class TcgenSparseMxObligation : uint8_t {
  ASharedWord, BSharedWord, AMajor, BMajor, MetadataIndices,
  LiveMetadataContents, SparseAContents, ALaneHalf, DLaneHalf,
  MetadataLaneHalf, ScaleALayout, ScaleBLayout, APackingFact,
  BPackingFact, ALivePackingContents, BLivePackingContents,
  ALowBitTransposeRule, BLowBitTransposeRule, Target
};
/** Known sparse MX facts; all word values are independent of live registers. */
struct TcgenSparseMxKnownFacts {
  /** Exact source kind to compare with the supplied descriptor kind. */
  TcgenMmaKind source_kind;
  /** Written CTA group. */
  TcgenCtaGroup group;
  /** Whether A uses Tensor Memory instead of a shared descriptor. */
  bool a_in_tmem;
  /** Written selector; Absent retains omitted-source provenance. */
  TcgenScaleVectorSize scale_selector;
  /** Independently known Table 46/47 instruction word. */
  TcgenInstructionWord instruction;
  /** Optional caller-known shared A word. */
  std::optional<TcgenSharedWord> a_shared_word;
  /** Optional caller-known shared B word. */
  std::optional<TcgenSharedWord> b_shared_word;
  /** Shared A decoder context, when a word is supplied. */
  TcgenSharedContext a_context;
  /** Shared B decoder context, when a word is supplied. */
  TcgenSharedContext b_context;
  /** Known A lane-half identity, 0 or 16, only for Tensor Memory A. */
  std::optional<uint8_t> a_lane_half;
  /** Known D lane-half identity, 0 or 16. */
  std::optional<uint8_t> d_lane_half;
  /** Known sparse metadata lane-half identity, 0 or 16. */
  std::optional<uint8_t> metadata_lane_half;
  /** Individually claimed metadata nibble indices, not live contents. */
  std::optional<std::vector<uint8_t>> metadata_nibbles;
  /** Caller claim about A scale layout and guaranteed alignment. */
  std::optional<TcgenMxScaleRoleFacts> scale_a_facts;
  /** Caller claim about B scale layout and guaranteed alignment. */
  std::optional<TcgenMxScaleRoleFacts> scale_b_facts;
  /** Claimed low-bit A packing identity. */
  std::optional<TcgenMxInputPacking> a_packing;
  /** Claimed low-bit B packing identity. */
  std::optional<TcgenMxInputPacking> b_packing;
  /** Catalogued target identity, when known. */
  std::optional<base::TargetIdentity> target;
  /** Explicit PTX version, when known. */
  std::optional<checker::PtxVersion> ptx_version;
};
/** Independent defined-field and operational results for sparse MX. */
struct TcgenSparseMxOperationalReport {
  /** Accepted Table 46/47 defined-field result. */
  TcgenDefinedFieldReport instruction_fields;
  /** Table 43 A result only when its independent word is supplied. */
  std::optional<TcgenDefinedFieldReport> a_shared_fields;
  /** Table 43 B result only when its independent word is supplied. */
  std::optional<TcgenDefinedFieldReport> b_shared_fields;
  /** Evaluated checks. */
  std::vector<TcgenSparseMxChecked> checked;
  /** Proven contradictions. */
  std::vector<TcgenSparseMxViolation> violations;
  /** Absent facts, unproved layouts and live-content obligations. */
  std::vector<TcgenSparseMxObligation> missing;
  /** Selected sparse shape row, if the known word has a legal shape. */
  std::optional<TcgenSparseShapeRow> shape;
  /** Selected metadata rule, if source kind is supported. */
  std::optional<TcgenSparseMetadataRule> metadata_rule;
  /** Selected scale-A row from the shared typed catalogue. */
  std::optional<TcgenMxScaleLayoutRow> scale_a_layout;
  /** Selected scale-B row from the shared typed catalogue. */
  std::optional<TcgenMxScaleLayoutRow> scale_b_layout;
  /** Selected A factor count, measured in physical scale factors. */
  std::optional<uint8_t> scale_a_factor_count;
  /** Selected B factor count, measured in physical scale factors. */
  std::optional<uint8_t> scale_b_factor_count;
  /** Known A packing rule where type and placement determine it. */
  std::optional<TcgenMxInputPacking> required_a_packing;
  /** Known B packing rule where type determines it. */
  std::optional<TcgenMxInputPacking> required_b_packing;
  /** Selected non-WS datapath identity, if a supported shape exists. */
  std::optional<char> path_layout;
  /** Whether every supplied evaluated fact passed, excluding missing facts. */
  [[nodiscard]] bool supplied_facts_ok() const noexcept {
    return instruction_fields.defined_fields_ok() &&
           (!a_shared_fields || a_shared_fields->defined_fields_ok()) &&
           (!b_shared_fields || b_shared_fields->defined_fields_ok()) &&
           violations.empty();
  }
};
/** Check independent sparse MX words without reading opaque source registers. */
[[nodiscard]] TcgenSparseMxOperationalReport
check_tcgen_sparse_mx_known_operation(const TcgenSparseMxKnownFacts& facts);
}  // namespace ptx_frontend::resolved_ir
'''


def render_sparse_mx_source() -> str:
    """Generate typed shape, metadata and target rows from one data owner."""

    operations.validate_catalogue()
    shape_rows = []
    for row in operations.SPARSE_MX_SHAPES:
        group = 'One' if row.group == 1 else 'Two'
        types = ', '.join('MatrixElementType::' + value for value in
                          row.a_types + (row.a_types[0],) *
                          (5 - len(row.a_types)))
        shape_rows.append(
            f'TcgenSparseShapeRow{{TcgenMmaKind::{row.kind}, '
            f'TcgenCtaGroup::{group}, MatrixElementType::F32, '
            f'{{{{{row.m_values[0]}, {row.m_values[1]}}}}}, '
            '{{0,0,0,0}}, 0, '
            f'{row.n_first}, {row.n_step}, {row.n_last}, {row.k}, '
            f'{{{{{types}}}}}, {len(row.a_types)}, '
            f'{{{{{types}}}}}, {len(row.b_types)}}}')
    metadata_rows = []
    for row in operations.SPARSE_MX_METADATA_RULES:
        name = ('TwoOfFour' if row.granularity is
                operations.SparseMxMetadataGranularity.TWO_OF_FOUR else
                'PairwiseFourOfEight')
        values = ', '.join(map(str, row.valid_nibbles))
        metadata_rows.append(
            f'TcgenSparseMetadataRule{{TcgenMmaKind::{row.kind}, '
            f'TcgenSparseGranularity::{name}, {{{{{values}}}}}, '
            f'{len(row.valid_nibbles)}}}')

    def target_rows(rows: tuple) -> str:
        """Encode exact/family target rows without duplicating their domain."""

        return ',\n'.join(
            f'SparseMxTargetGate{{TcgenMmaKind::{kind}, "{gate.feature}", '
            f'{str(gate.exact).lower()}, '
            f'{{{gate.ptx_major}, {gate.ptx_minor}}}}}'
            for kind, gate in rows)

    result = _SOURCE
    for key, value in (
        ('__SHAPE_COUNT__', str(len(shape_rows))),
        ('__SHAPE_ROWS__', ',\n'.join(shape_rows)),
        ('__META_COUNT__', str(len(metadata_rows))),
        ('__META_ROWS__', ',\n'.join(metadata_rows)),
        ('__BASE_COUNT__', str(len(operations.SPARSE_MX_BASE_GATES))),
        ('__BASE_ROWS__', target_rows(operations.SPARSE_MX_BASE_GATES)),
        ('__VEC_COUNT__', str(len(operations.SPARSE_MX_VEC_GATES))),
        ('__VEC_ROWS__', target_rows(operations.SPARSE_MX_VEC_GATES)),
        ('__BLOCK_COUNT__', str(len(operations.SPARSE_MX_BLOCK_GATES))),
        ('__BLOCK_ROWS__', target_rows(operations.SPARSE_MX_BLOCK_GATES)),
    ):
        result = result.replace(key, value)
    return result


_SOURCE = r'''
#include <algorithm>
#include <array>

namespace ptx_frontend::resolved_ir {
namespace {
constexpr std::array<TcgenSparseShapeRow, __SHAPE_COUNT__> kSparseMxShapes = {{
__SHAPE_ROWS__
}};
constexpr std::array<TcgenSparseMetadataRule, __META_COUNT__> kSparseMxMetadata = {{
__META_ROWS__
}};
/** One source-kind target floor selected independently of a scale suffix. */
struct SparseMxTargetGate {
  /** Descriptor/source kind. */
  TcgenMmaKind kind;
  /** Exact spelling or family feature. */
  std::string_view feature;
  /** Require an exact target spelling when true. */
  bool exact;
  /** Minimum fixed PTX version. */
  checker::PtxVersion minimum_ptx;
};
constexpr std::array<SparseMxTargetGate, __BASE_COUNT__> kSparseMxBase = {{
__BASE_ROWS__
}};
constexpr std::array<SparseMxTargetGate, __VEC_COUNT__> kSparseMxVec = {{
__VEC_ROWS__
}};
constexpr std::array<SparseMxTargetGate, __BLOCK_COUNT__> kSparseMxBlock = {{
__BLOCK_ROWS__
}};

/** Check one table of target gates against a catalogued profile. */
template <std::size_t N>
bool sparse_mx_target_match(const std::array<SparseMxTargetGate, N>& gates,
                            const TcgenSparseMxKnownFacts& facts,
                            const base::TargetProfile& profile) {
  for (const auto& gate : gates) {
    if (gate.kind != facts.source_kind ||
        *facts.ptx_version < gate.minimum_ptx)
      continue;
    if (gate.exact && facts.target->source_spelling == gate.feature)
      return true;
    if (!gate.exact &&
        std::find(profile.enabled_family_features.begin(),
                  profile.enabled_family_features.end(), gate.feature) !=
            profile.enabled_family_features.end())
      return true;
  }
  return false;
}

/** Intersect the base kind gate with the separately written selector gate. */
bool sparse_mx_target_accepts(const TcgenSparseMxKnownFacts& facts) {
  const auto profile = base::find_target_profile(facts.target->source_spelling);
  if (!profile || profile->identity != *facts.target ||
      !sparse_mx_target_match(kSparseMxBase, facts, *profile))
    return false;
  switch (facts.scale_selector) {
    case TcgenScaleVectorSize::Absent: return facts.source_kind != TcgenMmaKind::MxF4NvF4;
    case TcgenScaleVectorSize::Vec1X:
    case TcgenScaleVectorSize::Vec2X:
    case TcgenScaleVectorSize::Vec4X:
      return sparse_mx_target_match(kSparseMxVec, facts, *profile);
    case TcgenScaleVectorSize::Block32:
    case TcgenScaleVectorSize::Block16:
      return sparse_mx_target_match(kSparseMxBlock, facts, *profile);
  }
  return false;
}

/** Validate one independently supplied shared word and Table 57 claims. */
void check_sparse_mx_shared(bool is_a, bool transpose,
                            std::optional<MatrixElementType> type,
                            TcgenSharedWord word, TcgenSharedContext context,
                            const TcgenSparseMxKnownFacts& facts,
                            TcgenSparseMxOperationalReport& report) {
  if (context.target && facts.target && *context.target != *facts.target)
    report.violations.push_back(TcgenSparseMxViolation::InvalidContext);
  if (context.ptx_version && facts.ptx_version &&
      *context.ptx_version != *facts.ptx_version)
    report.violations.push_back(TcgenSparseMxViolation::InvalidContext);
  if (!context.target) context.target = facts.target;
  if (!context.ptx_version) context.ptx_version = facts.ptx_version;
  if (is_a) context.transpose_a = transpose;
  else context.transpose_b = transpose;
  auto fields = validate_tcgen_shared_defined_fields(word, context);
  if (is_a) {
    report.a_shared_fields = std::move(fields);
    report.checked.push_back(TcgenSparseMxChecked::ASharedFields);
  } else {
    report.b_shared_fields = std::move(fields);
    report.checked.push_back(TcgenSparseMxChecked::BSharedFields);
  }
  const bool low = facts.source_kind == TcgenMmaKind::MxF8F6F4 && type &&
      (*type == MatrixElementType::E2M1 || *type == MatrixElementType::E2M3 ||
       *type == MatrixElementType::E3M2);
  if (!context.major)
    report.missing.push_back(is_a ? TcgenSparseMxObligation::AMajor
                                  : TcgenSparseMxObligation::BMajor);
  else if ((!low || !transpose) &&
           *context.major != (transpose ? TcgenMajor::MN : TcgenMajor::K))
    report.violations.push_back(is_a ? TcgenSparseMxViolation::AMajor
                                      : TcgenSparseMxViolation::BMajor);
  const auto decoded = decode_tcgen_shared(word);
  if (!decoded.swizzle || (transpose &&
      *decoded.swizzle == TcgenSwizzle::B128Atom32))
    report.violations.push_back(is_a ? TcgenSparseMxViolation::ASwizzle
                                      : TcgenSparseMxViolation::BSwizzle);
}
}  // namespace

std::span<const TcgenSparseShapeRow> tcgen_sparse_mx_shape_rows() noexcept {
  return kSparseMxShapes;
}
std::span<const TcgenSparseMetadataRule>
tcgen_sparse_mx_metadata_rules() noexcept { return kSparseMxMetadata; }

TcgenSparseMxOperationalReport check_tcgen_sparse_mx_known_operation(
    const TcgenSparseMxKnownFacts& facts) {
  TcgenSparseMxOperationalReport report;
  report.instruction_fields = validate_tcgen_instruction_defined_fields(facts.instruction);
  report.checked.push_back(TcgenSparseMxChecked::InstructionFields);
  if (facts.source_kind != TcgenMmaKind::MxF8F6F4 &&
      facts.source_kind != TcgenMmaKind::MxF4 &&
      facts.source_kind != TcgenMmaKind::MxF4NvF4) {
    report.violations.push_back(TcgenSparseMxViolation::Kind);
    return report;
  }
  if (facts.instruction.kind != facts.source_kind) {
    report.violations.push_back(TcgenSparseMxViolation::Kind);
    return report;
  }
  if (facts.group != TcgenCtaGroup::One && facts.group != TcgenCtaGroup::Two) {
    report.violations.push_back(TcgenSparseMxViolation::Group);
    return report;
  }
  const auto decoded = decode_tcgen_instruction(facts.instruction);
  if (!decoded.sparse)
    report.violations.push_back(TcgenSparseMxViolation::SparseBit);
  if (facts.source_kind != TcgenMmaKind::MxF8F6F4 && decoded.k_choice_code)
    report.violations.push_back(TcgenSparseMxViolation::KChoice);
  for (const auto& row : kSparseMxShapes) {
    if (row.kind != facts.source_kind || row.group != facts.group ||
        (decoded.m != row.m_values[0] && decoded.m != row.m_values[1]) ||
        decoded.n < row.n_first || decoded.n > row.n_last ||
        (decoded.n - row.n_first) % row.n_step)
      continue;
    report.shape = row;
    break;
  }
  if (!report.shape) report.violations.push_back(TcgenSparseMxViolation::Shape);
  report.checked.push_back(TcgenSparseMxChecked::Shape);
  if (report.shape && decoded.a_type && decoded.b_type) {
    const auto& row = *report.shape;
    if (std::find(row.a_types.begin(), row.a_types.begin()+row.a_type_count,
                  *decoded.a_type) == row.a_types.begin()+row.a_type_count)
      report.violations.push_back(TcgenSparseMxViolation::AType);
    if (std::find(row.b_types.begin(), row.b_types.begin()+row.b_type_count,
                  *decoded.b_type) == row.b_types.begin()+row.b_type_count)
      report.violations.push_back(TcgenSparseMxViolation::BType);
    report.checked.push_back(TcgenSparseMxChecked::Types);
  }
  for (const auto& row : kSparseMxMetadata) {
    if (row.kind != facts.source_kind) continue;
    report.metadata_rule = row;
    if (!facts.metadata_nibbles)
      report.missing.push_back(TcgenSparseMxObligation::MetadataIndices);
    else {
      for (uint8_t value : *facts.metadata_nibbles)
        if (std::find(row.valid_nibbles.begin(),
                      row.valid_nibbles.begin()+row.valid_count, value) ==
            row.valid_nibbles.begin()+row.valid_count)
          report.violations.push_back(TcgenSparseMxViolation::MetadataIndex);
      report.checked.push_back(TcgenSparseMxChecked::Metadata);
    }
    break;
  }
  report.missing.push_back(TcgenSparseMxObligation::LiveMetadataContents);
  report.missing.push_back(TcgenSparseMxObligation::SparseAContents);
  if (facts.a_in_tmem) {
    if (facts.a_shared_word)
      report.violations.push_back(TcgenSparseMxViolation::APlacementFacts);
  } else if (!facts.a_shared_word)
    report.missing.push_back(TcgenSparseMxObligation::ASharedWord);
  else
    check_sparse_mx_shared(true, decoded.transpose_a, decoded.a_type,
                           *facts.a_shared_word, facts.a_context, facts, report);
  if (!facts.b_shared_word)
    report.missing.push_back(TcgenSparseMxObligation::BSharedWord);
  else
    check_sparse_mx_shared(false, decoded.transpose_b, decoded.b_type,
                           *facts.b_shared_word, facts.b_context, facts, report);
  if (facts.source_kind == TcgenMmaKind::MxF8F6F4 && decoded.transpose_b &&
      decoded.b_type && (*decoded.b_type == MatrixElementType::E4M3 ||
                         *decoded.b_type == MatrixElementType::E5M2)) {
    const bool valid_n = facts.group == TcgenCtaGroup::One
        ? decoded.n >= 16 && decoded.n % 16 == 0
        : decoded.n >= 32 && decoded.n % 32 == 0;
    if (!valid_n) report.violations.push_back(TcgenSparseMxViolation::BTransposeN);
  }
  const auto low_transpose = [](std::optional<MatrixElementType> type) {
    return type && (*type == MatrixElementType::E2M1 ||
                    *type == MatrixElementType::E2M3 ||
                    *type == MatrixElementType::E3M2);
  };
  if (facts.source_kind == TcgenMmaKind::MxF8F6F4) {
    if (!facts.a_in_tmem && decoded.transpose_a && low_transpose(decoded.a_type))
      report.missing.push_back(TcgenSparseMxObligation::ALowBitTransposeRule);
    if (decoded.transpose_b && low_transpose(decoded.b_type))
      report.missing.push_back(TcgenSparseMxObligation::BLowBitTransposeRule);
  }
  for (const auto& row : tcgen_sparse_path_rows()) {
    if (!report.shape || row.group != facts.group || row.m != decoded.m)
      continue;
    report.path_layout = row.layout;
    report.checked.push_back(TcgenSparseMxChecked::Datapath);
    const auto check_lane = [&report, &row](std::optional<uint8_t> value,
        TcgenSparseMxObligation obligation, TcgenSparseMxViolation violation) {
      if (!value) report.missing.push_back(obligation);
      else if (std::find(row.allowed_lane_halves.begin(),
                         row.allowed_lane_halves.begin()+row.allowed_lane_count,
                         *value) ==
               row.allowed_lane_halves.begin()+row.allowed_lane_count)
        report.violations.push_back(violation);
    };
    if (facts.a_in_tmem)
      check_lane(facts.a_lane_half, TcgenSparseMxObligation::ALaneHalf,
                 TcgenSparseMxViolation::ALaneHalf);
    check_lane(facts.d_lane_half, TcgenSparseMxObligation::DLaneHalf,
               TcgenSparseMxViolation::DLaneHalf);
    check_lane(facts.metadata_lane_half,
               TcgenSparseMxObligation::MetadataLaneHalf,
               TcgenSparseMxViolation::MetadataLaneHalf);
    break;
  }
  TcgenScaleVectorSize effective = facts.scale_selector;
  if (effective == TcgenScaleVectorSize::Absent) {
    if (facts.source_kind == TcgenMmaKind::MxF8F6F4)
      effective = TcgenScaleVectorSize::Vec1X;
    else if (facts.source_kind == TcgenMmaKind::MxF4)
      effective = TcgenScaleVectorSize::Block32;
    else
      report.violations.push_back(TcgenSparseMxViolation::Selector);
  }
  const bool selector_valid =
      (facts.source_kind == TcgenMmaKind::MxF8F6F4 &&
       (effective == TcgenScaleVectorSize::Vec1X ||
        effective == TcgenScaleVectorSize::Block32)) ||
      (facts.source_kind == TcgenMmaKind::MxF4 &&
       (effective == TcgenScaleVectorSize::Vec2X ||
        effective == TcgenScaleVectorSize::Block32)) ||
      (facts.source_kind == TcgenMmaKind::MxF4NvF4 &&
       (effective == TcgenScaleVectorSize::Vec2X ||
        effective == TcgenScaleVectorSize::Vec4X ||
        effective == TcgenScaleVectorSize::Block32 ||
        effective == TcgenScaleVectorSize::Block16));
  if (!selector_valid) report.violations.push_back(TcgenSparseMxViolation::Selector);
  const bool four_factor = effective == TcgenScaleVectorSize::Vec4X ||
                           effective == TcgenScaleVectorSize::Block16;
  if (!decoded.scale_type ||
      (*decoded.scale_type != MatrixScaleType::UE8M0 &&
       !(facts.source_kind == TcgenMmaKind::MxF4NvF4 && four_factor &&
         *decoded.scale_type == MatrixScaleType::UE4M3)))
    report.violations.push_back(TcgenSparseMxViolation::ScaleType);
  const auto low = [](std::optional<MatrixElementType> type) {
    return type && (*type == MatrixElementType::E2M1 ||
                    *type == MatrixElementType::E2M3 ||
                    *type == MatrixElementType::E3M2);
  };
  if (facts.source_kind == TcgenMmaKind::MxF8F6F4) {
    if (low(decoded.a_type))
      report.required_a_packing = facts.a_in_tmem
          ? TcgenMxInputPacking::TmemEightBitContainer
          : (*decoded.a_type == MatrixElementType::E2M1
             ? TcgenMxInputPacking::SharedPaddedFourBit
             : TcgenMxInputPacking::SharedPaddedSixBit);
    if (low(decoded.b_type))
      report.required_b_packing = *decoded.b_type == MatrixElementType::E2M1
          ? TcgenMxInputPacking::SharedPaddedFourBit
          : TcgenMxInputPacking::SharedPaddedSixBit;
  } else {
    if (decoded.a_type && *decoded.a_type == MatrixElementType::E2M1)
      report.required_a_packing = facts.a_in_tmem
          ? TcgenMxInputPacking::TmemPairedFourBit
          : TcgenMxInputPacking::SharedPairedFourBit;
    if (decoded.b_type && *decoded.b_type == MatrixElementType::E2M1)
      report.required_b_packing = TcgenMxInputPacking::SharedPairedFourBit;
  }
  const auto check_packing = [&report](
      std::optional<TcgenMxInputPacking> required,
      std::optional<TcgenMxInputPacking> supplied, bool type_known, bool is_a) {
    const auto violation = is_a ? TcgenSparseMxViolation::APackingFact
                                : TcgenSparseMxViolation::BPackingFact;
    const auto missing = is_a ? TcgenSparseMxObligation::APackingFact
                              : TcgenSparseMxObligation::BPackingFact;
    if (supplied && *supplied != TcgenMxInputPacking::TmemEightBitContainer &&
        *supplied != TcgenMxInputPacking::SharedPaddedFourBit &&
        *supplied != TcgenMxInputPacking::SharedPaddedSixBit &&
        *supplied != TcgenMxInputPacking::TmemPairedFourBit &&
        *supplied != TcgenMxInputPacking::SharedPairedFourBit) {
      report.violations.push_back(violation);
      return;
    }
    if (required) {
      if (!supplied) report.missing.push_back(missing);
      else if (*supplied != *required) report.violations.push_back(violation);
      report.missing.push_back(is_a
          ? TcgenSparseMxObligation::ALivePackingContents
          : TcgenSparseMxObligation::BLivePackingContents);
    } else if (supplied && type_known) report.violations.push_back(violation);
  };
  check_packing(report.required_a_packing, facts.a_packing,
                decoded.a_type.has_value(), true);
  check_packing(report.required_b_packing, facts.b_packing,
                decoded.b_type.has_value(), false);
  for (const auto& row : tcgen_mx_scale_layout_rows()) {
    if (!row.sparse || row.kind != facts.source_kind ||
        !report.shape || row.k != report.shape->k ||
        row.selector != effective) continue;
    const bool is_a = row.role == MatrixFragmentRole::A;
    const auto id = is_a ? decoded.scale_a_code : decoded.scale_b_code;
    const auto violation_id = is_a ? TcgenSparseMxViolation::ScaleAId
                                   : TcgenSparseMxViolation::ScaleBId;
    const auto violation_layout = is_a ? TcgenSparseMxViolation::ScaleALayout
                                       : TcgenSparseMxViolation::ScaleBLayout;
    const auto violation_align = is_a ? TcgenSparseMxViolation::ScaleAAlignment
                                      : TcgenSparseMxViolation::ScaleBAlignment;
    const auto missing_layout = is_a ? TcgenSparseMxObligation::ScaleALayout
                                     : TcgenSparseMxObligation::ScaleBLayout;
    if (std::find(row.valid_ids.begin(),
                  row.valid_ids.begin()+row.valid_id_count, id) ==
        row.valid_ids.begin()+row.valid_id_count)
      report.violations.push_back(violation_id);
    const auto& known = is_a ? facts.scale_a_facts : facts.scale_b_facts;
    if (is_a) {
      report.scale_a_layout = row;
      report.scale_a_factor_count = row.factor_count;
    } else {
      report.scale_b_layout = row;
      report.scale_b_factor_count = row.factor_count;
    }
    if (!known) report.missing.push_back(missing_layout);
    else {
      if (known->layout_id && *known->layout_id != row.layout_id)
        report.violations.push_back(violation_layout);
      const auto alignment = known->subcolumn_alignment_bytes;
      if (alignment && (*alignment < row.subcolumn_alignment_bytes ||
          (*alignment & (*alignment-1)) != 0 ||
          *alignment % row.subcolumn_alignment_bytes != 0 ||
          (row.id_alignment_policy ==
               TcgenScaleIdAlignmentPolicy::ByteSlotOffset &&
           id % *alignment != 0)))
        report.violations.push_back(violation_align);
      if (!known->layout_id || !alignment)
        report.missing.push_back(missing_layout);
    }
    report.checked.push_back(is_a ? TcgenSparseMxChecked::ScaleA
                                  : TcgenSparseMxChecked::ScaleB);
  }
  if (!facts.target || !facts.ptx_version)
    report.missing.push_back(TcgenSparseMxObligation::Target);
  else {
    if (!sparse_mx_target_accepts(facts))
      report.violations.push_back(TcgenSparseMxViolation::Target);
    report.checked.push_back(TcgenSparseMxChecked::Target);
  }
  return report;
}
}  // namespace ptx_frontend::resolved_ir
'''
