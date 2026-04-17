import shutil
import subprocess
import os
from typing import Optional


def find_clang_format() -> Optional[str]:
    for name in (
        "clang-format",
        "clang-format-15",
        "clang-format-14",
        "clang-format-13",
    ):
        p = shutil.which(name)
        if p:
            return p
    return None


def format_code(
    code: str,
    clang_bin: Optional[str] = None,
    style: str = "file",
    filename_hint: str = "file.cpp",
) -> str:
    """
    Format a code string with clang-format and return formatted string.
    - style: "file" (use .clang-format) or JSON-like string, e.g. "{BasedOnStyle: LLVM, IndentWidth: 2}"
    - filename_hint: passed to --assume-filename so clang-format detects language.
    """
    clang = clang_bin or find_clang_format()
    if not clang:
        raise RuntimeError("clang-format not found on PATH")
    args = [clang, f"--assume-filename={filename_hint}", f"-style={style}"]
    proc = subprocess.run(
        args, input=code.encode("utf-8"), stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"clang-format failed: {proc.stderr.decode('utf-8').strip()}"
        )
    return proc.stdout.decode("utf-8")


def format_file_inplace(
    path: str, clang_bin: Optional[str] = None, style: str = "file"
) -> None:
    """Format a file in-place (like `clang-format -i`)."""
    clang = clang_bin or find_clang_format()
    if not clang:
        raise RuntimeError("clang-format not found on PATH")
    subprocess.run([clang, f"-style={style}", "-i", path], check=True)
