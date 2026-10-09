"""Render dense weight-stationary MMA facts from the shared operations data."""

from __future__ import annotations

from ptx_frontend.spec import tcgen_mma_operations as operations


def render_ws_header() -> str:
    """Declare typed WS shapes and independent zero-column/history reports."""

    return r'''
namespace ptx_frontend::resolved_ir {
/** One Table 42 WS kind/output row with exact M, N and K sets. */
struct TcgenWsShapeRow {
  /** Kind from the selected source class. */
  TcgenMmaKind kind;
  /** Whether this row compresses sparse A. */
  bool sparse;
  /** Defined output element type. */
  MatrixElementType d_type;
  /** Allowed M values in rows. */
  std::array<uint16_t, 3> m_values;
  /** Allowed N values in columns. */
  std::array<uint16_t, 3> n_values;
  /** Number of defined N entries. */
  uint8_t n_count;
  /** Fixed K for this kind, in elements. */
  uint16_t k;
  /** Permitted A types, padded after a_type_count. */
  std::array<MatrixElementType, 5> a_types;
  /** Number of defined A types. */
  uint8_t a_type_count;
  /** Permitted B types, padded after b_type_count. */
  std::array<MatrixElementType, 5> b_types;
  /** Number of defined B types. */
  uint8_t b_type_count;
};
/** Borrow the fixed WS shape rows from the operations catalogue. */
[[nodiscard]] std::span<const TcgenWsShapeRow> tcgen_ws_shape_rows() noexcept;
/** Borrow M32/G, M64/E and M128/D through the common lane-row shape. */
[[nodiscard]] std::span<const TcgenSparsePathRow> tcgen_ws_path_rows() noexcept;
/** Known WS contradictions from supplied words and independent facts. */
enum class TcgenWsViolation : uint8_t {
  Kind, Group, SparseBit, SparseSelector, MetadataIndex, DenseMetadataFacts,
  Shape, AType, BType, APlacementFacts, ASharedFields, BSharedFields,
  AMajor, BMajor, ASwizzle, BSwizzle, BTransposeN, ALaneHalf, DLaneHalf,
  MetadataLaneHalf,
  ZeroPresence, ZeroWord, CollectorDomain, CollectorHistory, Target,
  InvalidContext
};
/** Missing facts or live-state conditions that source syntax cannot prove. */
enum class TcgenWsObligation : uint8_t {
  ASharedWord, BSharedWord, AMajor, BMajor, MixedInputPair,
  ALowBitTransposeRule, BLowBitTransposeRule,
  ALowBitPackingRule, BLowBitPackingRule, ALaneHalf, DLaneHalf,
  MetadataLaneHalf, MetadataIndices, LiveMetadataContents, SparseAContents,
  MetadataLayoutRule,
  ZeroColumnWord, LiveZeroColumnWord, CollectorHistory, CollectorSequence,
  SourceStabilityUntilCompletion, Target
};
/** Caller-known WS words and B-buffer assertions, never read from source regs. */
struct TcgenWsKnownFacts {
  /** Exact WS kind selected by source spelling. */
  TcgenMmaKind source_kind;
  /** Written sparse source identity, independent of descriptor bits. */
  bool source_sparse = false;
  /** Source qualifier, required to name CTA group one. */
  TcgenCtaGroup group;
  /** True when A is a Tensor Memory address. */
  bool a_in_tmem;
  /** Independently known instruction-descriptor word. */
  TcgenInstructionWord instruction;
  /** Optional independently known Table 43 A word. */
  std::optional<TcgenSharedWord> a_shared_word;
  /** Optional independently known Table 43 B word. */
  std::optional<TcgenSharedWord> b_shared_word;
  /** Independent shared A interpretation context. */
  TcgenSharedContext a_context;
  /** Independent shared B interpretation context. */
  TcgenSharedContext b_context;
  /** Known A lane-half identity, applicable only to Tensor Memory A. */
  std::optional<uint8_t> a_lane_half;
  /** Known D lane-half identity. */
  std::optional<uint8_t> d_lane_half;
  /** Known metadata lane-half identity on sparse WS forms. */
  std::optional<uint8_t> metadata_lane_half;
  /** Independently supplied Table 45 sparsity selector. */
  std::optional<uint8_t> sparse_selector;
  /** Independently supplied logical metadata nibbles. */
  std::optional<std::vector<uint8_t>> metadata_nibbles;
  /** Whether the source has its optional final scalar register. */
  bool zero_column_operand_present = false;
  /** Optional known word; this never authenticates that register's live bits. */
  std::optional<TcgenZeroColumnWord> zero_column_word;
  /** Written B-buffer control, preserving canonical omission. */
  TcgenCollectorControl collector;
  /** Independent prior-validity assertions indexed B0 through B3. */
  std::array<std::optional<bool>, 4> collector_b_valid;
  /** Catalogued target identity, if independently known. */
  std::optional<base::TargetIdentity> target;
  /** PTX version, if independently known. */
  std::optional<checker::PtxVersion> ptx_version;
};
/** Defined-field reports and conditional operational WS conclusions. */
struct TcgenWsOperationalReport {
  /** Table 45 defined-field checks of supplied instruction bits. */
  TcgenDefinedFieldReport instruction_fields;
  /** Table 43 checks when a known shared A word was supplied. */
  std::optional<TcgenDefinedFieldReport> a_shared_fields;
  /** Table 43 checks when a known shared B word was supplied. */
  std::optional<TcgenDefinedFieldReport> b_shared_fields;
  /** Table 48 checks only when a source operand and known word were supplied. */
  std::optional<TcgenDefinedFieldReport> zero_fields;
  /** Contradictions from independently supplied facts. */
  std::vector<TcgenWsViolation> violations;
  /** Missing facts and live-state obligations. */
  std::vector<TcgenWsObligation> missing;
  /** Selected fixed WS shape, when known instruction fields permit it. */
  std::optional<TcgenWsShapeRow> shape;
  /** Selected common allowed-lane row, when M is a WS path. */
  std::optional<TcgenSparsePathRow> path;
  /** Existing kind-specific nibble rule on sparse WS forms. */
  std::optional<TcgenSparseMetadataRule> metadata_rule;
  /** Sparse compressed A K, when a known row was selected. */
  std::optional<uint16_t> compressed_a_k;
  /** Logical B K, when a known row was selected. */
  std::optional<uint16_t> logical_b_k;
  /** Omission derives B0/discard without changing the owned source. */
  TcgenCollectorControl effective_collector;
  /** Whether no supplied checkable fact contradicted a fixed rule. */
  [[nodiscard]] bool supplied_facts_ok() const noexcept {
    return violations.empty() && instruction_fields.defined_fields_ok() &&
           (!a_shared_fields || a_shared_fields->defined_fields_ok()) &&
           (!b_shared_fields || b_shared_fields->defined_fields_ok()) &&
           (!zero_fields || zero_fields->defined_fields_ok());
  }
};
/** Check known WS bits, Table 48, lanes, target and selected B history. */
[[nodiscard]] TcgenWsOperationalReport check_tcgen_ws_known_operation(
    const TcgenWsKnownFacts& facts);
}  // namespace ptx_frontend::resolved_ir
'''


