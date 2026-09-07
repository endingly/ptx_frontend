"""Access to reusable code-generation resources packaged with the wheel."""

from importlib.resources import files


def packaged_cpp_backend():
    """Return the packaged C++ backend specification."""

    return files(__package__).joinpath("ptx_cpp_backend_spec/ptx_frontend.yaml")


__all__ = ["packaged_cpp_backend"]
