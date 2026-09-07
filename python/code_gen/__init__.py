"""Frontend-internal code-generation implementation and compatibility namespace."""

from pathlib import Path

_private_dir = Path(__file__).resolve().parent / "_frontend"
if _private_dir.is_dir():
    __path__.append(str(_private_dir))
del _private_dir
