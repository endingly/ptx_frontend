import os
import subprocess
from pathlib import Path

VCPKG_ROOT = Path.home() / ".vcpkg"
VCPKG_URL = "git@github.com:microsoft/vcpkg.git"


def download_vcpkg(url: str):
    subprocess.run(
        f"git clone {VCPKG_URL} .vcpkg",
        shell=True,
        text=True,
        check=True,
        cwd=VCPKG_ROOT.parent,
    )


def install_vcpkg():
    if not VCPKG_ROOT.exists():
        download_vcpkg(VCPKG_URL)
    subprocess.run(
        f"""
        chmod +x {VCPKG_ROOT}/bootstrap-vcpkg.sh
        bash {VCPKG_ROOT}/bootstrap-vcpkg.sh
        """,
        check=True,
        text=True,
        shell=True,
        cwd=VCPKG_ROOT,
    )

    # if VCPKG_ROOT is not already in PATH, add it
    if str(VCPKG_ROOT) not in os.environ.get("PATH", ""):
        # export VCPKG_ROOT in .bashrc
        # export VCPKG_ROOT to PATH
        bashrc_path = Path.home() / ".bashrc"
        export_line = f'export VCPKG_ROOT="{VCPKG_ROOT}"\n'
        path_line = f'export PATH="$VCPKG_ROOT:$PATH"\n'
        with open(bashrc_path, "a") as f:
            f.write(f"\n# Added by install_vcpkg.py\n{export_line}{path_line}")

        subprocess.run(f"source {bashrc_path}", shell=True, check=True)


if __name__ == "__main__":
    install_vcpkg()
