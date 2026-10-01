"""Known-value dense f16/tf32 TCGEN MMA rules beyond descriptor fields.

These facts apply to caller-supplied values, never to opaque live source
registers. Table 43 and Tables 45–48 remain owned by tcgen_descriptor_domains.
"""

from __future__ import annotations

from dataclasses import dataclass

from ptx_frontend.spec import tcgen_descriptor_domains as descriptor


@dataclass(frozen=True)
class F16ShapeRow:
    """One Table 42 group/output-type row with a uniquely determined K."""

    group: int
    d_type: str
    m_values: tuple[int, ...]
    n_first: int
    n_step: int
    n_last: int
    k: int
    a_types: tuple[str, ...]
    b_types: tuple[str, ...]

    def contains(self, m: int, n: int, k: int) -> bool:
        """Check known dimensions against this row without decoding a word."""

        return (m in self.m_values and self.n_first <= n <= self.n_last
                and (n - self.n_first) % self.n_step == 0 and k == self.k)


@dataclass(frozen=True)
class F16PathRow:
    """One non-WS dense f16 path from section 9.7.17.10.5."""

    group: int
    m: int
    layout: str
    half_path: bool


@dataclass(frozen=True)
class F16TargetGate:
    """One exact or catalog-family target floor for the initial source form."""

    feature: str
    exact: bool
    ptx_major: int
    ptx_minor: int
    scaled_d: bool


@dataclass(frozen=True)
class SharedOperandFacts:
    """Independently supplied major/swizzle facts for a shared A or B word."""

    major: str | None = None
    swizzle: str | None = None


@dataclass(frozen=True)
class F16KnownFacts:
    """Caller-known operational facts; none are read from an MMA source register.

    ``a_shared`` identifies source placement. Lane alignment is a known 0/16
    lane-half identity, not a byte address. A live Tensor Memory address does
    not by itself prove this alignment or an allocation history.
    """

    group: int | None = None
    m: int | None = None
    n: int | None = None
    k: int | None = None
    d_type: str | None = None
    a_type: str | None = None
    b_type: str | None = None
    sparse: bool | None = None
    transpose_a: bool | None = None
    transpose_b: bool | None = None
    a_shared: bool | None = None
    a_shared_facts: SharedOperandFacts | None = None
    b_shared_facts: SharedOperandFacts | None = None
    a_lane_half: int | None = None
    d_lane_half: int | None = None


@dataclass(frozen=True)
class F16OperationalReport:
    """Independent operational violations and facts still owed by the caller."""

    violations: tuple[str, ...]
    obligations: tuple[str, ...]
    layout: str | None

    @property
    def known_facts_ok(self) -> bool:
        """Whether supplied facts passed; obligations may still remain."""

        return not self.violations


@dataclass(frozen=True)
class Tf32ShapeRow(F16ShapeRow):
    """One dense tf32 Table 42 row with an implicit K of eight."""


@dataclass(frozen=True)
class Tf32KnownFacts(F16KnownFacts):
    """Caller-known tf32 facts, independent of opaque MMA source registers."""


@dataclass(frozen=True)
class Tf32OperationalReport(F16OperationalReport):
    """Known tf32 violations and facts still owed by the caller."""


F16_SHAPES = (
    F16ShapeRow(1, "F16", (64, 128), 8, 8, 256, 16, ("F16",), ("F16",)),
    F16ShapeRow(1, "F32", (64, 128), 8, 8, 256, 16,
                ("F16", "BF16"), ("F16", "BF16")),
    F16ShapeRow(2, "F16", (128, 256), 16, 16, 256, 16,
                ("F16",), ("F16",)),
    F16ShapeRow(2, "F32", (128, 256), 16, 16, 256, 16,
                ("F16", "BF16"), ("F16", "BF16")),
)

F16_PATHS = (
    F16PathRow(1, 64, "F", True),
    F16PathRow(1, 128, "D", False),
    F16PathRow(2, 128, "B", False),
    F16PathRow(2, 256, "A", False),
)

# Family membership is resolved by the accepted target catalogue at query
# time; these rows do not invent numeric-sm inheritance or new target names.
F16_TARGET_GATES = (
    F16TargetGate("sm_100a", True, 8, 6, False),
    F16TargetGate("sm_100f", False, 8, 8, False),
    F16TargetGate("sm_110f", False, 9, 0, False),
    F16TargetGate("sm_100a", True, 8, 6, True),
    F16TargetGate("sm_100f", False, 8, 8, True),
)

TF32_SHAPES = (
    Tf32ShapeRow(1, "F32", (64, 128), 8, 8, 256, 8,
                 ("TF32",), ("TF32",)),
    Tf32ShapeRow(2, "F32", (128, 256), 16, 16, 256, 8,
                 ("TF32",), ("TF32",)),
)
TF32_PATHS = F16_PATHS
TF32_TARGET_GATES = F16_TARGET_GATES

