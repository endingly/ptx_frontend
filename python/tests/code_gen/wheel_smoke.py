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
        "ptx_frontend/spec/__init__.py",
        "ptx_frontend/spec/model.py",
        "ptx_frontend/spec/database.py",
        "ptx_frontend/spec/resources.py",
        "ptx_frontend/spec/normalize.py",
        "ptx_frontend/spec/load_yaml.py",
        "ptx_frontend/code_gen/model.py",
        "ptx_frontend/code_gen/database.py",
        "ptx_frontend/code_gen/normalize.py",
        "ptx_frontend/code_gen/cpp_backend.py",
        "ptx_frontend/code_gen/resources/ptx_cpp_backend_spec/ptx_frontend.yaml",
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
    if any(name.startswith(("base/", "code_gen/", "ir/", "spec/")) for name in names):
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
from importlib.util import find_spec

from ptx_frontend.spec import load_packaged_spec_database
from ptx_frontend.spec.model import InstructionSpec
from ptx_frontend.spec.resources import packaged_spec_schema
from ptx_frontend.code_gen.model import InstructionSpec as CompatibilityInstructionSpec
from ptx_frontend.code_gen.resources import packaged_cpp_backend

assert version('ptx_frontend') == {EXPECTED_VERSION!r}
assert InstructionSpec is CompatibilityInstructionSpec
assert packaged_spec_schema().is_file()
assert packaged_cpp_backend().is_file()
assert not any(ep.name == 'ptx-frontend-codegen' for ep in distribution('ptx_frontend').entry_points)
for module in (
    'ptx_frontend.code_gen.__main__',
    'ptx_frontend.code_gen.cli',
    'ptx_frontend.code_gen.gen_resolved_ir',
    'ptx_frontend.code_gen.m12_natural_corpus',
):
    assert find_spec(module) is None, module

database = load_packaged_spec_database()
assert database.instructions
assert all(isinstance(item, InstructionSpec) for item in database.instructions)
assert any(item.opcode == 'add' for item in database.instructions)
fma = next(item for item in database.instructions if item.opcode == 'fma')
assert tuple(variant.name for variant in fma.variants) == (
    'fma_rn_f32', 'fma_directed_f32', 'fma_rn_f64', 'fma_directed_f64',
    'fma_f32x2', 'fma_rn_f16', 'fma_rn_f16x2', 'fma_half_relu',
    'fma_half_oob', 'fma_half_oob_relu', 'fma_bf16', 'fma_bf16x2',
    'fma_bf16_oob', 'fma_bf16x2_oob', 'fma_mixed_f32_f16',
    'fma_mixed_f32_bf16',
)
layouts = {{variant.name: variant.operand_layouts[0].operands for variant in fma.variants}}
assert [operand.kind for operand in layouts['fma_rn_f32'][1:]] == ['reg_or_imm'] * 3
assert [operand.kind for operand in layouts['fma_f32x2']] == ['reg'] * 4
assert [operand.kind for operand in layouts['fma_bf16x2']] == ['reg'] * 4
for name in ('fma_mixed_f32_f16', 'fma_mixed_f32_bf16'):
    operands = layouts[name]
    assert [operand.kind for operand in operands] == ['reg', 'reg', 'reg', 'reg_or_imm']
    assert operands[0].type_expression.modifier_name == 'result_type'
    assert operands[3].type_expression.modifier_name == 'result_type'
assert layouts['fma_mixed_f32_f16'][1].type_expression.modifier_name == 'input_type'
assert layouts['fma_mixed_f32_bf16'][1].type_expression.scalar_type == 'b16'
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
