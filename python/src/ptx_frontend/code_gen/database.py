"""Compatibility exports for the relocated PTX specification database."""

from ptx_frontend.spec.database import (
    CodegenDatabase,
    discover_spec_files,
    load_codegen_database,
    load_packaged_spec_database,
    load_spec_database,
)

__all__ = [
    "CodegenDatabase",
    "discover_spec_files",
    "load_codegen_database",
    "load_packaged_spec_database",
    "load_spec_database",
]
