"""Fixed PTX 9.3 TCGEN descriptor fields and conditional layout facts.

The words described here are caller-supplied values. They are independent of
the opaque register operands that may hold descriptor values at execution.
"""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Field:
    """One encoded field, with an optional mandated fixed value."""

    name: str
    first: int
    width: int
    fixed: int | None = None

    @property
    def mask(self) -> int:
        """Return this field's mask in its containing word."""

        return ((1 << self.width) - 1) << self.first


@dataclass(frozen=True)
class Word:
    """Complete finite field partition of one descriptor word."""

    name: str
    width: int
    fields: tuple[Field, ...]
    unclassified: int = 0

    def field(self, name: str) -> Field:
        """Find a named field; catalogue miss is a source error."""

        return next(field for field in self.fields if field.name == name)

    @property
    def fixed_mask(self) -> int:
        """Mask every bit with a mandated encoded value."""

        return sum(field.mask for field in self.fields if field.fixed is not None)

    @property
    def reserved_mask(self) -> int:
        """Mask reserved-zero fields without conflating other fixed codes."""

        return sum(field.mask for field in self.fields
                   if field.name.startswith("reserved_"))


@dataclass(frozen=True)
class InstructionKind:
    """Intrinsic encoded domains for one explicit, non-encoded MMA kind."""

    name: str
    table: str
    a_types: tuple[tuple[int, str], ...]
    b_types: tuple[tuple[int, str], ...]
    d_types: tuple[tuple[int, str], ...] = ()
    saturation: bool = False
    negate: bool = True
    transpose: bool = True
    scale_ids: tuple[int, ...] = ()
    scale_types: tuple[tuple[int, str], ...] = ()


@dataclass(frozen=True)
class LayoutTerm:
    """A symbolic coefficient times T, m, k, LBO, SBO, or one."""

    coefficient: int
    symbol: str = "one"


@dataclass(frozen=True)
class RelativeLayout:
    """One documented relative-leading major/swizzle layout row."""

    major: str
    swizzle: str
    shape: tuple[tuple[LayoutTerm, ...], tuple[LayoutTerm, ...]]
    stride: tuple[tuple[LayoutTerm, ...], tuple[LayoutTerm, ...]]
    swizzle_tuple: tuple[int, int, int]


def _terms(*items: str) -> tuple[LayoutTerm, ...]:
    """Parse the closed symbolic factors used by the eight normative rows."""

    parsed: list[LayoutTerm] = []
    for item in items:
        if item == "1":
            parsed.append(LayoutTerm(1))
        elif item.isdecimal():
            parsed.append(LayoutTerm(int(item)))
        elif item.startswith("2") and item[1:] in {"T", "k"}:
            parsed.append(LayoutTerm(2, item[1:]))
        elif item.startswith("4") and item[1:] == "T":
            parsed.append(LayoutTerm(4, "T"))
        elif item.startswith("8") and item[1:] == "T":
            parsed.append(LayoutTerm(8, "T"))
        elif item in {"T", "m", "k", "LBO", "SBO"}:
            parsed.append(LayoutTerm(1, item))
        else:
            raise ValueError(f"unknown TCGEN layout factor: {item}")
    return tuple(parsed)


SHARED = Word("shared", 64, (
    Field("start", 0, 14), Field("reserved_14", 14, 2, 0),
    Field("leading", 16, 14), Field("reserved_30", 30, 2, 0),
    Field("stride", 32, 14), Field("fixed_code", 46, 3, 1),
    Field("base", 49, 3), Field("absolute", 52, 1),
    Field("reserved_53", 53, 8, 0), Field("swizzle", 61, 3),
))

INSTRUCTION_45 = Word("instruction_45", 32, (
    Field("sparse_selector", 0, 2), Field("sparse", 2, 1),
    Field("saturate", 3, 1), Field("d_type", 4, 2),
    Field("reserved_6", 6, 1, 0), Field("a_type", 7, 3),
    Field("b_type", 10, 3), Field("negate_a", 13, 1),
    Field("negate_b", 14, 1), Field("transpose_a", 15, 1),
    Field("transpose_b", 16, 1), Field("n", 17, 6),
    Field("reserved_23", 23, 1, 0), Field("m", 24, 5),
    Field("reserved_29", 29, 1, 0), Field("reuse", 30, 2),
))

