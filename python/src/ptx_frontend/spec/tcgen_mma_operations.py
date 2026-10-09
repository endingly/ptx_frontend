"""Known-value dense f16/tf32/i8/f8f6f4 TCGEN MMA rules beyond fields.

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


@dataclass(frozen=True)
class I8ShapeRow:
    """One dense i8 Table 42 row with K in elements and M/N in rows/columns.

    ``n_small`` lists exceptional N values before the inclusive regular
    ``n_first``/``n_step``/``n_last`` grid. ``a_types`` and ``b_types`` are
    independent defined domains; they do not settle mixed signedness.
    """

    group: int
    d_type: str
    m_values: tuple[int, ...]
    n_small: tuple[int, ...]
    n_first: int
    n_step: int
    n_last: int
    k: int
    a_types: tuple[str, ...]
    b_types: tuple[str, ...]

    def contains(self, m: int, n: int, k: int) -> bool:
        """Check the irregular group-one N prefix without widening its grid."""

        return (m in self.m_values and k == self.k and
                (n in self.n_small or
                 (self.n_first <= n <= self.n_last and
                  (n - self.n_first) % self.n_step == 0)))


@dataclass(frozen=True)
class I8KnownFacts(F16KnownFacts):
    """Caller-known i8 operational facts, independent of source registers."""


@dataclass(frozen=True)
class I8OperationalReport(F16OperationalReport):
    """Proven i8 violations and facts or pair rules still owed."""


@dataclass(frozen=True)
class F8F6F4ShapeRow(F16ShapeRow):
    """One closed Table 42 dense low-bit group/output-type grid."""


@dataclass(frozen=True)
class F8F6F4KnownFacts(F16KnownFacts):
    """Caller-known unscaled low-bit facts, never inferred from live words."""


@dataclass(frozen=True)
class F8F6F4OperationalReport(F16OperationalReport):
    """Separate proven violations from unverified layout/packing facts."""


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

I8_SHAPES = (
    I8ShapeRow(1, "S32", (64, 128), (8, 16, 24, 32), 48, 16, 256, 32,
               ("U8", "S8"), ("U8", "S8")),
    I8ShapeRow(2, "S32", (128, 256), (), 32, 32, 256, 32,
               ("U8", "S8"), ("U8", "S8")),
)
# Table 54 uses the same group/M-selected non-WS path as the accepted kinds.
I8_PATHS = F16_PATHS
# Exact identities only; the current target catalogue lacks historical 101a.
I8_TARGET_GATES = (
    F16TargetGate("sm_100a", True, 8, 6, False),
    F16TargetGate("sm_110a", True, 9, 0, False),
)

_F8F6F4_TYPES = ("E4M3", "E5M2", "E2M3", "E3M2", "E2M1")
_F8F6F4_LOW_TYPES = frozenset(("E2M3", "E3M2", "E2M1"))
F8F6F4_SHAPES = tuple(
    F8F6F4ShapeRow(group, d_type, m_values, first, step, 256, 32,
                    _F8F6F4_TYPES, _F8F6F4_TYPES)
    for d_type in ("F16", "F32")
    for group, m_values, first, step in (
        (1, (64, 128), 8, 8),
        (2, (128, 256), 16, 16)))
F8F6F4_PATHS = F16_PATHS
F8F6F4_TARGET_GATES = (
    F16TargetGate("sm_100a", True, 8, 6, False),
    F16TargetGate("sm_100f", False, 8, 8, False),
    F16TargetGate("sm_110a", True, 9, 0, False),
    F16TargetGate("sm_110f", False, 9, 0, False),
)

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
    i8 = next((kind for kind in descriptor.KINDS if kind.name == "I8"), None)
    if i8 is None:
        raise ValueError("accepted descriptor kind I8 is missing")
    if (len(I8_SHAPES) != 2 or
            {(row.group, row.d_type, row.m_values, row.n_small,
              row.n_first, row.n_step, row.n_last, row.k,
              row.a_types, row.b_types) for row in I8_SHAPES} != {
                  (1, "S32", (64, 128), (8, 16, 24, 32), 48, 16, 256,
                   32, ("U8", "S8"), ("U8", "S8")),
                  (2, "S32", (128, 256), (), 32, 32, 256, 32,
                   ("U8", "S8"), ("U8", "S8"))} or
            I8_PATHS != F16_PATHS or
            {(gate.feature, gate.exact, gate.ptx_major, gate.ptx_minor,
              gate.scaled_d) for gate in I8_TARGET_GATES} != {
                  ("sm_100a", True, 8, 6, False),
                  ("sm_110a", True, 9, 0, False)} or
            {name.upper() for _, name in i8.a_types} != {"U8", "S8"} or
            {name.upper() for _, name in i8.b_types} != {"U8", "S8"} or
            {name.upper() for _, name in i8.d_types} != {"S32"} or
            not i8.saturation or i8.negate):
        raise ValueError("dense i8 rows drifted from Tables 42/45 or fields")
    f8 = next((kind for kind in descriptor.KINDS if kind.name == "F8F6F4"),
              None)
    if (f8 is None or len(F8F6F4_SHAPES) != 4 or
            {(row.group, row.d_type, row.m_values, row.n_first,
              row.n_step, row.n_last, row.k) for row in F8F6F4_SHAPES} != {
                (group, dtype, ms, first, step, 256, 32)
                for dtype in ("F16", "F32")
                for group, ms, first, step in (
                    (1, (64, 128), 8, 8),
                    (2, (128, 256), 16, 16))} or
            any(row.a_types != _F8F6F4_TYPES or
                row.b_types != _F8F6F4_TYPES for row in F8F6F4_SHAPES) or
            {name.upper() for _, name in f8.a_types} != set(_F8F6F4_TYPES) or
            {name.upper() for _, name in f8.b_types} != set(_F8F6F4_TYPES) or
            {name.upper() for _, name in f8.d_types} != {"F16", "F32"} or
            f8.saturation or not f8.negate or F8F6F4_PATHS != F16_PATHS or
            {(gate.feature, gate.exact, gate.ptx_major, gate.ptx_minor,
              gate.scaled_d) for gate in F8F6F4_TARGET_GATES} != {
                  ("sm_100a", True, 8, 6, False),
                  ("sm_100f", False, 8, 8, False),
                  ("sm_110a", True, 9, 0, False),
                  ("sm_110f", False, 9, 0, False)}):
        raise ValueError("dense f8f6f4 rows drifted from Tables 42/45")


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


def check_i8_known_facts(facts: I8KnownFacts) -> I8OperationalReport:
    """Check dense i8 Table 42/55/57 facts without reading source registers."""

    violations: list[str] = []
    obligations: list[str] = []
    if facts.group is None:
        obligations.append("cta_group")
    elif facts.group not in (1, 2):
        violations.append("cta_group_invalid")
    for name in ("m", "n", "k", "d_type", "a_type", "b_type"):
        if getattr(facts, name) is None:
            obligations.append(name)
    if (facts.group in (1, 2) and facts.m is not None and
            facts.n is not None and facts.k is not None and
            facts.d_type is not None and not any(
                row.group == facts.group and row.d_type == facts.d_type and
                row.contains(facts.m, facts.n, facts.k)
                for row in I8_SHAPES)):
        violations.append("shape_or_output_type")
    if facts.d_type is not None and facts.d_type != "S32":
        violations.append("output_type")
    for role in ("a", "b"):
        value = getattr(facts, f"{role}_type")
        if value is not None and value not in ("U8", "S8"):
            violations.append(f"{role}_type")
    if (facts.a_type in ("U8", "S8") and
            facts.b_type in ("U8", "S8") and
            facts.a_type != facts.b_type):
        obligations.append("mixed_i8_signedness_pair_rule")
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
    if (facts.transpose_b and facts.group in (1, 2) and
            facts.n is not None and not (
                (facts.group == 1 and 16 <= facts.n <= 256 and
                 facts.n % 16 == 0) or
                (facts.group == 2 and 32 <= facts.n <= 256 and
                 facts.n % 32 == 0))):
        violations.append("b_transpose_n")
    path = next((row for row in I8_PATHS
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
    return I8OperationalReport(tuple(violations), tuple(obligations),
                               path.layout if path else None)


def _check_f8f6f4_shared(
        role: str, element_type: str | None, transpose: bool | None,
        facts: SharedOperandFacts | None, violations: list[str],
        obligations: list[str]) -> None:
    """Keep low-bit transpose unresolved while checking defined supplied facts."""

    if element_type not in _F8F6F4_LOW_TYPES or not transpose:
        _check_shared(role, transpose, facts, violations, obligations)
        return
    if facts is None:
        obligations.append(f"{role}_shared_word")
    else:
        if facts.major is None:
            obligations.append(f"{role}_major")
        elif facts.major not in ("K", "MN"):
            violations.append(f"{role}_major_invalid")
        if facts.swizzle is None:
            obligations.append(f"{role}_swizzle")
        elif facts.swizzle not in {name for _, name in descriptor.SWIZZLES}:
            violations.append(f"{role}_swizzle_invalid")
        elif facts.swizzle == _TRANSPOSE_EXCLUDED:
            violations.append(f"{role}_transpose_swizzle")
    obligations.append(f"{role}_transpose_layout_rule")


def check_f8f6f4_known_facts(
        facts: F8F6F4KnownFacts) -> F8F6F4OperationalReport:
    """Check caller-known ordinary low-bit facts without claiming unknown rules."""

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
                   for row in F8F6F4_SHAPES):
            violations.append("shape_or_output_type")
    if facts.d_type is not None and facts.d_type not in ("F16", "F32"):
        violations.append("output_type")
    for role in ("a", "b"):
        value = getattr(facts, f"{role}_type")
        if value is not None and value not in _F8F6F4_TYPES:
            violations.append(f"{role}_type")
        if value in _F8F6F4_LOW_TYPES:
            obligations.append(f"{role}_low_bit_packing_rule")
    if facts.sparse is None:
        obligations.append("dense_sparsity_bit")
    elif facts.sparse:
        violations.append("dense_sparse_bit")
    if facts.a_shared is None:
        obligations.append("a_placement")
    elif facts.a_shared:
        _check_f8f6f4_shared("a", facts.a_type, facts.transpose_a,
                             facts.a_shared_facts, violations, obligations)
    _check_f8f6f4_shared("b", facts.b_type, facts.transpose_b,
                         facts.b_shared_facts, violations, obligations)
    if (facts.transpose_b and facts.b_type in ("E4M3", "E5M2") and
            facts.group in (1, 2) and facts.n is not None and not (
                (facts.group == 1 and 16 <= facts.n <= 256 and
                 facts.n % 16 == 0) or
                (facts.group == 2 and 32 <= facts.n <= 256 and
                 facts.n % 32 == 0))):
        violations.append("b_transpose_n")
    path = next((row for row in F8F6F4_PATHS
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
    return F8F6F4OperationalReport(tuple(violations), tuple(obligations),
                                   path.layout if path else None)