# Table 57 applies to each 16-bit shared operand independently. The accepted
# descriptor catalogue remains the only owner of swizzle encoded values.
_TRANSPOSE_EXCLUDED = "B128Atom32"


def validate_catalogue() -> None:
    """Reject operational rows that drift from the accepted field domains."""

    f16 = next((kind for kind in descriptor.KINDS if kind.name == "F16"), None)
    if f16 is None:
        raise ValueError("accepted descriptor kind F16 is missing")
    accepted_a = {name.upper() for _, name in f16.a_types}
    accepted_b = {name.upper() for _, name in f16.b_types}
    accepted_d = {name.upper() for _, name in f16.d_types}
    if (len(F16_SHAPES) != 4 or
            {(row.group, row.d_type) for row in F16_SHAPES}
            != {(1, "F16"), (1, "F32"), (2, "F16"), (2, "F32")} or
            any(row.group not in (1, 2) or row.k != 16 or row.n_step <= 0
                or row.n_first % row.n_step or row.n_last > 256
                or not set(row.a_types) <= accepted_a
                or not set(row.b_types) <= accepted_b
                or row.d_type not in accepted_d for row in F16_SHAPES)):
        raise ValueError("dense f16 shape/type rows drifted from Table 42")
    if {(row.group, row.m, row.layout, row.half_path)
        for row in F16_PATHS} != {
            (1, 64, "F", True), (1, 128, "D", False),
            (2, 128, "B", False), (2, 256, "A", False)}:
        raise ValueError("dense f16 datapath mapping changed")
    if _TRANSPOSE_EXCLUDED not in {name for _, name in descriptor.SWIZZLES}:
        raise ValueError("Table 57 exclusion lacks accepted swizzle identity")
    if len(F16_TARGET_GATES) != 5 or len({
        (gate.feature, gate.exact, gate.scaled_d)
        for gate in F16_TARGET_GATES}) != 5:
        raise ValueError("dense f16 target gates overlap or are missing")
    tf32 = next((kind for kind in descriptor.KINDS if kind.name == "Tf32"), None)
    if tf32 is None:
        raise ValueError("accepted descriptor kind Tf32 is missing")
    if (len(TF32_SHAPES) != 2 or
            {(row.group, row.d_type, row.m_values, row.n_first, row.n_step,
              row.n_last, row.k, row.a_types, row.b_types)
             for row in TF32_SHAPES} != {
                 (1, "F32", (64, 128), 8, 8, 256, 8, ("TF32",), ("TF32",)),
                 (2, "F32", (128, 256), 16, 16, 256, 8,
                  ("TF32",), ("TF32",))} or
            TF32_PATHS != F16_PATHS or
            TF32_TARGET_GATES != F16_TARGET_GATES or
            {name.upper() for _, name in tf32.a_types} != {"TF32"} or
            {name.upper() for _, name in tf32.b_types} != {"TF32"} or
            {name.upper() for _, name in tf32.d_types} != {"F32"}):
        raise ValueError("dense tf32 rows drifted from Table 42 or fields")


def _check_shared(role: str, transpose: bool | None,
                  facts: SharedOperandFacts | None,
                  violations: list[str], obligations: list[str],
                  *, tf32: bool = False) -> None:
    """Check the kind-specific Table 57 transpose/major/swizzle relation."""

    if transpose is None:
        obligations.append(f"{role}_transpose")
    if facts is None:
        obligations.append(f"{role}_shared_word")
        return
    if facts.major is None:
        obligations.append(f"{role}_major")
    elif facts.major not in ("K", "MN"):
        violations.append(f"{role}_major_invalid")
    elif transpose is not None and facts.major != ("MN" if transpose else "K"):
        violations.append(f"{role}_major_transpose")
    swizzles = {name for _, name in descriptor.SWIZZLES}
    if facts.swizzle is None:
        obligations.append(f"{role}_swizzle")
    elif facts.swizzle not in swizzles:
        violations.append(f"{role}_swizzle_invalid")
    elif transpose and ((facts.swizzle != _TRANSPOSE_EXCLUDED) if tf32 else
                        (facts.swizzle == _TRANSPOSE_EXCLUDED)):
        violations.append(f"{role}_transpose_swizzle")