INSTRUCTION_46 = Word("instruction_46", 32, (
    Field("reserved_0", 0, 2, 0), Field("sparse", 2, 1),
    Field("reserved_3", 3, 1, 0), Field("scale_b", 4, 2),
    Field("reserved_6", 6, 1, 0), Field("a_type", 7, 3),
    Field("b_type", 10, 3), Field("negate_a", 13, 1),
    Field("negate_b", 14, 1), Field("transpose_a", 15, 1),
    Field("transpose_b", 16, 1), Field("n", 17, 6),
    Field("scale_type", 23, 1, 1), Field("reserved_24", 24, 3, 0),
    Field("m", 27, 2), Field("scale_a", 29, 2),
    Field("reserved_31", 31, 1, 0),
))

INSTRUCTION_47 = Word("instruction_47", 32, (
    Field("reserved_0", 0, 2, 0), Field("sparse", 2, 1),
    Field("reserved_3", 3, 1, 0), Field("scale_b", 4, 2),
    Field("reserved_6", 6, 1, 0), Field("a_type", 7, 3, 1),
    Field("b_type", 10, 2, 1), Field("reserved_12", 12, 1, 0),
    Field("negate_a", 13, 1), Field("negate_b", 14, 1),
    Field("transpose_a", 15, 1, 0), Field("transpose_b", 16, 1, 0),
    Field("n", 17, 6), Field("scale_type", 23, 1),
    Field("reserved_24", 24, 3, 0), Field("m", 27, 2),
    Field("scale_a", 29, 2), Field("k_choice", 31, 1),
))

ZERO_COLUMN = Word("zero_column", 64, (
    Field("sc0", 0, 8), Field("sc1", 8, 8),
    Field("sc2", 16, 8), Field("sc3", 24, 8),
    Field("fs0", 32, 1), Field("fs1", 33, 1),
    Field("fs2", 34, 1), Field("fs3", 35, 1),
    Field("reserved_36", 36, 3, 0), Field("generate_mask", 39, 1),
    Field("skip_span", 40, 8), Field("use_span", 48, 8),
    Field("shift", 56, 6),
), unclassified=3 << 62)

WORDS = (SHARED, INSTRUCTION_45, INSTRUCTION_46,
         INSTRUCTION_47, ZERO_COLUMN)

# Codes are encoded values, not a runtime source-register value inference.
SWIZZLES = ((0, "None"), (1, "B128Atom32"), (2, "B128Atom16"),
            (4, "B64"), (6, "B32"))
REUSE_SHIFTS = ((0, 0), (1, 8), (2, 16), (3, 32))
ORDINARY_PATTERN_BYTES = ((2, 1024), (4, 512), (6, 256))
ABSOLUTE_TARGET = ("sm_103a", 8, 8)
ZERO_PARTITIONS = ((128, 1), (64, 2), (32, 4))
SPECIAL_ATOM = ("MN", 1, 8, 4)

_F8 = ((0, "E4M3"), (1, "E5M2"), (3, "E2M3"),
       (4, "E3M2"), (5, "E2M1"))
KINDS = (
    InstructionKind("Tf32", "instruction_45", ((2, "TF32"),),
                    ((2, "TF32"),), ((1, "F32"),)),
    InstructionKind("F16", "instruction_45", ((0, "F16"), (1, "BF16")),
                    ((0, "F16"), (1, "BF16")), ((0, "F16"), (1, "F32"))),
    InstructionKind("F8F6F4", "instruction_45", _F8, _F8,
                    ((0, "F16"), (1, "F32"))),
    InstructionKind("I8", "instruction_45", ((0, "U8"), (1, "S8")),
                    ((0, "U8"), (1, "S8")), ((2, "S32"),),
                    saturation=True, negate=False),
    InstructionKind("MxF8F6F4", "instruction_46", _F8, _F8,
                    scale_ids=(0, 1, 2, 3), scale_types=((1, "UE8M0"),)),
    InstructionKind("MxF4", "instruction_47", ((1, "E2M1"),),
                    ((1, "E2M1"),), transpose=False,
                    scale_ids=(0, 2), scale_types=((1, "UE8M0"),)),
    InstructionKind("MxF4NvF4", "instruction_47", ((1, "E2M1"),),
                    ((1, "E2M1"),), transpose=False,
                    scale_ids=(0, 2),
                    scale_types=((0, "UE4M3"), (1, "UE8M0"))),
)

