"""Public loaders for normalized PTX instruction specifications."""

from pathlib import Path

from ptx_frontend.code_gen.database import (
    CodegenDatabase as PtxSpecDatabase,
    discover_spec_files,
    load_codegen_database,
)

from .resources import packaged_spec_dir


def load_spec_database(*, spec_dir: Path) -> PtxSpecDatabase:
    """Load and normalize PTX instruction specs from ``spec_dir``."""

    return load_codegen_database(spec_dir=spec_dir)


def load_packaged_spec_database() -> PtxSpecDatabase:
    """Load the PTX instruction specs shipped with the installed wheel."""

    return load_spec_database(spec_dir=Path(str(packaged_spec_dir())))


__all__ = [
    "PtxSpecDatabase",
    "discover_spec_files",
    "load_packaged_spec_database",
    "load_spec_database",
]