def check_f16_known_facts(facts: F16KnownFacts) -> F16OperationalReport:
    """Check only supplied Table 42/57 and datapath facts for dense non-WS f16.

    A separate generated C++ query must call the accepted descriptor
    defined-field validators first. This pure catalogue never authenticates
    caller words against MMA register operands or proves runtime completion.
    """

    violations: list[str] = []
    obligations: list[str] = []
    if facts.group is None:
        obligations.append("cta_group")
    elif facts.group not in (1, 2):
        violations.append("cta_group_invalid")
    for name in ("m", "n", "k", "d_type", "a_type", "b_type"):
        if getattr(facts, name) is None:
            obligations.append(name)
    if (facts.group in (1, 2) and facts.m is not None
            and facts.n is not None and facts.k is not None
            and facts.d_type is not None):
        rows = [row for row in F16_SHAPES
                if row.group == facts.group and row.d_type == facts.d_type]
        if not rows or not any(row.contains(facts.m, facts.n, facts.k)
                               for row in rows):
            violations.append("shape_or_output_type")
    type_row = next((row for row in F16_SHAPES
                     if row.d_type == facts.d_type), None)
    if type_row:
        if facts.a_type is not None and facts.a_type not in type_row.a_types:
            violations.append("a_type")
        if facts.b_type is not None and facts.b_type not in type_row.b_types:
            violations.append("b_type")
        if (facts.d_type == "F32" and facts.a_type is not None
                and facts.b_type is not None
                and facts.a_type != facts.b_type
                and facts.a_type in type_row.a_types
                and facts.b_type in type_row.b_types):
            obligations.append("mixed_f16_bf16_pair_rule")
    elif facts.d_type is not None:
        violations.append("output_type")
    if facts.sparse is None:
        obligations.append("dense_sparsity_bit")
    elif facts.sparse:
        violations.append("dense_sparse_bit")
    if facts.a_shared is None:
        obligations.append("a_placement")
    elif facts.a_shared:
        _check_shared("a", facts.transpose_a, facts.a_shared_facts,
                      violations, obligations)
    _check_shared("b", facts.transpose_b, facts.b_shared_facts,
                  violations, obligations)
    path = next((row for row in F16_PATHS
                 if row.group == facts.group and row.m == facts.m), None)
    if path and path.half_path and facts.a_shared is False:
        for name in ("a_lane_half", "d_lane_half"):
            value = getattr(facts, name)
            if value is None:
                obligations.append(name)
            elif value not in (0, 16):
                violations.append(f"{name}_invalid")
        if (facts.a_lane_half in (0, 16) and facts.d_lane_half in (0, 16)
                and facts.a_lane_half != facts.d_lane_half):
            violations.append("half_path_alignment")
    return F16OperationalReport(tuple(violations), tuple(obligations),
                                path.layout if path else None)


def check_tf32_known_facts(facts: Tf32KnownFacts) -> Tf32OperationalReport:
    """Check supplied dense tf32 Table 42/57 facts without decoding live words."""

    violations: list[str] = []
    obligations: list[str] = []
    if facts.group is None:
        obligations.append("cta_group")
    elif facts.group not in (1, 2):
        violations.append("cta_group_invalid")
    for name in ("m", "n", "k", "d_type", "a_type", "b_type"):
        if getattr(facts, name) is None:
            obligations.append(name)
    if all(getattr(facts, name) is not None for name in
           ("group", "m", "n", "k", "d_type")):
        if not any(row.group == facts.group and row.d_type == facts.d_type
                   and row.contains(facts.m, facts.n, facts.k)
                   for row in TF32_SHAPES):
            violations.append("shape_or_output_type")
    if facts.d_type is not None and facts.d_type != "F32":
        violations.append("output_type")
    if facts.a_type is not None and facts.a_type != "TF32":
        violations.append("a_type")
    if facts.b_type is not None and facts.b_type != "TF32":
        violations.append("b_type")
    if facts.sparse is None:
        obligations.append("dense_sparsity_bit")
    elif facts.sparse:
        violations.append("dense_sparse_bit")
    if facts.a_shared is None:
        obligations.append("a_placement")
    elif facts.a_shared:
        _check_shared("a", facts.transpose_a, facts.a_shared_facts,
                      violations, obligations, tf32=True)
    _check_shared("b", facts.transpose_b, facts.b_shared_facts,
                  violations, obligations, tf32=True)
    path = next((row for row in TF32_PATHS
                 if row.group == facts.group and row.m == facts.m), None)
    if path and path.half_path and facts.a_shared is False:
        for name in ("a_lane_half", "d_lane_half"):
            value = getattr(facts, name)
            if value is None:
                obligations.append(name)
            elif value not in (0, 16):
                violations.append(f"{name}_invalid")
        if (facts.a_lane_half in (0, 16) and facts.d_lane_half in (0, 16)
                and facts.a_lane_half != facts.d_lane_half):
            violations.append("half_path_alignment")
    return Tf32OperationalReport(tuple(violations), tuple(obligations),
                                 path.layout if path else None)
