"""Typed catalogue projections for fixed TCGEN descriptor declarations and tables."""

from dataclasses import dataclass

from ptx_frontend.spec import tcgen_descriptor_domains as domain


@dataclass(frozen=True)
class WordModel:
    """Selected fields and precomputed masks, in catalogue table order."""

    fields: tuple[domain.Field, ...]
    fixed_mask: int
    reserved_mask: int
    unclassified: int


@dataclass(frozen=True)
class CodeModel:
    """An encoded numeric value with an optional closed enum identity."""

    code: int
    name: str = ""


@dataclass(frozen=True)
class KindArrayModel:
    """One kind-owned backing array with its selected C++ element category."""

    suffix: str
    cpp_type: str
    element_type: str
    values: tuple[CodeModel, ...]


@dataclass(frozen=True)
class KindModel:
    """One explicit kind's selected table, arrays, and supported operations."""

    name: str
    table_index: int
    arrays: tuple[KindArrayModel, ...]
    saturation: bool
    negate: bool
    transpose: bool


@dataclass(frozen=True)
class LayoutModel:
    """Symbolic layout terms whose names already use C++ enum identities."""

    major: str
    swizzle: str
    shape: tuple[tuple[domain.LayoutTerm, ...], ...]
    stride: tuple[tuple[domain.LayoutTerm, ...], ...]
    swizzle_tuple: tuple[int, int, int]


@dataclass(frozen=True)
class TcgenDescriptorHeaderModel:
    """Closed enum members selected in stable catalogue order."""

    kinds: tuple[str, ...]
    swizzle_names: tuple[str, ...]


@dataclass(frozen=True)
class TcgenDescriptorSourceModel:
    """All table and layout facts consumed by the fixed descriptor algorithms."""

    words: tuple[WordModel, ...]
    swizzles: tuple[tuple[int, str], ...]
    patterns: tuple[tuple[int, int], ...]
    partitions: tuple[tuple[int, int], ...]
    kinds: tuple[KindModel, ...]
    layouts: tuple[LayoutModel, ...]
    absolute_target: str
    absolute_major: int
    absolute_minor: int
    special_m: int
    special_n: int


def build_tcgen_descriptor_header() -> TcgenDescriptorHeaderModel:
    """Select public identities without introducing a second catalogue."""

    return TcgenDescriptorHeaderModel(
        tuple(kind.name for kind in domain.KINDS),
        tuple(name for _, name in domain.SWIZZLES),
    )


def build_tcgen_descriptor_source() -> TcgenDescriptorSourceModel:
    """Precompute table references and map symbolic terms to public identities."""

    kinds = []
    for kind in domain.KINDS:
        arrays = tuple(
            KindArrayModel(suffix, cpp_type, element_type,
                           tuple(CodeModel(code, name) for code, name in rows))
            for suffix, cpp_type, element_type, rows in (
                ("A", "TypeCode", "MatrixElementType", kind.a_types),
                ("B", "TypeCode", "MatrixElementType", kind.b_types),
                ("D", "TypeCode", "MatrixElementType", kind.d_types),
                ("ScaleIds", "uint8_t", "", tuple((code, "") for code in kind.scale_ids)),
                ("ScaleTypes", "ScaleCode", "MatrixScaleType", kind.scale_types),
            )
        )
        kinds.append(KindModel(
            kind.name, next(i for i, word in enumerate(domain.WORDS)
                            if word.name == kind.table), arrays,
            kind.saturation, kind.negate, kind.transpose,
        ))
    names = {"one": "One", "T": "T", "m": "M", "k": "K", "LBO": "LBO", "SBO": "SBO"}

    def groups(items: tuple[tuple[domain.LayoutTerm, ...], ...]) -> tuple[tuple[domain.LayoutTerm, ...], ...]:
        """Resolve catalogue symbols before template traversal."""

        return tuple(tuple(domain.LayoutTerm(term.coefficient, names[term.symbol])
                           for term in terms) for terms in items)

    return TcgenDescriptorSourceModel(
        tuple(WordModel(word.fields, word.fixed_mask, word.reserved_mask,
                        word.unclassified) for word in domain.WORDS),
        domain.SWIZZLES, domain.ORDINARY_PATTERN_BYTES, domain.ZERO_PARTITIONS,
        tuple(kinds), tuple(LayoutModel(row.major, row.swizzle, groups(row.shape),
                                       groups(row.stride), row.swizzle_tuple)
                            for row in domain.RELATIVE_LAYOUTS),
        *domain.ABSOLUTE_TARGET, domain.SPECIAL_ATOM[2], domain.SPECIAL_ATOM[3],
    )
