import argparse
from configparser import ConfigParser
import os
from pathlib import Path
import subprocess
import tempfile
import venv
import zipfile


ROOT = Path(__file__).resolve().parents[3]
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
        "ptx_frontend/base/utils.py",
        "ptx_frontend/code_gen/model.py",
        "ptx_frontend/code_gen/database.py",
        "ptx_frontend/code_gen/normalize.py",
        "ptx_frontend/code_gen/cpp_backend.py",
        "ptx_frontend/code_gen/resources/ptx_spec/arithmetic.yaml",
        "ptx_frontend/ir/resolved_ir.py",
        f"ptx_frontend-{EXPECTED_VERSION}.dist-info/METADATA",
    ):
        if name not in names:
            raise AssertionError(f"wheel is missing {name}")
    private_names = (
        "ptx_frontend/code_gen/__main__.py",
        "ptx_frontend/code_gen/cli.py",
        "ptx_frontend/code_gen/gen_resolved_checker_descriptor.py",
        "ptx_frontend/code_gen/gen_resolved_descriptor.py",
        "ptx_frontend/code_gen/gen_resolved_ir.py",
        "ptx_frontend/code_gen/gen_resolved_value_domains.py",
        "ptx_frontend/code_gen/gen_syntax_ast_arch.py",
        "ptx_frontend/code_gen/m12_natural_corpus.py",
    )
    for name in private_names:
        if name in names:
            raise AssertionError(f"wheel exports frontend-private module {name}")
    if any("/code_gen/_frontend/" in name for name in names):
        raise AssertionError("wheel contains the source-only frontend generator directory")
    if any("ptx_cpp_backend_spec" in name for name in names):
        raise AssertionError("wheel contains a private backend specification")
    if any(name.startswith(("base/", "code_gen/", "ir/")) for name in names):
        raise AssertionError("wheel contains an unqualified top-level package")

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
            check=True,
            cwd=root,
            env=wheel_environment,
        )
        smoke = f"""
from importlib.metadata import distribution, version
from importlib.resources import files
from importlib.util import find_spec
from pathlib import Path

from ptx_frontend.code_gen.database import load_codegen_database
from ptx_frontend.code_gen.model import InstructionSpec
import ptx_frontend.ir.resolved_ir

assert version('ptx_frontend') == {EXPECTED_VERSION!r}
assert not any(ep.name == 'ptx-frontend-codegen' for ep in distribution('ptx_frontend').entry_points)
for module in (
    'ptx_frontend.code_gen.__main__',
    'ptx_frontend.code_gen.cli',
    'ptx_frontend.code_gen.gen_resolved_ir',
    'ptx_frontend.code_gen.m12_natural_corpus',
):
    assert find_spec(module) is None, module
spec_dir = Path(str(files('ptx_frontend.code_gen.resources').joinpath('ptx_spec')))
database = load_codegen_database(spec_dir=spec_dir)
assert database.instructions
assert all(isinstance(item, InstructionSpec) for item in database.instructions)
assert any(item.opcode == 'add' for item in database.instructions)
"""
        subprocess.run(
            [executable, "-c", smoke],
            check=True,
            cwd=root,
            env=wheel_environment,
        )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Smoke-test an already-built ptx_frontend wheel."
    )
    parser.add_argument("wheel", type=Path)
    main(parser.parse_args().wheel)
