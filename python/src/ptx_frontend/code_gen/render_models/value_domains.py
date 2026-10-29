"""Typed static suffix lookup tables selected by the backend contract."""

from dataclasses import dataclass

from ptx_frontend.base.utils import file_stem_to_pascal_case, generated_at_comment
from ptx_frontend.code_gen.context import GenerationContext
from ptx_frontend.spec.model import DomainBackend, RuntimeLookupKind


@dataclass(frozen=True)
class PtxSuffixTableModel:
    """Ordered suffix/value mappings and the table's final C++ name/type."""

    name: str
    cpp_type: str
    entries: tuple[tuple[str, str], ...]


@dataclass(frozen=True)
class ValueDomainHeaderModel:
    """Private header containing selected runtime suffix tables."""

    generated_at: str
    tables: tuple[PtxSuffixTableModel, ...]


def build_ptx_suffix_table(name: str, domain: DomainBackend) -> PtxSuffixTableModel:
    """Preserve domain value order and established public type mapping."""

    return PtxSuffixTableModel(f"k{file_stem_to_pascal_case(name)}", domain.cpp_type, tuple(domain.values.items()))


def build_value_domain_header(context: GenerationContext) -> ValueDomainHeaderModel:
    """Select only domains explicitly requiring PTX suffix runtime lookup."""

    return ValueDomainHeaderModel(generated_at_comment(), tuple(build_ptx_suffix_table(name, domain)
        for name, domain in context.backend.domains.items() if domain.runtime_lookup is RuntimeLookupKind.PTX_SUFFIX))
