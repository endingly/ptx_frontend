"""Emit typed A-collector conditional checks from the shared source domain."""


def render_collector_header() -> str:
    """Expose independent collector facts without a sequence-history claim."""

    return r'''
namespace ptx_frontend::resolved_ir {
/** Independently supplied A-collector and ashift facts. */
struct TcgenACollectorKnownFacts {
  /** Written qualifier, preserving the all-Unspecified omitted source. */
  TcgenCollectorControl collector;
  /** Whether the optional ashift token was written. */
  bool ashift = false;
  /** Known A placement, true for Tensor Memory, if supplied. */
  std::optional<bool> a_in_tmem;
  /** Known M from an independently supplied descriptor word, in rows. */
  std::optional<uint16_t> m;
  /** Caller assertion of prior A-buffer validity, not a sequence proof. */
  std::optional<bool> collector_a_valid;
};
/** Contradictions established without reading live descriptor or buffer state. */
enum class TcgenACollectorViolation : uint8_t {
  CollectorDomain, AshiftAPlacement, AshiftM, AshiftCollector, History
};
/** Unknown facts and persistent live-state duties. */
enum class TcgenACollectorObligation : uint8_t {
  APlacement, AshiftM, History, CollectorSequence, SourceStabilityUntilCompletion
};
/** Conditional report preserving source and effective collector separately. */
struct TcgenACollectorReport {
  /** Omitted source derives A/discard only for non-WS operation semantics. */
  TcgenCollectorControl effective;
  /** Contradictory supplied values. */
  std::vector<TcgenACollectorViolation> violations;
  /** Absent facts and unproved temporal conditions. */
  std::vector<TcgenACollectorObligation> missing;
  /** Whether all supplied checkable facts passed. */
  [[nodiscard]] bool supplied_facts_ok() const noexcept {
    return violations.empty();
  }
};
/** Check a non-WS A collector without asserting runtime history. */
[[nodiscard]] TcgenACollectorReport check_tcgen_a_collector_known_facts(
    const TcgenACollectorKnownFacts& facts);
}  // namespace ptx_frontend::resolved_ir
'''


def render_collector_source() -> str:
    """Emit the typed conditional checks for A collector and ashift."""

    return r'''
namespace ptx_frontend::resolved_ir {
namespace {
/** Validate both collector fields as one closed source-domain pair. */
bool valid_a_collector(TcgenCollectorControl control) noexcept {
  if (control.buffer == TcgenCollectorBuffer::Unspecified ||
      control.operation == TcgenCollectorOp::Unspecified)
    return control.buffer == TcgenCollectorBuffer::Unspecified &&
           control.operation == TcgenCollectorOp::Unspecified;
  return control.buffer == TcgenCollectorBuffer::A &&
         (control.operation == TcgenCollectorOp::Fill ||
          control.operation == TcgenCollectorOp::Use ||
          control.operation == TcgenCollectorOp::LastUse ||
          control.operation == TcgenCollectorOp::Discard);
}
}  // namespace

TcgenACollectorReport check_tcgen_a_collector_known_facts(
    const TcgenACollectorKnownFacts& facts) {
  TcgenACollectorReport report;
  report.missing.push_back(
      TcgenACollectorObligation::SourceStabilityUntilCompletion);
  if (!valid_a_collector(facts.collector)) {
    report.violations.push_back(TcgenACollectorViolation::CollectorDomain);
    return report;
  }
  report.effective = facts.collector.is_present()
      ? facts.collector
      : TcgenCollectorControl{TcgenCollectorBuffer::A,
                              TcgenCollectorOp::Discard};
  if (facts.ashift) {
    if (!facts.a_in_tmem)
      report.missing.push_back(TcgenACollectorObligation::APlacement);
    else if (!*facts.a_in_tmem)
      report.violations.push_back(TcgenACollectorViolation::AshiftAPlacement);
    if (!facts.m)
      report.missing.push_back(TcgenACollectorObligation::AshiftM);
    else if (*facts.m != 128 && *facts.m != 256)
      report.violations.push_back(TcgenACollectorViolation::AshiftM);
    if (facts.collector.operation == TcgenCollectorOp::Fill ||
        facts.collector.operation == TcgenCollectorOp::Use)
      report.violations.push_back(TcgenACollectorViolation::AshiftCollector);
  }
  if (facts.collector.operation == TcgenCollectorOp::Use ||
      facts.collector.operation == TcgenCollectorOp::LastUse) {
    if (!facts.collector_a_valid)
      report.missing.push_back(TcgenACollectorObligation::History);
    else if (!*facts.collector_a_valid)
      report.violations.push_back(TcgenACollectorViolation::History);
    report.missing.push_back(TcgenACollectorObligation::CollectorSequence);
  }
  return report;
}
}  // namespace ptx_frontend::resolved_ir
'''
