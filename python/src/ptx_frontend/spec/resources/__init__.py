"""Access to PTX specification resources packaged with the wheel."""

from importlib.resources import files, as_file
from importlib.resources.abc import Traversable


def packaged_spec_dir() -> Traversable:
    """Return the packaged PTX instruction-spec directory."""
    return files(__name__).joinpath("ptx_spec")


def packaged_backend_spec() -> Traversable:
    """Return the packaged C++ backend specification."""
    return files(__name__).joinpath("ptx_cpp_backend_spec/ptx_frontend.yaml")


def packaged_spec_schema() -> Traversable:
    """Return the packaged ``ptx-instr-v1`` schema."""
    return files(__name__).joinpath("ptx-instr-v1.schema.yaml")


def packaged_backend_spec_schema() -> Traversable:
    """Return the packaged ``ptx-cpp-backend-v1`` schema."""
    return files(__name__).joinpath("ptx-cpp-backend-v1.schema.yaml")


__all__ = [
    "packaged_backend_spec",
    "packaged_backend_spec_schema",
    "packaged_spec_dir",
    "packaged_spec_schema",
    "as_file",
]
