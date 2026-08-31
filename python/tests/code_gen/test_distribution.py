import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import venv
import zipfile


ROOT = Path(__file__).resolve().parents[3]
PYTHON_PROJECT = ROOT / "python"
BACKEND_SPEC = ROOT / "submod/resolved_ir/test/package_consumer/backend.yaml"


class DistributionTests(unittest.TestCase):
    def test_wheel_exposes_codegen_without_private_backend(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            wheels = root / "wheels"
            subprocess.run(
                [sys.executable, "-m", "pip", "wheel", "--no-deps", "--wheel-dir", str(wheels), str(PYTHON_PROJECT)],
                check=True,
            )
            wheel = next(wheels.glob("*.whl"))
            with zipfile.ZipFile(wheel) as archive:
                names = archive.namelist()
            self.assertIn("code_gen/cli.py", names)
            self.assertIn("code_gen/__main__.py", names)
            self.assertIn("code_gen/resources/ptx_spec/arithmetic.yaml", names)
            self.assertFalse(any("ptx_cpp_backend_spec" in name for name in names))

            environment = root / "venv"
            venv.create(environment, with_pip=True)
            executable = environment / "bin/python"
            wheel_environment = os.environ.copy()
            wheel_environment.pop("PYTHONPATH", None)
            subprocess.run(
                [executable, "-m", "pip", "install", "--force-reinstall", wheel],
                check=True,
                cwd=root,
                env=wheel_environment,
            )
            installed_spec_dir = subprocess.check_output(
                [
                    executable,
                    "-c",
                    "from importlib.resources import files; print(files('code_gen.resources').joinpath('ptx_spec'))",
                ],
                text=True,
                cwd=root,
                env=wheel_environment,
            ).strip()
            subprocess.run(
                [executable, "-m", "code_gen", "--help"],
                check=True,
                cwd=root,
                env=wheel_environment,
            )
            subprocess.run(
                [environment / "bin/ptx-frontend-codegen", "--help"],
                check=True,
                cwd=root,
                env=wheel_environment,
            )
            generated = root / "generated"
            listed_outputs = subprocess.check_output(
                [
                    executable,
                    "-m",
                    "code_gen",
                    "--spec-dir",
                    installed_spec_dir,
                    "--backend-spec",
                    str(BACKEND_SPEC),
                    "--output",
                    str(generated),
                    "--list-outputs",
                ],
                text=True,
                cwd=root,
                env=wheel_environment,
            )
            self.assertIn(str(generated / "private/resolved_ir_arithmetic.gen.cpp"), listed_outputs)
            self.assertIn(str(generated / "public/resolved_ir.gen.hpp"), listed_outputs)
            subprocess.run(
                [
                    executable,
                    "-m",
                    "code_gen",
                    "--spec-dir",
                    installed_spec_dir,
                    "--backend-spec",
                    str(BACKEND_SPEC),
                    "--output",
                    str(generated),
                ],
                check=True,
                cwd=root,
                env=wheel_environment,
            )
            self.assertTrue((generated / "private/resolved_ir_arithmetic.gen.cpp").is_file())
            self.assertTrue((generated / "public/resolved_ir.gen.hpp").is_file())


if __name__ == "__main__":
    unittest.main()
