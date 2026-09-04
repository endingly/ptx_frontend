"""Access to PTX specification resources packaged with the wheel."""

from importlib.resources import files

_RESOURCE_PACKAGE = "ptx_frontend.code_gen.resources"


def packaged_spec_dir():
    """Return the packaged PTX instruction-spec directory."""

    return files(_RESOURCE_PACKAGE).joinpath("ptx_spec")


def packaged_spec_schema():
    """Return the packaged ``ptx-instr-v1`` schema."""

    return files(_RESOURCE_PACKAGE).joinpath("ptx-instr-v1.schema.yaml")


__all__ = ["packaged_spec_dir", "packaged_spec_schema"]