RELATIVE_LAYOUTS = (
    RelativeLayout("MN", "None", (_terms("T", "1", "m"), _terms("8", "k")),
                   (_terms("1", "T", "SBO"), _terms("T", "LBO")), (0, 4, 3)),
    RelativeLayout("MN", "B32", (_terms("T", "2", "m"), _terms("8", "k")),
                   (_terms("1", "T", "LBO"), _terms("2T", "SBO")), (1, 4, 3)),
    RelativeLayout("MN", "B64", (_terms("T", "4", "m"), _terms("8", "k")),
                   (_terms("1", "T", "LBO"), _terms("4T", "SBO")), (2, 4, 3)),
    RelativeLayout("MN", "B128Atom16", (_terms("T", "8", "m"), _terms("8", "k")),
                   (_terms("1", "T", "LBO"), _terms("8T", "SBO")), (3, 4, 3)),
    RelativeLayout("K", "None", (_terms("8", "m"), _terms("T", "2k")),
                   (_terms("T", "SBO"), _terms("1", "LBO")), (0, 4, 3)),
    RelativeLayout("K", "B32", (_terms("8", "m"), _terms("T", "2k")),
                   (_terms("2T", "SBO"), _terms("1", "T")), (1, 4, 3)),
    RelativeLayout("K", "B64", (_terms("8", "m"), _terms("T", "2k")),
                   (_terms("4T", "SBO"), _terms("1", "T")), (2, 4, 3)),
    RelativeLayout("K", "B128Atom16", (_terms("8", "m"), _terms("T", "2k")),
                   (_terms("8T", "SBO"), _terms("1", "T")), (3, 4, 3)),
)


def validate_catalogue() -> None:
    """Reject incomplete or internally overlapping fixed-table source data."""

    for word in WORDS:
        used = word.unclassified
        if word.unclassified & ~((1 << word.width) - 1):
            raise ValueError(f"{word.name}: unclassified bit exceeds word")
        names: set[str] = set()
        for field in word.fields:
            if (field.name in names or field.first < 0 or field.width < 1
                    or field.first + field.width > word.width
                    or used & field.mask
                    or (field.fixed is not None
                        and not 0 <= field.fixed < (1 << field.width))):
                raise ValueError(f"{word.name}: invalid or overlapping {field.name}")
            names.add(field.name)
            used |= field.mask
        if used != (1 << word.width) - 1:
            raise ValueError(f"{word.name}: field partition has a gap")
    if len({kind.name for kind in KINDS}) != 7:
        raise ValueError("TCGEN instruction kind set is not closed")
    words = {word.name for word in WORDS}
    for kind in KINDS:
        if kind.table not in words:
            raise ValueError(f"{kind.name}: missing word table")
        for codes in (kind.a_types, kind.b_types, kind.d_types,
                      kind.scale_types):
            if len({code for code, _ in codes}) != len(codes):
                raise ValueError(f"{kind.name}: duplicate encoded code")
    if len({code for code, _ in SWIZZLES}) != 5:
        raise ValueError("TCGEN swizzle codes overlap")
    if len({(row.major, row.swizzle) for row in RELATIVE_LAYOUTS}) != 8:
        raise ValueError("TCGEN relative layout rows overlap or are missing")
    valid_symbols = {"one", "T", "m", "k", "LBO", "SBO"}
    for row in RELATIVE_LAYOUTS:
        for group in (*row.shape, *row.stride):
            if not 1 <= len(group) <= 3:
                raise ValueError("TCGEN layout factor count is outside its row")
            if any(term.coefficient < 1 or term.symbol not in valid_symbols
                   for term in group):
                raise ValueError("TCGEN layout row has invalid symbolic factor")


validate_catalogue()