_SOURCE = r'''
namespace ptx_frontend::resolved_ir {
namespace {
constexpr std::array<TcgenWsShapeRow, __SHAPE_COUNT__> kWsShapes = {{
__SHAPE_ROWS__
}};
constexpr std::array<TcgenSparsePathRow, __PATH_COUNT__> kWsPaths = {{
__PATH_ROWS__
}};
/** A target floor retained from the corresponding non-WS kind catalogue. */
struct TcgenWsTargetGate {
  TcgenMmaKind kind;
  std::string_view feature;
  bool exact;
  checker::PtxVersion minimum_ptx;
};
constexpr std::array<TcgenWsTargetGate, __TARGET_COUNT__> kWsTargets = {{
__TARGET_ROWS__
}};

/** Match exact and enabled-family target features without numeric guessing. */
bool accepts_ws_target(const TcgenWsKnownFacts& facts) {
  const auto profile = base::find_target_profile(facts.target->source_spelling);
  if (!profile || profile->identity != *facts.target) return false;
  for (const auto& gate : kWsTargets) {
    if (gate.kind != facts.source_kind ||
        *facts.ptx_version < gate.minimum_ptx) continue;
    if (gate.exact && facts.target->source_spelling == gate.feature) return true;
    if (!gate.exact && std::find(profile->enabled_family_features.begin(),
                                profile->enabled_family_features.end(),
                                gate.feature) !=
                           profile->enabled_family_features.end()) return true;
  }
  return false;
}

/** Reject mixed sentinels, invalid enums and activation-buffer spellings. */
bool valid_ws_collector(TcgenCollectorControl value) noexcept {
  if (value.buffer == TcgenCollectorBuffer::Unspecified ||
      value.operation == TcgenCollectorOp::Unspecified)
    return value.buffer == TcgenCollectorBuffer::Unspecified &&
           value.operation == TcgenCollectorOp::Unspecified;
  const bool buffer = value.buffer == TcgenCollectorBuffer::B0 ||
                      value.buffer == TcgenCollectorBuffer::B1 ||
                      value.buffer == TcgenCollectorBuffer::B2 ||
                      value.buffer == TcgenCollectorBuffer::B3;
  const bool action = value.operation == TcgenCollectorOp::Fill ||
                      value.operation == TcgenCollectorOp::Use ||
                      value.operation == TcgenCollectorOp::LastUse ||
                      value.operation == TcgenCollectorOp::Discard;
  return buffer && action;
}

/** Decode a valid B-buffer identity to its independent history slot. */
std::optional<size_t> ws_buffer_index(TcgenCollectorBuffer value) noexcept {
  switch (value) {
    case TcgenCollectorBuffer::B0: return 0;
    case TcgenCollectorBuffer::B1: return 1;
    case TcgenCollectorBuffer::B2: return 2;
    case TcgenCollectorBuffer::B3: return 3;
    default: return std::nullopt;
  }
}

/** Apply existing Table 43 fields and Table 54/55/57 role rules. */
void check_ws_shared(bool is_a, TcgenSharedWord word,
                     TcgenSharedContext context,
                     const TcgenInstructionFields& decoded,
                     const TcgenWsKnownFacts& facts,
                     TcgenWsOperationalReport& report) {
  if (context.target && facts.target && *context.target != *facts.target)
    report.violations.push_back(TcgenWsViolation::InvalidContext);
  if (context.ptx_version && facts.ptx_version &&
      *context.ptx_version != *facts.ptx_version)
    report.violations.push_back(TcgenWsViolation::InvalidContext);
  if (!context.target) context.target = facts.target;
  if (!context.ptx_version) context.ptx_version = facts.ptx_version;
  const bool transpose = is_a ? decoded.transpose_a : decoded.transpose_b;
  if (is_a) context.transpose_a = transpose;
  else context.transpose_b = transpose;
  auto fields = validate_tcgen_shared_defined_fields(word, context);
  if (!fields.defined_fields_ok())
    report.violations.push_back(is_a ? TcgenWsViolation::ASharedFields
                                     : TcgenWsViolation::BSharedFields);
  if (is_a) report.a_shared_fields = std::move(fields);
  else report.b_shared_fields = std::move(fields);
  const auto type = is_a ? decoded.a_type : decoded.b_type;
  const bool low = facts.source_kind == TcgenMmaKind::F8F6F4 && type &&
      (*type == MatrixElementType::E2M1 || *type == MatrixElementType::E2M3 ||
       *type == MatrixElementType::E3M2);
  if (!context.major)
    report.missing.push_back(is_a ? TcgenWsObligation::AMajor
                                  : TcgenWsObligation::BMajor);
  else if ((!low || !transpose) &&
           *context.major != (transpose ? TcgenMajor::MN : TcgenMajor::K))
    report.violations.push_back(is_a ? TcgenWsViolation::AMajor
                                     : TcgenWsViolation::BMajor);
  if (low && transpose)
    report.missing.push_back(is_a ? TcgenWsObligation::ALowBitTransposeRule
                                  : TcgenWsObligation::BLowBitTransposeRule);
  const auto shared = decode_tcgen_shared(word);
  const bool bad_swizzle = !shared.swizzle ||
      (transpose && (facts.source_kind == TcgenMmaKind::Tf32
          ? *shared.swizzle != TcgenSwizzle::B128Atom32
          : *shared.swizzle == TcgenSwizzle::B128Atom32));
  if (bad_swizzle)
    report.violations.push_back(is_a ? TcgenWsViolation::ASwizzle
                                     : TcgenWsViolation::BSwizzle);
}
}  // namespace

std::span<const TcgenWsShapeRow> tcgen_ws_shape_rows() noexcept {
  return kWsShapes;
}
std::span<const TcgenSparsePathRow> tcgen_ws_path_rows() noexcept {
  return kWsPaths;
}
TcgenWsOperationalReport check_tcgen_ws_known_operation(
    const TcgenWsKnownFacts& facts) {
  TcgenWsOperationalReport report;
  report.missing.push_back(TcgenWsObligation::SourceStabilityUntilCompletion);
  report.instruction_fields =
      validate_tcgen_instruction_defined_fields(facts.instruction);
  if (facts.source_kind != TcgenMmaKind::F16 &&
      facts.source_kind != TcgenMmaKind::Tf32 &&
      facts.source_kind != TcgenMmaKind::F8F6F4 &&
      facts.source_kind != TcgenMmaKind::I8) {
    report.violations.push_back(TcgenWsViolation::Kind);
    return report;
  }
  if (facts.instruction.kind != facts.source_kind) {
    report.violations.push_back(TcgenWsViolation::Kind);
    return report;
  }
  if (facts.group != TcgenCtaGroup::One)
    report.violations.push_back(TcgenWsViolation::Group);
  const auto decoded = decode_tcgen_instruction(facts.instruction);
  if (decoded.sparse != facts.source_sparse)
    report.violations.push_back(TcgenWsViolation::SparseBit);
  for (const auto& row : kWsShapes) {
    if (row.kind != facts.source_kind || row.sparse != facts.source_sparse ||
        !decoded.d_type ||
        *decoded.d_type != row.d_type ||
        std::find(row.m_values.begin(), row.m_values.end(), decoded.m) ==
            row.m_values.end() ||
        std::find(row.n_values.begin(), row.n_values.begin()+row.n_count,
                  decoded.n) == row.n_values.begin()+row.n_count) continue;
    report.shape = row;
    break;
  }
  if (!report.shape)
    report.violations.push_back(TcgenWsViolation::Shape);
  else {
    const auto& row = *report.shape;
    report.logical_b_k = row.k;
    if (facts.source_sparse) report.compressed_a_k = row.k / 2;
    if (!decoded.a_type ||
        std::find(row.a_types.begin(), row.a_types.begin()+row.a_type_count,
                  *decoded.a_type) == row.a_types.begin()+row.a_type_count)
      report.violations.push_back(TcgenWsViolation::AType);
    if (!decoded.b_type ||
        std::find(row.b_types.begin(), row.b_types.begin()+row.b_type_count,
                  *decoded.b_type) == row.b_types.begin()+row.b_type_count)
      report.violations.push_back(TcgenWsViolation::BType);
    if (facts.source_kind == TcgenMmaKind::F16 &&
        decoded.d_type == MatrixElementType::F32 &&
        decoded.a_type && decoded.b_type &&
        *decoded.a_type != *decoded.b_type)
      report.missing.push_back(TcgenWsObligation::MixedInputPair);
    for (const auto& path : kWsPaths)
      if (decoded.m == path.m) { report.path = path; break; }
  }
  if (facts.source_kind == TcgenMmaKind::F8F6F4) {
    const auto low = [](std::optional<MatrixElementType> type) {
      return type && (*type == MatrixElementType::E2M1 ||
                      *type == MatrixElementType::E2M3 ||
                      *type == MatrixElementType::E3M2);
    };
    if (low(decoded.a_type))
      report.missing.push_back(TcgenWsObligation::ALowBitPackingRule);
    if (low(decoded.b_type))
      report.missing.push_back(TcgenWsObligation::BLowBitPackingRule);
    if (decoded.transpose_b && decoded.b_type &&
        (*decoded.b_type == MatrixElementType::E4M3 ||
         *decoded.b_type == MatrixElementType::E5M2) &&
        (decoded.n < 16 || decoded.n > 256 || decoded.n % 16 != 0))
      report.violations.push_back(TcgenWsViolation::BTransposeN);
  }
  if (facts.source_sparse) {
    if ((facts.source_kind == TcgenMmaKind::I8 ||
         facts.source_kind == TcgenMmaKind::F8F6F4) &&
        decoded.sparse_selector != 0)
      report.violations.push_back(TcgenWsViolation::SparseSelector);
    if (facts.sparse_selector && *facts.sparse_selector != decoded.sparse_selector)
      report.violations.push_back(TcgenWsViolation::SparseSelector);
    for (const auto& rule : tcgen_sparse_metadata_rules()) {
      if (rule.kind != facts.source_kind) continue;
      report.metadata_rule = rule;
      if (!facts.metadata_nibbles)
        report.missing.push_back(TcgenWsObligation::MetadataIndices);
      else for (uint8_t nibble : *facts.metadata_nibbles)
        if (std::find(rule.valid_nibbles.begin(),
                      rule.valid_nibbles.begin()+rule.valid_count, nibble) ==
            rule.valid_nibbles.begin()+rule.valid_count)
          report.violations.push_back(TcgenWsViolation::MetadataIndex);
      break;
    }
    report.missing.push_back(TcgenWsObligation::LiveMetadataContents);
    report.missing.push_back(TcgenWsObligation::SparseAContents);
    if (decoded.m == 32)
      report.missing.push_back(TcgenWsObligation::MetadataLayoutRule);
  } else if (facts.sparse_selector || facts.metadata_nibbles ||
             facts.metadata_lane_half)
    report.violations.push_back(TcgenWsViolation::DenseMetadataFacts);
  if (facts.a_in_tmem) {
    if (facts.a_shared_word)
      report.violations.push_back(TcgenWsViolation::APlacementFacts);
  } else {
    if (!facts.a_shared_word)
      report.missing.push_back(TcgenWsObligation::ASharedWord);
    else check_ws_shared(true, *facts.a_shared_word, facts.a_context,
                         decoded, facts, report);
  }
  if (!facts.b_shared_word)
    report.missing.push_back(TcgenWsObligation::BSharedWord);
  else check_ws_shared(false, *facts.b_shared_word, facts.b_context,
                       decoded, facts, report);
  if (report.path) {
    if (facts.a_in_tmem) {
      if (!facts.a_lane_half)
        report.missing.push_back(TcgenWsObligation::ALaneHalf);
      else if (*facts.a_lane_half != 0)
        report.violations.push_back(TcgenWsViolation::ALaneHalf);
    }
    if (!facts.d_lane_half)
      report.missing.push_back(TcgenWsObligation::DLaneHalf);
    else if (*facts.d_lane_half != 0)
      report.violations.push_back(TcgenWsViolation::DLaneHalf);
    if (facts.source_sparse) {
      if (!facts.metadata_lane_half)
        report.missing.push_back(TcgenWsObligation::MetadataLaneHalf);
      else if (*facts.metadata_lane_half != 0)
        report.violations.push_back(TcgenWsViolation::MetadataLaneHalf);
    }
  }
  if (facts.zero_column_operand_present) {
    if (!facts.zero_column_word)
      report.missing.push_back(TcgenWsObligation::ZeroColumnWord);
    else {
      report.zero_fields = validate_tcgen_zero_defined_fields(
          *facts.zero_column_word, {decoded.m, decoded.n});
      if (!report.zero_fields->defined_fields_ok())
        report.violations.push_back(TcgenWsViolation::ZeroWord);
      report.missing.push_back(TcgenWsObligation::LiveZeroColumnWord);
    }
  } else if (facts.zero_column_word)
    report.violations.push_back(TcgenWsViolation::ZeroPresence);
  if (!valid_ws_collector(facts.collector))
    report.violations.push_back(TcgenWsViolation::CollectorDomain);
  report.effective_collector = valid_ws_collector(facts.collector) &&
      facts.collector.is_present()
      ? facts.collector
      : TcgenCollectorControl{TcgenCollectorBuffer::B0,
                              TcgenCollectorOp::Discard};
  if (valid_ws_collector(facts.collector) &&
      (facts.collector.operation == TcgenCollectorOp::Use ||
       facts.collector.operation == TcgenCollectorOp::LastUse)) {
    const auto index = ws_buffer_index(facts.collector.buffer);
    if (index) {
      if (!facts.collector_b_valid[*index])
        report.missing.push_back(TcgenWsObligation::CollectorHistory);
      else if (!*facts.collector_b_valid[*index])
        report.violations.push_back(TcgenWsViolation::CollectorHistory);
      report.missing.push_back(TcgenWsObligation::CollectorSequence);
    }
  }
  if (!facts.target || !facts.ptx_version)
    report.missing.push_back(TcgenWsObligation::Target);
  else if (!accepts_ws_target(facts))
    report.violations.push_back(TcgenWsViolation::Target);
  return report;
}
}  // namespace ptx_frontend::resolved_ir
'''


