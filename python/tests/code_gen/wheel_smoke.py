import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import tomllib
import venv
import zipfile

ROOT = Path(__file__).resolve().parents[3]
INSTALLED_SMOKE = Path(__file__).with_name("wheel_installed_smoke.py")

with (ROOT / "python/pyproject.toml").open("rb") as file:
    EXPECTED_VERSION = tomllib.load(file)["project"]["version"]


def check_wheel_contents(wheel: Path) -> None:
    """Verify the wheel contains the intended src-layout package surface."""

    with zipfile.ZipFile(wheel) as archive:
        names = archive.namelist()

    required_files = (
        # Shared/public Python implementation.
        "ptx_frontend/base/utils.py",
        "ptx_frontend/spec/model.py",
        "ptx_frontend/spec/database.py",
        "ptx_frontend/spec/resources/__init__.py",
        "ptx_frontend/spec/normalize/__init__.py",
        "ptx_frontend/spec/load_yaml.py",
        "ptx_frontend/code_gen/model.py",
        "ptx_frontend/code_gen/database.py",
        "ptx_frontend/code_gen/normalize.py",
        "ptx_frontend/code_gen/load_yaml.py",
        "ptx_frontend/code_gen/cpp_backend.py",
        "ptx_frontend/code_gen/resolved_field_names.py",
        "ptx_frontend/ir/resolved_ir.py",
        "ptx_frontend/ir/syntax_ast.py",
        # Frontend generator implementation is intentionally packaged.
        "ptx_frontend/code_gen/_frontend/__main__.py",
        "ptx_frontend/code_gen/_frontend/cli.py",
        "ptx_frontend/code_gen/_frontend/gen_resolved_checker_descriptor.py",
        "ptx_frontend/code_gen/_frontend/gen_resolved_descriptor.py",
        "ptx_frontend/code_gen/_frontend/gen_resolved_ir.py",
        "ptx_frontend/code_gen/_frontend/gen_resolved_value_domains.py",
        "ptx_frontend/code_gen/_frontend/gen_syntax_ast_arch.py",
        "ptx_frontend/code_gen/_frontend/m12_natural_corpus.py",
        # Packaged helper scripts.
        "ptx_frontend/scripts/gen_all.py",
        "ptx_frontend/scripts/regenerate_m12_corpus.py",
        "ptx_frontend/scripts/validate_yaml.py",
        # Packaged schemas and specification resources.
        "ptx_frontend/spec/resources/ptx-instr-v1.schema.yaml",
        "ptx_frontend/spec/resources/ptx-cpp-backend-v1.schema.yaml",
        "ptx_frontend/spec/resources/" "ptx_cpp_backend_spec/ptx_frontend.yaml",
        "ptx_frontend/spec/resources/ptx_spec/arithmetic.yaml",
        # Distribution metadata.
        f"ptx_frontend-{EXPECTED_VERSION}.dist-info/METADATA",
    )

    for name in required_files:
        if name not in names:
            raise AssertionError(f"wheel is missing {name}")

    # These generators were relocated under code_gen._frontend.  Their old
    # flat module paths must not accidentally reappear.
    legacy_generator_files = (
        "ptx_frontend/code_gen/__main__.py",
        "ptx_frontend/code_gen/cli.py",
        "ptx_frontend/code_gen/gen_resolved_checker_descriptor.py",
        "ptx_frontend/code_gen/gen_resolved_descriptor.py",
        "ptx_frontend/code_gen/gen_resolved_ir.py",
        "ptx_frontend/code_gen/gen_resolved_value_domains.py",
        "ptx_frontend/code_gen/gen_syntax_ast_arch.py",
        "ptx_frontend/code_gen/m12_natural_corpus.py",
    )

    for name in legacy_generator_files:
        if name in names:
            raise AssertionError(
                f"wheel exports legacy flat frontend-generator module {name}"
            )

    # The wheel must expose only the fully-qualified ptx_frontend namespace.
    if any(name.startswith(("base/", "code_gen/", "ir/", "spec/")) for name in names):
        raise AssertionError("wheel contains an unqualified top-level Python package")


def run_installed_smoke(wheel: Path) -> None:
    """Install the wheel into a fresh venv and validate it in isolation."""

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        environment = root / "venv"

        venv.create(environment, with_pip=True)

        bin_dir = environment / ("Scripts" if os.name == "nt" else "bin")
        executable = bin_dir / ("python.exe" if os.name == "nt" else "python")

        wheel_environment = os.environ.copy()
        wheel_environment.pop("PYTHONPATH", None)
        wheel_environment["PTX_FRONTEND_EXPECTED_VERSION"] = EXPECTED_VERSION

        subprocess.run(
            [
                executable,
                "-m",
                "pip",
                "install",
                "--force-reinstall",
                str(wheel),
            ],
            check=True,
            cwd=root,
            env=wheel_environment,
        )

        subprocess.run(
            [
                executable,
                str(INSTALLED_SMOKE),
            ],
            check=True,
            cwd=root,
            env=wheel_environment,
        )


def main(wheel: Path) -> None:
    wheel = wheel.resolve()

    if not wheel.is_file():
        raise FileNotFoundError(wheel)

    if not INSTALLED_SMOKE.is_file():
        raise FileNotFoundError(INSTALLED_SMOKE)

    check_wheel_contents(wheel)
    run_installed_smoke(wheel)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Smoke-test an already-built ptx_frontend wheel."
    )
    parser.add_argument("wheel", type=Path)
    main(parser.parse_args().wheel)
