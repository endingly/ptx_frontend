#!/usr/bin/env python3
"""Compatibility wrapper for the public ``ptx_frontend.code_gen`` CLI."""

from pathlib import Path
import sys

PYTHON_ROOT = Path(__file__).resolve().parents[1]
if str(PYTHON_ROOT) not in sys.path:
    sys.path.insert(0, str(PYTHON_ROOT))

from ptx_frontend.code_gen.cli import main


if __name__ == "__main__":
    main()