def render_ws_source() -> str:
    """Emit C++ WS rows and checks from the sole Python operation catalogue."""

    operations.validate_catalogue()
    shapes=[]
    for row in operations.WS_SHAPES:
        a_types=", ".join("MatrixElementType::"+value for value in
                          row.a_types + (row.a_types[0],)*(5-len(row.a_types)))
        b_types=", ".join("MatrixElementType::"+value for value in
                          row.b_types + (row.b_types[0],)*(5-len(row.b_types)))
        n_values=", ".join(map(str, row.n_values +
                               (row.n_values[-1],)*(3-len(row.n_values))))
        shapes.append(
            f'  TcgenWsShapeRow{{TcgenMmaKind::{row.kind}, '
            f'{"true" if row.sparse else "false"}, '
            f'MatrixElementType::{row.d_type}, '
            f'{{{{{", ".join(map(str,row.m_values))}}}}}, '
            f'{{{{{n_values}}}}}, '
            f'{len(row.n_values)}, {row.k}, '
            f'{{{{{a_types}}}}}, {len(row.a_types)}, '
            f'{{{{{b_types}}}}}, {len(row.b_types)}}}')
    paths=[f'  TcgenSparsePathRow{{TcgenCtaGroup::One, {row.m}, '
           f"'{row.layout}', false, {{{{0, 0}}}}, 1}}"
           for row in operations.WS_PATHS]
    targets=[]
    for kind,gates in (("F16",operations.F16_TARGET_GATES),
                       ("Tf32",operations.TF32_TARGET_GATES),
                       ("F8F6F4",operations.F8F6F4_TARGET_GATES),
                       ("I8",operations.I8_TARGET_GATES)):
        for gate in gates:
            if gate.scaled_d: continue
            targets.append(f'  TcgenWsTargetGate{{TcgenMmaKind::{kind}, '
                           f'"{gate.feature}", '
                           f'{"true" if gate.exact else "false"}, '
                           f'{{{gate.ptx_major}, {gate.ptx_minor}}}}}')
    out=_SOURCE
    for old,new in (("__SHAPE_COUNT__",str(len(shapes))),
                    ("__SHAPE_ROWS__",",\n".join(shapes)),
                    ("__PATH_COUNT__",str(len(paths))),
                    ("__PATH_ROWS__",",\n".join(paths)),
                    ("__TARGET_COUNT__",str(len(targets))),
                    ("__TARGET_ROWS__",",\n".join(targets))):
        out=out.replace(old,new)
    return out
