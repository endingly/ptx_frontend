from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import shutil
import stat
import tempfile
import unittest

from scripts.regenerate_m12_corpus import VERSION, json_bytes, main


ROOT = Path(__file__).resolve().parents[3]
M12_OUTPUTS = (
    "corpus/m12/common_kernel_sm80.ptx",
    "corpus/m12/common_kernel_sm90a.ptx",
    "corpus/m12/common_kernel_sm100.ptx",
    "corpus/m12/natural_kernel_sm80.ptx",
    "corpus/m12/natural_kernel_sm90a.ptx",
    "corpus/m12/natural_kernel_sm100.ptx",
    "corpus/m12/natural_manifest.json",
    "corpus/provenance.json",
)


def write_fake_nvcc(directory: Path, version: str = VERSION, wrong_target: bool = False) -> Path:
    path = directory / "nvcc"
    path.write_text(
        f"""#!/usr/bin/env python3
import pathlib
import sys

if sys.argv[1:] == ["--version"]:
    print({version!r})
    raise SystemExit(0)
target = next(argument.split("=", 1)[1] for argument in sys.argv if argument.startswith("-arch="))
if {wrong_target!r}:
    target = "sm_80"
source = pathlib.Path(sys.argv[sys.argv.index("-o") - 1])
output = pathlib.Path(sys.argv[sys.argv.index("-o") + 1])
output.parent.mkdir(parents=True, exist_ok=True)
body = ""
if source.name == "natural_kernel.cu":
    body = ".visible .entry natural_kernel()\\n{{\\n  ret;  \\n}}\\n"
output.write_text(".version 9.3\\n.target " + target + "\\n.address_size 64\\n\\n" + body + "\\n")
""",
        encoding="utf-8",
    )
    path.chmod(path.stat().st_mode | stat.S_IXUSR)
    return path


class RegenerateM12CorpusTests(unittest.TestCase):
    def make_root(self) -> tuple[tempfile.TemporaryDirectory[str], Path]:
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name)
        shutil.copytree(ROOT / "corpus", root / "corpus")
        shutil.copytree(ROOT / "instructions", root / "instructions")
        return temporary, root

    def test_write_then_check_is_clean_and_check_does_not_write(self) -> None:
        temporary, root = self.make_root()
        with temporary:
            nvcc = write_fake_nvcc(root)
            self.assertEqual(main(["--nvcc", str(nvcc)], root), 0)
            manifest = json.loads((root / "corpus/m12/natural_manifest.json").read_text())
            self.assertEqual(manifest["generator"], "nvcc V13.3.33")
            self.assertEqual(
                (root / "corpus/m12/natural_manifest.json").read_bytes(),
                json_bytes(manifest),
            )
            self.assertEqual(
                (root / "corpus/provenance.json").read_bytes(),
                json_bytes(
                    json.loads((root / "corpus/provenance.json").read_text())
                ),
            )
            before = {relative: (root / relative).read_bytes() for relative in M12_OUTPUTS}
            self.assertEqual(main(["--check", "--nvcc", str(nvcc)], root), 0)
            self.assertEqual(
                before,
                {relative: (root / relative).read_bytes() for relative in M12_OUTPUTS},
            )

    def test_check_reports_tampering_without_replacing_it(self) -> None:
        temporary, root = self.make_root()
        with temporary:
            nvcc = write_fake_nvcc(root)
            self.assertEqual(main(["--nvcc", str(nvcc)], root), 0)
            fixture = root / "corpus/m12/natural_kernel_sm80.ptx"
            expected_fixture = fixture.read_bytes()
            fixture.write_text("tampered\n", encoding="utf-8")
            with redirect_stdout(io.StringIO()) as output:
                self.assertEqual(main(["--check", "--nvcc", str(nvcc)], root), 1)
            self.assertIn("natural_kernel_sm80.ptx", output.getvalue())
            self.assertEqual(fixture.read_text(encoding="utf-8"), "tampered\n")
            fixture.write_bytes(expected_fixture)
            provenance = root / "corpus/provenance.json"
            expected_provenance = provenance.read_bytes()
            document = json.loads(provenance.read_text(encoding="utf-8"))
            next(
                record
                for record in document["fixtures"]
                if record["path"] == "corpus/m12/natural_kernel_sm80.ptx"
            )["sha256"] = "0" * 64
            provenance.write_text(json.dumps(document), encoding="utf-8")
            tampered_provenance = provenance.read_bytes()
            with redirect_stdout(io.StringIO()) as output:
                self.assertEqual(main(["--check", "--nvcc", str(nvcc)], root), 1)
            self.assertIn("corpus/provenance.json", output.getvalue())
            self.assertEqual(provenance.read_bytes(), tampered_provenance)
            provenance.write_bytes(expected_provenance)

            source = root / "corpus/m12/natural_kernel.cu"
            source.write_text(
                source.read_text(encoding="utf-8") + "// source tamper\n",
                encoding="utf-8",
            )
            tampered_source = source.read_bytes()
            with redirect_stdout(io.StringIO()) as output:
                self.assertEqual(main(["--check", "--nvcc", str(nvcc)], root), 1)
            self.assertIn("corpus/provenance.json", output.getvalue())
            self.assertEqual(source.read_bytes(), tampered_source)
            self.assertEqual(provenance.read_bytes(), expected_provenance)

    def test_rejects_wrong_nvcc_version(self) -> None:
        temporary, root = self.make_root()
        with temporary:
            self.assertEqual(
                main(
                    ["--check", "--nvcc", str(write_fake_nvcc(root, f"{VERSION} extra"))],
                    root,
                ),
                2,
            )

    def test_rejects_wrong_emitted_target(self) -> None:
        temporary, root = self.make_root()
        with temporary:
            self.assertEqual(
                main(
                    ["--check", "--nvcc", str(write_fake_nvcc(root, wrong_target=True))],
                    root,
                ),
                2,
            )


if __name__ == "__main__":
    unittest.main()
