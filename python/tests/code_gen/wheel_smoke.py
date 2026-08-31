import argparse
from configparser import ConfigParser
import os
from pathlib import Path
import subprocess
import tempfile
import venv
import zipfile


ROOT = Path(__file__).resolve().parents[3]
BACKEND_SPEC = ROOT / "submod/resolved_ir/test/package_consumer/backend.yaml"
CONFIG = ConfigParser()
CONFIG.read(ROOT / "python/setup.cfg")
EXPECTED_VERSION = CONFIG["metadata"]["version"]


def main(wheel: Path) -> None:
    wheel = wheel.resolve()
    if not wheel.is_file():
        raise FileNotFoundError(wheel)
    with zipfile.ZipFile(wheel) as archive:
        names = archive.namelist()
    for name in (
        "code_gen/cli.py",
        "code_gen/__main__.py",
        "code_gen/resources/ptx_spec/arithmetic.yaml",
        f"ptx_frontend-{EXPECTED_VERSION}.dist-info/METADATA",
    ):
        if name not in names:
            raise AssertionError(f"wheel is missing {name}")
    if any("ptx_cpp_backend_spec" in name for name in names):
        raise AssertionError("wheel contains a private backend specification")

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        environment = root / "venv"
        venv.create(environment, with_pip=True)
        bin_dir = environment / ("Scripts" if os.name == "nt" else "bin")
        executable = bin_dir / ("python.exe" if os.name == "nt" else "python")
        wheel_environment = os.environ.copy()
        wheel_environment.pop("PYTHONPATH", None)
        subprocess.run(
            [executable, "-m", "pip", "install", "--force-reinstall", wheel],
            check=True, cwd=root, env=wheel_environment,
        )
        installed_spec_dir = subprocess.check_output(
            [executable, "-c", f"from importlib.metadata import version; from importlib.resources import files; assert version('ptx_frontend') == {EXPECTED_VERSION!r}; print(files('code_gen.resources').joinpath('ptx_spec'))"],
            text=True, cwd=root, env=wheel_environment,
        ).strip()
        subprocess.run([executable, "-m", "code_gen", "--help"], check=True, cwd=root, env=wheel_environment)
        subprocess.run([bin_dir / "ptx-frontend-codegen", "--help"], check=True, cwd=root, env=wheel_environment)
        generated = root / "generated"
        command = [
            executable, "-m", "code_gen", "--spec-dir", installed_spec_dir,
            "--backend-spec", str(BACKEND_SPEC), "--output", str(generated),
        ]
        listed_outputs = subprocess.check_output(
            [*command, "--list-outputs"], text=True, cwd=root, env=wheel_environment,
        )
        for path in (
            generated / "private/resolved_ir_arithmetic.gen.cpp",
            generated / "public/resolved_ir.gen.hpp",
        ):
            if str(path) not in listed_outputs:
                raise AssertionError(f"--list-outputs is missing {path}")
        subprocess.run(command, check=True, cwd=root, env=wheel_environment)
        for path in (
            generated / "private/resolved_ir_arithmetic.gen.cpp",
            generated / "public/resolved_ir.gen.hpp",
        ):
            if not path.is_file():
                raise AssertionError(f"generation did not create {path}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Smoke-test an already-built ptx_frontend wheel.")
    parser.add_argument("wheel", type=Path)
    main(parser.parse_args().wheel)
