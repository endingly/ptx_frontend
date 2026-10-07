#!/usr/bin/env python3
"""Compatibility wrapper for the public ``ptx_frontend.code_gen`` CLI."""

from pathlib import Path
import sys

from ptx_frontend.code_gen.cli import main

if __name__ == "__main__":
    main()
