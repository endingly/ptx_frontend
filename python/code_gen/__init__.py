"""Reusable PTX specification models with source-tree generator compatibility."""

from pathlib import Path

_private_dir = Path(__file__).resolve().parent / "_frontend"
if _private_dir.is_dir():
    __path__.append(str(_private_dir))
del _private_dir
